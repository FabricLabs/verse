#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test spring placement at bedrock level
void test_spring_bedrock_placement() {
    printf("Testing spring placement at bedrock level...\n");

    // Create a small test world
    World* world = world_create(16, 16, 16);
    if (!world) {
        printf("Failed to create test world\n");
        return;
    }

    // Generate a RANDOM world
    world_generate_with_type(world, "test_spring_bedrock", WORLD_TYPE_RANDOM);

    // Check that all springs are at bedrock level (y=0)
    int springs_found = 0;
    int springs_at_bedrock = 0;

    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* voxel = world_get_voxel(world, x, y, z);
                if (voxel && voxel->type == VOXEL_SPRING) {
                    springs_found++;
                    if (y == 0) {
                        springs_at_bedrock++;
                        printf("Spring found at bedrock level: (%u, %u, %u)\n", x, y, z);
                    } else {
                        printf("ERROR: Spring found above bedrock level: (%u, %u, %u)\n", x, y, z);
                    }
                }
            }
        }
    }

    printf("Total springs found: %d\n", springs_found);
    printf("Springs at bedrock level: %d\n", springs_at_bedrock);

    if (springs_found == springs_at_bedrock) {
        printf("✓ All springs correctly placed at bedrock level\n");
    } else {
        printf("✗ Some springs placed above bedrock level\n");
    }

    // Print world log
    printf("\nWorld generation log:\n%s\n", world->log);

    world_destroy(world);
}

// Test water generation when voxel above is removed
void test_spring_water_generation() {
    printf("\nTesting spring water generation...\n");

    // Create a small test world
    World* world = world_create(8, 8, 8);
    if (!world) {
        printf("Failed to create test world\n");
        return;
    }

    // Generate a RANDOM world
    world_generate_with_type(world, "test_spring_water", WORLD_TYPE_RANDOM);

    // Find a spring
    int spring_x = -1, spring_z = -1;
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t x = 0; x < world->width; x++) {
            Voxel* voxel = world_get_voxel(world, x, 0, z);
            if (voxel && voxel->type == VOXEL_SPRING) {
                spring_x = x;
                spring_z = z;
                break;
            }
        }
        if (spring_x >= 0) break;
    }

    if (spring_x < 0) {
        printf("No spring found in test world\n");
        world_destroy(world);
        return;
    }

    printf("Found spring at (%d, 0, %d)\n", spring_x, spring_z);

    // Check initial state
    Voxel* spring_voxel = world_get_voxel(world, spring_x, 0, spring_z);
    Voxel* above_voxel = world_get_voxel(world, spring_x, 1, spring_z);

    printf("Initial state:\n");
    printf("  Spring voxel type: %d\n", spring_voxel ? spring_voxel->type : -1);
    printf("  Above voxel type: %d\n", above_voxel ? above_voxel->type : -1);

    // Remove voxel above spring
    if (above_voxel && above_voxel->type != VOXEL_AIR) {
        printf("Removing voxel above spring...\n");
        world_set_voxel(world, spring_x, 1, spring_z, VOXEL_AIR);

        // Update springs
        world_update_springs(world, 0);

        // Check if water was generated
        Voxel* new_spring_voxel = world_get_voxel(world, spring_x, 0, spring_z);
        Voxel* new_above_voxel = world_get_voxel(world, spring_x, 1, spring_z);

        printf("After removing voxel above:\n");
        printf("  Spring voxel type: %d\n", new_spring_voxel ? new_spring_voxel->type : -1);
        printf("  Above voxel type: %d\n", new_above_voxel ? new_above_voxel->type : -1);

        if (new_spring_voxel && new_spring_voxel->type == VOXEL_WATER) {
            printf("✓ Water generated in spring when voxel above was removed\n");
        } else {
            printf("✗ No water generated in spring\n");
        }
    } else {
        printf("Voxel above spring is already air\n");
    }

    world_destroy(world);
}

// Test vertical water movement
void test_vertical_water_movement() {
    printf("\nTesting vertical water movement...\n");

    // Create a small test world
    World* world = world_create(8, 8, 8);
    if (!world) {
        printf("Failed to create test world\n");
        return;
    }

    // Manually place a spring at bedrock level
    world_set_voxel(world, 4, 0, 4, VOXEL_SPRING);

    // Fill spring with water
    world_set_voxel(world, 4, 0, 4, VOXEL_WATER);

    // Remove voxel above spring
    world_set_voxel(world, 4, 1, 4, VOXEL_AIR);

    printf("Initial state:\n");
    printf("  Spring (4,0,4): %d\n", world_get_voxel(world, 4, 0, 4)->type);
    printf("  Above (4,1,4): %d\n", world_get_voxel(world, 4, 1, 4)->type);

    // Update springs to push water upward
    world_update_springs(world, 0);

    printf("After spring update:\n");
    printf("  Spring (4,0,4): %d\n", world_get_voxel(world, 4, 0, 4)->type);
    printf("  Above (4,1,4): %d\n", world_get_voxel(world, 4, 1, 4)->type);

    // Check if water moved vertically upward
    Voxel* above_voxel = world_get_voxel(world, 4, 1, 4);
    if (above_voxel && above_voxel->type == VOXEL_WATER) {
        printf("✓ Water moved vertically upward\n");
    } else {
        printf("✗ Water did not move vertically upward\n");
    }

    world_destroy(world);
}

int main() {
    printf("Testing Spring Bedrock Placement and Water Generation\n");
    printf("==================================================\n\n");

    test_spring_bedrock_placement();
    test_spring_water_generation();
    test_vertical_water_movement();

    printf("\nTest completed.\n");
    return 0;
}
