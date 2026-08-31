#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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

// Helper function to get voxel at coordinates
static uint8_t world_voxel_get(const World *w, int x, int y, int z) {
    if (x < 0 || x >= w->width || y < 0 || y >= w->height || z < 0 || z >= w->depth) {
        return VOXEL_AIR;
    }
    size_t idx = ((size_t)z * w->height + y) * w->width + x;
    return w->voxels[idx];
}

// Test visibility detection for a specific pattern
static void test_visibility_pattern(const World *w, int face, int slice, int W, int H, const char *pattern_name) {
    printf("\n=== Testing Visibility for %s (Face %d, Slice %d) ===\n", pattern_name, face, slice);

    // Allocate visibility mask
    uint8_t *mask = malloc(W * H * sizeof(uint8_t));
    memset(mask, 0, W * H * sizeof(uint8_t));

    // Generate visibility mask
    int visible_count = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int world_x, world_y, world_z;

            // Map mask coordinates to world coordinates based on face direction
            if (face == 0 || face == 1) { // z fixed (top/bottom faces) - mask over X-Y plane
                world_x = x;
                world_y = y;
                world_z = slice;
            } else if (face == 2 || face == 4) { // y fixed (front/back faces) - mask over X-Z plane
                world_x = x;
                world_y = slice;
                world_z = y; // y in mask is actually z coordinate
            } else { // x fixed (left/right faces) - mask over Y-Z plane
                world_x = slice;
                world_y = x; // x in mask is actually y coordinate
                world_z = y; // y in mask is actually z coordinate
            }

            const uint8_t voxel_type = world_voxel_get(w, world_x, world_y, world_z);
            uint8_t neighbor_type = VOXEL_AIR;

            // Check neighbor based on face direction
            if (face == 0) {
                neighbor_type = world_voxel_get(w, world_x, world_y, world_z + 1);
            } else if (face == 1) {
                neighbor_type = world_voxel_get(w, world_x, world_y, world_z - 1);
            } else if (face == 2) {
                neighbor_type = world_voxel_get(w, world_x, world_y + 1, world_z);
            } else if (face == 4) {
                neighbor_type = world_voxel_get(w, world_x, world_y - 1, world_z);
            } else if (face == 3) {
                neighbor_type = world_voxel_get(w, world_x + 1, world_y, world_z);
            } else {
                neighbor_type = world_voxel_get(w, world_x - 1, world_y, world_z);
            }

            bool is_visible = (voxel_type != VOXEL_AIR && neighbor_type == VOXEL_AIR);
            mask[y * W + x] = is_visible ? 1 : 0;

            if (is_visible) {
                visible_count++;
                printf("  Visible at mask(%d,%d) -> world(%d,%d,%d): voxel=%d, neighbor=%d\n",
                       x, y, world_x, world_y, world_z, voxel_type, neighbor_type);
            }
        }
    }

    printf("  Total visible voxels: %d\n", visible_count);

    // Print mask for debugging (only if reasonable size)
    if (W <= 32 && H <= 32) {
        printf("  Visibility mask (%dx%d):\n", W, H);
        for (int y = 0; y < H; y++) {
            printf("    ");
            for (int x = 0; x < W; x++) {
                printf("%c", mask[y * W + x] ? '#' : '.');
            }
            printf("\n");
        }
    }

    free(mask);
}

// Test 1: Check if visibility detection works correctly for connected voxels
static void test_connected_voxel_visibility() {
    printf("\n=== Test 1: Connected Voxel Visibility ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create a 3x3x3 solid cube
    int base_x = 6, base_y = 6, base_z = 6;
    for (int z = 0; z < 3; z++) {
        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 3; x++) {
                size_t idx = ((size_t)(base_z + z) * test_world.height + (base_y + y)) * test_world.width + (base_x + x);
                test_world.voxels[idx] = VOXEL_STONE;
            }
        }
    }

    printf("Created 3x3x3 solid cube at (%d,%d,%d) to (%d,%d,%d)\n",
           base_x, base_y, base_z, base_x + 2, base_y + 2, base_z + 2);

    // Test visibility for each face
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
            char pattern_name[64];
            snprintf(pattern_name, sizeof(pattern_name), "3x3x3 cube face %d", face);
            test_visibility_pattern(&test_world, face, slice, W, H, pattern_name);
        }
    }

    free(test_world.voxels);
}

