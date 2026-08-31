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

// Helper function to get voxel at coordinates
static uint8_t world_voxel_get(const World *w, int x, int y, int z) {
    if (x < 0 || x >= w->width || y < 0 || y >= w->height || z < 0 || z >= w->depth) {
        return VOXEL_AIR;
    }
    size_t idx = ((size_t)z * w->height + y) * w->width + x;
    return w->voxels[idx];
}

// Test slice selection logic
static void test_slice_selection() {
    printf("=== Testing Slice Selection Logic ===\n");

    // Create a simple test world with a single voxel
    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Place a single voxel at (8,8,8)
    int voxel_x = 8, voxel_y = 8, voxel_z = 8;
    size_t idx = ((size_t)voxel_z * test_world.height + voxel_y) * test_world.width + voxel_x;
    test_world.voxels[idx] = VOXEL_STONE;

    printf("Single voxel at (%d,%d,%d)\n", voxel_x, voxel_y, voxel_z);

    // Test each face direction
    for (int face = 0; face < 6; face++) {
        printf("\n--- Face %d ---\n", face);

        // Determine which slice to check for this face
        int slice;
        int W, H;
        const char *face_name;

        if (face == 0) { // +Z face (top)
            slice = voxel_z + 1;
            W = test_world.width;
            H = test_world.height;
            face_name = "+Z (top)";
        } else if (face == 1) { // -Z face (bottom)
            slice = voxel_z - 1;
            W = test_world.width;
            H = test_world.height;
            face_name = "-Z (bottom)";
        } else if (face == 2) { // +Y face (front)
            slice = voxel_y + 1;
            W = test_world.width;
            H = test_world.depth;
            face_name = "+Y (front)";
        } else if (face == 4) { // -Y face (back)
            slice = voxel_y - 1;
            W = test_world.width;
            H = test_world.depth;
            face_name = "-Y (back)";
        } else if (face == 3) { // +X face (right)
            slice = voxel_x + 1;
            W = test_world.height;
            H = test_world.depth;
            face_name = "+X (right)";
        } else { // -X face (left)
            slice = voxel_x - 1;
            W = test_world.height;
            H = test_world.depth;
            face_name = "-X (left)";
        }

        printf("Face %d (%s): checking slice %d\n", face, face_name, slice);

        // Check if slice is valid
        if (slice < 0 || slice >= (face < 2 ? test_world.depth : face < 4 ? test_world.height : test_world.width)) {
            printf("  ERROR: Slice %d is out of bounds!\n", slice);
            continue;
        }

        // Check what's at the slice position
        printf("  Slice %d is valid\n", slice);

        // Check a few positions around the voxel to see what's visible
        int visible_count = 0;
        for (int test_y = 0; test_y < 3; test_y++) {
            for (int test_x = 0; test_x < 3; test_x++) {
                int world_x, world_y, world_z;

                // Map mask coordinates to world coordinates
                if (face == 0 || face == 1) { // Z faces
                    world_x = test_x + (voxel_x - 1);
                    world_y = test_y + (voxel_y - 1);
                    world_z = slice;
                } else if (face == 2 || face == 4) { // Y faces
                    world_x = test_x + (voxel_x - 1);
                    world_y = slice;
                    world_z = test_y + (voxel_z - 1);
                } else { // X faces
                    world_x = slice;
                    world_y = test_x + (voxel_y - 1);
                    world_z = test_y + (voxel_z - 1);
                }

                const uint8_t voxel_type = world_voxel_get(&test_world, world_x, world_y, world_z);
                uint8_t neighbor_type = VOXEL_AIR;

                // Check neighbor based on face direction
                if (face == 0) {
                    neighbor_type = world_voxel_get(&test_world, world_x, world_y, world_z + 1);
                } else if (face == 1) {
                    neighbor_type = world_voxel_get(&test_world, world_x, world_y, world_z - 1);
                } else if (face == 2) {
                    neighbor_type = world_voxel_get(&test_world, world_x, world_y + 1, world_z);
                } else if (face == 4) {
                    neighbor_type = world_voxel_get(&test_world, world_x, world_y - 1, world_z);
                } else if (face == 3) {
                    neighbor_type = world_voxel_get(&test_world, world_x + 1, world_y, world_z);
                } else {
                    neighbor_type = world_voxel_get(&test_world, world_x - 1, world_y, world_z);
                }

                bool is_visible = (voxel_type != VOXEL_AIR && neighbor_type == VOXEL_AIR);
                if (is_visible) {
                    visible_count++;
                    printf("  Visible at mask(%d,%d) -> world(%d,%d,%d): voxel=%d, neighbor=%d\n",
                           test_x, test_y, world_x, world_y, world_z, voxel_type, neighbor_type);
                }
            }
        }

        printf("  Total visible voxels in 3x3 area around voxel: %d\n", visible_count);
    }

    free(test_world.voxels);
}

