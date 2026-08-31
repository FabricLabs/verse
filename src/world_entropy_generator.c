#include "world_entropy_generator.h"
#include "entropy_field.h"
#include "universe_coords.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Global entropy field instances for different generation types
static EntropyField g_terrain_field;
static EntropyField g_ore_field;
static EntropyField g_vegetation_field;
static EntropyField g_structure_field;
static bool g_entropy_initialized = false;

// Initialize the entropy field system for world generation
void world_entropy_generator_init(uint32_t universe_seed)
{
    if (g_entropy_initialized) return;

    // Initialize specialized entropy fields
    g_terrain_field = entropy_field_terrain(universe_seed);
    g_ore_field = entropy_field_ore_deposits(universe_seed);
    g_vegetation_field = entropy_field_vegetation(universe_seed);
    g_structure_field = entropy_field_structures(universe_seed);

    g_entropy_initialized = true;

    printf("[ENTROPY] Initialized entropy field system with universe seed: %u\n", universe_seed);
}

// Helper function to convert world coordinates to universe coordinates
static UniverseCoord world_local_to_universe(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                            uint32_t local_x, uint32_t local_y, uint32_t local_z)
{
    return get_world_universe_coords(world_x, world_y, world_z, local_x, local_y, local_z,
                                   world->width, world->height, world->depth);
}

// Generate terrain using entropy field sampling
void world_generate_terrain_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z)
{
    if (!world || !g_entropy_initialized) return;

    printf("[ENTROPY] Generating terrain for world (%d, %d, %d)\n", world_x, world_y, world_z);

    // Generate terrain height map using entropy field
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            // Sample terrain height from entropy field
            float height_value = world_sample_terrain_height_entropy(world, world_x, world_y, world_z, x, y);

            // Convert height value (0-1) to actual height in world
            uint32_t terrain_height = (uint32_t)(height_value * (float)world->depth * 0.7f); // Use 70% of depth

            // Fill from bottom up to terrain height
            for (uint32_t z = 0; z < world->depth; z++) {
                Voxel *voxel = world_get_voxel(world, x, y, z);
                if (!voxel) continue;

                if (z < terrain_height) {
                    // Below terrain - solid rock
                    if (z == 0) {
                        voxel->type = VOXEL_STONE_BASALT; // Bedrock
                    } else if (z < terrain_height - 3) {
                        voxel->type = VOXEL_STONE_GRANITE; // Deep rock
                    } else {
                        voxel->type = VOXEL_STONE; // Surface rock
                    }
                } else {
                    // Above terrain - air
                    voxel->type = VOXEL_AIR;
                }
            }
        }
    }
    // Voxels were assigned directly above, so the renderer's derived caches no longer
    // describe this world. Discarded rather than rebuilt because callers usually run
    // several of these passes in a row; world_generate_composite_entropy rebuilds once at
    // the end.
    world_discard_derived_caches(world);
}

// Generate ore deposits using entropy field sampling
void world_generate_ore_deposits_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z)
{
    if (!world || !g_entropy_initialized) return;

    printf("[ENTROPY] Generating ore deposits for world (%d, %d, %d)\n", world_x, world_y, world_z);

    // Generate ore deposits based on entropy field sampling
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel *voxel = world_get_voxel(world, x, y, z);
                if (!voxel || voxel->type == VOXEL_AIR) continue;

                // Sample ore density from entropy field
                float ore_density = world_sample_ore_density_entropy(world, world_x, world_y, world_z, x, y, z);

                // Convert ore density to actual ore placement
                if (ore_density > 0.85f) {
                    // High density - valuable ore
                    if (z < world->depth / 4) {
                        voxel->type = VOXEL_STONE_GRANITE; // Deep valuable ore
                    } else {
                        voxel->type = VOXEL_STONE; // Surface valuable ore
                    }
                } else if (ore_density > 0.7f) {
                    // Medium density - common ore
                    if (z < world->depth / 3) {
                        voxel->type = VOXEL_STONE_LIMESTONE; // Common deep ore
                    }
                }
            }
        }
    }
    // Voxels were assigned directly above, so the renderer's derived caches no longer
    // describe this world. Discarded rather than rebuilt because callers usually run
    // several of these passes in a row; world_generate_composite_entropy rebuilds once at
    // the end.
    world_discard_derived_caches(world);
}

