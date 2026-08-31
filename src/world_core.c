/*
 * world_core.c - Core world management functions
 *
 * This module provides basic world creation, destruction,
 * and property access functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "world_internal.h"
#include "world_core.h"

// Gravity constants (from world.h)
#define GRAVITY_DEFAULT 9.81f

// Create a new world with specified dimensions
World* world_create(uint32_t width, uint32_t height, uint32_t depth) {
    if (width == 0 || height == 0 || depth == 0) {
        return NULL;
    }

    World* world = (World*)calloc(1, sizeof(World));
    if (!world) {
        return NULL;
    }

    // Set dimensions
    world->width = width;
    world->height = height;
    world->depth = depth;

    // Allocate voxel array
    size_t voxel_count = (size_t)width * height * depth;
    world->voxels = (Voxel*)calloc(voxel_count, sizeof(Voxel));
    if (!world->voxels) {
        free(world);
        return NULL;
    }

    // Initialize all voxels to air
    for (size_t i = 0; i < voxel_count; i++) {
        world->voxels[i].type = VOXEL_AIR;
        world->voxels[i].condition_mask = 0;
        world->voxels[i].data8 = 0;
    }

    // Set default values
    world->version = WORLD_VERSION_CURRENT;
    world->gravity = GRAVITY_DEFAULT;
    world->rarity = 0.5f;
    world->vector_clock = 0;
    world->rng_state = 1;
    world->universe_depth = 0;

    // Initialize empty seed
    memset(world->seed_id, 0, sizeof(world->seed_id));

    return world;
}

// Destroy a world and free all resources
void world_destroy(World* world) {
    if (!world) return;

    // Free voxel array
    if (world->voxels) {
        free(world->voxels);
    }

    // Free log string
    if (world->log) {
        free(world->log);
    }

    // Free runtime metadata arrays
    if (world->flower_bloom_epochs) {
        free(world->flower_bloom_epochs);
    }
    if (world->decay_start_epochs) {
        free(world->decay_start_epochs);
    }

    // Free GPU buffer if present
    if (world->gpu_buffer) {
        // GPU buffer cleanup would go here
    }

    // Free occupancy cache
    if (world->occupancy_cache) {
        free(world->occupancy_cache);
    }

    // Free the world structure
    free(world);
}

// Create an empty world (alias for world_create)
World* world_create_empty(uint32_t width, uint32_t height, uint32_t depth) {
    return world_create(width, height, depth);
}

// World property access functions
uint32_t world_get_width(const World* world) {
    return world ? world->width : 0;
}

uint32_t world_get_height(const World* world) {
    return world ? world->height : 0;
}

uint32_t world_get_depth(const World* world) {
    return world ? world->depth : 0;
}

const char* world_get_seed(const World* world) {
    return world ? world->seed_id : "";
}

float world_get_gravity(const World* world) {
    return world ? world->gravity : GRAVITY_DEFAULT;
}

float world_get_rarity(const World* world) {
    return world ? world->rarity : 0.5f;
}

uint64_t world_get_vector_clock(const World* world) {
    return world ? world->vector_clock : 0;
}

void world_increment_vector_clock(World* world) {
    if (world) {
        world->vector_clock++;
    }
}

// World validation
bool world_is_valid(const World* world) {
    return world &&
           world->voxels &&
           world->width > 0 &&
           world->height > 0 &&
           world->depth > 0;
}

// Universe context management
void world_set_universe_context(World* world, struct Universe* universe,
                               uint64_t x, uint64_t y, uint64_t z) {
    if (!world) return;

    world->universe_context = universe;
    world->universe_x = x;
    world->universe_y = y;
    world->universe_z = z;
}

struct Universe* world_get_universe_context(const World* world) {
    return world ? world->universe_context : NULL;
}

void world_get_universe_position(const World* world,
                                uint64_t* x, uint64_t* y, uint64_t* z) {
    if (!world) return;

    if (x) *x = world->universe_x;
    if (y) *y = world->universe_y;
    if (z) *z = world->universe_z;
}
