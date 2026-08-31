#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "world.h"

static void usage(const char* argv0) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s autocrop <file.world> [more.world ...]\n", argv0);
  fprintf(stderr, "  %s animation_experiment\n", argv0);
}

static void compute_non_air_bbox(const World* w,
                                 uint32_t* out_minx, uint32_t* out_miny, uint32_t* out_minz,
                                 uint32_t* out_maxx, uint32_t* out_maxy, uint32_t* out_maxz)
{
  uint32_t minx = w->width, miny = w->height, minz = w->depth;
  int maxx = -1, maxy = -1, maxz = -1;
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
      {
        const Voxel* v = world_get_voxel((World*)w, x, y, z);
        if (v && v->type != VOXEL_AIR)
        {
          if (x < minx) minx = x;
          if (y < miny) miny = y;
          if (z < minz) minz = z;
          if ((int)x > maxx) maxx = (int)x;
          if ((int)y > maxy) maxy = (int)y;
          if ((int)z > maxz) maxz = (int)z;
        }
      }
  *out_minx = (maxx >= 0) ? minx : 0;
  *out_miny = (maxy >= 0) ? miny : 0;
  *out_minz = (maxz >= 0) ? minz : 0;
  *out_maxx = (maxx >= 0) ? (uint32_t)maxx : 0;
  *out_maxy = (maxy >= 0) ? (uint32_t)maxy : 0;
  *out_maxz = (maxz >= 0) ? (uint32_t)maxz : 0;
}

static void compute_centroid_xy(const World* w, double* cx, double* cy)
{
  double sumx = 0.0, sumy = 0.0; size_t count = 0;
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
      {
        const Voxel* v = world_get_voxel((World*)w, x, y, z);
        if (v && v->type != VOXEL_AIR) { sumx += (double)x; sumy += (double)y; count++; }
      }
  if (count == 0) { *cx = (double)w->width * 0.5; *cy = (double)w->height * 0.5; return; }
  *cx = sumx / (double)count; *cy = sumy / (double)count;
}

