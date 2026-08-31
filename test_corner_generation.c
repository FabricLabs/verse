#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

// Simplified VoxelFaceQuad structure
typedef struct {
    int face;
    int x0, y0, z0, x1, y1, z1;
} VoxelFaceQuad;

// Test the corner generation logic for each face type
static void test_corner_generation() {
    printf("=== Testing Corner Generation Logic ===\n");

    // Create a test quad representing a single voxel at (1,1,1)
    VoxelFaceQuad test_quad = {
        .face = 0,  // +Z face
        .x0 = 1, .y0 = 1, .z0 = 1,  // bottom corner
        .x1 = 1, .y1 = 1, .z1 = 1   // top corner (same for single voxel)
    };

    printf("Test quad: face=%d, (%d,%d,%d) to (%d,%d,%d)\n",
           test_quad.face, test_quad.x0, test_quad.y0, test_quad.z0,
           test_quad.x1, test_quad.y1, test_quad.z1);

    // Test each face type
    for (int face = 0; face < 6; face++) {
        test_quad.face = face;

        printf("\n--- Face %d ---\n", face);

        // Generate corners based on face type
        float corners[4][3];

        if (face == 0) // +Z face (top) - normal pointing +Z
        {
            // Counter-clockwise winding when viewed from outside (+Z direction)
            // Bottom-left, top-left, top-right, bottom-right
            corners[0][0] = (float)test_quad.x0; corners[0][1] = (float)test_quad.y0; corners[0][2] = (float)test_quad.z1;  // FIXED: use z1
            corners[1][0] = (float)test_quad.x0; corners[1][1] = (float)test_quad.y1; corners[1][2] = (float)test_quad.z1;  // FIXED: use z1
            corners[2][0] = (float)test_quad.x1; corners[2][1] = (float)test_quad.y1; corners[2][2] = (float)test_quad.z1;  // FIXED: use z1
            corners[3][0] = (float)test_quad.x1; corners[3][1] = (float)test_quad.y0; corners[3][2] = (float)test_quad.z1;  // FIXED: use z1
            printf("+Z face (top): should use z1=%d for all corners\n", test_quad.z1);
        }
        else if (face == 1) // -Z face (bottom) - normal pointing -Z
        {
            // Counter-clockwise winding when viewed from outside (-Z direction)
            // Bottom-left, bottom-right, top-right, top-left
            corners[0][0] = (float)test_quad.x0; corners[0][1] = (float)test_quad.y0; corners[0][2] = (float)test_quad.z0;  // CORRECT: use z0
            corners[1][0] = (float)test_quad.x1; corners[1][1] = (float)test_quad.y0; corners[1][2] = (float)test_quad.z0;  // CORRECT: use z0
            corners[2][0] = (float)test_quad.x1; corners[2][1] = (float)test_quad.y1; corners[2][2] = (float)test_quad.z0;  // CORRECT: use z0
            corners[3][0] = (float)test_quad.x0; corners[3][1] = (float)test_quad.y1; corners[3][2] = (float)test_quad.z0;  // CORRECT: use z0
            printf("-Z face (bottom): should use z0=%d for all corners\n", test_quad.z0);
        }
        else if (face == 2) // +Y face (front) - normal pointing +Y
        {
            // Counter-clockwise winding when viewed from outside (+Y direction)
            // Bottom-left, top-left, top-right, bottom-right
            corners[0][0] = (float)test_quad.x0; corners[0][1] = (float)test_quad.y1; corners[0][2] = (float)test_quad.z0;  // FIXED: use y1
            corners[1][0] = (float)test_quad.x0; corners[1][1] = (float)test_quad.y1; corners[1][2] = (float)test_quad.z1;  // FIXED: use y1
            corners[2][0] = (float)test_quad.x1; corners[2][1] = (float)test_quad.y1; corners[2][2] = (float)test_quad.z1;  // FIXED: use y1
            corners[3][0] = (float)test_quad.x1; corners[3][1] = (float)test_quad.y1; corners[3][2] = (float)test_quad.z0;  // FIXED: use y1
            printf("+Y face (front): should use y1=%d for all corners\n", test_quad.y1);
        }
        else if (face == 4) // -Y face (back) - normal pointing -Y
        {
            // Counter-clockwise winding when viewed from outside (-Y direction)
            // Bottom-left, bottom-right, top-right, top-left
            corners[0][0] = (float)test_quad.x0; corners[0][1] = (float)test_quad.y0; corners[0][2] = (float)test_quad.z0;  // CORRECT: use y0
            corners[1][0] = (float)test_quad.x1; corners[1][1] = (float)test_quad.y0; corners[1][2] = (float)test_quad.z0;  // CORRECT: use y0
            corners[2][0] = (float)test_quad.x1; corners[2][1] = (float)test_quad.y0; corners[2][2] = (float)test_quad.z1;  // CORRECT: use y0
            corners[3][0] = (float)test_quad.x0; corners[3][1] = (float)test_quad.y0; corners[3][2] = (float)test_quad.z1;  // CORRECT: use y0
            printf("-Y face (back): should use y0=%d for all corners\n", test_quad.y0);
        }
        else if (face == 3) // +X face (right) - normal pointing +X
        {
            // Counter-clockwise winding when viewed from outside (+X direction)
            // Bottom-left, top-left, top-right, bottom-right
            corners[0][0] = (float)test_quad.x1; corners[0][1] = (float)test_quad.y0; corners[0][2] = (float)test_quad.z0;  // FIXED: use x1
            corners[1][0] = (float)test_quad.x1; corners[1][1] = (float)test_quad.y1; corners[1][2] = (float)test_quad.z0;  // FIXED: use x1
            corners[2][0] = (float)test_quad.x1; corners[2][1] = (float)test_quad.y1; corners[2][2] = (float)test_quad.z1;  // FIXED: use x1
            corners[3][0] = (float)test_quad.x1; corners[3][1] = (float)test_quad.y0; corners[3][2] = (float)test_quad.z1;  // FIXED: use x1
            printf("+X face (right): should use x1=%d for all corners\n", test_quad.x1);
        }
        else // face == 5, -X face (left) - normal pointing -X
        {
            // Counter-clockwise winding when viewed from outside (-X direction)
            // Bottom-left, bottom-right, top-right, top-left
            corners[0][0] = (float)test_quad.x0; corners[0][1] = (float)test_quad.y0; corners[0][2] = (float)test_quad.z0;  // CORRECT: use x0
            corners[1][0] = (float)test_quad.x0; corners[1][1] = (float)test_quad.y1; corners[1][2] = (float)test_quad.z0;  // CORRECT: use x0
            corners[2][0] = (float)test_quad.x0; corners[2][1] = (float)test_quad.y1; corners[2][2] = (float)test_quad.z1;  // CORRECT: use x0
            corners[3][0] = (float)test_quad.x0; corners[3][1] = (float)test_quad.y0; corners[3][2] = (float)test_quad.z1;  // CORRECT: use x0
            printf("-X face (left): should use x0=%d for all corners\n", test_quad.x0);
        }

        // Display generated corners
        printf("Generated corners:\n");
        for (int j = 0; j < 4; j++) {
            printf("  Corner %d: (%.1f,%.1f,%.1f)\n", j, corners[j][0], corners[j][1], corners[j][2]);
        }

        // Check if corners are distinct (they should be for a proper quad)
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
            printf("    ✓ All corners are distinct\n");
        } else {
            printf("    ✗ Some corners are identical (degenerate quad)\n");
        }
    }
}

