#include "gamepad.h"

#include "game_state.h"
#include "player_controls.h"
#include "window.h"
#include "dialogue.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define GAMEPAD_MAX 4
#define GAMEPAD_DEADZONE 0.22f
#define GAMEPAD_LOOK_DEG_PER_SEC 210.0f
#define GAMEPAD_TRIGGER_FIRE 0.55f

typedef struct
{
  SDL_GameController *pad;
  SDL_JoystickID joy_id;
  bool prev_a, prev_b, prev_x, prev_y;
  bool prev_start, prev_back;
  bool prev_lb, prev_rb;
  bool prev_dpad_u, prev_dpad_d, prev_dpad_l, prev_dpad_r;
  bool prev_rstick, prev_lstick;
  bool prev_rt_fire;
} GamepadSlot;

static GamepadSlot g_pads[GAMEPAD_MAX];
static int g_pad_count = 0;
static bool g_inited = false;

static float axis_deadzone(Sint16 raw)
{
  float v = (float)raw / 32767.0f;
  if (v < -1.0f)
    v = -1.0f;
  if (v > 1.0f)
    v = 1.0f;
  const float dz = GAMEPAD_DEADZONE;
  if (v > -dz && v < dz)
    return 0.0f;
  // Rescale so leaving the deadzone starts at 0 rather than jumping to dz.
  float sign = v < 0.0f ? -1.0f : 1.0f;
  return sign * (fabsf(v) - dz) / (1.0f - dz);
}

static float trigger_axis(SDL_GameController *pad, SDL_GameControllerAxis axis)
{
  Sint16 raw = SDL_GameControllerGetAxis(pad, axis);
  float v = (float)raw / 32767.0f;
  if (v < 0.0f)
    v = 0.0f;
  if (v > 1.0f)
    v = 1.0f;
  return v;
}

static GamepadSlot *slot_for_joy(SDL_JoystickID id)
{
  for (int i = 0; i < g_pad_count; i++)
  {
    if (g_pads[i].pad && g_pads[i].joy_id == id)
      return &g_pads[i];
  }
  return NULL;
}

static void close_slot(GamepadSlot *slot)
{
  if (!slot || !slot->pad)
    return;
  printf("Gamepad: closed %s\n", SDL_GameControllerName(slot->pad));
  SDL_GameControllerClose(slot->pad);
  memset(slot, 0, sizeof(*slot));
}

static bool open_controller_index(int device_index)
{
  if (!SDL_IsGameController(device_index))
  {
    printf("Gamepad: joystick %d is not a recognised game controller\n", device_index);
    return false;
  }
  if (g_pad_count >= GAMEPAD_MAX)
  {
    printf("Gamepad: ignoring extra controller (max %d)\n", GAMEPAD_MAX);
    return false;
  }

  SDL_GameController *pad = SDL_GameControllerOpen(device_index);
  if (!pad)
  {
    printf("Gamepad: failed to open index %d: %s\n", device_index, SDL_GetError());
    return false;
  }

  SDL_Joystick *joy = SDL_GameControllerGetJoystick(pad);
  SDL_JoystickID id = joy ? SDL_JoystickInstanceID(joy) : -1;
  if (slot_for_joy(id))
  {
    SDL_GameControllerClose(pad);
    return false;
  }

  // Compact into first free slot (holes from disconnects).
  GamepadSlot *slot = NULL;
  for (int i = 0; i < GAMEPAD_MAX; i++)
  {
    if (!g_pads[i].pad)
    {
      slot = &g_pads[i];
      if (i >= g_pad_count)
        g_pad_count = i + 1;
      break;
    }
  }
  if (!slot)
  {
    SDL_GameControllerClose(pad);
    return false;
  }

  memset(slot, 0, sizeof(*slot));
  slot->pad = pad;
  slot->joy_id = id;
  printf("Gamepad: opened %s (instance %d)\n", SDL_GameControllerName(pad), (int)id);

  extern GameState *g_game_state;
  if (g_game_state)
  {
    snprintf(g_game_state->toast_text, sizeof(g_game_state->toast_text), "Controller: %s",
             SDL_GameControllerName(pad) ? SDL_GameControllerName(pad) : "Xbox");
    g_game_state->toast_text[sizeof(g_game_state->toast_text) - 1] = '\0';
    g_game_state->toast_start_ms = SDL_GetTicks();
    g_game_state->toast_duration_ms = 3000;
    g_game_state->toast_y_offset = 0.0f;
    g_game_state->toast_active = true;
  }
  return true;
}

