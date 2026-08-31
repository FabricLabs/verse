/*
 * world_generation_farm.c - FARM world generator
 *
 * Generates a flat world with layered terrain (bedrock, stone, soil),
 * grass patches, and scattered trees.
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
#include "world_bulk_ops.h"

// Generate farm world (flat terrain with soil and grass)
void world_generate_farm(World* world, const char* seed) {
    (void)seed; // Suppress unused parameter warning
    printf("[DEBUG] world_generate_farm called for world %p\n", (void*)world);

    // Clear the world to air first
    world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                     VOXEL_AIR, BULK_OP_REPLACE, NULL, NULL);

    // Create layered terrain using bulk operations
    VoxelType layer_types[] = {VOXEL_BEDROCK, VOXEL_STONE, VOXEL_STONE, VOXEL_SOIL, VOXEL_SOIL};
    float layer_heights[] = {0.0f, 0.3f, 0.45f, 0.5f, 0.5f};
    uint32_t layer_count = 5;

    // Convert percentage heights to absolute heights
    float height_f = (float)world->height;
    float layer_heights_abs[] = {
        0.0f,             // Bedrock at bottom
        0.3f * height_f,  // Stone layer 1
        0.45f * height_f, // Stone layer 2
        0.5f * height_f,  // Dirt layer
        0.5f * height_f   // Soil layer
    };

    world_fill_layered_terrain(world, 0, 0, 0, world->width, world->height, world->depth,
                              layer_types, layer_heights_abs, layer_count,
                              BULK_OP_REPLACE, NULL, NULL);

    // Add grass patches on the top layer using noise pattern
    uint32_t top_y = (uint32_t)(world->height * 0.5);
    VoxelType soil_type = VOXEL_SOIL;
    world_apply_noise_pattern(world, 0, top_y, 0, world->width, 1, world->depth,
                             VOXEL_GRASS, 0.1f, 0.7f, BULK_OP_MASKED,
                             voxel_filter_type, (void*)&soil_type);

    // Add trees identical to home world generator
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t x = 0; x < world->width; x++) {
            // Find the top block in this column
            int top_y_found = -1;
            for (uint32_t y = 0; y < world->height; y++) {
                Voxel* voxel = world_get_voxel(world, x, y, z);
                if (voxel && voxel->type != VOXEL_AIR) {
                    top_y_found = (int)y;
                }
            }

            // Check if top block is grass and place trees
            if (top_y_found >= 0) {
                Voxel* top_voxel = world_get_voxel(world, x, (uint32_t)top_y_found, z);
                if (top_voxel && top_voxel->type == VOXEL_GRASS) {
                    // Simple random tree placement
                    if (seeded_rand_range(100) < 3) {  // 3% chance
                        world_try_place_any_tree_at(world, x, (uint32_t)top_y_found, z);
                    }
                }
            }
        }
    }

    printf("[DEBUG] world_generate_farm complete\n");
}
