#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "world.h"
#include "mob_ai.h"

// External function declaration for extended actor stepping
extern void world_step_actors_extended(World *world, float dt_seconds);

// Find a suitable spawn position in the labyrinth (center clearing)
bool find_spawn_position(World* world, int* out_x, int* out_y, int* out_z) {
    if (!world || !out_x || !out_y || !out_z) return false;

    // Labyrinth generates with a 3x3 clearing in the center at z=1
    int center_x = world->width / 2;
    int center_y = world->height / 2;
    int spawn_z = 1; // Labyrinth floor is at z=1

    // Try positions in the center clearing
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = center_x + dx;
            int y = center_y + dy;

            // Check if position is valid and walkable
            if (x >= 0 && y >= 0 && x < (int)world->width && y < (int)world->height) {
                Voxel* v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)spawn_z);
                if (v && v->type == VOXEL_AIR) {
                    *out_x = x;
                    *out_y = y;
                    *out_z = spawn_z;
                    return true;
                }
            }
        }
    }

    return false;
}

// Print a simple top-down view of the labyrinth with the solver's position
void print_labyrinth_view(World* world, MobActor* solver) {
    if (!world || !solver) return;

    int solver_x = (int)solver->base.x;
    int solver_y = (int)solver->base.y;
    int z = 1; // Labyrinth floor

    printf("\n=== Labyrinth View (z=%d) ===\n", z);
    printf("Legend: # = Wall, . = Path, S = Solver, E = Exit (border)\n\n");

    // Print top border with column numbers
    printf("   ");
    for (int x = 0; x < (int)world->width && x < 40; x++) {
        printf("%d", x % 10);
    }
    printf("\n");

    // Print maze
    for (int y = (int)world->height - 1; y >= 0 && y >= (int)world->height - 40; y--) {
        printf("%2d ", y);
        for (int x = 0; x < (int)world->width && x < 40; x++) {
            if (x == solver_x && y == solver_y) {
                printf("S");
            } else if (mob_is_at_world_border(world, x, y, z)) {
                Voxel* v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                if (v && v->type == VOXEL_AIR) {
                    printf("E");
                } else {
                    printf("#");
                }
            } else {
                Voxel* v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                if (v) {
                    if (v->type == VOXEL_AIR) {
                        printf(".");
                    } else {
                        printf("#");
                    }
                } else {
                    printf("?");
                }
            }
        }
        printf(" %d\n", y);
    }

    printf("\nSolver position: (%d, %d, %d)\n", solver_x, solver_y, (int)solver->base.z);
}

int main(int argc, char* argv[]) {
    printf("=== VERSE Mob SOLVER Test ===\n");
    printf("Testing maze-solving AI in a labyrinth world\n\n");

    // Parse command line arguments
    const char* seed = "solver_test";
    int max_steps = 10000;
    float step_time = 0.1f; // 100ms per step
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = argv[++i];
        } else if (strcmp(argv[i], "--max-steps") == 0 && i + 1 < argc) {
            max_steps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--step-time") == 0 && i + 1 < argc) {
            step_time = atof(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --seed <seed>        World generation seed (default: solver_test)\n");
            printf("  --max-steps <n>      Maximum simulation steps (default: 10000)\n");
            printf("  --step-time <t>      Time per simulation step in seconds (default: 0.1)\n");
            printf("  --verbose, -v        Print detailed progress\n");
            printf("  --help, -h           Show this help message\n");
            return 0;
        }
    }

    // Create a labyrinth world
    printf("Creating labyrinth world with seed: %s\n", seed);
    World* world = world_create(32, 32, 32); // 32x32x32 world
    if (!world) {
        fprintf(stderr, "Failed to create world\n");
        return 1;
    }

    // Generate labyrinth
    world_generate_with_type(world, seed, WORLD_TYPE_LABYRINTH_SQUARE);
    printf("Generated %dx%dx%d labyrinth world\n", world->width, world->height, world->depth);

    // Find spawn position
    int spawn_x, spawn_y, spawn_z;
    if (!find_spawn_position(world, &spawn_x, &spawn_y, &spawn_z)) {
        fprintf(stderr, "Failed to find spawn position\n");
        world_destroy(world);
        return 1;
    }

    printf("Spawn position found at: (%d, %d, %d)\n", spawn_x, spawn_y, spawn_z);

    // Create SOLVER mob
    MobActor* solver = mob_actor_create("Maze Solver", MOB_TYPE_SOLVER,
                                        spawn_x + 0.5, spawn_y + 0.5, spawn_z + 0.5);
    if (!solver) {
        fprintf(stderr, "Failed to create solver mob\n");
        world_destroy(world);
        return 1;
    }

    // Add solver to world's runtime actors
    Actor* actors = (Actor*)calloc(1, sizeof(Actor));
    if (!actors) {
        fprintf(stderr, "Failed to allocate actors array\n");
        mob_actor_destroy(solver);
        world_destroy(world);
        return 1;
    }

    // Copy base actor data and link to mob
    memcpy(&actors[0], &solver->base, sizeof(Actor));
    actors[0].extra_data = solver;

    world->runtime_actors = actors;
    world->runtime_actor_count = 1;
    world->runtime_actor_capacity = 1;

    // Print initial state
    if (verbose) {
        print_labyrinth_view(world, solver);
    }

    // Run simulation
    printf("\nStarting simulation (max %d steps, %.2f seconds per step)\n", max_steps, step_time);
    printf("Solver will use right-hand wall following to find an exit...\n\n");

    clock_t start_time = clock();
    int steps = 0;
    bool found_exit = false;

    while (steps < max_steps && solver->base.is_active) {
        // Step the simulation
        world_step_actors_extended(world, step_time);
        steps++;

        // Check if solver found exit
        if (mob_solver_is_at_exit(solver, world)) {
            found_exit = true;
            solver->base.is_active = false;
        }

        // Print progress
        if (verbose || steps % 100 == 0) {
            printf("Step %d: Solver at (%.1f, %.1f, %.1f)",
                   steps, solver->base.x, solver->base.y, solver->base.z);
            if (solver->ai_state.stuck_counter > 0) {
                printf(" [Stuck: %d]", solver->ai_state.stuck_counter);
            }
            printf("\n");

            if (verbose && steps % 10 == 0) {
                print_labyrinth_view(world, solver);
                usleep(100000); // 100ms delay for visualization
            }
        }
    }

    clock_t end_time = clock();
    double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    // Print results
    printf("\n=== Simulation Complete ===\n");
    printf("Steps taken: %d\n", steps);
    printf("Real time elapsed: %.2f seconds\n", elapsed);
    printf("Simulated time: %.2f seconds\n", steps * step_time);

    if (found_exit) {
        printf("SUCCESS: Solver found the exit at position (%.1f, %.1f, %.1f)!\n",
               solver->base.x, solver->base.y, solver->base.z);
    } else if (steps >= max_steps) {
        printf("TIMEOUT: Solver did not find exit within %d steps\n", max_steps);
    } else {
        printf("STOPPED: Solver became inactive\n");
    }

    // Cleanup
    world->runtime_actors = NULL; // Don't free, we manage it separately
    world->runtime_actor_count = 0;
    world->runtime_actor_capacity = 0;

    free(actors);
    mob_actor_destroy(solver);
    world_destroy(world);

    return found_exit ? 0 : 1;
}
