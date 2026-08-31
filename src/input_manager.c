#include "input_manager.h"
#include "window.h"
#include <stdio.h>
#include <string.h>

// External function declarations
extern void handle_button_click(int button_id);

// Initialize the input manager
void input_manager_init(InputManager* manager) {
    if (!manager) return;

    manager->current_state = INPUT_STATE_MAIN_MENU;
    manager->previous_state = INPUT_STATE_MAIN_MENU;
    manager->state_changed = false;

    manager->selected_button_id = 0;
    manager->has_selection = false;

    manager->can_navigate = true;
    manager->navigation_index = 0;
    manager->max_navigation_items = 0;

    manager->modal_active = false;
    manager->modal_button_count = 0;

    manager->key_handled = false;
    manager->mouse_handled = false;
}

// Set the current input state
void input_manager_set_state(InputManager* manager, InputState state) {
    if (!manager) return;

    manager->previous_state = manager->current_state;
    manager->current_state = state;
    manager->state_changed = (manager->previous_state != state);

    // Reset selection when state changes
    if (manager->state_changed) {
        input_manager_clear_selection(manager);
    }
}

// Get the current input state
InputState input_manager_get_state(InputManager* manager) {
    return manager ? manager->current_state : INPUT_STATE_MAIN_MENU;
}

// Handle keyboard input for the current state
bool input_manager_handle_key(InputManager* manager, int key) {
    if (!manager) return false;

    manager->key_handled = false;

    switch (manager->current_state) {
        case INPUT_STATE_MAIN_MENU:
            manager->key_handled = input_handle_main_menu(manager, key);
            break;
        case INPUT_STATE_GAME_WORLD:
            manager->key_handled = input_handle_game_world(manager, key);
            break;
        case INPUT_STATE_WORLD_EDITOR:
            manager->key_handled = input_handle_world_editor(manager, key);
            break;
        case INPUT_STATE_SETTINGS:
            manager->key_handled = input_handle_settings(manager, key);
            break;
        case INPUT_STATE_NAME_INPUT:
            manager->key_handled = input_handle_name_input(manager, key);
            break;
        case INPUT_STATE_EXIT_CONFIRMATION:
            manager->key_handled = input_handle_exit_confirmation(manager, key);
            break;
        case INPUT_STATE_NEW_GAME_WARNING:
            manager->key_handled = input_handle_new_game_warning(manager, key);
            break;
        case INPUT_STATE_LOADING:
            manager->key_handled = input_handle_loading(manager, key);
            break;
        case INPUT_STATE_SCENE:
            manager->key_handled = input_handle_scene(manager, key);
            break;
        case INPUT_STATE_QUEST:
            manager->key_handled = input_handle_quest(manager, key);
            break;
        case INPUT_STATE_IN_GAME_MENU:
            manager->key_handled = input_handle_in_game_menu(manager, key);
            break;
        case INPUT_STATE_CHAPTER:
            manager->key_handled = input_handle_chapter(manager, key);
            break;
    }

    return manager->key_handled;
}

// Handle mouse input for the current state
bool input_manager_handle_mouse(InputManager* manager, int x, int y, int button) {
    if (!manager) return false;

    manager->mouse_handled = false;

    // Handle battle arena mouse controls in game world state
    if (manager->current_state == INPUT_STATE_GAME_WORLD) {
        // Forward mouse input to the isometric renderer for battle arena controls
        // This will be handled by the main application that has access to the renderer
        manager->mouse_handled = true; // Mark as handled to pass to main app
        return true;
    }

    // Handle mouse clicks for menu states
    if (button == SDL_BUTTON_LEFT) {
        // Let the window system handle button clicks
        int clicked_button = window_handle_button_click(x, y);
        if (clicked_button != 0) {
            manager->mouse_handled = input_manager_handle_button_click(manager, clicked_button);
        }
    }

    return manager->mouse_handled;
}

// Handle button clicks for the current state
bool input_manager_handle_button_click(InputManager* manager, int button_id) {
    if (!manager) return false;

    // Update selection
    manager->selected_button_id = button_id;
    manager->has_selection = true;

    // The main application will handle the actual button logic
    return true;
}

// Update navigation state
void input_manager_update_navigation(InputManager* manager, int max_items) {
    if (!manager) return;

    manager->max_navigation_items = max_items;
    manager->can_navigate = (max_items > 0);
}

// Get the currently selected button
int input_manager_get_selected_button(InputManager* manager) {
    return manager ? manager->selected_button_id : 0;
}

// Set the selected button
void input_manager_set_selected_button(InputManager* manager, int button_id) {
    if (!manager) return;

    manager->selected_button_id = button_id;
    manager->has_selection = (button_id != 0);
}