static void find_hand_like_extremity_xy(const World* w, uint32_t* out_x, uint32_t* out_y, uint32_t* out_z)
{
  double cx = 0.0, cy = 0.0; compute_centroid_xy(w, &cx, &cy);
  double best_d2 = -1.0; uint32_t best_x = 0, best_y = 0, best_z = 0;
  uint32_t z_min = (uint32_t)((double)w->depth * 0.45); // search upper ~55%
  for (uint32_t z = z_min; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
      {
        const Voxel* v = world_get_voxel((World*)w, x, y, z);
        if (!v || v->type == VOXEL_AIR) continue;
        double dx = (double)x - cx, dy = (double)y - cy;
        double d2 = dx*dx + dy*dy;
        if (d2 > best_d2) { best_d2 = d2; best_x = x; best_y = y; best_z = z; }
      }
  *out_x = best_x; *out_y = best_y; *out_z = best_z;
}

static int cmd_animation_experiment(void)
{
  const char* skel_path = "models/vds_skeleton.world";
  const char* human_pref = "models/vds_human_base_colored.world";
  const char* human_alt  = "models/vds_human_base.world";
  const char* shield_path = "models/vds_shield.world";
  World *skel = world_create(1,1,1), *human = world_create(1,1,1), *shield = world_create(1,1,1);
  if (!skel || !human || !shield) { fprintf(stderr, "alloc failed\n"); return 2; }
  if (!world_load(skel, skel_path)) { fprintf(stderr, "load failed: %s\n", skel_path); return 2; }
  bool human_ok = world_load(human, human_pref);
  if (!human_ok) human_ok = world_load(human, human_alt);
  if (!human_ok) { fprintf(stderr, "load failed: %s and %s\n", human_pref, human_alt); return 2; }
  if (!world_load(shield, shield_path)) { fprintf(stderr, "load failed: %s\n", shield_path); return 2; }

  // Determine hand attachment from human
  uint32_t hand_x=0, hand_y=0, hand_z=0;
  find_hand_like_extremity_xy(human, &hand_x, &hand_y, &hand_z);

  // Compute shield bbox center
  uint32_t s_minx, s_miny, s_minz, s_maxx, s_maxy, s_maxz;
  compute_non_air_bbox(shield, &s_minx, &s_miny, &s_minz, &s_maxx, &s_maxy, &s_maxz);
  uint32_t s_w = (s_maxx >= s_minx) ? (s_maxx - s_minx + 1) : 0;
  uint32_t s_h = (s_maxy >= s_miny) ? (s_maxy - s_miny + 1) : 0;
  uint32_t s_d = (s_maxz >= s_minz) ? (s_maxz - s_minz + 1) : 0;
  uint32_t s_cx = s_minx + s_w/2;
  uint32_t s_cy = s_miny + s_h/2;
  uint32_t s_cz = s_minz + s_d/2;

  // Offset shield so its center aligns to hand; push 1 voxel outward in XY away from torso
  double cx=0.0, cy=0.0; compute_centroid_xy(human, &cx, &cy);
  int push_x = (hand_x > (uint32_t)cx) ? 1 : -1;
  int push_y = (hand_y > (uint32_t)cy) ? 1 : -1;
  int dx = (int)hand_x - (int)s_cx + push_x;
  int dy = (int)hand_y - (int)s_cy + push_y;
  int dz = (int)hand_z - (int)s_cz; // same height as hand
  if (dx < 0) dx = 0; if (dy < 0) dy = 0; if (dz < 0) dz = 0;

  // Determine output dimensions sufficient to hold all
  uint32_t out_w = skel->width; if (human->width > out_w) out_w = human->width; if ((uint32_t)(dx + (int)shield->width) > out_w) out_w = (uint32_t)(dx + (int)shield->width);
  uint32_t out_h = skel->height; if (human->height > out_h) out_h = human->height; if ((uint32_t)(dy + (int)shield->height) > out_h) out_h = (uint32_t)(dy + (int)shield->height);
  uint32_t out_d = skel->depth; if (human->depth > out_d) out_d = human->depth; if ((uint32_t)(dz + (int)shield->depth) > out_d) out_d = (uint32_t)(dz + (int)shield->depth);

  World* out = world_create(out_w, out_h, out_d);
  if (!out) { fprintf(stderr, "alloc failed for output world\n"); return 2; }
  strcpy(out->seed_id, "animation_experiment");

  // Merge order: skeleton -> human -> shield
  (void)world_copy_subvolume(skel, 0,0,0, out, 0,0,0, skel->width, skel->height, skel->depth);
  (void)world_copy_subvolume(human, 0,0,0, out, 0,0,0, human->width, human->height, human->depth);
  // Copy shield with offset; if exceeds bounds, clamp copy region
  if ((uint32_t)dx + shield->width <= out->width && (uint32_t)dy + shield->height <= out->height && (uint32_t)dz + shield->depth <= out->depth)
  {
    (void)world_copy_subvolume(shield, 0,0,0, out, (uint32_t)dx, (uint32_t)dy, (uint32_t)dz, shield->width, shield->height, shield->depth);
  }
  else
  {
    // Clamp copy region to fit
    uint32_t copy_w = shield->width;
    uint32_t copy_h = shield->height;
    uint32_t copy_d = shield->depth;
    if ((uint32_t)dx + copy_w > out->width) copy_w = out->width - (uint32_t)dx;
    if ((uint32_t)dy + copy_h > out->height) copy_h = out->height - (uint32_t)dy;
    if ((uint32_t)dz + copy_d > out->depth) copy_d = out->depth - (uint32_t)dz;
    if (copy_w > 0 && copy_h > 0 && copy_d > 0)
      (void)world_copy_subvolume(shield, 0,0,0, out, (uint32_t)dx, (uint32_t)dy, (uint32_t)dz, copy_w, copy_h, copy_d);
  }

  // Optional autocrop and save
  (void)world_autocrop(out);
  if (!world_save(out, "models/animation_experiment.world")) { fprintf(stderr, "save failed: models/animation_experiment.world\n"); return 2; }
  printf("Created models/animation_experiment.world (hand at %u,%u,%u; shield offset %d,%d,%d)\n", hand_x, hand_y, hand_z, dx, dy, dz);

  world_destroy(skel); world_destroy(human); world_destroy(shield); world_destroy(out);
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(argv[0]); return 1; }
  const char* cmd = argv[1];
  if (strcmp(cmd, "autocrop") == 0)
  {
    if (argc < 3) { usage(argv[0]); return 1; }
    int failures = 0;
    for (int i = 2; i < argc; i++) {
      const char* path = argv[i];
      World* w = world_create(1,1,1);
      if (!w) { fprintf(stderr, "alloc failed for %s\n", path); failures++; continue; }
      if (!world_load(w, path)) { fprintf(stderr, "load failed: %s\n", path); world_destroy(w); failures++; continue; }
      bool cropped = world_autocrop(w);
      if (!world_save(w, path)) { fprintf(stderr, "save failed: %s\n", path); world_destroy(w); failures++; continue; }
      printf("%s: %s\n", path, cropped ? "autocropped" : "no-change");
      world_destroy(w);
    }
    return failures ? 2 : 0;
  }
  else if (strcmp(cmd, "animation_experiment") == 0)
  {
    return cmd_animation_experiment();
  }
  else
  {
    usage(argv[0]);
    return 1;
  }
}


