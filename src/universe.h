#ifndef UNIVERSE_H
#define UNIVERSE_H

#include <stdint.h>
#include <stdbool.h>
#include "world.h"
#include "wfc3d.h"
#include "chronicle.h"

// Tagged so that `struct Universe` forward declarations (world.h, universe_context.h)
// name this same type; with an untagged typedef the two are unrelated types.
typedef struct Universe {
  char seed[65];
  int affinity;        // placement bias toward origin
  uint32_t min_gap;    // minimum Chebyshev distance between worlds
  // history log
  char* log;
  uint64_t vector_clock;
  uint32_t history_event_count;
  // Deterministic civilization / epoch chronicle (rebuild from seed).
  Chronicle chronicle;
  bool chronicle_ready;
  // simple open addressing hash map for (x,y,z)->World*
  struct Entry { uint64_t x,y,z; World* w; bool used; } *entries;
  size_t capacity;
  size_t count;
} Universe;

// Event type bitmask (extendable)
typedef enum {
  UE_NONE        = 0,
  UE_GENESIS     = 1u << 0,
  UE_SET_SEED    = 1u << 1,
  UE_PLACE_WORLD = 1u << 2,
} UniverseEventMask;

bool universe_init(Universe* u, const char* seed_hex, int affinity, uint32_t min_gap);
void universe_free(Universe* u);
bool universe_has(const Universe* u, uint64_t x, uint64_t y, uint64_t z);
World* universe_get(const Universe* u, uint64_t x, uint64_t y, uint64_t z);
bool universe_place(Universe* u, uint64_t x, uint64_t y, uint64_t z, World* w);

// Drops a cell from the map and hands its world back; the caller becomes responsible for freeing it.
// Needed for eviction: without it the universe only ever grows, and 100 worlds is 9.6GB.
bool universe_remove(Universe* u, uint64_t x, uint64_t y, uint64_t z, World** out_world);

void universe_append_event(Universe* u, const char* type, const char* json_patch);

// Create universe context for a standalone world
bool universe_create_for_world(Universe* u, World* w, const char* seed);

// Ensure seed consistency when adding worlds to universe
bool universe_ensure_seed_consistency(Universe* u, World* w, uint64_t x, uint64_t y, uint64_t z);

// Replay GLOBAL.log into a fresh Universe by decoding hex and applying events
// Returns true on success; stops at first malformed record
bool universe_replay_from_global_log(Universe* u, const char* log_path);

// pick a location near origin (0,0,0) with min_gap constraints; returns false if not found by search
bool universe_find_player_wilderness_location(const Universe* u,
                                              uint64_t* out_x, uint64_t* out_y, uint64_t* out_z);

// adjacency placement relative to a base location; errors if occupied
bool universe_place_adjacent(Universe* u,
                             uint64_t base_x, uint64_t base_y, uint64_t base_z,
                             int dx, int dy, int dz,
                             World* w);

// Universe WFC-based generator interface (mirrors world.c flow)
typedef enum {
  UNIVERSE_GEN_GAMEWORLD = 0,
  UNIVERSE_GEN_ARENA     = 1
} UniverseGeneratorType;

bool universe_wfc_decide_cell(UniverseGeneratorType gen,
                              int gx, int gy, int gz,
                              WorldGenerationType* out_type,
                              VoxelType* out_fill);

// Generate and place neighbor worlds around a base location using the WFC tileset.
// - Generates 4 cardinals and 4 diagonals on the same Z as base (if include_diagonals=true).
// - If include_clouds=true, also generates a cloud layer (+Z) above the base and above each neighbor.
// - Newly created worlds inherit dimensions from base_world and have aligned noise domains so borders tile seamlessly.
// Returns true if at least one neighbor was created/placed.
bool universe_generate_neighbors_wfc_around(
    Universe* u,
    const char* base_seed,
    uint64_t base_x, uint64_t base_y, uint64_t base_z,
    World* base_world,
    UniverseGeneratorType generator,
    bool include_diagonals,
    bool include_clouds);

// Generate a Chebyshev-square of worlds of radius R around base (excluding base when R>0),
// using the WFC tileset policy. Also optionally generates a cloud layer at +Z for each cell.
// Returns true if any worlds were created/placed.
bool universe_generate_wfc_square_around(
    Universe* u,
    const char* base_seed,
    uint64_t base_x, uint64_t base_y, uint64_t base_z,
    World* base_world,
    UniverseGeneratorType generator,
    int radius,
    bool include_clouds);

#endif