// Clear selection
void input_manager_clear_selection(InputManager* manager) {
    if (!manager) return;

    manager->selected_button_id = 0;
    manager->has_selection = false;
    manager->navigation_index = 0;
}

// Check if input was handled
bool input_manager_was_handled(InputManager* manager) {
    return manager && (manager->key_handled || manager->mouse_handled);
}

// Reset input handling flags
void input_manager_reset_flags(InputManager* manager) {
    if (!manager) return;

    manager->key_handled = false;
    manager->mouse_handled = false;
}

// State-specific input handlers - simplified versions

bool input_handle_main_menu(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_UP:
            window_prev_selection();
            return true;
        case SDLK_DOWN:
            window_next_selection();
            return true;
        case SDLK_TAB:
            window_next_selection();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            // Activate the currently selected button
            int selected_button = window_get_selected_button();
            if (selected_button != 0) {
                // Call the button callback directly
                handle_button_click(selected_button);
            }
            return true;
        }
        case SDLK_ESCAPE:
            return true; // Let main app handle
    }
    return false;
}

bool input_handle_game_world(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_ESCAPE:
            return true; // Let main app handle
        case SDLK_w:
        case SDLK_s:
        case SDLK_a:
        case SDLK_d:
        case SDLK_SPACE:
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return false; // Let existing handler deal with movement
    }
    return false;
}

bool input_handle_world_editor(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_i:
            // Actor insertion - let main app handle
            return false;
        case SDLK_ESCAPE:
            return true; // Let main app handle
        case SDLK_w:
        case SDLK_s:
        case SDLK_a:
        case SDLK_d:
        case SDLK_SPACE:
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return false; // Let existing handler deal with movement
        case SDLK_m:
            // Mouse look toggle - let main app handle
            return false;
        case SDLK_F1:
            // Actor selection - let main app handle
            return false;
        case SDLK_PERIOD:
        case SDLK_GREATER:
        case SDLK_COMMA:
        case SDLK_LESS:
            // Time controls - let main app handle
            return false;
        case SDLK_PLUS:
        case SDLK_EQUALS:
        case SDLK_MINUS:
            // Zoom controls - let main app handle
            return false;
        default:
            return false; // Let all other keys pass through to main app
    }
}

bool input_handle_settings(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_UP:
            window_settings_prev_selection();
            return true;
        case SDLK_DOWN:
            window_settings_next_selection();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return true; // Let main app handle
        case SDLK_ESCAPE:
            return true; // Let main app handle
    }
    return false;
}

bool input_handle_name_input(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_ESCAPE:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_BACKSPACE:
            return false; // Let main app handle these keys
        default:
            // Handle character input
            if (key >= 32 && key <= 126) {
                return false; // Let main app handle character input
            }
            return false;
    }
}

bool input_handle_exit_confirmation(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_LEFT:
        case SDLK_RIGHT:
            window_next_selection();
            return true;
        case SDLK_TAB:
            window_next_selection();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            // Activate the currently selected button
            int selected_button = window_get_selected_button();
            if (selected_button != 0) {
                handle_button_click(selected_button);
            }
            return true;
        }
        case SDLK_ESCAPE:
            return true; // Let main app handle
    }
    return false;
}

bool input_handle_new_game_warning(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_LEFT:
        case SDLK_RIGHT:
            window_next_selection();
            return true;
        case SDLK_TAB:
            window_next_selection();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            // Activate the currently selected button
            int selected_button = window_get_selected_button();
            if (selected_button != 0) {
                handle_button_click(selected_button);
            }
            return true;
        }
        case SDLK_ESCAPE:
            return true; // Let main app handle
    }
    return false;
}

bool input_handle_loading(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    (void)key; // Suppress unused parameter warning
    // No input during loading
    return false;
}

bool input_handle_scene(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
        case SDLK_ESCAPE:
            return true; // Let main app handle
    }
    return false;
}

bool input_handle_quest(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_ESCAPE:
            return true; // Let main app handle
    }
    return false;
}

bool input_handle_in_game_menu(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_UP:
            window_prev_selection();
            return true;
        case SDLK_DOWN:
            window_next_selection();
            return true;
        case SDLK_TAB:
            window_next_selection();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            // Activate the currently selected button
            int selected_button = window_get_selected_button();
            if (selected_button != 0) {
                handle_button_click(selected_button);
            }
            return true;
        }
        case SDLK_ESCAPE:
            return true; // Let main app handle
    }
    return false;
}

bool input_handle_chapter(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
        case SDLK_ESCAPE:
            return true; // Let main app handle
    }
    return false;
}
