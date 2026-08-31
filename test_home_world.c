#include <stdio.h>
#include <stdlib.h>
#include "src/voxel_mesh.h"
#include "src/world.h"

int main() {
    printf("Testing HOME world generation and Y-up OBJ export...\n");

    // Generate a HOME world (64x64x64 as updated)
    World test_world = {0};
    test_world.width = 64;
    test_world.height = 64;
    test_world.depth = 64;

    // Allocate the voxels array
    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(Voxel));
    if (!test_world.voxels) {
        printf("Failed to allocate voxels array!\n");
        return 1;
    }

    // Initialize all voxels to AIR
    for (size_t i = 0; i < voxel_count; i++) {
        test_world.voxels[i] = (Voxel){.type = VOXEL_AIR, .condition_mask = 0, .data8 = 0, .rotation = {0, 0, 0}, .momentum = {0, 0, 0}};
    }

    printf("Generated empty 64x64x64 world\n");

    // Generate the HOME world using the world_generate_with_type function
    // We need to set the generation type first
    test_world.generation_type = WORLD_TYPE_HOME;

    // Generate the home world
    world_generate_with_type(&test_world, "test_home_seed", WORLD_TYPE_HOME);

    printf("Generated HOME world with island in the sky\n");

    // Save the world using the enhanced World struct save functionality
    // This now automatically saves to worlds/<SEED>.<TYPE>.world format
    if (world_save_by_seed(&test_world, "test_home_seed")) {
        printf("Saved HOME world to worlds directory using enhanced save format\n");
    } else {
        printf("Failed to save HOME world!\n");
    }

    // Generate mesh
    VoxelMesh mesh;
    voxel_mesh_init(&mesh);
    voxel_mesh_build_all_faces_greedy(&test_world, &mesh);

    printf("Generated mesh with %d quads\n", mesh.count);

    // Export with Y-up (transformed) - this is the primary export format
    if (voxel_mesh_export_obj_with_transform(&mesh, "home_world_yup.obj", true)) {
        printf("Exported HOME world Y-up mesh to home_world_yup.obj\n");
    } else {
        printf("Failed to export HOME world mesh!\n");
    }

    // Cleanup
    voxel_mesh_free(&mesh);
    free(test_world.voxels);

    printf("Test complete! Check home_world_yup.obj for the mesh export and worlds/ for the saved world.\n");
    return 0;
}
