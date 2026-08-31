#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "renderer.h"
#include "world.h"
#include "engine.h"

// Renderer state
typedef struct {
    int width;
    int height;
    int view_distance;
    int show_coordinates;
    int show_debug_info;
} RendererState;

static RendererState renderer_state = {0};

void renderer_init() {
    renderer_state.width = 80;
    renderer_state.height = 24;
    renderer_state.view_distance = 10;
    renderer_state.show_coordinates = 1;
    renderer_state.show_debug_info = 0;
}

void renderer_clear_screen() {
    printf("\033[2J\033[H"); // Clear screen and move cursor to top
}

void renderer_draw_world(World* world, int player_x, int player_y, int player_z) {
    renderer_clear_screen();

    printf("=== VERSE World Renderer ===\n");
    printf("Player Position: (%d, %d, %d)\n", player_x, player_y, player_z);
    printf("World Size: %ux%ux%u\n", world->width, world->height, world->depth);
    printf("\n");

    // Draw a simple 2D slice of the world around the player
    int start_x = player_x - renderer_state.view_distance;
    int end_x = player_x + renderer_state.view_distance;
    int start_y = player_y - renderer_state.view_distance;
    int end_y = player_y + renderer_state.view_distance;

    // Clamp to world boundaries
    if (start_x < 0) start_x = 0;
    if (end_x >= (int)world->width) end_x = world->width - 1;
    if (start_y < 0) start_y = 0;
    if (end_y >= (int)world->height) end_y = world->height - 1;

    printf("World View (Z=%d):\n", player_z);
    printf("  ");
    for (int x = start_x; x <= end_x; x++) {
        printf("%2d", x % 100);
    }
    printf("\n");

    for (int y = start_y; y <= end_y; y++) {
        printf("%2d ", y % 100);
        for (int x = start_x; x <= end_x; x++) {
            if (x == player_x && y == player_y) {
                printf(" @"); // Player
            } else {
                // Get voxel at position
                Voxel* voxel = world_get_voxel(world, x, y, player_z);
                if (voxel) {
                    switch (voxel->type) {
                        case VOXEL_AIR:
                            printf("  ");
                            break;
                        case VOXEL_GRASS:
                            printf(" .");
                            break;
                        case VOXEL_STONE:
                            printf(" #");
                            break;
                        case VOXEL_WATER:
                            printf(" ~");
                            break;
                        case VOXEL_WOOD:
                            printf(" T");
                            break;
                        case VOXEL_LEAVES:
                            printf(" L");
                            break;
                        case VOXEL_SAND:
                            printf(" s");
                            break;
                        case VOXEL_SOIL:
                            printf(" d");
                            break;
                        case VOXEL_WORLD:
                            printf(" *");
                            break;
                        default:
                            printf(" ?");
                            break;
                    }
                } else {
                    printf("  ");
                }
            }
        }
        printf("\n");
    }
    printf("\n");
}

void renderer_draw_actor_info(Actor* actor) {
    printf("=== Actor Information ===\n");
    printf("Name: %s\n", actor->name);
    printf("Description: %s\n", actor->description);
    printf("Position: (%.2f, %.2f, %.2f)\n", actor->x, actor->y, actor->z);
    printf("Health: %u\n", actor->health);
    printf("Level: %u\n", actor->level);
    printf("Experience: %u\n", actor->experience);
    printf("\n");
}

void renderer_draw_inventory(Actor* actor) {
    printf("=== Inventory ===\n");
    printf("Inventory Size: %u\n", actor->inventory_size);
    printf("Items:\n");
    printf("  (Inventory system to be implemented)\n");
    printf("\n");
}

void renderer_draw_character_sheet(Actor* actor) {
    printf("=== Character Sheet ===\n");
    printf("Name: %s\n", actor->name);
    printf("Description: %s\n", actor->description);
    printf("Level: %u\n", actor->level);
    printf("Experience: %u\n", actor->experience);

    printf("\nAttributes:\n");
    printf("  Strength:     %u\n", actor->strength);
    printf("  Dexterity:    %u\n", actor->dexterity);
    printf("  Intelligence: %u\n", actor->intelligence);
    printf("  Wisdom:       %u\n", actor->wisdom);
    printf("  Constitution: %u\n", actor->constitution);
    printf("  Charisma:     %u\n", actor->charisma);
    printf("  Luck:         %u\n", actor->luck);
    printf("  Turn Speed:   %u deg/s\n", actor->turn_speed);

    printf("\nCombat Stats:\n");
    printf("  Health:       %u\n", actor->health);
    printf("  Stamina:      %.0f\n", actor->stamina);
    printf("\n");
}

void renderer_draw_battle_interface(Actor* player, Actor* enemy) {
    printf("=== BATTLE ===\n");
    printf("Player: %s (HP: %u)\n", player->name, player->health);
    printf("Enemy:  %s (HP: %u)\n", enemy->name, enemy->health);
    printf("\n");

    // Draw health bars (simplified for now)
    printf("Player: [");
    int player_bars = (player->health * 20) / 100; // Assume max health of 100
    for (int i = 0; i < 20; i++) {
        if (i < player_bars) {
            printf("=");
        } else {
            printf(" ");
        }
    }
    printf("]\n");

    printf("Enemy:  [");
    int enemy_bars = (enemy->health * 20) / 100; // Assume max health of 100
    for (int i = 0; i < 20; i++) {
        if (i < enemy_bars) {
            printf("=");
        } else {
            printf(" ");
        }
    }
    printf("]\n");
    printf("\n");
}

void renderer_draw_navigation_map(World* current_world, World** connected_worlds, int num_connected) {
    printf("=== Navigation Map ===\n");
    printf("Current World Size: %ux%ux%u\n", current_world->width, current_world->height, current_world->depth);
    printf("\nConnected Worlds:\n");

    for (int i = 0; i < num_connected; i++) {
        if (connected_worlds[i]) {
            printf("  %d. World %d (%ux%ux%u)\n", i + 1, i + 1,
                   connected_worlds[i]->width, connected_worlds[i]->height, connected_worlds[i]->depth);
        }
    }
    printf("\n");
}

void renderer_draw_building_interface(World* world, int x, int y, int z) {
    printf("=== Building Mode ===\n");
    printf("Position: (%d, %d, %d)\n", x, y, z);
    printf("World Size: %ux%ux%u\n", world->width, world->height, world->depth);
    printf("\nAvailable Materials:\n");
    printf("  1. Stone Block\n");
    printf("  2. Wood Plank\n");
    printf("  3. Metal Bar\n");
    printf("  4. Cancel\n");
    printf("\n");
}

void renderer_set_debug_mode(int enabled) {
    renderer_state.show_debug_info = enabled;
}

void renderer_set_view_distance(int distance) {
    renderer_state.view_distance = distance;
}

void renderer_show_coordinates(int show) {
    renderer_state.show_coordinates = show;
}
