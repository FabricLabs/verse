/*
 * test_generation_labyrinth.c - Test program for LABYRINTH world generator
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

void test_labyrinth_basic() {
    printf("Testing LABYRINTH world generator...\n");

    World* world = world_create(32, 32, 16);
    assert(world != NULL);

    // Generate labyrinth world
    world_generate_labyrinth(world, "labyrinth_test");

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

    // Check for maze walls at z=1
    int wall_count = 0;
    int air_count = 0;
    int sand_count = 0;
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            Voxel* v = world_get_voxel(world, x, y, 1);
            if (v) {
                if (v->type == VOXEL_BEDROCK) wall_count++;
                else if (v->type == VOXEL_AIR) air_count++;
                else if (v->type == VOXEL_SAND) sand_count++;
            }
        }
    }

    // Should have walls and passages
    assert(wall_count > 0);
    assert(air_count > 0);
    printf("  ✓ Maze structure: walls=%d, air=%d, path=%d\n", wall_count, air_count, sand_count);

    // Check for outer walls
    int outer_walls = 0;
    // Check horizontal walls
    for (uint32_t x = 1; x < world->width - 1; x++) {
        Voxel* v1 = world_get_voxel(world, x, 1, 1);
        Voxel* v2 = world_get_voxel(world, x, world->height - 2, 1);
        if (v1 && v1->type == VOXEL_BEDROCK) outer_walls++;
        if (v2 && v2->type == VOXEL_BEDROCK) outer_walls++;
    }
    // Check vertical walls
    for (uint32_t y = 1; y < world->height - 1; y++) {
        Voxel* v1 = world_get_voxel(world, 1, y, 1);
        Voxel* v2 = world_get_voxel(world, world->width - 2, y, 1);
        if (v1 && v1->type == VOXEL_BEDROCK) outer_walls++;
        if (v2 && v2->type == VOXEL_BEDROCK) outer_walls++;
    }

    assert(outer_walls > 0);
    printf("  ✓ Outer walls present: %d segments\n", outer_walls);

    // Check for center clearing
    uint32_t cx = world->width / 2;
    uint32_t cy = world->height / 2;
    int center_clear = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            Voxel* v = world_get_voxel(world, cx + dx, cy + dy, 1);
            if (v && (v->type == VOXEL_AIR || v->type == VOXEL_SAND)) {
                center_clear++;
            }
        }
    }
    assert(center_clear > 0);
    printf("  ✓ Center clearing: %d clear cells\n", center_clear);

    // Check that most of world above z=1 is air
    int above_air = 0;
    for (uint32_t z = 2; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type == VOXEL_AIR) {
                    above_air++;
                }
            }
        }
    }
    assert(above_air == (world->depth - 2) * world->width * world->height);
    printf("  ✓ Upper levels are clear\n");

    world_destroy(world);
    printf("  ✓ LABYRINTH generator test passed\n");
}

void test_labyrinth_deterministic() {
    printf("Testing LABYRINTH determinism...\n");

    World* world1 = world_create(16, 16, 8);
    World* world2 = world_create(16, 16, 8);

    world_generate_labyrinth(world1, "deterministic_lab");
    world_generate_labyrinth(world2, "deterministic_lab");

    // Compare all voxels
    bool match = true;
    for (uint32_t z = 0; z < 8; z++) {
        for (uint32_t y = 0; y < 16; y++) {
            for (uint32_t x = 0; x < 16; x++) {
                Voxel* v1 = world_get_voxel(world1, x, y, z);
                Voxel* v2 = world_get_voxel(world2, x, y, z);
                if (v1 && v2 && v1->type != v2->type) {
                    match = false;
                }
            }
        }
    }

    assert(match);
    printf("  ✓ LABYRINTH generation is deterministic\n");

    world_destroy(world1);
    world_destroy(world2);
}

void test_labyrinth_connectivity() {
    printf("Testing LABYRINTH connectivity...\n");

    World* world = world_create(20, 20, 8);
    world_generate_labyrinth(world, "connectivity_test");

    // Check that there are entrances (air blocks on the outer walls at z=1)
    int entrance_count = 0;

    // Check horizontal walls for entrances
    for (uint32_t x = 0; x < world->width; x++) {
        Voxel* v1 = world_get_voxel(world, x, 1, 1);
        Voxel* v2 = world_get_voxel(world, x, world->height - 2, 1);
        if (v1 && v1->type == VOXEL_AIR) entrance_count++;
        if (v2 && v2->type == VOXEL_AIR) entrance_count++;
    }

    // Check vertical walls for entrances
    for (uint32_t y = 0; y < world->height; y++) {
        Voxel* v1 = world_get_voxel(world, 1, y, 1);
        Voxel* v2 = world_get_voxel(world, world->width - 2, y, 1);
        if (v1 && v1->type == VOXEL_AIR) entrance_count++;
        if (v2 && v2->type == VOXEL_AIR) entrance_count++;
    }

    assert(entrance_count >= 4); // Should have at least 4 entrances
    printf("  ✓ Found %d entrances\n", entrance_count);

    // Check for sand path (marked path from center to entrance)
    int path_blocks = 0;
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            Voxel* v = world_get_voxel(world, x, y, 1);
            if (v && v->type == VOXEL_SAND) {
                path_blocks++;
            }
        }
    }

    assert(path_blocks > 0);
    printf("  ✓ Marked path found: %d blocks\n", path_blocks);

    world_destroy(world);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== LABYRINTH World Generator Test Suite ===\n\n");

    test_labyrinth_basic();
    test_labyrinth_deterministic();
    test_labyrinth_connectivity();

    printf("\n✅ All tests passed!\n");
    printf("\nThe LABYRINTH generator has been successfully extracted and works correctly.\n");

    return 0;
}
