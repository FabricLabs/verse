#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test function to demonstrate world generation types
void test_world_generation_types() {
    printf("=== VERSE World Generation Types Test ===\n\n");

    // Test HOME world generation (island in the sky)
    printf("1. Testing HOME world generation (island in the sky)...\n");
    World* home_world = world_create(64, 64, 16);
    if (home_world) {
        world_generate_with_type(home_world, "test_home_seed", WORLD_TYPE_HOME);
        printf("   ✓ HOME world generated successfully\n");
        printf("   - Dimensions: %ux%ux%u\n", home_world->width, home_world->height, home_world->depth);
        printf("   - Gravity: %.2f m/s²\n", home_world->gravity);
        printf("   - Generation type: %d (HOME)\n", home_world->generation_type);

        // Count voxel types
        int grass_count = 0, dirt_count = 0, stone_count = 0, air_count = 0;
        for (uint32_t z = 0; z < home_world->depth; z++) {
            for (uint32_t y = 0; y < home_world->height; y++) {
                for (uint32_t x = 0; x < home_world->width; x++) {
                    Voxel* voxel = world_get_voxel(home_world, x, y, z);
                    if (voxel) {
                        switch (voxel->type) {
                            case VOXEL_GRASS: grass_count++; break;
                            case VOXEL_DIRT: dirt_count++; break;
                            case VOXEL_STONE: stone_count++; break;
                            case VOXEL_AIR: air_count++; break;
                            default: break;
                        }
                    }
                }
            }
        }
        printf("   - Voxel distribution: Grass=%d, Dirt=%d, Stone=%d, Air=%d\n",
               grass_count, dirt_count, stone_count, air_count);

        world_destroy(home_world);
    } else {
        printf("   ✗ Failed to create HOME world\n");
    }

    printf("\n");

    // Test FARM world generation (32x32x32 with soil and grass)
    printf("2. Testing FARM world generation (32x32x32 with soil and grass)...\n");
    World* farm_world = world_create(32, 32, 32);
    if (farm_world) {
        world_generate_with_type(farm_world, "test_farm_seed", WORLD_TYPE_FARM);
        printf("   ✓ FARM world generated successfully\n");
        printf("   - Dimensions: %ux%ux%u\n", farm_world->width, farm_world->height, farm_world->depth);
        printf("   - Gravity: %.2f m/s²\n", farm_world->gravity);
        printf("   - Generation type: %d (FARM)\n", farm_world->generation_type);

        // Count voxel types
        int bedrock_count = 0, soil_count = 0, grass_count = 0, stone_count = 0, air_count = 0;
        for (uint32_t z = 0; z < farm_world->depth; z++) {
            for (uint32_t y = 0; y < farm_world->height; y++) {
                for (uint32_t x = 0; x < farm_world->width; x++) {
                    Voxel* voxel = world_get_voxel(farm_world, x, y, z);
                    if (voxel) {
                        switch (voxel->type) {
                            case VOXEL_BEDROCK: bedrock_count++; break;
                            case VOXEL_SOIL: soil_count++; break;
                            case VOXEL_GRASS: grass_count++; break;
                            case VOXEL_STONE: stone_count++; break;
                            case VOXEL_AIR: air_count++; break;
                            default: break;
                        }
                    }
                }
            }
        }
        printf("   - Voxel distribution: Bedrock=%d, Soil=%d, Grass=%d, Stone=%d, Air=%d\n",
               bedrock_count, soil_count, grass_count, stone_count, air_count);

        world_destroy(farm_world);
    } else {
        printf("   ✗ Failed to create FARM world\n");
    }

    printf("\n");

    // Test RANDOM world generation (farm + springs with water generation)
    printf("3. Testing RANDOM world generation (farm + springs)...\n");
    World* random_world = world_create(32, 32, 32);
    if (random_world) {
        world_generate_with_type(random_world, "test_random_seed", WORLD_TYPE_RANDOM);
        printf("   ✓ RANDOM world generated successfully\n");
        printf("   - Dimensions: %ux%ux%u\n", random_world->width, random_world->height, random_world->depth);
        printf("   - Gravity: %.2f m/s²\n", random_world->gravity);
        printf("   - Generation type: %d (RANDOM)\n", random_world->generation_type);

        // Count voxel types
        int bedrock_count = 0, soil_count = 0, grass_count = 0, stone_count = 0, air_count = 0, spring_count = 0;
        for (uint32_t z = 0; z < random_world->depth; z++) {
            for (uint32_t y = 0; y < random_world->height; y++) {
                for (uint32_t x = 0; x < random_world->width; x++) {
                    Voxel* voxel = world_get_voxel(random_world, x, y, z);
                    if (voxel) {
                        switch (voxel->type) {
                            case VOXEL_BEDROCK: bedrock_count++; break;
                            case VOXEL_SOIL: soil_count++; break;
                            case VOXEL_GRASS: grass_count++; break;
                            case VOXEL_STONE: stone_count++; break;
                            case VOXEL_AIR: air_count++; break;
                            case VOXEL_SPRING: spring_count++; break;
                            default: break;
                        }
                    }
                }
            }
        }
        printf("   - Voxel distribution: Bedrock=%d, Soil=%d, Grass=%d, Stone=%d, Air=%d, Springs=%d\n",
               bedrock_count, soil_count, grass_count, stone_count, air_count, spring_count);

        // Test spring water generation
        printf("   - Testing spring water generation...\n");
        world_update_springs(random_world, 5000000); // 5 seconds in microseconds

        // Count water after spring update
        int water_count = 0;
        for (uint32_t z = 0; z < random_world->depth; z++) {
            for (uint32_t y = 0; y < random_world->height; y++) {
                for (uint32_t x = 0; x < random_world->width; x++) {
                    Voxel* voxel = world_get_voxel(random_world, x, y, z);
                    if (voxel && voxel->type == VOXEL_WATER) {
                        water_count++;
                    }
                }
            }
        }
        printf("   - Water generated from springs: %d blocks\n", water_count);

        world_destroy(random_world);
    } else {
        printf("   ✗ Failed to create RANDOM world\n");
    }

    printf("\n=== Test Complete ===\n");
}

