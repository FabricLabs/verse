#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Simulate the mask building and greedy algorithm for face 2 (+Y face)
int main() {
    printf("Testing mask building and greedy algorithm for 2x2x2 cube\n");

    // For a 2x2x2 cube at (15,15,15) to (16,16,16)
    // Face 2 (+Y) should see the front face at y=16
    // The mask should be 2x2 with all values = 1

    int W = 32, H = 32; // World dimensions
    int slice = 16; // Y slice where the +Y face is visible

    // Create a simple 2x2 visibility mask
    uint8_t *mask = malloc(W * H);
    memset(mask, 0, W * H);

    // Set the 2x2 area as visible (this is what the mask building should produce)
    mask[15 * W + 15] = 1; // (15,15) in mask
    mask[15 * W + 16] = 1; // (15,16) in mask
    mask[16 * W + 15] = 1; // (16,15) in mask
    mask[16 * W + 16] = 1; // (16,16) in mask

    printf("Mask created for 2x2 area at (15,15) to (16,16)\n");
    printf("Mask values:\n");
    for (int y = 14; y <= 17; y++) {
        for (int x = 14; x <= 17; x++) {
            printf("%d ", mask[y * W + x]);
        }
        printf("\n");
    }

    // Now simulate the greedy algorithm
    printf("\nSimulating greedy algorithm:\n");

    int debug_quad_count = 0;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (!mask[y * W + x]) continue;

            printf("Found visible pixel at mask(%d,%d)\n", x, y);

            // Find the width of this run
            int wlen = 1;
            while (x + wlen < W && mask[y * W + (x + wlen)]) {
                wlen++;
            }
            printf("  Width of run: %d\n", wlen);

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
            printf("  Height of run: %d\n", hlen);

            printf("  Generated quad: x[%d,%d], y[%d,%d]\n",
                   x, x + wlen - 1, y, y + hlen - 1);

            // Mark this entire area as used
            for (int dy = 0; dy < hlen; dy++) {
                for (int dx = 0; dx < wlen; dx++) {
                    mask[(y + dy) * W + (x + dx)] = 0;
                }
            }

            debug_quad_count++;
            printf("  Quad %d complete\n", debug_quad_count);

            // Skip the rest of this run
            x += wlen - 1;
            break;
        }
    }

    printf("\nTotal quads generated: %d\n", debug_quad_count);

    free(mask);
    return 0;
}
