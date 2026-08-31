#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"

int main(int argc, char* argv[]) {
    printf("Voxel Condition Test\n");
    printf("--------------------\n\n");

    // Create a small test world
    World* world = world_create(3, 3, 3);
    if (!world) {
        printf("Failed to create world\n");
        return 1;
    }

    // Set some voxel types
    world_set_voxel(world, 0, 0, 0, VOXEL_SOIL);
    world_set_voxel(world, 1, 1, 1, VOXEL_GRASS);
    world_set_voxel(world, 2, 2, 2, VOXEL_STONE);

    // Add some conditions to voxels
    printf("Adding conditions...\n");

    // Add conditions to the dirt voxel
    if (world_add_voxel_condition(world, 0, 0, 0, "WET")) {
        printf("Added 'WET' condition to (0,0,0)\n");
    }

    if (world_add_voxel_condition(world, 0, 0, 0, "FERTILIZED")) {
        printf("Added 'FERTILIZED' condition to (0,0,0)\n");
    }

    // Add conditions to the grass voxel
    if (world_add_voxel_condition(world, 1, 1, 1, "TALL")) {
        printf("Added 'TALL' condition to (1,1,1)\n");
    }

    // Add the same condition multiple times (should only be added once)
    if (world_add_voxel_condition(world, 1, 1, 1, "TALL")) {
        printf("Added 'TALL' condition to (1,1,1) again\n");
    }

    // Add another condition
    if (world_add_voxel_condition(world, 1, 1, 1, "FLOWERING")) {
        printf("Added 'FLOWERING' condition to (1,1,1)\n");
    }

    // Test checking for conditions
    printf("\nChecking conditions...\n");
    printf("(0,0,0) WET: %s\n", world_has_voxel_condition(world, 0, 0, 0, "WET") ? "yes" : "no");
    printf("(0,0,0) DRY: %s\n", world_has_voxel_condition(world, 0, 0, 0, "DRY") ? "yes" : "no");
    printf("(1,1,1) TALL: %s\n", world_has_voxel_condition(world, 1, 1, 1, "TALL") ? "yes" : "no");
    printf("(2,2,2) RADIOACTIVE: %s\n", world_has_voxel_condition(world, 2, 2, 2, "RADIOACTIVE") ? "yes" : "no");

    // Test removing conditions
    printf("\nRemoving conditions...\n");
    if (world_remove_voxel_condition(world, 0, 0, 0, "WET")) {
        printf("Removed 'WET' condition from (0,0,0)\n");
    }
    printf("(0,0,0) WET: %s\n", world_has_voxel_condition(world, 0, 0, 0, "WET") ? "yes" : "no");

    // Add more conditions to demonstrate MAX_VOXEL_CONDITIONS
    printf("\nAdding many conditions to test limit...\n");
    char condition_name[32];
    int added = 0;
    for (int i = 0; i < 257; i++) {
        sprintf(condition_name, "condition_%d", i);
        if (world_add_voxel_condition(world, 2, 2, 2, condition_name)) {
            added++;
        } else {
            printf("Failed to add condition after %d conditions\n", added);
            break;
        }
    }

    // Test serialization and deserialization
    printf("\nTesting serialization...\n");
    char* serialized = world_serialize(world);
    if (serialized) {
        printf("Serialized world (%zu bytes)\n", strlen(serialized));

        // Create a new world from the serialized data
        World* new_world = world_deserialize(serialized);
        if (new_world) {
            printf("Deserialized world successfully\n");

            // Check if conditions were preserved
            printf("\nChecking conditions in deserialized world...\n");
            printf("(0,0,0) FERTILIZED: %s\n",
                world_has_voxel_condition(new_world, 0, 0, 0, "FERTILIZED") ? "yes" : "no");
            printf("(1,1,1) TALL: %s\n",
                world_has_voxel_condition(new_world, 1, 1, 1, "TALL") ? "yes" : "no");
            printf("(1,1,1) FLOWERING: %s\n",
                world_has_voxel_condition(new_world, 1, 1, 1, "FLOWERING") ? "yes" : "no");

            // Clean up
            world_destroy(new_world);
        } else {
            printf("Failed to deserialize world\n");
        }

        free(serialized);
    } else {
        printf("Failed to serialize world\n");
    }

    // Clean up
    world_destroy(world);
    printf("\nTest completed successfully\n");

    return 0;
}
