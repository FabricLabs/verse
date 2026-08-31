#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/world.h"

static void make_seed(char* buf, size_t n, int idx) {
  snprintf(buf, n, "spring_test_seed_%d", idx);
}

int main(void) {
  int found_worlds = 0;
  int attempts = 0;
  int worlds_checked = 0;
  const int target = 10;

  while (found_worlds < target && attempts < 100000) {
    attempts++;
    char seed[128];
    make_seed(seed, sizeof(seed), attempts);

    World* w = world_create(32, 32, 32);
    if (!w) { fprintf(stderr, "alloc failed\n"); return 1; }
    // Boost base level so RANDOM generation will place springs deterministically more often
    world_set_base_level(w, 256);
    world_generate_with_type(w, seed, WORLD_TYPE_RANDOM);
    worlds_checked++;

    int springs = 0;
    // Bedrock layer at y=0; scan x,z
    for (uint32_t z = 0; z < w->depth; z++) {
      for (uint32_t x = 0; x < w->width; x++) {
        uint32_t y = 0;
        Voxel* v = world_get_voxel(w, x, y, z);
        if (v && v->type == VOXEL_SPRING) {
          springs++;
          // Remove voxel above to allow water generation
          if (world_is_position_valid(w, x, y + 1, z)) {
            world_set_voxel(w, x, y + 1, z, VOXEL_AIR);
          }
        }
      }
    }

    if (springs > 0) {
      found_worlds++;
      printf("World #%d seed=%s has %d spring(s)\n", found_worlds, seed, springs);
      // Simulate time to trigger water generation (function expects microseconds)
      // Two passes to fill spring then push upward
      world_update_springs(w, 0);
      world_update_springs(w, 6000000ULL); // > 5s

      // Count water produced at bedrock and just above
      int water_bedrock = 0, water_above = 0;
      for (uint32_t z = 0; z < w->depth; z++) {
        for (uint32_t x = 0; x < w->width; x++) {
          uint32_t y = 0;
          Voxel* vb = world_get_voxel(w, x, y, z);
          if (vb && vb->type == VOXEL_WATER) water_bedrock++;
          if (world_is_position_valid(w, x, y + 1, z)) {
            Voxel* va = world_get_voxel(w, x, y + 1, z);
            if (va && va->type == VOXEL_WATER) water_above++;
          }
        }
      }
      printf("  After update: water at bedrock=%d, water above=%d\n", water_bedrock, water_above);
    }

    world_destroy(w);
  }

  printf("Checked %d worlds, found %d with springs.\n", worlds_checked, found_worlds);
  return (found_worlds >= target) ? 0 : 2;
}


