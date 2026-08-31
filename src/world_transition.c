#include "world_transition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global state for world transition system
static GameWorlds* g_game_worlds = NULL;
static bool g_transition_system_initialized = false;

// Initialize world transition system
void world_transition_init(void) {
    if (g_transition_system_initialized) {
        return;
    }

    printf("World transition system initialized\n");
    g_transition_system_initialized = true;
}

// Check if player should transition to another world
bool world_transition_should_transition(World* world, int x, int y, int z) {
    if (!world || !g_transition_system_initialized) {
        return false;
    }

    // Check if player has fallen through the bottom of the world
    if (y < 0) {
        return true;
    }

    // Check if player has fallen through the top of the world (rare case)
    if (y >= world->height) {
        return true;
    }

    // Check if player is at world boundaries and there's no solid ground
    if (x < 0 || x >= world->width || z < 0 || z >= world->depth) {
        return true;
    }

    // Check if player is on a portal voxel (VOXEL_WORLD)
    if (y >= 0 && y < world->height && x >= 0 && x < world->width && z >= 0 && z < world->depth) {
        Voxel* voxel = world_get_voxel(world, x, y, z);
        if (voxel && voxel->type == VOXEL_WORLD) {
            return true;
        }
    }

    return false;
}

// Get adjacent world based on direction
World* world_transition_get_adjacent_world(World* current_world, int direction_x, int direction_y, int direction_z) {
    if (!current_world || !g_game_worlds) {
        return NULL;
    }

    // Determine which adjacent world to use based on direction
    // For now, we'll use a simple mapping based on the direction
    int world_index = -1;

    if (direction_y < 0) {
        // Falling down - use adjacent farm world
        // Map direction to adjacent world index (0-25)
        int dx = (direction_x > 0) ? 1 : (direction_x < 0) ? -1 : 0;
        int dz = (direction_z > 0) ? 1 : (direction_z < 0) ? -1 : 0;

        // Convert to index: (dx+1) + (dz+1)*3
        world_index = (dx + 1) + (dz + 1) * 3;

        if (world_index >= 0 && world_index < 26) {
            return g_game_worlds->adjacent_farm_worlds[world_index];
        }
    } else if (direction_y > 0) {
        // Falling up - use adjacent home world
        int dx = (direction_x > 0) ? 1 : (direction_x < 0) ? -1 : 0;
        int dz = (direction_z > 0) ? 1 : (direction_z < 0) ? -1 : 0;

        world_index = (dx + 1) + (dz + 1) * 3;

        if (world_index >= 0 && world_index < 26) {
            return g_game_worlds->adjacent_home_worlds[world_index];
        }
    }

    // Fallback to home world if no adjacent world found
    return g_game_worlds->home_world;
}

