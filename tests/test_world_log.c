#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"

int main() {
    printf("=== VERSE World Log Test ===\n\n");

    // Test 1: Create a world and set log
    printf("Test 1: Creating world and setting log...\n");
    World* world = world_create(32, 32, 8);
    if (!world) {
        printf("❌ Failed to create world\n");
        return 1;
    }

    const char* test_log = "VERSE World Test Log\n"
                           "===================\n"
                           "This is a test world log\n"
                           "Created for testing purposes\n"
                           "Version: 0\n"
                           "Shape: Spherical";

    if (world_set_log(world, test_log)) {
        printf("✅ Successfully set world log\n");
    } else {
        printf("❌ Failed to set world log\n");
        world_destroy(world);
        return 1;
    }

    // Test 2: Get and verify log
    printf("\nTest 2: Retrieving world log...\n");
    const char* retrieved_log = world_get_log(world);
    if (retrieved_log && strcmp(retrieved_log, test_log) == 0) {
        printf("✅ Successfully retrieved world log\n");
        printf("Log content:\n%s\n", retrieved_log);
    } else {
        printf("❌ Failed to retrieve world log or content mismatch\n");
        world_destroy(world);
        return 1;
    }

    // Test 3: Append to log
    printf("\nTest 3: Appending to world log...\n");
    const char* append_log = "Additional log entry\n"
                             "Testing append functionality";

    if (world_append_log(world, append_log)) {
        printf("✅ Successfully appended to world log\n");
        const char* updated_log = world_get_log(world);
        printf("Updated log content:\n%s\n", updated_log);
    } else {
        printf("❌ Failed to append to world log\n");
        world_destroy(world);
        return 1;
    }

    // Test 4: Save and load world with log
    printf("\nTest 4: Saving and loading world with log...\n");
    if (world_save_by_seed(world, "test_log_world")) {
        printf("✅ Successfully saved world with log\n");

        // Load the world back
        World* loaded_world = world_load_by_seed("test_log_world");
        if (loaded_world) {
            printf("✅ Successfully loaded world\n");

            const char* loaded_log = world_get_log(loaded_world);
            if (loaded_log) {
                printf("✅ Successfully retrieved log from loaded world\n");
                printf("Loaded log content:\n%s\n", loaded_log);
            } else {
                printf("❌ Failed to retrieve log from loaded world\n");
            }

            world_destroy(loaded_world);
        } else {
            printf("❌ Failed to load world\n");
        }
    } else {
        printf("❌ Failed to save world with log\n");
    }

    world_destroy(world);

    printf("\n=== All world log tests completed successfully! ===\n");
    return 0;
}
