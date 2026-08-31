/*
 * client_input.c - Input handling implementation
 */

#include "client_input.h"
#include "client_state.h"
#include "client_audio.h"
#include "window.h"
#include "game_state.h"
#include <stdio.h>
#include <string.h>

// Static state
static bool g_should_exit = false;

// Initialize input system
void client_input_init(void) {
    g_should_exit = false;
}

// Check if we should exit
bool client_input_should_exit(void) {
    return g_should_exit || client_state_should_exit();
}

// Reset exit flag
void client_input_reset_exit(void) {
    g_should_exit = false;
}

// Handle keyboard input
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
            // Simulate clicking the Confirm button
            handle_button_click(BUTTON_CONFIRM_NAME);
        } else if (key == SDLK_ESCAPE) {
            // Cancel name input
            handle_button_click(BUTTON_CANCEL_EXIT);
        } else if (key == SDLK_BACKSPACE) {
            // Handle backspace
            int len = strlen(g_game_state->player_name);
            if (len > 0) {
                g_game_state->player_name[len - 1] = '\0';
                printf("Name after backspace: %s\n", g_game_state->player_name);
            }
        }
        return;
    }

    // Handle exit prompt
    if (g_game_state->show_exit_prompt) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_y) {
            // Confirm exit
            handle_button_click(BUTTON_CONFIRM_EXIT);
        } else if (key == SDLK_ESCAPE || key == SDLK_n) {
            // Cancel exit
            handle_button_click(BUTTON_CANCEL_EXIT);
        }
        return;
    }

    // General key handling
    switch (key) {
    case SDLK_ESCAPE:
        if (g_game_state->game_started) {
            // Open in-game menu
            if (g_game_state->current_screen == GAME_SCREEN_WORLD) {
                g_game_state->current_screen = GAME_SCREEN_IN_GAME_MENU;
                window_clear_selection();
            } else if (g_game_state->current_screen == GAME_SCREEN_IN_GAME_MENU) {
                g_game_state->current_screen = GAME_SCREEN_WORLD;
            }
        } else {
            // Main menu - show exit prompt
            if (g_game_state->current_screen == GAME_SCREEN_MAIN_MENU) {
                g_game_state->show_exit_prompt = true;
            }
        }
        break;

    case SDLK_SPACE:
        printf("Space pressed\n");
        // Handle space for various contexts
        if (g_game_state->show_chapter) {
            g_game_state->chapter_current_char = g_game_state->chapter_total_chars;
            g_game_state->chapter_text_complete = true;
        }
        break;

    case SDLK_LEFT:
    case SDLK_a:
        window_shift_selection_left();
        play_highlight_sound();
        break;

    case SDLK_RIGHT:
    case SDLK_d:
        window_shift_selection_right();
        play_highlight_sound();
        break;

    case SDLK_UP:
    case SDLK_w:
        window_shift_selection_up();
        play_highlight_sound();
        break;

    case SDLK_DOWN:
    case SDLK_s:
        window_shift_selection_down();
        play_highlight_sound();
        break;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        printf("Enter pressed\n");
        if (g_game_state->show_chapter) {
            if (g_game_state->chapter_current_char >= g_game_state->chapter_total_chars) {
                g_game_state->show_chapter = false;
                g_game_state->chapter_fade_alpha = 0.0f;
                g_game_state->current_screen = GAME_SCREEN_WORLD;
            } else {
                g_game_state->chapter_current_char = g_game_state->chapter_total_chars;
                g_game_state->chapter_text_complete = true;
            }
        } else if (g_game_state->current_screen == GAME_SCREEN_IN_GAME_MENU) {
            // Handle in-game menu selection
            int selected_option = window_get_selected_button();
            switch (selected_option) {
            case 0: // Resume Game
                g_game_state->current_screen = GAME_SCREEN_WORLD;
                break;
            case 1: // Save Game
                printf("Save game not implemented yet\n");
                break;
            case 2: // Settings
                g_game_state->current_screen = GAME_SCREEN_SETTINGS;
                break;
            case 3: // Main Menu
                g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
                break;
            case 4: // Exit Game
                client_state_request_exit();
                break;
            }
        } else {
            int selected_button = window_get_selected_button();
            if (selected_button > 0) {
                handle_button_click(selected_button);
            }
        }
        break;

    case SDLK_TAB:
        printf("Tab pressed\n");
        window_next_selection();
        break;
    }
}

