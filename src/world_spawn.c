#include "world_spawn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Initialize spawn system
void world_spawn_init(void)
{
    printf("World spawn system initialized\n");
}

// Get the true center of a world (ensures single tile, not corner intersection)
void world_get_center_position(World *world, int *center_x, int *center_z)
{
    if (!world || !center_x || !center_z)
    {
        return;
    }

    // For odd dimensions, use the middle tile
    // For even dimensions, use the tile just before the middle (ensures single tile)
    if (world->width % 2 == 1)
    {
        *center_x = world->width / 2;
    }
    else
    {
        *center_x = (world->width / 2) - 1;
    }

    if (world->depth % 2 == 1)
    {
        *center_z = world->depth / 2;
    }
    else
    {
        *center_z = (world->depth / 2) - 1;
    }

    printf("World center calculated: (%d, %d) for world %dx%d\n",
           *center_x, *center_z, world->width, world->depth);
}

// Check if a position is a center tile (not corner intersection)
bool world_is_center_tile(World *world, int x, int z)
{
    if (!world)
        return false;

    int center_x, center_z;
    world_get_center_position(world, &center_x, &center_z);

    return (x == center_x && z == center_z);
}

// Find safe ground at a specific X,Y coordinate (Z-vertical system)
int world_find_safe_ground(World *world, int x, int y)
{
    if (!world || !world_is_position_valid(world, x, y, 0))
    {
        return -1;
    }

    // Start from the top and work down to find solid ground (Z is vertical now)
    printf("SPAWN DEBUG: Scan column for ground at (x=%d, y=%d) from z=%d..0\n", x, y, world->depth - 1);
    for (int z = world->depth - 1; z >= 0; z--)
    {
        Voxel *voxel = world_get_voxel(world, x, y, z);
        if (voxel && voxel->type != VOXEL_AIR && voxel->type != VOXEL_WATER)
        {
            // Found solid ground, check if position above is safe
            if (z + 1 < world->depth)
            {
                Voxel *above_voxel = world_get_voxel(world, x, y, z + 1);
                if (above_voxel && above_voxel->type == VOXEL_AIR)
                {

                    printf("SPAWN DEBUG: Found solid ground (type %d) at (%d,%d,%d), placing player at (%d,%d,%d)\n",
                           voxel->type, x, y, z, x, y, z + 1);
                    return z + 1; // Return position above solid ground
                }
                else
                {
                    printf("SPAWN DEBUG: Solid ground at (%d,%d,%d) but above is blocked (type %d)\n",
                           x, y, z, above_voxel ? above_voxel->type : -1);
                }
            }
        }
    }
    printf("SPAWN DEBUG: No safe ground found in column (x=%d, y=%d)\n", x, y);
    return -1; // No safe ground found
}

// True when a column's top voxel is ground the player can be set down on, rather than something
// standing on the ground. Landing on a tree canopy or a boulder would put the player above the
// island's real surface, and the point of the home spawn is to arrive on open ground.
static bool spawn_is_open_ground(VoxelType type)
{
    switch (type)
    {
    case VOXEL_GRASS:
    case VOXEL_GRASS_WIDE:
    case VOXEL_GRASS_SHARP:
    case VOXEL_GRASS_CLOVER:
    case VOXEL_GRASS_MOSS:
    case VOXEL_SOIL:
    case VOXEL_SOIL_CLAY:
    case VOXEL_SOIL_LOAM:
    case VOXEL_SOIL_SILT:
    case VOXEL_STONE:
        return true;
    default:
        return false;
    }
}

