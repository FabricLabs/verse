#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "world.h"
#include "universe.h"
#include "constants.h"
#include "wfc3d.h"

// Composition statistics structure
typedef struct {
    uint64_t total_voxels;
    uint64_t air_count;
    uint64_t stone_count;
    uint64_t dirt_count;
    uint64_t sand_count;
    uint64_t water_count;
    uint64_t magma_count;
    uint64_t ore_counts[20]; // For different ore types
    uint64_t crystal_counts[5]; // For different crystal types
    uint64_t other_counts[10]; // For other voxel types
} WorldComposition;

// Global statistics
typedef struct {
    int total_worlds;
    int successful_worlds;
    WorldComposition totals;
    WorldComposition averages;
    WorldComposition min_values;
    WorldComposition max_values;
    double standard_deviations[50]; // For key metrics
} AnalysisResults;

// Function to analyze a single world's composition
static void analyze_world_composition(World *world, WorldComposition *comp) {
    if (!world || !comp) return;

    // Initialize composition
    memset(comp, 0, sizeof(WorldComposition));

    // Count voxels by type
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel *voxel = world_get_voxel(world, x, y, z);
                if (!voxel) continue;

                comp->total_voxels++;

                switch (voxel->type) {
                    case VOXEL_AIR:
                        comp->air_count++;
                        break;
                    case VOXEL_STONE:
                    case VOXEL_STONE_BASALT:
                    case VOXEL_STONE_GRANITE:
                    case VOXEL_STONE_LIMESTONE:
                    case VOXEL_STONE_SANDSTONE:
                        comp->stone_count++;
                        break;
                    case VOXEL_SOIL:
                    case VOXEL_SOIL_CLAY:
                    case VOXEL_SOIL_LOAM:
                    case VOXEL_SOIL_SILT:
                        comp->dirt_count++;
                        break;
                    case VOXEL_SAND:
                    case VOXEL_SAND_BASALT:
                    case VOXEL_SAND_GRANITE:
                    case VOXEL_SAND_LIMESTONE:
                    case VOXEL_SAND_SANDSTONE:
                        comp->sand_count++;
                        break;
                    case VOXEL_WATER:
                        comp->water_count++;
                        break;
                    case VOXEL_MAGMA:
                        comp->magma_count++;
                        break;
                    // Ore types
                    case VOXEL_ORE_PLATINUM:
                        comp->ore_counts[0]++;
                        break;
                    case VOXEL_ORE_ADAMANTITE:
                        comp->ore_counts[1]++;
                        break;
                    case VOXEL_ORE_HEMATITE:
                        comp->ore_counts[2]++;
                        break;
                    case VOXEL_ORE_GOLD:
                        comp->ore_counts[3]++;
                        break;
                    case VOXEL_ORE_SILVER:
                        comp->ore_counts[4]++;
                        break;
                    case VOXEL_ORE_MITHRIL:
                        comp->ore_counts[5]++;
                        break;
                    case VOXEL_ORE_COPPER:
                        comp->ore_counts[6]++;
                        break;
                    case VOXEL_ORE_IRON:
                        comp->ore_counts[7]++;
                        break;
                    case VOXEL_ORE_TIN:
                        comp->ore_counts[8]++;
                        break;
                    case VOXEL_ORE_LEAD:
                        comp->ore_counts[9]++;
                        break;
                    case VOXEL_ORE_ZINC:
                        comp->ore_counts[10]++;
                        break;
                    case VOXEL_ORE_TITANIUM:
                        comp->ore_counts[11]++;
                        break;
                    case VOXEL_ORE_COBALT:
                        comp->ore_counts[12]++;
                        break;
                    case VOXEL_ORE_NICKEL:
                        comp->ore_counts[13]++;
                        break;
                    case VOXEL_ORE_ALUMINUM:
                        comp->ore_counts[14]++;
                        break;
                    case VOXEL_ORE_MAGNESIUM:
                        comp->ore_counts[15]++;
                        break;
                    case VOXEL_ORE_COAL:
                        comp->ore_counts[16]++;
                        break;
                    // Crystal types
                    case VOXEL_CRYSTAL_RED:
                        comp->crystal_counts[0]++;
                        break;
                    case VOXEL_CRYSTAL_GREEN:
                        comp->crystal_counts[1]++;
                        break;
                    case VOXEL_CRYSTAL_BLUE:
                        comp->crystal_counts[2]++;
                        break;
                    default:
                        comp->other_counts[0]++;
                        break;
                }
            }
        }
    }
}

