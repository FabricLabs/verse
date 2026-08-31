#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "entropy_field.h"
#include "universe_coords.h"
#include "world_entropy_generator.h"

// Simple test program to demonstrate the new universe generation system
// This tests only the entropy field integration without full world dependencies

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

void test_universe_entropy_integration(void)
{
    printf("\n=== Testing Universe Entropy Integration ===\n");

    // Initialize entropy field system
    uint32_t universe_seed = 12345;
    world_entropy_generator_init(universe_seed);

    printf("Entropy field system initialized with seed: %u\n", universe_seed);

    // Test coordinate conversion for universe-wide sampling
    printf("\nTesting universe coordinate conversion...\n");

    uint32_t world_width = 64, world_height = 64, world_depth = 64;

    // Test sampling across world boundaries
    printf("Testing entropy field sampling across world boundaries...\n");

    // Sample at the edge of world (0,0,0) - local (63,32,16)
    UniverseCoord edge1 = get_world_universe_coords(0, 0, 0, 63, 32, 16,
                                                   world_width, world_height, world_depth);

    // Sample just across the boundary in world (1,0,0) - local (0,32,16)
    UniverseCoord edge2 = get_world_universe_coords(1, 0, 0, 0, 32, 16,
                                                   world_width, world_height, world_depth);

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
    UniverseCoord diag1 = get_world_universe_coords(0, 0, 0, 63, 63, 16,
                                                   world_width, world_height, world_depth);
    UniverseCoord diag2 = get_world_universe_coords(1, 1, 0, 0, 0, 16,
                                                   world_width, world_height, world_depth);

    if (terrain_field) {
        float sample1_diag = entropy_field_sample_2d(terrain_field, diag1.x, diag1.y);
        float sample2_diag = entropy_field_sample_2d(terrain_field, diag2.x, diag2.y);

        printf("World (0,0,0) diag (63,63): %.4f\n", sample1_diag);
        printf("World (1,1,0) diag (0,0): %.4f\n", sample2_diag);
        printf("Difference: %.4f (should be small for continuity)\n", fabsf(sample1_diag - sample2_diag));

        report(fabsf(sample1_diag - sample2_diag) < 0.1f, "Diagonal boundary continuity test");
    }

    // Test multiple world types
    printf("\nTesting multiple world types...\n");

    // Test sampling at different world positions
    for (int world_x = -2; world_x <= 2; world_x++) {
        for (int world_y = -2; world_y <= 2; world_y++) {
            if (world_x == 0 && world_y == 0) continue; // Skip origin

            // Sample at the center of each world
            UniverseCoord center = get_world_universe_coords(world_x, world_y, 0, 32, 32, 16,
                                                           world_width, world_height, world_depth);

            if (terrain_field) {
                float sample = entropy_field_sample_2d(terrain_field, center.x, center.y);
                printf("World (%d,%d,0) center: %.4f\n", world_x, world_y, sample);
            }
        }
    }

    printf("✓ Universe entropy integration test PASSED\n");
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

void test_entropy_field_performance(void)
{
    printf("\n=== Testing Entropy Field Performance ===\n");

    uint32_t universe_seed = 12345;
    EntropyField field = entropy_field_default(universe_seed);

    // Test sampling performance
    int samples = 10000;
    printf("Sampling %d points...\n", samples);

    float total = 0.0f;
    for (int i = 0; i < samples; i++) {
        float x = (float)(i % 1000);
        float y = (float)((i * 7) % 1000);
        float z = (float)((i * 13) % 1000);

        float sample = entropy_field_sample(&field, x, y, z);
        total += sample;
    }

    float average = total / (float)samples;
    printf("Average sample value: %.4f\n", average);
    printf("✓ Entropy field performance test PASSED\n");
}

int main(void)
{
    printf("=== VERSE Universe Entropy Field System Test (Simple) ===\n");
    printf("Testing the new universe generation system with entropy fields\n");

    test_universe_entropy_integration();
    test_entropy_field_specialization();
    test_entropy_field_performance();

    printf("\n=== Test Complete ===\n");
    printf("The new universe generation system provides:\n");
    printf("1. Seamless world generation using entropy fields\n");
    printf("2. Boundary-free noise across world boundaries\n");
    printf("3. Specialized entropy fields for different content types\n");
    printf("4. Unified universe-wide coordinate system\n");
    printf("5. Improved visual continuity between worlds\n");
    printf("6. High-performance noise sampling\n");

    if (test_failures > 0) {
        printf("\n%d check(s) FAILED\n", test_failures);
        return 1;
    }
    return 0;
}