// Generate vegetation using entropy field sampling
void world_generate_vegetation_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z)
{
    if (!world || !g_entropy_initialized) return;

    printf("[ENTROPY] Generating vegetation for world (%d, %d, %d)\n", world_x, world_y, world_z);

    // Generate vegetation based on entropy field sampling
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            // Find the top solid block
            int top_z = -1;
            for (int z = (int)world->depth - 1; z >= 0; z--) {
                Voxel *voxel = world_get_voxel(world, x, y, (uint32_t)z);
                if (voxel && voxel->type != VOXEL_AIR) {
                    top_z = z;
                    break;
                }
            }

            if (top_z >= 0 && top_z < (int)world->depth - 1) {
                // Sample vegetation density from entropy field
                float vegetation_density = world_sample_vegetation_density_entropy(world, world_x, world_y, world_z, x, y);

                // Place vegetation based on density
                if (vegetation_density > 0.8f) {
                    // High density - forest
                    Voxel *voxel_above = world_get_voxel(world, x, y, (uint32_t)(top_z + 1));
                    if (voxel_above && voxel_above->type == VOXEL_AIR) {
                        voxel_above->type = VOXEL_STONE; // Tree trunk (simplified)

                        // Add some tree canopy
                        if (top_z + 2 < (int)world->depth) {
                            Voxel *canopy = world_get_voxel(world, x, y, (uint32_t)(top_z + 2));
                            if (canopy && canopy->type == VOXEL_AIR) {
                                canopy->type = VOXEL_STONE_GRANITE; // Tree leaves (simplified)
                            }
                        }
                    }
                } else if (vegetation_density > 0.6f) {
                    // Medium density - scattered vegetation
                    Voxel *voxel_above = world_get_voxel(world, x, y, (uint32_t)(top_z + 1));
                    if (voxel_above && voxel_above->type == VOXEL_AIR) {
                        voxel_above->type = VOXEL_STONE_LIMESTONE; // Grass/bush (simplified)
                    }
                }
            }
        }
    }
    // Voxels were assigned directly above, so the renderer's derived caches no longer
    // describe this world. Discarded rather than rebuilt because callers usually run
    // several of these passes in a row; world_generate_composite_entropy rebuilds once at
    // the end.
    world_discard_derived_caches(world);
}

// Generate structures using entropy field sampling
void world_generate_structures_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z)
{
    if (!world || !g_entropy_initialized) return;

    printf("[ENTROPY] Generating structures for world (%d, %d, %d)\n", world_x, world_y, world_z);

    // Generate structures based on entropy field sampling
    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            // Sample structure probability from entropy field
            float structure_prob = world_sample_structure_probability_entropy(world, world_x, world_y, world_z, x, y);

            // Place structures based on probability
            if (structure_prob > 0.95f) {
                // Very high probability - major structure
                int top_z = -1;
                for (int z = (int)world->depth - 1; z >= 0; z--) {
                    Voxel *voxel = world_get_voxel(world, x, y, (uint32_t)z);
                    if (voxel && voxel->type != VOXEL_AIR) {
                        top_z = z;
                        break;
                    }
                }

                if (top_z >= 0 && top_z < (int)world->depth - 3) {
                    // Build a simple structure
                    for (int dz = 1; dz <= 3; dz++) {
                        Voxel *voxel = world_get_voxel(world, x, y, (uint32_t)(top_z + dz));
                        if (voxel && voxel->type == VOXEL_AIR) {
                            voxel->type = VOXEL_STONE_GRANITE; // Structure material
                        }
                    }
                }
            }
        }
    }
    // Voxels were assigned directly above, so the renderer's derived caches no longer
    // describe this world. Discarded rather than rebuilt because callers usually run
    // several of these passes in a row; world_generate_composite_entropy rebuilds once at
    // the end.
    world_discard_derived_caches(world);
}

// Sample terrain height at a specific world position
float world_sample_terrain_height_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                         uint32_t local_x, uint32_t local_y)
{
    if (!world || !g_entropy_initialized) return 0.5f;

    // Convert to universe coordinates
    UniverseCoord universe_coord = world_local_to_universe(world, world_x, world_y, world_z, local_x, local_y, 0);

    // Sample the terrain entropy field
    return entropy_field_sample_2d(&g_terrain_field, universe_coord.x, universe_coord.y);
}

