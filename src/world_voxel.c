/*
 * world_voxel.c - Basic voxel operations for world management
 *
 * This module provides fundamental voxel get/set operations,
 * type queries, and voxel property access.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "world_internal.h"
#include "world_voxel.h"

// Defined in world.c; used so render transparency matches foliage / sparse-material classification.
bool world_voxel_type_is_foliage(VoxelType type);
bool world_voxel_type_has_material_gaps(VoxelType type);

// Check if a position is within the world bounds
bool world_is_position_valid(const World *world, uint32_t x, uint32_t y, uint32_t z) {
    return world &&
           x < world->width &&
           y < world->height &&
           z < world->depth;
}

// Get a voxel at a specific position
Voxel* world_get_voxel(World *world, uint32_t x, uint32_t y, uint32_t z) {
    if (!world_is_position_valid(world, x, y, z)) {
        return NULL;
    }

    return &world->voxels[world_index(world, x, y, z)];
}

// Set a voxel at a specific position
bool world_set_voxel(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType type) {
    if (!world_is_position_valid(world, x, y, z)) {
        return false;
    }

    // Allow VOXEL_WORLD (255) as a special case, but validate other types
    if (type != VOXEL_WORLD && type >= VOXEL_COUNT) {
        return false;
    }

    size_t index = world_index(world, x, y, z);
    VoxelType prev = world->voxels[index].type;
    world->voxels[index].type = type;

    // Mark occupancy cache as dirty
    if (world->occupancy_cache) {
        world->occupancy_dirty = true;
    }

    (void)prev; // Unused for now, but kept for future use
    return true;
}

// Voxel type queries
bool world_is_air(World *world, uint32_t x, uint32_t y, uint32_t z) {
    Voxel *v = world_get_voxel(world, x, y, z);
    return v && v->type == VOXEL_AIR;
}

bool world_is_solid(World *world, uint32_t x, uint32_t y, uint32_t z) {
    Voxel *v = world_get_voxel(world, x, y, z);
    if (!v) return false;

    // Air and liquids are not solid
    switch (v->type) {
        case VOXEL_AIR:
        case VOXEL_WATER:
        case VOXEL_MAGMA:
        case VOXEL_STEAM:
            return false;
        default:
            return true;
    }
}

bool world_is_liquid(World *world, uint32_t x, uint32_t y, uint32_t z) {
    Voxel *v = world_get_voxel(world, x, y, z);
    return v && (v->type == VOXEL_WATER || v->type == VOXEL_MAGMA);
}

// Voxel metadata operations
void world_set_voxel_metadata(World *world, uint32_t x, uint32_t y, uint32_t z, uint8_t metadata) {
    Voxel *v = world_get_voxel(world, x, y, z);
    if (v) {
        // Store in lower 8 bits of data8
        v->data8 = (v->data8 & 0xFFFFFFFFFFFFFF00ULL) | metadata;
    }
}

uint8_t world_get_voxel_metadata(World *world, uint32_t x, uint32_t y, uint32_t z) {
    Voxel *v = world_get_voxel(world, x, y, z);
    return v ? (uint8_t)(v->data8 & 0xFF) : 0;
}

// Helper function to extract RGB values from hex color constant
static void extract_rgb_from_hex(uint32_t hex_color, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (uint8_t)((hex_color >> 16) & 0xFF);
    *g = (uint8_t)((hex_color >> 8) & 0xFF);
    *b = (uint8_t)(hex_color & 0xFF);
}

// Get RGB color for voxel type
void world_voxel_type_color(VoxelType t, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t rr = 64, gg = 64, bb = 64;

    switch (t) {
    case VOXEL_AIR:
        extract_rgb_from_hex(VOXEL_COLOR_AIR, &rr, &gg, &bb);
        break;
    case VOXEL_BEDROCK:
        extract_rgb_from_hex(VOXEL_COLOR_BEDROCK, &rr, &gg, &bb);
        break;
    case VOXEL_ACTOR:
        extract_rgb_from_hex(VOXEL_COLOR_ACTOR, &rr, &gg, &bb);
        break;
    case VOXEL_STONE:
        extract_rgb_from_hex(VOXEL_COLOR_STONE, &rr, &gg, &bb);
        break;
    case VOXEL_STONE_BASALT:
        extract_rgb_from_hex(VOXEL_COLOR_STONE_BASALT, &rr, &gg, &bb);
        break;
    case VOXEL_STONE_GRANITE:
        extract_rgb_from_hex(VOXEL_COLOR_STONE_GRANITE, &rr, &gg, &bb);
        break;
    // VOXEL_STONE_MARBLE not defined, skip
    case VOXEL_STONE_LIMESTONE:
        extract_rgb_from_hex(VOXEL_COLOR_STONE_LIMESTONE, &rr, &gg, &bb);
        break;
    case VOXEL_STONE_SANDSTONE:
        extract_rgb_from_hex(VOXEL_COLOR_STONE_SANDSTONE, &rr, &gg, &bb);
        break;
    case VOXEL_SOIL:
        extract_rgb_from_hex(VOXEL_COLOR_SOIL, &rr, &gg, &bb);
        break;
    case VOXEL_SOIL_CLAY:
        extract_rgb_from_hex(VOXEL_COLOR_SOIL_CLAY, &rr, &gg, &bb);
        break;
    // VOXEL_SOIL_PEAT not defined, skip
    // VOXEL_SOIL_SAND not defined, skip
    case VOXEL_SOIL_SILT:
        extract_rgb_from_hex(VOXEL_COLOR_SOIL_SILT, &rr, &gg, &bb);
        break;
    // VOXEL_SOIL_GRAVEL not defined, skip
    case VOXEL_ORE_IRON:
        extract_rgb_from_hex(VOXEL_COLOR_ORE_IRON, &rr, &gg, &bb);
        break;
    case VOXEL_ORE_COPPER:
        extract_rgb_from_hex(VOXEL_COLOR_ORE_COPPER, &rr, &gg, &bb);
        break;
    case VOXEL_ORE_SILVER:
        extract_rgb_from_hex(VOXEL_COLOR_ORE_SILVER, &rr, &gg, &bb);
        break;
    case VOXEL_ORE_GOLD:
        extract_rgb_from_hex(VOXEL_COLOR_ORE_GOLD, &rr, &gg, &bb);
        break;
    case VOXEL_CRYSTAL:
        extract_rgb_from_hex(VOXEL_COLOR_CRYSTAL, &rr, &gg, &bb);
        break;
    case VOXEL_CRYSTAL_RED:
        extract_rgb_from_hex(VOXEL_COLOR_CRYSTAL_RED, &rr, &gg, &bb);
        break;
    case VOXEL_CRYSTAL_GREEN:
        extract_rgb_from_hex(VOXEL_COLOR_CRYSTAL_GREEN, &rr, &gg, &bb);
        break;
    case VOXEL_CRYSTAL_BLUE:
        extract_rgb_from_hex(VOXEL_COLOR_CRYSTAL_BLUE, &rr, &gg, &bb);
        break;
    case VOXEL_WOOD:
        extract_rgb_from_hex(VOXEL_COLOR_WOOD, &rr, &gg, &bb);
        break;
    // VOXEL_WOOD_LEAVES not defined, skip
    case VOXEL_WATER:
        extract_rgb_from_hex(VOXEL_COLOR_WATER, &rr, &gg, &bb);
        break;
    case VOXEL_MAGMA:
        extract_rgb_from_hex(VOXEL_COLOR_MAGMA, &rr, &gg, &bb);
        break;
    case VOXEL_SPRING:
        extract_rgb_from_hex(VOXEL_COLOR_SPRING, &rr, &gg, &bb);
        break;
    case VOXEL_STEAM:
        extract_rgb_from_hex(VOXEL_COLOR_STEAM, &rr, &gg, &bb);
        break;
    case VOXEL_WORLD:
        extract_rgb_from_hex(VOXEL_COLOR_WORLD, &rr, &gg, &bb);
        break;
    default:
        rr = 255; gg = 0; bb = 255; // Magenta for unknown
        break;
    }

    if (r) *r = rr;
    if (g) *g = gg;
    if (b) *b = bb;
}

// Get mass for voxel type (in kg)
float world_voxel_type_mass(VoxelType t) {
    return voxel_type_mass_kg(t);
}

// Check if voxel type is transparent to rendering (does not occlude neighbour faces).
// Keep in sync with voxel_is_transparent_type in voxel_render_common.h.
bool world_voxel_type_is_transparent(VoxelType t) {
    if (t == VOXEL_AIR || t == VOXEL_WATER || t == VOXEL_STEAM || t == VOXEL_GAS || t == VOXEL_ICE ||
        t == VOXEL_OIL)
        return true;
    return world_voxel_type_has_material_gaps(t);
}

// Check if voxel type is a source block
bool world_voxel_type_is_source(VoxelType t) {
    return t == VOXEL_SPRING;
}

// Region operations
bool world_fill_region(World *world, uint32_t x1, uint32_t y1, uint32_t z1,
                      uint32_t x2, uint32_t y2, uint32_t z2, VoxelType type) {
    if (!world) return false;

    // Ensure x1 <= x2, y1 <= y2, z1 <= z2
    if (x1 > x2) { uint32_t tmp = x1; x1 = x2; x2 = tmp; }
    if (y1 > y2) { uint32_t tmp = y1; y1 = y2; y2 = tmp; }
    if (z1 > z2) { uint32_t tmp = z1; z1 = z2; z2 = tmp; }

    // Clamp to world bounds
    if (x1 >= world->width) return false;
    if (y1 >= world->height) return false;
    if (z1 >= world->depth) return false;

    if (x2 >= world->width) x2 = world->width - 1;
    if (y2 >= world->height) y2 = world->height - 1;
    if (z2 >= world->depth) z2 = world->depth - 1;

    for (uint32_t z = z1; z <= z2; z++) {
        for (uint32_t y = y1; y <= y2; y++) {
            for (uint32_t x = x1; x <= x2; x++) {
                world_set_voxel(world, x, y, z, type);
            }
        }
    }

    return true;
}

bool world_clear_region(World *world, uint32_t x1, uint32_t y1, uint32_t z1,
                       uint32_t x2, uint32_t y2, uint32_t z2) {
    return world_fill_region(world, x1, y1, z1, x2, y2, z2, VOXEL_AIR);
}

// Neighbor queries
int world_count_neighbors(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType type) {
    if (!world) return 0;

    int count = 0;

    // Check all 6 face neighbors
    if (x > 0) {
        Voxel *v = world_get_voxel(world, x - 1, y, z);
        if (v && v->type == type) count++;
    }
    if (x < world->width - 1) {
        Voxel *v = world_get_voxel(world, x + 1, y, z);
        if (v && v->type == type) count++;
    }
    if (y > 0) {
        Voxel *v = world_get_voxel(world, x, y - 1, z);
        if (v && v->type == type) count++;
    }
    if (y < world->height - 1) {
        Voxel *v = world_get_voxel(world, x, y + 1, z);
        if (v && v->type == type) count++;
    }
    if (z > 0) {
        Voxel *v = world_get_voxel(world, x, y, z - 1);
        if (v && v->type == type) count++;
    }
    if (z < world->depth - 1) {
        Voxel *v = world_get_voxel(world, x, y, z + 1);
        if (v && v->type == type) count++;
    }

    return count;
}

bool world_has_neighbor(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType type) {
    return world_count_neighbors(world, x, y, z, type) > 0;
}
