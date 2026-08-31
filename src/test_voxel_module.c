/*
 * test_voxel_module.c - Test program for world_voxel module
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "world_internal.h"
#include "world_voxel.h"

// Simple world creation for testing
static World* create_test_world(uint32_t width, uint32_t height, uint32_t depth) {
    World* world = (World*)calloc(1, sizeof(World));
    if (!world) return NULL;

    world->width = width;
    world->height = height;
    world->depth = depth;

    size_t voxel_count = (size_t)width * height * depth;
    world->voxels = (Voxel*)calloc(voxel_count, sizeof(Voxel));
    if (!world->voxels) {
        free(world);
        return NULL;
    }

    // Initialize all voxels to air
    for (size_t i = 0; i < voxel_count; i++) {
        world->voxels[i].type = VOXEL_AIR;
    }

    return world;
}

static void destroy_test_world(World* world) {
    if (world) {
        free(world->voxels);
        free(world);
    }
}

void test_basic_operations() {
    printf("Testing basic voxel operations...\n");

    World* world = create_test_world(16, 16, 16);
    assert(world != NULL);

    // Test position validation
    assert(world_is_position_valid(world, 0, 0, 0));
    assert(world_is_position_valid(world, 15, 15, 15));
    assert(!world_is_position_valid(world, 16, 0, 0));
    assert(!world_is_position_valid(world, 0, 16, 0));
    assert(!world_is_position_valid(world, 0, 0, 16));

    // Test get/set voxel
    assert(world_set_voxel(world, 5, 5, 5, VOXEL_STONE));
    Voxel* v = world_get_voxel(world, 5, 5, 5);
    assert(v != NULL);
    assert(v->type == VOXEL_STONE);

    // Test invalid position
    assert(!world_set_voxel(world, 100, 100, 100, VOXEL_STONE));
    assert(world_get_voxel(world, 100, 100, 100) == NULL);

    printf("  ✓ Basic operations passed\n");
    destroy_test_world(world);
}

void test_voxel_queries() {
    printf("Testing voxel type queries...\n");

    World* world = create_test_world(10, 10, 10);
    assert(world != NULL);

    // Test air detection
    assert(world_is_air(world, 0, 0, 0));
    world_set_voxel(world, 0, 0, 0, VOXEL_STONE);
    assert(!world_is_air(world, 0, 0, 0));

    // Test solid detection
    assert(!world_is_solid(world, 1, 1, 1)); // Air is not solid
    world_set_voxel(world, 1, 1, 1, VOXEL_STONE);
    assert(world_is_solid(world, 1, 1, 1));
    world_set_voxel(world, 1, 1, 1, VOXEL_WATER);
    assert(!world_is_solid(world, 1, 1, 1)); // Water is not solid

    // Test liquid detection
    assert(!world_is_liquid(world, 2, 2, 2)); // Air is not liquid
    world_set_voxel(world, 2, 2, 2, VOXEL_WATER);
    assert(world_is_liquid(world, 2, 2, 2));
    world_set_voxel(world, 3, 3, 3, VOXEL_MAGMA);
    assert(world_is_liquid(world, 3, 3, 3));

    printf("  ✓ Voxel queries passed\n");
    destroy_test_world(world);
}

void test_metadata() {
    printf("Testing voxel metadata...\n");

    World* world = create_test_world(8, 8, 8);
    assert(world != NULL);

    // Test metadata storage
    world_set_voxel(world, 2, 2, 2, VOXEL_STONE);
    world_set_voxel_metadata(world, 2, 2, 2, 42);
    assert(world_get_voxel_metadata(world, 2, 2, 2) == 42);

    // Test metadata preservation
    world_set_voxel_metadata(world, 2, 2, 2, 255);
    assert(world_get_voxel_metadata(world, 2, 2, 2) == 255);

    // Test invalid position
    assert(world_get_voxel_metadata(world, 100, 100, 100) == 0);

    printf("  ✓ Metadata operations passed\n");
    destroy_test_world(world);
}

void test_colors() {
    printf("Testing voxel colors...\n");

    uint8_t r, g, b;

    // Test some common voxel colors
    world_voxel_type_color(VOXEL_STONE, &r, &g, &b);
    printf("  Stone color: RGB(%d, %d, %d)\n", r, g, b);
    assert(r > 0 || g > 0 || b > 0); // Not black

    world_voxel_type_color(VOXEL_WATER, &r, &g, &b);
    printf("  Water color: RGB(%d, %d, %d)\n", r, g, b);
    assert(b > r && b > g); // Blue dominant

    world_voxel_type_color(VOXEL_MAGMA, &r, &g, &b);
    printf("  Magma color: RGB(%d, %d, %d)\n", r, g, b);
    assert(r > b); // Red dominant

    printf("  ✓ Color tests passed\n");
}

void test_properties() {
    printf("Testing voxel properties...\n");

    // Test mass
    float stone_mass = world_voxel_type_mass(VOXEL_STONE);
    float air_mass = world_voxel_type_mass(VOXEL_AIR);
    printf("  Stone mass: %.3f kg\n", stone_mass);
    printf("  Air mass: %.3f kg\n", air_mass);
    assert(stone_mass > air_mass);

    // Test transparency
    assert(world_voxel_type_is_transparent(VOXEL_AIR));
    assert(world_voxel_type_is_transparent(VOXEL_WATER));
    assert(!world_voxel_type_is_transparent(VOXEL_STONE));

    // Test source blocks
    assert(world_voxel_type_is_source(VOXEL_SPRING));
    assert(!world_voxel_type_is_source(VOXEL_WATER));

    printf("  ✓ Property tests passed\n");
}

void test_region_operations() {
    printf("Testing region operations...\n");

    World* world = create_test_world(20, 20, 20);
    assert(world != NULL);

    // Test fill region
    assert(world_fill_region(world, 5, 5, 5, 10, 10, 10, VOXEL_STONE));

    // Check filled region
    for (uint32_t z = 5; z <= 10; z++) {
        for (uint32_t y = 5; y <= 10; y++) {
            for (uint32_t x = 5; x <= 10; x++) {
                Voxel* v = world_get_voxel(world, x, y, z);
                assert(v && v->type == VOXEL_STONE);
            }
        }
    }

    // Check outside region is still air
    assert(world_is_air(world, 4, 4, 4));
    assert(world_is_air(world, 11, 11, 11));

    // Test clear region
    assert(world_clear_region(world, 7, 7, 7, 8, 8, 8));
    assert(world_is_air(world, 7, 7, 7));
    assert(world_is_air(world, 8, 8, 8));

    printf("  ✓ Region operations passed\n");
    destroy_test_world(world);
}

void test_neighbors() {
    printf("Testing neighbor queries...\n");

    World* world = create_test_world(10, 10, 10);
    assert(world != NULL);

    // Create a cross pattern of stone
    world_set_voxel(world, 5, 5, 5, VOXEL_STONE); // Center
    world_set_voxel(world, 4, 5, 5, VOXEL_STONE); // West
    world_set_voxel(world, 6, 5, 5, VOXEL_STONE); // East
    world_set_voxel(world, 5, 4, 5, VOXEL_STONE); // North
    world_set_voxel(world, 5, 6, 5, VOXEL_STONE); // South

    // Test neighbor counting
    assert(world_count_neighbors(world, 5, 5, 5, VOXEL_STONE) == 4);
    assert(world_count_neighbors(world, 4, 5, 5, VOXEL_STONE) == 1);
    assert(world_count_neighbors(world, 3, 3, 3, VOXEL_STONE) == 0);

    // Test has neighbor
    assert(world_has_neighbor(world, 5, 5, 5, VOXEL_STONE));
    assert(!world_has_neighbor(world, 0, 0, 0, VOXEL_STONE));

    printf("  ✓ Neighbor queries passed\n");
    destroy_test_world(world);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== World Voxel Module Test Suite ===\n\n");

    test_basic_operations();
    test_voxel_queries();
    test_metadata();
    test_colors();
    test_properties();
    test_region_operations();
    test_neighbors();

    printf("\n✅ All tests passed!\n");
    printf("\nThe voxel module has been successfully extracted and works correctly.\n");

    return 0;
}
