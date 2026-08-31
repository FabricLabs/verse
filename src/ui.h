#ifndef UI_H
#define UI_H

// UI Function Declarations
void ui_init();
void ui_show_main_menu();
void ui_character_creation();
void ui_show_world();
void ui_show_battle();
void ui_show_navigation();
void ui_show_dialog(const char* message);
void ui_show_building();
void ui_show_game_over();
void ui_process_input(int choice);
void ui_start_battle();
void ui_complete_tutorial();
void ui_complete_first_quest();
void ui_main_loop();

// UI Constants
#define BATTLE_TIME_LIMIT 600  // 10 minutes in seconds
#define TUTORIAL_MOB_SPAWN_TIME 300  // 5 minutes in seconds
#define TUTORIAL_GAME_OVER_TIME 600  // 10 minutes in seconds

// UI Screen States
#define SCREEN_MAIN_MENU 0
#define SCREEN_CHARACTER_CREATION 1
#define SCREEN_GAME_WORLD 2
#define SCREEN_BATTLE 3
#define SCREEN_NAVIGATION 4
#define SCREEN_DIALOG 5
#define SCREEN_BUILDING 6
#define SCREEN_GAME_OVER 7

#endif // UI_H
