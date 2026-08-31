// Mouse look must never rotate the view faster than the player's turn speed, however violently
// the mouse is moved. That is the whole point of routing deltas through aim_yaw/aim_pitch instead
// of writing facing directly, so it is worth asserting rather than eyeballing.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "console.h"
#include "player_controls.h"

// The client shell (verse_client.c) owns these; this test links the client's guts against its own
// main, so it has to supply them. Nothing under test touches them.
GameState *g_game_state = NULL;
int g_show_exit_prompt = 0;
Console *g_console = NULL;
void handle_button_click(int button_id) { (void)button_id; }
void play_highlight_sound(void) {}

static int g_failures = 0;

static void check(bool ok, const char *what)
{
  printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
  if (!ok)
    g_failures++;
}

static float wrapped_delta(float from, float to)
{
  float d = to - from;
  while (d > (float)M_PI)
    d -= 2.0f * (float)M_PI;
  while (d < -(float)M_PI)
    d += 2.0f * (float)M_PI;
  return d;
}

// A NULL GameState makes player_controls_* fall back to PLAYER_DEFAULT_TURN_SPEED_DEG, which is
// exactly the rate this test wants to measure against.
static const float TURN_DEG = PLAYER_DEFAULT_TURN_SPEED_DEG;

static void test_yaw_rate_is_capped(void)
{
  PlayerControls ctrl;
  player_controls_init(&ctrl);

  const double dt = 1.0 / 60.0;
  const float max_step = TURN_DEG * (float)M_PI / 180.0f * (float)dt;

  float worst_step = 0.0f;
  // A hard flick every frame: far more rotation than turn speed can deliver.
  for (int frame = 0; frame < 240; frame++)
  {
    player_controls_apply_mouse_look(&ctrl, 900, 0);
    const float before = ctrl.facing_yaw;
    player_controls_update_facing(NULL, &ctrl, dt);
    const float step = fabsf(wrapped_delta(before, ctrl.facing_yaw));
    if (step > worst_step)
      worst_step = step;
  }

  printf("       worst yaw step %.4f rad vs limit %.4f rad\n", worst_step, max_step);
  check(worst_step <= max_step + 1e-4f, "yaw never exceeds turn speed under a sustained flick");

  // And the aim is not allowed to run away from facing, so releasing the mouse settles promptly.
  const float lead = fabsf(wrapped_delta(ctrl.facing_yaw, ctrl.aim_yaw));
  const float max_lead = PLAYER_MAX_AIM_LEAD_DEG * (float)M_PI / 180.0f;
  printf("       aim leads facing by %.4f rad, cap %.4f rad\n", lead, max_lead);
  check(lead <= max_lead + 1e-4f, "aim yaw never leads facing past the cap");
}

static void test_yaw_direction_and_convergence(void)
{
  PlayerControls ctrl;
  player_controls_init(&ctrl);

  player_controls_apply_mouse_look(&ctrl, 40, 0);
  check(wrapped_delta(0.0f, ctrl.aim_yaw) > 0.0f, "moving the mouse right increases yaw");

  player_controls_init(&ctrl);
  player_controls_apply_mouse_look(&ctrl, -40, 0);
  check(wrapped_delta(0.0f, ctrl.aim_yaw) < 0.0f, "moving the mouse left decreases yaw");

  // With the mouse still, facing must converge on aim rather than oscillate or stall.
  player_controls_init(&ctrl);
  player_controls_apply_mouse_look(&ctrl, 200, 0);
  const float target = ctrl.aim_yaw;
  for (int frame = 0; frame < 120; frame++)
    player_controls_update_facing(NULL, &ctrl, 1.0 / 60.0);

  printf("       settled yaw error %.6f rad\n", fabsf(wrapped_delta(ctrl.facing_yaw, target)));
  check(fabsf(wrapped_delta(ctrl.facing_yaw, target)) < 1e-4f,
        "facing settles exactly on aim once the mouse stops");
}

static void test_pitch_is_capped_and_clamped(void)
{
  PlayerControls ctrl;
  player_controls_init(&ctrl);

  const double dt = 1.0 / 60.0;
  const float max_step = TURN_DEG * (float)M_PI / 180.0f * (float)dt;
  const float max_pitch = PLAYER_MAX_PITCH_DEG * (float)M_PI / 180.0f;

  float worst_step = 0.0f;
  for (int frame = 0; frame < 240; frame++)
  {
    player_controls_apply_mouse_look(&ctrl, 0, -900); // slam the mouse up
    const float before = ctrl.pitch;
    player_controls_update_facing(NULL, &ctrl, dt);
    const float step = fabsf(ctrl.pitch - before);
    if (step > worst_step)
      worst_step = step;
  }

  printf("       worst pitch step %.4f rad vs limit %.4f rad; settled pitch %.4f rad (cap %.4f)\n",
         worst_step, max_step, ctrl.pitch, max_pitch);
  check(worst_step <= max_step + 1e-4f, "pitch never exceeds turn speed");
  check(ctrl.pitch <= max_pitch + 1e-4f, "looking up stops at the pitch limit");

  for (int frame = 0; frame < 480; frame++)
  {
    player_controls_apply_mouse_look(&ctrl, 0, 900); // and back down
    player_controls_update_facing(NULL, &ctrl, dt);
  }
  printf("       settled pitch after looking down %.4f rad\n", ctrl.pitch);
  check(ctrl.pitch >= -max_pitch - 1e-4f, "looking down stops at the pitch limit");
}

static void test_zero_delta_is_inert(void)
{
  PlayerControls ctrl;
  player_controls_init(&ctrl);
  player_controls_apply_mouse_look(&ctrl, 0, 0);
  check(ctrl.aim_yaw == 0.0f && ctrl.aim_pitch == 0.0f, "no mouse motion leaves aim untouched");
  player_controls_apply_mouse_look(NULL, 10, 10);
  check(true, "a NULL controls pointer is ignored rather than crashing");
}

int main(void)
{
  printf("mouse look audit (turn speed %.0f deg/s)\n", (double)TURN_DEG);
  printf("[yaw]\n");
  test_yaw_rate_is_capped();
  test_yaw_direction_and_convergence();
  printf("[pitch]\n");
  test_pitch_is_capped_and_clamped();
  printf("[edges]\n");
  test_zero_delta_is_inert();

  printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
         g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