// Function to calculate statistics
static void calculate_statistics(WorldComposition *compositions, int count, AnalysisResults *results) {
    if (!compositions || !results || count <= 0) return;

    memset(results, 0, sizeof(AnalysisResults));
    results->total_worlds = count;
    results->successful_worlds = count;

    // Initialize min/max with first world
    results->min_values = compositions[0];
    results->max_values = compositions[0];

    // Calculate totals and min/max
    for (int i = 0; i < count; i++) {
        WorldComposition *comp = &compositions[i];

        // Add to totals
        results->totals.total_voxels += comp->total_voxels;
        results->totals.air_count += comp->air_count;
        results->totals.stone_count += comp->stone_count;
        results->totals.dirt_count += comp->dirt_count;
        results->totals.sand_count += comp->sand_count;
        results->totals.water_count += comp->water_count;
        results->totals.magma_count += comp->magma_count;

        for (int j = 0; j < 20; j++) {
            results->totals.ore_counts[j] += comp->ore_counts[j];
        }
        for (int j = 0; j < 5; j++) {
            results->totals.crystal_counts[j] += comp->crystal_counts[j];
        }
        for (int j = 0; j < 10; j++) {
            results->totals.other_counts[j] += comp->other_counts[j];
        }

        // Update min/max
        if (comp->total_voxels < results->min_values.total_voxels) {
            results->min_values.total_voxels = comp->total_voxels;
        }
        if (comp->total_voxels > results->max_values.total_voxels) {
            results->max_values.total_voxels = comp->total_voxels;
        }

        if (comp->air_count < results->min_values.air_count) {
            results->min_values.air_count = comp->air_count;
        }
        if (comp->air_count > results->max_values.air_count) {
            results->max_values.air_count = comp->air_count;
        }

        if (comp->stone_count < results->min_values.stone_count) {
            results->min_values.stone_count = comp->stone_count;
        }
        if (comp->stone_count > results->max_values.stone_count) {
            results->max_values.stone_count = comp->stone_count;
        }
    }

    // Calculate averages
    results->averages.total_voxels = results->totals.total_voxels / count;
    results->averages.air_count = results->totals.air_count / count;
    results->averages.stone_count = results->totals.stone_count / count;
    results->averages.dirt_count = results->totals.dirt_count / count;
    results->averages.sand_count = results->totals.sand_count / count;
    results->averages.water_count = results->totals.water_count / count;
    results->averages.magma_count = results->totals.magma_count / count;

    for (int j = 0; j < 20; j++) {
        results->averages.ore_counts[j] = results->totals.ore_counts[j] / count;
    }
    for (int j = 0; j < 5; j++) {
        results->averages.crystal_counts[j] = results->totals.crystal_counts[j] / count;
    }
    for (int j = 0; j < 10; j++) {
        results->averages.other_counts[j] = results->totals.other_counts[j] / count;
    }
}