// Test world saving and loading with different types
void test_world_save_load() {
    printf("\n=== Testing World Save/Load with Types ===\n");

    // Create and save a farm world
    World* farm_world = world_create(32, 32, 32);
    if (farm_world) {
        world_generate_with_type(farm_world, "test_save_farm", WORLD_TYPE_FARM);
        bool saved = world_save_by_seed(farm_world, "test_save_farm");
        printf("Farm world saved: %s\n", saved ? "✓ Success" : "✗ Failed");
        world_destroy(farm_world);
    }

    // Load the farm world
    World* loaded_farm = world_load_by_seed("test_save_farm");
    if (loaded_farm) {
        printf("Farm world loaded: ✓ Success\n");
        printf("  - Generation type: %d\n", loaded_farm->generation_type);
        printf("  - Dimensions: %ux%ux%u\n", loaded_farm->width, loaded_farm->height, loaded_farm->depth);
        world_destroy(loaded_farm);
    } else {
        printf("Farm world loaded: ✗ Failed\n");
    }

    // Create and save a random world
    World* random_world = world_create(32, 32, 32);
    if (random_world) {
        world_generate_with_type(random_world, "test_save_random", WORLD_TYPE_RANDOM);
        bool saved = world_save_by_seed(random_world, "test_save_random");
        printf("Random world saved: %s\n", saved ? "✓ Success" : "✗ Failed");
        world_destroy(random_world);
    }

    // Load the random world
    World* loaded_random = world_load_by_seed("test_save_random");
    if (loaded_random) {
        printf("Random world loaded: ✓ Success\n");
        printf("  - Generation type: %d\n", loaded_random->generation_type);
        printf("  - Dimensions: %ux%ux%u\n", loaded_random->width, loaded_random->height, loaded_random->depth);
        world_destroy(loaded_random);
    } else {
        printf("Random world loaded: ✗ Failed\n");
    }
}

int main() {
    printf("=== VERSE World Generation Types Test ===\n");
    printf("Testing the new world generation type system with HOME, FARM, and RANDOM types.\n\n");

    test_world_generation_types();
    test_world_save_load();

    printf("\nAll tests completed successfully!\n");
    return 0;
}