// Get fixed spawn position for home world (deterministic).
//
// world_generate_home keeps the island's central arena flat, at the island's maximum height, and
// clear of trees and boulders. So this looks for the highest open-ground column and takes the one
// nearest the axis: the topmost level is what makes the arrival a short drop onto the surface
// rather than a fall past the side of the island, and preferring the centre keeps the spawn
// deterministic and well away from the rim.
SpawnPosition world_get_home_spawn_position(World *world)
{
    SpawnPosition spawn = {0};
    spawn.is_safe = false;
    spawn.spawn_reason = strdup("No safe spawn found");

    if (!world)
    {
        return spawn;
    }

    const int center_x = (int)(world->width / 2);
    const int center_y = (int)(world->height / 2);

    int best_x = -1, best_y = -1, best_top = -1;
    long best_distance2 = 0;

    for (uint32_t y = 0; y < world->height; y++)
    {
        for (uint32_t x = 0; x < world->width; x++)
        {
            int top_z = -1;
            for (int z = (int)world->depth - 1; z >= 0; z--)
            {
                Voxel *voxel = world_get_voxel(world, x, y, (uint32_t)z);
                if (voxel && voxel->type != VOXEL_AIR)
                {
                    top_z = z;
                    break;
                }
            }
            if (top_z < 0 || top_z + 1 >= (int)world->depth)
                continue;

            Voxel *ground = world_get_voxel(world, x, y, (uint32_t)top_z);
            Voxel *headroom = world_get_voxel(world, x, y, (uint32_t)(top_z + 1));
            if (!ground || !spawn_is_open_ground(ground->type))
                continue;
            if (!headroom || headroom->type != VOXEL_AIR)
                continue;

            const long dx = (long)x - center_x;
            const long dy = (long)y - center_y;
            const long distance2 = dx * dx + dy * dy;

            if (top_z > best_top || (top_z == best_top && distance2 < best_distance2))
            {
                best_top = top_z;
                best_distance2 = distance2;
                best_x = (int)x;
                best_y = (int)y;
            }
        }
    }

    if (best_top >= 0)
    {
        spawn.x = best_x;
        spawn.y = best_y;
        spawn.z = best_top + 1;
        spawn.is_safe = true;
        free(spawn.spawn_reason);
        spawn.spawn_reason = strdup("Home world spawn on the island's topmost open ground");

        printf("HOME SPAWN: topmost open ground at (%d,%d) z=%d, standing at z=%d\n",
               best_x, best_y, best_top, spawn.z);
    }
    else
    {
        printf("HOME SPAWN: No open ground with headroom found on the island\n");
    }

    return spawn;
}

// Validate a spawn position
SpawnValidation world_validate_spawn_position(World *world, int x, int y, int z)
{
    SpawnValidation validation = {0};

    if (!world)
    {
        validation.valid = false;
        validation.validation_msg = strdup("Invalid world");
        return validation;
    }

    // Check if position is within world bounds
    if (!world_is_position_valid(world, x, y, z))
    {
        validation.valid = false;
        validation.validation_msg = strdup("Position outside world bounds");
        return validation;
    }

    // Check if this is a center tile
    validation.is_center_tile = world_is_center_tile(world, x, z);

    // In the current coordinate system, Z is vertical.
    // Ground must exist at (z - 1), clear space must exist at (z + 1).
    if (z > 0)
    {
        Voxel *ground_voxel = world_get_voxel(world, x, y, z - 1);
        validation.has_solid_ground = (ground_voxel &&
                                       ground_voxel->type != VOXEL_AIR &&
                                       ground_voxel->type != VOXEL_WATER);
    }
    else
    {
        validation.has_solid_ground = false;
    }

    if (z + 1 < world->depth)
    {
        Voxel *above_voxel = world_get_voxel(world, x, y, z + 1);
        validation.has_clear_space = (above_voxel && above_voxel->type == VOXEL_AIR);
    }
    else
    {
        validation.has_clear_space = false;
    }

    // Position is valid if it has solid ground and clear space
    validation.valid = validation.has_solid_ground && validation.has_clear_space;

    if (validation.valid)
    {
        validation.validation_msg = strdup("Valid spawn position");
    }
    else
    {
        validation.validation_msg = strdup("Invalid spawn position - missing solid ground or clear space");
    }

    return validation;
}

