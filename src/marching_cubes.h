#ifndef MARCHING_CUBES_H
#define MARCHING_CUBES_H

#include "simple_world.h"
#include <stdint.h>
#include <stdbool.h>

// Vertex structure for the generated mesh
typedef struct {
    float x, y, z;
    float nx, ny, nz;  // Normal vector
    uint8_t r, g, b, a; // Color
} MarchingCubesVertex;

// Triangle structure
typedef struct {
    uint32_t vertices[3];  // Indices into vertex array
} MarchingCubesTriangle;

// Generated mesh structure
typedef struct {
    MarchingCubesVertex *vertices;
    MarchingCubesTriangle *triangles;
    uint32_t vertex_count;
    uint32_t triangle_count;
    uint32_t capacity;
} MarchingCubesMesh;

// Marching cubes configuration
typedef struct {
    float isolevel;           // Surface threshold (0.0 = air, 1.0 = solid)
    float voxel_size;         // Size of each voxel in world units
    bool generate_normals;    // Whether to generate normal vectors
    bool generate_colors;     // Whether to generate colors from voxel types
    bool smooth_normals;      // Whether to smooth normals across vertices
} MarchingCubesConfig;

// Default configuration
#define MARCHING_CUBES_DEFAULT_CONFIG { \
    .isolevel = 0.5f, \
    .voxel_size = 0.06f, \
    .generate_normals = true, \
    .generate_colors = true, \
    .smooth_normals = true \
}

// Function prototypes
MarchingCubesMesh* marching_cubes_create_mesh(void);
void marching_cubes_destroy_mesh(MarchingCubesMesh *mesh);
bool marching_cubes_generate_from_world(const SimpleWorld *world,
                                       const MarchingCubesConfig *config,
                                       MarchingCubesMesh *mesh);

// Utility functions
bool marching_cubes_export_obj(const MarchingCubesMesh *mesh, const char *filename);
bool marching_cubes_export_stl(const MarchingCubesMesh *mesh, const char *filename);

// Memory management
bool marching_cubes_reserve_capacity(MarchingCubesMesh *mesh, uint32_t vertex_capacity, uint32_t triangle_capacity);

#endif // MARCHING_CUBES_H
