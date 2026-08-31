#ifndef WORLD_INTERNAL_H
#define WORLD_INTERNAL_H

/*
 * Internal header for world module implementation.
 * This header should ONLY be included by world_*.c files.
 * External code should use the public headers (world_core.h, etc.)
 */

#include <stdint.h>
#include <stdbool.h>
#include "voxel.h"
#include "universe_context.h"
#include "world_generation.h"

// Include public headers to ensure consistency
#include "world_core.h"
#include "world_voxel.h"

// Forward declaration
typedef struct GpuVoxelBuffer GpuVoxelBuffer;

// Complete world structure (internal use only)
struct World {
    uint16_t version;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    float gravity;    // Gravity in m/s² (deterministic from seed)
    float rarity;     // 0..1 global rarity budget
    char seed_id[65]; // Hex seed identifier
    char *log;        // Text-based log
    Voxel *voxels;
    uint32_t generation_type; // WorldType enum value
    uint64_t vector_clock; // Vector clock for state transitions
    uint32_t rng_state;    // Per-world deterministic RNG state
    int32_t universe_depth; // Z in universe grid

    // Computed world properties
    uint32_t history_event_count;
    uint32_t unique_player_count;
    uint32_t base_level;
    double score;
    double level;

    // Runtime metadata (not serialized)
    uint32_t *flower_bloom_epochs;
    uint32_t *decay_start_epochs;

    // Universe context
    struct Universe* universe_context;
    uint64_t universe_x, universe_y, universe_z;

    // Performance optimization
    GpuVoxelBuffer* gpu_buffer;
    uint8_t* occupancy_cache;
    bool occupancy_dirty;
};

// Internal utility functions shared between world modules
static inline uint32_t world_index(const World* world,
                                  uint32_t x, uint32_t y, uint32_t z) {
    return x + y * world->width + z * world->width * world->height;
}

static inline bool world_in_bounds(const World* world,
                                  int32_t x, int32_t y, int32_t z) {
    return x >= 0 && x < (int32_t)world->width &&
           y >= 0 && y < (int32_t)world->height &&
           z >= 0 && z < (int32_t)world->depth;
}

static inline Voxel* world_get_voxel_unsafe(World* world,
                                           uint32_t x, uint32_t y, uint32_t z) {
    return &world->voxels[world_index(world, x, y, z)];
}

// Shared constants
#define WORLD_VERSION_CURRENT 1
#define WORLD_CHUNK_SIZE 16

#endif // WORLD_INTERNAL_H
