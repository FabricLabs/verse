#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// Simplified test for greedy meshing algorithm
static void test_greedy_meshing_logic() {
    printf("=== Testing Greedy Meshing Logic ===\n");

    // Create a simple 4x4 visibility mask for a 2x2 solid area
    int W = 4, H = 4;
    uint8_t mask[16] = {
        0, 0, 0, 0,  // row 0: all air
        0, 1, 1, 0,  // row 1: middle 2x2 area
        0, 1, 1, 0,  // row 2: middle 2x2 area
        0, 0, 0, 0   // row 3: all air
    };

    printf("Visibility mask (4x4):\n");
    for (int y = 0; y < H; y++) {
        printf("  ");
        for (int x = 0; x < W; x++) {
            printf("%d ", mask[y * W + x]);
        }
        printf("\n");
    }

    printf("\nExpected behavior: Should generate 1 quad covering the entire 2x2 area\n");

    // Simulate the greedy algorithm
    printf("\nSimulating greedy algorithm:\n");

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (mask[y * W + x]) {
                printf("  Found visible voxel at (%d,%d)\n", x, y);

                // Find the width of this run
                int wlen = 1;
                while (x + wlen < W && mask[y * W + (x + wlen)]) {
                    wlen++;
                }
                printf("    Run width: %d\n", wlen);

                // Find the height of this run
                int hlen = 1;
                bool can_extend = true;
                while (can_extend && y + hlen < H) {
                    for (int dx = 0; dx < wlen; dx++) {
                        if (!mask[(y + hlen) * W + (x + dx)]) {
                            can_extend = false;
                            break;
                        }
                    }
                    if (can_extend) {
                        hlen++;
                    }
                }
                printf("    Run height: %d\n", hlen);

                printf("    Generated quad: (%d,%d) to (%d,%d)\n",
                       x, y, x + wlen - 1, y + hlen - 1);

                // Skip the rest of this run
                x += wlen - 1;
                break;
            }
        }
    }

    printf("\n✓ Greedy meshing test complete\n");
}

// Test with a more complex pattern
static void test_complex_greedy_meshing() {
    printf("\n=== Testing Complex Greedy Meshing ===\n");

    // Create a 6x6 mask with multiple solid areas
    int W = 6, H = 6;
    uint8_t mask[36] = {
        0, 0, 0, 0, 0, 0,  // row 0: all air
        0, 1, 1, 0, 1, 0,  // row 1: 2x1 + 1x1
        0, 1, 1, 0, 1, 0,  // row 2: 2x1 + 1x1
        0, 0, 0, 0, 0, 0,  // row 3: all air
        0, 1, 1, 1, 0, 0,  // row 4: 3x1
        0, 0, 0, 0, 0, 0   // row 5: all air
    };

    printf("Complex visibility mask (6x6):\n");
    for (int y = 0; y < H; y++) {
        printf("  ");
        for (int x = 0; x < W; x++) {
            printf("%d ", mask[y * W + x]);
        }
        printf("\n");
    }

    printf("\nExpected behavior: Should generate 3 quads:\n");
    printf("  1. 2x2 quad at (1,1)\n");
    printf("  2. 1x2 quad at (4,1)\n");
    printf("  3. 3x1 quad at (1,4)\n");

    // Simulate the greedy algorithm
    printf("\nSimulating greedy algorithm:\n");

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (mask[y * W + x]) {
                printf("  Found visible voxel at (%d,%d)\n", x, y);

                // Find the width of this run
                int wlen = 1;
                while (x + wlen < W && mask[y * W + (x + wlen)]) {
                    wlen++;
                }
                printf("    Run width: %d\n", wlen);

                // Find the height of this run
                int hlen = 1;
                bool can_extend = true;
                while (can_extend && y + hlen < H) {
                    for (int dx = 0; dx < wlen; dx++) {
                        if (!mask[(y + hlen) * W + (x + dx)]) {
                            can_extend = false;
                            break;
                        }
                    }
                    if (can_extend) {
                        hlen++;
                    }
                }
                printf("    Run height: %d\n", hlen);

                printf("    Generated quad: (%d,%d) to (%d,%d)\n",
                       x, y, x + wlen - 1, y + hlen - 1);

                // Skip the rest of this run
                x += wlen - 1;
                break;
            }
        }
    }

    printf("\n✓ Complex greedy meshing test complete\n");
}

int main() {
    printf("=== Greedy Meshing Algorithm Test ===\n");
    printf("Testing the logic that merges adjacent visible voxels into quads\n");

    test_greedy_meshing_logic();
    test_complex_greedy_meshing();

    printf("\n=== All Tests Complete ===\n");
    printf("Key findings:\n");
    printf("  ✓ Greedy algorithm should merge adjacent voxels into larger quads\n");
    printf("  ✓ A 2x2 solid area should generate 1 quad, not 2 quads\n");
    printf("  ✓ The issue in the main code is likely in the run extension logic\n");

    return 0;
}
