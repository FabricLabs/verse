# Marching Cubes Algorithm Implementation

This implementation provides a complete marching cubes algorithm for generating smooth 3D meshes from voxel data. It's designed to work with the existing voxel world system in the project.

## Overview

The marching cubes algorithm is a computer graphics algorithm for creating a 3D mesh from a 3D scalar field (voxel data). It's particularly useful for:
- Converting voxel worlds into smooth, renderable 3D models
- Creating terrain meshes from height maps
- Generating organic shapes from procedural data
- Converting medical imaging data into 3D models

## Features

- **Complete Implementation**: Full 256-case marching cubes algorithm
- **Flexible Configuration**: Configurable isolevel, voxel size, and output options
- **Multiple Export Formats**: OBJ and STL file export
- **Normal Generation**: Automatic normal calculation with smoothing options
- **Color Support**: Voxel type-based color interpolation
- **Memory Efficient**: Dynamic memory allocation with capacity management
- **Integration Ready**: Designed to work with existing world.h/world.c system

## Files

- `marching_cubes.h` - Header file with data structures and function declarations
- `marching_cubes.c` - Implementation of the marching cubes algorithm
- `marching_cubes_demo.c` - Demo program showing usage
- `Makefile` - Build configuration
- `README_marching_cubes.md` - This documentation

## Quick Start

### Building

```bash
make
```

### Running the Demo

```bash
make run
```

This will generate a test world and create both OBJ and STL files that you can open in any 3D viewer.

## API Usage

### Basic Usage

```c
#include "marching_cubes.h"

// Create a mesh
MarchingCubesMesh *mesh = marching_cubes_create_mesh();

// Configure the algorithm
MarchingCubesConfig config = MARCHING_CUBES_DEFAULT_CONFIG;
config.isolevel = 0.5f;           // Surface threshold
config.voxel_size = 0.06f;        // Size of each voxel
config.generate_normals = true;    // Generate normal vectors
config.generate_colors = true;     // Generate colors from voxel types

// Generate mesh from world
if (marching_cubes_generate_from_world(world, &config, mesh)) {
    printf("Generated %u vertices and %u triangles\n",
           mesh->vertex_count, mesh->triangle_count);

    // Export to file
    marching_cubes_export_obj(mesh, "output.obj");
    marching_cubes_export_stl(mesh, "output.stl");
}

// Cleanup
marching_cubes_destroy_mesh(mesh);
```

### Configuration Options

- **isolevel**: Threshold for determining inside/outside the surface (0.0 = air, 1.0 = solid)
- **voxel_size**: Physical size of each voxel in world units
- **generate_normals**: Whether to calculate normal vectors
- **generate_colors**: Whether to interpolate colors from voxel types
- **smooth_normals**: Whether to smooth normals across vertices

### Data Structures

#### MarchingCubesVertex
```c
typedef struct {
    float x, y, z;           // Position
    float nx, ny, nz;        // Normal vector
    uint8_t r, g, b, a;      // Color (RGBA)
} MarchingCubesVertex;
```

#### MarchingCubesTriangle
```c
typedef struct {
    uint32_t vertices[3];    // Indices into vertex array
} MarchingCubesTriangle;
```

#### MarchingCubesMesh
```c
typedef struct {
    MarchingCubesVertex *vertices;
    MarchingCubesTriangle *triangles;
    uint32_t vertex_count;
    uint32_t triangle_count;
    uint32_t capacity;
} MarchingCubesMesh;
```

## Algorithm Details

### How It Works

1. **Grid Sampling**: The algorithm processes the voxel grid cube by cube
2. **Case Classification**: Each cube is classified based on which of its 8 corners are inside/outside the surface
3. **Edge Interpolation**: For edges that cross the surface, vertices are interpolated at the isolevel
4. **Triangle Generation**: Using lookup tables, triangles are generated for each cube configuration
5. **Vertex Deduplication**: Shared vertices are identified to avoid duplication
6. **Normal Calculation**: Surface normals are calculated and optionally smoothed

### Performance Characteristics

- **Time Complexity**: O(n³) where n is the grid dimension
- **Space Complexity**: O(n³) for worst-case scenarios
- **Memory Usage**: Efficient with dynamic allocation and vertex deduplication
- **Optimizations**: Early exit for empty cubes, efficient edge table lookups

## Integration with Existing Code

This implementation is designed to work seamlessly with the existing world system:

- Uses `world_get_voxel()` for voxel access
- Integrates with `world_voxel_type_color()` for color generation
- Respects the `VOXEL_METERS_PER_SIDE` constant for proper scaling
- Compatible with all existing voxel types

## Export Formats

### OBJ Format
- Human-readable text format
- Includes vertices, normals, and faces
- Compatible with most 3D software (Blender, Maya, 3ds Max, etc.)

### STL Format
- Binary format for efficiency
- Standard format for 3D printing
- Includes triangle normals and vertices

## Examples

### Terrain Generation
```c
// Generate terrain from height map
for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
        int height = generate_height(x, y);
        for (int z = 0; z < height; z++) {
            world_set_voxel(world, x, y, z, VOXEL_STONE);
        }
    }
}
```

### Procedural Caves
```c
// Generate cave system using noise
for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
        for (int z = 0; z < depth; z++) {
            float noise = perlin_noise(x * 0.1, y * 0.1, z * 0.1);
            if (noise > 0.6) {
                world_set_voxel(world, x, y, z, VOXEL_AIR);
            }
        }
    }
}
```

## Troubleshooting

### Common Issues

1. **No triangles generated**: Check that your isolevel is appropriate for your data
2. **Memory errors**: Ensure your world dimensions aren't too large
3. **Export failures**: Check file permissions and disk space
4. **Poor quality meshes**: Try adjusting the isolevel or voxel size

### Debug Tips

- Start with small worlds (16x16x16) for testing
- Use the demo program to verify basic functionality
- Check that voxel types are properly set (not all AIR)
- Verify that the world bounds are correct

## Future Enhancements

Potential improvements that could be added:

- **Level of Detail**: Adaptive mesh generation based on distance
- **Texture Coordinates**: UV mapping for material textures
- **Compression**: Efficient storage of large meshes
- **GPU Acceleration**: OpenGL/DirectX integration
- **Animation Support**: Keyframe-based mesh morphing
- **LOD Generation**: Multiple detail levels for performance

## License

This implementation is part of the main project and follows the same licensing terms.

## Contributing

When contributing to this implementation:

1. Maintain the existing API compatibility
2. Add tests for new features
3. Update documentation for API changes
4. Follow the existing code style
5. Test with various world sizes and configurations
