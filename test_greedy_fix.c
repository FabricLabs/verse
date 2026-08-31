#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// Simplified voxel types
#define VOXEL_AIR 0
#define VOXEL_STONE 9

// Simplified World structure
typedef struct {
    uint32_t width, height, depth;
    uint8_t *voxels; // Linear array of voxel types
} World;

// Simplified VoxelFaceQuad structure
typedef struct {
    int face;
    int x0, y0, z0, x1, y1, z1;
} VoxelFaceQuad;

// Helper function to get voxel at coordinates
static uint8_t world_voxel_get(const World *w, int x, int y, int z) {
    if (x < 0 || x >= w->width || y < 0 || y >= w->height || z < 0 || z >= w->depth) {
        return VOXEL_AIR;
    }
    size_t idx = ((size_t)z * w->height + y) * w->width + x;
    return w->voxels[idx];
}

// Fixed greedy merge algorithm
static void fixed_greedy_merge(const World *w, int face, int slice, int W, int H, VoxelFaceQuad *quads, int *quad_count) {
    // Allocate visibility mask
    uint8_t *mask = malloc(W * H * sizeof(uint8_t));
    memset(mask, 0, W * H * sizeof(uint8_t));

    // Generate visibility mask first
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int world_x, world_y, world_z;

            if (face == 0 || face == 1) {
                world_x = x;
                world_y = y;
                world_z = slice;
            } else if (face == 2 || face == 4) {
                world_x = x;
                world_y = slice;
                world_z = y;
            } else {
                world_x = slice;
                world_y = x;
                world_z = y;
            }

            const uint8_t voxel_type = world_voxel_get(w, world_x, world_y, world_z);
            uint8_t neighbor_type = VOXEL_AIR;

            if (face == 0) neighbor_type = world_voxel_get(w, world_x, world_y, world_z + 1);
            else if (face == 1) neighbor_type = world_voxel_get(w, world_x, world_y, world_z - 1);
            else if (face == 2) neighbor_type = world_voxel_get(w, world_x, world_y + 1, world_z);
            else if (face == 4) neighbor_type = world_voxel_get(w, world_x, world_y - 1, world_z);
            else if (face == 3) neighbor_type = world_voxel_get(w, world_x + 1, world_y, world_z);
            else neighbor_type = world_voxel_get(w, world_x - 1, world_y, world_z);

            mask[y * W + x] = (voxel_type != VOXEL_AIR && neighbor_type == VOXEL_AIR) ? 1 : 0;
        }
    }

    // Print mask for debugging
    printf("  Visibility mask (%dx%d):\n", W, H);
    for (int y = 0; y < H; y++) {
        printf("    ");
        for (int x = 0; x < W; x++) {
            printf("%c", mask[y * W + x] ? '#' : '.');
        }
        printf("\n");
    }

    // Fixed greedy merge algorithm
    *quad_count = 0;
    bool *processed = malloc(W * H * sizeof(bool));
    memset(processed, 0, W * H * sizeof(bool));

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (!mask[y * W + x] || processed[y * W + x]) continue;

            // Find the width of this run
            int wlen = 1;
            while (x + wlen < W && mask[y * W + (x + wlen)]) {
                wlen++;
            }

            // Find the maximum height by checking if the entire row below has the same pattern
            int hlen = 1;
            bool can_extend = true;

            while (can_extend && y + hlen < H) {
                // Check if the entire row below has the same width pattern
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

            // Mark all voxels in this quad as processed
            for (int dy = 0; dy < hlen; dy++) {
                for (int dx = 0; dx < wlen; dx++) {
                    processed[(y + dy) * W + (x + dx)] = true;
                }
            }

            // Create quad
            VoxelFaceQuad q;
            q.face = face;

            // Map mask coordinates to world coordinates
            if (face == 0 || face == 1) { // z fixed
                q.x0 = x;
                q.y0 = y;
                q.z0 = slice;
                q.x1 = x + wlen - 1;
                q.y1 = y + hlen - 1;
                q.z1 = slice;
            } else if (face == 2 || face == 4) { // y fixed
                q.x0 = x;
                q.y0 = slice;
                q.z0 = y;
                q.x1 = x + wlen - 1;
                q.y1 = slice;
                q.z1 = y + hlen - 1;
            } else { // x fixed
                q.x0 = slice;
                q.y0 = x;
                q.z0 = y;
                q.x1 = slice;
                q.y1 = x + wlen - 1;
                q.z1 = y + hlen - 1;
            }

            quads[*quad_count] = q;
            (*quad_count)++;

            printf("  Quad %d: (%d,%d,%d) to (%d,%d,%d) [w=%d, h=%d]\n",
                   *quad_count - 1, q.x0, q.y0, q.z0, q.x1, q.y1, q.z1, wlen, hlen);

            // Check if quad is degenerate
            if (q.x0 == q.x1 || q.y0 == q.y1 || q.z0 == q.z1) {
                printf("    WARNING: Degenerate quad detected!\n");
            }
        }
    }

    free(processed);
    free(mask);
}

