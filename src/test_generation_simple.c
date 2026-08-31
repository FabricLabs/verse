/*
 * test_generation_simple.c - Simple test for world generation modules
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

void test_home_generator() {
    printf("Testing HOME world generator...\n");

    World* world = world_create(32, 32, 32);
    assert(world != NULL);

    // Set generation type
    world->generation_type = WORLD_TYPE_HOME;

    // Generate home world
    world_generate_home(world, "test_seed_123");

    // Check that it created an island
    uint32_t center_x = world->width / 2;
    uint32_t center_y = world->height / 2;

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
    assert(center_solid > 0); // Center should have some solid blocks

    world_destroy(world);
    printf("  ✓ HOME generator test passed\n");
}

void test_farm_generator() {
    printf("Testing FARM world generator...\n");

    World* world = world_create(16, 16, 16);
    assert(world != NULL);

    // Set generation type
    world->generation_type = WORLD_TYPE_FARM;

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

    // Set generation type
    world->generation_type = WORLD_TYPE_ARENA;

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

void test_tree_functions() {
    printf("Testing tree planting functions...\n");

    World* world = world_create(16, 16, 16);
    assert(world != NULL);

    // Place ground
    world_set_voxel(world, 8, 8, 4, VOXEL_STONE);

    // Plant trees
    assert(world_try_plant_oak_Z(world, 8, 8, 4));
    assert(world_try_plant_birch_Y(world, 4, 4, 8));
    assert(world_try_plant_pine_Z(world, 12, 12, 4));

    // Check oak trunk
    Voxel* oak = world_get_voxel(world, 8, 8, 5);
    assert(oak && oak->type == VOXEL_WOOD);
    printf("  ✓ Oak tree planted\n");

    // Check birch trunk
    Voxel* birch = world_get_voxel(world, 4, 5, 8);
    assert(birch && birch->type == VOXEL_WOOD);
    printf("  ✓ Birch tree planted\n");

    // Check pine trunk
    Voxel* pine = world_get_voxel(world, 12, 12, 5);
    assert(pine && pine->type == VOXEL_WOOD);
    printf("  ✓ Pine tree planted\n");

    world_destroy(world);
    printf("  ✓ Tree planting tests passed\n");
}

void test_underworld_generator() {
    printf("Testing UNDERWORLD world generator...\n");

    World* world = world_create(16, 16, 16);
    assert(world != NULL);

    // Set generation type
    world->generation_type = WORLD_TYPE_UNDERWORLD;

    // Generate underworld
    world_generate_underworld(world, "underworld_seed");

    // Check bedrock floor and ceiling
    Voxel* floor = world_get_voxel(world, 0, 0, 0);
    assert(floor && floor->type == VOXEL_BEDROCK);
    Voxel* ceiling = world_get_voxel(world, 0, 0, 15);
    assert(ceiling && ceiling->type == VOXEL_BEDROCK);
    printf("  ✓ Has bedrock floor and ceiling\n");

    // Check for stalactites/stalagmites
    int bedrock_count = 0;
    int air_count = 0;
    for (uint32_t z = 1; z < 15; z++) {
        for (uint32_t y = 0; y < 16; y++) {
            for (uint32_t x = 0; x < 16; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v) {
                    if (v->type == VOXEL_BEDROCK) bedrock_count++;
                    if (v->type == VOXEL_AIR) air_count++;
                }
            }
        }
    }

    assert(bedrock_count > 20); // Should have formations
    assert(air_count > 100); // Should have open space
    printf("  ✓ Has stalactites/stalagmites (bedrock: %d, air: %d)\n", bedrock_count, air_count);

    // Check center column exists
    Voxel* center = world_get_voxel(world, 8, 8, 8);
    assert(center && center->type == VOXEL_BEDROCK);
    printf("  ✓ Has center column\n");

    world_destroy(world);
    printf("  ✓ UNDERWORLD generator test passed\n");
}

void test_cloud_generator() {
    printf("Testing CLOUD world generator...\n");

    World* world = world_create(32, 32, 16);
    assert(world != NULL);

    // Set generation type
    world->generation_type = WORLD_TYPE_CLOUD;

    // Generate cloud world
    world_generate_cloud(world, "cloud_seed");

    // Count steam and air voxels
    int steam_count = 0;
    int air_count = 0;

    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v) {
                    if (v->type == VOXEL_STEAM) steam_count++;
                    if (v->type == VOXEL_AIR) air_count++;
                }
            }
        }
    }

    printf("  Steam voxels: %d, Air voxels: %d\n", steam_count, air_count);
    assert(steam_count > 50); // Should have some clouds
    assert(air_count > 100); // Should be mostly air
    printf("  ✓ Has sparse cloud formations\n");

    world_destroy(world);
    printf("  ✓ CLOUD generator test passed\n");
}

void test_random_generator() {
    printf("Testing RANDOM world generator...\n");

    World* world = world_create(16, 16, 16);
    assert(world != NULL);

    // Set generation type
    world->generation_type = WORLD_TYPE_RANDOM;

    // Generate random world
    world_generate_random(world, "random_seed_42");

    // Check has varied terrain
    int min_height = 16;
    int max_height = 0;
    bool has_grass = false;
    bool has_soil = false;
    bool has_stone = false;

    for (uint32_t x = 0; x < world->width; x++) {
        for (uint32_t z = 0; z < world->depth; z++) {
            // Find height
            int height = -1;
            for (uint32_t y = 0; y < world->height; y++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type != VOXEL_AIR) {
                    height = y;
                    if (v->type == VOXEL_GRASS) has_grass = true;
                    if (v->type == VOXEL_SOIL) has_soil = true;
                    if (v->type == VOXEL_STONE) has_stone = true;
                }
            }
            if (height >= 0) {
                if (height < min_height) min_height = height;
                if (height > max_height) max_height = height;
            }
        }
    }

    printf("  Terrain height range: %d to %d\n", min_height, max_height);
    assert(max_height > min_height); // Should have variation
    assert(has_grass && has_soil && has_stone);
    printf("  ✓ Has varied terrain with grass, soil, and stone\n");

    world_destroy(world);
    printf("  ✓ RANDOM generator test passed\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== World Generation Module Test Suite ===\n\n");

    test_home_generator();
    test_farm_generator();
    test_arena_generator();
    test_underworld_generator();
    test_cloud_generator();
    test_random_generator();
    test_solid_fill();
    test_tree_functions();

    printf("\n✅ All tests passed!\n");
    printf("\nThe generation modules have been successfully extracted and work correctly.\n");

    return 0;
}
