#ifndef WORLD_CORE_H
#define WORLD_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "voxel.h"
#include "universe_context.h"

// Forward declaration
typedef struct World World;

// World creation and destruction
World* world_create(uint32_t width, uint32_t height, uint32_t depth);
void world_destroy(World* world);
World* world_create_empty(uint32_t width, uint32_t height, uint32_t depth);

// World property access
uint32_t world_get_width(const World* world);
uint32_t world_get_height(const World* world);
uint32_t world_get_depth(const World* world);
const char* world_get_seed(const World* world);
float world_get_gravity(const World* world);
float world_get_rarity(const World* world);
uint64_t world_get_vector_clock(const World* world);
void world_increment_vector_clock(World* world);

// World validation
bool world_is_valid(const World* world);
bool world_is_position_valid(const World* world, uint32_t x, uint32_t y, uint32_t z);

// Universe context
void world_set_universe_context(World* world, struct Universe* universe,
                               uint64_t x, uint64_t y, uint64_t z);
struct Universe* world_get_universe_context(const World* world);
void world_get_universe_position(const World* world,
                                uint64_t* x, uint64_t* y, uint64_t* z);

#endif // WORLD_CORE_H
