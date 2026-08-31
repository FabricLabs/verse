/*
 * world_generation_wfc_town.c - WFC_TOWN world generator
 *
 * Uses Wave Function Collapse algorithm to generate a procedural town layout.
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
#include "wfc.h"
#include "voxel.h"

// Map WFC tile types to voxel layouts
static void place_tile_voxels(World* world, int tile_type, int x, int y) {
    // Each tile is 4x4 in the world
    const int TILE_SIZE = 4;
    int base_x = x * TILE_SIZE;
    int base_y = y * TILE_SIZE;

    // Base layer at z=0 is always bedrock
    for (int dy = 0; dy < TILE_SIZE; dy++) {
        for (int dx = 0; dx < TILE_SIZE; dx++) {
            world_set_voxel(world, base_x + dx, base_y + dy, 0, VOXEL_BEDROCK);
        }
    }

    // Place tile-specific voxels at z=1
    switch (tile_type) {
        case 0: // grass
            for (int dy = 0; dy < TILE_SIZE; dy++) {
                for (int dx = 0; dx < TILE_SIZE; dx++) {
                    world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_GRASS);
                }
            }
            break;

        case 1: // road_ns (north-south)
            for (int dy = 0; dy < TILE_SIZE; dy++) {
                for (int dx = 0; dx < TILE_SIZE; dx++) {
                    if (dx >= 1 && dx <= 2) {
                        world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_STONE);
                    } else {
                        world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_GRASS);
                    }
                }
            }
            break;

        case 2: // road_ew (east-west)
            for (int dy = 0; dy < TILE_SIZE; dy++) {
                for (int dx = 0; dx < TILE_SIZE; dx++) {
                    if (dy >= 1 && dy <= 2) {
                        world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_STONE);
                    } else {
                        world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_GRASS);
                    }
                }
            }
            break;

        case 3: // intersection
            for (int dy = 0; dy < TILE_SIZE; dy++) {
                for (int dx = 0; dx < TILE_SIZE; dx++) {
                    if ((dx >= 1 && dx <= 2) || (dy >= 1 && dy <= 2)) {
                        world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_STONE);
                    } else {
                        world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_GRASS);
                    }
                }
            }
            break;

        case 4: // lot (building plot)
            // Foundation
            for (int dy = 0; dy < TILE_SIZE; dy++) {
                for (int dx = 0; dx < TILE_SIZE; dx++) {
                    world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_CLAY);
                }
            }
            // Simple building (2x2 in center)
            for (int dz = 2; dz <= 4; dz++) {
                for (int dy = 1; dy <= 2; dy++) {
                    for (int dx = 1; dx <= 2; dx++) {
                        if (dz == 4 || dx == 1 || dx == 2 || dy == 1 || dy == 2) {
                            // Walls and roof
                            world_set_voxel(world, base_x + dx, base_y + dy, dz, VOXEL_BRICK);
                        }
                    }
                }
            }
            // Door (remove one wall block)
            world_set_voxel(world, base_x + 1, base_y + 0, 2, VOXEL_AIR);
            world_set_voxel(world, base_x + 1, base_y + 0, 3, VOXEL_AIR);
            break;

        case 5: // wall
            // Stone wall around the tile
            for (int dy = 0; dy < TILE_SIZE; dy++) {
                for (int dx = 0; dx < TILE_SIZE; dx++) {
                    world_set_voxel(world, base_x + dx, base_y + dy, 1, VOXEL_STONE);
                    // Add wall height
                    if (dx == 0 || dx == TILE_SIZE-1 || dy == 0 || dy == TILE_SIZE-1) {
                        for (int dz = 2; dz <= 3; dz++) {
                            world_set_voxel(world, base_x + dx, base_y + dy, dz, VOXEL_STONE);
                        }
                    }
                }
            }
            break;
    }

    // Add some decorations
    if (tile_type == 0) { // grass tile
        // Randomly place a tree
        if (seeded_rand_range(100) < 20) {
            int tree_x = base_x + 1 + seeded_rand_range(2);
            int tree_y = base_y + 1 + seeded_rand_range(2);
            // Simple tree
            for (int dz = 2; dz <= 4; dz++) {
                world_set_voxel(world, tree_x, tree_y, dz, VOXEL_WOOD);
            }
            // Leaves
            for (int dz = 4; dz <= 5; dz++) {
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (tree_x + dx >= 0 && tree_x + dx < world->width &&
                            tree_y + dy >= 0 && tree_y + dy < world->height) {
                            world_set_voxel(world, tree_x + dx, tree_y + dy, dz, VOXEL_LEAVES);
                        }
                    }
                }
            }
        }
    }
}

// Generate WFC_TOWN world - procedural town using Wave Function Collapse
void world_generate_wfc_town(World* world, const char* seed) {
    if (!world || !world->voxels)
        return;

    printf("[DEBUG] world_generate_wfc_town called for world %p, seed=%s\n",
           (void*)world, seed ? seed : "NULL");

    seed_rand_with_world_seed(seed);

    // Clear world to air first
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                world_set_voxel(world, x, y, z, VOXEL_AIR);
            }
        }
    }

        // Calculate grid size for WFC (each tile is 4x4 voxels)
    const int TILE_SIZE = 4;
    int grid_width = world->width / TILE_SIZE;
    int grid_height = world->height / TILE_SIZE;

    if (grid_width < 3 || grid_height < 3) {
        // World too small for WFC, just fill with grass
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
                world_set_voxel(world, x, y, 1, VOXEL_GRASS);
            }
        }
        printf("[DEBUG] World too small for WFC (%dx%d tiles), using grass fallback\n",
               grid_width, grid_height);
        return;
    }

    // Build WFC model
    WfcModel model;
    wfc_build_default_town_model(&model);

    // Allocate grid for WFC output
    int* grid = calloc(grid_width * grid_height, sizeof(int));
    if (!grid) {
        wfc_model_free(&model);
        return;
    }

    // Generate seed from world seed
    uint32_t wfc_seed = 0;
    if (seed) {
        for (const char* p = seed; *p; p++) {
            wfc_seed = wfc_seed * 31 + (uint32_t)*p;
        }
    }

    // Run WFC algorithm
    bool success = wfc_solve(&model, grid_width, grid_height, wfc_seed, 10, grid);

    if (success) {
        // Place tiles in the world
        for (int y = 0; y < grid_height; y++) {
            for (int x = 0; x < grid_width; x++) {
                int tile_type = grid[y * grid_width + x];
                place_tile_voxels(world, tile_type, x, y);
            }
        }

        // Add a central fountain or plaza
        int cx = grid_width / 2;
        int cy = grid_height / 2;
        int center_tile = grid[cy * grid_width + cx];

        // If center is grass or road, add a fountain
        if (center_tile == 0 || center_tile == 1 || center_tile == 2) {
            int fx = cx * TILE_SIZE + TILE_SIZE/2;
            int fy = cy * TILE_SIZE + TILE_SIZE/2;

            // Fountain base
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    if (abs(dx) + abs(dy) <= 2) {
                        if (fx + dx >= 0 && fx + dx < world->width &&
                            fy + dy >= 0 && fy + dy < world->height) {
                            world_set_voxel(world, fx + dx, fy + dy, 1, VOXEL_STONE);
                            world_set_voxel(world, fx + dx, fy + dy, 2, VOXEL_CLAY);
                        }
                    }
                }
            }

            // Water in center
            world_set_voxel(world, fx, fy, 2, VOXEL_WATER);
            world_set_voxel(world, fx, fy, 3, VOXEL_SPRING_WATER);
        }
    } else {
        // WFC failed, create a simple default town
        printf("[DEBUG] WFC failed, creating default town layout\n");

        // Create a simple grid pattern
        for (int y = 0; y < grid_height; y++) {
            for (int x = 0; x < grid_width; x++) {
                int tile_type = 0; // default to grass

                // Main roads every 3 tiles
                if (x % 3 == 1) tile_type = 1; // north-south road
                if (y % 3 == 1) tile_type = 2; // east-west road
                if (x % 3 == 1 && y % 3 == 1) tile_type = 3; // intersection

                // Some buildings
                if (x % 3 == 0 && y % 3 == 0 && x > 0 && y > 0) {
                    tile_type = 4; // lot with building
                }

                place_tile_voxels(world, tile_type, x, y);
            }
        }
    }

    // Cleanup
    free(grid);
    wfc_model_free(&model);

    printf("[DEBUG] WFC town generation complete\n");
}