// Find the nearest safe spawn position to a given horizontal coordinate
SpawnPosition world_find_nearest_safe_spawn(World *world, int target_x, int target_y)
{
    SpawnPosition spawn = {0};
    spawn.is_safe = false;
    spawn.spawn_reason = strdup("No safe spawn found");

    if (!world)
    {
        return spawn;
    }

    // Search in expanding spiral pattern from target position
    int max_search_radius = 20; // Limit search area

    printf("SPAWN SEARCH: target=(%d,%d) max_radius=%d\n", target_x, target_y, max_search_radius);
    for (int radius = 0; radius <= max_search_radius; radius++)
    {
        printf("SPAWN SEARCH: radius=%d\n", radius);
        for (int dx = -radius; dx <= radius; dx++)
        {
            for (int dy = -radius; dy <= radius; dy++)
            {
                // Only check the perimeter of the current search radius
                if (abs(dx) == radius || abs(dy) == radius)
                {
                    int test_x = target_x + dx;
                    int test_y = target_y + dy;

                    if (test_x < 0 || test_x >= (int)world->width ||
                        test_y < 0 || test_y >= (int)world->height)
                    {
                        printf("SPAWN SEARCH: skip out-of-bounds (x=%d, y=%d)\n", test_x, test_y);
                        continue;
                    }

                    printf("SPAWN SEARCH: test tile (x=%d, y=%d)\n", test_x, test_y);
                    int safe_z = world_find_safe_ground(world, test_x, test_y);
                    if (safe_z >= 0)
                    {
                        // Validate the spawn position
                        SpawnValidation validation = world_validate_spawn_position(world, test_x, test_y, safe_z);
                        if (validation.valid)
                        {
                            printf("SPAWN SEARCH: VALID spawn at (%d,%d,%d) center=%d ground=%d clear=%d\n",
                                   test_x, test_y, safe_z,
                                   validation.is_center_tile,
                                   validation.has_solid_ground,
                                   validation.has_clear_space);
                            spawn.x = test_x;
                            spawn.y = test_y;
                            spawn.z = safe_z;
                            spawn.is_safe = true;

                            // Create reason message
                            char reason[256];
                            if (validation.is_center_tile)
                            {
                                snprintf(reason, sizeof(reason),
                                         "Found safe center tile at (%d, %d, %d)", test_x, test_y, safe_z);
                            }
                            else
                            {
                                snprintf(reason, sizeof(reason),
                                         "Found safe spawn at (%d, %d, %d) near target", test_x, test_y, safe_z);
                            }
                            free(spawn.spawn_reason);
                            spawn.spawn_reason = strdup(reason);

                            // Clean up validation
                            free(validation.validation_msg);
                            return spawn;
                        }
                        else
                        {
                            printf("SPAWN SEARCH: INVALID at (%d,%d,%d): center=%d ground=%d clear=%d\n",
                                   test_x, test_y, safe_z,
                                   validation.is_center_tile,
                                   validation.has_solid_ground,
                                   validation.has_clear_space);
                        }
                        free(validation.validation_msg);
                    }
                    else
                    {
                        printf("SPAWN SEARCH: no ground found for (x=%d, y=%d)\n", test_x, test_y);
                    }
                }
            }
        }
    }

    return spawn;
}

// Find the best spawn position in a world
SpawnPosition world_find_best_spawn_position(World *world)
{
    SpawnPosition spawn = {0};
    spawn.is_safe = false;
    spawn.spawn_reason = strdup("No safe spawn found");

    if (!world)
    {
        return spawn;
    }

    // For home worlds, use fixed deterministic spawn position
    if (world->generation_type == WORLD_TYPE_HOME)
    {
        printf("SPAWN: Using fixed home world spawn position\n");
        return world_get_home_spawn_position(world);
    }

    printf("SPAWN: Using dynamic spawn search for non-home world (type %d)\n", world->generation_type);

    // For wilderness, compute deterministic candidate spawns and record COMPUTE_SPAWN event
    if (world->generation_type == WORLD_TYPE_WILDERNESS)
    {
        SpawnPosition w = world_find_wilderness_spawn(world, false);
        if (w.is_safe)
        {
            char msg[160];
            snprintf(msg, sizeof(msg), "EVENT type=COMPUTE_SPAWN data=x:%d y:%d z:%d", w.x, w.y, w.z);
            world_append_log(world, msg);
            world->history_event_count++;
            world->vector_clock = world->history_event_count;
            return w;
        }
    }

    // First, try to find a safe spawn at the horizontal center
    int center_x, ignored;
    world_get_center_position(world, &center_x, &ignored);
    int center_y = (int)(world->height / 2);

    printf("Looking for safe spawn at center (%d, %d)\n", center_x, center_y);

    int safe_z = world_find_safe_ground(world, center_x, center_y);
    if (safe_z >= 0)
    {
        SpawnValidation validation = world_validate_spawn_position(world, center_x, center_y, safe_z);
        if (validation.valid)
        {
            spawn.x = center_x;
            spawn.y = center_y;
            spawn.z = safe_z;
            spawn.is_safe = true;
            free(spawn.spawn_reason);
            spawn.spawn_reason = strdup("Found safe center spawn position");
            free(validation.validation_msg);
            return spawn;
        }
        free(validation.validation_msg);
    }

    // If center is not safe, find nearest safe position
    printf("Center not safe, searching for nearest safe spawn...\n");
    spawn = world_find_nearest_safe_spawn(world, center_x, center_y);

    return spawn;
}