// Test the fixed greedy merge algorithm
static void test_fixed_greedy_merge(const World *w, int face, int slice, int W, int H) {
    printf("\n=== Testing FIXED Greedy Merge for Face %d (slice %d) ===\n", face, slice);

    VoxelFaceQuad quads[100]; // Max 100 quads
    int quad_count = 0;

    fixed_greedy_merge(w, face, slice, W, H, quads, &quad_count);

    printf("  Total quads generated: %d\n", quad_count);

    // Analyze the quads
    int total_area = 0;
    for (int i = 0; i < quad_count; i++) {
        VoxelFaceQuad *q = &quads[i];
        int area = (q->x1 - q->x0 + 1) * (q->y1 - q->y0 + 1) * (q->z1 - q->z0 + 1);
        total_area += area;
        printf("    Quad %d area: %d\n", i, area);
    }
    printf("  Total area covered: %d\n", total_area);
}

int main() {
    printf("=== Fixed Greedy Merge Test ===\n");

    // Create a simple 2x2x2 test world
    World test_world;
    test_world.width = 32;
    test_world.height = 32;
    test_world.depth = 32;

    // Allocate and initialize voxel array
    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create a 2x2x2 cube at (15,15,15) to (16,16,16)
    int base_x = 15, base_y = 15, base_z = 15;
    for (int z = 0; z < 2; z++) {
        for (int y = 0; y < 2; y++) {
            for (int x = 0; x < 2; x++) {
                size_t idx = ((size_t)(base_z + z) * test_world.height + (base_y + y)) * test_world.width + (base_x + x);
                test_world.voxels[idx] = VOXEL_STONE;
            }
        }
    }

    printf("Created 2x2x2 test world at (%d,%d,%d) to (%d,%d,%d)\n",
           base_x, base_y, base_z, base_x + 1, base_y + 1, base_z + 1);

    // Test the fixed greedy merge for each face
    for (int face = 0; face < 6; face++) {
        int slice;
        int W, H;

        if (face == 0 || face == 1) { // Z faces
            slice = base_z + (face == 0 ? 1 : 0); // +Z at z=16, -Z at z=15
            W = test_world.width;
            H = test_world.height;
        } else if (face == 2 || face == 4) { // Y faces
            slice = base_y + (face == 2 ? 1 : 0); // +Y at y=16, -Y at y=15
            W = test_world.width;
            H = test_world.depth;
        } else { // X faces
            slice = base_x + (face == 3 ? 1 : 0); // +X at x=16, -X at x=15
            W = test_world.height;
            H = test_world.depth;
        }

        test_fixed_greedy_merge(&test_world, face, slice, W, H);
    }

    // Cleanup
    free(test_world.voxels);

    printf("\n=== Test Complete ===\n");
    return 0;
}
