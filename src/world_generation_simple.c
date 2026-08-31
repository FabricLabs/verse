/*
 * world_generation_simple.c - Simple implementations using basic voxel operations
 *
 * These implementations don't rely on bulk operations for now.
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

// Random number generation
static unsigned int g_seed_state = 1;

void seed_rand_with_world_seed(const char* seed) {
    g_seed_state = 1;
    if (seed) {
        while (*seed) {
            g_seed_state = g_seed_state * 31 + (unsigned int)*seed;
            seed++;
        }
    }
}

int seeded_rand_range(int max) {
    g_seed_state = g_seed_state * 1103515245 + 12345;
    return (int)((g_seed_state / 65536) % (max + 1));
}

// Generate home world - simplified version
void world_generate_home(World* world, const char* seed) {
    (void)seed;

    if (!world || !world->voxels) return;

    // Clear world
    world_clear_region(world, 0, 0, 0, world->width, world->height, world->depth);

    // Create floating island
    uint32_t cx = world->width / 2;
    uint32_t cy = world->height / 2;
    uint32_t cz = world->depth / 2;
    uint32_t radius = fmin(fmin(world->width, world->height), world->depth) / 3;

    // Build island with exponential curve
    for (uint32_t z = 0; z < cz; z++) {
        float z_ratio = (float)z / (float)cz;
        float factor = z_ratio * z_ratio;
        uint32_t r = (uint32_t)(radius * factor);

        if (r > 0) {
            for (uint32_t y = cy - r; y <= cy + r && y < world->height; y++) {
                for (uint32_t x = cx - r; x <= cx + r && x < world->width; x++) {
                    int dx = (int)x - (int)cx;
                    int dy = (int)y - (int)cy;
                    if (dx*dx + dy*dy <= (int)(r*r)) {
                        world_set_voxel(world, x, y, z, VOXEL_STONE);
                    }
                }
            }
        }
    }
}

// Generate farm world - simplified version
void world_generate_farm(World* world, const char* seed) {
    (void)seed;

    if (!world || !world->voxels) return;

    // Clear world
    world_clear_region(world, 0, 0, 0, world->width, world->height, world->depth);

    // Layer heights
    uint32_t bedrock_height = 1;
    uint32_t stone_height = world->height * 3 / 10;
    uint32_t soil_height = world->height / 2;

    // Build layers
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t x = 0; x < world->width; x++) {
            // Bedrock
            for (uint32_t y = 0; y < bedrock_height && y < world->height; y++) {
                world_set_voxel(world, x, y, z, VOXEL_BEDROCK);
            }

            // Stone
            for (uint32_t y = bedrock_height; y < stone_height && y < world->height; y++) {
                world_set_voxel(world, x, y, z, VOXEL_STONE);
            }

            // Soil
            for (uint32_t y = stone_height; y < soil_height && y < world->height; y++) {
                world_set_voxel(world, x, y, z, VOXEL_SOIL);
            }

            // Top layer - grass patches
            if (soil_height < world->height) {
                uint32_t hash = x * 73856093u ^ z * 83492791u;
                if ((hash & 0xFF) > 200) {
                    world_set_voxel(world, x, soil_height, z, VOXEL_GRASS);
                }
            }
        }
    }
}

// Generate arena world - simplified version
void world_generate_arena(World* world, const char* seed) {
    (void)seed;

    if (!world || !world->voxels) return;

    // Clear world
    world_clear_region(world, 0, 0, 0, world->width, world->height, world->depth);

    // Fill bottom half with limestone
    uint32_t half_z = world->depth / 2;
    for (uint32_t z = 0; z < half_z; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                world_set_voxel(world, x, y, z, VOXEL_STONE_LIMESTONE);
            }
        }
    }

    // Carve hemisphere
    uint32_t cx = world->width / 2;
    uint32_t cy = world->height / 2;
    uint32_t cz = world->depth / 2;
    uint32_t radius = fmin(fmin(world->width, world->height), world->depth) / 4;

    for (uint32_t z = 0; z <= cz; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                int dx = (int)x - (int)cx;
                int dy = (int)y - (int)cy;
                int dz = (int)z - (int)cz;

                if (dx*dx + dy*dy + dz*dz <= (int)(radius*radius)) {
                    world_set_voxel(world, x, y, z, VOXEL_AIR);
                }
            }
        }
    }
}

// Solid fill is already simple
void world_generate_solid_fill(World* world, VoxelType fill_type) {
    if (!world || !world->voxels) return;

    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                world_set_voxel(world, x, y, z, fill_type);
            }
        }
    }
}

// Tree planting stubs - scaled trunks with height variation (full canopy trees live in world.c)
bool world_try_plant_oak_Z(World* world, int x, int y, int ground_z) {
    if (!world) return false;

    int trunk_h = 6 + seeded_rand_range(7); // 6-12
    for (int z = ground_z + 1; z <= ground_z + trunk_h && z < (int)world->depth; z++) {
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_WOOD);
    }
    return true;
}

bool world_try_plant_oak_Y(World* world, int x, int ground_y, int z) {
    if (!world) return false;

    int trunk_h = 6 + seeded_rand_range(7); // 6-12
    for (int y = ground_y + 1; y <= ground_y + trunk_h && y < (int)world->height; y++) {
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_WOOD);
    }
    return true;
}

bool world_try_plant_birch_Z(World* world, int x, int y, int ground_z) {
    if (!world) return false;

    int trunk_h = 5 + seeded_rand_range(6); // 5-10
    for (int z = ground_z + 1; z <= ground_z + trunk_h && z < (int)world->depth; z++) {
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_WOOD);
    }
    return true;
}

bool world_try_plant_birch_Y(World* world, int x, int ground_y, int z) {
    if (!world) return false;

    int trunk_h = 5 + seeded_rand_range(6); // 5-10
    for (int y = ground_y + 1; y <= ground_y + trunk_h && y < (int)world->height; y++) {
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_WOOD);
    }
    return true;
}

bool world_try_plant_pine_Z(World* world, int x, int y, int ground_z) {
    if (!world) return false;

    int trunk_h = 7 + seeded_rand_range(8); // 7-14
    for (int z = ground_z + 1; z <= ground_z + trunk_h && z < (int)world->depth; z++) {
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_WOOD);
    }
    return true;
}

bool world_try_plant_pine_Y(World* world, int x, int ground_y, int z) {
    if (!world) return false;

    int trunk_h = 7 + seeded_rand_range(8); // 7-14
    for (int y = ground_y + 1; y <= ground_y + trunk_h && y < (int)world->height; y++) {
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_WOOD);
    }
    return true;
}

bool world_try_place_any_tree_at(World* world, uint32_t x, uint32_t ground_y, uint32_t z) {
    int type = seeded_rand_range(2);

    switch(type) {
        case 0: return world_try_plant_oak_Y(world, (int)x, (int)ground_y, (int)z);
        case 1: return world_try_plant_birch_Y(world, (int)x, (int)ground_y, (int)z);
        default: return world_try_plant_pine_Y(world, (int)x, (int)ground_y, (int)z);
    }
}

// Generate underworld - bedrock ceiling/floor with stalactites/stalagmites
void world_generate_underworld(World* world, const char* seed) {
    (void)seed;

    if (!world || !world->voxels) return;

    // Clear world
    world_clear_region(world, 0, 0, 0, world->width, world->height, world->depth);

    const uint32_t d = world->depth;
    if (d < 8) {
        world_generate_solid_fill(world, VOXEL_BEDROCK);
        return;
    }

    // Bedrock ceiling and floor
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
            world_set_voxel(world, x, y, d - 1, VOXEL_BEDROCK);
        }
    }

    // Simple stalactites and stalagmites
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            // Use simple hash for placement
            uint32_t hash = x * 73856093u ^ y * 19349663u;
            float noise = (float)(hash & 0xFF) / 255.0f;

            if (noise > 0.8f) {
                // Stalactite from ceiling
                uint32_t length = 2 + (hash >> 8) % 4;
                for (uint32_t z = 1; z < length && z < d/2; z++) {
                    world_set_voxel(world, x, y, z, VOXEL_BEDROCK);
                }
            }
            if (noise < 0.2f) {
                // Stalagmite from floor
                uint32_t length = 2 + (hash >> 16) % 4;
                for (uint32_t z = 1; z < length && z < d/2; z++) {
                    world_set_voxel(world, x, y, d - 1 - z, VOXEL_BEDROCK);
                }
            }
        }
    }

    // 3x3 connector column in center
    uint32_t cx = world->width / 2;
    uint32_t cy = world->height / 2;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            for (uint32_t z = d/3; z < 2*d/3; z++) {
                if ((uint32_t)((int)cx + dx) < world->width &&
                    (uint32_t)((int)cy + dy) < world->height) {
                    world_set_voxel(world, cx + dx, cy + dy, z, VOXEL_BEDROCK);
                }
            }
        }
    }
}

// Generate cloud world - sparse steam ellipsoids
void world_generate_cloud(World* world, const char* seed) {
    (void)seed;

    if (!world || !world->voxels) return;

    // Clear world
    world_clear_region(world, 0, 0, 0, world->width, world->height, world->depth);

    // Number of cloud blobs
    int blobs = (world->width * world->height * world->depth) / 4096;
    if (blobs < 3) blobs = 3;
    if (blobs > 20) blobs = 20;

    // Place cloud blobs
    for (int i = 0; i < blobs; i++) {
        // Random position using simple hash
        uint32_t hash1 = (uint32_t)i * 73856093u;
        uint32_t hash2 = (uint32_t)i * 19349663u;
        uint32_t hash3 = (uint32_t)i * 83492791u;

        int cx = (hash1 % world->width);
        int cy = (hash2 % world->height);
        int cz = (hash3 % world->depth);

        // Flattened ellipsoid
        int rx = 3 + (hash1 >> 16) % 5;
        int ry = 3 + (hash2 >> 16) % 5;
        int rz = 1 + (hash3 >> 16) % 2; // Flat clouds

        for (int z = cz - rz; z <= cz + rz; z++) {
            for (int y = cy - ry; y <= cy + ry; y++) {
                for (int x = cx - rx; x <= cx + rx; x++) {
                    if (x >= 0 && y >= 0 && z >= 0 &&
                        x < (int)world->width && y < (int)world->height && z < (int)world->depth) {
                        float dx = (float)(x - cx) / (float)rx;
                        float dy = (float)(y - cy) / (float)ry;
                        float dz = (float)(z - cz) / (float)rz;
                        float d2 = dx*dx + dy*dy + dz*dz;

                        if (d2 <= 1.0f) {
                            // Sparse cloud - only place some voxels
                            uint32_t voxel_hash = (uint32_t)x * 12345u ^ (uint32_t)y * 67890u ^ (uint32_t)z * 54321u;
                            if ((voxel_hash & 0xFF) > 100) { // ~60% density
                                world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_STEAM);
                            }
                        }
                    }
                }
            }
        }
    }
}

// Generate random world - simple random terrain
void world_generate_random(World* world, const char* seed) {
    seed_rand_with_world_seed(seed);

    if (!world || !world->voxels) return;

    // Clear world
    world_clear_region(world, 0, 0, 0, world->width, world->height, world->depth);

    // Random floor height for each column
    for (uint32_t x = 0; x < world->width; x++) {
        for (uint32_t z = 0; z < world->depth; z++) {
            // Random height between 20% and 60% of world height
            uint32_t height = world->height / 5 + (seeded_rand_range(100) * world->height * 2 / 500);

            for (uint32_t y = 0; y < height && y < world->height; y++) {
                VoxelType type = VOXEL_STONE;

                // Top layer
                if (y == height - 1) {
                    type = (seeded_rand_range(100) < 70) ? VOXEL_GRASS : VOXEL_SOIL;
                }
                // Near top - soil
                else if (y >= height - 3) {
                    type = VOXEL_SOIL;
                }

                world_set_voxel(world, x, y, z, type);
            }
        }
    }
}
