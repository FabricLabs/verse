#ifndef SIMPLE_WORLD_H
#define SIMPLE_WORLD_H

#include <stdint.h>
#include <stdbool.h>

// Simplified voxel types for demo
typedef enum {
    VOXEL_AIR = 0,
    VOXEL_BEDROCK,
    VOXEL_STONE,
    VOXEL_SOIL,
    VOXEL_GRASS,
    VOXEL_WOOD,
    VOXEL_LEAVES,
    VOXEL_BRICK,
    VOXEL_COUNT
} SimpleVoxelType;

// Simplified voxel structure
typedef struct {
    SimpleVoxelType type;
} SimpleVoxel;

// Simplified world structure
typedef struct {
    uint32_t width, height, depth;
    SimpleVoxel *voxels;
} SimpleWorld;

// Function prototypes
SimpleWorld* simple_world_create(uint32_t width, uint32_t height, uint32_t depth);
void simple_world_destroy(SimpleWorld *world);
SimpleVoxel* simple_world_get_voxel(SimpleWorld *world, uint32_t x, uint32_t y, uint32_t z);
void simple_world_set_voxel(SimpleWorld *world, uint32_t x, uint32_t y, uint32_t z, SimpleVoxelType type);
void simple_world_voxel_type_color(SimpleVoxelType type, uint8_t *r, uint8_t *g, uint8_t *b);

#endif // SIMPLE_WORLD_H
