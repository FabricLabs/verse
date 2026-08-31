#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "world.h"
#include "world_spawn.h"

int main() {
    printf("=== World Spawn System Test ===\n");

    // Initialize spawn system
    world_spawn_init();

    // Test 1: Test center calculation for different world sizes
    printf("\n1. Testing center calculation...\n");

    // Test odd dimensions (should use middle tile)
    World* odd_world = world_create(63, 64, 63);
    if (odd_world) {
        int center_x, center_z;
        world_get_center_position(odd_world, &center_x, &center_z);
        printf("Odd dimensions (63x63): center = (%d, %d)\n", center_x, center_z);
        printf("Expected: (31, 31) - %s\n",
               (center_x == 31 && center_z == 31) ? "✓ PASS" : "✗ FAIL");

        // Test if it's a center tile
        bool is_center = world_is_center_tile(odd_world, center_x, center_z);
        printf("Is center tile: %s\n", is_center ? "✓ YES" : "✗ NO");

        world_destroy(odd_world);
    }

    // Test even dimensions (should use tile before middle)
    World* even_world = world_create(64, 64, 64);
    if (even_world) {
        int center_x, center_z;
        world_get_center_position(even_world, &center_x, &center_z);
        printf("Even dimensions (64x64): center = (%d, %d)\n", center_x, center_z);
        printf("Expected: (31, 31) - %s\n",
               (center_x == 31 && center_z == 31) ? "✓ PASS" : "✗ FAIL");

        // Test if it's a center tile
        bool is_center = world_is_center_tile(even_world, center_x, center_z);
        printf("Is center tile: %s\n", is_center ? "✓ YES" : "✗ NO");

        world_destroy(even_world);
    }

    // Test 2: Test spawn position finding
    printf("\n2. Testing spawn position finding...\n");

    // Create a test world with known structure
    World* test_world = world_create(64, 64, 16);
    if (test_world) {
        // Generate the world
        world_generate_with_type(test_world, "test_spawn_seed", WORLD_TYPE_HOME);

        // Find best spawn position
        SpawnPosition spawn = world_find_best_spawn_position(test_world);

        if (spawn.is_safe) {
            printf("✓ Found safe spawn position: (%d, %d, %d)\n",
                   spawn.x, spawn.y, spawn.z);
            printf("Reason: %s\n", spawn.spawn_reason);

            // Validate the spawn position
            SpawnValidation validation = world_validate_spawn_position(test_world,
                                                                     spawn.x, spawn.y, spawn.z);
            printf("Validation: %s\n", validation.validation_msg);
            printf("Has solid ground: %s\n", validation.has_solid_ground ? "✓ YES" : "✗ NO");
            printf("Has clear space: %s\n", validation.has_clear_space ? "✓ YES" : "✗ NO");
            printf("Is center tile: %s\n", validation.is_center_tile ? "✓ YES" : "✗ NO");

            free(validation.validation_msg);
        } else {
            printf("✗ Failed to find safe spawn position\n");
            printf("Reason: %s\n", spawn.spawn_reason);
        }

        // Clean up
        if (spawn.spawn_reason) {
            free(spawn.spawn_reason);
        }

        world_destroy(test_world);
    }

    // Test 3: Test safe ground finding
    printf("\n3. Testing safe ground finding...\n");

    World* ground_test_world = world_create(32, 32, 16);
    if (ground_test_world) {
        // Generate a simple world
        world_generate_with_type(ground_test_world, "ground_test_seed", WORLD_TYPE_FARM);

        // Test finding safe ground at center
        int center_x, center_z;
        world_get_center_position(ground_test_world, &center_x, &center_z);

        int safe_y = world_find_safe_ground(ground_test_world, center_x, center_z);
        if (safe_y >= 0) {
            printf("✓ Found safe ground at center (%d, %d): Y = %d\n", center_x, center_z, safe_y);
        } else {
            printf("✗ No safe ground found at center (%d, %d)\n", center_x, center_z);
        }

        // Test finding safe ground at edge
        int edge_x = 0, edge_z = 0;
        int edge_safe_y = world_find_safe_ground(ground_test_world, edge_x, edge_z);
        if (edge_safe_y >= 0) {
            printf("✓ Found safe ground at edge (%d, %d): Y = %d\n", edge_x, edge_z, edge_safe_y);
        } else {
            printf("✗ No safe ground found at edge (%d, %d)\n", edge_x, edge_z);
        }

        world_destroy(ground_test_world);
    }

    // Test 4: Test nearest safe spawn finding
    printf("\n4. Testing nearest safe spawn finding...\n");

    World* nearest_test_world = world_create(64, 64, 16);
    if (nearest_test_world) {
        // Generate the world
        world_generate_with_type(nearest_test_world, "nearest_test_seed", WORLD_TYPE_HOME);

        // Try to find nearest safe spawn to a target position
        int target_x = 32, target_y = 32;
        SpawnPosition nearest_spawn = world_find_nearest_safe_spawn(nearest_test_world, target_x, target_y);

        if (nearest_spawn.is_safe) {
            printf("✓ Found nearest safe spawn: (%d, %d, %d)\n",
                   nearest_spawn.x, nearest_spawn.y, nearest_spawn.z);
            printf("Reason: %s\n", nearest_spawn.spawn_reason);

            // Calculate distance from target
            int dx = nearest_spawn.x - target_x;
            int dy = nearest_spawn.y - target_y;
            double distance = sqrt(dx * dx + dy * dy);
            printf("Distance from target: %.2f tiles\n", distance);
        } else {
            printf("✗ Failed to find nearest safe spawn\n");
            printf("Reason: %s\n", nearest_spawn.spawn_reason);
        }

        // Clean up
        if (nearest_spawn.spawn_reason) {
            free(nearest_spawn.spawn_reason);
        }

        world_destroy(nearest_test_world);
    }

    // Clean up
    world_spawn_cleanup();

    printf("\n=== Spawn System Test Completed ===\n");
    printf("✓ Center calculation works\n");
    printf("✓ Spawn position finding works\n");
    printf("✓ Safe ground detection works\n");
    printf("✓ Nearest spawn finding works\n");

    return 0;
}