// Find safe position in target world
bool world_transition_find_safe_position(World* target_world, int* x, int* y, int* z) {
    if (!target_world || !x || !y || !z) {
        return false;
    }

    // Start from a reasonable height and work down to find solid ground
    int start_y = target_world->height - 10; // Start 10 blocks below top
    if (start_y < 0) start_y = 0;

    // Try to find a safe position near the center of the world
    int center_x = target_world->width / 2;
    int center_z = target_world->depth / 2;

    // Search in a spiral pattern around the center
    int search_radius = 0;
    int max_search_radius = 20; // Limit search area

    while (search_radius <= max_search_radius) {
        for (int dx = -search_radius; dx <= search_radius; dx++) {
            for (int dz = -search_radius; dz <= search_radius; dz++) {
                // Only check the perimeter of the current search radius
                if (abs(dx) == search_radius || abs(dz) == search_radius) {
                    int test_x = center_x + dx;
                    int test_z = center_z + dz;

                    // Check bounds
                    if (test_x < 0 || test_x >= target_world->width ||
                        test_z < 0 || test_z >= target_world->depth) {
                        continue;
                    }

                    // Find solid ground at this position
                    for (int test_y = start_y; test_y >= 0; test_y--) {
                        Voxel* voxel = world_get_voxel(target_world, test_x, test_y, test_z);
                        if (voxel && voxel->type != VOXEL_AIR && voxel->type != VOXEL_WATER) {
                            // Found solid ground, check if position above is safe
                            if (test_y + 1 < target_world->height) {
                                Voxel* above_voxel = world_get_voxel(target_world, test_x, test_y + 1, test_z);
                                if (above_voxel && above_voxel->type == VOXEL_AIR) {
                                    *x = test_x;
                                    *y = test_y + 1; // Position above solid ground
                                    *z = test_z;
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
        search_radius++;
    }

    // Fallback: use center position at a reasonable height
    *x = center_x;
    *y = 8; // Default height
    *z = center_z;
    return true;
}

// Perform world transition
WorldTransitionResult world_transition_perform(WorldTransitionContext* context) {
    WorldTransitionResult result = {0};

    if (!context || !context->current_world || !context->player) {
        result.success = false;
        result.transition_message = strdup("Invalid transition context");
        return result;
    }

    // Determine transition type and direction
    WorldTransitionType transition_type = TRANSITION_FALL_DOWN;
    int direction_x = 0, direction_y = -1, direction_z = 0; // Default: fall down

    if (context->player_y < 0) {
        transition_type = TRANSITION_FALL_DOWN;
        direction_y = -1;
    } else if (context->player_y >= context->current_world->height) {
        transition_type = TRANSITION_FALL_UP;
        direction_y = 1;
    } else if (context->player_x < 0) {
        transition_type = TRANSITION_FALL_DOWN;
        direction_x = -1;
    } else if (context->player_x >= context->current_world->width) {
        transition_type = TRANSITION_FALL_DOWN;
        direction_x = 1;
    } else if (context->player_z < 0) {
        transition_type = TRANSITION_FALL_DOWN;
        direction_z = -1;
    } else if (context->player_z >= context->current_world->depth) {
        transition_type = TRANSITION_FALL_DOWN;
        direction_z = 1;
    }

    // Get target world
    World* target_world = world_transition_get_adjacent_world(context->current_world,
                                                            direction_x, direction_y, direction_z);

    if (!target_world) {
        result.success = false;
        result.transition_message = strdup("No adjacent world available");
        return result;
    }

    // Find safe position in target world
    int new_x, new_y, new_z;
    if (!world_transition_find_safe_position(target_world, &new_x, &new_y, &new_z)) {
        result.success = false;
        result.transition_message = strdup("No safe position found in target world");
        return result;
    }

    // Update player position and world
    world_transition_update_player(context->player, target_world, new_x, new_y, new_z);

    // Set result
    result.success = true;
    result.target_world = target_world;
    result.new_x = new_x;
    result.new_y = new_y;
    result.new_z = new_z;
    result.type = transition_type;

    // Create transition message
    char message[256];
    switch (transition_type) {
        case TRANSITION_FALL_DOWN:
            snprintf(message, sizeof(message),
                    "Fell through to adjacent world! New position: (%d, %d, %d)",
                    new_x, new_y, new_z);
            break;
        case TRANSITION_FALL_UP:
            snprintf(message, sizeof(message),
                    "Rose up to adjacent world! New position: (%d, %d, %d)",
                    new_x, new_y, new_z);
            break;
        case TRANSITION_PORTAL:
            snprintf(message, sizeof(message),
                    "Portal transition! New position: (%d, %d, %d)",
                    new_x, new_y, new_z);
            break;
        case TRANSITION_TELEPORT:
            snprintf(message, sizeof(message),
                    "Teleported! New position: (%d, %d, %d)",
                    new_x, new_y, new_z);
            break;
    }
    result.transition_message = strdup(message);

    // Log the transition
    world_transition_log(context, &result);

    return result;
}

// Update player position and world after transition
void world_transition_update_player(Player* player, World* new_world, int x, int y, int z) {
    if (!player || !new_world) {
        return;
    }

    // Update player position
    actor_set_position(&player->base, (double)x, (double)y, (double)z);

    // Update player's world ID
    // For now, we'll use a simple world identifier
    // In a full implementation, this would be the world's seed or ID
    snprintf(player->base.world_id, sizeof(player->base.world_id), "world_%p", (void*)new_world);

    // Update player's world seed if we have it
    // This would need to be implemented based on how world seeds are stored
    // For now, we'll leave the existing world_seed

    // Increment history event in the new world
    world_increment_history_event(new_world);

    // Add player to unique players list
    char player_id_str[65];
    snprintf(player_id_str, sizeof(player_id_str), "%u", player->base.id);
    world_add_unique_player(new_world, player_id_str);

    printf("Player transitioned to world %s at position (%d, %d, %d)\n",
           player->base.world_id, x, y, z);
}

// Log transition for debugging
void world_transition_log(const WorldTransitionContext* context, const WorldTransitionResult* result) {
    if (!context || !result) {
        return;
    }

    printf("=== World Transition Log ===\n");
    printf("From: World %p at (%d, %d, %d)\n",
           (void*)context->current_world,
           context->player_x, context->player_y, context->player_z);

    if (result->success) {
        printf("To: World %p at (%d, %d, %d)\n",
               (void*)result->target_world,
               result->new_x, result->new_y, result->new_z);
        printf("Type: %d\n", result->type);
        printf("Message: %s\n", result->transition_message);
        printf("Clock cycle: %llu\n", context->transition_time);
    } else {
        printf("Transition failed: %s\n", result->transition_message);
    }
    printf("===========================\n");
}

// Set the game worlds for the transition system
void world_transition_set_game_worlds(GameWorlds* game_worlds) {
    g_game_worlds = game_worlds;
}

// Clean up world transition system
void world_transition_cleanup(void) {
    if (g_transition_system_initialized) {
        printf("World transition system cleaned up\n");
        g_transition_system_initialized = false;
        g_game_worlds = NULL;
    }
}
