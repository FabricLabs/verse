/*
 * world_physics.c - Physics simulation for world management
 *
 * This module handles physics calculations including gravity,
 * fluid dynamics, heat transfer, and spring mechanics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "world_internal.h"
#include "world_physics.h"
#include "world_voxel.h"
#include "voxel.h"

// Forward declarations
static void world_generate_spring_water(World* world, uint32_t x, uint32_t y, uint32_t z);

// Constants
#define GRAVITY_DEFAULT 20.0f
// Use the value from voxel.h

// Fast inline helpers for physics calculations
static inline bool world_pos_in_bounds_fast(const World* world, int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 &&
           x < (int)world->width && y < (int)world->height && z < (int)world->depth;
}

static inline int world_height_at_fast(World* world, int x, int y) {
    if (x < 0 || y < 0 || x >= (int)world->width || y >= (int)world->height)
        return -1;

    // Find highest non-air voxel
    for (int z = (int)world->depth - 1; z >= 0; z--) {
        Voxel* v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (v && v->type != VOXEL_AIR) {
            return z;
        }
    }
    return -1;
}

// Note: world_get_gravity is defined in world_core.c

// Step simple actor physics under gravity for dt seconds
// Note: This is a stub - actual actor management would be handled elsewhere
void world_step_actors(World* world, float dt_seconds) {
    // Actors are managed externally, not stored in the world structure
    // This function is kept for API compatibility
    (void)world;
    (void)dt_seconds;
}

// Generate water from a spring (only when voxel above is air)
static void world_generate_spring_water(World* world, uint32_t x, uint32_t y, uint32_t z) {
    // Check if spring is already full of water
    Voxel* spring_voxel = world_get_voxel(world, x, y, z);
    if (spring_voxel && spring_voxel->type == VOXEL_WATER) {
        // Spring is full, try to push water
        world_push_spring_water(world, x, y, z);
    }
    else if (spring_voxel && spring_voxel->type == VOXEL_SPRING) {
        // Spring is empty, fill it with water
        world_set_voxel(world, x, y, z, VOXEL_WATER);
        Voxel* w = world_get_voxel(world, x, y, z);
        if (w)
            voxel_set_quantity(w, 0xFF);
    }
}

// Push water from a full spring to adjacent positions
void world_push_spring_water(World* world, uint32_t x, uint32_t y, uint32_t z) {
    // Create deterministic random sequence based on world state
    uint32_t hash_input = x * 73856093 + y * 19349663 + z * 83492791 + world->vector_clock;
    uint32_t hash = hash_input ^ (hash_input >> 13);
    hash = hash ^ (hash << 17);
    hash = hash ^ (hash >> 5);

    // Try positions in order: below, diagonal down, horizontal
    int directions[][3] = {
        {0, -1, 0},   // Below
        {-1, -1, -1}, {1, -1, -1}, {-1, -1, 1}, {1, -1, 1}, // Diagonal down
        {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, // Horizontal
        {0, 1, 0}     // Above (rarely)
    };

    for (int i = 0; i < 10; i++) {
        int new_x = (int)x + directions[i][0];
        int new_y = (int)y + directions[i][1];
        int new_z = (int)z + directions[i][2];

        if (world_is_position_valid(world, (uint32_t)new_x, (uint32_t)new_y, (uint32_t)new_z)) {
            Voxel* target_voxel = world_get_voxel(world, (uint32_t)new_x, (uint32_t)new_y, (uint32_t)new_z);
            if (target_voxel && target_voxel->type == VOXEL_AIR) {
                // Found empty space, place water
                world_set_voxel(world, (uint32_t)new_x, (uint32_t)new_y, (uint32_t)new_z, VOXEL_WATER);
                break;
            }
        }
    }
}

// Lightweight cellular automaton fluid step
// - Pulls water down into AIR or less-full water
// - Spreads laterally with quantity splits
// - Merges quantities to avoid fragmentation
// max_cells limits per-step work to stay realtime
void world_step_fluids(World* world, int max_cells) {
    if (!world || !world->voxels || max_cells <= 0)
        return;

    const int w = (int)world->width;
    const int h = (int)world->height;
    const int d = (int)world->depth;

    // Heat transfer from magma: 0..6 heat value, +1 per tick to all 6-neighbors (clamped)
    {
        const int dirs6[6][3] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
        for (int z = 0; z < d; z++) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    Voxel* v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                    if (!v || v->type != VOXEL_MAGMA)
                        continue;

                    for (int i = 0; i < 6; i++) {
                        int nx = x + dirs6[i][0];
                        int ny = y + dirs6[i][1];
                        int nz = z + dirs6[i][2];

                        if (nx < 0 || ny < 0 || nz < 0 || nx >= w || ny >= h || nz >= d)
                            continue;

                        Voxel* n = world_get_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz);
                        if (!n)
                            continue;

                        uint8_t hval = voxel_get_heat(n);
                        if (hval < 6)
                            voxel_set_heat(n, (uint8_t)(hval + 1));
                    }
                }
            }
        }
    }

    int budget_left = max_cells;

    // Gravity: drip one unit per tick downward for both WATER and MAGMA
    // Moves exactly 1 unit from a fluid cell to the AIR (or same-fluid q<6) cell directly below.
    {
        const size_t total_cells = (size_t)w * (size_t)h * (size_t)d;
        uint8_t* mark_down = (uint8_t*)calloc(total_cells, 1);
        uint8_t* donor_dec = (uint8_t*)calloc(total_cells, 1);
        VoxelType* mark_type3D = (VoxelType*)malloc(total_cells * sizeof(VoxelType));

        if (!mark_down || !donor_dec || !mark_type3D) {
            free(mark_down);
            free(donor_dec);
            free(mark_type3D);
            return;
        }

        int g_processed = 0;

        // Mark phase: identify downward transfers
        for (int z = d - 2; z >= 1 && g_processed < budget_left; z--) {
            for (int y = 0; y < h && g_processed < budget_left; y++) {
                for (int x = 0; x < w && g_processed < budget_left; x++) {
                    size_t idx = ((size_t)z * (size_t)h + (size_t)y) * (size_t)w + (size_t)x;
                    Voxel* v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                    if (!v)
                        continue;

                    if (!(v->type == VOXEL_WATER || v->type == VOXEL_MAGMA))
                        continue;

                    int nx = x, ny = y, nz = z - 1;
                    if (nz < 0)
                        continue;

                    Voxel* below = world_get_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz);
                    if (!below)
                        continue;

                    if (!(below->type == VOXEL_AIR ||
                          (below->type == v->type && voxel_get_quantity(below) < 6)))
                        continue;

                    size_t bidx = ((size_t)nz * (size_t)h + (size_t)ny) * (size_t)w + (size_t)nx;
                    if (mark_down[bidx])
                        continue; // only one unit per target this tick

                    // Mark one-unit downward transfer
                    mark_down[bidx] = 1;
                    mark_type3D[bidx] = v->type;
                    donor_dec[idx] = 1;
                    g_processed++;
                }
            }
        }

        // Commit phase: apply increments to targets
        for (int z = 0; z < d; z++) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    size_t idx = ((size_t)z * (size_t)h + (size_t)y) * (size_t)w + (size_t)x;
                    if (!mark_down[idx])
                        continue;

                    Voxel* t = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                    if (!t)
                        continue;

                    VoxelType ttype = mark_type3D[idx];
                    if (t->type != ttype) {
                        t->type = ttype;
                        voxel_set_quantity(t, 1);
                    } else {
                        uint8_t q = voxel_get_quantity(t);
                        if (q < 6)
                            voxel_set_quantity(t, (uint8_t)(q + 1));
                    }
                }
            }
        }

        // Commit phase: apply decrements to donors
        for (int z = 0; z < d; z++) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    size_t idx = ((size_t)z * (size_t)h + (size_t)y) * (size_t)w + (size_t)x;
                    if (!donor_dec[idx])
                        continue;

                    Voxel* dv = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                    if (!dv)
                        continue;

                    if (!(dv->type == VOXEL_WATER || dv->type == VOXEL_MAGMA))
                        continue;

                    int q = (int)voxel_get_quantity(dv);
                    int newq = q - 1;

                    if (newq <= 0) {
                        dv->type = VOXEL_AIR;
                        voxel_set_quantity(dv, 0);
                    } else {
                        if (newq > 6)
                            newq = 6; // clamp for stability
                        voxel_set_quantity(dv, (uint8_t)newq);
                    }
                }
            }
        }

        free(mark_down);
        free(donor_dec);
        free(mark_type3D);

        if (budget_left > g_processed)
            budget_left -= g_processed;
        else
            budget_left = 0;
    }

    // Lateral surface fill for water spread
    // This is a simplified version of the complex lateral fill algorithm
    // Full implementation would include column-based surface detection and
    // neighbor-based water spreading

    // Process springs - generate water at spring locations
    for (int z = 0; z < d && budget_left > 0; z++) {
        for (int y = 0; y < h && budget_left > 0; y++) {
            for (int x = 0; x < w && budget_left > 0; x++) {
                Voxel* v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                if (v && v->type == VOXEL_SPRING) {
                    world_generate_spring_water(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                    budget_left--;
                }
            }
        }
    }
}

// Apply gravity to a specific entity
void world_apply_entity_gravity(World* world, float* entity_x, float* entity_y, float* entity_z,
                               float* velocity_z, float dt_seconds, bool is_flying) {
    if (!world || !entity_z || !velocity_z || is_flying)
        return;

    float g = world_get_gravity(world);
    if (g <= 0.0f)
        g = GRAVITY_DEFAULT;
    *velocity_z -= g * dt_seconds;
    *entity_z += *velocity_z * dt_seconds;

    // Ground collision check
    if (entity_x && entity_y) {
        int vx = (int)floor(*entity_x);
        int vy = (int)floor(*entity_y);
        int vz = (int)floor(*entity_z);

        if (world_pos_in_bounds_fast(world, vx, vy, vz)) {
            // Find ground height
            int ground_z = -1;
            for (int z = vz; z >= 0; z--) {
                Voxel* v = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)z);
                if (v && v->type != VOXEL_AIR && v->type != VOXEL_WATER &&
                    v->type != VOXEL_MAGMA && v->type != VOXEL_STEAM) {
                    ground_z = z;
                    break;
                }
            }

            if (ground_z >= 0 && *entity_z <= (float)(ground_z + 1)) {
                *entity_z = (float)(ground_z + 1);
                *velocity_z = 0;
            }
        }
    }
}

// Check if a position would collide with solid voxels
bool world_check_collision(World* world, float x, float y, float z) {
    if (!world)
        return false;

    int vx = (int)floor(x);
    int vy = (int)floor(y);
    int vz = (int)floor(z);

    if (!world_pos_in_bounds_fast(world, vx, vy, vz))
        return true; // Out of bounds is considered collision

    Voxel* v = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)vz);
    if (!v)
        return true;

    // Air, water, magma, and steam are passable
    return !(v->type == VOXEL_AIR || v->type == VOXEL_WATER ||
             v->type == VOXEL_MAGMA || v->type == VOXEL_STEAM);
}

// Get the height of terrain at a specific x,y coordinate
int world_get_terrain_height(World* world, int x, int y) {
    if (!world)
        return -1;

    return world_height_at_fast(world, x, y);
}

// Simple temperature simulation step
void world_step_temperature(World* world, int max_cells) {
    if (!world || !world->voxels || max_cells <= 0)
        return;

    // Temperature dissipation - reduce heat by 1 per tick for non-magma voxels
    int processed = 0;
    for (uint32_t z = 0; z < world->depth && processed < max_cells; z++) {
        for (uint32_t y = 0; y < world->height && processed < max_cells; y++) {
            for (uint32_t x = 0; x < world->width && processed < max_cells; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                if (!v || v->type == VOXEL_MAGMA)
                    continue;

                uint8_t heat = voxel_get_heat(v);
                if (heat > 0) {
                    voxel_set_heat(v, heat - 1);
                    processed++;
                }
            }
        }
    }
}
