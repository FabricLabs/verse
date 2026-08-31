/*
 * test_mob_solver_minimal.c - Minimal standalone test for SOLVER mob concept
 *
 * This test demonstrates the maze-solving algorithm without any dependencies
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

// Simple maze solver actor
typedef struct {
    double x, y, z;
    int last_dir;
    bool is_active;
    int stuck_counter;
} SimpleSolver;

// Simple world structure
typedef struct {
    int width, height;
    char* grid;
} SimpleWorld;

// Create world
SimpleWorld* create_world(int width, int height) {
    SimpleWorld* world = (SimpleWorld*)malloc(sizeof(SimpleWorld));
    world->width = width;
    world->height = height;
    world->grid = (char*)calloc(width * height, sizeof(char));
    return world;
}

// Destroy world
void destroy_world(SimpleWorld* world) {
    if (world) {
        free(world->grid);
        free(world);
    }
}

// Get cell
char get_cell(SimpleWorld* world, int x, int y) {
    if (x < 0 || y < 0 || x >= world->width || y >= world->height) {
        return '#';
    }
    return world->grid[y * world->width + x];
}

// Set cell
void set_cell(SimpleWorld* world, int x, int y, char value) {
    if (x >= 0 && y >= 0 && x < world->width && y < world->height) {
        world->grid[y * world->width + x] = value;
    }
}

// Create simple labyrinth
void create_labyrinth(SimpleWorld* world) {
    // Fill with walls
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            set_cell(world, x, y, '#');
        }
    }

    // Create paths (simple pattern)
    int mid_x = world->width / 2;
    int mid_y = world->height / 2;

    // Horizontal corridor
    for (int x = 1; x < world->width - 1; x++) {
        set_cell(world, x, mid_y, '.');
    }

    // Vertical corridor
    for (int y = 1; y < world->height - 1; y++) {
        set_cell(world, mid_x, y, '.');
    }

    // Create exit at east border
    set_cell(world, world->width - 1, mid_y, 'E');

    // Clear spawn area
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = mid_x + dx;
            int y = mid_y + dy;
            if (x > 0 && y > 0 && x < world->width - 1 && y < world->height - 1) {
                set_cell(world, x, y, '.');
            }
        }
    }
}

// Print world
void print_world(SimpleWorld* world, SimpleSolver* solver) {
    printf("\n");
    for (int y = world->height - 1; y >= 0; y--) {
        for (int x = 0; x < world->width; x++) {
            if (x == (int)solver->x && y == (int)solver->y) {
                printf("S");
            } else {
                printf("%c", get_cell(world, x, y));
            }
        }
        printf("\n");
    }
    printf("Solver at: (%d, %d)\n", (int)solver->x, (int)solver->y);
}

// Check if position is walkable
bool can_move_to(SimpleWorld* world, int x, int y) {
    char cell = get_cell(world, x, y);
    return cell == '.' || cell == 'E';
}

// Check if at exit
bool is_at_exit(SimpleWorld* world, int x, int y) {
    return get_cell(world, x, y) == 'E';
}

// Update solver - prefer moving toward exit direction when in open areas
void update_solver(SimpleSolver* solver, SimpleWorld* world) {
    int x = (int)solver->x;
    int y = (int)solver->y;

    // Check if at exit
    if (is_at_exit(world, x, y)) {
        printf("Found exit!\n");
        solver->is_active = false;
        return;
    }

    // Directions: East=0, North=1, West=2, South=3
    int dx[] = {1, 0, -1, 0};
    int dy[] = {0, 1, 0, -1};
    const char* dir_names[] = {"East", "North", "West", "South"};

    // Count open neighbors
    int open_count = 0;
    for (int i = 0; i < 4; i++) {
        if (can_move_to(world, x + dx[i], y + dy[i])) {
            open_count++;
        }
    }

    // If in a corridor (2 or fewer open neighbors), use wall following
    if (open_count <= 2) {
        // Right-hand rule: try right, forward, left, back
        int dir_order[] = {
            (solver->last_dir + 3) % 4,  // Right
            solver->last_dir,            // Forward
            (solver->last_dir + 1) % 4,  // Left
            (solver->last_dir + 2) % 4   // Back
        };

        for (int i = 0; i < 4; i++) {
            int dir = dir_order[i];
            int new_x = x + dx[dir];
            int new_y = y + dy[dir];

            if (can_move_to(world, new_x, new_y)) {
                solver->x = new_x;
                solver->y = new_y;
                solver->last_dir = dir;
                solver->stuck_counter = 0;
                printf("Wall following: Moving %s to (%d, %d)\n", dir_names[dir], new_x, new_y);
                return;
            }
        }
    } else {
        // In open area - prioritize unexplored directions
        // Try: East (toward exit), then others
        int priority_dirs[] = {0, 1, 3, 2}; // East, North, South, West

        for (int i = 0; i < 4; i++) {
            int dir = priority_dirs[i];
            int new_x = x + dx[dir];
            int new_y = y + dy[dir];

            if (can_move_to(world, new_x, new_y)) {
                solver->x = new_x;
                solver->y = new_y;
                solver->last_dir = dir;
                solver->stuck_counter = 0;
                printf("Open area: Moving %s to (%d, %d)\n", dir_names[dir], new_x, new_y);
                return;
            }
        }
    }

    solver->stuck_counter++;
    printf("Stuck for %d steps\n", solver->stuck_counter);
}

int main() {
    printf("=== Minimal SOLVER Test ===\n");
    printf("Testing right-hand wall following algorithm\n\n");

    // Create world
    SimpleWorld* world = create_world(20, 20);
    create_labyrinth(world);

    // Create solver
    SimpleSolver solver = {
        .x = world->width / 2,
        .y = world->height / 2,
        .z = 0,
        .last_dir = 0,  // Start facing east
        .is_active = true,
        .stuck_counter = 0
    };

    printf("Initial state:\n");
    print_world(world, &solver);

    // Run simulation
    int max_steps = 200;
    int steps = 0;

    printf("\nStarting simulation...\n");
    while (steps < max_steps && solver.is_active) {
        update_solver(&solver, world);
        steps++;

        // Show progress every 10 steps
        if (steps % 10 == 0 || !solver.is_active) {
            printf("\nStep %d:\n", steps);
            print_world(world, &solver);
            usleep(200000);  // 200ms delay
        }
    }

    // Results
    printf("\n=== Results ===\n");
    printf("Steps taken: %d\n", steps);
    if (!solver.is_active && is_at_exit(world, (int)solver.x, (int)solver.y)) {
        printf("SUCCESS: Solver found the exit!\n");
    } else {
        printf("FAILED: Solver did not find the exit\n");
    }

    destroy_world(world);
    return 0;
}
