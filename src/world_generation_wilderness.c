/*
 * world_generation_wilderness_simple.c - Simplified WILDERNESS world generator
 *
 * Creates a basic wilderness world without complex features to debug the bedrock issue.
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

// Generate WILDERNESS world - simplified version
void world_generate_wilderness(World* world, const char* seed) {
    if (!world || !world->voxels)
        return;

    printf("[DEBUG] world_generate_wilderness (simplified) called for world %p, seed=%s\n",
           (void*)world, seed ? seed : "NULL");

    // Seed the random generator
    seed_rand_with_world_seed(seed);

    // Clear entire world to air first
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                world_set_voxel(world, x, y, z, VOXEL_AIR);
            }
        }
    }

    // Set bedrock floor at z=0
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
        }
    }

    // Simple terrain - just add some stone layers
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            // Simple height: 3-5 blocks
            int height = 3 + seeded_rand_range(3);

            for (int z = 1; z <= height && z < world->depth; z++) {
                if (z == height) {
                    world_set_voxel(world, x, y, z, VOXEL_GRASS);
                } else if (z == height - 1) {
                    world_set_voxel(world, x, y, z, VOXEL_SOIL);
                } else {
                    world_set_voxel(world, x, y, z, VOXEL_STONE);
                }
            }
        }
    }

    printf("[DEBUG] Simplified wilderness generation complete\n");
}
