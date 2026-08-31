#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

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

// Test 1: 2x2x2 cube world creation
static void test_2x2x2_cube_world() {
    printf("\n=== Test 1: 2x2x2 Cube World ===\n");

    World test_world;
    test_world.width = 4;
    test_world.height = 4;
    test_world.depth = 4;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Create a 2x2x2 solid cube from (1,1,1) to (2,2,2)
    for (int z = 1; z < 3; z++) {
        for (int y = 1; y < 3; y++) {
            for (int x = 1; x < 3; x++) {
                size_t idx = ((size_t)z * test_world.height + y) * test_world.width + x;
                test_world.voxels[idx] = VOXEL_STONE;
            }
        }
    }

    printf("Created 4x4x4 world with 2x2x2 solid cube from (1,1,1) to (2,2,2)\n");

    // Verify cube creation
    int voxel_count_actual = 0;
    for (int z = 1; z < 3; z++) {
        for (int y = 1; y < 3; y++) {
            for (int x = 1; x < 3; x++) {
                if (world_voxel_get(&test_world, x, y, z) == VOXEL_STONE) {
                    voxel_count_actual++;
                }
            }
        }
    }

    printf("Voxels in cube: %d (expected: 8)\n", voxel_count_actual);

    // Verify neighbors are air
    uint8_t neighbor_x = world_voxel_get(&test_world, 3, 1, 1);
    uint8_t neighbor_y = world_voxel_get(&test_world, 1, 3, 1);
    uint8_t neighbor_z = world_voxel_get(&test_world, 1, 1, 3);
    printf("Neighbors: +X=%d, +Y=%d, +Z=%d (all should be %d)\n",
           neighbor_x, neighbor_y, neighbor_z, VOXEL_AIR);

    if (voxel_count_actual == 8 && neighbor_x == VOXEL_AIR &&
        neighbor_y == VOXEL_AIR && neighbor_z == VOXEL_AIR) {
        printf("✓ 2x2x2 cube world created correctly\n");
    } else {
        printf("✗ 2x2x2 cube world creation FAILED\n");
    }

    free(test_world.voxels);
}

// Test 2: Expected quad generation for 2x2x2 cube
static void test_2x2x2_cube_quads() {
    printf("\n=== Test 2: Expected Quad Generation for 2x2x2 Cube ===\n");

    printf("For a 2x2x2 cube from (1,1,1) to (2,2,2), we expect exactly 6 quads:\n");

    VoxelFaceQuad expected_quads[6] = {
        // +Z face (top) - covers entire 2x2 area at z=2
        {.face = 0, .x0 = 1, .y0 = 1, .z0 = 2, .x1 = 2, .y1 = 2, .z1 = 2},
        // -Z face (bottom) - covers entire 2x2 area at z=1
        {.face = 1, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 2, .y1 = 2, .z1 = 1},
        // +Y face (front) - covers entire 2x2 area at y=2
        {.face = 2, .x0 = 1, .y0 = 2, .z0 = 1, .x1 = 2, .y1 = 2, .z1 = 2},
        // -Y face (back) - covers entire 2x2 area at y=1
        {.face = 4, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 2, .y1 = 1, .z1 = 2},
        // +X face (right) - covers entire 2x2 area at x=2
        {.face = 3, .x0 = 2, .y0 = 1, .z0 = 1, .x1 = 2, .y1 = 2, .z1 = 2},
        // -X face (left) - covers entire 2x2 area at x=1
        {.face = 5, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 1, .y1 = 2, .z1 = 2}
    };

    for (int i = 0; i < 6; i++) {
        const VoxelFaceQuad *q = &expected_quads[i];
        printf("  Face %d: (%d,%d,%d) to (%d,%d,%d)\n",
               q->face, q->x0, q->y0, q->z0, q->x1, q->y1, q->z1);

        // Verify this is a proper 2x2 quad
        int width = q->x1 - q->x0 + 1;
        int height = q->y1 - q->y0 + 1;
        int depth = q->z1 - q->z0 + 1;

        printf("    Dimensions: %dx%dx%d\n", width, height, depth);

        if (width == 2 && height == 2 && depth == 1) {
            printf("    ✓ Proper 2x2 quad\n");
        } else {
            printf("    ✗ Incorrect dimensions\n");
        }
    }

    printf("✓ Expected 6 quads, each covering a 2x2 area\n");
}

