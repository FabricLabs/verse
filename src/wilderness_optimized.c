#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "world.h"
#include "constants.h"
#include "universe.h"

// Optimized wilderness generation with early filtering
// This demonstrates Phase 1 optimizations from the optimization plan

// Pre-computed data structures for optimization
typedef struct {
    VoxelType stone_type;
    bool can_have_ore;
    bool can_have_crystal;
    float noise_value;
    float ore_probability;
    float crystal_probability;
} VoxelData;

// Optimized wilderness strata generation with early filtering
static int *apply_wilderness_strata_optimized(
    World *world,
    uint32_t surface_cap,
    uint32_t clay_cap_layers,
    uint64_t *dbg_count_magma,
    uint64_t *dbg_count_ore_cu,
    uint64_t *dbg_count_ore_ag,
    uint64_t *dbg_count_ore_au,
    uint64_t *dbg_count_crystals,
    bool limit_to_edge_band,
    uint32_t edge_band_thickness,
    bool enable_crystals)
{
    if (!world)
        return NULL;

    // Timing instrumentation
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    const uint32_t W = world->width, H = world->height, D = world->depth;
    int *top_of_column = (int *)malloc((size_t)W * (size_t)H * sizeof(int));
    if (!top_of_column)
        return NULL;
    for (uint32_t i = 0; i < W * H; i++)
        top_of_column[i] = -1;

    // Phase 1: Pre-compute voxel data for early filtering
    printf("Phase 1: Pre-computing voxel data...\n");
    struct timeval precompute_start, precompute_end;
    gettimeofday(&precompute_start, NULL);

    // Allocate voxel data array
    VoxelData *voxel_data = (VoxelData *)malloc((size_t)W * (size_t)H * (size_t)D * sizeof(VoxelData));
    if (!voxel_data) {
        free(top_of_column);
        return NULL;
    }

    // Pre-compute stone types and valid regions
    uint32_t valid_ore_count = 0;
    uint32_t valid_crystal_count = 0;
    uint32_t total_voxels = 0;

    for (uint32_t z = 1; z < D; z++) {
        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) {
                size_t idx = (size_t)z * (size_t)W * (size_t)H + (size_t)y * (size_t)W + (size_t)x;
                VoxelData *data = &voxel_data[idx];

                total_voxels++;

                // Skip edge band filtering if not needed
                if (limit_to_edge_band) {
                    bool on_edge = (x < edge_band_thickness) || (y < edge_band_thickness) ||
                                   (x >= W - edge_band_thickness) || (y >= H - edge_band_thickness);
                    if (!on_edge) {
                        data->can_have_ore = false;
                        data->can_have_crystal = false;
                        continue;
                    }
                }

                // Pre-compute noise for terrain height
                float combined_noise = sample_nine_phase_noise_field(world, 0.0f, (float)x, (float)y, (float)z, 1.0f, 1.0f, 1.0f);
                float surface_height = (float)(D - 1) * (0.6f + 0.4f * combined_noise);
                data->noise_value = combined_noise;

                // Skip if above surface height
                if ((float)z > surface_height) {
                    data->can_have_ore = false;
                    data->can_have_crystal = false;
                    continue;
                }

                // Pre-compute stone type
                float t = (float)z / (float)D;
                float base_thickness = 8.0f + 4.0f * combined_noise;
                uint32_t geological_layer = (uint32_t)(z / base_thickness);
                data->stone_type = get_geological_layer_type(world, x, y, geological_layer);

                // Pre-compute ore validity
                data->can_have_ore = is_stone_type_for_ores(data->stone_type);
                if (data->can_have_ore) {
                    // Check depth constraints
                    uint32_t top_no_ore_margin = (uint32_t)fmaxf(3.0f, floorf(0.09375f * (float)D));
                    if (top_no_ore_margin >= surface_cap)
                        top_no_ore_margin = surface_cap - 1;
                    if (z >= surface_cap - top_no_ore_margin) {
                        data->can_have_ore = false;
                    } else {
                        valid_ore_count++;
                        // Pre-compute ore probability
                        data->ore_probability = calculate_ore_probability(world, x, y, z, t);
                    }
                }

                // Pre-compute crystal validity
                data->can_have_crystal = enable_crystals && is_stone_type_for_crystals(data->stone_type);
                if (data->can_have_crystal) {
                    valid_crystal_count++;
                    // Pre-compute crystal probability
                    data->crystal_probability = calculate_crystal_probability(world, x, y, z, t);
                }
            }
        }
    }

    gettimeofday(&precompute_end, NULL);
    double precompute_time = (precompute_end.tv_sec - precompute_start.tv_sec) * 1000.0 +
                            (precompute_end.tv_usec - precompute_start.tv_usec) / 1000.0;

    printf("Pre-computation complete: %.2f ms\n", precompute_time);
    printf("Total voxels: %u, Valid ore voxels: %u (%.2f%%), Valid crystal voxels: %u (%.2f%%)\n",
           total_voxels, valid_ore_count, (float)valid_ore_count / total_voxels * 100.0f,
           valid_crystal_count, (float)valid_crystal_count / total_voxels * 100.0f);

    // Phase 2: Optimized generation using pre-computed data
    printf("Phase 2: Optimized generation...\n");
    struct timeval generation_start, generation_end;
    gettimeofday(&generation_start, NULL);

    uint32_t ore_placed = 0;
    uint32_t crystal_placed = 0;

    for (uint32_t z = 1; z < D; z++) {
        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) {
                size_t idx = (size_t)z * (size_t)W * (size_t)H + (size_t)y * (size_t)W + (size_t)x;
                VoxelData *data = &voxel_data[idx];

                // Skip if no valid generation possible
                if (!data->can_have_ore && !data->can_have_crystal) {
                    continue;
                }

                // Generate terrain (simplified for demo)
                if (data->stone_type != VOXEL_AIR) {
                    world_set_voxel(world, x, y, z, data->stone_type);
                }

                // Generate ore only if valid and probability check passes
                if (data->can_have_ore && data->ore_probability > 0.001f) {
                    VoxelType ore_type = generate_canonical_ore_type(world, x, y, z, (float)z / (float)D);
                    if (ore_type != VOXEL_AIR) {
                        paint_simple_ore_vein(world, (int)x, (int)y, (int)z, ore_type);
                        ore_placed++;

                        // Update debug counters
                        if (ore_type == VOXEL_ORE_GOLD && dbg_count_ore_au)
                            (*dbg_count_ore_au)++;
                        else if (ore_type == VOXEL_ORE_SILVER && dbg_count_ore_ag)
                            (*dbg_count_ore_ag)++;
                        else if (ore_type == VOXEL_ORE_COPPER && dbg_count_ore_cu)
                            (*dbg_count_ore_cu)++;
                    }
                }

                // Generate crystal only if valid and probability check passes
                if (data->can_have_crystal && data->crystal_probability > 0.001f) {
                    VoxelType crystal_type = generate_canonical_crystal_type(world, x, y, z, (float)z / (float)D);
                    if (crystal_type != VOXEL_AIR) {
                        Voxel *voxel = world_get_voxel(world, x, y, z);
                        if (voxel) {
                            voxel->type = crystal_type;
                            voxel_set_temperature(voxel, 255);
                            crystal_placed++;
                            if (dbg_count_crystals)
                                (*dbg_count_crystals)++;
                        }
                    }
                }

                // Update top of column
                size_t cidx = (size_t)y * (size_t)W + (size_t)x;
                if ((int)z > top_of_column[cidx])
                    top_of_column[cidx] = (int)z;
            }
        }
    }

    gettimeofday(&generation_end, NULL);
    double generation_time = (generation_end.tv_sec - generation_start.tv_sec) * 1000.0 +
                            (generation_end.tv_usec - generation_start.tv_usec) / 1000.0;

    // Complete timing
    gettimeofday(&end_time, NULL);
    double total_time = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                       (end_time.tv_usec - start_time.tv_usec) / 1000.0;

    printf("Generation complete: %.2f ms\n", generation_time);
    printf("Ore placed: %u, Crystal placed: %u\n", ore_placed, crystal_placed);
    printf("Total optimized time: %.2f ms\n", total_time);
    printf("Pre-computation: %.1f%%, Generation: %.1f%%\n",
           precompute_time / total_time * 100, generation_time / total_time * 100);

    // Cleanup
    free(voxel_data);
    (void)clay_cap_layers; // used by callers post-return
    return top_of_column;
}

