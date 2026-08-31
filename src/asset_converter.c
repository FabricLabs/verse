// asset_converter.c
// Scans the models directory and converts supported files into .world snapshots.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "world.h"
#include "model_transformer.h"

static bool has_ext(const char* path, const char* ext) {
  size_t lp = strlen(path), le = strlen(ext);
  if (lp < le) return false;
  return strcasecmp(path + (lp - le), ext) == 0;
}

static void ensure_dir(const char* path) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
  system(cmd);
}

static void basename_no_ext(const char* path, char* out, size_t out_sz) {
  const char* slash = strrchr(path, '/');
  const char* name = slash ? slash + 1 : path;
  snprintf(out, out_sz, "%s", name);
  char* dot = strrchr(out, '.');
  if (dot) *dot = '\0';
}

typedef struct {
  bool only_vox;
  bool with_png;
} ConvertOptions;

static bool convert_vox(const char* in_path, const char* out_dir) {
  uint32_t sx=0, sy=0, sz=0;
  if (!model_transformer_probe_vox_dimensions(in_path, &sx,&sy,&sz)) {
    fprintf(stderr, "[vox] failed to probe dimensions: %s\n", in_path);
    return false;
  }
  if (sx == 0 || sy == 0 || sz == 0) {
    fprintf(stderr, "[vox] invalid dimensions in %s\n", in_path);
    return false;
  }
  World* w = world_create(sx, sy, sz);
  if (!w) return false;
  if (!model_transformer_load_vox(w, in_path, 0,0,0, VOXEL_STONE)) {
    fprintf(stderr, "[vox] load failed: %s\n", in_path);
    world_destroy(w);
    return false;
  }
  // Unique base for outputs
  char base[256]; basename_no_ext(in_path, base, sizeof(base));
  ensure_dir(out_dir);

  // Export a quick top-down snapshot image alongside
  char out_img[1024];
  snprintf(out_img, sizeof(out_img), "%s/%s.preview.bmp", out_dir, base);
  model_transformer_export_image(w, out_img, 0,0,(int)(sz-1), "topdown");

  // Save as .world via existing save API using a derived name
  char out_world[1024];
  snprintf(out_world, sizeof(out_world), "%s/%s.world", out_dir, base);
  if (!world_save(w, out_world)) {
    fprintf(stderr, "[vox] save failed: %s\n", out_world);
    world_destroy(w);
    return false;
  }
  world_destroy(w);
  return true;
}

static bool convert_bmp_plane(const char* in_path, const char* out_dir) {
  // Probe BMP header for width/height (reuse transformer reader indirectly by loading into world)
  // We'll create a world of size w x h x 2 and stamp the image at z=0.
  // Use a small temporary world to derive dimensions by loading into a dummy world sized generously.
  FILE* f = fopen(in_path, "rb");
  if (!f) return false;
  unsigned char hdr[54];
  if (fread(hdr, 1, 54, f) != 54) { fclose(f); return false; }
  if (hdr[0] != 'B' || hdr[1] != 'M') { fclose(f); return false; }
  int w = hdr[18] | (hdr[19]<<8) | (hdr[20]<<16) | (hdr[21]<<24);
  int h = hdr[22] | (hdr[23]<<8) | (hdr[24]<<16) | (hdr[25]<<24);
  if (w <= 0) w = 1; if (h == 0) h = 1; if (h < 0) h = -h; // handle top-down
  fclose(f);

  World* world = world_create((uint32_t)w, (uint32_t)h, 2);
  if (!world) return false;
  if (!model_transformer_load_image_plane(world, in_path, 0, true)) {
    fprintf(stderr, "[bmp] load failed: %s\n", in_path);
    world_destroy(world);
    return false;
  }
  char base[256]; basename_no_ext(in_path, base, sizeof(base));
  ensure_dir(out_dir);
  char out_img[1024];
  snprintf(out_img, sizeof(out_img), "%s/%s.preview.bmp", out_dir, base);
  model_transformer_export_image(world, out_img, 0,0,1, "topdown");

  char out_world[1024];
  snprintf(out_world, sizeof(out_world), "%s/%s.world", out_dir, base);
  if (!world_save(world, out_world)) {
    fprintf(stderr, "[bmp] save failed: %s\n", out_world);
    world_destroy(world);
    return false;
  }
  world_destroy(world);
  return true;
}

static void walk_and_convert(const char* root, const char* out_root, const ConvertOptions* opts) {
  DIR* d = opendir(root);
  if (!d) return;
  struct dirent* de;
  while ((de = readdir(d)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
    char in_path[1024];
    snprintf(in_path, sizeof(in_path), "%s/%s", root, de->d_name);
    struct stat st;
    if (stat(in_path, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      // mirror directory in out_root
      char new_out[1024];
      snprintf(new_out, sizeof(new_out), "%s/%s", out_root, de->d_name);
      walk_and_convert(in_path, new_out, opts);
    } else if (S_ISREG(st.st_mode)) {
      // skip temporary BMP files created by previous runs
      if (strstr(in_path, ".tmp.bmp") != NULL) {
        // best-effort cleanup
        remove(in_path);
        continue;
      }
      if (has_ext(in_path, ".vox")) {
        if (convert_vox(in_path, out_root)) {
          fprintf(stdout, "[vox] converted: %s\n", in_path);
        }
      } else if (!opts->only_vox && has_ext(in_path, ".bmp")) {
        if (convert_bmp_plane(in_path, out_root)) {
          fprintf(stdout, "[bmp] converted: %s\n", in_path);
        }
      } else if (!opts->only_vox && opts->with_png && has_ext(in_path, ".png")) {
        // Optional: try to convert PNG -> BMP with sips then process
        char tmp_bmp[1024];
        snprintf(tmp_bmp, sizeof(tmp_bmp), "%s.tmp.bmp", in_path);
        char cmd[1400];
        snprintf(cmd, sizeof(cmd), "sips -s format bmp '%s' --out '%s' >/dev/null 2>&1", in_path, tmp_bmp);
        int rc = system(cmd);
        if (rc == 0) {
          if (convert_bmp_plane(tmp_bmp, out_root)) {
            fprintf(stdout, "[png->bmp] converted: %s\n", in_path);
          }
          remove(tmp_bmp);
        }
      }
    }
  }
  closedir(d);
}

int main(int argc, char** argv) {
  const char* in_root = (argc > 1) ? argv[1] : "models";
  const char* out_root = (argc > 2) ? argv[2] : "converted";
  ConvertOptions opts = { .only_vox = false, .with_png = false };
  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "--only-vox") == 0) opts.only_vox = true;
    if (strcmp(argv[i], "--with-png") == 0) opts.with_png = true;
  }
  ensure_dir(out_root);
  walk_and_convert(in_root, out_root, &opts);
  printf("Conversion complete: %s -> %s\n", in_root, out_root);
  return 0;
}


