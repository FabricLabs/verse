#include <stdio.h>
#include <stdlib.h>
#include "src/voxel_mesh.h"
#include "src/world.h"

int main() {
    printf("Testing Y-up coordinate transformation with 1x1x1 solid world...\n");

    // Generate a 1x1x1 world using solid fill
    World test_world = {0};
    test_world.width = 1;
    test_world.height = 1;
    test_world.depth = 1;

    // Allocate the voxels array
    size_t voxel_count = test_world.width * test_world.height * test_world.depth;
    test_world.voxels = malloc(voxel_count * sizeof(Voxel));
    if (!test_world.voxels) {
        printf("Failed to allocate voxels array!\n");
        return 1;
    }

    // Fill with solid voxels (type 1 = stone, data8 = 0)
    for (size_t i = 0; i < voxel_count; i++) {
        test_world.voxels[i] = (Voxel){.type = 1, .condition_mask = 0, .data8 = 0, .rotation = {0, 0, 0}, .momentum = {0, 0, 0}};
    }

    printf("Generated 1x1x1 solid world\n");

    // Generate mesh
    VoxelMesh mesh;
    voxel_mesh_init(&mesh);
    voxel_mesh_build_all_faces_greedy(&test_world, &mesh);

    printf("Generated mesh with %d quads\n", mesh.count);

    // Export with Z-up (VERSE native)
    if (voxel_mesh_export_obj_with_transform(&mesh, "test_1x1x1_zup.obj", false)) {
        printf("Exported Z-up mesh to test_1x1x1_zup.obj\n");
    }

    // Export with Y-up (transformed)
    if (voxel_mesh_export_obj_with_transform(&mesh, "test_1x1x1_yup.obj", true)) {
        printf("Exported Y-up mesh to test_1x1x1_yup.obj\n");
    }

    // Cleanup
    voxel_mesh_free(&mesh);
    free(test_world.voxels);

    printf("Test complete!\n");
    return 0;
}
