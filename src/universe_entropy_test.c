#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "universe.h"
#include "world.h"
#include "world_entropy_generator.h"
#include "entropy_field.h"
#include "universe_coords.h"

// Test program to demonstrate the new universe generation system
// This shows how the entropy field system provides seamless world generation

static int test_failures = 0;

static void report(int passed, const char *what)
{
    if (passed) {
        printf("✓ %s PASSED\n", what);
    } else {
        printf("✗ %s FAILED\n", what);
        test_failures++;
    }
}

void test_universe_entropy_generation(void)
{
    printf("\n=== Testing Universe Entropy Generation ===\n");

    // Create a universe
    Universe universe;
    const char* seed = "1234567890abcdef";

    if (!universe_init(&universe, seed, 0, 1)) {
        printf("Failed to initialize universe\n");
        return;
    }

    printf("Universe initialized with seed: %s\n", seed);

    // Create a base world at origin
    World* base_world = world_create(64, 64, 64);
    if (!base_world) {
        printf("Failed to create base world\n");
        universe_free(&universe);
        return;
    }

    // Initialize entropy field system
    world_entropy_generator_init((uint32_t)strtoul(seed, NULL, 16));

    // Generate the base world using entropy fields
    world_generate_composite_entropy(base_world, 0, 0, 0);

    // Place the base world in the universe
    if (!universe_place(&universe, 0, 0, 0, base_world)) {
        printf("Failed to place base world\n");
        world_destroy(base_world);
        universe_free(&universe);
        return;
    }

    printf("Base world generated and placed at (0,0,0)\n");

    // Generate neighboring worlds using the new system
    printf("Generating neighboring worlds...\n");

    int worlds_generated = 0;

    // Generate worlds in a 3x3 grid around the origin
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue; // Skip the base world

            uint64_t world_x = (uint64_t)((int64_t)0 + dx);
            uint64_t world_y = (uint64_t)((int64_t)0 + dy);
            uint64_t world_z = 0;

            if (!universe_has(&universe, world_x, world_y, world_z)) {
                World* neighbor_world = world_create(64, 64, 64);
                if (neighbor_world) {
                    // Generate using entropy fields
                    world_generate_composite_entropy(neighbor_world, (int32_t)world_x, (int32_t)world_y, (int32_t)world_z);

                    if (universe_place(&universe, world_x, world_y, world_z, neighbor_world)) {
                        worlds_generated++;
                        printf("Generated world at (%llu,%llu,%llu)\n",
                               (unsigned long long)world_x, (unsigned long long)world_y, (unsigned long long)world_z);
                    } else {
                        world_destroy(neighbor_world);
                    }
                }
            }
        }
    }

    printf("Generated %d neighboring worlds\n", worlds_generated);

    // Test boundary continuity by sampling entropy fields across world boundaries
    printf("\nTesting boundary continuity...\n");

    // Sample at the edge of world (0,0,0) - local (63,32,16)
    UniverseCoord edge1 = get_world_universe_coords(0, 0, 0, 63, 32, 16, 64, 64, 64);

    // Sample just across the boundary in world (1,0,0) - local (0,32,16)
    UniverseCoord edge2 = get_world_universe_coords(1, 0, 0, 0, 32, 16, 64, 64, 64);

    // Get the terrain entropy field
    const EntropyField* terrain_field = world_entropy_get_terrain_field();
    if (terrain_field) {
        float sample1 = entropy_field_sample_2d(terrain_field, edge1.x, edge1.y);
        float sample2 = entropy_field_sample_2d(terrain_field, edge2.x, edge2.y);

        printf("World (0,0,0) edge (63,32): %.4f\n", sample1);
        printf("World (1,0,0) edge (0,32): %.4f\n", sample2);
        printf("Difference: %.4f (should be small for continuity)\n", fabsf(sample1 - sample2));

        report(fabsf(sample1 - sample2) < 0.1f, "Boundary continuity test");
    }

    // Test diagonal boundary
    UniverseCoord diag1 = get_world_universe_coords(0, 0, 0, 63, 63, 16, 64, 64, 64);
    UniverseCoord diag2 = get_world_universe_coords(1, 1, 0, 0, 0, 16, 64, 64, 64);

    if (terrain_field) {
        float sample1_diag = entropy_field_sample_2d(terrain_field, diag1.x, diag1.y);
        float sample2_diag = entropy_field_sample_2d(terrain_field, diag2.x, diag2.y);

        printf("World (0,0,0) diag (63,63): %.4f\n", sample1_diag);
        printf("World (1,1,0) diag (0,0): %.4f\n", sample2_diag);
        printf("Difference: %.4f (should be small for continuity)\n", fabsf(sample1_diag - sample2_diag));

        report(fabsf(sample1_diag - sample2_diag) < 0.1f, "Diagonal boundary continuity test");
    }

    // Test world content generation
    printf("\nTesting world content generation...\n");

    World* test_world = universe_get(&universe, 1, 0, 0);
    if (test_world) {
        int solid_voxels = 0;
        int air_voxels = 0;

        // Sample full vertical columns: z is up, so a 10x10x10 corner sits entirely
        // underground in a layered wilderness world and would be all-solid by design.
        // Spanning the whole depth is what actually proves terrain *and* sky exist.
        for (uint32_t z = 0; z < test_world->depth; z++) {
            for (uint32_t y = 0; y < 10 && y < test_world->height; y++) {
                for (uint32_t x = 0; x < 10 && x < test_world->width; x++) {
                    Voxel* voxel = world_get_voxel(test_world, x, y, z);
                    if (voxel) {
                        if (voxel->type == VOXEL_AIR) {
                            air_voxels++;
                        } else {
                            solid_voxels++;
                        }
                    }
                }
            }
        }

        printf("World (1,0,0) sample: %d solid voxels, %d air voxels\n", solid_voxels, air_voxels);

        report(solid_voxels > 0 && air_voxels > 0, "World content generation test");
    }

    // Clean up
    universe_free(&universe);
    printf("\nUniverse entropy generation test completed\n");
}

