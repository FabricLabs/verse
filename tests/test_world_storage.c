#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"

int main() {
    printf("=== VERSE World Storage Test ===\n\n");

    // Test 1: Check if main menu world exists
    const char* main_menu_seed = "main_menu_seed_verse_2024";
    printf("Test 1: Checking if main menu world exists...\n");
    if (world_exists_by_seed(main_menu_seed)) {
        printf("✅ Main menu world exists at worlds/%s.world\n", main_menu_seed);
    } else {
        printf("❌ Main menu world does not exist\n");
        return 1;
    }

    // Test 2: Load the main menu world
    printf("\nTest 2: Loading main menu world...\n");
    World* loaded_world = world_load_by_seed(main_menu_seed);
    if (loaded_world) {
        printf("✅ Successfully loaded world:\n");
        printf("   Dimensions: %ux%ux%u\n", loaded_world->width, loaded_world->height, loaded_world->depth);
        printf("   Version: %u\n", loaded_world->version);
        printf("   Gravity: %.2f m/s²\n", world_get_gravity(loaded_world));

        // Test 3: Check some voxels
        printf("\nTest 3: Checking voxel data...\n");
        int air_count = 0, grass_count = 0, stone_count = 0;

        // Sample a few voxels
        for (int x = 30; x < 35; x++) {
            for (int y = 30; y < 35; y++) {
                for (int z = 8; z < 12; z++) {
                    Voxel* voxel = world_get_voxel(loaded_world, x, y, z);
                    if (voxel) {
                        switch (voxel->type) {
                            case VOXEL_AIR: air_count++; break;
                            case VOXEL_GRASS: grass_count++; break;
                            case VOXEL_STONE: stone_count++; break;
                            default: break;
                        }
                    }
                }
            }
        }

        printf("   Sample voxels - Air: %d, Grass: %d, Stone: %d\n", air_count, grass_count, stone_count);

        // Test 4: Save a test world
        printf("\nTest 4: Creating and saving a test world...\n");
        World* test_world = world_create(32, 32, 8);
        if (test_world) {
            world_generate(test_world, "test_storage_seed");

            if (world_save_by_seed(test_world, "test_storage_seed")) {
                printf("✅ Test world saved to worlds/test_storage_seed.world\n");

                // Test 5: Load the test world
                printf("\nTest 5: Loading test world...\n");
                World* reloaded_world = world_load_by_seed("test_storage_seed");
                if (reloaded_world) {
                    printf("✅ Successfully reloaded test world:\n");
                    printf("   Dimensions: %ux%ux%u\n", reloaded_world->width, reloaded_world->height, reloaded_world->depth);
                    printf("   Gravity: %.2f m/s²\n", world_get_gravity(reloaded_world));
                    world_destroy(reloaded_world);
                } else {
                    printf("❌ Failed to reload test world\n");
                }
            } else {
                printf("❌ Failed to save test world\n");
            }
            world_destroy(test_world);
        } else {
            printf("❌ Failed to create test world\n");
        }

        world_destroy(loaded_world);
    } else {
        printf("❌ Failed to load main menu world\n");
        return 1;
    }

    printf("\n=== All tests completed successfully! ===\n");
    return 0;
}