// Handle button clicks
void handle_button_click(int button_id) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    printf("Button clicked: %d\n", button_id);
    play_button_sound(); // Play sound effect

    switch (button_id) {
    case 901: // Start/Stop runtime clock
        g_game_state->runtime_clock_running = !g_game_state->runtime_clock_running;
        snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                 g_game_state->runtime_clock_running ? "Clock: running" : "Clock: stopped");
        break;

    case BUTTON_NEW_GAME:
        if (!g_game_state->game_started) {
            printf("Starting new game - requesting player name...\n");
            // First step: ask for player name
            g_game_state->show_name_input = true;
            strcpy(g_game_state->player_name, ""); // Start with empty name
            window_start_text_input();             // Enable text input
        }
        break;

    case BUTTON_CONTINUE:
        printf("Continue game selected\n");
        if (game_state_load_most_recent_save(g_game_state)) {
            printf("Game loaded successfully - moving to Genesis screen\n");
            g_game_state->current_screen = GAME_SCREEN_CHAPTER;
            g_game_state->show_chapter = true;
            g_game_state->chapter_current_page = 0;
            g_game_state->chapter_current_char = 0;
            g_game_state->chapter_total_chars = 0;
            g_game_state->chapter_text_complete = false;
            g_game_state->chapter_fade_alpha = 0.0f;
            g_game_state->chapter_fade_start_time = SDL_GetTicks();
            g_game_state->chapter_last_text_update = SDL_GetTicks();
        } else {
            printf("Failed to load saved game\n");
        }
        break;

    case BUTTON_LOAD_GAME:
        printf("Load game selected\n");
        game_state_open_save_browser(g_game_state);
        break;

    case BUTTON_SETTINGS:
        g_game_state->current_screen = GAME_SCREEN_SETTINGS;
        window_sync_settings_with_audio(client_audio_get_music_system());
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
        game_state_save_game(g_game_state);
        break;

    case BUTTON_CANCEL_EXIT:
        if (g_game_state->show_name_input) {
            printf("Cancel name input selected\n");
            g_game_state->show_name_input = false;
            window_stop_text_input();
            g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
        } else {
            printf("Cancel exit selected\n");
            g_game_state->show_exit_prompt = false;
        }
        break;

    case BUTTON_CONFIRM_NAME:
        printf("Name confirmed via button\n");
        if (strlen(g_game_state->player_name) > 0) {
            g_game_state->show_name_input = false;
            window_stop_text_input();
            printf("Name confirmed: %s\n", g_game_state->player_name);

            // Proceed to loading screen
            g_game_state->current_screen = GAME_SCREEN_LOADING;
            g_game_state->show_loading = true;
            g_game_state->loading_progress = 0;
            g_game_state->loading_total = 100;
            strcpy(g_game_state->loading_message, "Preparing to generate worlds...");

            printf("Proceeding to loading screen...\n");
        } else {
            // Play error sound for empty name
            printf("Error: Cannot confirm empty name\n");
            play_error_sound();
        }
        break;

    case BUTTON_CONFIRM_EXIT:
        printf("Confirm exit selected\n");
        client_state_request_exit();
        break;
    }
}

