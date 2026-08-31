#include "world_editor_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

// Forward declarations for context-specific input handlers
static bool handle_game_input(WorldEditorInputSystem *input, SDL_Event *event);
static bool handle_ui_input(WorldEditorInputSystem *input, SDL_Event *event);
static bool handle_text_input(WorldEditorInputSystem *input, SDL_Event *event);
static bool handle_editor_input(WorldEditorInputSystem *input, SDL_Event *event);

// Reset key state for a single key
static void reset_key_state(WorldEditorKeyState *key_state)
{
  key_state->just_pressed = false;
  key_state->just_released = false;
}

// Update modifier states from SDL event
static void update_modifier_states(WorldEditorModifierState *mods, SDL_Keymod sdl_mods)
{
  mods->ctrl = (sdl_mods & KMOD_CTRL) != 0;
  mods->shift = (sdl_mods & KMOD_SHIFT) != 0;
  mods->alt = (sdl_mods & KMOD_ALT) != 0;
  mods->super = (sdl_mods & KMOD_GUI) != 0; // Command/Windows key
}

// ============================================================================
// INITIALIZATION & CLEANUP
// ============================================================================

WorldEditorInputSystem *world_editor_input_create(void)
{
  WorldEditorInputSystem *input = calloc(1, sizeof(WorldEditorInputSystem));
  if (!input)
  {
    printf("Failed to allocate WorldEditorInputSystem\n");
    return NULL;
  }

  // Initialize key states
  for (int i = 0; i < SDL_NUM_SCANCODES; i++)
  {
    input->keys[i].pressed = false;
    input->keys[i].just_pressed = false;
    input->keys[i].just_released = false;
    input->keys[i].press_time = 0;
  }

  // Initialize modifier states
  input->modifiers.ctrl = false;
  input->modifiers.shift = false;
  input->modifiers.alt = false;
  input->modifiers.super = false;

  // Initialize mouse state
  input->mouse.x = 0;
  input->mouse.y = 0;
  input->mouse.delta_x = 0;
  input->mouse.delta_y = 0;
  input->mouse.left_pressed = false;
  input->mouse.right_pressed = false;
  input->mouse.middle_pressed = false;
  input->mouse.wheel_x = 0;
  input->mouse.wheel_y = 0;

  // Initialize context
  input->current_context = INPUT_CONTEXT_GAME;
  input->previous_context = INPUT_CONTEXT_GAME;

  // Initialize callbacks to NULL
  input->on_actor_insert = NULL;
  input->on_actor_select = NULL;
  input->on_camera_move = NULL;
  input->on_tool_change = NULL;
  input->on_voxel_place = NULL;
  input->on_voxel_remove = NULL;
  input->on_mouse_look_toggle = NULL;
  input->on_time_control = NULL;
  input->on_zoom_control = NULL;

  // Initialize user data
  input->user_data = NULL;

  // Initialize configuration
  input->enable_mouse_look = false;
  input->mouse_sensitivity = 1.0f;
  input->capture_mouse = false;

  return input;
}

void world_editor_input_destroy(WorldEditorInputSystem *input)
{
  if (!input)
    return;

  free(input);
}

// ============================================================================
// UPDATE & EVENT HANDLING
// ============================================================================

void world_editor_input_update(WorldEditorInputSystem *input)
{
  if (!input)
    return;

  // Reset just_pressed and just_released flags for all keys
  for (int i = 0; i < SDL_NUM_SCANCODES; i++)
  {
    reset_key_state(&input->keys[i]);
  }

  // Reset mouse deltas
  input->mouse.delta_x = 0;
  input->mouse.delta_y = 0;
  input->mouse.wheel_x = 0;
  input->mouse.wheel_y = 0;

  // Update context change tracking
  input->previous_context = input->current_context;
}

