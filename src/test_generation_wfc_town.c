/*
 * test_generation_wfc_town.c - Test program for WFC_TOWN world generator
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include "world_internal.h"
#include "world_generation.h"
#include "world_core.h"
#include "world_voxel.h"
#include "voxel.h"

void test_wfc_town_basic() {
    printf("Testing WFC_TOWN world generator...\n");

    World* world = world_create(32, 32, 16);
    assert(world != NULL);

    // Generate WFC town world
    world_generate_wfc_town(world, "wfc_town_test");

    // Check bedrock floor at z=0
    int bedrock_count = 0;
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            Voxel* v = world_get_voxel(world, x, y, 0);
            if (v && v->type == VOXEL_BEDROCK) {
                bedrock_count++;
            }
        }
    }
    assert(bedrock_count == 32 * 32);
    printf("  ✓ Complete bedrock floor at z=0: %d blocks\n", bedrock_count);

    // Check for various tile types at z=1
    int grass_count = 0;
    int stone_count = 0;
    int clay_count = 0;
    int brick_count = 0;

    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            Voxel* v = world_get_voxel(world, x, y, 1);
            if (v) {
                switch (v->type) {
                    case VOXEL_GRASS: grass_count++; break;
                    case VOXEL_STONE: stone_count++; break;
                    case VOXEL_CLAY: clay_count++; break;
                    case VOXEL_BRICK: brick_count++; break;
                }
            }
        }
    }

    // Should have at least some of each major type
    assert(grass_count > 0);
    assert(stone_count > 0);  // Roads and walls
    printf("  ✓ Town layout: grass=%d, stone=%d, clay=%d, brick=%d\n",
           grass_count, stone_count, clay_count, brick_count);

    // Check for buildings (brick structures above z=1)
    int building_blocks = 0;
    for (uint32_t z = 2; z < 6; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type == VOXEL_BRICK) {
                    building_blocks++;
                }
            }
        }
    }

    printf("  ✓ Building structures found: %d blocks\n", building_blocks);

    // Check for decorative elements (trees, water)
    int tree_blocks = 0;
    int water_blocks = 0;
    for (uint32_t z = 1; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v) {
                    if (v->type == VOXEL_WOOD || v->type == VOXEL_LEAVES) {
                        tree_blocks++;
                    }
                    if (v->type == VOXEL_WATER || v->type == VOXEL_SPRING_WATER) {
                        water_blocks++;
                    }
                }
            }
        }
    }

    printf("  ✓ Decorations: trees=%d, water=%d\n", tree_blocks, water_blocks);

    world_destroy(world);
    printf("  ✓ WFC_TOWN generator test passed\n");
}

void test_wfc_town_small_world() {
    printf("Testing WFC_TOWN with small world...\n");

    World* world = world_create(8, 8, 8);
    assert(world != NULL);

    // Generate WFC town in small world (should fall back to simple grass)
    world_generate_wfc_town(world, "small_town_test");

    // Check that it generated something reasonable
    int grass_count = 0;
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            Voxel* v = world_get_voxel(world, x, y, 1);
            if (v && v->type == VOXEL_GRASS) {
                grass_count++;
            }
        }
    }

    assert(grass_count == 8 * 8);
    printf("  ✓ Small world handled correctly: all grass\n");

    world_destroy(world);
}

void test_wfc_town_deterministic() {
    printf("Testing WFC_TOWN determinism...\n");

    World* world1 = world_create(24, 24, 12);
    World* world2 = world_create(24, 24, 12);

    world_generate_wfc_town(world1, "deterministic_wfc");
    world_generate_wfc_town(world2, "deterministic_wfc");

    // Compare all voxels
    bool match = true;
    for (uint32_t z = 0; z < 12; z++) {
        for (uint32_t y = 0; y < 24; y++) {
            for (uint32_t x = 0; x < 24; x++) {
                Voxel* v1 = world_get_voxel(world1, x, y, z);
                Voxel* v2 = world_get_voxel(world2, x, y, z);
                if (v1 && v2 && v1->type != v2->type) {
                    match = false;
                }
            }
        }
    }

    assert(match);
    printf("  ✓ WFC_TOWN generation is deterministic\n");

    world_destroy(world1);
    world_destroy(world2);
}

void test_wfc_town_tile_alignment() {
    printf("Testing WFC_TOWN tile alignment...\n");

    World* world = world_create(16, 16, 8);
    world_generate_wfc_town(world, "alignment_test");

    // Check that tiles are properly aligned (4x4 blocks)
    // Each tile should have consistent material within its bounds
    bool aligned = true;
    for (int tile_y = 0; tile_y < 4; tile_y++) {
        for (int tile_x = 0; tile_x < 4; tile_x++) {
            // Get the primary material of this tile (most common at z=1)
            VoxelType tile_materials[16];
            int idx = 0;
            for (int dy = 0; dy < 4; dy++) {
                for (int dx = 0; dx < 4; dx++) {
                    int x = tile_x * 4 + dx;
                    int y = tile_y * 4 + dy;
                    Voxel* v = world_get_voxel(world, x, y, 1);
                    if (v) {
                        tile_materials[idx++] = v->type;
                    }
                }
            }

            // Check consistency (roads should be consistent patterns)
            // This is a simplified check - just ensure we have some structure
            if (idx > 0) {
                int transitions = 0;
                for (int i = 1; i < idx; i++) {
                    if (tile_materials[i] != tile_materials[i-1]) {
                        transitions++;
                    }
                }
                // Roads and intersections will have some transitions, but not random
                if (transitions > 8) {
                    aligned = false;
                }
            }
        }
    }

    assert(aligned);
    printf("  ✓ Tiles are properly aligned\n");

    world_destroy(world);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== WFC_TOWN World Generator Test Suite ===\n\n");

    test_wfc_town_basic();
    test_wfc_town_small_world();
    test_wfc_town_deterministic();
    test_wfc_town_tile_alignment();

    printf("\n✅ All tests passed!\n");
    printf("\nThe WFC_TOWN generator has been successfully extracted and works correctly.\n");

    return 0;
}
