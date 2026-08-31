#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "simple_world.h"
#include "marching_cubes.h"

// Create a test world for marching cubes demonstration
SimpleWorld *create_test_world(void)
{
  SimpleWorld *world = simple_world_create(2, 2, 2);
  if (!world)
  {
    return NULL;
  }

  // Fill with air first
  for (uint32_t x = 0; x < world->width; x++)
  {
    for (uint32_t y = 0; y < world->height; y++)
    {
      for (uint32_t z = 0; z < world->depth; z++)
      {
        simple_world_set_voxel(world, x, y, z, VOXEL_AIR);
      }
    }
  }

  // Create a solid 2x2x1 face that will generate a proper surface
  simple_world_set_voxel(world, 0, 0, 0, VOXEL_STONE);  // Bottom-left
  simple_world_set_voxel(world, 1, 0, 0, VOXEL_STONE);  // Bottom-right
  simple_world_set_voxel(world, 0, 1, 0, VOXEL_STONE);  // Top-left
  simple_world_set_voxel(world, 1, 1, 0, VOXEL_STONE);  // Top-right - now solid

  return world;
}

int main(void)
{
  printf("Marching Cubes Demo\n");
  printf("==================\n\n");

  // Create test world
  printf("Creating test world...\n");
  SimpleWorld *world = create_test_world();
  if (!world)
  {
    printf("Failed to create test world\n");
    return 1;
  }

  printf("World created: %ux%ux%u\n", world->width, world->height, world->depth);

  // Count voxels by type
  size_t total_voxels = 0;
  size_t solid_voxels = 0;
  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t y = 0; y < world->height; y++)
    {
      for (uint32_t x = 0; x < world->width; x++)
      {
        const SimpleVoxel *voxel = simple_world_get_voxel(world, x, y, z);
        if (voxel && voxel->type != VOXEL_AIR)
        {
          solid_voxels++;
        }
        total_voxels++;
      }
    }
  }
  printf("Solid voxels: %zu / %zu (%.1f%%)\n", solid_voxels, total_voxels,
         (float)solid_voxels / total_voxels * 100.0f);

  // Create marching cubes mesh
  printf("\nGenerating mesh with marching cubes...\n");
  MarchingCubesMesh *mesh = marching_cubes_create_mesh();
  if (!mesh)
  {
    printf("Failed to create mesh\n");
    simple_world_destroy(world);
    return 1;
  }

  // Configure marching cubes
  MarchingCubesConfig config = MARCHING_CUBES_DEFAULT_CONFIG;
  config.isolevel = 0.3f; // More conservative threshold for solid mesh
  config.voxel_size = 0.06f;
  config.generate_normals = true;
  config.generate_colors = true;
  config.smooth_normals = true;

  // Generate the mesh
  if (!marching_cubes_generate_from_world(world, &config, mesh))
  {
    printf("Failed to generate mesh\n");
    marching_cubes_destroy_mesh(mesh);
    simple_world_destroy(world);
    return 1;
  }

  printf("Mesh generated successfully!\n");
  printf("Vertices: %u\n", mesh->vertex_count);
  printf("Triangles: %u\n", mesh->triangle_count);

  // Export to OBJ format
  printf("\nExporting to OBJ format...\n");
  if (marching_cubes_export_obj(mesh, "test_world.obj"))
  {
    printf("Exported to test_world.obj\n");
  }
  else
  {
    printf("Failed to export OBJ\n");
  }

  // Export to STL format
  printf("Exporting to STL format...\n");
  if (marching_cubes_export_stl(mesh, "test_world.stl"))
  {
    printf("Exported to test_world.stl\n");
  }
  else
  {
    printf("Failed to export STL\n");
  }

  // Cleanup
  marching_cubes_destroy_mesh(mesh);
  simple_world_destroy(world);

  printf("\nDemo completed successfully!\n");
  printf("You can now open test_world.obj or test_world.stl in a 3D viewer\n");

  return 0;
}
