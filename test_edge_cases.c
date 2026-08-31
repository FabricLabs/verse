#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

// Simplified voxel types
#define VOXEL_AIR 0
#define VOXEL_STONE 9
#define VOXEL_DIRT 1
#define VOXEL_GRASS 2

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

// Fixed greedy merge algorithm (from our working test)
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
        }
    }

    free(processed);
    free(mask);
}

// Test 1: Single isolated voxel
static void test_single_voxel() {
    printf("\n=== Test 1: Single Isolated Voxel ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Place single voxel at center
    int x = 8, y = 8, z = 8;
    size_t idx = ((size_t)z * test_world.height + y) * test_world.width + x;
    test_world.voxels[idx] = VOXEL_STONE;

    printf("Single voxel at (%d,%d,%d)\n", x, y, z);

    // Test all faces
    for (int face = 0; face < 6; face++) {
        int slice;
        int W, H;

        if (face == 0 || face == 1) { // Z faces
            slice = z + (face == 0 ? 1 : -1);
            W = test_world.width;
            H = test_world.height;
        } else if (face == 2 || face == 4) { // Y faces
            slice = y + (face == 2 ? 1 : -1);
            W = test_world.width;
            H = test_world.depth;
        } else { // X faces
            slice = x + (face == 3 ? 1 : -1);
            W = test_world.height;
            H = test_world.depth;
        }

        if (slice >= 0 && slice < (face < 2 ? test_world.depth : face < 4 ? test_world.height : test_world.width)) {
            VoxelFaceQuad quads[10];
            int quad_count = 0;
            fixed_greedy_merge(&test_world, face, slice, W, H, quads, &quad_count);
            printf("  Face %d (slice %d): %d quads\n", face, slice, quad_count);

            for (int i = 0; i < quad_count; i++) {
                printf("    Quad %d: (%d,%d,%d) to (%d,%d,%d)\n",
                       i, quads[i].x0, quads[i].y0, quads[i].z0,
                       quads[i].x1, quads[i].y1, quads[i].z1);
            }
        }
    }

    free(test_world.voxels);
}

// Test 2: 2x1x1 line (should generate 4 faces)
static void test_line_2x1x1() {
    printf("\n=== Test 2: 2x1x1 Line ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Place 2 connected voxels
    int base_x = 8, base_y = 8, base_z = 8;
    for (int x = 0; x < 2; x++) {
        size_t idx = ((size_t)base_z * test_world.height + base_y) * test_world.width + (base_x + x);
        test_world.voxels[idx] = VOXEL_STONE;
    }

    printf("2x1x1 line at (%d,%d,%d) to (%d,%d,%d)\n",
           base_x, base_y, base_z, base_x + 1, base_y, base_z);

    // Test all faces
    for (int face = 0; face < 6; face++) {
        int slice;
        int W, H;

        if (face == 0 || face == 1) { // Z faces
            slice = base_z + (face == 0 ? 1 : -1);
            W = test_world.width;
            H = test_world.height;
        } else if (face == 2 || face == 4) { // Y faces
            slice = base_y + (face == 2 ? 1 : -1);
            W = test_world.width;
            H = test_world.depth;
        } else { // X faces
            slice = base_x + (face == 3 ? 1 : -1);
            W = test_world.height;
            H = test_world.depth;
        }

        if (slice >= 0 && slice < (face < 2 ? test_world.depth : face < 4 ? test_world.height : test_world.width)) {
            VoxelFaceQuad quads[10];
            int quad_count = 0;
            fixed_greedy_merge(&test_world, face, slice, W, H, quads, &quad_count);
            printf("  Face %d (slice %d): %d quads\n", face, slice, quad_count);

            for (int i = 0; i < quad_count; i++) {
                printf("    Quad %d: (%d,%d,%d) to (%d,%d,%d)\n",
                       i, quads[i].x0, quads[i].y0, quads[i].z0,
                       quads[i].x1, quads[i].y1, quads[i].z1);
            }
        }
    }

    free(test_world.voxels);
}

// Test 3: L-shaped structure (tests non-rectangular merging)
static void test_l_shape() {
    printf("\n=== Test 3: L-Shaped Structure ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create L-shape: vertical line + horizontal line
    int base_x = 8, base_y = 8, base_z = 8;

    // Vertical line
    for (int z = 0; z < 3; z++) {
        size_t idx = ((size_t)(base_z + z) * test_world.height + base_y) * test_world.width + base_x;
        test_world.voxels[idx] = VOXEL_STONE;
    }

    // Horizontal line
    for (int x = 1; x < 3; x++) {
        size_t idx = ((size_t)base_z * test_world.height + base_y) * test_world.width + (base_x + x);
        test_world.voxels[idx] = VOXEL_STONE;
    }

    printf("L-shape at (%d,%d,%d) with 5 voxels\n", base_x, base_y, base_z);

    // Test all faces
    for (int face = 0; face < 6; face++) {
        int slice;
        int W, H;

        if (face == 0 || face == 1) { // Z faces
            slice = base_z + (face == 0 ? 2 : -1);
            W = test_world.width;
            H = test_world.height;
        } else if (face == 2 || face == 4) { // Y faces
            slice = base_y + (face == 2 ? 1 : -1);
            W = test_world.width;
            H = test_world.depth;
        } else { // X faces
            slice = base_x + (face == 3 ? 2 : -1);
            W = test_world.height;
            H = test_world.depth;
        }

        if (slice >= 0 && slice < (face < 2 ? test_world.depth : face < 4 ? test_world.height : test_world.width)) {
            VoxelFaceQuad quads[10];
            int quad_count = 0;
            fixed_greedy_merge(&test_world, face, slice, W, H, quads, &quad_count);
            printf("  Face %d (slice %d): %d quads\n", face, slice, quad_count);

            for (int i = 0; i < quad_count; i++) {
                printf("    Quad %d: (%d,%d,%d) to (%d,%d,%d)\n",
                       i, quads[i].x0, quads[i].y0, quads[i].z0,
                       quads[i].x1, quads[i].y1, quads[i].z1);
            }
        }
    }

    free(test_world.voxels);
}

// Test 4: Hollow cube (tests internal faces)
static void test_hollow_cube() {
    printf("\n=== Test 4: Hollow Cube ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create hollow 3x3x3 cube
    int base_x = 8, base_y = 8, base_z = 8;

    for (int z = 0; z < 3; z++) {
        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 3; x++) {
                // Only place voxels on the surface (not inside)
                if (x == 0 || x == 2 || y == 0 || y == 2 || z == 0 || z == 2) {
                    size_t idx = ((size_t)(base_z + z) * test_world.height + (base_y + y)) * test_world.width + (base_x + x);
                    test_world.voxels[idx] = VOXEL_STONE;
                }
            }
        }
    }

    printf("Hollow 3x3x3 cube at (%d,%d,%d) with 26 surface voxels\n", base_x, base_y, base_z);

    // Test all faces
    for (int face = 0; face < 6; face++) {
        int slice;
        int W, H;

        if (face == 0 || face == 1) { // Z faces
            slice = base_z + (face == 0 ? 2 : -1);
            W = test_world.width;
            H = test_world.height;
        } else if (face == 2 || face == 4) { // Y faces
            slice = base_y + (face == 2 ? 2 : -1);
            W = test_world.width;
            H = test_world.depth;
        } else { // X faces
            slice = base_x + (face == 3 ? 2 : -1);
            W = test_world.height;
            H = test_world.depth;
        }

        if (slice >= 0 && slice < (face < 2 ? test_world.depth : face < 4 ? test_world.height : test_world.width)) {
            VoxelFaceQuad quads[10];
            int quad_count = 0;
            fixed_greedy_merge(&test_world, face, slice, W, H, quads, &quad_count);
            printf("  Face %d (slice %d): %d quads\n", face, slice, quad_count);

            for (int i = 0; i < quad_count; i++) {
                printf("    Quad %d: (%d,%d,%d) to (%d,%d,%d)\n",
                       i, quads[i].x0, quads[i].y0, quads[i].z0,
                       quads[i].x1, quads[i].y1, quads[i].z1);
            }
        }
    }

    free(test_world.voxels);
}

// Test 5: Checkerboard pattern (tests alternating visibility)
static void test_checkerboard() {
    printf("\n=== Test 5: Checkerboard Pattern ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create 4x4 checkerboard at z=8
    int base_x = 6, base_y = 6, base_z = 8;

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if ((x + y) % 2 == 0) { // Checkerboard pattern
                size_t idx = ((size_t)base_z * test_world.height + (base_y + y)) * test_world.width + (base_x + x);
                test_world.voxels[idx] = VOXEL_STONE;
            }
        }
    }

    printf("4x4 checkerboard at z=%d with 8 voxels\n", base_z);

    // Test Z faces
    for (int face = 0; face < 2; face++) {
        int slice = base_z + (face == 0 ? 1 : -1);
        int W = test_world.width;
        int H = test_world.height;

        VoxelFaceQuad quads[20];
        int quad_count = 0;
        fixed_greedy_merge(&test_world, face, slice, W, H, quads, &quad_count);
        printf("  Face %d (slice %d): %d quads\n", face, slice, quad_count);

        for (int i = 0; i < quad_count; i++) {
            printf("    Quad %d: (%d,%d,%d) to (%d,%d,%d)\n",
                   i, quads[i].x0, quads[i].y0, quads[i].z0,
                   quads[i].x1, quads[i].y1, quads[i].z1);
        }
    }

    free(test_world.voxels);
}

// Test 6: Validate coordinate mapping assumptions
static void test_coordinate_mapping() {
    printf("\n=== Test 6: Coordinate Mapping Validation ===\n");

    // Test the coordinate mapping logic for each face direction
    int test_slice = 10;
    int test_x = 5, test_y = 7;

    printf("Testing coordinate mapping for slice=%d, mask(%d,%d):\n", test_slice, test_x, test_y);

    for (int face = 0; face < 6; face++) {
        int world_x, world_y, world_z;

        if (face == 0 || face == 1) { // z fixed (top/bottom faces) - mask over X-Y plane
            world_x = test_x;
            world_y = test_y;
            world_z = test_slice;
            printf("  Face %d (Z-fixed): mask(%d,%d) -> world(%d,%d,%d)\n",
                   face, test_x, test_y, world_x, world_y, world_z);
        } else if (face == 2 || face == 4) { // y fixed (front/back faces) - mask over X-Z plane
            world_x = test_x;
            world_y = test_slice;
            world_z = test_y; // y in mask is actually z coordinate
            printf("  Face %d (Y-fixed): mask(%d,%d) -> world(%d,%d,%d)\n",
                   face, test_x, test_y, world_x, world_y, world_z);
        } else { // x fixed (left/right faces) - mask over Y-Z plane
            world_x = test_slice;
            world_y = test_x; // x in mask is actually y coordinate
            world_z = test_y; // y in mask is actually z coordinate
            printf("  Face %d (X-fixed): mask(%d,%d) -> world(%d,%d,%d)\n",
                   face, test_x, test_y, world_x, world_y, world_z);
        }
    }
}

int main() {
    printf("=== Edge Case and Assumption Tests ===\n");

    test_single_voxel();
    test_line_2x1x1();
    test_l_shape();
    test_hollow_cube();
    test_checkerboard();
    test_coordinate_mapping();

    printf("\n=== All Tests Complete ===\n");
    return 0;
}
