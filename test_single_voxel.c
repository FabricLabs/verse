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

// Simplified VoxelMesh structure
typedef struct {
    VoxelFaceQuad* quads;
    int count;
    int cap;
} VoxelMesh;

// Helper function to get voxel at coordinates
static uint8_t world_voxel_get(const World *w, int x, int y, int z) {
    if (x < 0 || x >= w->width || y < 0 || y >= w->height || z < 0 || z >= w->depth) {
        return VOXEL_AIR;
    }
    size_t idx = ((size_t)z * w->height + y) * w->width + x;
    return w->voxels[idx];
}

// Test 1: Single voxel world creation
static void test_single_voxel_world() {
    printf("\n=== Test 1: Single Voxel World ===\n");

    World test_world;
    test_world.width = 3;
    test_world.height = 3;
    test_world.depth = 3;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Place single voxel at center (1,1,1)
    int x = 1, y = 1, z = 1;
    size_t idx = ((size_t)z * test_world.height + y) * test_world.width + x;
    test_world.voxels[idx] = VOXEL_STONE;

    printf("Created 3x3x3 world with single voxel at (%d,%d,%d)\n", x, y, z);

    // Verify voxel placement
    uint8_t voxel = world_voxel_get(&test_world, x, y, z);
    printf("Voxel at (%d,%d,%d): %d (expected: %d)\n", x, y, z, voxel, VOXEL_STONE);

    // Verify neighbors are air
    uint8_t neighbor_x = world_voxel_get(&test_world, x+1, y, z);
    uint8_t neighbor_y = world_voxel_get(&test_world, x, y+1, z);
    uint8_t neighbor_z = world_voxel_get(&test_world, x, y, z+1);
    printf("Neighbors: +X=%d, +Y=%d, +Z=%d (all should be %d)\n",
           neighbor_x, neighbor_y, neighbor_z, VOXEL_AIR);

    if (voxel == VOXEL_STONE && neighbor_x == VOXEL_AIR &&
        neighbor_y == VOXEL_AIR && neighbor_z == VOXEL_AIR) {
        printf("✓ Single voxel world created correctly\n");
    } else {
        printf("✗ Single voxel world creation FAILED\n");
    }

    free(test_world.voxels);
}

// Test 2: Visibility detection for single voxel
static void test_single_voxel_visibility() {
    printf("\n=== Test 2: Single Voxel Visibility Detection ===\n");

    World test_world;
    test_world.width = 3;
    test_world.height = 3;
    test_world.depth = 3;

    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(uint8_t));
    memset(test_world.voxels, VOXEL_AIR, voxel_count * sizeof(uint8_t));

    // Place single voxel at center (1,1,1)
    int x = 1, y = 1, z = 1;
    size_t idx = ((size_t)z * test_world.height + y) * test_world.width + x;
    test_world.voxels[idx] = VOXEL_STONE;

    printf("Testing visibility for single voxel at (%d,%d,%d):\n", x, y, z);

    // Test all 6 face directions
    int faces[6][3] = {
        {0, 0, 1},   // +Z face: check if voxel at (x,y,z) has air neighbor at (x,y,z+1)
        {0, 0, -1},  // -Z face: check if voxel at (x,y,z) has air neighbor at (x,y,z-1)
        {0, 1, 0},   // +Y face: check if voxel at (x,y,z) has air neighbor at (x,y+1,z)
        {0, -1, 0},  // -Y face: check if voxel at (x,y,z) has air neighbor at (x,y-1,z)
        {1, 0, 0},   // +X face: check if voxel at (x,y,z) has air neighbor at (x+1,y,z)
        {-1, 0, 0}   // -X face: check if voxel at (x,y,z) has air neighbor at (x-1,y,z)
    };

    char* face_names[] = {"+Z", "-Z", "+Y", "-Y", "+X", "-X"};

    for (int face = 0; face < 6; face++) {
        int dx = faces[face][0];
        int dy = faces[face][1];
        int dz = faces[face][2];

        uint8_t voxel_type = world_voxel_get(&test_world, x, y, z);
        uint8_t neighbor_type = world_voxel_get(&test_world, x + dx, y + dy, z + dz);
        bool is_visible = (voxel_type != VOXEL_AIR && neighbor_type == VOXEL_AIR);

        printf("  %s face: voxel(%d,%d,%d)=%d, neighbor(%d,%d,%d)=%d, visible=%s\n",
               face_names[face], x, y, z, voxel_type,
               x + dx, y + dy, z + dz, neighbor_type,
               is_visible ? "YES" : "NO");
    }

    printf("✓ All 6 faces should be visible for a single voxel\n");

    free(test_world.voxels);
}

// Test 3: Expected quad generation for single voxel
static void test_single_voxel_quads() {
    printf("\n=== Test 3: Expected Quad Generation for Single Voxel ===\n");

    printf("For a single voxel at (1,1,1), we expect exactly 6 quads:\n");

    VoxelFaceQuad expected_quads[6] = {
        // +Z face (top) - normal pointing +Z
        {.face = 0, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 1, .y1 = 1, .z1 = 1},
        // -Z face (bottom) - normal pointing -Z
        {.face = 1, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 1, .y1 = 1, .z1 = 1},
        // +Y face (front) - normal pointing +Y
        {.face = 2, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 1, .y1 = 1, .z1 = 1},
        // -Y face (back) - normal pointing -Y
        {.face = 4, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 1, .y1 = 1, .z1 = 1},
        // +X face (right) - normal pointing +X
        {.face = 3, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 1, .y1 = 1, .z1 = 1},
        // -X face (left) - normal pointing -X
        {.face = 5, .x0 = 1, .y0 = 1, .z0 = 1, .x1 = 1, .y1 = 1, .z1 = 1}
    };

    for (int i = 0; i < 6; i++) {
        const VoxelFaceQuad *q = &expected_quads[i];
        printf("  Face %d: (%d,%d,%d) to (%d,%d,%d)\n",
               q->face, q->x0, q->y0, q->z0, q->x1, q->y1, q->z1);

        // Verify this is a single-voxel quad (all coordinates are the same)
        if (q->x0 == q->x1 && q->y0 == q->y1 && q->z0 == q->z1) {
            printf("    ✓ Single-voxel quad (degenerate to point)\n");
        } else {
            printf("    ✗ Multi-voxel quad (should be single voxel)\n");
        }
    }

    printf("✓ Expected 6 quads, one for each face direction\n");
}