// Sample ore density at a specific world position
float world_sample_ore_density_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                      uint32_t local_x, uint32_t local_y, uint32_t local_z)
{
    if (!world || !g_entropy_initialized) return 0.5f;

    // Convert to universe coordinates
    UniverseCoord universe_coord = world_local_to_universe(world, world_x, world_y, world_z, local_x, local_y, local_z);

    // Sample the ore entropy field
    return entropy_field_sample(&g_ore_field, universe_coord.x, universe_coord.y, universe_coord.z);
}

// Sample vegetation density at a specific world position
float world_sample_vegetation_density_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                             uint32_t local_x, uint32_t local_y)
{
    if (!world || !g_entropy_initialized) return 0.5f;

    // Convert to universe coordinates
    UniverseCoord universe_coord = world_local_to_universe(world, world_x, world_y, world_z, local_x, local_y, 0);

    // Sample the vegetation entropy field
    return entropy_field_sample_2d(&g_vegetation_field, universe_coord.x, universe_coord.y);
}

// Sample structure placement probability at a specific world position
float world_sample_structure_probability_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                                uint32_t local_x, uint32_t local_y)
{
    if (!world || !g_entropy_initialized) return 0.5f;

    // Convert to universe coordinates
    UniverseCoord universe_coord = world_local_to_universe(world, world_x, world_y, world_z, local_x, local_y, 0);

    // Sample the structure entropy field
    return entropy_field_sample_2d(&g_structure_field, universe_coord.x, universe_coord.y);
}

// Composite generation function that uses all entropy fields
void world_generate_composite_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z)
{
    if (!world) return;

    printf("[ENTROPY] Starting composite generation for world (%d, %d, %d)\n", world_x, world_y, world_z);

    // Initialize entropy system if not already done
    if (!g_entropy_initialized) {
        world_entropy_generator_init(12345); // Default seed, should be passed in
    }

    // Generate in phases for better control
    world_generate_terrain_entropy(world, world_x, world_y, world_z);
    world_generate_ore_deposits_entropy(world, world_x, world_y, world_z);
    world_generate_vegetation_entropy(world, world_x, world_y, world_z);
    world_generate_structures_entropy(world, world_x, world_y, world_z);

    // The phases above assign to voxel->type directly, so anything the renderer had derived from
    // the voxel array no longer describes it. Rebuild rather than discard: this runs once per
    // world, and the renderer's face culling reads the bitfield every frame.
    world_refresh_occupancy_bitfield(world);
    world_invalidate_fluid_presence(world);

    printf("[ENTROPY] Completed composite generation for world (%d, %d, %d)\n", world_x, world_y, world_z);
}

// Get the current entropy field configuration
const EntropyField* world_entropy_get_terrain_field(void) { return g_entropy_initialized ? &g_terrain_field : NULL; }
const EntropyField* world_entropy_get_ore_field(void) { return g_entropy_initialized ? &g_ore_field : NULL; }
const EntropyField* world_entropy_get_vegetation_field(void) { return g_entropy_initialized ? &g_vegetation_field : NULL; }
const EntropyField* world_entropy_get_structure_field(void) { return g_entropy_initialized ? &g_structure_field : NULL; }

// Update entropy field parameters (for runtime tuning)
void world_entropy_update_terrain_field(const EntropyField *field)
{
    if (field && g_entropy_initialized) {
        g_terrain_field = *field;
        printf("[ENTROPY] Updated terrain field parameters\n");
    }
}

void world_entropy_update_ore_field(const EntropyField *field)
{
    if (field && g_entropy_initialized) {
        g_ore_field = *field;
        printf("[ENTROPY] Updated ore field parameters\n");
    }
}

void world_entropy_update_vegetation_field(const EntropyField *field)
{
    if (field && g_entropy_initialized) {
        g_vegetation_field = *field;
        printf("[ENTROPY] Updated vegetation field parameters\n");
    }
}

void world_entropy_update_structure_field(const EntropyField *field)
{
    if (field && g_entropy_initialized) {
        g_structure_field = *field;
        printf("[ENTROPY] Updated structure field parameters\n");
    }
}