// Handle mouse clicks
void handle_mouse_click(int x, int y, int button) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    // Handle world interaction
    if (g_game_state->game_started && g_game_state->current_screen == GAME_SCREEN_WORLD) {
        // Try floating-point coordinate conversion first
        float world_x_float, world_y_float, world_z_float;
        if (window_screen_to_world_coords_float(x, y, &world_x_float, &world_y_float, &world_z_float)) {
            // Debug: Show player position vs click target (floating-point)
            printf("CLICK: Player at (%.2f,%.2f,%.2f), clicked (%.2f,%.2f,%.2f)\n",
                   g_game_state->player_world_x, g_game_state->player_world_y, g_game_state->player_world_z,
                   world_x_float, world_y_float, world_z_float);

            if (button == SDL_BUTTON_RIGHT) {
                // RIGHT CLICK to move using floating-point coordinates
                // Clear any existing voxel info panel
                g_game_state->show_voxel_info = false;

                // Check if target is valid (non-air, in bounds)
                int voxel_x = (int)world_x_float;
                int voxel_y = (int)world_y_float;
                int voxel_z = (int)world_z_float;

                if (voxel_x >= 0 && voxel_y >= 0 && voxel_z >= 0 &&
                    voxel_x < (int)g_game_state->current_world->width &&
                    voxel_y < (int)g_game_state->current_world->height &&
                    voxel_z < (int)g_game_state->current_world->depth) {

                    Voxel *target = world_get_voxel(g_game_state->current_world, voxel_x, voxel_y, voxel_z);
                    if (target && target->type != VOXEL_AIR) {
                        // Set floating-point movement target
                        window_set_movement_target_float(world_x_float, world_y_float, world_z_float);
                        game_state_set_movement_target_float(g_game_state, world_x_float, world_y_float, world_z_float);

                        printf("Movement target set to (%.2f, %.2f, %.2f)\n",
                               world_x_float, world_y_float, world_z_float);

                        // Show click feedback
                        snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                                 "Moving to (%.0f, %.0f, %.0f)", world_x_float, world_y_float, world_z_float);
                    } else {
                        printf("Invalid target: air or out of bounds\n");
                        play_error_sound();
                    }
                }
            } else if (button == SDL_BUTTON_LEFT) {
                // LEFT CLICK for voxel info
                int voxel_x = (int)world_x_float;
                int voxel_y = (int)world_y_float;
                int voxel_z = (int)world_z_float;

                if (voxel_x >= 0 && voxel_y >= 0 && voxel_z >= 0 &&
                    voxel_x < (int)g_game_state->current_world->width &&
                    voxel_y < (int)g_game_state->current_world->height &&
                    voxel_z < (int)g_game_state->current_world->depth) {

                    Voxel *voxel = world_get_voxel(g_game_state->current_world, voxel_x, voxel_y, voxel_z);
                    if (voxel && voxel->type != VOXEL_AIR) {
                        // Show voxel info panel
                        g_game_state->show_voxel_info = true;
                        g_game_state->voxel_info_x = voxel_x;
                        g_game_state->voxel_info_y = voxel_y;
                        g_game_state->voxel_info_z = voxel_z;

                        // Play UI sound
                        play_select_sound();

                        printf("Showing info for voxel at (%d, %d, %d)\n",
                               voxel_x, voxel_y, voxel_z);
                    }
                }
            }
        }
    } else {
        // Handle UI clicks
        window_handle_mouse_click(x, y, button);
    }
}

// Handle mouse motion
void handle_mouse_motion(int x, int y) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    if (g_game_state->game_started && g_game_state->current_screen == GAME_SCREEN_WORLD) {
        // Update hover position for world
        float world_x, world_y, world_z;
        if (window_screen_to_world_coords_float(x, y, &world_x, &world_y, &world_z)) {
            window_set_hover_position((int)world_x, (int)world_y, (int)world_z);
        }
    } else {
        // Handle UI hover
        window_handle_mouse_motion(x, y);
    }
}

// Handle mouse wheel
void handle_mouse_wheel(int x, int y, int delta) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    (void)x; // Unused
    (void)y; // Unused

    if (g_game_state->game_started && g_game_state->current_screen == GAME_SCREEN_WORLD) {
        // Handle zoom with mouse wheel in world view
        window_handle_zoom(delta);
    }
}

// Handle text input
void handle_text_input(const char* text) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    if (g_game_state->show_name_input && text && text[0]) {
        // Append character to player name if there's room
        int len = strlen(g_game_state->player_name);
        if (len < sizeof(g_game_state->player_name) - 1) {
            // Only accept alphanumeric and some punctuation
            char c = text[0];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ') {
                g_game_state->player_name[len] = c;
                g_game_state->player_name[len + 1] = '\0';
                printf("Name updated: %s\n", g_game_state->player_name);
            }
        }
    }
}