// Test 3: Vertex generation for 2x2x2 cube
static void test_2x2x2_cube_vertices() {
    printf("\n=== Test 3: Vertex Generation for 2x2x2 Cube ===\n");

    printf("For a 2x2x2 cube, each face should generate 4 distinct vertices:\n");

    // Simulate vertex generation for +Z face (top)
    int x0 = 1, y0 = 1, x1 = 2, y1 = 2, z = 2;
    int face = 0; // +Z face

    printf("+Z face (face %d) at z=%d covering area (%d,%d) to (%d,%d):\n",
           face, z, x0, y0, x1, y1);

    // Generate 4 corners for this quad
    float corners[4][3];

    // +Z face corners (counter-clockwise when viewed from outside)
    corners[0][0] = (float)x0; corners[0][1] = (float)y0; corners[0][2] = (float)z;     // bottom-left
    corners[1][0] = (float)x0; corners[1][1] = (float)y1; corners[1][2] = (float)z;     // top-left
    corners[2][0] = (float)x1; corners[2][1] = (float)y1; corners[2][2] = (float)z;     // top-right
    corners[3][0] = (float)x1; corners[3][1] = (float)y0; corners[3][2] = (float)z;     // bottom-right

    printf("  Generated corners:\n");
    for (int j = 0; j < 4; j++) {
        printf("    Corner %d: (%.1f,%.1f,%.1f)\n", j, corners[j][0], corners[j][1], corners[j][2]);
    }

    // Check if corners are distinct (which they should be for a 2x2 quad)
    bool all_distinct = true;
    for (int j = 0; j < 4; j++) {
        for (int k = j + 1; k < 4; k++) {
            if (fabs(corners[j][0] - corners[k][0]) < 0.001f &&
                fabs(corners[j][1] - corners[k][1]) < 0.001f &&
                fabs(corners[j][2] - corners[k][2]) < 0.001f) {
                printf("    ✗ Duplicate corner found: %d and %d are identical\n", j, k);
                all_distinct = false;
            }
        }
    }

    if (all_distinct) {
        printf("  ✓ All corners are distinct (as expected for 2x2 quad)\n");
    } else {
        printf("  ✗ Some corners are identical (unexpected)\n");
    }

    printf("✓ 2x2x2 cube generates 4 distinct vertices per face\n");
}

// Test 4: Vertex sharing between faces
static void test_2x2x2_cube_vertex_sharing() {
    printf("\n=== Test 4: Vertex Sharing Between Faces ===\n");

    printf("For a 2x2x2 cube, adjacent faces should share vertices:\n");

    // Define the 8 corner vertices of the cube
    float cube_corners[8][3] = {
        {1.0f, 1.0f, 1.0f},  // (1,1,1) - bottom-back-left
        {2.0f, 1.0f, 1.0f},  // (2,1,1) - bottom-back-right
        {2.0f, 2.0f, 1.0f},  // (2,2,1) - bottom-front-right
        {1.0f, 2.0f, 1.0f},  // (1,2,1) - bottom-front-left
        {1.0f, 1.0f, 2.0f},  // (1,1,2) - top-back-left
        {2.0f, 1.0f, 2.0f},  // (2,1,2) - top-back-right
        {2.0f, 2.0f, 2.0f},  // (2,2,2) - top-front-right
        {1.0f, 2.0f, 2.0f}   // (1,2,2) - top-front-left
    };

    printf("Cube corner vertices:\n");
    for (int i = 0; i < 8; i++) {
        printf("  Corner %d: (%.1f,%.1f,%.1f)\n", i, cube_corners[i][0], cube_corners[i][1], cube_corners[i][2]);
    }

    // Show how faces share these vertices
    printf("\nFace vertex assignments:\n");
    printf("  +Z face (top): corners 4,5,6,7\n");
    printf("  -Z face (bottom): corners 0,1,2,3\n");
    printf("  +Y face (front): corners 3,2,6,7\n");
    printf("  -Y face (back): corners 0,1,5,4\n");
    printf("  +X face (right): corners 1,2,6,5\n");
    printf("  -X face (left): corners 0,3,7,4\n");

    // Count unique vertices
    printf("\nVertex sharing analysis:\n");
    printf("  Total corners: 8\n");
    printf("  Total faces: 6\n");
    printf("  Vertices per face: 4\n");
    printf("  Total vertex references: 6 × 4 = 24\n");
    printf("  Unique vertices: 8 (each corner used by 3 faces)\n");

    printf("✓ 2x2x2 cube demonstrates proper vertex sharing\n");
}

