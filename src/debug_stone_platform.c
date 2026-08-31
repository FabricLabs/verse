#include <stdio.h>
#include <stdlib.h>
#include "world.h"
#include "world_bulk_ops.h"

int main(void) {
    printf("Debug Stone Platform Issue\n");
    printf("==========================\n\n");

    // Create a small test world
    World* world = world_create(32, 32, 32);
    if (!world) {
        printf("Failed to create world\n");
        return 1;
    }

    printf("World created: %ux%ux%u\n", world->width, world->height, world->depth);

    // Try to add a stone platform
    bool success = world_fill_region(world, 20, 20, 40, 24, 24, 44,
                                    VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
    printf("Stone platform result: %s\n", success ? "SUCCESS" : "FAILED");

    // Check if the coordinates are valid
    printf("Checking coordinate validity:\n");
    printf("  x range: 20 to 24 (valid: %s)\n", (20 < world->width && 24 < world->width) ? "YES" : "NO");
    printf("  y range: 20 to 24 (valid: %s)\n", (20 < world->height && 24 < world->height) ? "YES" : "NO");
    printf("  z range: 40 to 44 (valid: %s)\n", (40 < world->depth && 44 < world->depth) ? "YES" : "NO");

    // Try with smaller, valid coordinates
    printf("\nTrying with valid coordinates (10,10,10 to 15,15,15):\n");
    success = world_fill_region(world, 10, 10, 10, 15, 15, 15,
                                VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
    printf("Result: %s\n", success ? "SUCCESS" : "FAILED");

    // Count stone voxels
    uint32_t stone_count = 0;
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                Voxel* voxel = world_get_voxel(world, x, y, z);
                if (voxel && voxel->type == VOXEL_STONE) {
                    stone_count++;
                }
            }
        }
    }
    printf("Total stone voxels: %u\n", stone_count);

    world_destroy(world);
    return 0;
}
