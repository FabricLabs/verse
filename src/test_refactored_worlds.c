#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "world_bulk_ops.h"

static int test_failures = 0;

// Records the outcome as well as labelling it, so a failed bulk operation reaches
// the exit code instead of only being printed.
static const char *outcome(bool ok) {
    if (!ok) test_failures++;
    return ok ? "SUCCESS" : "FAILED";
}

// Test the refactored world generation functions
int main(void) {
    printf("Refactored World Generation Test\n");
    printf("================================\n\n");

    // Test 1: HOME world generation
    printf("--- Test 1: HOME World Generation ---\n");
    World* home_world = world_create(64, 64, 64);
    if (!home_world) {
        printf("Failed to create home world\n");
        return 1;
    }

    // Generate the home world
    world_generate_with_type(home_world, "test_home_seed", WORLD_TYPE_HOME);
    printf("✓ HOME world generated successfully\n");
    printf("  - Dimensions: %ux%ux%u\n", home_world->width, home_world->height, home_world->depth);
    printf("  - Generation type: %d\n", home_world->generation_type);

    // Count different voxel types
    uint32_t air_count = 0, stone_count = 0, grass_count = 0, dirt_count = 0;
    for (uint32_t z = 0; z < home_world->depth; z++) {
        for (uint32_t y = 0; y < home_world->height; y++) {
            for (uint32_t x = 0; x < home_world->width; x++) {
                Voxel* voxel = world_get_voxel(home_world, x, y, z);
                if (voxel) {
                    switch (voxel->type) {
                        case VOXEL_AIR: air_count++; break;
                        case VOXEL_STONE: stone_count++; break;
                        case VOXEL_GRASS: grass_count++; break;
                        case VOXEL_SOIL: dirt_count++; break;
                    }
                }
            }
        }
    }
    printf("  - Voxel counts: AIR=%u, STONE=%u, GRASS=%u, DIRT=%u\n",
           air_count, stone_count, grass_count, dirt_count);

    // Test 2: ARENA world generation
    printf("\n--- Test 2: ARENA World Generation ---\n");
    World* arena_world = world_create(64, 64, 64);
    if (!arena_world) {
        printf("Failed to create arena world\n");
        return 1;
    }

    world_generate_with_type(arena_world, "test_arena_seed", WORLD_TYPE_ARENA);
    printf("✓ ARENA world generated successfully\n");
    printf("  - Dimensions: %ux%ux%u\n", arena_world->width, arena_world->height, arena_world->depth);
    printf("  - Generation type: %d\n", arena_world->generation_type);

    // Count voxel types in arena
    uint32_t arena_air = 0, arena_limestone = 0;
    for (uint32_t z = 0; z < arena_world->depth; z++) {
        for (uint32_t y = 0; y < arena_world->height; y++) {
            for (uint32_t x = 0; x < arena_world->width; x++) {
                Voxel* voxel = world_get_voxel(arena_world, x, y, z);
                if (voxel) {
                    switch (voxel->type) {
                        case VOXEL_AIR: arena_air++; break;
                        case VOXEL_STONE_LIMESTONE: arena_limestone++; break;
                    }
                }
            }
        }
    }
    printf("  - Voxel counts: AIR=%u, LIMESTONE=%u\n", arena_air, arena_limestone);

    // Test 3: FARM world generation
    printf("\n--- Test 3: FARM World Generation ---\n");
    World* farm_world = world_create(32, 32, 32);
    if (!farm_world) {
        printf("Failed to create farm world\n");
        return 1;
    }

    world_generate_with_type(farm_world, "test_farm_seed", WORLD_TYPE_FARM);
    printf("✓ FARM world generated successfully\n");
    printf("  - Dimensions: %ux%ux%u\n", farm_world->width, farm_world->height, farm_world->depth);
    printf("  - Generation type: %d\n", farm_world->generation_type);

    // Count voxel types in farm
    uint32_t farm_air = 0, farm_bedrock = 0, farm_stone = 0, farm_dirt = 0, farm_soil = 0, farm_grass = 0;
    for (uint32_t z = 0; z < farm_world->depth; z++) {
        for (uint32_t y = 0; y < farm_world->height; y++) {
            for (uint32_t x = 0; x < farm_world->width; x++) {
                Voxel* voxel = world_get_voxel(farm_world, x, y, z);
                if (voxel) {
                    switch (voxel->type) {
                        case VOXEL_AIR: farm_air++; break;
                        case VOXEL_BEDROCK: farm_bedrock++; break;
                        case VOXEL_STONE: farm_stone++; break;
                        case VOXEL_SOIL: farm_dirt++; break;
                        case VOXEL_GRASS: farm_grass++; break;
                    }
                }
            }
        }
    }
    printf("  - Voxel counts: AIR=%u, BEDROCK=%u, STONE=%u, DIRT=%u, SOIL=%u, GRASS=%u\n",
           farm_air, farm_bedrock, farm_stone, farm_dirt, farm_soil, farm_grass);

    // Test 4: Bulk operations on generated worlds
    printf("\n--- Test 4: Bulk Operations on Generated Worlds ---\n");

        // Add a stone platform to the home world (using valid coordinates)
    bool success = world_fill_region(home_world, 20, 20, 20, 24, 24, 24,
                                    VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
    printf("  - Stone platform added to home world: %s\n", outcome(success));

    // Add a water pool to the arena
    success = world_fill_sphere(arena_world, 32, 32, 20, 8,
                               VOXEL_WATER, 1.0f, BULK_OP_REPLACE, NULL, NULL);
    printf("  - Water pool added to arena: %s\n", outcome(success));

    // Add a crystal formation to the farm
    success = world_fill_ellipsoid(farm_world, 16, 16, 20, 6, 4, 8,
                                  VOXEL_CRYSTAL, 1.0f, BULK_OP_REPLACE, NULL, NULL);
    printf("  - Crystal formation added to farm: %s\n", outcome(success));

    // Cleanup
    printf("\n--- Cleanup ---\n");
    world_destroy(home_world);
    world_destroy(arena_world);
    world_destroy(farm_world);
    printf("✓ All worlds cleaned up\n");

    if (test_failures > 0) {
        printf("\n=== %d operation(s) FAILED ===\n", test_failures);
        return 1;
    }

    printf("\n=== Refactored World Generation Test Complete ===\n");
    return 0;
}