// Test 2: Check if visibility detection fails for certain neighbor patterns
static void test_neighbor_pattern_visibility() {
    printf("\n=== Test 2: Neighbor Pattern Visibility ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create a pattern that might cause visibility issues
    int base_x = 6, base_y = 6, base_z = 6;

    // Place voxels in a way that tests edge cases
    for (int z = 0; z < 4; z++) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                // Create a pattern that alternates between solid and air
                if ((x + y + z) % 2 == 0) {
                    size_t idx = ((size_t)(base_z + z) * test_world.height + (base_y + y)) * test_world.width + (base_x + x);
                    test_world.voxels[idx] = VOXEL_STONE;
                }
            }
        }
    }

    printf("Created alternating pattern at (%d,%d,%d) to (%d,%d,%d)\n",
           base_x, base_y, base_z, base_x + 3, base_y + 3, base_z + 3);

    // Test visibility for Z faces
    for (int face = 0; face < 2; face++) {
        int slice = base_z + (face == 0 ? 3 : -1);
        int W = test_world.width;
        int H = test_world.height;

        if (slice >= 0 && slice < test_world.depth) {
            char pattern_name[64];
            snprintf(pattern_name, sizeof(pattern_name), "alternating pattern face %d", face);
            test_visibility_pattern(&test_world, face, slice, W, H, pattern_name);
        }
    }

    free(test_world.voxels);
}

// Test 3: Check if coordinate mapping is consistent across different world sizes
static void test_coordinate_consistency() {
    printf("\n=== Test 3: Coordinate Mapping Consistency ===\n");

    // Test with different world sizes to see if coordinate mapping breaks
    int world_sizes[] = {16, 32, 64, 128};

    for (int size_idx = 0; size_idx < 4; size_idx++) {
        int world_size = world_sizes[size_idx];
        printf("\nTesting world size %dx%dx%d:\n", world_size, world_size, world_size);

        World test_world;
        test_world.width = world_size;
        test_world.height = world_size;
        test_world.depth = world_size;

        size_t voxel_count = test_world.width * test_world.height * test_world.depth;
        test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
        memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

        // Place a single voxel at the center
        int center = world_size / 2;
        size_t idx = ((size_t)center * test_world.height + center) * test_world.width + center;
        test_world.voxels[idx] = VOXEL_STONE;

        printf("  Single voxel at center (%d,%d,%d)\n", center, center, center);

        // Test coordinate mapping for each face
        for (int face = 0; face < 6; face++) {
            int slice;
            int W, H;

            if (face == 0 || face == 1) { // Z faces
                slice = center + (face == 0 ? 1 : -1);
                W = test_world.width;
                H = test_world.height;
            } else if (face == 2 || face == 4) { // Y faces
                slice = center + (face == 2 ? 1 : -1);
                W = test_world.width;
                H = test_world.depth;
            } else { // X faces
                slice = center + (face == 3 ? 1 : -1);
                W = test_world.height;
                H = test_world.depth;
            }

            if (slice >= 0 && slice < (face < 2 ? test_world.depth : face < 4 ? test_world.height : test_world.width)) {
                // Test a few specific mask coordinates
                for (int test_y = 0; test_y < 3; test_y++) {
                    for (int test_x = 0; test_x < 3; test_x++) {
                        int world_x, world_y, world_z;

                        if (face == 0 || face == 1) {
                            world_x = test_x;
                            world_y = test_y;
                            world_z = slice;
                        } else if (face == 2 || face == 4) {
                            world_x = test_x;
                            world_y = slice;
                            world_z = test_y;
                        } else {
                            world_x = slice;
                            world_y = test_x;
                            world_z = test_y;
                        }

                        const uint8_t voxel_type = world_voxel_get(&test_world, world_x, world_y, world_z);
                        printf("    Face %d: mask(%d,%d) -> world(%d,%d,%d) = %d\n",
                               face, test_x, test_y, world_x, world_y, world_z, voxel_type);
                    }
                }
            }
        }

        free(test_world.voxels);
    }
}

// Test 4: Check if visibility detection works for different voxel types
static void test_voxel_type_visibility() {
    printf("\n=== Test 4: Voxel Type Visibility ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create a pattern with different voxel types
    int base_x = 6, base_y = 6, base_z = 6;

    // Place different voxel types
    for (int z = 0; z < 3; z++) {
        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 3; x++) {
                size_t idx = ((size_t)(base_z + z) * test_world.height + (base_y + y)) * test_world.width + (base_x + x);

                // Use different voxel types
                if (z == 0) test_world.voxels[idx] = VOXEL_DIRT;
                else if (z == 1) test_world.voxels[idx] = VOXEL_GRASS;
                else test_world.voxels[idx] = VOXEL_STONE;
            }
        }
    }

    printf("Created 3x3x3 cube with different voxel types at (%d,%d,%d)\n", base_x, base_y, base_z);

    // Test visibility for Z faces
    for (int face = 0; face < 2; face++) {
        int slice = base_z + (face == 0 ? 2 : -1);
        int W = test_world.width;
        int H = test_world.height;

        if (slice >= 0 && slice < test_world.depth) {
            char pattern_name[64];
            snprintf(pattern_name, sizeof(pattern_name), "mixed voxel types face %d", face);
            test_visibility_pattern(&test_world, face, slice, W, H, pattern_name);
        }
    }

    free(test_world.voxels);
}

int main() {
    printf("=== Visibility Logic Tests ===\n");

    test_connected_voxel_visibility();
    test_neighbor_pattern_visibility();
    test_coordinate_consistency();
    test_voxel_type_visibility();

    printf("\n=== All Visibility Tests Complete ===\n");
    return 0;
}