// Helper functions for optimization

// Calculate ore probability for a voxel
static float calculate_ore_probability(World *world, uint32_t x, uint32_t y, uint32_t z, float t) {
    // Simplified probability calculation
    float depth_ratio = t;
    float ore_density_var = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.061f, 137.0f);
    float rarity = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.0033f, 97.0f);
    float surf_decay = expf(-0.5f * ((t - 0.18f) / 0.16f) * ((t - 0.18f) / 0.16f));

    return 0.0015f * 200.0f * surf_decay * ore_density_var * rarity;
}

// Calculate crystal probability for a voxel
static float calculate_crystal_probability(World *world, uint32_t x, uint32_t y, uint32_t z, float t) {
    // Simplified probability calculation
    float depth_ratio = t;
    float crystal_density_var = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.061f, 137.0f);
    float rarity = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.0033f, 97.0f);
    float surf_decay = expf(-0.5f * ((t - 0.18f) / 0.16f) * ((t - 0.18f) / 0.16f));

    return 0.0008f * 200.0f * surf_decay * crystal_density_var * rarity;
}

// Check if stone type is suitable for ores
static bool is_stone_type_for_ores(VoxelType stone_type) {
    return (stone_type == VOXEL_STONE_BASALT ||
            stone_type == VOXEL_STONE_GRANITE ||
            stone_type == VOXEL_STONE_LIMESTONE ||
            stone_type == VOXEL_STONE_SANDSTONE);
}

