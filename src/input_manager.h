#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <SDL2/SDL.h>
#include <stdbool.h>

// Input states for different views
typedef enum {
    INPUT_STATE_MAIN_MENU,
    INPUT_STATE_GAME_WORLD,
    INPUT_STATE_WORLD_EDITOR,
    INPUT_STATE_SETTINGS,
    INPUT_STATE_NAME_INPUT,
    INPUT_STATE_EXIT_CONFIRMATION,
    INPUT_STATE_NEW_GAME_WARNING,
    INPUT_STATE_LOADING,
    INPUT_STATE_SCENE,
    INPUT_STATE_QUEST,
    INPUT_STATE_IN_GAME_MENU,
    INPUT_STATE_CHAPTER
} InputState;

// Input manager structure
typedef struct {
    InputState current_state;
    InputState previous_state;
    bool state_changed;

    // Selection state
    int selected_button_id;
    bool has_selection;

    // Navigation state
    bool can_navigate;
    int navigation_index;
    int max_navigation_items;

    // Modal state
    bool modal_active;
    int modal_button_count;

    // Input processing flags
    bool key_handled;
    bool mouse_handled;
} InputManager;

// Initialize the input manager
void input_manager_init(InputManager* manager);

// Set the current input state
void input_manager_set_state(InputManager* manager, InputState state);

// Get the current input state
InputState input_manager_get_state(InputManager* manager);

// Handle keyboard input for the current state
bool input_manager_handle_key(InputManager* manager, int key);

// Handle mouse input for the current state
bool input_manager_handle_mouse(InputManager* manager, int x, int y, int button);

// Handle button clicks for the current state
bool input_manager_handle_button_click(InputManager* manager, int button_id);

// Update navigation state
void input_manager_update_navigation(InputManager* manager, int max_items);

// Get the currently selected button
int input_manager_get_selected_button(InputManager* manager);

// Set the selected button
void input_manager_set_selected_button(InputManager* manager, int button_id);

// Clear selection
void input_manager_clear_selection(InputManager* manager);

// Check if input was handled
bool input_manager_was_handled(InputManager* manager);

// Reset input handling flags
void input_manager_reset_flags(InputManager* manager);

// State-specific input handlers
bool input_handle_main_menu(InputManager* manager, int key);
bool input_handle_game_world(InputManager* manager, int key);
bool input_handle_world_editor(InputManager* manager, int key);
bool input_handle_settings(InputManager* manager, int key);
bool input_handle_name_input(InputManager* manager, int key);
bool input_handle_exit_confirmation(InputManager* manager, int key);
bool input_handle_new_game_warning(InputManager* manager, int key);
bool input_handle_loading(InputManager* manager, int key);
bool input_handle_scene(InputManager* manager, int key);
bool input_handle_quest(InputManager* manager, int key);
bool input_handle_in_game_menu(InputManager* manager, int key);
bool input_handle_chapter(InputManager* manager, int key);

#endif // INPUT_MANAGER_H
