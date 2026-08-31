#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "entropy_field.h"
#include "universe_coords.h"
#include "world_entropy_generator.h"

// Test program to demonstrate the new entropy field system
// This shows how the system provides seamless noise across world boundaries

void test_entropy_field_basic(void)
{
    printf("\n=== Testing Basic Entropy Field ===\n");

    // Initialize entropy field system
    uint32_t universe_seed = 12345;
    world_entropy_generator_init(universe_seed);

    // Test basic sampling
    EntropyField field = entropy_field_default(universe_seed);

    // Sample at different coordinates
    float sample1 = entropy_field_sample(&field, 0.0f, 0.0f, 0.0f);
    float sample2 = entropy_field_sample(&field, 100.0f, 100.0f, 50.0f);
    float sample3 = entropy_field_sample(&field, 1000.0f, 1000.0f, 100.0f);

    printf("Sample at (0,0,0): %.4f\n", sample1);
    printf("Sample at (100,100,50): %.4f\n", sample2);
    printf("Sample at (1000,1000,100): %.4f\n", sample3);

    // Test 2D sampling
    float sample2d = entropy_field_sample_2d(&field, 500.0f, 500.0f);
    printf("2D Sample at (500,500): %.4f\n", sample2d);
}

void test_universe_coordinates(void)
{
    printf("\n=== Testing Universe Coordinates ===\n");

    // Test coordinate conversion
    uint32_t world_width = 64, world_height = 64, world_depth = 64;

    // Test world (1,1,0) with local position (32,32,16)
    UniverseCoord universe_coord = get_world_universe_coords(1, 1, 0, 32, 32, 16,
                                                           world_width, world_height, world_depth);
    printf("World (1,1,0) local (32,32,16) -> Universe (%.1f, %.1f, %.1f)\n",
           universe_coord.x, universe_coord.y, universe_coord.z);

    // Convert back to world coordinates
    UniverseWorldCoord world_coord = universe_to_world_coords(&universe_coord, world_width, world_height, world_depth);
    printf("Universe (%.1f, %.1f, %.1f) -> World (%d,%d,%d) local (%u,%u,%u)\n",
           universe_coord.x, universe_coord.y, universe_coord.z,
           world_coord.world_x, world_coord.world_y, world_coord.world_z,
           world_coord.local_x, world_coord.local_y, world_coord.local_z);

    // Test boundary case - world (0,0,0) with local position (63,63,63)
    universe_coord = get_world_universe_coords(0, 0, 0, 63, 63, 63,
                                             world_width, world_height, world_depth);
    printf("World (0,0,0) local (63,63,63) -> Universe (%.1f, %.1f, %.1f)\n",
           universe_coord.x, universe_coord.y, universe_coord.z);
}

void test_entropy_field_composition(void)
{
    printf("\n=== Testing Entropy Field Composition ===\n");

    uint32_t universe_seed = 12345;

    // Create specialized fields
    EntropyField terrain_field = entropy_field_terrain(universe_seed);
    EntropyField ore_field = entropy_field_ore_deposits(universe_seed);
    (void)entropy_field_vegetation(universe_seed); // Suppress unused variable warning
    (void)entropy_field_structures(universe_seed); // Suppress unused variable warning

    printf("Terrain field: scale(%.4f, %.4f, %.4f) amp(%.2f, %.2f, %.2f)\n",
           terrain_field.layer1_scale, terrain_field.layer2_scale, terrain_field.layer3_scale,
           terrain_field.layer1_amplitude, terrain_field.layer2_amplitude, terrain_field.layer3_amplitude);

    printf("Ore field: scale(%.4f, %.4f, %.4f) amp(%.2f, %.2f, %.2f)\n",
           ore_field.layer1_scale, ore_field.layer2_scale, ore_field.layer3_scale,
           ore_field.layer1_amplitude, ore_field.layer2_amplitude, ore_field.layer3_amplitude);

    // Test composition
    EntropyField combined_field;
    entropy_field_add_fields(&combined_field, &terrain_field, &ore_field);

    printf("Combined field: scale(%.4f, %.4f, %.4f) amp(%.2f, %.2f, %.2f)\n",
           combined_field.layer1_scale, combined_field.layer2_scale, combined_field.layer3_scale,
           combined_field.layer1_amplitude, combined_field.layer2_amplitude, combined_field.layer3_amplitude);
}

