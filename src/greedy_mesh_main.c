#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "world.h"
#include "model_transformer.h"
#include "greedy_mesh.h"

static void basename_no_ext(const char* path, char* out, size_t out_sz) {
  const char* slash = strrchr(path, '/');
  const char* name = slash ? slash + 1 : path;
  snprintf(out, out_sz, "%s", name);
  char* dot = strrchr(out, '.');
  if (dot) *dot = '\0';
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: greedy-mesh <path/to/model.vox>\n");
    return 1;
  }
  const char* vox_path = argv[1];

  uint32_t sx=0, sy=0, sz=0;
  if (!model_transformer_probe_vox_dimensions(vox_path, &sx,&sy,&sz) || sx==0 || sy==0 || sz==0) {
    fprintf(stderr, "Failed to read SIZE from .vox: %s\n", vox_path);
    return 1;
  }
  printf("Input: %s  size: %ux%ux%u\n", vox_path, sx, sy, sz);

  World* world = world_create(sx, sy, sz);
  if (!world) return 1;
  if (!model_transformer_load_vox(world, vox_path, 0,0,0, VOXEL_STONE)) {
    fprintf(stderr, "Failed to load .vox data: %s\n", vox_path);
    world_destroy(world); return 1;
  }

  // Count voxels by type
  size_t total = 0; size_t per_type[VOXEL_COUNT]; memset(per_type, 0, sizeof(per_type));
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++) {
        const Voxel* v = world_get_voxel(world, x,y,z);
        if (v && v->type != VOXEL_AIR && v->type < VOXEL_COUNT) { total++; per_type[v->type]++; }
      }
  printf("Solid voxels: %zu\n", total);
  for (int t = 0; t < (int)VOXEL_COUNT; t++) {
    if (per_type[t] == 0) continue;
    printf("  %-16s: %zu\n", world_voxel_type_name((VoxelType)t), per_type[t]);
  }

  // Build output base path (next to input)
  char base_only[256]; basename_no_ext(vox_path, base_only, sizeof(base_only));
  char out_base[1024];
  const char* slash = strrchr(vox_path, '/');
  if (slash) snprintf(out_base, sizeof(out_base), "%.*s/%s", (int)(slash - vox_path), vox_path, base_only);
  else snprintf(out_base, sizeof(out_base), "%s", base_only);

  int model_count = 0; size_t total_quads = 0;
  if (!greedy_mesh_export_obj_per_voxel_type(world, out_base, &model_count, &total_quads)) {
    fprintf(stderr, "Greedy mesh export failed\n");
    world_destroy(world); return 1;
  }
  printf("Models generated: %d, total quads: %zu\n", model_count, total_quads);

  world_destroy(world);
  return 0;
}