// Deterministic wilderness spawn among 5 candidates; optionally reseed to recalc in future
SpawnPosition world_find_wilderness_spawn(World *world, bool reseed_candidates)
{
    SpawnPosition spawn = {0};
    spawn.is_safe = false;
    spawn.spawn_reason = strdup("No safe spawn found");
    if (!world) return spawn;

    // Only applies to wilderness; fallback otherwise
    if (world->generation_type != WORLD_TYPE_WILDERNESS)
    {
        return world_find_best_spawn_position(world);
    }

    // Build a small deterministic RNG from world->seed_id
    unsigned int s = 2166136261u; // FNV-1a basis
    for (const unsigned char *p = (const unsigned char*)world->seed_id; p && *p; ++p) {
        s ^= *p; s *= 16777619u;
    }
    if (reseed_candidates) { s ^= 0x9e3779b9u; s *= 16777619u; }

    // Generate 5 candidate (x,y) tiles near the horizontal center
    int cx, ignored;
    world_get_center_position(world, &cx, &ignored);
    int cy = (int)(world->height / 2);
    int candidates = 5;
    printf("SPAWN WILDERNESS: computing %d candidates around center (%d,%d) reseed=%d\n",
           candidates, cx, cy, reseed_candidates ? 1 : 0);
    for (int i = 0; i < candidates; i++)
    {
        // Deterministic offsets in a ring around center
        s = s * 1103515245u + 12345u;
        int dx = (int)((s >> 16) & 0x07) - 3; // -3..+3
        s = s * 1103515245u + 12345u;
        int dy = (int)((s >> 16) & 0x07) - 3; // -3..+3

        int x = cx + dx * (2 + (i % 2));
        int y = cy + dy * (2 + ((i + 1) % 2));

        if (x < 0) x = 0;
        if (x >= (int)world->width) x = (int)world->width - 1;
        if (y < 0) y = 0;
        if (y >= (int)world->height) y = (int)world->height - 1;

        printf("SPAWN WILDERNESS: candidate %d tile (x=%d, y=%d)\n", i, x, y);
        int safe_z = world_find_safe_ground(world, x, y);
        if (safe_z >= 0)
        {
            // Validate requires ground at y-1 and air at y+1; our world_find_safe_ground already chose z above ground
            SpawnValidation validation = world_validate_spawn_position(world, x, y, safe_z);
            if (validation.valid)
            {
                printf("SPAWN WILDERNESS: VALID spawn at (%d,%d,%d)\n", x, y, safe_z);
                spawn.x = x; spawn.y = y; spawn.z = safe_z; spawn.is_safe = true;
                free(spawn.spawn_reason);
                spawn.spawn_reason = strdup("Wilderness deterministic spawn");
                free(validation.validation_msg);
                return spawn;
            }
            else
            {
                printf("SPAWN WILDERNESS: INVALID at (%d,%d,%d) center=%d ground=%d clear=%d\n",
                       x, y, safe_z,
                       validation.is_center_tile,
                       validation.has_solid_ground,
                       validation.has_clear_space);
            }
            free(validation.validation_msg);
        }
        else
        {
            printf("SPAWN WILDERNESS: no ground for (x=%d, y=%d)\n", x, y);
        }
    }

    // Fallback to nearest safe position from horizontal center
    return world_find_nearest_safe_spawn(world, cx, cy);
}

// Clean up spawn system
void world_spawn_cleanup(void)
{
    printf("World spawn system cleaned up\n");
}
