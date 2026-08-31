/*
 * world_generation_home.c - HOME world generator
 *
 * Generates a floating island in the sky with an exponential curve shape,
 * featuring trees in clustered distributions and a clear center area.
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

// Generate home world (island in the sky) - Built exclusively with exponential curve
void world_generate_home(World* world, const char* seed) {
    (void)seed; // Suppress unused parameter warning

    // Clear the world to air first
    world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                     VOXEL_AIR, BULK_OP_REPLACE, NULL, NULL);

    // Compute island parameters
    uint32_t center_x = world->width / 2;
    uint32_t center_y = world->height / 2;
    uint32_t center_z = world->depth / 2;

    // Calculate safe radius with 1-voxel border
    uint32_t max_radius = fmin(fmin(world->width, world->height), world->depth) / 2 - 1;
    uint32_t island_radius = fmin(max_radius, world->depth - 3);

    // Build the island from bottom to top using exponential curve exclusively
    // Start from the bottom and work our way up to create the semisphere
    for (uint32_t z = 0; z < center_z; z++) {
        // Calculate the radius at this Z level using an exponential curve
        // This creates the natural semisphere shape from bottom to top
        float z_ratio = (float)z / (float)center_z;
        float exponential_factor = z_ratio * z_ratio; // Exponential curve (z²)
        uint32_t radius_at_z = (uint32_t)(island_radius * exponential_factor);

        if (radius_at_z > 0) {
            // Fill a circular region at this Z level to form the semisphere
            for (uint32_t y = center_y - radius_at_z; y <= center_y + radius_at_z; y++) {
                for (uint32_t x = center_x - radius_at_z; x <= center_x + radius_at_z; x++) {
                    // Check if this position is within the circular radius at this Z level
                    int dx = (int)x - (int)center_x;
                    int dy = (int)y - (int)center_y;
                    int dist2 = dx * dx + dy * dy;

                    if (dist2 <= (int)(radius_at_z * radius_at_z)) {
                        // This position is within the circle at this Z level
                        Voxel* voxel = world_get_voxel(world, x, y, z);
                        if (voxel) {
                            voxel->type = VOXEL_STONE;
                        }
                    }
                }
            }
        }
    }

    // TODO: Add grass/dirt layers on top of stone
    // For now, just generate the stone shape without grass/dirt layers

    // Add trees in clustered distributions using noise patterns
    // Keep center area clear for combat
    uint32_t clear_radius = fmax(10, world->width / 4);

    // Create tree clusters using noise patterns
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            // Check if we're in the clear zone
            int dx_from_center = abs((int)x - (int)center_x);
            int dy_from_center = abs((int)y - (int)center_y);
            int dist2 = dx_from_center * dx_from_center + dy_from_center * dy_from_center;

            if (dist2 > (int)(clear_radius * clear_radius)) {
                // Find the top solid voxel at this position
                int top_z = -1;
                for (uint32_t z = 0; z < world->depth; z++) {
                    Voxel* voxel = world_get_voxel(world, x, y, z);
                    if (voxel && voxel->type != VOXEL_AIR) {
                        top_z = (int)z;
                    }
                }

                // Add trees only on grass surfaces (or stone for now)
                if (top_z >= 0) {
                    Voxel* top_voxel = world_get_voxel(world, x, y, (uint32_t)top_z);
                    // For now, place trees on stone since we don't have grass yet
                    if (top_voxel && (top_voxel->type == VOXEL_STONE || top_voxel->type == VOXEL_GRASS)) {
                        // Use noise-based clustering for tree placement
                        uint32_t hash = (uint32_t)(x * 73856093u ^ y * 19349663u ^ 12345u);
                        float noise = (float)(hash & 0xFFFF) / 65535.0f;

                        // Threshold for tree placement (creates clusters)
                        if (noise > 0.7f && seeded_rand_range(100) < 15) {
                            // Determine tree type based on position
                            uint32_t type_hash = (uint32_t)(x * 12345u ^ y * 67890u ^ 54321u);
                            float type_noise = (float)(type_hash & 0xFFFF) / 65535.0f;

                            if (type_noise < 0.33f)
                                world_try_plant_oak_Z(world, (int)x, (int)y, top_z);
                            else if (type_noise < 0.66f)
                                world_try_plant_birch_Z(world, (int)x, (int)y, top_z);
                            else
                                world_try_plant_pine_Z(world, (int)x, (int)y, top_z);
                        }
                    }
                }
            }
        }
    }
}
