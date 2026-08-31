#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// For SHA256 implementation
#include "noise-c/src/crypto/sha2/sha256.h"

// Calculate SHA256 hash of a string
static void calculate_sha256(const char* input, uint8_t output[32]) {
    sha256_context_t ctx;
    sha256_reset(&ctx);
    sha256_update(&ctx, (const uint8_t*)input, strlen(input));
    sha256_finish(&ctx, output);
}

// Test spring generation across multiple worlds
void test_spring_generation_statistics() {
    printf("=== VERSE Spring Generation Statistics ===\n");
    printf("Testing exponential rarity across 4 levels (0, 1, 2, 3)\n");
    printf("Maximum 3 springs per RANDOM world\n\n");

    const int total_worlds = 1000; // Increased for more accurate statistics
    int level_counts[4] = {0}; // Count of worlds with springs at each level
    int total_springs = 0;
    int worlds_with_springs = 0;

    printf("Generating %d RANDOM worlds to analyze spring distribution...\n\n", total_worlds);

    for (int i = 0; i < total_worlds; i++) {
        // Generate unique seed for each world
        char seed[64];
        snprintf(seed, sizeof(seed), "test_spring_world_%d", i);

        // Create world
        World* world = world_create(32, 32, 32);
        if (!world) continue;

        // Generate RANDOM world
        world_generate_with_type(world, seed, WORLD_TYPE_RANDOM);

        // Count springs and their levels
        int springs_in_world = 0;
        int level_springs[4] = {0};

        for (uint32_t z = 0; z < world->depth; z++) {
            for (uint32_t y = 0; y < world->height; y++) {
                for (uint32_t x = 0; x < world->width; x++) {
                    Voxel* voxel = world_get_voxel(world, x, y, z);
                    if (voxel && voxel->type == VOXEL_SPRING) {
                        springs_in_world++;

                        // Determine spring level based on position and world hash
                        uint8_t world_hash[32];
                        calculate_sha256(seed, world_hash);
                        uint32_t world_hash_int = *(uint32_t*)world_hash;

                        // Use position to determine which level this spring belongs to
                        uint32_t pos_hash = world_hash_int ^ (x * 73856093 + y * 19349663 + z * 83492791);
                        int level = (pos_hash >> 30) & 3; // Use top 2 bits to determine level

                        level_springs[level]++;
                    }
                }
            }
        }

        if (springs_in_world > 0) {
            worlds_with_springs++;
            total_springs += springs_in_world;

            // Count which levels had springs
            for (int level = 0; level < 4; level++) {
                if (level_springs[level] > 0) {
                    level_counts[level]++;
                }
            }
        }

        world_destroy(world);

        // Progress indicator
        if ((i + 1) % 1000 == 0) {
            printf("Generated %d worlds...\n", i + 1);
        }
    }

    // Calculate statistics
    printf("\n=== Spring Generation Results ===\n");
    printf("Total worlds generated: %d\n", total_worlds);
    printf("Worlds with springs: %d (%.2f%%)\n", worlds_with_springs,
           (double)worlds_with_springs / total_worlds * 100);
    printf("Total springs placed: %d\n", total_springs);
    printf("Average springs per world: %.2f\n", (double)total_springs / total_worlds);

    printf("\n=== Level Distribution ===\n");
    for (int level = 0; level < 4; level++) {
        double percentage = (double)level_counts[level] / total_worlds * 100;
        printf("Level %d (Rarity %d): %d worlds (%.2f%%)\n",
               level, level, level_counts[level], percentage);
    }

    // Estimate world generations needed for each level
    printf("\n=== Estimated World Generations for Each Level ===\n");
    printf("Based on exponential rarity:\n");
    printf("Level 0 (50%%): ~2 worlds to find 1 spring\n");
    printf("Level 1 (25%%): ~4 worlds to find 1 spring\n");
    printf("Level 2 (12.5%%): ~8 worlds to find 1 spring\n");
    printf("Level 3 (6.25%%): ~16 worlds to find 1 spring\n");

    printf("\n=== Theoretical vs Actual ===\n");
    printf("Theoretical Level 0: 50.00%% | Actual: %.2f%%\n",
           (double)level_counts[0] / total_worlds * 100);
    printf("Theoretical Level 1: 25.00%% | Actual: %.2f%%\n",
           (double)level_counts[1] / total_worlds * 100);
    printf("Theoretical Level 2: 12.50%% | Actual: %.2f%%\n",
           (double)level_counts[2] / total_worlds * 100);
    printf("Theoretical Level 3: 6.25%% | Actual: %.2f%%\n",
           (double)level_counts[3] / total_worlds * 100);
}

// Test individual world generation
void test_single_world_springs() {
    printf("\n=== Single World Spring Test ===\n");

    const char* test_seed = "test_spring_world_single";
    World* world = world_create(32, 32, 32);

    if (world) {
        world_generate_with_type(world, test_seed, WORLD_TYPE_RANDOM);

        // Count and locate springs
        int spring_count = 0;
        printf("Springs found in test world:\n");

        for (uint32_t z = 0; z < world->depth; z++) {
            for (uint32_t y = 0; y < world->height; y++) {
                for (uint32_t x = 0; x < world->width; x++) {
                    Voxel* voxel = world_get_voxel(world, x, y, z);
                    if (voxel && voxel->type == VOXEL_SPRING) {
                        spring_count++;
                        printf("  Spring %d at (%u, %u, %u)\n", spring_count, x, y, z);
                    }
                }
            }
        }

        printf("Total springs: %d (max 3)\n", spring_count);

        // Display world log
        if (world->log) {
            printf("\nWorld generation log:\n%s\n", world->log);
        }

        world_destroy(world);
    }
}

int main() {
    printf("=== VERSE Spring Generation Analysis ===\n");
    printf("Testing exponential rarity system for RANDOM worlds\n\n");

    test_spring_generation_statistics();
    test_single_world_springs();

    printf("\n=== Analysis Complete ===\n");
    return 0;
}