static void open_all_connected(void)
{
  const int n = SDL_NumJoysticks();
  for (int i = 0; i < n; i++)
    open_controller_index(i);
}

static void compact_pads(void)
{
  int w = 0;
  for (int i = 0; i < g_pad_count; i++)
  {
    if (g_pads[i].pad)
    {
      if (w != i)
        g_pads[w] = g_pads[i];
      w++;
    }
  }
  for (int i = w; i < g_pad_count; i++)
    memset(&g_pads[i], 0, sizeof(g_pads[i]));
  g_pad_count = w;
}

static void emit_key(int key)
{
  if (window_state.key_callback)
    window_state.key_callback(key);
}

static bool btn(SDL_GameController *pad, SDL_GameControllerButton b)
{
  return SDL_GameControllerGetButton(pad, b) != 0;
}

static void handle_ui_edges(GamepadSlot *slot)
{
  if (!slot || !slot->pad)
    return;

  SDL_GameController *pad = slot->pad;
  const bool a = btn(pad, SDL_CONTROLLER_BUTTON_A);
  const bool b = btn(pad, SDL_CONTROLLER_BUTTON_B);
  const bool start = btn(pad, SDL_CONTROLLER_BUTTON_START);
  const bool back = btn(pad, SDL_CONTROLLER_BUTTON_BACK);
  const bool du = btn(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
  const bool dd = btn(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
  const bool dl = btn(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  const bool dr = btn(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
  const bool rstick = btn(pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
  const bool lstick = btn(pad, SDL_CONTROLLER_BUTTON_LEFTSTICK);

  extern GameState *g_game_state;
  const bool in_world = g_game_state && g_game_state->game_started &&
                        g_game_state->current_screen == GAME_SCREEN_WORLD &&
                        !g_game_state->show_world_editor_modal &&
                        !g_game_state->show_name_input;

  // Menus / overlays: D-pad and A/B/Start behave like arrows / Enter / Escape.
  if (!in_world)
  {
    if (a && !slot->prev_a)
      emit_key(SDLK_RETURN);
    if (b && !slot->prev_b)
      emit_key(SDLK_ESCAPE);
    if (start && !slot->prev_start)
      emit_key(SDLK_ESCAPE);
    if (du && !slot->prev_dpad_u)
      emit_key(SDLK_UP);
    if (dd && !slot->prev_dpad_d)
      emit_key(SDLK_DOWN);
    if (dl && !slot->prev_dpad_l)
      emit_key(SDLK_LEFT);
    if (dr && !slot->prev_dpad_r)
      emit_key(SDLK_RIGHT);
  }
  else
  {
    // In-world: Start = pause, Back = inventory, RS click = journal, LS click = camera toggle.
    if (start && !slot->prev_start)
      emit_key(SDLK_ESCAPE);
    if (back && !slot->prev_back)
      emit_key(SDLK_i);
    if (rstick && !slot->prev_rstick)
      emit_key(SDLK_j);
    if (lstick && !slot->prev_lstick)
      emit_key(SDLK_F4);
    if (du && !slot->prev_dpad_u && g_game_state)
      player_controls_use_hotbar_slot(g_game_state, &g_game_state->controls, 0);
    if (dd && !slot->prev_dpad_d && g_game_state)
      player_controls_use_hotbar_slot(g_game_state, &g_game_state->controls, 1);
    if (dl && !slot->prev_dpad_l && g_game_state)
      player_controls_use_hotbar_slot(g_game_state, &g_game_state->controls, 2);
    if (dr && !slot->prev_dpad_r && g_game_state)
      player_controls_use_hotbar_slot(g_game_state, &g_game_state->controls, 3);
  }

  slot->prev_a = a;
  slot->prev_b = b;
  slot->prev_start = start;
  slot->prev_back = back;
  slot->prev_dpad_u = du;
  slot->prev_dpad_d = dd;
  slot->prev_dpad_l = dl;
  slot->prev_dpad_r = dr;
  slot->prev_rstick = rstick;
  slot->prev_lstick = lstick;
}

void gamepad_init(void)
{
  if (g_inited)
    return;

  if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0)
  {
    printf("Gamepad: SDL_InitSubSystem failed: %s\n", SDL_GetError());
    return;
  }

  // Prefer the wired/Bluetooth Xbox mapping database shipped with SDL when present.
  SDL_GameControllerEventState(SDL_ENABLE);
  memset(g_pads, 0, sizeof(g_pads));
  g_pad_count = 0;
  open_all_connected();
  g_inited = true;
  printf("Gamepad: ready (%d controller%s)\n", g_pad_count, g_pad_count == 1 ? "" : "s");
}

void gamepad_shutdown(void)
{
  if (!g_inited)
    return;
  for (int i = 0; i < g_pad_count; i++)
    close_slot(&g_pads[i]);
  g_pad_count = 0;
  SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
  g_inited = false;
}

bool gamepad_is_active(void)
{
  for (int i = 0; i < g_pad_count; i++)
  {
    if (g_pads[i].pad && SDL_GameControllerGetAttached(g_pads[i].pad))
      return true;
  }
  return false;
}

void gamepad_handle_event(const SDL_Event *event)
{
  if (!g_inited || !event)
    return;

  switch (event->type)
  {
  case SDL_CONTROLLERDEVICEADDED:
    open_controller_index(event->cdevice.which);
    break;
  case SDL_CONTROLLERDEVICEREMOVED:
  {
    GamepadSlot *slot = slot_for_joy((SDL_JoystickID)event->cdevice.which);
    if (slot)
    {
      close_slot(slot);
      compact_pads();
      extern GameState *g_game_state;
      if (g_game_state)
      {
        strncpy(g_game_state->toast_text, "Controller disconnected",
                sizeof(g_game_state->toast_text) - 1);
        g_game_state->toast_text[sizeof(g_game_state->toast_text) - 1] = '\0';
        g_game_state->toast_start_ms = SDL_GetTicks();
        g_game_state->toast_duration_ms = 2500;
        g_game_state->toast_active = true;
      }
    }
    break;
  }
  case SDL_CONTROLLERBUTTONDOWN:
  case SDL_CONTROLLERBUTTONUP:
  {
    GamepadSlot *slot = slot_for_joy(event->cbutton.which);
    if (slot)
      handle_ui_edges(slot);
    break;
  }
  default:
    break;
  }
}

void gamepad_apply_movement(GameState *state)
{
  if (!g_inited || !state || !state->game_started || state->current_screen != GAME_SCREEN_WORLD ||
      state->show_world_editor_modal)
    return;

  bool any_move = false;
  for (int i = 0; i < g_pad_count; i++)
  {
    SDL_GameController *pad = g_pads[i].pad;
    if (!pad || !SDL_GameControllerGetAttached(pad))
      continue;

    float lx = axis_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
    float ly = axis_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
    // SDL Y+: down on stick. Game forward is -Y on stick (tilt up).
    if (ly < -0.35f)
      state->controls.move_forward = true;
    if (ly > 0.35f)
      state->controls.move_backward = true;
    if (lx < -0.35f)
      state->controls.move_left = true;
    if (lx > 0.35f)
      state->controls.move_right = true;

    if (btn(pad, SDL_CONTROLLER_BUTTON_A) && !dialogue_active(&state->dialogue))
      state->controls.move_up = true;
    if (btn(pad, SDL_CONTROLLER_BUTTON_B))
      state->controls.move_down = true;
    // Right trigger: run / afterburner (mirrors keyboard Shift).
    if (axis_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) > 0.35f)
      state->controls.boost = true;
    if (btn(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
      state->controls.bank_left = true;
    if (btn(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
      state->controls.bank_right = true;

    if (state->controls.move_forward || state->controls.move_backward ||
        state->controls.move_left || state->controls.move_right || state->controls.move_up ||
        state->controls.move_down)
      any_move = true;
  }

  if (any_move)
  {
    player_controls_clear_command_queue(&state->controls);
    state->controls.has_move_target = false;
  }
}

static void swing_along_facing(GameState *state)
{
  if (!state || state->controls.is_attacking)
    return;
  const float yaw = state->controls.facing_yaw;
  const float pitch = state->controls.pitch;
  const float cos_pitch = cosf(pitch);
  const float tx = state->player_world_x + cosf(yaw) * cos_pitch * PLAYER_SWING_RADIUS;
  const float ty = state->player_world_y + sinf(yaw) * cos_pitch * PLAYER_SWING_RADIUS;
  player_controls_start_attack(state, &state->controls, tx, ty);
  int vx = 0, vy = 0, vz = 0;
  if (player_controls_pick_melee_voxel(state, &state->controls, &vx, &vy, &vz))
    player_controls_set_attack_voxel(&state->controls, vx, vy, vz);
}

void gamepad_apply_actions(GameState *state, double dt_seconds)
{
  if (!g_inited || !state)
    return;

  // Keep button-edge tracking warm on menus so a held A does not fire Enter on the next open.
  if (!state->game_started || state->current_screen != GAME_SCREEN_WORLD ||
      state->show_world_editor_modal || state->show_name_input)
  {
    for (int i = 0; i < g_pad_count; i++)
    {
      if (!g_pads[i].pad)
        continue;
      // UI edges are event-driven; still refresh X/Y/LB/RB/RT prev for gameplay.
      g_pads[i].prev_x = btn(g_pads[i].pad, SDL_CONTROLLER_BUTTON_X);
      g_pads[i].prev_y = btn(g_pads[i].pad, SDL_CONTROLLER_BUTTON_Y);
      g_pads[i].prev_lb = btn(g_pads[i].pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
      g_pads[i].prev_rb = btn(g_pads[i].pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
      g_pads[i].prev_rt_fire =
          trigger_axis(g_pads[i].pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >= GAMEPAD_TRIGGER_FIRE;
    }
    return;
  }

  if (dt_seconds < 0.0)
    dt_seconds = 0.0;
  if (dt_seconds > 0.1)
    dt_seconds = 0.1;

  for (int i = 0; i < g_pad_count; i++)
  {
    GamepadSlot *slot = &g_pads[i];
    SDL_GameController *pad = slot->pad;
    if (!pad || !SDL_GameControllerGetAttached(pad))
      continue;

    float rx = axis_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
    float ry = axis_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));
    if (rx != 0.0f || ry != 0.0f)
    {
      // Same path as mouse look: aim_yaw/pitch, then facing slews to catch up.
      const float deg = GAMEPAD_LOOK_DEG_PER_SEC * (float)dt_seconds;
      const float px_per_deg = 1.0f / PLAYER_MOUSE_LOOK_DEG_PER_PX;
      int dx = (int)lroundf(rx * deg * px_per_deg);
      int dy = (int)lroundf(ry * deg * px_per_deg);
      if (dx != 0 || dy != 0)
        player_controls_apply_mouse_look(&state->controls, dx, dy);
    }

    const bool x = btn(pad, SDL_CONTROLLER_BUTTON_X);
    const bool y = btn(pad, SDL_CONTROLLER_BUTTON_Y);
    const float rt = trigger_axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    const bool rt_fire = rt >= GAMEPAD_TRIGGER_FIRE;

    if ((x && !slot->prev_x) || (rt_fire && !slot->prev_rt_fire))
      swing_along_facing(state);

    // Y = primary skill (fireball / hotbar 0).
    if (y && !slot->prev_y)
      player_controls_use_hotbar_slot(state, &state->controls, 0);

    slot->prev_x = x;
    slot->prev_y = y;
    slot->prev_rt_fire = rt_fire;
    slot->prev_lb = btn(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    slot->prev_rb = btn(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  }
}

bool gamepad_town_portal_held(void)
{
  if (!g_inited)
    return false;
  for (int i = 0; i < g_pad_count; i++)
  {
    SDL_GameController *pad = g_pads[i].pad;
    if (!pad || !SDL_GameControllerGetAttached(pad))
      continue;
    if (trigger_axis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >= GAMEPAD_TRIGGER_FIRE)
      return true;
  }
  return false;
}
