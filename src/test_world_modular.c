/*
 * test_world_modular.c - Test if modular world code works without world.c
 */

#include <stdio.h>
#include <assert.h>
#include "world.h"
#include "world_core.h"
#include "world_generation.h"
#include "world_voxel.h"
#include "world_serialize.h"
#include "voxel.h"

int main() {
    printf("Testing modular world implementation...\n");

    // Test world creation
    World* world = world_create(32, 32, 16);
    assert(world != NULL);
    assert(world->width == 32);
    assert(world->height == 32);
    assert(world->depth == 16);
    printf("✓ World creation works\n");

    // Test voxel operations
    world_set_voxel(world, 10, 10, 5, VOXEL_STONE);
    Voxel* v = world_get_voxel(world, 10, 10, 5);
    assert(v != NULL);
    assert(v->type == VOXEL_STONE);
    printf("✓ Voxel operations work\n");

    // Test generation
    world_generate_with_type(world, "test_seed", WORLD_TYPE_HOME);
    printf("✓ World generation works\n");

    // Test save/load
    bool saved = world_save(world, "test_modular.world");
    assert(saved);
    printf("✓ World save works\n");

    World* loaded = world_load("test_modular.world");
    assert(loaded != NULL);
    assert(loaded->width == world->width);
    world_destroy(loaded);
    printf("✓ World load works\n");

    // Cleanup
    world_destroy(world);

    printf("\n✅ All modular tests passed!\n");
    printf("The modular implementation is working correctly.\n");

    return 0;
}
