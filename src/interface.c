#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "interface.h"
#include "ui.h"
#include "renderer.h"
#include "engine.h"
#include "world.h"
#include "actor.h"

// Interface state
typedef struct {
    int input_mode;
    int last_key;
    char input_buffer[256];
    int buffer_pos;
    int auto_save_interval;
    time_t last_save;
} InterfaceState;

static InterfaceState interface_state = {0};

void interface_init() {
    memset(&interface_state, 0, sizeof(InterfaceState));
    interface_state.input_mode = INPUT_MODE_NORMAL;
    interface_state.auto_save_interval = 300; // 5 minutes
    interface_state.last_save = time(NULL);
}

int interface_get_key() {
    int key = getchar();
    interface_state.last_key = key;
    return key;
}

void interface_process_input(int key, GameState* game_state) {
    switch (interface_state.input_mode) {
        case INPUT_MODE_NORMAL:
            interface_process_normal_input(key, game_state);
            break;
        case INPUT_MODE_TEXT:
            interface_process_text_input(key, game_state);
            break;
        case INPUT_MODE_MENU:
            interface_process_menu_input(key, game_state);
            break;
        case INPUT_MODE_BATTLE:
            interface_process_battle_input(key, game_state);
            break;
    }
}

void interface_process_normal_input(int key, GameState* game_state) {
    switch (key) {
        case 'w':
        case 'W':
            // engine_move_player(game_state, 0, -1, 0);
            printf("Moving north...\n");
            break;
        case 's':
        case 'S':
            // engine_move_player(game_state, 0, 1, 0);
            printf("Moving south...\n");
            break;
        case 'a':
        case 'A':
            // engine_move_player(game_state, -1, 0, 0);
            printf("Moving west...\n");
            break;
        case 'd':
        case 'D':
            // engine_move_player(game_state, 1, 0, 0);
            printf("Moving east...\n");
            break;
        case 'q':
        case 'Q':
            // engine_move_player(game_state, 0, 0, -1);
            printf("Moving up...\n");
            break;
        case 'e':
        case 'E':
            // engine_move_player(game_state, 0, 0, 1);
            printf("Moving down...\n");
            break;
        case 'i':
        case 'I':
            interface_show_inventory(game_state);
            break;
        case 'c':
        case 'C':
            interface_show_character(game_state);
            break;
        case 'm':
        case 'M':
            interface_show_map(game_state);
            break;
        case 'n':
        case 'N':
            interface_show_navigation(game_state);
            break;
        case 'b':
        case 'B':
            interface_show_building(game_state);
            break;
        case 'h':
        case 'H':
            interface_show_help();
            break;
        case 'x':
        case 'X':
            interface_exit_game(game_state);
            break;
        case ' ':
            // engine_interact_at_position(game_state);
            printf("Interacting...\n");
            break;
        case '\n':
        case '\r':
            // engine_confirm_action(game_state);
            printf("Action confirmed.\n");
            break;
        case 27: // ESC
            interface_cancel_action(game_state);
            break;
    }
}

void interface_process_text_input(int key, GameState* game_state) {
    if (key == '\n' || key == '\r') {
        // Finish text input
        interface_state.input_mode = INPUT_MODE_NORMAL;
        interface_state.input_buffer[interface_state.buffer_pos] = '\0';
        interface_process_text_command(game_state, interface_state.input_buffer);
        interface_state.buffer_pos = 0;
        memset(interface_state.input_buffer, 0, sizeof(interface_state.input_buffer));
    } else if (key == 8 || key == 127) { // Backspace
        if (interface_state.buffer_pos > 0) {
            interface_state.buffer_pos--;
            interface_state.input_buffer[interface_state.buffer_pos] = '\0';
        }
    } else if (key == 27) { // ESC
        // Cancel text input
        interface_state.input_mode = INPUT_MODE_NORMAL;
        interface_state.buffer_pos = 0;
        memset(interface_state.input_buffer, 0, sizeof(interface_state.input_buffer));
    } else if (isprint(key) && interface_state.buffer_pos < 255) {
        interface_state.input_buffer[interface_state.buffer_pos++] = key;
    }
}

void interface_process_menu_input(int key, GameState* game_state) {
    switch (key) {
        case '1':
            interface_menu_select(game_state, 1);
            break;
        case '2':
            interface_menu_select(game_state, 2);
            break;
        case '3':
            interface_menu_select(game_state, 3);
            break;
        case '4':
            interface_menu_select(game_state, 4);
            break;
        case '5':
            interface_menu_select(game_state, 5);
            break;
        case '6':
            interface_menu_select(game_state, 6);
            break;
        case '7':
            interface_menu_select(game_state, 7);
            break;
        case '8':
            interface_menu_select(game_state, 8);
            break;
        case '9':
            interface_menu_select(game_state, 9);
            break;
        case '0':
            interface_menu_select(game_state, 0);
            break;
        case 27: // ESC
            interface_cancel_menu(game_state);
            break;
    }
}

void interface_process_battle_input(int key, GameState* game_state) {
    switch (key) {
        case '1':
            // engine_battle_action(game_state, BATTLE_ACTION_ATTACK);
            printf("Attacking...\n");
            break;
        case '2':
            // engine_battle_action(game_state, BATTLE_ACTION_SPECIAL);
            printf("Using special ability...\n");
            break;
        case '3':
            // engine_battle_action(game_state, BATTLE_ACTION_ITEM);
            printf("Using item...\n");
            break;
        case '4':
            // engine_battle_action(game_state, BATTLE_ACTION_FLEE);
            printf("Fleeing...\n");
            break;
        case 'i':
        case 'I':
            interface_show_battle_inventory(game_state);
            break;
        case 27: // ESC
            interface_cancel_battle(game_state);
            break;
    }
}