// Function to print analysis results
static void print_analysis_results(AnalysisResults *results) {
    if (!results) return;

    printf("\n=== WORLD COMPOSITION ANALYSIS ===\n");
    printf("Total worlds analyzed: %d\n", results->total_worlds);
    printf("Successful generations: %d\n", results->successful_worlds);
    printf("\n");

    printf("=== AVERAGE COMPOSITION ===\n");
    printf("Total voxels: %llu\n", (unsigned long long)results->averages.total_voxels);
    printf("Air: %llu (%.2f%%)\n",
           (unsigned long long)results->averages.air_count,
           100.0 * results->averages.air_count / results->averages.total_voxels);
    printf("Stone: %llu (%.2f%%)\n",
           (unsigned long long)results->averages.stone_count,
           100.0 * results->averages.stone_count / results->averages.total_voxels);
    printf("Dirt: %llu (%.2f%%)\n",
           (unsigned long long)results->averages.dirt_count,
           100.0 * results->averages.dirt_count / results->averages.total_voxels);
    printf("Sand: %llu (%.2f%%)\n",
           (unsigned long long)results->averages.sand_count,
           100.0 * results->averages.sand_count / results->averages.total_voxels);
    printf("Water: %llu (%.2f%%)\n",
           (unsigned long long)results->averages.water_count,
           100.0 * results->averages.water_count / results->averages.total_voxels);
    printf("Magma: %llu (%.2f%%)\n",
           (unsigned long long)results->averages.magma_count,
           100.0 * results->averages.magma_count / results->averages.total_voxels);

    printf("\n=== AVERAGE ORE COMPOSITION ===\n");
    const char* ore_names[] = {
        "Platinum", "Adamantite", "Hematite", "Gold", "Silver", "Mithril",
        "Copper", "Iron", "Tin", "Lead", "Zinc", "Titanium", "Cobalt",
        "Nickel", "Aluminum", "Magnesium", "Coal"
    };

    for (int i = 0; i < 17; i++) {
        if (results->averages.ore_counts[i] > 0) {
            printf("%s: %llu (%.4f%%)\n",
                   ore_names[i],
                   (unsigned long long)results->averages.ore_counts[i],
                   100.0 * results->averages.ore_counts[i] / results->averages.total_voxels);
        }
    }

    printf("\n=== AVERAGE CRYSTAL COMPOSITION ===\n");
    const char* crystal_names[] = {"Red", "Green", "Blue"};
    for (int i = 0; i < 3; i++) {
        if (results->averages.crystal_counts[i] > 0) {
            printf("Crystal %s: %llu (%.4f%%)\n",
                   crystal_names[i],
                   (unsigned long long)results->averages.crystal_counts[i],
                   100.0 * results->averages.crystal_counts[i] / results->averages.total_voxels);
        }
    }

    printf("\n=== RANGE STATISTICS ===\n");
    printf("Total voxels - Min: %llu, Max: %llu\n",
           (unsigned long long)results->min_values.total_voxels,
           (unsigned long long)results->max_values.total_voxels);
    printf("Air voxels - Min: %llu, Max: %llu\n",
           (unsigned long long)results->min_values.air_count,
           (unsigned long long)results->max_values.air_count);
    printf("Stone voxels - Min: %llu, Max: %llu\n",
           (unsigned long long)results->min_values.stone_count,
           (unsigned long long)results->max_values.stone_count);

    printf("\n=== TOTAL RESOURCES ACROSS ALL WORLDS ===\n");
    uint64_t total_ores = 0;
    for (int i = 0; i < 17; i++) {
        total_ores += results->totals.ore_counts[i];
    }
    printf("Total ore voxels: %llu\n", (unsigned long long)total_ores);

    uint64_t total_crystals = 0;
    for (int i = 0; i < 3; i++) {
        total_crystals += results->totals.crystal_counts[i];
    }
    printf("Total crystal voxels: %llu\n", (unsigned long long)total_crystals);
}

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    printf("Generating and analyzing 100 worlds...\n");

    // Set universe noise seed for consistency
    world_set_universe_noise_seed("analysis_base_seed");

    const int num_worlds = 100;
    WorldComposition *compositions = malloc(num_worlds * sizeof(WorldComposition));
    if (!compositions) {
        fprintf(stderr, "Failed to allocate memory for compositions\n");
        return 1;
    }

    int successful_worlds = 0;

    for (int i = 0; i < num_worlds; i++) {
        char seed[64];
        snprintf(seed, sizeof(seed), "analysis_world_%d", i + 1);

        printf("Generating world %d/%d with seed: %s\n", i + 1, num_worlds, seed);

                        // Create world directly using the same approach as universe-generate-gameworld
        World *world = world_create(WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z);
        if (!world) {
            printf("Failed to create world %d\n", i + 1);
            continue;
        }

        // Generate the world using the same method as universe-generate-gameworld
        WorldCoord coord = {0, 0, 0}; // Generate at origin
        char *world_seed = world_generate_seed_for_coord(seed, coord);

        // Use WFC to decide world type and generate
        WorldGenerationType gtype;
        VoxelType fill;
        universe_wfc_decide_cell(UNIVERSE_GEN_GAMEWORLD, 0, 0, 0, &gtype, &fill);

        if (gtype == WORLD_TYPE_SOLID) {
            world_generate_with_type_and_fill(world, world_seed ? world_seed : seed, gtype, fill);
        } else {
            world_generate_with_type(world, world_seed ? world_seed : seed, gtype);
        }

        // Analyze composition
        analyze_world_composition(world, &compositions[successful_worlds]);
        successful_worlds++;

        // Clean up
        if (world_seed) free(world_seed);
        world_destroy(world);

        if ((i + 1) % 10 == 0) {
            printf("Completed %d worlds...\n", i + 1);
        }
    }

    printf("\nAnalysis complete. Processing results...\n");

    // Calculate statistics
    AnalysisResults results;
    calculate_statistics(compositions, successful_worlds, &results);

    // Print results
    print_analysis_results(&results);

    // Clean up
    free(compositions);

    printf("\nAnalysis completed successfully!\n");
    return 0;
}
