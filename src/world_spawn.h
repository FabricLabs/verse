#ifndef WORLD_SPAWN_H
#define WORLD_SPAWN_H

#include "world.h"
#include <stdbool.h>

// Spawn position structure
typedef struct {
    int x, y, z;           // Spawn coordinates
    bool is_safe;          // Whether the position is safe to spawn
    char* spawn_reason;    // Reason for spawn position selection
} SpawnPosition;

// Spawn validation result
typedef struct {
    bool valid;            // Whether the spawn position is valid
    bool has_solid_ground; // Whether there's solid ground below
    bool has_clear_space;  // Whether there's clear space above
    bool is_center_tile;   // Whether this is a center tile
    char* validation_msg;  // Validation message
} SpawnValidation;

// Initialize spawn system
void world_spawn_init(void);

// Get the true center of a world (ensures single tile, not corner intersection)
void world_get_center_position(World* world, int* center_x, int* center_z);

// Find the best spawn position in a world
SpawnPosition world_find_best_spawn_position(World* world);
// Deterministic wilderness spawn among 5 candidates; caches to world log/seed if needed
SpawnPosition world_find_wilderness_spawn(World* world, bool reseed_candidates);

// Get fixed spawn position for home world (deterministic)
SpawnPosition world_get_home_spawn_position(World* world);

// Validate a spawn position
SpawnValidation world_validate_spawn_position(World* world, int x, int y, int z);

// Find safe ground at a specific X,Y coordinate (Z-vertical system)
int world_find_safe_ground(World* world, int x, int y);

// Check if a position is a center tile (not corner intersection)
bool world_is_center_tile(World* world, int x, int z);

// Find the nearest safe spawn position to a given horizontal coordinate
SpawnPosition world_find_nearest_safe_spawn(World* world, int target_x, int target_y);

// Clean up spawn system
void world_spawn_cleanup(void);

#endif // WORLD_SPAWN_H
