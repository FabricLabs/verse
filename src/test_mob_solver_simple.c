/*
 * test_mob_solver_simple.c - Simplified test for SOLVER mob type
 *
 * This is a minimal test that creates a simple hardcoded labyrinth
 * and tests the SOLVER mob's ability to find the exit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include "mob_ai.h"

// Simple world structure for testing
typedef struct {
    int width;
    int height;
    int depth;
    char* grid;  // 3D array stored as 1D
} SimpleWorld;

// Create a simple world
SimpleWorld* simple_world_create(int width, int height, int depth) {
    SimpleWorld* world = (SimpleWorld*)malloc(sizeof(SimpleWorld));
    if (!world) return NULL;

    world->width = width;
    world->height = height;
    world->depth = depth;
    world->grid = (char*)calloc(width * height * depth, sizeof(char));

    if (!world->grid) {
        free(world);
        return NULL;
    }

    return world;
}

// Destroy a simple world
void simple_world_destroy(SimpleWorld* world) {
    if (world) {
        free(world->grid);
        free(world);
    }
}

// Get voxel at position
char simple_world_get(SimpleWorld* world, int x, int y, int z) {
    if (!world || x < 0 || y < 0 || z < 0 ||
        x >= world->width || y >= world->height || z >= world->depth) {
        return '#';  // Out of bounds = wall
    }
    return world->grid[z * world->width * world->height + y * world->width + x];
}

// Set voxel at position
void simple_world_set(SimpleWorld* world, int x, int y, int z, char value) {
    if (!world || x < 0 || y < 0 || z < 0 ||
        x >= world->width || y >= world->height || z >= world->depth) {
        return;
    }
    world->grid[z * world->width * world->height + y * world->width + x] = value;
}

// Create a simple labyrinth pattern
void create_simple_labyrinth(SimpleWorld* world) {
    // Fill with walls
    for (int z = 0; z < world->depth; z++) {
        for (int y = 0; y < world->height; y++) {
            for (int x = 0; x < world->width; x++) {
                simple_world_set(world, x, y, z, '#');
            }
        }
    }

    // Create floor at z=0
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            simple_world_set(world, x, y, 0, '#');
        }
    }

    // Create paths at z=1 (simple cross pattern with exit at border)
    int z = 1;

    // Horizontal corridor
    int mid_y = world->height / 2;
    for (int x = 1; x < world->width - 1; x++) {
        simple_world_set(world, x, mid_y, z, '.');
    }

    // Vertical corridor
    int mid_x = world->width / 2;
    for (int y = 1; y < world->height - 1; y++) {
        simple_world_set(world, mid_x, y, z, '.');
    }

    // Create exit at east border
    simple_world_set(world, world->width - 1, mid_y, z, '.');

    // Clear spawn area (center)
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = mid_x + dx;
            int y = mid_y + dy;
            if (x >= 0 && y >= 0 && x < world->width && y < world->height) {
                simple_world_set(world, x, y, z, '.');
            }
        }
    }
}

// Print world view
void print_world(SimpleWorld* world, MobActor* solver) {
    int z = 1;  // Maze level
    int solver_x = (int)solver->base.x;
    int solver_y = (int)solver->base.y;

    printf("\n=== World View (z=%d) ===\n", z);
    printf("# = Wall, . = Path, S = Solver, E = Exit\n\n");

    for (int y = world->height - 1; y >= 0; y--) {
        for (int x = 0; x < world->width; x++) {
            if (x == solver_x && y == solver_y) {
                printf("S");
            } else if ((x == 0 || x == world->width - 1 ||
                       y == 0 || y == world->height - 1) &&
                       simple_world_get(world, x, y, z) == '.') {
                printf("E");
            } else {
                printf("%c", simple_world_get(world, x, y, z));
            }
        }
        printf("\n");
    }
    printf("\nSolver at: (%.1f, %.1f, %.1f)\n",
           solver->base.x, solver->base.y, solver->base.z);
}

// Simplified mob movement for testing
bool simple_can_move_to(SimpleWorld* world, int x, int y, int z) {
    return simple_world_get(world, x, y, z) == '.';
}

bool simple_is_at_border(SimpleWorld* world, int x, int y) {
    return x == 0 || x == world->width - 1 ||
           y == 0 || y == world->height - 1;
}

// Update solver position
void update_solver(MobActor* solver, SimpleWorld* world, float dt) {
    int current_x = (int)solver->base.x;
    int current_y = (int)solver->base.y;
    int current_z = (int)solver->base.z;

    // Check if at exit
    if (simple_is_at_border(world, current_x, current_y)) {
        printf("SOLVER: Found exit at (%d, %d)!\n", current_x, current_y);
        solver->base.is_active = false;
        return;
    }

    // Simple movement: try each direction
    int dx[] = {1, 0, -1, 0};
    int dy[] = {0, 1, 0, -1};
    const char* dir_names[] = {"East", "North", "West", "South"};

    // Try to continue in same direction first (right-hand rule)
    static int last_dir = 0;

    for (int i = 0; i < 4; i++) {
        int dir = (last_dir + i) % 4;
        int new_x = current_x + dx[dir];
        int new_y = current_y + dy[dir];

        if (simple_can_move_to(world, new_x, new_y, current_z)) {
            solver->base.x = new_x + 0.5;
            solver->base.y = new_y + 0.5;
            last_dir = dir;
            printf("SOLVER: Moving %s to (%d, %d)\n",
                   dir_names[dir], new_x, new_y);
            return;
        }
    }

    printf("SOLVER: Stuck at (%d, %d)\n", current_x, current_y);
}

int main(int argc, char* argv[]) {
    printf("=== Simple SOLVER Mob Test ===\n");
    printf("Testing maze-solving behavior\n\n");

    // Create a simple world
    int world_size = 15;
    SimpleWorld* world = simple_world_create(world_size, world_size, 3);
    if (!world) {
        fprintf(stderr, "Failed to create world\n");
        return 1;
    }

    // Create labyrinth
    create_simple_labyrinth(world);
    printf("Created %dx%dx%d labyrinth\n", world->width, world->height, world->depth);

    // Create solver at center
    int spawn_x = world->width / 2;
    int spawn_y = world->height / 2;
    int spawn_z = 1;

    MobActor* solver = mob_actor_create("SimpleSolver", MOB_TYPE_SOLVER,
                                        spawn_x + 0.5, spawn_y + 0.5, spawn_z + 0.5);
    if (!solver) {
        fprintf(stderr, "Failed to create solver\n");
        simple_world_destroy(world);
        return 1;
    }

    printf("Spawned solver at (%.1f, %.1f, %.1f)\n",
           solver->base.x, solver->base.y, solver->base.z);

    // Print initial state
    print_world(world, solver);

    // Run simulation
    printf("\nStarting simulation...\n");
    int max_steps = 100;
    int steps = 0;
    bool found_exit = false;
    float dt = 0.1f;

    while (steps < max_steps && solver->base.is_active) {
        update_solver(solver, world, dt);
        steps++;

        if (!solver->base.is_active) {
            found_exit = true;
        }

        // Print state every 5 steps
        if (steps % 5 == 0 || !solver->base.is_active) {
            printf("\nStep %d:\n", steps);
            print_world(world, solver);
            usleep(500000);  // 500ms delay for visualization
        }
    }

    // Print results
    printf("\n=== Results ===\n");
    printf("Steps taken: %d\n", steps);
    if (found_exit) {
        printf("SUCCESS: Solver found the exit!\n");
    } else if (steps >= max_steps) {
        printf("TIMEOUT: Solver did not find exit within %d steps\n", max_steps);
    } else {
        printf("STOPPED: Solver became inactive\n");
    }

    // Cleanup
    mob_actor_destroy(solver);
    simple_world_destroy(world);

    return found_exit ? 0 : 1;
}
