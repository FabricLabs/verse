#ifndef WORLD_TRANSITION_H
#define WORLD_TRANSITION_H

#include "world.h"
#include "character.h"
#include <stdbool.h>

// World transition types
typedef enum {
    TRANSITION_FALL_DOWN,      // Player fell through bottom of world
    TRANSITION_FALL_UP,        // Player fell through top of world (rare)
    TRANSITION_PORTAL,         // Player used a portal
    TRANSITION_TELEPORT        // Player was teleported
} WorldTransitionType;

// World transition result
typedef struct {
    bool success;              // Whether transition was successful
    World* target_world;       // Target world (NULL if failed)
    int new_x, new_y, new_z;  // New position in target world
    WorldTransitionType type;  // Type of transition
    char* transition_message;  // Message to display to player
} WorldTransitionResult;

// World transition context
typedef struct {
    World* current_world;      // Current world
    int player_x, player_y, player_z;  // Current player position
    Player* player;            // Player data
    uint64_t transition_time;  // Clock cycle when transition occurred
} WorldTransitionContext;

// Initialize world transition system
void world_transition_init(void);

// Check if player should transition to another world
bool world_transition_should_transition(World* world, int x, int y, int z);

// Perform world transition
WorldTransitionResult world_transition_perform(WorldTransitionContext* context);

// Get adjacent world based on direction
World* world_transition_get_adjacent_world(World* current_world, int direction_x, int direction_y, int direction_z);

// Find safe position in target world
bool world_transition_find_safe_position(World* target_world, int* x, int* y, int* z);

// Update player position and world after transition
void world_transition_update_player(Player* player, World* new_world, int x, int y, int z);

// Log transition for debugging
void world_transition_log(const WorldTransitionContext* context, const WorldTransitionResult* result);

// Set game worlds for transition system
void world_transition_set_game_worlds(GameWorlds* game_worlds);

// Clean up world transition system
void world_transition_cleanup(void);

#endif // WORLD_TRANSITION_H
