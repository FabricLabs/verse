#include <stdio.h>
#include <stdlib.h>
#include "src/world.h"

int main() {
    printf("Testing enhanced world loading functionality...\n");

    // Test if the world exists with the new naming format
    if (world_exists_by_seed("test_home_seed")) {
        printf("World 'test_home_seed' exists!\n");
    } else {
        printf("World 'test_home_seed' does not exist!\n");
        return 1;
    }

    // Create a new world instance to load into
    World loaded_world = {0};

    // Try to load the world using the enhanced load functionality
    if (world_load_by_seed(&loaded_world, "test_home_seed")) {
        printf("Successfully loaded world!\n");
        printf("World dimensions: %ux%ux%u\n", loaded_world.width, loaded_world.height, loaded_world.depth);
        printf("World generation type: %d\n", loaded_world.generation_type);
        printf("World seed ID: %s\n", loaded_world.seed_id);

        // Clean up
        if (loaded_world.voxels) {
            free(loaded_world.voxels);
        }
        if (loaded_world.log) {
            free(loaded_world.log);
        }

        printf("Test completed successfully!\n");
        return 0;
    } else {
        printf("Failed to load world!\n");
        return 1;
    }
}
