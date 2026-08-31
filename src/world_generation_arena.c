/*
 * world_generation_arena.c - ARENA world generator
 *
 * Generates a world with limestone bottom half and a carved hemisphere
 * for combat/arena gameplay.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "world_internal.h"
#include "world_generation.h"
#include "world_voxel.h"
#include "voxel.h"

// Arena generator: Limestone bottom with carved hemisphere
void world_generate_arena(World* world, const char* seed) {
    (void)seed;
    if (!world || !world->voxels)
        return;

    // Clear everything to AIR first
    world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                     VOXEL_AIR, BULK_OP_REPLACE, NULL, NULL);

    const uint32_t half_z = world->depth / 2u;

    // Fill bottom half with limestone
    world_fill_region(world, 0, 0, 0, world->width, world->height, half_z,
                     VOXEL_STONE_LIMESTONE, BULK_OP_REPLACE, NULL, NULL);

    // Carve a centered lower hemisphere using half-sphere subtractive operation
    const uint32_t center_x = world->width / 2;
    const uint32_t center_y = world->height / 2;
    const uint32_t center_z = world->depth / 2;
    const uint32_t radius = fmin(fmin(world->width, world->height), world->depth) / 4;

    // Use subtractive mode to carve out the hemisphere
    world_fill_half_sphere(world, center_x, center_y, center_z, radius,
                          VOXEL_AIR, false, 1.0f, BULK_OP_SUBTRACTIVE, NULL, NULL);
}
