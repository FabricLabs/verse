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
    VoxelFaceQuad *quads;
    int count;
    int capacity;
} VoxelMesh;

// Helper function to get voxel at coordinates
static uint8_t world_voxel_get(const World *w, int x, int y, int z) {
    if (x < 0 || x >= w->width || y < 0 || y >= w->height || z < 0 || z >= w->depth) {
        return VOXEL_AIR;
    }
    size_t idx = ((size_t)z * w->height + y) * w->width + x;
    return w->voxels[idx];
}

// Fixed greedy merge algorithm (copied from test_greedy_fix.c)
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

// Test OBJ export logic
static void test_obj_export(const VoxelFaceQuad *quads, int quad_count, const char *filename) {
    printf("\n=== Testing OBJ Export ===\n");
    printf("Exporting %d quads to %s\n", quad_count, filename);

    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("ERROR: Could not open file %s\n", filename);
        return;
    }

    // Write OBJ header
    fprintf(file, "# Voxel mesh export\n");
    fprintf(file, "# Generated by verse voxel mesh generator\n");
    fprintf(file, "# %d faces\n\n", quad_count);

    // First pass: collect all unique vertices
    typedef struct {
        float x, y, z;
        int index;
    } UniqueVertex;

    UniqueVertex *unique_vertices = malloc(quad_count * 4 * sizeof(UniqueVertex));
    if (!unique_vertices) {
        fclose(file);
        return;
    }

    int unique_count = 0;
    printf("First pass: collecting unique vertices from %d quads\n", quad_count);

    // Collect all vertices from all quads
    for (int i = 0; i < quad_count; i++) {
        const VoxelFaceQuad *quad = &quads[i];
        printf("Processing quad %d: face=%d, (%d,%d,%d) to (%d,%d,%d)\n",
               i, quad->face, quad->x0, quad->y0, quad->z0, quad->x1, quad->y1, quad->z1);

        // Generate the 4 corners of this quad
        float corners[4][3];

        if (quad->face == 0) { // +Z face (top) - normal pointing +Z
            // Counter-clockwise winding when viewed from outside (+Z direction)
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x0; corners[1][1] = (float)quad->y1; corners[1][2] = (float)quad->z0;
            corners[2][0] = (float)quad->x1; corners[2][1] = (float)quad->y1; corners[2][2] = (float)quad->z0;
            corners[3][0] = (float)quad->x1; corners[3][1] = (float)quad->y0; corners[3][2] = (float)quad->z0;
        } else if (quad->face == 1) { // -Z face (bottom) - normal pointing -Z
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x1; corners[1][1] = (float)quad->y0; corners[1][2] = (float)quad->z0;
            corners[2][0] = (float)quad->x1; corners[2][1] = (float)quad->y1; corners[2][2] = (float)quad->z0;
            corners[3][0] = (float)quad->x0; corners[3][1] = (float)quad->y1; corners[3][2] = (float)quad->z0;
        } else if (quad->face == 2) { // +Y face (front) - normal pointing +Y
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x0; corners[1][1] = (float)quad->y0; corners[1][2] = (float)quad->z1;
            corners[2][0] = (float)quad->x1; corners[2][1] = (float)quad->y0; corners[2][2] = (float)quad->z1;
            corners[3][0] = (float)quad->x1; corners[3][1] = (float)quad->y0; corners[3][2] = (float)quad->z0;
        } else if (quad->face == 4) { // -Y face (back) - normal pointing -Y
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x1; corners[1][1] = (float)quad->y0; corners[1][2] = (float)quad->z0;
            corners[2][0] = (float)quad->x1; corners[2][1] = (float)quad->y0; corners[2][2] = (float)quad->z1;
            corners[3][0] = (float)quad->x0; corners[3][1] = (float)quad->y0; corners[3][2] = (float)quad->z1;
        } else if (quad->face == 3) { // +X face (right) - normal pointing +X
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x0; corners[1][1] = (float)quad->y1; corners[1][2] = (float)quad->z0;
            corners[2][0] = (float)quad->x0; corners[2][1] = (float)quad->y1; corners[2][2] = (float)quad->z1;
            corners[3][0] = (float)quad->x0; corners[3][1] = (float)quad->y0; corners[3][2] = (float)quad->z1;
        } else { // -X face (left) - normal pointing -X
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x0; corners[1][1] = (float)quad->y0; corners[1][2] = (float)quad->z1;
            corners[2][0] = (float)quad->x0; corners[2][1] = (float)quad->y1; corners[2][2] = (float)quad->z1;
            corners[3][0] = (float)quad->x0; corners[3][1] = (float)quad->y1; corners[3][2] = (float)quad->z0;
        }

        // Add all 4 corners to unique vertices
        for (int j = 0; j < 4; j++) {
            unique_vertices[unique_count].x = corners[j][0];
            unique_vertices[unique_count].y = corners[j][1];
            unique_vertices[unique_count].z = corners[j][2];
            unique_vertices[unique_count].index = unique_count;
            unique_count++;
        }
    }

    printf("Collected %d total vertices\n", unique_count);

    // Second pass: deduplicate vertices and assign indices
    printf("Second pass: deduplicating vertices\n");
    for (int i = 0; i < unique_count; i++) {
        if (unique_vertices[i].index != i) continue; // Already processed

        for (int j = i + 1; j < unique_count; j++) {
            if (fabs(unique_vertices[i].x - unique_vertices[j].x) < 0.001f &&
                fabs(unique_vertices[i].y - unique_vertices[j].y) < 0.001f &&
                fabs(unique_vertices[i].z - unique_vertices[j].z) < 0.001f) {
                unique_vertices[j].index = i; // Point to the first occurrence
            }
        }
    }

    // Count actual unique vertices
    int actual_unique_count = 0;
    for (int i = 0; i < unique_count; i++) {
        if (unique_vertices[i].index == i) {
            actual_unique_count++;
        }
    }
    printf("Found %d unique vertices after deduplication\n", actual_unique_count);

    // Third pass: write unique vertices
    printf("Third pass: writing unique vertices\n");
    for (int i = 0; i < unique_count; i++) {
        if (unique_vertices[i].index == i) {
            fprintf(file, "v %.1f %.1f %.1f\n",
                    unique_vertices[i].x, unique_vertices[i].y, unique_vertices[i].z);
        }
    }

    // Fourth pass: write faces using vertex indices
    printf("Fourth pass: writing faces\n");
    for (int i = 0; i < quad_count; i++) {
        const VoxelFaceQuad *quad = &quads[i];

        // Generate the 4 corners of this quad (same logic as above)
        float corners[4][3];

        if (quad->face == 0) { // +Z face
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x0; corners[1][1] = (float)quad->y1; corners[1][2] = (float)quad->z0;
            corners[2][0] = (float)quad->x1; corners[2][1] = (float)quad->y1; corners[2][2] = (float)quad->z0;
            corners[3][0] = (float)quad->x1; corners[3][1] = (float)quad->y0; corners[3][2] = (float)quad->z0;
        } else if (quad->face == 1) { // -Z face
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x1; corners[1][1] = (float)quad->y0; corners[1][2] = (float)quad->z0;
            corners[2][0] = (float)quad->x1; corners[2][1] = (float)quad->y1; corners[2][2] = (float)quad->z0;
            corners[3][0] = (float)quad->x0; corners[3][1] = (float)quad->y1; corners[3][2] = (float)quad->z0;
        } else if (quad->face == 2) { // +Y face
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x0; corners[1][1] = (float)quad->y0; corners[1][2] = (float)quad->z1;
            corners[2][0] = (float)quad->x1; corners[2][1] = (float)quad->y0; corners[2][2] = (float)quad->z1;
            corners[3][0] = (float)quad->x1; corners[3][1] = (float)quad->y0; corners[3][2] = (float)quad->z0;
        } else if (quad->face == 4) { // -Y face
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x1; corners[1][1] = (float)quad->y0; corners[1][2] = (float)quad->z0;
            corners[2][0] = (float)quad->x1; corners[2][1] = (float)quad->y0; corners[2][2] = (float)quad->z1;
            corners[3][0] = (float)quad->x0; corners[3][1] = (float)quad->y0; corners[3][2] = (float)quad->z1;
        } else if (quad->face == 3) { // +X face
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x0; corners[1][1] = (float)quad->y1; corners[1][2] = (float)quad->z0;
            corners[2][0] = (float)quad->x0; corners[2][1] = (float)quad->y1; corners[2][2] = (float)quad->z1;
            corners[3][0] = (float)quad->x0; corners[3][1] = (float)quad->y0; corners[3][2] = (float)quad->z1;
        } else { // -X face
            corners[0][0] = (float)quad->x0; corners[0][1] = (float)quad->y0; corners[0][2] = (float)quad->z0;
            corners[1][0] = (float)quad->x0; corners[1][1] = (float)quad->y0; corners[1][2] = (float)quad->z1;
            corners[2][0] = (float)quad->x0; corners[2][1] = (float)quad->y1; corners[2][2] = (float)quad->z1;
            corners[3][0] = (float)quad->x0; corners[3][1] = (float)quad->y1; corners[3][2] = (float)quad->z0;
        }

        // Find the vertex indices for all 4 corners
        int vertex_indices[4];
        for (int j = 0; j < 4; j++) {
            vertex_indices[j] = -1; // Initialize to invalid index
            for (int k = 0; k < unique_count; k++) {
                if (fabs(unique_vertices[k].x - corners[j][0]) < 0.001f &&
                    fabs(unique_vertices[k].y - corners[j][1]) < 0.001f &&
                    fabs(unique_vertices[k].z - corners[j][2]) < 0.001f) {
                    vertex_indices[j] = k + 1; // OBJ indices are 1-based
                    break;
                }
            }
            // Verify we found a valid index
            if (vertex_indices[j] == -1) {
                printf("ERROR: Could not find vertex index for corner %d (%.1f,%.1f,%.1f)\n",
                       j, corners[j][0], corners[j][1], corners[j][2]);
                fclose(file);
                free(unique_vertices);
                return;
            }
        }

        // Write the face as two triangles (quad triangulation)
        fprintf(file, "f %d %d %d\n", vertex_indices[0], vertex_indices[1], vertex_indices[2]);
        fprintf(file, "f %d %d %d\n", vertex_indices[0], vertex_indices[2], vertex_indices[3]);
    }

    printf("OBJ export complete: %d faces written\n", quad_count * 2);
    fclose(file);
    free(unique_vertices);
}

int main() {
    printf("=== OBJ Export Test ===\n");

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

    // Generate mesh using fixed greedy merge
    VoxelFaceQuad all_quads[100]; // Max 100 quads
    int total_quads = 0;

    for (int face = 0; face < 6; face++) {
        int slice;
        int W, H;

        if (face == 0 || face == 1) { // Z faces
            slice = base_z + (face == 0 ? 1 : 0);
            W = test_world.width;
            H = test_world.height;
        } else if (face == 2 || face == 4) { // Y faces
            slice = base_y + (face == 2 ? 1 : 0);
            W = test_world.width;
            H = test_world.depth;
        } else { // X faces
            slice = base_x + (face == 3 ? 1 : 0);
            W = test_world.height;
            H = test_world.depth;
        }

        int face_quads = 0;
        fixed_greedy_merge(&test_world, face, slice, W, H, &all_quads[total_quads], &face_quads);
        total_quads += face_quads;
    }

    printf("\nTotal quads generated: %d\n", total_quads);

    // Test OBJ export
    test_obj_export(all_quads, total_quads, "test_fixed_mesh.obj");

    // Cleanup
    free(test_world.voxels);

    printf("\n=== Test Complete ===\n");
    return 0;
}
