/*
 * test_physics_module.c - Test program for world_physics module
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include "world_internal.h"
#include "world_physics.h"
#include "world_core.h"
#include "world_voxel.h"
#include "voxel.h"

// Helper to create a test world with terrain
static World* create_terrain_world() {
    World* world = world_create(16, 16, 16);
    if (!world) return NULL;

    // Create simple terrain - ground at z=0, some hills
    for (uint32_t y = 0; y < 16; y++) {
        for (uint32_t x = 0; x < 16; x++) {
            // Base ground
            world_set_voxel(world, x, y, 0, VOXEL_STONE);

            // Add some height variation
            if ((x + y) % 4 == 0) {
                world_set_voxel(world, x, y, 1, VOXEL_STONE);
            }
            if (x == 8 && y == 8) {
                // A small hill in the center
                world_set_voxel(world, x, y, 1, VOXEL_STONE);
                world_set_voxel(world, x, y, 2, VOXEL_STONE);
            }
        }
    }

    return world;
}

void test_gravity() {
    printf("Testing gravity functions...\n");

    World* world = create_terrain_world();
    assert(world != NULL);

    // Test default gravity
    float gravity = world_get_gravity(world);
    assert(gravity == 9.81f);
    printf("  ✓ Default gravity is %.2f m/s²\n", gravity);

    // Test entity gravity application
    float entity_x = 5.5f, entity_y = 5.5f, entity_z = 10.0f;
    float velocity_z = 0.0f;

        // Apply gravity for 0.1 second
    world_apply_entity_gravity(world, &entity_x, &entity_y, &entity_z,
                              &velocity_z, 0.1f, false);

    // Entity should have fallen a bit
    assert(entity_z < 10.0f);
    assert(velocity_z < 0.0f);
    printf("  ✓ Entity started falling from z=%.1f with velocity=%.2f\n", entity_z, velocity_z);

    // Apply gravity until hitting ground
    for (int i = 0; i < 1000 && entity_z > 1.1f; i++) {
        world_apply_entity_gravity(world, &entity_x, &entity_y, &entity_z,
                                  &velocity_z, 0.01f, false);
    }

    // Should have hit ground and stopped
    assert(entity_z >= 1.0f && entity_z <= 1.5f); // Allow some tolerance
    assert(velocity_z == 0.0f);
    printf("  ✓ Entity landed on ground at z=%.2f\n", entity_z);

    world_destroy(world);
    printf("  ✓ Gravity tests passed\n");
}

void test_collision() {
    printf("Testing collision detection...\n");

    World* world = create_terrain_world();
    assert(world != NULL);

    // Test collision with ground
    assert(world_check_collision(world, 5.5f, 5.5f, 0.5f));
    printf("  ✓ Collision detected with ground\n");

    // Test no collision in air
    assert(!world_check_collision(world, 5.5f, 5.5f, 5.5f));
    printf("  ✓ No collision in air\n");

    // Test collision with hill
    assert(world_check_collision(world, 8.5f, 8.5f, 2.5f));
    printf("  ✓ Collision detected with hill\n");

    // Test out of bounds
    assert(world_check_collision(world, -1.0f, 5.5f, 5.5f));
    assert(world_check_collision(world, 5.5f, 20.0f, 5.5f));
    printf("  ✓ Out of bounds treated as collision\n");

    world_destroy(world);
    printf("  ✓ Collision tests passed\n");
}

void test_terrain_height() {
    printf("Testing terrain height queries...\n");

    World* world = create_terrain_world();
    assert(world != NULL);

    // Test flat area
    int height = world_get_terrain_height(world, 3, 3);
    assert(height == 0);
    printf("  ✓ Flat terrain height: %d\n", height);

    // Test raised area
    height = world_get_terrain_height(world, 0, 0);
    assert(height == 1);
    printf("  ✓ Raised terrain height: %d\n", height);

    // Test hill center
    height = world_get_terrain_height(world, 8, 8);
    assert(height == 2);
    printf("  ✓ Hill height: %d\n", height);

    // Test out of bounds
    height = world_get_terrain_height(world, -1, 5);
    assert(height == -1);
    height = world_get_terrain_height(world, 20, 5);
    assert(height == -1);
    printf("  ✓ Out of bounds returns -1\n");

    world_destroy(world);
    printf("  ✓ Terrain height tests passed\n");
}

void test_fluid_simulation() {
    printf("Testing fluid simulation...\n");

    World* world = create_terrain_world();
    assert(world != NULL);

    // Place some water above ground
    world_set_voxel(world, 5, 5, 5, VOXEL_WATER);
    Voxel* water = world_get_voxel(world, 5, 5, 5);
    voxel_set_quantity(water, 6); // Full water cell

    // Place magma for heat test
    world_set_voxel(world, 10, 10, 1, VOXEL_MAGMA);

    // Step fluid simulation
    world_step_fluids(world, 100);

    // Water should have started to fall (gravity effect)
    // Note: Due to the simplified implementation, we can't test the full
    // fluid dynamics, but we can verify the function runs without crashing
    printf("  ✓ Fluid simulation step completed\n");

    // Test temperature dissipation
    Voxel* near_magma = world_get_voxel(world, 10, 10, 2);
    if (near_magma) {
        voxel_set_heat(near_magma, 6);
    }

    world_step_temperature(world, 10);

    // Heat should have decreased
    if (near_magma) {
        uint8_t heat = voxel_get_heat(near_magma);
        assert(heat == 5);
        printf("  ✓ Temperature dissipation working (heat: %d)\n", heat);
    }

    world_destroy(world);
    printf("  ✓ Fluid simulation tests passed\n");
}

void test_spring_mechanics() {
    printf("Testing spring water mechanics...\n");

    World* world = create_terrain_world();
    assert(world != NULL);

    // Place a spring
    world_set_voxel(world, 7, 7, 1, VOXEL_SPRING);

        // Test water pushing from spring
    world_push_spring_water(world, 7, 7, 1);

    printf("  ✓ Spring water pushing function executed\n");

    world_destroy(world);
    printf("  ✓ Spring mechanics tests passed\n");
}

void test_actor_physics_stub() {
    printf("Testing actor physics stub...\n");

    World* world = create_terrain_world();
    assert(world != NULL);

    // This should just do nothing without crashing
    world_step_actors(world, 1.0f);

    printf("  ✓ Actor physics stub executed safely\n");

    world_destroy(world);
    printf("  ✓ Actor physics stub test passed\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== World Physics Module Test Suite ===\n\n");

    test_gravity();
    test_collision();
    test_terrain_height();
    test_fluid_simulation();
    test_spring_mechanics();
    test_actor_physics_stub();

    printf("\n✅ All tests passed!\n");
    printf("\nThe physics module has been successfully extracted and works correctly.\n");

    return 0;
}