bool world_editor_input_handle_event(WorldEditorInputSystem *input, SDL_Event *event)
{
  if (!input || !event)
    return false;

  if (event->type == SDL_KEYDOWN) {
    printf("[DEBUG] Input system received keydown event: %d\n", event->key.keysym.sym);
  }

  switch (event->type)
  {
  case SDL_KEYDOWN:
  {
    SDL_Scancode scancode = event->key.keysym.scancode;
    if (scancode < SDL_NUM_SCANCODES)
    {
      WorldEditorKeyState *key_state = &input->keys[scancode];
      if (!key_state->pressed)
      {
        key_state->pressed = true;
        key_state->just_pressed = true;
        key_state->press_time = SDL_GetTicks();
      }
    }

    // Update modifier states
    update_modifier_states(&input->modifiers, SDL_GetModState());

    // Handle specific key actions based on context
    if (input->current_context == INPUT_CONTEXT_GAME)
    {
      return handle_game_input(input, event);
    }
    else if (input->current_context == INPUT_CONTEXT_UI)
    {
      return handle_ui_input(input, event);
    }
    else if (input->current_context == INPUT_CONTEXT_TEXT)
    {
      return handle_text_input(input, event);
    }
    else if (input->current_context == INPUT_CONTEXT_EDITOR)
    {
      return handle_editor_input(input, event);
    }
    break;
  }

  case SDL_KEYUP:
  {
    SDL_Scancode scancode = event->key.keysym.scancode;
    if (scancode < SDL_NUM_SCANCODES)
    {
      WorldEditorKeyState *key_state = &input->keys[scancode];
      key_state->pressed = false;
      key_state->just_released = true;
    }

    // Update modifier states
    update_modifier_states(&input->modifiers, SDL_GetModState());
    break;
  }

  case SDL_MOUSEMOTION:
  {
    input->mouse.delta_x = event->motion.x - input->mouse.x;
    input->mouse.delta_y = event->motion.y - input->mouse.y;
    input->mouse.x = event->motion.x;
    input->mouse.y = event->motion.y;
    break;
  }

  case SDL_MOUSEBUTTONDOWN:
  {
    switch (event->button.button)
    {
    case SDL_BUTTON_LEFT:
      input->mouse.left_pressed = true;
      break;
    case SDL_BUTTON_RIGHT:
      input->mouse.right_pressed = true;
      break;
    case SDL_BUTTON_MIDDLE:
      input->mouse.middle_pressed = true;
      break;
    }
    break;
  }

  case SDL_MOUSEBUTTONUP:
  {
    switch (event->button.button)
    {
    case SDL_BUTTON_LEFT:
      input->mouse.left_pressed = false;
      break;
    case SDL_BUTTON_RIGHT:
      input->mouse.right_pressed = false;
      break;
    case SDL_BUTTON_MIDDLE:
      input->mouse.middle_pressed = false;
      break;
    }
    break;
  }

  case SDL_MOUSEWHEEL:
  {
    input->mouse.wheel_x = event->wheel.x;
    input->mouse.wheel_y = event->wheel.y;
    break;
  }
  }

  return false;
}

// ============================================================================
// CONTEXT-SPECIFIC INPUT HANDLERS
// ============================================================================

