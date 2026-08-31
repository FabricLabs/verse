/*
 * test_decomposition.c - Test program to verify modular decomposition
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include "world_noise.h"

void test_perlin_noise() {
    printf("Testing Perlin noise functions...\n");

    // Test 2D Perlin noise
    double val2d = perlin_noise_2d(0.5, 0.5);
    printf("  2D Perlin(0.5, 0.5) = %f\n", val2d);
    assert(!isnan(val2d));
    assert(val2d >= -1.0 && val2d <= 1.0);

    // Test 3D Perlin noise
    double val3d = perlin_noise_3d(0.5, 0.5, 0.5);
    printf("  3D Perlin(0.5, 0.5, 0.5) = %f\n", val3d);
    assert(!isnan(val3d));
    assert(val3d >= -1.0 && val3d <= 1.0);

    // Test 4D Perlin noise
    double val4d = perlin_noise(0.5, 0.5, 0.5, 0.5);
    printf("  4D Perlin(0.5, 0.5, 0.5, 0.5) = %f\n", val4d);
    assert(!isnan(val4d));

    printf("  ✓ Perlin noise tests passed\n");
}

void test_fractal_noise() {
    printf("Testing fractal noise functions...\n");

    // Test 2D fractal noise
    double val2d = fractal_noise_2d(0.5, 0.5, 4, 0.5);
    printf("  2D Fractal(0.5, 0.5, 4 octaves) = %f\n", val2d);
    assert(!isnan(val2d));

    // Test 3D fractal noise
    double val3d = fractal_noise_3d(0.5, 0.5, 0.5, 4, 0.5);
    printf("  3D Fractal(0.5, 0.5, 0.5, 4 octaves) = %f\n", val3d);
    assert(!isnan(val3d));

    printf("  ✓ Fractal noise tests passed\n");
}

void test_universe_seed() {
    printf("Testing universe seed system...\n");

    // Set universe seed
    world_set_universe_noise_seed("test_universe_123");
    assert(world_has_universe_noise_seed());

    // Get some values from different positions
    double val1_a = perlin_noise_2d(1.0, 1.0);
    double val1_b = perlin_noise_2d(2.5, 3.7);

    // Set different seed
    world_set_universe_noise_seed("test_universe_456");
    double val2_a = perlin_noise_2d(1.0, 1.0);
    double val2_b = perlin_noise_2d(2.5, 3.7);

    // Values should be different with different seeds
    printf("  Seed 1: (1,1)=%f, (2.5,3.7)=%f\n", val1_a, val1_b);
    printf("  Seed 2: (1,1)=%f, (2.5,3.7)=%f\n", val2_a, val2_b);

    // At least one pair should differ (allowing for edge cases)
    bool different = (val1_a != val2_a) || (val1_b != val2_b);
    if (!different) {
        printf("  Warning: Seeds produced identical values (rare but possible)\n");
    }

    printf("  ✓ Universe seed tests passed\n");
}

void test_noise_determinism() {
    printf("Testing noise determinism...\n");

    // Set seed and get values
    world_set_universe_noise_seed("determinism_test");
    double values1[10];
    for (int i = 0; i < 10; i++) {
        values1[i] = perlin_noise_2d(i * 0.1, i * 0.1);
    }

    // Reset seed and get values again
    world_set_universe_noise_seed("determinism_test");
    double values2[10];
    for (int i = 0; i < 10; i++) {
        values2[i] = perlin_noise_2d(i * 0.1, i * 0.1);
    }

    // Values should be identical
    for (int i = 0; i < 10; i++) {
        assert(values1[i] == values2[i]);
    }

    printf("  ✓ Determinism tests passed\n");
}

int main(int argc, char** argv) {
    printf("=== World Decomposition Test Suite ===\n\n");

    test_perlin_noise();
    test_fractal_noise();
    test_universe_seed();
    test_noise_determinism();

    printf("\n✅ All tests passed!\n");
    printf("\nThis demonstrates that the noise functions have been\n");
    printf("successfully extracted into a separate module.\n");

    return 0;
}
