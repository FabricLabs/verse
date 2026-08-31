#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "world.h"

// Note: SHA256 implementation is provided by the world.c module

void test_level_system() {
    printf("=== Testing World Level System ===\n\n");

    // Test 1: Brand new world (should be level 0)
    printf("Test 1: Brand new world\n");
    World* new_world = world_create(32, 32, 32);
    if (new_world) {
        world_update_score_and_level(new_world);
        printf("  Base Level: %u\n", world_get_base_level(new_world));
        printf("  Score: %.2f\n", world_get_score(new_world));
        printf("  Level: %.2f\n", world_get_level(new_world));
        printf("  Expected: Level 0 (Epic - 0%% springs)\n\n");
        world_destroy(new_world);
    }

    // Test 2: World with some activity
    printf("Test 2: World with activity\n");
    World* active_world = world_create(32, 32, 32);
    if (active_world) {
        // Simulate some activity
        active_world->vector_clock = 100;
        active_world->history_event_count = 10;
        active_world->unique_player_count = 3;
        active_world->base_level = 1;

        world_update_score_and_level(active_world);
        printf("  Vector Clock: %llu\n", active_world->vector_clock);
        printf("  History Events: %u\n", active_world->history_event_count);
        printf("  Unique Players: %u\n", active_world->unique_player_count);
        printf("  Base Level: %u\n", world_get_base_level(active_world));
        printf("  Score: %.2f\n", world_get_score(active_world));
        printf("  Level: %.2f\n", world_get_level(active_world));
        printf("  Expected: Level > 1 (Very Rare - 6.25%% springs)\n\n");
        world_destroy(active_world);
    }

    // Test 3: High-level world
    printf("Test 3: High-level world\n");
    World* high_level_world = world_create(32, 32, 32);
    if (high_level_world) {
        // Simulate high activity
        high_level_world->vector_clock = 10000;
        high_level_world->history_event_count = 1000;
        high_level_world->unique_player_count = 50;
        high_level_world->base_level = 3;

        world_update_score_and_level(high_level_world);
        printf("  Vector Clock: %llu\n", high_level_world->vector_clock);
        printf("  History Events: %u\n", high_level_world->history_event_count);
        printf("  Unique Players: %u\n", high_level_world->unique_player_count);
        printf("  Base Level: %u\n", world_get_base_level(high_level_world));
        printf("  Score: %.2f\n", world_get_score(high_level_world));
        printf("  Level: %.2f\n", world_get_level(high_level_world));
        printf("  Expected: Level >= 4 (Common - 50%% springs)\n\n");
        world_destroy(high_level_world);
    }

    // Test 4: Test RANDOM world generation with level system
    printf("Test 4: RANDOM world generation with level system\n");
    World* random_world = world_create(32, 32, 32);
    if (random_world) {
        // Set some activity to test level-based spring generation
        random_world->vector_clock = 5000;
        random_world->history_event_count = 500;
        random_world->unique_player_count = 25;
        random_world->base_level = 2;

        world_generate_with_type(random_world, "test_level_world", WORLD_TYPE_RANDOM);

        printf("  World Level: %.2f\n", world_get_level(random_world));
        printf("  World Score: %.2f\n", world_get_score(random_world));

        // Count springs
        int spring_count = 0;
        for (uint32_t z = 0; z < random_world->depth; z++) {
            for (uint32_t y = 0; y < random_world->height; y++) {
                for (uint32_t x = 0; x < random_world->width; x++) {
                    Voxel* voxel = world_get_voxel(random_world, x, y, z);
                    if (voxel && voxel->type == VOXEL_SPRING) {
                        spring_count++;
                    }
                }
            }
        }
        printf("  Springs generated: %d\n", spring_count);

        // Display world log
        if (random_world->log) {
            printf("  World log:\n%s\n", random_world->log);
        }

        world_destroy(random_world);
    }

    printf("=== Level System Test Complete ===\n");
}

int main() {
    test_level_system();
    return 0;
}