void test_boundary_continuity(void)
{
    printf("\n=== Testing Boundary Continuity ===\n");

    uint32_t universe_seed = 12345;
    world_entropy_generator_init(universe_seed);

    uint32_t world_width = 64, world_height = 64, world_depth = 64;

    // Test sampling across world boundaries
    printf("Testing continuity across world boundaries...\n");

    // Sample at the edge of world (0,0,0) - local (63,32,16)
    UniverseCoord edge1 = get_world_universe_coords(0, 0, 0, 63, 32, 16,
                                                   world_width, world_height, world_depth);

    // Sample just across the boundary in world (1,0,0) - local (0,32,16)
    UniverseCoord edge2 = get_world_universe_coords(1, 0, 0, 0, 32, 16,
                                                   world_width, world_height, world_depth);

    // Sample the terrain field at both points
    const EntropyField* terrain_field = world_entropy_get_terrain_field();
    if (terrain_field) {
        float sample1 = entropy_field_sample_2d(terrain_field, edge1.x, edge1.y);
        float sample2 = entropy_field_sample_2d(terrain_field, edge2.x, edge2.y);

        printf("World (0,0,0) edge (63,32): %.4f\n", sample1);
        printf("World (1,0,0) edge (0,32): %.4f\n", sample2);
        printf("Difference: %.4f (should be small for continuity)\n", fabsf(sample1 - sample2));
    }

    // Test diagonal boundary
    UniverseCoord diag1 = get_world_universe_coords(0, 0, 0, 63, 63, 16,
                                                   world_width, world_height, world_depth);
    UniverseCoord diag2 = get_world_universe_coords(1, 1, 0, 0, 0, 16,
                                                   world_width, world_height, world_depth);

    if (terrain_field) {
        float sample1 = entropy_field_sample_2d(terrain_field, diag1.x, diag1.y);
        float sample2 = entropy_field_sample_2d(terrain_field, diag2.x, diag2.y);

        printf("World (0,0,0) diag (63,63): %.4f\n", sample1);
        printf("World (1,1,0) diag (0,0): %.4f\n", sample2);
        printf("Difference: %.4f (should be small for continuity)\n", fabsf(sample1 - sample2));
    }
}

void test_entropy_field_layers(void)
{
    printf("\n=== Testing Entropy Field Layers ===\n");

    uint32_t universe_seed = 12345;
    EntropyField field = entropy_field_default(universe_seed);

    float x = 100.0f, y = 200.0f, z = 50.0f;

    // Sample individual layers
    float layer1 = entropy_field_sample_layer(&field, 1, x, y, z);
    float layer2 = entropy_field_sample_layer(&field, 2, x, y, z);
    float layer3 = entropy_field_sample_layer(&field, 3, x, y, z);

    // Sample combined field
    float combined = entropy_field_sample(&field, x, y, z);

    printf("Sample at (%.1f, %.1f, %.1f):\n", x, y, z);
    printf("  Layer 1: %.4f\n", layer1);
    printf("  Layer 2: %.4f\n", layer2);
    printf("  Layer 3: %.4f\n", layer3);
    printf("  Combined: %.4f\n", combined);

    // Verify that combined is approximately the weighted sum
    float expected = field.layer1_amplitude * layer1 +
                     field.layer2_amplitude * layer2 +
                     field.layer3_amplitude * layer3;
    expected = (expected + 1.0f) * 0.5f; // Normalize to [0,1]

    printf("  Expected: %.4f\n", expected);
    printf("  Difference: %.4f\n", fabsf(combined - expected));
}

int main(void)
{
    printf("=== VERSE Entropy Field System Test ===\n");
    printf("Testing the new universe-wide entropy field system\n");

    test_entropy_field_basic();
    test_universe_coordinates();
    test_entropy_field_composition();
    test_boundary_continuity();
    test_entropy_field_layers();

    printf("\n=== Test Complete ===\n");
    printf("The entropy field system provides:\n");
    printf("1. Seamless noise across world boundaries\n");
    printf("2. Three-phase noise generation with configurable layers\n");
    printf("3. Domain warping for natural variation\n");
    printf("4. Specialized fields for different generation types\n");
    printf("5. Universe-wide coordinate system\n");

    return 0;
}
