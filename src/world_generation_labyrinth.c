/*
 * world_generation_labyrinth.c - LABYRINTH world generator
 *
 * Creates a maze-like world with bedrock walls and pathways.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "world_internal.h"
#include "world_generation.h"
#include "world_voxel.h"
#include "voxel.h"

// Simple pathfinding structure for maze generation
typedef struct {
    int x, y;
} PathNode;

// Check if a position is valid and passable
static bool is_passable(World* world, int x, int y, int z) {
    if (x < 0 || x >= world->width || y < 0 || y >= world->height || z < 0 || z >= world->depth)
        return false;

    Voxel* v = world_get_voxel(world, x, y, z);
    return (v && v->type == VOXEL_AIR);
}

// Simple breadth-first pathfinding to ensure connectivity
static bool find_path(World* world, int z, int start_x, int start_y, int end_x, int end_y,
                     PathNode** out_path, int* out_length) {
    const int max_nodes = world->width * world->height;
    bool* visited = calloc(max_nodes, sizeof(bool));
    PathNode* queue = calloc(max_nodes, sizeof(PathNode));
    int* parent_x = calloc(max_nodes, sizeof(int));
    int* parent_y = calloc(max_nodes, sizeof(int));

    int queue_start = 0, queue_end = 0;

    // Initialize parent arrays
    for (int i = 0; i < max_nodes; i++) {
        parent_x[i] = -1;
        parent_y[i] = -1;
    }

    // Add start node
    queue[queue_end].x = start_x;
    queue[queue_end].y = start_y;
    queue_end++;
    visited[start_y * world->width + start_x] = true;

    // BFS
    bool found = false;
    while (queue_start < queue_end && !found) {
        PathNode current = queue[queue_start++];

        // Check if we reached the end
        if (current.x == end_x && current.y == end_y) {
            found = true;
            break;
        }

        // Check 4 directions
        int dx[] = {0, 1, 0, -1};
        int dy[] = {-1, 0, 1, 0};

        for (int i = 0; i < 4; i++) {
            int nx = current.x + dx[i];
            int ny = current.y + dy[i];
            int idx = ny * world->width + nx;

            if (nx >= 0 && nx < world->width && ny >= 0 && ny < world->height &&
                !visited[idx] && is_passable(world, nx, ny, z)) {
                visited[idx] = true;
                parent_x[idx] = current.x;
                parent_y[idx] = current.y;
                queue[queue_end].x = nx;
                queue[queue_end].y = ny;
                queue_end++;
            }
        }
    }

    // Reconstruct path if found
    if (found) {
        // Count path length
        int len = 0;
        int x = end_x, y = end_y;
        while (x != start_x || y != start_y) {
            len++;
            int idx = y * world->width + x;
            int px = parent_x[idx];
            int py = parent_y[idx];
            x = px;
            y = py;
        }
        len++; // Include start

        // Build path
        *out_path = calloc(len, sizeof(PathNode));
        *out_length = len;

        x = end_x;
        y = end_y;
        int i = len - 1;
        while (i >= 0) {
            (*out_path)[i].x = x;
            (*out_path)[i].y = y;
            if (x == start_x && y == start_y) break;
            int idx = y * world->width + x;
            int px = parent_x[idx];
            int py = parent_y[idx];
            x = px;
            y = py;
            i--;
        }
    }

    free(visited);
    free(queue);
    free(parent_x);
    free(parent_y);

    return found;
}

// Generate LABYRINTH world - a square maze
void world_generate_labyrinth(World* world, const char* seed) {
    if (!world || !world->voxels || world->depth <= 1)
        return;

    printf("[DEBUG] world_generate_labyrinth called for world %p, seed=%s\n",
           (void*)world, seed ? seed : "NULL");

    seed_rand_with_world_seed(seed);

    const uint32_t W = world->width;
    const uint32_t H = world->height;
    const uint32_t z = 1; // Maze at z=1

    // Clear world first
    for (uint32_t cz = 0; cz < world->depth; cz++) {
        for (uint32_t cy = 0; cy < world->height; cy++) {
            for (uint32_t cx = 0; cx < world->width; cx++) {
                if (cz == 0) {
                    world_set_voxel(world, cx, cy, cz, VOXEL_BEDROCK);
                } else {
                    world_set_voxel(world, cx, cy, cz, VOXEL_AIR);
                }
            }
        }
    }

    // Maze generation parameters
    uint32_t cx = W / 2, cy = H / 2; // Center
    uint32_t xmin = 1, ymin = 1;
    uint32_t xmax = (W > 2 ? W - 2 : 0);
    uint32_t ymax = (H > 2 ? H - 2 : 0);

    if (xmax <= xmin || ymax <= ymin)
        return;

    // Place outer walls
    for (uint32_t x = xmin; x <= xmax; x++) {
        world_set_voxel(world, x, ymin, z, VOXEL_BEDROCK);
        world_set_voxel(world, x, ymax, z, VOXEL_BEDROCK);
    }
    for (uint32_t y = ymin; y <= ymax; y++) {
        world_set_voxel(world, xmin, y, z, VOXEL_BEDROCK);
        world_set_voxel(world, xmax, y, z, VOXEL_BEDROCK);
    }

    // Create 4 entrances (one on each side)
    uint32_t entrances[4][2];
    entrances[0][0] = xmin; entrances[0][1] = cy; // West
    entrances[1][0] = xmax; entrances[1][1] = cy; // East
    entrances[2][0] = cx; entrances[2][1] = ymin; // South
    entrances[3][0] = cx; entrances[3][1] = ymax; // North

    for (int i = 0; i < 4; i++) {
        world_set_voxel(world, entrances[i][0], entrances[i][1], z, VOXEL_AIR);
    }

    // Create maze using simple grid pattern
    // This creates a checkerboard pattern with corridors
    for (uint32_t y = ymin + 1; y < ymax; y++) {
        for (uint32_t x = xmin + 1; x < xmax; x++) {
            // Keep center area clear (3x3)
            if (x >= cx - 1 && x <= cx + 1 && y >= cy - 1 && y <= cy + 1)
                continue;

            // Create grid pattern
            if (((x - xmin) % 2 == 0) || ((y - ymin) % 2 == 0)) {
                world_set_voxel(world, x, y, z, VOXEL_BEDROCK);
            }
        }
    }

    // Carve random connections between cells to create a maze
    for (uint32_t y = ymin + 2; y + 2 <= ymax; y += 2) {
        for (uint32_t x = xmin + 2; x + 2 <= xmax; x += 2) {
            // Randomly choose a direction to carve
            int dir = seeded_rand_range(4);
            uint32_t gx = x, gy = y;

            switch (dir) {
                case 0: // East
                    if (x + 1 <= xmax) gx = x + 1;
                    break;
                case 1: // West
                    if (x > xmin + 1) gx = x - 1;
                    break;
                case 2: // North
                    if (y + 1 <= ymax) gy = y + 1;
                    break;
                case 3: // South
                    if (y > ymin + 1) gy = y - 1;
                    break;
            }

            world_set_voxel(world, gx, gy, z, VOXEL_AIR);
        }
    }

    // Ensure center clearing
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int xx = cx + dx;
            int yy = cy + dy;
            if (xx >= 0 && xx < W && yy >= 0 && yy < H) {
                world_set_voxel(world, xx, yy, z, VOXEL_AIR);
            }
        }
    }

    // Mark a path from center to nearest entrance with sand
    PathNode* path = NULL;
    int path_len = 0;

    // Find nearest entrance
    int best_entrance = 0;
    uint32_t best_dist = UINT32_MAX;
    for (int i = 0; i < 4; i++) {
        int dx = entrances[i][0] - cx;
        int dy = entrances[i][1] - cy;
        uint32_t dist = dx * dx + dy * dy;
        if (dist < best_dist) {
            best_dist = dist;
            best_entrance = i;
        }
    }

    // Find path from center to entrance
    if (find_path(world, z, cx, cy,
                  entrances[best_entrance][0], entrances[best_entrance][1],
                  &path, &path_len)) {
        // Mark path with sand
        for (int i = 0; i < path_len; i++) {
            world_set_voxel(world, path[i].x, path[i].y, z, VOXEL_SAND);
        }
        free(path);
    }

    printf("[DEBUG] Labyrinth generation complete\n");
}