static bool handle_game_input(WorldEditorInputSystem *input, SDL_Event *event)
{
  SDL_Keycode key = event->key.keysym.sym;
  printf("[DEBUG] handle_game_input called with key: %d\n", key);

  switch (key)
  {
  case SDLK_i:
    if (input->on_actor_insert)
    {
      input->on_actor_insert(input->user_data);
      return true;
    }
    break;

  case SDLK_m:
    if (input->on_mouse_look_toggle)
    {
      input->on_mouse_look_toggle(!input->enable_mouse_look, input->user_data);
      return true;
    }
    break;

  case SDLK_F1:
    if (input->on_actor_select)
    {
      input->on_actor_select(0, input->user_data); // Select first actor
      return true;
    }
    break;

  case SDLK_PERIOD:
  case SDLK_GREATER:
    if (input->on_time_control)
    {
      input->on_time_control(1, input->user_data); // Speed up time
      return true;
    }
    break;

  case SDLK_COMMA:
  case SDLK_LESS:
    if (input->on_time_control)
    {
      input->on_time_control(-1, input->user_data); // Slow down time
      return true;
    }
    break;

  case SDLK_PLUS:
  case SDLK_EQUALS:
    if (input->on_zoom_control)
    {
      input->on_zoom_control(1.1f, input->user_data); // Zoom in
      return true;
    }
    break;

  case SDLK_MINUS:
    if (input->on_zoom_control)
    {
      input->on_zoom_control(0.9f, input->user_data); // Zoom out
      return true;
    }
    break;

  case SDLK_PAGEUP:
  case SDLK_RIGHTBRACKET:
    if (input->on_camera_move)
    {
      input->on_camera_move(0, 0, 1, input->user_data); // Move camera up in Z
      return true;
    }
    break;

  case SDLK_PAGEDOWN:
  case SDLK_LEFTBRACKET:
    if (input->on_camera_move)
    {
      input->on_camera_move(0, 0, -1, input->user_data); // Move camera down in Z
      return true;
    }
    break;

  case SDLK_a:
    if (input->on_camera_rotate)
    {
      input->on_camera_rotate(-1, input->user_data); // Rotate CCW
      return true;
    }
    break;

  case SDLK_d:
    if (input->on_camera_rotate)
    {
      input->on_camera_rotate(1, input->user_data); // Rotate CW
      return true;
    }
    break;

  case SDLK_HOME:
    if (input->on_camera_reset)
    {
      input->on_camera_reset(input->user_data);
      return true;
    }
    break;

  case SDLK_g:
    if (input->on_renderer_toggle)
    {
      input->on_renderer_toggle(0, input->user_data); // Toggle greedy neighbors
      return true;
    }
    break;

  case SDLK_b:
    if (input->on_renderer_toggle)
    {
      input->on_renderer_toggle(1, input->user_data); // Toggle baseline overlay
      return true;
    }
    break;

  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    if (input->on_chat_toggle)
    {
      input->on_chat_toggle(input->user_data);
      return true;
    }
    break;

  case SDLK_SPACE:
    if (input->on_clock_toggle)
    {
      input->on_clock_toggle(input->user_data);
      return true;
    }
    break;

  case SDLK_UP:
  case SDLK_DOWN:
  case SDLK_LEFT:
  case SDLK_RIGHT:
    // Handle arrow keys for camera movement
    if (input->on_camera_move)
    {
      int dx = 0, dy = 0;
      // Map arrows to visual directions in isometric:
      // Screen-left moves along (x-1, y+1)
      // Screen-right moves along (x+1, y-1)
      // Screen-up moves along (x+1, y+1)
      // Screen-down moves along (x-1, y-1)
      if (key == SDLK_LEFT)
      {
        dx = -1; dy = +1;
      }
      else if (key == SDLK_RIGHT)
      {
        dx = +1; dy = -1;
      }
      else if (key == SDLK_UP)
      {
        dx = -1; dy = -1; // screen-up = (x-1, y-1)
      }
      else if (key == SDLK_DOWN)
      {
        dx = +1; dy = +1; // screen-down = (x+1, y+1)
      }

      input->on_camera_move(dx, dy, 0, input->user_data);
      return true;
    }
    break;
  }

  return false;
}

static bool handle_ui_input(WorldEditorInputSystem *input, SDL_Event *event)
{
  // UI input handling - for future use
  (void)input;
  (void)event;
  return false;
}

static bool handle_text_input(WorldEditorInputSystem *input, SDL_Event *event)
{
  // Text input handling - for future use
  (void)input;
  (void)event;
  return false;
}

static bool handle_editor_input(WorldEditorInputSystem *input, SDL_Event *event)
{
  // Editor-specific input handling - for future use
  (void)input;
  (void)event;
  return false;
}

// ============================================================================
// KEY STATE QUERIES
// ============================================================================

bool world_editor_input_is_key_pressed(WorldEditorInputSystem *input, SDL_Scancode scancode)
{
  if (!input || scancode >= SDL_NUM_SCANCODES)
    return false;
  return input->keys[scancode].pressed;
}

bool world_editor_input_is_key_just_pressed(WorldEditorInputSystem *input, SDL_Scancode scancode)
{
  if (!input || scancode >= SDL_NUM_SCANCODES)
    return false;
  return input->keys[scancode].just_pressed;
}

bool world_editor_input_is_key_just_released(WorldEditorInputSystem *input, SDL_Scancode scancode)
{
  if (!input || scancode >= SDL_NUM_SCANCODES)
    return false;
  return input->keys[scancode].just_released;
}

bool world_editor_input_is_key_held(WorldEditorInputSystem *input, SDL_Scancode scancode, uint32_t hold_time_ms)
{
  if (!input || scancode >= SDL_NUM_SCANCODES)
    return false;

  WorldEditorKeyState *key_state = &input->keys[scancode];
  if (!key_state->pressed)
    return false;

  uint32_t current_time = SDL_GetTicks();
  return (current_time - key_state->press_time) >= hold_time_ms;
}

// ============================================================================
// MODIFIER QUERIES
// ============================================================================

bool world_editor_input_is_ctrl_pressed(WorldEditorInputSystem *input)
{
  return input ? input->modifiers.ctrl : false;
}

bool world_editor_input_is_shift_pressed(WorldEditorInputSystem *input)
{
  return input ? input->modifiers.shift : false;
}

bool world_editor_input_is_alt_pressed(WorldEditorInputSystem *input)
{
  return input ? input->modifiers.alt : false;
}

