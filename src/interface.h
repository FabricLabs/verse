#ifndef INTERFACE_H
#define INTERFACE_H

#include <time.h>
#include "engine.h"
#include "actor.h"
#include "world.h"

// GameState structure for UI interface
typedef struct {
    Engine* engine;
    Actor* player;
    World* current_world;
    int game_mode;
    int battle_mode;
    time_t game_start_time;
} GameState;

// Input modes
#define INPUT_MODE_NORMAL 0
#define INPUT_MODE_TEXT 1
#define INPUT_MODE_MENU 2
#define INPUT_MODE_BATTLE 3

// Battle actions
#define BATTLE_ACTION_ATTACK 1
#define BATTLE_ACTION_SPECIAL 2
#define BATTLE_ACTION_ITEM 3
#define BATTLE_ACTION_FLEE 4

// Interface Function Declarations
void interface_init();
int interface_get_key();
void interface_process_input(int key, GameState* game_state);
void interface_process_normal_input(int key, GameState* game_state);
void interface_process_text_input(int key, GameState* game_state);
void interface_process_menu_input(int key, GameState* game_state);
void interface_process_battle_input(int key, GameState* game_state);

// Interface Display Functions
void interface_show_inventory(GameState* game_state);
void interface_show_character(GameState* game_state);
void interface_show_map(GameState* game_state);
void interface_show_navigation(GameState* game_state);
void interface_show_building(GameState* game_state);
void interface_show_battle_inventory(GameState* game_state);
void interface_show_help();

// Interface Control Functions
void interface_exit_game(GameState* game_state);
void interface_cancel_action(GameState* game_state);
void interface_cancel_menu(GameState* game_state);
void interface_cancel_battle(GameState* game_state);
void interface_menu_select(GameState* game_state, int choice);
void interface_process_text_command(GameState* game_state, const char* command);

// Interface Mode Functions
void interface_start_text_input();
void interface_start_battle_mode();
void interface_start_menu_mode();
void interface_check_auto_save(GameState* game_state);

#endif // INTERFACE_H
