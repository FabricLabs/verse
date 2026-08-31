/*
 * world_generation_scoured.c - SCOURED world generator
 *
 * Creates a barren world with a bedrock floor and minimal features.
 * This is a simplified version for initial decomposition.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "world_internal.h"
#include "world_generation.h"
#include "world_voxel.h"
#include "voxel.h"

// Generate SCOURED world - a barren landscape with just a bedrock floor
void world_generate_scoured(World* world, const char* seed) {
    if (!world || !world->voxels)
        return;

    printf("[DEBUG] world_generate_scoured called for world %p, seed=%s\n",
           (void*)world, seed ? seed : "NULL");

    // Seed the random generator
    seed_rand_with_world_seed(seed);

    // Clear world to air
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                world_set_voxel(world, x, y, z, VOXEL_AIR);
            }
        }
    }

    // Apply bedrock floor at z=0
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
        }
    }

    // In the full implementation, this would include:
    // - Magma layer generation
    // - Strata layers with minerals
    // - Clay cap layers
    // - Spring water placement
    // - Surface scattering

    // For now, let's add a few scattered rocks on the surface
    int num_rocks = seeded_rand_range(5) + 3;
    for (int i = 0; i < num_rocks; i++) {
        uint32_t x = seeded_rand_range(world->width);
        uint32_t y = seeded_rand_range(world->height);
        world_set_voxel(world, x, y, 1, VOXEL_STONE);

        // Sometimes make it 2 blocks tall
        if (seeded_rand_range(100) < 30) {
            world_set_voxel(world, x, y, 2, VOXEL_STONE);
        }
    }

    // Add a few mineral deposits in the bedrock
    int num_minerals = seeded_rand_range(3) + 1;
    for (int i = 0; i < num_minerals; i++) {
        uint32_t x = seeded_rand_range(world->width);
        uint32_t y = seeded_rand_range(world->height);

        VoxelType mineral = VOXEL_IRON;
        int r = seeded_rand_range(100);
        if (r < 10) mineral = VOXEL_GOLD;
        else if (r < 20) mineral = VOXEL_SILVER;
        else if (r < 40) mineral = VOXEL_COPPER;

        world_set_voxel(world, x, y, 0, mineral);
    }
}