bool world_editor_input_is_super_pressed(WorldEditorInputSystem *input)
{
  return input ? input->modifiers.super : false;
}

// ============================================================================
// MOUSE QUERIES
// ============================================================================

void world_editor_input_get_mouse_position(WorldEditorInputSystem *input, int *x, int *y)
{
  if (!input || !x || !y)
    return;

  *x = input->mouse.x;
  *y = input->mouse.y;
}

void world_editor_input_get_mouse_delta(WorldEditorInputSystem *input, int *dx, int *dy)
{
  if (!input || !dx || !dy)
    return;

  *dx = input->mouse.delta_x;
  *dy = input->mouse.delta_y;
}

bool world_editor_input_is_mouse_button_pressed(WorldEditorInputSystem *input, int button)
{
  if (!input)
    return false;

  switch (button)
  {
  case SDL_BUTTON_LEFT:
    return input->mouse.left_pressed;
  case SDL_BUTTON_RIGHT:
    return input->mouse.right_pressed;
  case SDL_BUTTON_MIDDLE:
    return input->mouse.middle_pressed;
  default:
    return false;
  }
}

// ============================================================================
// CONTEXT MANAGEMENT
// ============================================================================

WorldEditorInputContext world_editor_input_get_context(WorldEditorInputSystem *input)
{
  return input ? input->current_context : INPUT_CONTEXT_GAME;
}

void world_editor_input_set_context(WorldEditorInputSystem *input, WorldEditorInputContext context)
{
  if (!input)
    return;

  input->previous_context = input->current_context;
  input->current_context = context;
}

bool world_editor_input_context_changed(WorldEditorInputSystem *input)
{
  return input ? (input->current_context != input->previous_context) : false;
}

// ============================================================================
// CALLBACK SETTERS
// ============================================================================

void world_editor_input_set_actor_insert_callback(WorldEditorInputSystem *input, void (*callback)(void *user_data), void *user_data)
{
  if (input)
  {
    input->on_actor_insert = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_actor_select_callback(WorldEditorInputSystem *input, void (*callback)(int actor_id, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_actor_select = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_camera_move_callback(WorldEditorInputSystem *input, void (*callback)(int dx, int dy, int dz, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_camera_move = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_tool_change_callback(WorldEditorInputSystem *input, void (*callback)(int tool_id, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_tool_change = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_voxel_place_callback(WorldEditorInputSystem *input, void (*callback)(int x, int y, int z, int voxel_type, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_voxel_place = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_voxel_remove_callback(WorldEditorInputSystem *input, void (*callback)(int x, int y, int z, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_voxel_remove = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_mouse_look_toggle_callback(WorldEditorInputSystem *input, void (*callback)(bool enabled, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_mouse_look_toggle = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_time_control_callback(WorldEditorInputSystem *input, void (*callback)(int direction, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_time_control = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_zoom_control_callback(WorldEditorInputSystem *input, void (*callback)(float factor, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_zoom_control = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_camera_rotate_callback(WorldEditorInputSystem *input, void (*callback)(int direction, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_camera_rotate = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_camera_reset_callback(WorldEditorInputSystem *input, void (*callback)(void *user_data), void *user_data)
{
  if (input)
  {
    input->on_camera_reset = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_renderer_toggle_callback(WorldEditorInputSystem *input, void (*callback)(int toggle_type, void *user_data), void *user_data)
{
  if (input)
  {
    input->on_renderer_toggle = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_chat_toggle_callback(WorldEditorInputSystem *input, void (*callback)(void *user_data), void *user_data)
{
  if (input)
  {
    input->on_chat_toggle = callback;
    input->user_data = user_data;
  }
}

void world_editor_input_set_clock_toggle_callback(WorldEditorInputSystem *input, void (*callback)(void *user_data), void *user_data)
{
  if (input)
  {
    input->on_clock_toggle = callback;
    input->user_data = user_data;
  }
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void world_editor_input_set_mouse_look_enabled(WorldEditorInputSystem *input, bool enabled)
{
  if (input)
    input->enable_mouse_look = enabled;
}

void world_editor_input_set_mouse_sensitivity(WorldEditorInputSystem *input, float sensitivity)
{
  if (input && sensitivity > 0.0f)
    input->mouse_sensitivity = sensitivity;
}

void world_editor_input_set_capture_mouse(WorldEditorInputSystem *input, bool capture)
{
  if (input)
    input->capture_mouse = capture;
}