void interface_show_inventory(GameState* game_state) {
    // Get the player actor from the engine
    Actor* player = engine_get_actor(game_state->engine, "player");
    if (player) {
        renderer_draw_inventory(player);
    } else {
        printf("Player not found.\n");
    }
    interface_state.input_mode = INPUT_MODE_MENU;
}

void interface_show_character(GameState* game_state) {
    // Get the player actor from the engine
    Actor* player = engine_get_actor(game_state->engine, "player");
    if (player) {
        renderer_draw_character_sheet(player);
    } else {
        printf("Player not found.\n");
    }
    interface_state.input_mode = INPUT_MODE_MENU;
}

void interface_show_map(GameState* game_state) {
    if (game_state->current_world && game_state->player) {
        renderer_draw_world(game_state->current_world,
                           (int)game_state->player->x,
                           (int)game_state->player->y,
                           (int)game_state->player->z);
    } else {
        printf("No world or player data available.\n");
    }
    interface_state.input_mode = INPUT_MODE_NORMAL;
}

void interface_show_navigation(GameState* game_state) {
    // Get connected worlds (stub implementation)
    World** connected = NULL;
    int num_connected = 0;

    if (game_state->current_world) {
        renderer_draw_navigation_map(game_state->current_world, connected, num_connected);
    } else {
        printf("No current world available.\n");
    }
    interface_state.input_mode = INPUT_MODE_MENU;
}

void interface_show_building(GameState* game_state) {
    if (game_state->current_world && game_state->player) {
        renderer_draw_building_interface(game_state->current_world,
                                       (int)game_state->player->x,
                                       (int)game_state->player->y,
                                       (int)game_state->player->z);
    } else {
        printf("No world or player data available.\n");
    }
    interface_state.input_mode = INPUT_MODE_MENU;
}

void interface_show_battle_inventory(GameState* game_state) {
    renderer_draw_inventory(game_state->player);
    interface_state.input_mode = INPUT_MODE_BATTLE;
}

void interface_show_help() {
    printf("\n=== VERSE Help ===\n");
    printf("Movement:\n");
    printf("  W/A/S/D - Move up/left/down/right\n");
    printf("  Q/E - Move up/down in Z-axis\n");
    printf("  Space - Interact\n");
    printf("  Enter - Confirm action\n");
    printf("  ESC - Cancel/Cancel menu\n");
    printf("\nInterface:\n");
    printf("  I - Inventory\n");
    printf("  C - Character sheet\n");
    printf("  M - Map view\n");
    printf("  N - Navigation\n");
    printf("  B - Building mode\n");
    printf("  H - Help (this screen)\n");
    printf("  X - Exit game\n");
    printf("\nBattle:\n");
    printf("  1-4 - Battle actions\n");
    printf("  I - Battle inventory\n");
    printf("\nPress any key to continue...\n");
    getchar();
}

void interface_exit_game(GameState* game_state) {
    printf("\nSaving game...\n");
    // engine_save_game(game_state);
    printf("Game saved. Goodbye!\n");
    exit(0);
}

void interface_cancel_action(GameState* game_state) {
    // Return to normal input mode
    interface_state.input_mode = INPUT_MODE_NORMAL;
    printf("Action cancelled.\n");
}

void interface_cancel_menu(GameState* game_state) {
    interface_state.input_mode = INPUT_MODE_NORMAL;
    printf("Menu cancelled.\n");
}

void interface_cancel_battle(GameState* game_state) {
    printf("Cannot cancel battle!\n");
}

void interface_menu_select(GameState* game_state, int choice) {
    // Handle menu selection based on current context
    // This would be implemented based on the current menu state
    printf("Selected option %d\n", choice);
    interface_state.input_mode = INPUT_MODE_NORMAL;
}

void interface_process_text_command(GameState* game_state, const char* command) {
    // Process text commands like "/help", "/save", etc.
    if (strncmp(command, "/help", 5) == 0) {
        interface_show_help();
    } else if (strncmp(command, "/save", 5) == 0) {
        // engine_save_game(game_state);
        printf("Game saved.\n");
    } else if (strncmp(command, "/quit", 5) == 0) {
        interface_exit_game(game_state);
    } else {
        printf("Unknown command: %s\n", command);
    }
}

void interface_start_text_input() {
    interface_state.input_mode = INPUT_MODE_TEXT;
    interface_state.buffer_pos = 0;
    memset(interface_state.input_buffer, 0, sizeof(interface_state.input_buffer));
    printf("Enter command: ");
}

void interface_start_battle_mode() {
    interface_state.input_mode = INPUT_MODE_BATTLE;
}

void interface_start_menu_mode() {
    interface_state.input_mode = INPUT_MODE_MENU;
}

void interface_check_auto_save(GameState* game_state) {
    time_t current_time = time(NULL);
    if (current_time - interface_state.last_save >= interface_state.auto_save_interval) {
        // engine_save_game(game_state);
        interface_state.last_save = current_time;
        printf("Auto-save completed.\n");
    }
}
