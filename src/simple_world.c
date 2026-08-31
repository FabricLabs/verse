#include "simple_world.h"
#include <stdlib.h>
#include <string.h>

// Create a new simple world
SimpleWorld* simple_world_create(uint32_t width, uint32_t height, uint32_t depth) {
    SimpleWorld *world = malloc(sizeof(SimpleWorld));
    if (!world) return NULL;

    world->width = width;
    world->height = height;
    world->depth = depth;

    size_t voxel_count = width * height * depth;
    world->voxels = calloc(voxel_count, sizeof(SimpleVoxel));
    if (!world->voxels) {
        free(world);
        return NULL;
    }

    // Initialize all voxels as air
    for (size_t i = 0; i < voxel_count; i++) {
        world->voxels[i].type = VOXEL_AIR;
    }

    return world;
}

// Destroy a simple world
void simple_world_destroy(SimpleWorld *world) {
    if (!world) return;

    if (world->voxels) {
        free(world->voxels);
    }
    free(world);
}

// Get voxel at position
SimpleVoxel* simple_world_get_voxel(SimpleWorld *world, uint32_t x, uint32_t y, uint32_t z) {
    if (!world || !world->voxels) return NULL;
    if (x >= world->width || y >= world->height || z >= world->depth) return NULL;

    size_t index = (z * world->height + y) * world->width + x;
    return &world->voxels[index];
}

// Set voxel at position
void simple_world_set_voxel(SimpleWorld *world, uint32_t x, uint32_t y, uint32_t z, SimpleVoxelType type) {
    SimpleVoxel *voxel = simple_world_get_voxel(world, x, y, z);
    if (voxel) {
        voxel->type = type;
    }
}

// Get color for voxel type
void simple_world_voxel_type_color(SimpleVoxelType type, uint8_t *r, uint8_t *g, uint8_t *b) {
    switch (type) {
        case VOXEL_AIR:
            *r = *g = *b = 0;
            break;
        case VOXEL_BEDROCK:
            *r = 64; *g = 64; *b = 64;
            break;
        case VOXEL_STONE:
            *r = 128; *g = 128; *b = 128;
            break;
        case VOXEL_SOIL:
            *r = 139; *g = 69; *b = 19;
            break;
        case VOXEL_GRASS:
            *r = 124; *g = 252; *b = 0;
            break;
        case VOXEL_WOOD:
            *r = 139; *g = 69; *b = 19;
            break;
        case VOXEL_LEAVES:
            *r = 34; *g = 139; *b = 34;
            break;
        case VOXEL_BRICK:
            *r = 178; *g = 34; *b = 34;
            break;
        default:
            *r = *g = *b = 128;
            break;
    }
}
