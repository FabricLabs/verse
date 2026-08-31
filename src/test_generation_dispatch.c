/*
 * test_generation_dispatch.c - Test program for world generation dispatch
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

void test_world_type_conversions() {
    printf("Testing world type conversions...\n");

    // Test string to type
    assert(world_type_from_string("HOME") == WORLD_TYPE_HOME);
    assert(world_type_from_string("FARM") == WORLD_TYPE_FARM);
    assert(world_type_from_string("ARENA") == WORLD_TYPE_ARENA);
    assert(world_type_from_string("CLOUD") == WORLD_TYPE_CLOUD);
    assert(world_type_from_string("UNDERWORLD") == WORLD_TYPE_UNDERWORLD);
    assert(world_type_from_string("RANDOM") == WORLD_TYPE_RANDOM);
    assert(world_type_from_string("INVALID") == WORLD_TYPE_UNKNOWN);
    printf("  ✓ String to type conversions work\n");

    // Test type to string
    assert(strcmp(world_type_to_string(WORLD_TYPE_HOME), "HOME") == 0);
    assert(strcmp(world_type_to_string(WORLD_TYPE_FARM), "FARM") == 0);
    assert(strcmp(world_type_to_string(WORLD_TYPE_ARENA), "ARENA") == 0);
    assert(strcmp(world_type_to_string(WORLD_TYPE_CLOUD), "CLOUD") == 0);
    assert(strcmp(world_type_to_string(WORLD_TYPE_UNDERWORLD), "UNDERWORLD") == 0);
    assert(strcmp(world_type_to_string(WORLD_TYPE_RANDOM), "RANDOM") == 0);
    assert(strcmp(world_type_to_string(WORLD_TYPE_UNKNOWN), "UNKNOWN") == 0);
    printf("  ✓ Type to string conversions work\n");
}

void test_world_generate_dispatch() {
    printf("Testing world_generate main dispatch...\n");

    World* world = world_create(16, 16, 16);
    assert(world != NULL);

    // Test default generation (should use SCOURED)
    world_generate(world, "test_seed");
    assert(world->generation_type == WORLD_TYPE_SCOURED);
    printf("  ✓ Default generation sets type to SCOURED\n");

    // Test gravity generation
    assert(world->gravity >= 4.0f && world->gravity <= 15.0f);
    printf("  ✓ Gravity generated in valid range: %.2f m/s²\n", world->gravity);

    // Test seed ID generation
    assert(strlen(world->seed_id) == 64);
    printf("  ✓ Seed ID generated: %.8s...\n", world->seed_id);

    // Test rarity generation
    assert(world->rarity >= 0.01f && world->rarity <= 0.15f);
    printf("  ✓ Rarity generated in valid range: %.3f\n", world->rarity);

    world_destroy(world);
}

void test_world_generate_with_type() {
    printf("Testing world_generate_with_type dispatch...\n");

    // Test each world type
    WorldType types[] = {
        WORLD_TYPE_HOME,
        WORLD_TYPE_FARM,
        WORLD_TYPE_ARENA,
        WORLD_TYPE_UNDERWORLD,
        WORLD_TYPE_CLOUD,
        WORLD_TYPE_RANDOM,
        WORLD_TYPE_SOLID
    };

    for (int i = 0; i < 7; i++) {
        World* world = world_create(16, 16, 16);
        assert(world != NULL);

        world_generate_with_type(world, "test_seed", types[i]);
        assert(world->generation_type == (uint32_t)types[i]);

        // Verify something was generated
        int non_air_count = 0;
        for (uint32_t z = 0; z < world->depth; z++) {
            for (uint32_t y = 0; y < world->height; y++) {
                for (uint32_t x = 0; x < world->width; x++) {
                    Voxel* v = world_get_voxel(world, x, y, z);
                    if (v && v->type != VOXEL_AIR) {
                        non_air_count++;
                    }
                }
            }
        }

        // All world types should generate something
        assert(non_air_count > 0);
        printf("  ✓ %s world generated (%d non-air voxels)\n",
               world_type_to_string(types[i]), non_air_count);

        world_destroy(world);
    }
}

void test_world_generate_with_fill() {
    printf("Testing world_generate_with_type_and_fill...\n");

    World* world = world_create(8, 8, 8);
    assert(world != NULL);

    // Test SOLID world with custom fill
    world_generate_with_type_and_fill(world, "solid_test", WORLD_TYPE_SOLID, VOXEL_GOLD);

    // Check all voxels are gold
    int gold_count = 0;
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (v && v->type == VOXEL_GOLD) {
                    gold_count++;
                }
            }
        }
    }

    assert(gold_count == 8 * 8 * 8);
    printf("  ✓ SOLID world filled with GOLD: %d voxels\n", gold_count);

    world_destroy(world);
}

void test_deterministic_generation() {
    printf("Testing deterministic generation...\n");

    // Generate two worlds with same seed
    World* world1 = world_create(16, 16, 16);
    World* world2 = world_create(16, 16, 16);

    world_generate_with_type(world1, "deterministic_test", WORLD_TYPE_RANDOM);
    world_generate_with_type(world2, "deterministic_test", WORLD_TYPE_RANDOM);

    // Check same gravity
    assert(world1->gravity == world2->gravity);
    printf("  ✓ Same gravity: %.2f\n", world1->gravity);

    // Check same rarity
    assert(world1->rarity == world2->rarity);
    printf("  ✓ Same rarity: %.3f\n", world1->rarity);

    // Check same seed ID
    assert(strcmp(world1->seed_id, world2->seed_id) == 0);
    printf("  ✓ Same seed ID: %.8s...\n", world1->seed_id);

    // Check same voxels
    bool voxels_match = true;
    for (uint32_t z = 0; z < 16 && voxels_match; z++) {
        for (uint32_t y = 0; y < 16 && voxels_match; y++) {
            for (uint32_t x = 0; x < 16 && voxels_match; x++) {
                Voxel* v1 = world_get_voxel(world1, x, y, z);
                Voxel* v2 = world_get_voxel(world2, x, y, z);
                if (v1 && v2) {
                    if (v1->type != v2->type) {
                        voxels_match = false;
                    }
                }
            }
        }
    }

    assert(voxels_match);
    printf("  ✓ All voxels match\n");

    world_destroy(world1);
    world_destroy(world2);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== World Generation Dispatch Test Suite ===\n\n");

    test_world_type_conversions();
    test_world_generate_dispatch();
    test_world_generate_with_type();
    test_world_generate_with_fill();
    test_deterministic_generation();

    printf("\n✅ All tests passed!\n");
    printf("\nThe generation dispatch module has been successfully extracted and works correctly.\n");

    return 0;
}
