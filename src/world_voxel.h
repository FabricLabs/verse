#ifndef WORLD_VOXEL_H
#define WORLD_VOXEL_H

#include <stdint.h>
#include <stdbool.h>
#include "voxel.h"

// Forward declaration
typedef struct World World;

// Basic voxel operations
Voxel* world_get_voxel(World* world, uint32_t x, uint32_t y, uint32_t z);
bool world_set_voxel(World* world, uint32_t x, uint32_t y, uint32_t z, VoxelType type);

// Voxel type queries
bool world_is_air(World* world, uint32_t x, uint32_t y, uint32_t z);
bool world_is_solid(World* world, uint32_t x, uint32_t y, uint32_t z);
bool world_is_liquid(World* world, uint32_t x, uint32_t y, uint32_t z);

// Voxel metadata operations
void world_set_voxel_metadata(World* world, uint32_t x, uint32_t y, uint32_t z, uint8_t metadata);
uint8_t world_get_voxel_metadata(World* world, uint32_t x, uint32_t y, uint32_t z);

// Voxel color and properties
void world_voxel_type_color(VoxelType t, uint8_t *r, uint8_t *g, uint8_t *b);
float world_voxel_type_mass(VoxelType t);
bool world_voxel_type_is_transparent(VoxelType t);
bool world_voxel_type_is_source(VoxelType t);

// Region operations
bool world_fill_region(World* world, uint32_t x1, uint32_t y1, uint32_t z1,
                      uint32_t x2, uint32_t y2, uint32_t z2, VoxelType type);
bool world_clear_region(World* world, uint32_t x1, uint32_t y1, uint32_t z1,
                       uint32_t x2, uint32_t y2, uint32_t z2);

// Neighbor queries
int world_count_neighbors(World* world, uint32_t x, uint32_t y, uint32_t z, VoxelType type);
bool world_has_neighbor(World* world, uint32_t x, uint32_t y, uint32_t z, VoxelType type);

#endif // WORLD_VOXEL_H
