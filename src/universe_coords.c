#include "universe_coords.h"
#include <math.h>
#include <stdbool.h>

// Convert world-local coordinates to universe coordinates
UniverseCoord world_to_universe_coords(const UniverseWorldCoord *world_coord,
                                      uint32_t world_width, uint32_t world_height, uint32_t world_depth)
{
    UniverseCoord universe_coord = {0.0f, 0.0f, 0.0f};

    if (!world_coord) return universe_coord;

    // Calculate absolute universe coordinates
    // world_x * world_width gives the universe X offset for this world
    // local_x gives the position within the world
    universe_coord.x = (float)(world_coord->world_x * (int32_t)world_width) + (float)world_coord->local_x;
    universe_coord.y = (float)(world_coord->world_y * (int32_t)world_height) + (float)world_coord->local_y;
    universe_coord.z = (float)(world_coord->world_z * (int32_t)world_depth) + (float)world_coord->local_z;

    return universe_coord;
}

// Convert universe coordinates to world coordinates
UniverseWorldCoord universe_to_world_coords(const UniverseCoord *universe_coord,
                                           uint32_t world_width, uint32_t world_height, uint32_t world_depth)
{
        UniverseWorldCoord world_coord = {0, 0, 0, 0, 0, 0};

    if (!universe_coord) return world_coord;

    // Calculate which world this coordinate falls in
    world_coord.world_x = (int32_t)floorf(universe_coord->x / (float)world_width);
    world_coord.world_y = (int32_t)floorf(universe_coord->y / (float)world_height);
    world_coord.world_z = (int32_t)floorf(universe_coord->z / (float)world_depth);

    // Calculate local position within that world
    world_coord.local_x = (uint32_t)((int32_t)floorf(universe_coord->x) % (int32_t)world_width);
    world_coord.local_y = (uint32_t)((int32_t)floorf(universe_coord->y) % (int32_t)world_height);
    world_coord.local_z = (uint32_t)((int32_t)floorf(universe_coord->z) % (int32_t)world_depth);

    // Handle negative coordinates correctly
    if (universe_coord->x < 0.0f) {
        world_coord.local_x = world_width - 1 - world_coord.local_x;
        if (world_coord.local_x == world_width - 1) {
            world_coord.world_x--;
            world_coord.local_x = 0;
        }
    }
    if (universe_coord->y < 0.0f) {
        world_coord.local_y = world_height - 1 - world_coord.local_y;
        if (world_coord.local_y == world_height - 1) {
            world_coord.world_y--;
            world_coord.local_y = 0;
        }
    }
    if (universe_coord->z < 0.0f) {
        world_coord.local_z = world_depth - 1 - world_coord.local_z;
        if (world_coord.local_z == world_depth - 1) {
            world_coord.world_z--;
            world_coord.local_z = 0;
        }
    }

    return world_coord;
}

// Get universe coordinates for a specific world position
UniverseCoord get_world_universe_coords(int32_t world_x, int32_t world_y, int32_t world_z,
                                       uint32_t local_x, uint32_t local_y, uint32_t local_z,
                                       uint32_t world_width, uint32_t world_height, uint32_t world_depth)
{
    UniverseWorldCoord world_coord = {
        .world_x = world_x,
        .world_y = world_y,
        .world_z = world_z,
        .local_x = local_x,
        .local_y = local_y,
        .local_z = local_z
    };

    return world_to_universe_coords(&world_coord, world_width, world_height, world_depth);
}

// Float-precision variant: avoids integer truncation for sub-voxel sampling
UniverseCoord get_world_universe_coords_f(int32_t world_x, int32_t world_y, int32_t world_z,
                                         float local_x, float local_y, float local_z,
                                         uint32_t world_width, uint32_t world_height, uint32_t world_depth)
{
    UniverseCoord universe_coord = {0.0f, 0.0f, 0.0f};
    universe_coord.x = (float)(world_x * (int32_t)world_width) + local_x;
    universe_coord.y = (float)(world_y * (int32_t)world_height) + local_y;
    universe_coord.z = (float)(world_z * (int32_t)world_depth) + local_z;
    return universe_coord;
}

// Get universe coordinates for a world's origin (0,0,0 in local space)
UniverseCoord get_world_origin_universe_coords(int32_t world_x, int32_t world_y, int32_t world_z,
                                              uint32_t world_width, uint32_t world_height, uint32_t world_depth)
{
    return get_world_universe_coords(world_x, world_y, world_z, 0, 0, 0,
                                   world_width, world_height, world_depth);
}

// Check if universe coordinates fall within a specific world
bool universe_coords_in_world(const UniverseCoord *universe_coord,
                             int32_t world_x, int32_t world_y, int32_t world_z,
                             uint32_t world_width, uint32_t world_height, uint32_t world_depth)
{
    if (!universe_coord) return false;

    // Calculate the world's universe bounds
    float world_min_x = (float)(world_x * (int32_t)world_width);
    float world_max_x = world_min_x + (float)world_width;
    float world_min_y = (float)(world_y * (int32_t)world_height);
    float world_max_y = world_min_y + (float)world_height;
    float world_min_z = (float)(world_z * (int32_t)world_depth);
    float world_max_z = world_min_z + (float)world_depth;

    // Check if coordinates fall within bounds
    return (universe_coord->x >= world_min_x && universe_coord->x < world_max_x &&
            universe_coord->y >= world_min_y && universe_coord->y < world_max_y &&
            universe_coord->z >= world_min_z && universe_coord->z < world_max_z);
}

// Calculate distance between two universe coordinates
float universe_coords_distance(const UniverseCoord *coord1, const UniverseCoord *coord2)
{
    if (!coord1 || !coord2) return 0.0f;

    float dx = coord1->x - coord2->x;
    float dy = coord1->y - coord2->y;
    float dz = coord1->z - coord2->z;

    return sqrtf(dx*dx + dy*dy + dz*dz);
}

// Interpolate between two universe coordinates
UniverseCoord universe_coords_lerp(const UniverseCoord *coord1, const UniverseCoord *coord2, float t)
{
    UniverseCoord result = {0.0f, 0.0f, 0.0f};

    if (!coord1 || !coord2) return result;

    // Clamp t to [0,1] range
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // Linear interpolation
    result.x = coord1->x + (coord2->x - coord1->x) * t;
    result.y = coord1->y + (coord2->y - coord1->y) * t;
    result.z = coord1->z + (coord2->z - coord1->z) * t;

    return result;
}
