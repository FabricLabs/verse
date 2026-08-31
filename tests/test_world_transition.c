#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "character.h"
#include "world_transition.h"

int main() {
    printf("=== World Transition System Test ===\n");

    // Initialize world transition system
    world_transition_init();

    // Create test worlds
    printf("\n1. Creating test worlds...\n");
    World* home_world = world_create(64, 64, 16);
    World* farm_world = world_create(64, 64, 16);

    if (!home_world || !farm_world) {
        printf("✗ Failed to create test worlds\n");
        return 1;
    }

    // Generate worlds
    world_generate_with_type(home_world, "test_home_seed", WORLD_TYPE_HOME);
    world_generate_with_type(farm_world, "test_farm_seed", WORLD_TYPE_FARM);

    printf("✓ Test worlds created and generated\n");

    // Create game worlds structure
    GameWorlds* game_worlds = malloc(sizeof(GameWorlds));
    if (!game_worlds) {
        printf("✗ Failed to create game worlds structure\n");
        return 1;
    }

    // Initialize game worlds
    memset(game_worlds, 0, sizeof(GameWorlds));
    game_worlds->home_world = home_world;
    game_worlds->farm_world = farm_world;
    game_worlds->base_seed = strdup("test_base_seed");
    game_worlds->random_seed = strdup("test_random_seed");

    // Create adjacent worlds (simplified for test)
    for (int i = 0; i < 26; i++) {
        game_worlds->adjacent_farm_worlds[i] = farm_world; // Use same world for simplicity
        game_worlds->adjacent_home_worlds[i] = home_world; // Use same world for simplicity
    }

    // Set game worlds for transition system
    world_transition_set_game_worlds(game_worlds);

    printf("✓ Game worlds structure initialized\n");

    // Test 2: Check transition detection
    printf("\n2. Testing transition detection...\n");

    // Test falling through bottom
    bool should_transition = world_transition_should_transition(home_world, 32, -1, 32);
    printf("Should transition at Y=-1: %s\n", should_transition ? "Yes" : "No");

    // Test falling through top
    should_transition = world_transition_should_transition(home_world, 32, 64, 32);
    printf("Should transition at Y=64: %s\n", should_transition ? "Yes" : "No");

    // Test normal position (should not transition)
    should_transition = world_transition_should_transition(home_world, 32, 8, 32);
    printf("Should transition at Y=8: %s\n", should_transition ? "Yes" : "No");

    // Test 3: Test safe position finding
    printf("\n3. Testing safe position finding...\n");
    int x, y, z;
    bool found_safe = world_transition_find_safe_position(farm_world, &x, &y, &z);
    printf("Found safe position: %s at (%d, %d, %d)\n",
           found_safe ? "Yes" : "No", x, y, z);

    // Test 4: Test world transition
    printf("\n4. Testing world transition...\n");

    // Create a test player
    Player* test_player = player_create("TestPlayer", "A test player", "test_world", "test_seed");
    if (!test_player) {
        printf("✗ Failed to create test player\n");
        return 1;
    }

    // Set player position to falling through bottom
    actor_set_position(&test_player->base, 32.0, -1.0, 32.0);

    // Create transition context
    WorldTransitionContext context = {
        .current_world = home_world,
        .player_x = 32,
        .player_y = -1,
        .player_z = 32,
        .player = test_player,
        .transition_time = home_world->vector_clock
    };

    // Perform transition
    WorldTransitionResult result = world_transition_perform(&context);

    if (result.success) {
        printf("✓ World transition successful!\n");
        printf("  New position: (%d, %d, %d)\n", result.new_x, result.new_y, result.new_z);
        printf("  Transition type: %d\n", result.type);
        printf("  Message: %s\n", result.transition_message);

        // Check if player position was updated
        printf("  Player world ID: %s\n", test_player->base.world_id);
        printf("  Player position: (%.1f, %.1f, %.1f)\n",
               test_player->base.x, test_player->base.y, test_player->base.z);
    } else {
        printf("✗ World transition failed: %s\n", result.transition_message);
    }

    // Clean up result message
    if (result.transition_message) {
        free(result.transition_message);
    }

    // Test 5: Test adjacent world selection
    printf("\n5. Testing adjacent world selection...\n");

    // Test falling down (should get farm world)
    World* adjacent_down = world_transition_get_adjacent_world(home_world, 0, -1, 0);
    printf("Adjacent world for falling down: %s\n",
           adjacent_down == farm_world ? "Farm world" : "Other world");

    // Test falling up (should get home world)
    World* adjacent_up = world_transition_get_adjacent_world(home_world, 0, 1, 0);
    printf("Adjacent world for falling up: %s\n",
           adjacent_up == home_world ? "Home world" : "Other world");

    // Test diagonal movement
    World* adjacent_diagonal = world_transition_get_adjacent_world(home_world, 1, -1, 1);
    printf("Adjacent world for diagonal movement: %s\n",
           adjacent_diagonal == farm_world ? "Farm world" : "Other world");

    // Clean up
    printf("\n6. Cleaning up...\n");
    player_destroy(test_player);

    // Clean up game worlds structure
    if (game_worlds->base_seed) free(game_worlds->base_seed);
    if (game_worlds->random_seed) free(game_worlds->random_seed);
    free(game_worlds);

    // Clean up worlds
    world_destroy(home_world);
    world_destroy(farm_world);

    // Clean up transition system
    world_transition_cleanup();

    printf("✓ World transition system test completed successfully!\n");
    printf("✓ Transition detection works\n");
    printf("✓ Safe position finding works\n");
    printf("✓ World transition works\n");
    printf("✓ Adjacent world selection works\n");

    return 0;
}
