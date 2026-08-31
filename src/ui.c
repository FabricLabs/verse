#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"
#include "engine.h"
#include "actor.h"
#include "world.h"

// UI State Management
typedef struct {
    int current_screen;
    int dialog_active;
    int battle_mode;
    int navigation_mode;
    int building_mode;
    time_t battle_timer_start;
    int battle_time_limit;
    char current_dialog[1024];
    char player_name[64];
    int tutorial_complete;
    int first_quest_complete;
} UIState;

static UIState ui_state = {0};

// Screen definitions
#define SCREEN_MAIN_MENU 0
#define SCREEN_CHARACTER_CREATION 1
#define SCREEN_GAME_WORLD 2
#define SCREEN_BATTLE 3
#define SCREEN_NAVIGATION 4
#define SCREEN_DIALOG 5
#define SCREEN_BUILDING 6
#define SCREEN_GAME_OVER 7

// UI Functions
void ui_init() {
    memset(&ui_state, 0, sizeof(UIState));
    ui_state.current_screen = SCREEN_MAIN_MENU;
    ui_state.battle_time_limit = 600; // 10 minutes in seconds
    ui_state.tutorial_complete = 0;
    ui_state.first_quest_complete = 0;
}

void ui_show_main_menu() {
    printf("\n=== VERSE ===\n");
    printf("A robust rogue-like RPG\n\n");
    printf("1. New Game\n");
    printf("2. Load Game\n");
    printf("3. Settings\n");
    printf("4. Exit\n\n");
    printf("Enter your choice: ");
}

void ui_character_creation() {
    printf("\n=== Character Creation ===\n");
    printf("Enter your character's name: ");
    scanf("%63s", ui_state.player_name);
    printf("Welcome, %s!\n", ui_state.player_name);

    // Initialize player with default stats
    // Note: Player initialization will be handled by the engine
    printf("Character created successfully!\n");

    ui_state.current_screen = SCREEN_GAME_WORLD;
    printf("\nFading to narrative experience...\n");
    printf("Welcome to The Garden - your tutorial world.\n");
}

void ui_show_world() {
    printf("\n=== The Garden ===\n");
    printf("Player: %s\n", ui_state.player_name);
    printf("Location: Tutorial World\n");
    printf("Time: %ld seconds\n", time(NULL));

    if (!ui_state.tutorial_complete) {
        printf("\nQuest: Humble Beginnings (In Progress)\n");
        printf("Mobs will spawn at 5 minutes if quest not completed\n");
        printf("Game Over if mobs not cleared by 10 minutes\n");
    }

    printf("\nCommands:\n");
    printf("1. Move\n");
    printf("2. Interact\n");
    printf("3. Inventory\n");
    printf("4. Character\n");
    printf("5. Navigation\n");
    printf("6. Exit\n");
}

void ui_show_battle() {
    time_t current_time = time(NULL);
    int elapsed = current_time - ui_state.battle_timer_start;
    int remaining = ui_state.battle_time_limit - elapsed;

    printf("\n=== BATTLE MODE ===\n");
    printf("Time Remaining: %d:%02d\n", remaining / 60, remaining % 60);
    printf("Enemy Overview:\n");
    printf("- Enemy Type: [Generated]\n");
    printf("- HP: [Calculated]\n");
    printf("- Level: [Generated]\n");

    printf("\nBattle Options:\n");
    printf("1. Attack\n");
    printf("2. Special Ability\n");
    printf("3. Use Item\n");
    printf("4. Flee\n");

    if (remaining <= 0) {
        printf("\n*** BATTLE TIME EXPIRED ***\n");
        ui_state.current_screen = SCREEN_GAME_OVER;
    }
}

void ui_show_navigation() {
    printf("\n=== Navigation Screen ===\n");
    printf("Available Worlds:\n");
    printf("1. Home World (Unlocked)\n");
    printf("2. North World\n");
    printf("3. South World\n");
    printf("4. East World\n");
    printf("5. West World\n");
    printf("6. Up World\n");
    printf("7. Down World\n");
    printf("8. Return to Current World\n");
}

void ui_show_dialog(const char* message) {
    printf("\n=== DIALOG ===\n");
    printf("%s\n", message);
    printf("\n1. Continue\n");
    printf("2. Skip\n");
}