// Check if stone type is suitable for crystals
static bool is_stone_type_for_crystals(VoxelType stone_type) {
    return (stone_type == VOXEL_STONE_BASALT ||
            stone_type == VOXEL_STONE_GRANITE);
}

// Get geological layer type (simplified)
static VoxelType get_geological_layer_type(World *world, uint32_t x, uint32_t y, uint32_t layer) {
    // Simplified geological layer assignment
    switch (layer % 4) {
        case 0: return VOXEL_STONE_BASALT;
        case 1: return VOXEL_STONE_GRANITE;
        case 2: return VOXEL_STONE_LIMESTONE;
        case 3: return VOXEL_STONE_SANDSTONE;
        default: return VOXEL_STONE;
    }
}

// Sample nine-phase noise field (simplified)
static float sample_nine_phase_noise_field(World *world, float w, float x, float y, float z, float small_weight, float regional_weight, float universal_weight) {
    // Simplified noise sampling - use basic simplex noise for now
    return (float)((simplex_noise((double)w, (double)x * 0.01, (double)y * 0.01, (double)z * 0.01) + 1.0) / 2.0);
}

// Sample field noise (simplified)
static float sample_field_noise(World *world, float w, float x, float y, float z, float scale, float seed) {
    // Simplified noise sampling
    return (float)((simplex_noise((double)w, (double)x * scale, (double)y * scale, (double)z * scale + seed) + 1.0) / 2.0);
}

// Generate canonical ore type (simplified)
static VoxelType generate_canonical_ore_type(World *world, uint32_t x, uint32_t y, uint32_t z, float depth_ratio) {
    // Simplified ore type generation
    float ore_noise = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.061f, 137.0f);

    if (ore_noise < 0.1f) return VOXEL_ORE_GOLD;
    if (ore_noise < 0.2f) return VOXEL_ORE_SILVER;
    if (ore_noise < 0.4f) return VOXEL_ORE_COPPER;
    if (ore_noise < 0.7f) return VOXEL_ORE_IRON;
    if (ore_noise < 0.9f) return VOXEL_ORE_COAL;

    return VOXEL_AIR;
}

// Generate canonical crystal type (simplified)
static VoxelType generate_canonical_crystal_type(World *world, uint32_t x, uint32_t y, uint32_t z, float depth_ratio) {
    // Simplified crystal type generation
    float crystal_noise = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.061f, 137.0f);

    if (crystal_noise < 0.05f) return VOXEL_CRYSTAL;

    return VOXEL_AIR;
}

// Paint simple ore vein (simplified)
static void paint_simple_ore_vein(World *world, int x, int y, int z, VoxelType ore_type) {
    // Simplified ore vein placement
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, ore_type);
}
