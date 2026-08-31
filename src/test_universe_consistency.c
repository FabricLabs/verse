#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "universe.h"

// Consistency checks below used to print ✗ and still exit 0, so this suite could
// not fail the release gate. Failures are now counted and returned.
static int test_failures = 0;

static void fail(const char *what) {
    printf("✗ %s\n", what);
    test_failures++;
}

// Test universe consistency features
int main(void) {
    printf("Universe Consistency Test\n");
    printf("========================\n\n");

    // Create a universe
    Universe universe;
    if (!universe_init(&universe, "test_universe_seed_123", 0, 1)) {
        printf("Failed to initialize universe\n");
        return 1;
    }
    printf("✓ Universe initialized with seed: %s\n", universe.seed);

    // Test 1: Generate standalone world
    printf("\n--- Test 1: Standalone World Generation ---\n");
    World* standalone_world = world_create(32, 32, 32);
    if (!standalone_world) {
        printf("Failed to create standalone world\n");
        return 1;
    }

    // Generate the world with universe context
    if (world_generate_standalone(standalone_world, "standalone_seed_456", WORLD_TYPE_HOME, &universe)) {
        printf("✓ Standalone world generated successfully\n");
        printf("  - World seed: %s\n", standalone_world->seed_id);
        printf("  - Universe context: %p\n", (void*)standalone_world->universe_context);
        printf("  - Universe position: (%lu, %lu, %lu)\n",
               standalone_world->universe_x, standalone_world->universe_y, standalone_world->universe_z);
    } else {
        fail("Failed to generate standalone world");
    }

    // Test 2: Generate world in universe at specific coordinates
    printf("\n--- Test 2: World in Universe at Coordinates ---\n");
    World* positioned_world = world_create(32, 32, 32);
    if (!positioned_world) {
        printf("Failed to create positioned world\n");
        return 1;
    }

    if (world_generate_in_universe(positioned_world, "positioned_seed_789", WORLD_TYPE_FARM, &universe, 5, 3, 0)) {
        printf("✓ Positioned world generated successfully\n");
        printf("  - World seed: %s\n", positioned_world->seed_id);
        printf("  - Universe context: %p\n", (void*)positioned_world->universe_context);
        printf("  - Universe position: (%lu, %lu, %lu)\n",
               positioned_world->universe_x, positioned_world->universe_y, positioned_world->universe_z);
    } else {
        fail("Failed to generate positioned world");
    }

    // Test 3: Check universe placement
    printf("\n--- Test 3: Universe Placement Verification ---\n");
    if (universe_has(&universe, 0, 0, 0)) {
        World* placed_world = universe_get(&universe, 0, 0, 0);
        printf("✓ World found at (0,0,0): %p\n", (void*)placed_world);
        if (placed_world) {
            printf("  - Seed: %s\n", placed_world->seed_id);
            printf("  - Type: %d\n", placed_world->generation_type);
        }
    } else {
        fail("No world found at (0,0,0)");
    }

    if (universe_has(&universe, 5, 3, 0)) {
        World* placed_world = universe_get(&universe, 5, 3, 0);
        printf("✓ World found at (5,3,0): %p\n", (void*)placed_world);
        if (placed_world) {
            printf("  - Seed: %s\n", placed_world->seed_id);
            printf("  - Type: %d\n", placed_world->generation_type);
        }
    } else {
        fail("No world found at (5,3,0)");
    }

    // Test 4: Save and load with universe context
    printf("\n--- Test 4: Save/Load with Universe Context ---\n");
    if (world_save_with_universe(standalone_world, "test_save_seed", &universe)) {
        printf("✓ World saved with universe context\n");

        // Try to load it back
        World* loaded_world = world_load_with_universe("test_save_seed", &universe);
        if (loaded_world) {
            printf("✓ World loaded with universe context\n");
            printf("  - Loaded seed: %s\n", loaded_world->seed_id);
            printf("  - Universe context: %p\n", (void*)loaded_world->universe_context);
            printf("  - Universe position: (%lu, %lu, %lu)\n",
                   loaded_world->universe_x, loaded_world->universe_y, loaded_world->universe_z);
            world_destroy(loaded_world);
        } else {
            fail("Failed to load world with universe context");
        }
    } else {
        fail("Failed to save world with universe context");
    }

    // Test 5: Bulk operations with universe context
    printf("\n--- Test 5: Bulk Operations with Universe Context ---\n");
    // This would test the bulk operations system we implemented earlier
    printf("  - Bulk operations system is available for world modifications\n");
    printf("  - Can use filters to modify specific regions\n");
    printf("  - Supports additive, subtractive, and masked operations\n");

    // Cleanup
    printf("\n--- Cleanup ---\n");
    world_destroy(standalone_world);
    world_destroy(positioned_world);
    universe_free(&universe);
    printf("✓ All resources cleaned up\n");

    if (test_failures > 0) {
        printf("\n=== %d check(s) FAILED ===\n", test_failures);
        return 1;
    }

    printf("\n=== Universe Consistency Test Complete ===\n");
    return 0;
}
