// schematic_importer.c
// Imports a Minecraft .schematic from models/modern-split into a World and saves it to worlds/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdbool.h>

#include "world.h"
#include "model_transformer.h"

static bool file_exists(const char* path) {
  struct stat st; return stat(path, &st) == 0 && (st.st_mode & S_IFREG);
}

static void ensure_dir(const char* path) {
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
  (void)system(cmd);
}

static bool has_ext(const char* name, const char* ext) {
  size_t ln = strlen(name), le = strlen(ext);
  if (ln < le) return false;
  return strcasecmp(name + (ln - le), ext) == 0;
}

static bool find_schematic_recursive(const char* root, char* out_path, size_t out_sz) {
  DIR* d = opendir(root);
  if (!d) return false;
  struct dirent* de;
  while ((de = readdir(d)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
    char child[1024];
    snprintf(child, sizeof(child), "%s/%s", root, de->d_name);
    struct stat st; if (stat(child, &st) != 0) continue;
    if (S_ISREG(st.st_mode)) {
      if (has_ext(child, ".schematic") || has_ext(child, ".schematic.gz")) {
        snprintf(out_path, out_sz, "%s", child);
        closedir(d);
        return true;
      }
    } else if (S_ISDIR(st.st_mode)) {
      if (find_schematic_recursive(child, out_path, out_sz)) { closedir(d); return true; }
    }
  }
  closedir(d);
  return false;
}

int main(int argc, char** argv) {
  const char* models_dir = (argc > 1) ? argv[1] : "models/modern-split";
  const char* worlds_dir = (argc > 2) ? argv[2] : "worlds";
  const char* base_name = (argc > 3) ? argv[3] : "modern-split";

  // Find a schematic file by scanning recursively
  char found[1024] = {0};
  if (!find_schematic_recursive(models_dir, found, sizeof(found))) {
    fprintf(stderr, "No .schematic found under %s (recursively). Expected a .schematic or .schematic.gz file.\n", models_dir);
    return 1;
  }

  uint16_t w=0,h=0,l=0;
  if (!model_transformer_probe_schematic_dimensions(found, &w,&h,&l)) {
    fprintf(stderr, "Failed to probe schematic dimensions: %s\n", found);
    return 1;
  }

  World* world = world_create((uint32_t)w, (uint32_t)l, (uint32_t)h); // our axes: X=w, Y=l, Z=h
  if (!world) { fprintf(stderr, "Allocation failed for world %ux%ux%u\n", (unsigned)w, (unsigned)l, (unsigned)h); return 1; }

  if (!model_transformer_import_minecraft_schematic(world, found, 0, 0, 0)) {
    fprintf(stderr, "Import failed: %s\n", found);
    world_destroy(world);
    return 1;
  }

  ensure_dir(worlds_dir);
  char out_path[1024]; snprintf(out_path, sizeof(out_path), "%s/%s.world", worlds_dir, base_name);
  if (!world_save(world, out_path)) {
    fprintf(stderr, "Save failed: %s\n", out_path);
    world_destroy(world);
    return 1;
  }
  printf("Saved world to %s (%ux%ux%u)\n", out_path, (unsigned)w, (unsigned)l, (unsigned)h);
  world_destroy(world);
  return 0;
}