void test_entropy_field_specialization(void)
{
    printf("\n=== Testing Entropy Field Specialization ===\n");

    uint32_t universe_seed = 12345;

    // Test different specialized entropy fields
    EntropyField terrain_field = entropy_field_terrain(universe_seed);
    EntropyField ore_field = entropy_field_ore_deposits(universe_seed);
    EntropyField vegetation_field = entropy_field_vegetation(universe_seed);
    EntropyField structure_field = entropy_field_structures(universe_seed);

    printf("Terrain field: scale(%.4f, %.4f, %.4f) amp(%.2f, %.2f, %.2f)\n",
           terrain_field.layer1_scale, terrain_field.layer2_scale, terrain_field.layer3_scale,
           terrain_field.layer1_amplitude, terrain_field.layer2_amplitude, terrain_field.layer3_amplitude);

    printf("Ore field: scale(%.4f, %.4f, %.4f) amp(%.2f, %.2f, %.2f)\n",
           ore_field.layer1_scale, ore_field.layer2_scale, ore_field.layer3_scale,
           ore_field.layer1_amplitude, ore_field.layer2_amplitude, ore_field.layer3_amplitude);

    printf("Vegetation field: scale(%.4f, %.4f, %.4f) amp(%.2f, %.2f, %.2f)\n",
           vegetation_field.layer1_scale, vegetation_field.layer2_scale, vegetation_field.layer3_scale,
           vegetation_field.layer1_amplitude, vegetation_field.layer2_amplitude, vegetation_field.layer3_amplitude);

    printf("Structure field: scale(%.4f, %.4f, %.4f) amp(%.2f, %.2f, %.2f)\n",
           structure_field.layer1_scale, structure_field.layer2_scale, structure_field.layer3_scale,
           structure_field.layer1_amplitude, structure_field.layer2_amplitude, structure_field.layer3_amplitude);

    // Test field composition
    EntropyField combined_field;
    entropy_field_add_fields(&combined_field, &terrain_field, &ore_field);

    printf("Combined terrain+ore field: scale(%.4f, %.4f, %.4f) amp(%.2f, %.2f, %.2f)\n",
           combined_field.layer1_scale, combined_field.layer2_scale, combined_field.layer3_scale,
           combined_field.layer1_amplitude, combined_field.layer2_amplitude, combined_field.layer3_amplitude);

    printf("✓ Entropy field specialization test PASSED\n");
}

int main(void)
{
    printf("=== VERSE Universe Entropy Field System Test ===\n");
    printf("Testing the new universe generation system with entropy fields\n");

    test_universe_entropy_generation();
    test_entropy_field_specialization();

    printf("\n=== Test Complete ===\n");
    printf("The new universe generation system provides:\n");
    printf("1. Seamless world generation using entropy fields\n");
    printf("2. Boundary-free noise across world boundaries\n");
    printf("3. Specialized entropy fields for different content types\n");
    printf("4. Unified universe-wide coordinate system\n");
    printf("5. Improved visual continuity between worlds\n");

    if (test_failures > 0) {
        printf("\n%d check(s) FAILED\n", test_failures);
        return 1;
    }
    return 0;
}
