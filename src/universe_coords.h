#ifndef UNIVERSE_COORDS_H
#define UNIVERSE_COORDS_H

#include <stdint.h>
#include <stdbool.h>

// Universe coordinate system utilities
// This provides seamless conversion between world-local and universe-wide coordinates
// for consistent entropy field sampling across world boundaries

typedef struct UniverseCoord {
    float x, y, z;  // Absolute universe coordinates
} UniverseCoord;

typedef struct UniverseWorldCoord {
    int32_t world_x, world_y, world_z;  // World grid position
    uint32_t local_x, local_y, local_z; // Local position within world
} UniverseWorldCoord;

// Convert world-local coordinates to universe coordinates
UniverseCoord world_to_universe_coords(const UniverseWorldCoord *world_coord,
                                      uint32_t world_width, uint32_t world_height, uint32_t world_depth);

// Convert universe coordinates to world coordinates
UniverseWorldCoord universe_to_world_coords(const UniverseCoord *universe_coord,
                                           uint32_t world_width, uint32_t world_height, uint32_t world_depth);

// Get universe coordinates for a specific world position
UniverseCoord get_world_universe_coords(int32_t world_x, int32_t world_y, int32_t world_z,
                                       uint32_t local_x, uint32_t local_y, uint32_t local_z,
                                       uint32_t world_width, uint32_t world_height, uint32_t world_depth);

// Float-precision variant for sub-voxel sampling without integer truncation
UniverseCoord get_world_universe_coords_f(int32_t world_x, int32_t world_y, int32_t world_z,
                                         float local_x, float local_y, float local_z,
                                         uint32_t world_width, uint32_t world_height, uint32_t world_depth);

// Get universe coordinates for a world's origin (0,0,0 in local space)
UniverseCoord get_world_origin_universe_coords(int32_t world_x, int32_t world_y, int32_t world_z,
                                              uint32_t world_width, uint32_t world_height, uint32_t world_depth);

// Check if universe coordinates fall within a specific world
bool universe_coords_in_world(const UniverseCoord *universe_coord,
                             int32_t world_x, int32_t world_y, int32_t world_z,
                             uint32_t world_width, uint32_t world_height, uint32_t world_depth);

// Calculate distance between two universe coordinates
float universe_coords_distance(const UniverseCoord *coord1, const UniverseCoord *coord2);

// Interpolate between two universe coordinates
UniverseCoord universe_coords_lerp(const UniverseCoord *coord1, const UniverseCoord *coord2, float t);

#endif // UNIVERSE_COORDS_H