void ui_show_building() {
    printf("\n=== Building Mode ===\n");
    printf("Quest: Creating a Legacy\n");
    printf("Required: Build a statue\n");
    printf("Materials: Stone Block (1)\n");

    printf("\nBuilding Options:\n");
    printf("1. Place Stone Block\n");
    printf("2. View Blueprint\n");
    printf("3. Cancel\n");
}

void ui_show_game_over() {
    printf("\n=== GAME OVER ===\n");
    printf("Your journey has ended.\n");
    printf("Would you like to try again with the same character?\n");
    printf("1. Yes\n");
    printf("2. No\n");
}

void ui_process_input(int choice) {
    switch (ui_state.current_screen) {
        case SCREEN_MAIN_MENU:
            switch (choice) {
                case 1:
                    ui_state.current_screen = SCREEN_CHARACTER_CREATION;
                    ui_character_creation();
                    break;
                case 4:
                    exit(0);
                    break;
                default:
                    printf("Invalid choice.\n");
            }
            break;

        case SCREEN_GAME_WORLD:
            switch (choice) {
                case 1:
                    printf("Moving...\n");
                    break;
                case 2:
                    printf("Interacting...\n");
                    break;
                case 5:
                    ui_state.current_screen = SCREEN_NAVIGATION;
                    ui_show_navigation();
                    break;
                case 6:
                    ui_state.current_screen = SCREEN_MAIN_MENU;
                    ui_show_main_menu();
                    break;
                default:
                    printf("Invalid choice.\n");
            }
            break;

        case SCREEN_BATTLE:
            switch (choice) {
                case 1:
                    printf("Attacking...\n");
                    break;
                case 4:
                    printf("Fleeing from battle...\n");
                    ui_state.current_screen = SCREEN_GAME_WORLD;
                    ui_show_world();
                    break;
                default:
                    printf("Invalid choice.\n");
            }
            break;

        case SCREEN_NAVIGATION:
            if (choice >= 1 && choice <= 8) {
                printf("Navigating to world %d...\n", choice);
                ui_state.current_screen = SCREEN_GAME_WORLD;
                ui_show_world();
            } else {
                printf("Invalid choice.\n");
            }
            break;

        case SCREEN_BUILDING:
            switch (choice) {
                case 1:
                    printf("Placing stone block...\n");
                    break;
                case 3:
                    ui_state.current_screen = SCREEN_GAME_WORLD;
                    ui_show_world();
                    break;
                default:
                    printf("Invalid choice.\n");
            }
            break;

        case SCREEN_GAME_OVER:
            switch (choice) {
                case 1:
                    printf("Restarting with same character...\n");
                    ui_state.current_screen = SCREEN_GAME_WORLD;
                    ui_show_world();
                    break;
                case 2:
                    exit(0);
                    break;
                default:
                    printf("Invalid choice.\n");
            }
            break;
    }
}

void ui_start_battle() {
    ui_state.battle_mode = 1;
    ui_state.battle_timer_start = time(NULL);
    ui_state.current_screen = SCREEN_BATTLE;
    ui_show_battle();
}

void ui_complete_tutorial() {
    ui_state.tutorial_complete = 1;
    printf("\n*** Tutorial Complete! ***\n");
    printf("Quest 'Humble Beginnings' completed!\n");
    printf("You can now navigate to your Home World.\n");
}

void ui_complete_first_quest() {
    ui_state.first_quest_complete = 1;
    printf("\n*** First Quest Complete! ***\n");
    printf("You received a stone block!\n");
    printf("You can now build in your Home World.\n");
}

// Main UI loop
void ui_main_loop() {
    int choice;

    ui_init();
    ui_show_main_menu();

    while (1) {
        scanf("%d", &choice);
        ui_process_input(choice);

        // Show appropriate screen after processing
        switch (ui_state.current_screen) {
            case SCREEN_MAIN_MENU:
                ui_show_main_menu();
                break;
            case SCREEN_GAME_WORLD:
                ui_show_world();
                break;
            case SCREEN_BATTLE:
                ui_show_battle();
                break;
            case SCREEN_NAVIGATION:
                ui_show_navigation();
                break;
            case SCREEN_BUILDING:
                ui_show_building();
                break;
            case SCREEN_GAME_OVER:
                ui_show_game_over();
                break;
        }
    }
}