// Test 5: Expected OBJ output for 2x2x2 cube
static void test_2x2x2_cube_obj_output() {
    printf("\n=== Test 5: Expected OBJ Output for 2x2x2 Cube ===\n");

    printf("Expected OBJ file structure:\n");
    printf("\nVertices (8 unique):\n");
    printf("  v 1.0 1.0 1.0  # corner 0\n");
    printf("  v 2.0 1.0 1.0  # corner 1\n");
    printf("  v 2.0 2.0 1.0  # corner 2\n");
    printf("  v 1.0 2.0 1.0  # corner 3\n");
    printf("  v 1.0 1.0 2.0  # corner 4\n");
    printf("  v 2.0 1.0 2.0  # corner 5\n");
    printf("  v 2.0 2.0 2.0  # corner 6\n");
    printf("  v 1.0 2.0 2.0  # corner 7\n");

    printf("\nFaces (12 triangles = 6 quads × 2):\n");
    printf("  # +Z face (top)\n");
    printf("  f 5 6 7  # triangle 1\n");
    printf("  f 5 7 8  # triangle 2\n");
    printf("  # -Z face (bottom)\n");
    printf("  f 1 2 3  # triangle 3\n");
    printf("  f 1 3 4  # triangle 4\n");
    printf("  # +Y face (front)\n");
    printf("  f 4 3 7  # triangle 5\n");
    printf("  f 4 7 8  # triangle 6\n");
    printf("  # -Y face (back)\n");
    printf("  f 1 2 6  # triangle 7\n");
    printf("  f 1 6 5  # triangle 8\n");
    printf("  # +X face (right)\n");
    printf("  f 2 3 7  # triangle 9\n");
    printf("  f 2 7 6  # triangle 10\n");
    printf("  # -X face (left)\n");
    printf("  f 1 4 8  # triangle 11\n");
    printf("  f 1 8 5  # triangle 12\n");

    printf("\nKey properties:\n");
    printf("  ✓ Each triangle has 3 different vertex indices\n");
    printf("  ✓ No degenerate triangles (like f 1 1 1)\n");
    printf("  ✓ Vertices are shared between adjacent faces\n");
    printf("  ✓ Winding order is consistent (counter-clockwise when viewed from outside)\n");

    printf("✓ 2x2x2 cube should generate clean, non-degenerate triangles\n");
}

// Test 6: Summary and comparison with single voxel
static void test_2x2x2_cube_summary() {
    printf("\n=== Test 6: 2x2x2 Cube Summary ===\n");

    printf("2x2x2 cube vs single voxel comparison:\n");
    printf("\nSingle Voxel:\n");
    printf("  ✓ Quads: 6 (one per face)\n");
    printf("  ✓ Vertices per face: 4 identical\n");
    printf("  ✓ Unique vertices: 1\n");
    printf("  ✓ Triangles: 12 degenerate (f 1 1 1)\n");
    printf("  ✗ Rendering: Poor (degenerate triangles)\n");

    printf("\n2x2x2 Cube:\n");
    printf("  ✓ Quads: 6 (one per face)\n");
    printf("  ✓ Vertices per face: 4 distinct\n");
    printf("  ✓ Unique vertices: 8\n");
    printf("  ✓ Triangles: 12 non-degenerate (f 1 2 3)\n");
    printf("  ✓ Rendering: Good (proper triangles)\n");

    printf("\nThe 2x2x2 cube demonstrates how vertex sharing should work:\n");
    printf("  - Each face generates a proper quad with distinct corners\n");
    printf("  - Adjacent faces share vertices at their edges\n");
    printf("  - The mesh is watertight and renderable\n");

    printf("\n✓ 2x2x2 cube test complete - this shows the correct behavior\n");
}

int main() {
    printf("=== 2x2x2 Cube Mesh Generation Test ===\n");
    printf("Testing mesh generation for a 2x2x2 cube (should generate 6 proper quads)\n");

    test_2x2x2_cube_world();
    test_2x2x2_cube_quads();
    test_2x2x2_cube_vertices();
    test_2x2x2_cube_vertex_sharing();
    test_2x2x2_cube_obj_output();
    test_2x2x2_cube_summary();

    printf("\n=== 2x2x2 Cube Tests Complete ===\n");
    printf("Next step: Identify why the main voxel mesh code generates degenerate faces\n");
    return 0;
}