// Test with a larger structure to see the full picture
static void test_larger_structure() {
    printf("\n=== Testing Larger Structure ===\n");

    World test_world;
    test_world.width = 16;
    test_world.height = 16;
    test_world.depth = 16;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create a 2x2x2 cube at (7,7,7) to (8,8,8)
    int base_x = 7, base_y = 7, base_z = 7;
    for (int z = 0; z < 2; z++) {
        for (int y = 0; y < 2; y++) {
            for (int x = 0; x < 2; x++) {
                size_t idx = ((size_t)(base_z + z) * test_world.height + (base_y + y)) * test_world.width + (base_x + x);
                test_world.voxels[idx] = VOXEL_STONE;
            }
        }
    }

    printf("2x2x2 cube at (%d,%d,%d) to (%d,%d,%d)\n",
           base_x, base_y, base_z, base_x + 1, base_y + 1, base_z + 1);

    // Test each face
    for (int face = 0; face < 6; face++) {
        printf("\n--- Face %d ---\n", face);

        int slice;
        int W, H;
        const char *face_name;

        if (face == 0) { // +Z face (top)
            slice = base_z + 1;
            W = test_world.width;
            H = test_world.height;
            face_name = "+Z (top)";
        } else if (face == 1) { // -Z face (bottom)
            slice = base_z - 1;
            W = test_world.width;
            H = test_world.height;
            face_name = "-Z (bottom)";
        } else if (face == 2) { // +Y face (front)
            slice = base_y + 1;
            W = test_world.width;
            H = test_world.depth;
            face_name = "+Y (front)";
        } else if (face == 4) { // -Y face (back)
            slice = base_y - 1;
            W = test_world.width;
            H = test_world.depth;
            face_name = "-Y (back)";
        } else if (face == 3) { // +X face (right)
            slice = base_x + 1;
            W = test_world.height;
            H = test_world.depth;
            face_name = "+X (right)";
        } else { // -X face (left)
            slice = base_x - 1;
            W = test_world.height;
            H = test_world.depth;
            face_name = "-X (left)";
        }

        printf("Face %d (%s): checking slice %d\n", face, face_name, slice);

        // Check if slice is valid
        if (slice < 0 || slice >= (face < 2 ? test_world.depth : face < 4 ? test_world.height : test_world.width)) {
            printf("  ERROR: Slice %d is out of bounds!\n", slice);
            continue;
        }

        // Count visible voxels in the entire slice
        int visible_count = 0;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int world_x, world_y, world_z;

                // Map mask coordinates to world coordinates
                if (face == 0 || face == 1) { // Z faces
                    world_x = x;
                    world_y = y;
                    world_z = slice;
                } else if (face == 2 || face == 4) { // Y faces
                    world_x = x;
                    world_y = slice;
                    world_z = y;
                } else { // X faces
                    world_x = slice;
                    world_y = x;
                    world_z = y;
                }

                const uint8_t voxel_type = world_voxel_get(&test_world, world_x, world_y, world_z);
                uint8_t neighbor_type = VOXEL_AIR;

                // Check neighbor based on face direction
                if (face == 0) {
                    neighbor_type = world_voxel_get(&test_world, world_x, world_y, world_z + 1);
                } else if (face == 1) {
                    neighbor_type = world_voxel_get(&test_world, world_x, world_y, world_z - 1);
                } else if (face == 2) {
                    neighbor_type = world_voxel_get(&test_world, world_x, world_y + 1, world_z);
                } else if (face == 4) {
                    neighbor_type = world_voxel_get(&test_world, world_x, world_y - 1, world_z);
                } else if (face == 3) {
                    neighbor_type = world_voxel_get(&test_world, world_x + 1, world_y, world_z);
                } else {
                    neighbor_type = world_voxel_get(&test_world, world_x - 1, world_y, world_z);
                }

                bool is_visible = (voxel_type != VOXEL_AIR && neighbor_type == VOXEL_AIR);
                if (is_visible) {
                    visible_count++;
                }
            }
        }

        printf("  Total visible voxels in slice: %d\n", visible_count);

        // Show the visibility pattern for small slices
        if (W <= 16 && H <= 16) {
            printf("  Visibility pattern:\n");
            for (int y = 0; y < H; y++) {
                printf("    ");
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

                    const uint8_t voxel_type = world_voxel_get(&test_world, world_x, world_y, world_z);
                    uint8_t neighbor_type = VOXEL_AIR;

                    if (face == 0) neighbor_type = world_voxel_get(&test_world, world_x, world_y, world_z + 1);
                    else if (face == 1) neighbor_type = world_voxel_get(&test_world, world_x, world_y, world_z - 1);
                    else if (face == 2) neighbor_type = world_voxel_get(&test_world, world_x, world_y + 1, world_z);
                    else if (face == 4) neighbor_type = world_voxel_get(&test_world, world_x, world_y - 1, world_z);
                    else if (face == 3) neighbor_type = world_voxel_get(&test_world, world_x + 1, world_y, world_z);
                    else neighbor_type = world_voxel_get(&test_world, world_x - 1, world_y, world_z);

                    bool is_visible = (voxel_type != VOXEL_AIR && neighbor_type == VOXEL_AIR);
                    printf("%c", is_visible ? '#' : '.');
                }
                printf("\n");
            }
        }
    }

    free(test_world.voxels);
}

int main() {
    printf("=== Slice Selection Test ===\n");

    test_slice_selection();
    test_larger_structure();

    printf("\n=== Test Complete ===\n");
    return 0;
}
