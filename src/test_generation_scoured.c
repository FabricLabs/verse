/*
 * test_generation_scoured.c - Test program for SCOURED world generator
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

void test_scoured_basic() {
    printf("Testing SCOURED world generator...\n");

    World* world = world_create(16, 16, 16);
    assert(world != NULL);

    // Generate scoured world
    world_generate_scoured(world, "scoured_test");

    // Check bedrock floor at z=0
    int bedrock_count = 0;
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            Voxel* v = world_get_voxel(world, x, y, 0);
            if (v && (v->type == VOXEL_BEDROCK ||
                     v->type == VOXEL_IRON || v->type == VOXEL_GOLD ||
                     v->type == VOXEL_SILVER || v->type == VOXEL_COPPER)) {
                bedrock_count++;
            }
        }
    }

    // Should have complete bedrock floor (possibly with some minerals)
    assert(bedrock_count == 16 * 16);
    printf("  ✓ Complete bedrock floor at z=0: %d blocks\n", bedrock_count);

    // Check for scattered rocks above
    int rock_count = 0;
    for (uint32_t z = 1; z < 3; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type == VOXEL_STONE) {
                    rock_count++;
                }
            }
        }
    }

    assert(rock_count >= 3 && rock_count <= 20);
    printf("  ✓ Scattered rocks found: %d\n", rock_count);

    // Check for mineral deposits
    int mineral_count = 0;
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            Voxel* v = world_get_voxel(world, x, y, 0);
            if (v && (v->type == VOXEL_IRON || v->type == VOXEL_GOLD ||
                     v->type == VOXEL_SILVER || v->type == VOXEL_COPPER)) {
                mineral_count++;
            }
        }
    }

    assert(mineral_count >= 1);
    printf("  ✓ Mineral deposits in bedrock: %d\n", mineral_count);

    // Most of the world should be air
    int air_count = 0;
    for (uint32_t z = 1; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type == VOXEL_AIR) {
                    air_count++;
                }
            }
        }
    }

    assert(air_count > (15 * 16 * 16) - 20); // Most blocks above z=0 should be air
    printf("  ✓ Mostly barren landscape (air blocks: %d)\n", air_count);

    world_destroy(world);
    printf("  ✓ SCOURED generator test passed\n");
}

void test_scoured_deterministic() {
    printf("Testing SCOURED determinism...\n");

    World* world1 = world_create(8, 8, 8);
    World* world2 = world_create(8, 8, 8);

    world_generate_scoured(world1, "deterministic_scoured");
    world_generate_scoured(world2, "deterministic_scoured");

    // Compare all voxels
    bool match = true;
    for (uint32_t z = 0; z < 8; z++) {
        for (uint32_t y = 0; y < 8; y++) {
            for (uint32_t x = 0; x < 8; x++) {
                Voxel* v1 = world_get_voxel(world1, x, y, z);
                Voxel* v2 = world_get_voxel(world2, x, y, z);
                if (v1 && v2 && v1->type != v2->type) {
                    match = false;
                }
            }
        }
    }

    assert(match);
    printf("  ✓ SCOURED generation is deterministic\n");

    world_destroy(world1);
    world_destroy(world2);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== SCOURED World Generator Test Suite ===\n\n");

    test_scoured_basic();
    test_scoured_deterministic();

    printf("\n✅ All tests passed!\n");
    printf("\nThe SCOURED generator has been successfully extracted and works correctly.\n");

    return 0;
}