// Test with a 2x2x2 cube to show proper behavior
static void test_2x2x2_cube_corners() {
    printf("\n=== Testing 2x2x2 Cube Corner Generation ===\n");

    // Create a test quad representing a 2x2 face
    VoxelFaceQuad test_quad = {
        .face = 0,  // +Z face
        .x0 = 1, .y0 = 1, .z0 = 1,  // bottom corner
        .x1 = 2, .y1 = 2, .z1 = 2   // top corner (2x2x2 cube)
    };

    printf("Test quad: face=%d, (%d,%d,%d) to (%d,%d,%d)\n",
           test_quad.face, test_quad.x0, test_quad.y0, test_quad.z0,
           test_quad.x1, test_quad.y1, test_quad.z1);

    // Test +Z face (top)
    test_quad.face = 0;
    float corners[4][3];

    // +Z face corners (counter-clockwise when viewed from outside)
    corners[0][0] = (float)test_quad.x0; corners[0][1] = (float)test_quad.y0; corners[0][2] = (float)test_quad.z1;  // (1,1,2)
    corners[1][0] = (float)test_quad.x0; corners[1][1] = (float)test_quad.y1; corners[1][2] = (float)test_quad.z1;  // (1,2,2)
    corners[2][0] = (float)test_quad.x1; corners[2][1] = (float)test_quad.y1; corners[2][2] = (float)test_quad.z1;  // (2,2,2)
    corners[3][0] = (float)test_quad.x1; corners[3][1] = (float)test_quad.y0; corners[3][2] = (float)test_quad.z1;  // (2,1,2)

    printf("\n+Z face (top) at z=%d:\n", test_quad.z1);
    for (int j = 0; j < 4; j++) {
        printf("  Corner %d: (%.1f,%.1f,%.1f)\n", j, corners[j][0], corners[j][1], corners[j][2]);
    }

    // Verify all corners are distinct
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
        printf("    ✓ All corners are distinct (proper 2x2 quad)\n");
    } else {
        printf("    ✗ Some corners are identical (degenerate quad)\n");
    }
}

int main() {
    printf("=== Corner Generation Logic Test ===\n");
    printf("Testing the correct coordinate selection for each face type\n");

    test_corner_generation();
    test_2x2x2_cube_corners();

    printf("\n=== Test Complete ===\n");
    printf("Key findings:\n");
    printf("  ✓ +Z face should use z1 (front of voxel)\n");
    printf("  ✓ -Z face should use z0 (back of voxel)\n");
    printf("  ✓ +Y face should use y1 (front of voxel)\n");
    printf("  ✓ -Y face should use y0 (back of voxel)\n");
    printf("  ✓ +X face should use x1 (right of voxel)\n");
    printf("  ✓ -X face should use x0 (left of voxel)\n");
    printf("\nThe current code has these coordinates wrong, causing degenerate quads!\n");

    return 0;
}
