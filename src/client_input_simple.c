/*
 * client_input_simple.c - Simplified input handling for the verse client
 *
 * This is a minimal version that compiles without missing functions.
 */

#include "client_input.h"
#include "client_state.h"
#include "client_audio.h"
#include "window.h"
#include "game_state.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

// Initialize input system
void client_input_init(void) {
    printf("Input system initialized (simple version)\n");
}

// Handle key press events
void handle_key_press(int key) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    printf("Key press: %d\n", key);

    // F4 toggles isometric / first-person; F is reserved for looting corpses in-game.
    if (key == SDLK_F4) {
        window_toggle_render_mode();
        return;
    }

    // Handle name input screen
    if (g_game_state->show_name_input) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            // Confirm name
            printf("Name confirmed: %s\n", g_game_state->player_name);
            g_game_state->show_name_input = false;
            g_game_state->current_screen = GAME_SCREEN_LOADING;
            return;
        } else if (key == SDLK_ESCAPE) {
            // Cancel name input
            g_game_state->show_name_input = false;
            return;
        }
        return;
    }

    // Handle exit prompt
    if (g_game_state->show_exit_prompt) {
        if (key == SDLK_y || key == SDLK_RETURN) {
            client_state_request_exit();
        } else if (key == SDLK_n || key == SDLK_ESCAPE) {
            g_game_state->show_exit_prompt = false;
        }
        return;
    }

    // General key handling
    switch (key) {
    case SDLK_ESCAPE:
        if (g_game_state->current_screen == GAME_SCREEN_WORLD) {
            g_game_state->current_screen = GAME_SCREEN_IN_GAME_MENU;
        } else if (g_game_state->current_screen == GAME_SCREEN_IN_GAME_MENU) {
            g_game_state->current_screen = GAME_SCREEN_WORLD;
        } else {
            g_game_state->show_exit_prompt = true;
        }
        break;

    case SDLK_SPACE:
        printf("Space pressed\n");
        if (g_game_state->current_screen == GAME_SCREEN_MAIN_MENU) {
            // Start new game
            g_game_state->show_name_input = true;
            memset(g_game_state->player_name, 0, sizeof(g_game_state->player_name));
        }
        break;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        printf("Enter pressed\n");
        if (g_game_state->current_screen == GAME_SCREEN_MAIN_MENU) {
            g_game_state->show_name_input = true;
            memset(g_game_state->player_name, 0, sizeof(g_game_state->player_name));
        }
        break;

    case SDLK_TAB:
        printf("Tab pressed\n");
        break;

    default:
        break;
    }
}

// Handle button click events
void handle_button_click(int button_id) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    printf("Button clicked: %d\n", button_id);
    play_button_sound(); // Play sound effect

    switch (button_id) {
    case BUTTON_NEW_GAME:
        if (!g_game_state->game_started) {
            g_game_state->show_name_input = true;
            memset(g_game_state->player_name, 0, sizeof(g_game_state->player_name));
        }
        break;

    case BUTTON_CONTINUE:
        printf("Continue game selected\n");
        if (g_game_state->game_started) {
            g_game_state->current_screen = GAME_SCREEN_WORLD;
        }
        break;

    case BUTTON_LOAD_GAME:
        printf("Load game selected\n");
        if (g_game_state->game_started) {
            g_game_state->current_screen = GAME_SCREEN_WORLD;
        }
        break;

    case BUTTON_SETTINGS:
        g_game_state->current_screen = GAME_SCREEN_SETTINGS;
        break;

    case BUTTON_EXIT:
        if (g_game_state->current_screen == GAME_SCREEN_MAIN_MENU) {
            g_game_state->show_exit_prompt = true;
        }
        break;

    case BUTTON_MAIN_MENU:
        g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
        break;

    case BUTTON_RESUME:
        printf("Resume game selected\n");
        g_game_state->current_screen = GAME_SCREEN_WORLD;
        break;

    case BUTTON_SAVE_GAME:
        printf("Save game selected\n");
        // game_state_save_game(g_game_state);
        break;

    case BUTTON_CANCEL_EXIT:
        if (g_game_state->show_name_input) {
            g_game_state->show_name_input = false;
        } else if (g_game_state->show_exit_prompt) {
            g_game_state->show_exit_prompt = false;
        }
        break;

    case BUTTON_CONFIRM_NAME:
        printf("Name confirmed via button\n");
        if (g_game_state->show_name_input && strlen(g_game_state->player_name) > 0) {
            g_game_state->show_name_input = false;
            printf("Name confirmed: %s\n", g_game_state->player_name);
            g_game_state->current_screen = GAME_SCREEN_LOADING;
        }
        break;

    case BUTTON_CONFIRM_EXIT:
        printf("Confirm exit selected\n");
        client_state_request_exit();
        break;

    default:
        printf("Unknown button: %d\n", button_id);
        break;
    }
}

// Handle mouse click events
void handle_mouse_click(int x, int y, int button) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    printf("Mouse click: (%d, %d) button %d\n", x, y, button);

    // Simple button handling - just call the button handler
    // In a real implementation, this would check which button was clicked
    if (button == SDL_BUTTON_LEFT) {
        // Left click - could be used for selection
        printf("Left click at (%d, %d)\n", x, y);
    } else if (button == SDL_BUTTON_RIGHT) {
        // Right click - could be used for context menu
        printf("Right click at (%d, %d)\n", x, y);
    }
}

// Handle mouse motion events
void handle_mouse_motion(int x, int y) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    // Simple mouse motion handling
    // In a real implementation, this would update hover states
    (void)x; // Unused
    (void)y; // Unused
}

// Handle mouse wheel events
void handle_mouse_wheel(int x, int y, int delta) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    printf("Mouse wheel: delta=%d at (%d, %d)\n", delta, x, y);

    // Simple zoom handling
    if (g_game_state->game_started && g_game_state->current_screen == GAME_SCREEN_WORLD) {
        if (delta > 0) {
            printf("Zoom in\n");
        } else if (delta < 0) {
            printf("Zoom out\n");
        }
    }
}

// Handle text input events
void handle_text_input(const char* text) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    if (g_game_state->show_name_input && text && text[0]) {
        // Append character to player name if there's room
        size_t len = strlen(g_game_state->player_name);
        if (len < sizeof(g_game_state->player_name) - 1) {
            g_game_state->player_name[len] = text[0];
            g_game_state->player_name[len + 1] = '\0';
        }
    }
}
