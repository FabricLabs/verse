#ifndef WORLD_GENERATION_H
#define WORLD_GENERATION_H

#include <stdint.h>
#include <stdbool.h>
#include "voxel.h"

// Forward declaration
typedef struct World World;

// World generation type enumeration
typedef enum {
    WORLD_TYPE_UNKNOWN = 0,
    WORLD_TYPE_HOME,
    WORLD_TYPE_FARM,
    WORLD_TYPE_RANDOM,
    WORLD_TYPE_WILDERNESS,
    WORLD_TYPE_SOLID,
    WORLD_TYPE_UNDERWORLD,
    WORLD_TYPE_SCOURED,
    WORLD_TYPE_LABYRINTH_SQUARE,
    WORLD_TYPE_WFC_TOWN,
    WORLD_TYPE_CLOUD,
    WORLD_TYPE_ARENA,
    WORLD_TYPE_COUNT
} WorldType;

// Main generation function - dispatches to specific generators
void world_generate(World* world, const char* seed);
void world_generate_with_type(World* world, const char* seed, WorldType type);
void world_generate_with_type_and_fill(World* world, const char* seed, WorldType type, VoxelType fill_type);

// Individual world generators
void world_generate_home(World* world, const char* seed);
void world_generate_farm(World* world, const char* seed);
void world_generate_random(World* world, const char* seed);
void world_generate_wilderness(World* world, const char* seed);
void world_generate_solid_fill(World* world, VoxelType fill_type);
void world_generate_underworld(World* world, const char* seed);
void world_generate_scoured(World* world, const char* seed);
void world_generate_labyrinth(World* world, const char* seed);
void world_generate_wfc_town(World* world, const char* seed);
void world_generate_cloud(World* world, const char* seed);
void world_generate_arena(World* world, const char* seed);

// Helper functions for generation
bool world_try_plant_oak_Z(World* world, int x, int y, int ground_z);
bool world_try_plant_oak_Y(World* world, int x, int ground_y, int z);
bool world_try_plant_birch_Z(World* world, int x, int y, int ground_z);
bool world_try_plant_birch_Y(World* world, int x, int ground_y, int z);
bool world_try_plant_pine_Z(World* world, int x, int y, int ground_z);
bool world_try_plant_pine_Y(World* world, int x, int ground_y, int z);
bool world_try_place_any_tree_at(World* world, uint32_t x, uint32_t ground_y, uint32_t z);

// Utility functions for world generation
int seeded_rand_range(int max);
void seed_rand_with_world_seed(const char* seed);
void world_append_log(World* world, const char* message);
WorldType world_type_from_string(const char* type_str);
const char* world_type_to_string(WorldType type);

#endif // WORLD_GENERATION_H
