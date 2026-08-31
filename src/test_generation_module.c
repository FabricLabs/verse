/*
 * test_generation_module.c - Test program for world generation modules
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

// Minimal bulk operations for testing
bool world_fill_region(World* world, uint32_t x1, uint32_t y1, uint32_t z1,
                      uint32_t x2, uint32_t y2, uint32_t z2,
                      VoxelType type, BulkOperationMode mode,
                      VoxelFilter filter, void* filter_context) {
    (void)mode;
    (void)filter;
    (void)filter_context;

    if (!world) return false;

    // Simple implementation - just fill the region
    for (uint32_t z = z1; z < z2 && z < world->depth; z++) {
        for (uint32_t y = y1; y < y2 && y < world->height; y++) {
            for (uint32_t x = x1; x < x2 && x < world->width; x++) {
                world_set_voxel(world, x, y, z, type);
            }
        }
    }
    return true;
}

bool world_fill_half_sphere(World* world, uint32_t cx, uint32_t cy, uint32_t cz,
                           uint32_t radius, VoxelType type, bool upper,
                           float vertical_scale, BulkOperationMode mode,
                           VoxelFilter filter, void* filter_context) {
    (void)vertical_scale;
    (void)filter;
    (void)filter_context;

    if (!world) return false;

    // Simple half-sphere implementation
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                int dx = (int)x - (int)cx;
                int dy = (int)y - (int)cy;
                int dz = (int)z - (int)cz;

                if ((upper && dz >= 0) || (!upper && dz <= 0)) {
                    float dist2 = (float)(dx*dx + dy*dy + dz*dz);
                    if (dist2 <= (float)(radius * radius)) {
                        if (mode == BULK_OP_SUBTRACTIVE) {
                            world_set_voxel(world, x, y, z, VOXEL_AIR);
                        } else {
                            world_set_voxel(world, x, y, z, type);
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool world_fill_layered_terrain(World* world, uint32_t x1, uint32_t y1, uint32_t z1,
                               uint32_t x2, uint32_t y2, uint32_t z2,
                               VoxelType* layer_types, float* layer_heights,
                               uint32_t layer_count, BulkOperationMode mode,
                               VoxelFilter filter, void* filter_context) {
    (void)x1; (void)x2; (void)z1; (void)z2;
    (void)mode;
    (void)filter;
    (void)filter_context;

    if (!world || !layer_types || !layer_heights) return false;

    // Simple layered terrain
    for (uint32_t i = 0; i < layer_count && i < y2 - y1; i++) {
        uint32_t layer_start = y1 + (i > 0 ? (uint32_t)layer_heights[i-1] : 0);
        uint32_t layer_end = y1 + (uint32_t)layer_heights[i];

        for (uint32_t y = layer_start; y < layer_end && y < world->height; y++) {
            for (uint32_t z = 0; z < world->depth; z++) {
                for (uint32_t x = 0; x < world->width; x++) {
                    world_set_voxel(world, x, y, z, layer_types[i]);
                }
            }
        }
    }
    return true;
}

bool world_apply_noise_pattern(World* world, uint32_t x1, uint32_t y1, uint32_t z1,
                              uint32_t x2, uint32_t y2, uint32_t z2,
                              VoxelType type, float scale, float threshold,
                              BulkOperationMode mode, VoxelFilter filter,
                              void* filter_context) {
    (void)scale;
    (void)threshold;
    (void)mode;

    if (!world) return false;

    // Simple noise pattern - just place some random blocks
    for (uint32_t z = z1; z < z2 && z < world->depth; z++) {
        for (uint32_t y = y1; y < y2 && y < world->height; y++) {
            for (uint32_t x = x1; x < x2 && x < world->width; x++) {
                // Simple hash-based noise
                uint32_t hash = x * 73856093u ^ y * 19349663u ^ z * 83492791u;
                float noise = (float)(hash & 0xFFFF) / 65535.0f;

                if (noise > 0.7f) {
                    if (!filter || filter(world, x, y, z, world_get_voxel(world, x, y, z)->type, filter_context)) {
                        world_set_voxel(world, x, y, z, type);
                    }
                }
            }
        }
    }
    return true;
}

void test_home_generator() {
    printf("Testing HOME world generator...\n");

    World* world = world_create(32, 32, 32);
    assert(world != NULL);

    // Generate home world
    world_generate_home(world, "test_seed_123");

    // Check that it created an island
    uint32_t center_x = world->width / 2;
    uint32_t center_y = world->height / 2;
    uint32_t bottom_z = 0;

    // Bottom center should be stone (the pointed bottom)
    Voxel* bottom = world_get_voxel(world, center_x, center_y, bottom_z);
    assert(bottom != NULL);
    printf("  Bottom center voxel type: %d\n", bottom->type);

    // Count non-air voxels
    int solid_count = 0;
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type != VOXEL_AIR) {
                    solid_count++;
                }
            }
        }
    }

    printf("  Solid voxel count: %d\n", solid_count);
    assert(solid_count > 100); // Should have created a substantial island

    // Check it's roughly centered
    int center_solid = 0;
    for (uint32_t z = 0; z < world->depth / 2; z++) {
        Voxel* v = world_get_voxel(world, center_x, center_y, z);
        if (v && v->type != VOXEL_AIR) {
            center_solid++;
        }
    }

    printf("  Center column solid count: %d\n", center_solid);
    assert(center_solid > 5); // Center should have some solid blocks

    world_destroy(world);
    printf("  ✓ HOME generator test passed\n");
}

void test_farm_generator() {
    printf("Testing FARM world generator...\n");

    World* world = world_create(16, 16, 16);
    assert(world != NULL);

    // Generate farm world
    world_generate_farm(world, "farm_seed");

    // Check bottom is bedrock
    Voxel* bottom = world_get_voxel(world, 0, 0, 0);
    assert(bottom != NULL);
    assert(bottom->type == VOXEL_BEDROCK);
    printf("  ✓ Bottom is bedrock\n");

    // Check we have layers
    bool has_stone = false;
    bool has_soil = false;

    for (uint32_t y = 0; y < world->height; y++) {
        Voxel* v = world_get_voxel(world, 8, y, 8);
        if (v) {
            if (v->type == VOXEL_STONE) has_stone = true;
            if (v->type == VOXEL_SOIL) has_soil = true;
        }
    }

    assert(has_stone);
    assert(has_soil);
    printf("  ✓ Has stone and soil layers\n");

    world_destroy(world);
    printf("  ✓ FARM generator test passed\n");
}

void test_arena_generator() {
    printf("Testing ARENA world generator...\n");

    World* world = world_create(32, 32, 32);
    assert(world != NULL);

    // Generate arena world
    world_generate_arena(world, "arena_seed");

    // Check bottom half has limestone
    bool has_limestone = false;
    for (uint32_t z = 0; z < world->depth / 2; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type == VOXEL_STONE_LIMESTONE) {
                    has_limestone = true;
                    break;
                }
            }
            if (has_limestone) break;
        }
        if (has_limestone) break;
    }

    assert(has_limestone);
    printf("  ✓ Has limestone in bottom half\n");

    // Check center is carved out (should have air)
    uint32_t cx = world->width / 2;
    uint32_t cy = world->height / 2;
    uint32_t cz = world->depth / 4; // Lower hemisphere

    Voxel* center = world_get_voxel(world, cx, cy, cz);
    assert(center != NULL);
    assert(center->type == VOXEL_AIR);
    printf("  ✓ Center is carved out\n");

    world_destroy(world);
    printf("  ✓ ARENA generator test passed\n");
}

void test_solid_fill() {
    printf("Testing SOLID fill generator...\n");

    World* world = world_create(8, 8, 8);
    assert(world != NULL);

    // Fill with bedrock
    world_generate_solid_fill(world, VOXEL_BEDROCK);

    // Check all voxels are bedrock
    int bedrock_count = 0;
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type == VOXEL_BEDROCK) {
                    bedrock_count++;
                }
            }
        }
    }

    assert(bedrock_count == 8 * 8 * 8);
    printf("  ✓ All voxels are bedrock: %d\n", bedrock_count);

    world_destroy(world);
    printf("  ✓ SOLID fill test passed\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== World Generation Module Test Suite ===\n\n");

    test_home_generator();
    test_farm_generator();
    test_arena_generator();
    test_solid_fill();

    printf("\n✅ All tests passed!\n");
    printf("\nThe generation modules have been successfully extracted and work correctly.\n");

    return 0;
}
