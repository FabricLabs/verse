#ifndef VERSE_ENGINE_H
#define VERSE_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include "world.h"
#include "actor.h"

// Maximum number of worlds and actors
#define MAX_WORLDS 64
#define MAX_ACTORS 256

// Neighbor directions
typedef enum {
    NEIGHBOR_NORTH = 0,
    NEIGHBOR_SOUTH,
    NEIGHBOR_EAST,
    NEIGHBOR_WEST,
    NEIGHBOR_UP,
    NEIGHBOR_DOWN
} NeighborDirection;

// World entry in the engine
typedef struct {
    char id[64];
    World* world;
    char save_file[256];
} WorldEntry;

// Actor entry in the engine
typedef struct {
    char id[64];
    float x;
    float y;
    float z;
    char world_id[64];
} ActorEntry;

// Engine state
typedef struct {
    WorldEntry worlds[MAX_WORLDS];
    int world_count;
    ActorEntry actors[MAX_ACTORS];
    int actor_count;
    char center_world_id[64];
} Engine;

// Engine functions
Engine* engine_create(void);
void engine_destroy(Engine* engine);

// World management
bool engine_add_world_entry(Engine* engine, const char* id, World* world);
World* engine_get_world(Engine* engine, const char* id);
bool engine_remove_world_entry(Engine* engine, const char* id);
bool engine_save_worlds(Engine* engine);
bool engine_load_center_world(Engine* engine);

// Actor management
Actor* engine_find_actor(Engine* engine, const char* id);
Actor* engine_get_actor_by_index(Engine* engine, int index);
int engine_get_actor_count(Engine* engine);
Actor* engine_add_actor(Engine* engine, const char* id, float x, float y, float z, const char* world_id);
Actor* engine_get_actor(Engine* engine, const char* id);
bool engine_remove_actor(Engine* engine, const char* id);
bool engine_load_actors(Engine* engine, const char* filename);
bool engine_save_actors(Engine* engine, const char* filename);
bool engine_update_all_actors(Engine* engine);
void engine_update_actor(Engine* engine, Actor* actor, double delta_time);

// Neighbor world management
bool engine_generate_neighbor(Engine* engine, const char* source_world_id, NeighborDirection direction, const char* seed);
World* engine_get_neighbor(Engine* engine, const char* world_id, NeighborDirection direction);
bool engine_compute_neighbors(Engine* engine, const char* world_id);
bool engine_save_neighbors(Engine* engine, const char* world_id);

#endif // VERSE_ENGINE_H 