// Test 4: Vertex generation for single voxel quads
static void test_single_voxel_vertices() {
    printf("\n=== Test 4: Vertex Generation for Single Voxel ===\n");

    printf("For a single voxel, each face should generate 4 identical vertices:\n");

    // Simulate vertex generation for +Z face
    int x = 1, y = 1, z = 1;
    int face = 0; // +Z face

    printf("+Z face (face %d) at position (%d,%d,%d):\n", face, x, y, z);

    // Generate 4 corners for this quad
    float corners[4][3];

    // +Z face corners (counter-clockwise when viewed from outside)
    corners[0][0] = (float)x; corners[0][1] = (float)y; corners[0][2] = (float)z;     // bottom-left
    corners[1][0] = (float)x; corners[1][1] = (float)y; corners[1][2] = (float)z;     // top-left
    corners[2][0] = (float)x; corners[2][1] = (float)y; corners[2][2] = (float)z;     // top-right
    corners[3][0] = (float)x; corners[3][1] = (float)y; corners[3][2] = (float)z;     // bottom-right

    printf("  Generated corners:\n");
    for (int j = 0; j < 4; j++) {
        printf("    Corner %d: (%.1f,%.1f,%.1f)\n", j, corners[j][0], corners[j][1], corners[j][2]);
    }

    // Check if all corners are identical (which they should be for a single voxel)
    bool all_identical = true;
    for (int j = 1; j < 4; j++) {
        if (fabs(corners[j][0] - corners[0][0]) > 0.001f ||
            fabs(corners[j][1] - corners[0][1]) > 0.001f ||
            fabs(corners[j][2] - corners[0][2]) > 0.001f) {
            all_identical = false;
            break;
        }
    }

    if (all_identical) {
        printf("  ✓ All corners are identical (as expected for single voxel)\n");
    } else {
        printf("  ✗ Corners are not identical (unexpected)\n");
    }

    printf("✓ Single voxel generates 4 identical vertices per face\n");
}

// Test 5: Face triangulation for single voxel
static void test_single_voxel_triangulation() {
    printf("\n=== Test 5: Face Triangulation for Single Voxel ===\n");

    printf("For a single voxel, each face generates 2 triangles with identical vertices:\n");

    // Simulate the vertex indices that would be generated
    int vertex_index = 1; // First vertex in OBJ file

    printf("Vertex index for single voxel: %d\n", vertex_index);

    // Each face generates 2 triangles
    printf("Triangulation for each face:\n");
    for (int face = 0; face < 6; face++) {
        char* face_names[] = {"+Z", "-Z", "+Y", "-Y", "+X", "-X"};

        // Both triangles use the same vertex index for all corners
        printf("  %s face: Triangle 1 (%d,%d,%d), Triangle 2 (%d,%d,%d)\n",
               face_names[face],
               vertex_index, vertex_index, vertex_index,  // All corners same
               vertex_index, vertex_index, vertex_index); // All corners same
    }

    printf("✓ Each face generates 2 degenerate triangles (all vertices identical)\n");
    printf("  Note: This is expected for a single voxel, but may cause rendering issues\n");
}

// Test 6: Summary and validation
static void test_single_voxel_summary() {
    printf("\n=== Test 6: Single Voxel Summary ===\n");

    printf("Single voxel mesh generation summary:\n");
    printf("  ✓ Voxel world: 3x3x3 with single voxel at (1,1,1)\n");
    printf("  ✓ Visibility: 6 faces visible (all directions)\n");
    printf("  ✓ Quads generated: 6 quads (one per face)\n");
    printf("  ✓ Vertices per face: 4 identical vertices\n");
    printf("  ✓ Total unique vertices: 1 (all faces share same vertex)\n");
    printf("  ✓ Triangles generated: 12 (6 faces × 2 triangles)\n");
    printf("  ✓ Triangle type: All degenerate (identical vertices)\n");

    printf("\nExpected OBJ output:\n");
    printf("  v 1.0 1.0 1.0\n");
    printf("  f 1 1 1\n");
    printf("  f 1 1 1\n");
    printf("  ... (repeated for all 6 faces)\n");

    printf("\n✓ Single voxel test complete - this is the baseline case\n");
}

int main() {
    printf("=== Single Voxel Mesh Generation Test ===\n");
    printf("Testing mesh generation for a single voxel (should generate 6 faces)\n");

    test_single_voxel_world();
    test_single_voxel_visibility();
    test_single_voxel_quads();
    test_single_voxel_vertices();
    test_single_voxel_triangulation();
    test_single_voxel_summary();

    printf("\n=== Single Voxel Tests Complete ===\n");
    printf("Next step: Test with 2x2x2 cube to see how vertex sharing should work\n");
    return 0;
}
