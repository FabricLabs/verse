#ifndef WORLD_EDITOR_INPUT_H
#define WORLD_EDITOR_INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// INPUT SYSTEM TYPES
// ============================================================================

// Key state tracking
typedef struct
{
  bool pressed;
  bool just_pressed;   // True only on the frame the key was first pressed
  bool just_released;  // True only on the frame the key was released
  uint32_t press_time; // When the key was pressed (SDL_GetTicks())
} WorldEditorKeyState;

// Modifier key states
typedef struct
{
  bool ctrl;
  bool shift;
  bool alt;
  bool super; // Command/Windows key
} WorldEditorModifierState;

// Mouse state
typedef struct
{
  int x, y;
  int delta_x, delta_y;
  bool left_pressed;
  bool right_pressed;
  bool middle_pressed;
  int wheel_x, wheel_y;
} WorldEditorMouseState;

// Input context - determines what type of input is being handled
typedef enum
{
  INPUT_CONTEXT_GAME,  // Game controls (movement, actions, tools)
  INPUT_CONTEXT_UI,    // UI interaction (button clicks, navigation)
  INPUT_CONTEXT_TEXT,  // Text input (chat, commands)
  INPUT_CONTEXT_EDITOR // Editor-specific controls
} WorldEditorInputContext;

// Input system for World Editor
typedef struct
{
  // Key states for all SDL keys
  WorldEditorKeyState keys[SDL_NUM_SCANCODES];

  // Modifier states
  WorldEditorModifierState modifiers;

  // Mouse state
  WorldEditorMouseState mouse;

  // Current input context
  WorldEditorInputContext current_context;
  WorldEditorInputContext previous_context;

  // Callbacks for game actions
  void (*on_actor_insert)(void *user_data);
  void (*on_actor_select)(int actor_id, void *user_data);
  void (*on_camera_move)(int dx, int dy, int dz, void *user_data);
  void (*on_camera_rotate)(int direction, void *user_data); // -1: CCW, +1: CW
  void (*on_camera_reset)(void *user_data);
  void (*on_tool_change)(int tool_id, void *user_data);
  void (*on_voxel_place)(int x, int y, int z, int voxel_type, void *user_data);
  void (*on_voxel_remove)(int x, int y, int z, void *user_data);
  void (*on_mouse_look_toggle)(bool enabled, void *user_data);
  void (*on_time_control)(int direction, void *user_data); // -1: slower, +1: faster
  void (*on_zoom_control)(float factor, void *user_data);
  void (*on_renderer_toggle)(int toggle_type, void *user_data); // 0: greedy, 1: baseline
  void (*on_chat_toggle)(void *user_data);
  void (*on_clock_toggle)(void *user_data);

  // User data for callbacks
  void *user_data;

  // Configuration
  bool enable_mouse_look;
  float mouse_sensitivity;
  bool capture_mouse;
} WorldEditorInputSystem;

// ============================================================================
// INPUT SYSTEM FUNCTIONS
// ============================================================================

// Create and destroy input system
WorldEditorInputSystem *world_editor_input_create(void);
void world_editor_input_destroy(WorldEditorInputSystem *input);

// Update input system (call once per frame)
void world_editor_input_update(WorldEditorInputSystem *input);

// Handle SDL events
bool world_editor_input_handle_event(WorldEditorInputSystem *input, SDL_Event *event);

// Key state queries
bool world_editor_input_is_key_pressed(WorldEditorInputSystem *input, SDL_Scancode scancode);
bool world_editor_input_is_key_just_pressed(WorldEditorInputSystem *input, SDL_Scancode scancode);
bool world_editor_input_is_key_just_released(WorldEditorInputSystem *input, SDL_Scancode scancode);
bool world_editor_input_is_key_held(WorldEditorInputSystem *input, SDL_Scancode scancode, uint32_t hold_time_ms);

// Modifier queries
bool world_editor_input_is_ctrl_pressed(WorldEditorInputSystem *input);
bool world_editor_input_is_shift_pressed(WorldEditorInputSystem *input);
bool world_editor_input_is_alt_pressed(WorldEditorInputSystem *input);
bool world_editor_input_is_super_pressed(WorldEditorInputSystem *input);

// Mouse queries
void world_editor_input_get_mouse_position(WorldEditorInputSystem *input, int *x, int *y);
void world_editor_input_get_mouse_delta(WorldEditorInputSystem *input, int *dx, int *dy);
bool world_editor_input_is_mouse_button_pressed(WorldEditorInputSystem *input, int button);

// Context management
WorldEditorInputContext world_editor_input_get_context(WorldEditorInputSystem *input);
void world_editor_input_set_context(WorldEditorInputSystem *input, WorldEditorInputContext context);
bool world_editor_input_context_changed(WorldEditorInputSystem *input);

// Callback setters
void world_editor_input_set_actor_insert_callback(WorldEditorInputSystem *input, void (*callback)(void *user_data), void *user_data);
void world_editor_input_set_actor_select_callback(WorldEditorInputSystem *input, void (*callback)(int actor_id, void *user_data), void *user_data);
void world_editor_input_set_camera_move_callback(WorldEditorInputSystem *input, void (*callback)(int dx, int dy, int dz, void *user_data), void *user_data);
void world_editor_input_set_camera_rotate_callback(WorldEditorInputSystem *input, void (*callback)(int direction, void *user_data), void *user_data);
void world_editor_input_set_camera_reset_callback(WorldEditorInputSystem *input, void (*callback)(void *user_data), void *user_data);
void world_editor_input_set_tool_change_callback(WorldEditorInputSystem *input, void (*callback)(int tool_id, void *user_data), void *user_data);
void world_editor_input_set_voxel_place_callback(WorldEditorInputSystem *input, void (*callback)(int x, int y, int z, int voxel_type, void *user_data), void *user_data);
void world_editor_input_set_voxel_remove_callback(WorldEditorInputSystem *input, void (*callback)(int x, int y, int z, void *user_data), void *user_data);
void world_editor_input_set_mouse_look_toggle_callback(WorldEditorInputSystem *input, void (*callback)(bool enabled, void *user_data), void *user_data);
void world_editor_input_set_time_control_callback(WorldEditorInputSystem *input, void (*callback)(int direction, void *user_data), void *user_data);
void world_editor_input_set_zoom_control_callback(WorldEditorInputSystem *input, void (*callback)(float factor, void *user_data), void *user_data);
void world_editor_input_set_renderer_toggle_callback(WorldEditorInputSystem *input, void (*callback)(int toggle_type, void *user_data), void *user_data);
void world_editor_input_set_chat_toggle_callback(WorldEditorInputSystem *input, void (*callback)(void *user_data), void *user_data);
void world_editor_input_set_clock_toggle_callback(WorldEditorInputSystem *input, void (*callback)(void *user_data), void *user_data);

// Configuration
void world_editor_input_set_mouse_look_enabled(WorldEditorInputSystem *input, bool enabled);
void world_editor_input_set_mouse_sensitivity(WorldEditorInputSystem *input, float sensitivity);
void world_editor_input_set_capture_mouse(WorldEditorInputSystem *input, bool capture);

#endif // WORLD_EDITOR_INPUT_H
