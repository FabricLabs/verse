// Verifies that a new game puts the player one voxel clear of the island surface and that
// gravity carries them down to a landing, rather than snapping them onto the ground.
//
// This drives the real GameState functions against a real generated home world. GameState is
// a plain struct, so the test builds one directly instead of going through game_state_create,
// which would want a window and an audio device.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "constants.h"
#include "actor.h"
#include "game_state.h"
#include "task_scheduler.h"

#include <unistd.h>
#include "player_controls.h"
#include "shadow_world.h"
#include "universe.h"
#include "world.h"
#include "world_gen_job.h"

// The client's UI objects reach back into verse_client.c for these. Standing in for them is what
// lets this test link the real spawn and gravity code without the client's main(). The test drives
// GameState directly and renders nothing, so none of them are ever called.
GameState *g_game_state = NULL;
int g_show_exit_prompt = 0;
Console *g_console = NULL;
void handle_button_click(int button_id) { (void)button_id; }
void play_highlight_sound(void) {}

static int test_failures = 0;

static void report(const char *name, bool passed)
{
  printf("%s %s\n", passed ? "PASS" : "FAILED", name);
  if (!passed)
    test_failures++;
}

// Highest non-air voxel in the player's column, or -1 if the column is empty.
static int top_solid_z(World *world, int x, int y)
{
  for (int z = (int)world->depth - 1; z >= 0; z--)
  {
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (v && v->type != VOXEL_AIR)
      return z;
  }
  return -1;
}

int main(void)
{
  printf("=== Spawn and Gravity Tests ===\n\n");

  World *home = world_create(WORLD_SIZE_CUBE);
  if (!home)
  {
    printf("FAILED could not allocate home world\n");
    return 1;
  }
  world_generate_with_type(home, "spawn-gravity-test", WORLD_TYPE_HOME);

  GameState *state = (GameState *)calloc(1, sizeof(GameState));
  if (!state)
  {
    printf("FAILED could not allocate game state\n");
    world_destroy(home);
    return 1;
  }
  state->current_world = home;
  strncpy(state->player_name, "tester", sizeof(state->player_name) - 1);

  report("home world generated", true);
  report("generated gravity is in the game range",
         home->gravity >= GRAVITY_MIN && home->gravity <= GRAVITY_MAX);

  {
    const float original = world_get_gravity(home);
    world_set_gravity(home, 0.1f);
    report("gravity clamps to minimum", world_get_gravity(home) == GRAVITY_MIN);
    world_set_gravity(home, 999.0f);
    report("gravity clamps to maximum", world_get_gravity(home) == GRAVITY_MAX);
    world_set_gravity(home, GRAVITY_DEFAULT);
    report("world gravity is configurable", world_get_gravity(home) == GRAVITY_DEFAULT);
    report("player gravity matches world gravity in voxels/s^2",
           game_state_player_gravity(state) == GRAVITY_DEFAULT);
    world_set_gravity(home, original);
  }

  if (!game_state_place_player_at_surface_spawn(state))
  {
    report("spawn found on the island", false);
    free(state);
    world_destroy(home);
    return 1;
  }
  report("spawn found on the island", true);

  int spawn_x = state->player_x;
  int spawn_y = state->player_y;
  int spawn_z = state->player_z;
  int ground_z = top_solid_z(home, spawn_x, spawn_y);

  report("spawn column has solid ground", ground_z >= 0);

  // The standing position is the voxel directly above the ground; spawning is one higher so
  // there is a voxel of air under the player's feet for gravity to close.
  report("spawn is one voxel above the standing position", spawn_z == ground_z + 2);

  Voxel *under_feet = world_get_voxel(home, (uint32_t)spawn_x, (uint32_t)spawn_y,
                                      (uint32_t)(spawn_z - 1));
  report("voxel under the player at spawn is air",
         under_feet != NULL && under_feet->type == VOXEL_AIR);
  report("player starts at rest", state->controls.velocity_z == 0.0f);
  report("spirit defaults to hover-flight at spawn", state->player_flying);

  // Hover-flight ignores gravity; the spirit floats where it was placed.
  float before = state->player_world_z;
  game_state_apply_gravity(state, 1.0f / 60.0f);
  report("gravity does not act before the game starts", state->player_world_z == before);

  state->game_started = true;

  game_state_apply_gravity(state, 1.0f / 60.0f);
  report("hover-flight holds altitude against gravity",
         fabsf(state->player_world_z - before) < 1e-4f);

  // Toggle gravity on to verify the spawn column still settles under walk mode.
  state->player_flying = false;
  if (state->player)
    state->player->is_flying = false;

  // Now run frames until the player settles, as the game loop would.
  const float dt = 1.0f / 60.0f;
  int frames = 0;
  int frames_falling = 0;
  float lowest = state->player_world_z;
  bool never_rose = true;
  float previous = state->player_world_z;

  while (frames < 600)
  {
    game_state_apply_gravity(state, dt);
    frames++;

    if (state->player_world_z > previous + 1e-6f)
      never_rose = false;
    if (state->player_world_z < previous - 1e-6f)
      frames_falling++;
    if (state->player_world_z < lowest)
      lowest = state->player_world_z;
    previous = state->player_world_z;

    if (state->controls.velocity_z == 0.0f && state->player_world_z < (float)spawn_z)
      break; // landed
  }

  report("player fell under gravity", state->player_world_z < (float)spawn_z + 0.5f);
  report("the fall took more than one frame", frames_falling >= 1 && frames > 1);
  report("gravity never moved the player upward", never_rose);
  report("player came to rest", state->controls.velocity_z == 0.0f);

  // Landed standing on the ground: feet in the voxel above the surface.
  report("player landed on the voxel above the ground",
         state->player_z == ground_z + 1);
  report("landing position is the voxel centre",
         fabsf(state->player_world_z - ((float)(ground_z + 1) + 0.5f)) < 1e-4f);

  Voxel *support = world_get_voxel(home, (uint32_t)state->player_x, (uint32_t)state->player_y,
                                   (uint32_t)(state->player_z - 1));
  report("player is standing on something solid",
         support != NULL && support->type != VOXEL_AIR);

  report("integer and float positions agree after landing",
         state->player_z == (int)floorf(state->player_world_z));

  // A settled player must stay put: gravity should be a no-op from here.
  float rest_z = state->player_world_z;
  for (int i = 0; i < 30; i++)
    game_state_apply_gravity(state, dt);
  report("a landed player does not sink further", state->player_world_z == rest_z);

  // Hover-flight: no gravity pull. With no vertical thrust the spirit holds altitude.
  state->player_flying = true;
  state->controls.velocity_z = 0.0f;
  game_state_apply_gravity(state, dt);
  report("hover-flight with no thrust holds altitude",
         state->player_world_z == rest_z && state->controls.velocity_z == 0.0f);

  float hover_z = state->player_world_z;
  state->controls.velocity_z = PLAYER_SPIRIT_VERT_SPEED;
  game_state_apply_gravity(state, dt);
  report("hover-flight rises under upward thrust", state->player_world_z > hover_z);
  state->controls.velocity_z = 0.0f;

  // -------------------------------------------------------------------------------------------
  // Jumping, ceilings, and walking off a ledge.
  //
  // Two failures with one cause between them: gravity only ever integrated downward, so a jump set
  // an upward velocity that was drained away again without the player ever leaving the ground, and
  // horizontal movement refused any destination without solid ground beneath it, so the player
  // stopped dead at the lip of every drop rather than falling off it.
  //
  // Driven against a purpose-built arena rather than the generated island, so the drop height and
  // the ceiling are known exactly.
  // -------------------------------------------------------------------------------------------

  printf("\n--- jumping and ledges ---\n");

  World *arena = world_create(32, 32, 32);
  if (!arena)
  {
    report("arena world allocated", false);
    free(state);
    world_destroy(home);
    return 1;
  }

  // A lower floor topping out at z=3, and a plateau over half of it topping out at z=7. So a player
  // standing on the plateau has their feet at z=8, and stepping west off it is a four-voxel drop to
  // a standing height of z=4.
  const int lower_top = 3;
  const int plateau_top = 7;
  const int plateau_x0 = 16;
  for (uint32_t y = 0; y < arena->height; y++)
    for (uint32_t x = 0; x < arena->width; x++)
    {
      for (int z = 0; z <= lower_top; z++)
        world_set_voxel(arena, x, y, (uint32_t)z, VOXEL_STONE);
      if (x >= (uint32_t)plateau_x0)
        for (int z = lower_top + 1; z <= plateau_top; z++)
          world_set_voxel(arena, x, y, (uint32_t)z, VOXEL_STONE);
    }
  world_refresh_occupancy_bitfield(arena);

  World *saved_world = state->current_world;
  state->current_world = arena;
  state->player_flying = false;
  state->controls.velocity_z = 0.0f;
  player_controls_init(&state->controls);

  const int stand_z = plateau_top + 1;
  const float stand_world_z = (float)stand_z + 0.5f;
  game_state_set_player_position(state, 20, 16, stand_z);

  report("player stands on the plateau", game_state_player_is_grounded(state));

  // The jump is specified as a height, so the speed it needs depends on the world's gravity. What
  // matters is that whatever speed comes out clears a step the player could otherwise not climb.
  const float gravity_voxels = game_state_player_gravity(state);
  const float jump_v = game_state_player_jump_velocity(state);
  const float predicted_apex = (jump_v * jump_v) / (2.0f * gravity_voxels);
  printf("     gravity %.1f voxels/s^2, jump %.2f voxels/s, apex %.2f voxels\n", gravity_voxels,
         jump_v, predicted_apex);
  report("jump speed is derived to clear a one-voxel step", predicted_apex > 1.0f);

  // Strength scales only the takeoff impulse, not the gravity-derived baseline. This harness has
  // no spirit actor, so borrow a temporary one.
  {
    Actor body;
    memset(&body, 0, sizeof(body));
    Actor *prev = state->player;
    state->player = &body;
    body.strength = 10;
    const float base_at_10 = game_state_player_jump_velocity(state);
    const float launch_at_10 = player_controls_jump_launch_speed(state);
    body.strength = 30;
    const float base_at_30 = game_state_player_jump_velocity(state);
    const float launch_at_30 = player_controls_jump_launch_speed(state);
    state->player = prev;
    printf("     gravity baseline %.2f (str10) / %.2f (str30); launch %.2f / %.2f\n",
           base_at_10, base_at_30, launch_at_10, launch_at_30);
    report("gravity-derived jump baseline ignores strength",
           fabsf(base_at_10 - base_at_30) < 1e-4f);
    report("stronger bodies get a harder upward shove",
           launch_at_30 > launch_at_10 * 1.05f && fabsf(launch_at_10 - base_at_10) < 1e-4f);
  }

  // Hold the jump key and run the loop the game runs: vertical intent, then gravity.
  // Repeated at two frame rates, because integrating with the end-of-step velocity made the height
  // reached depend on how often this ran.
  const float rates[] = {1.0f / 60.0f, 1.0f / 20.0f};
  for (int r = 0; r < 2; r++)
  {
    const float step = rates[r];
    game_state_set_player_position(state, 20, 16, stand_z);
    state->controls.velocity_z = 0.0f;
    state->controls.move_up = true;

    float apex = state->player_world_z;
    int airborne_frames = 0;
    bool landed = false;
    for (int i = 0; i < 400; i++)
    {
      player_controls_apply_vertical(state, &state->controls, step);
      game_state_apply_gravity(state, step);

      if (state->player_world_z > apex)
        apex = state->player_world_z;
      if (state->player_world_z > stand_world_z + 1e-4f)
        airborne_frames++;
      else if (airborne_frames > 0)
      {
        landed = true;
        break;
      }
      // Holding the key would jump again the moment the player touches down.
      if (airborne_frames > 0)
        state->controls.move_up = false;
    }
    state->controls.move_up = false;

    char label[96];
    snprintf(label, sizeof(label), "jump at %.0ffps leaves the ground", 1.0f / step);
    report(label, apex > stand_world_z + 1e-3f);

    snprintf(label, sizeof(label), "jump at %.0ffps clears a one-voxel step (reached %.2f)",
             1.0f / step, apex - stand_world_z);
    report(label, apex - stand_world_z >= 1.0f);

    snprintf(label, sizeof(label), "jump at %.0ffps spends several frames in the air", 1.0f / step);
    report(label, airborne_frames > 1);

    snprintf(label, sizeof(label), "jump at %.0ffps comes back down to the surface", 1.0f / step);
    report(label, landed && fabsf(state->player_world_z - stand_world_z) < 1e-3f);
  }

  // A ceiling two voxels above the plateau: the player fits standing, so the jump must be stopped by
  // it rather than passing through. Without a clearance check the rise loop would tunnel straight up.
  const int ceiling_z = stand_z + 2;
  for (uint32_t y = 0; y < arena->height; y++)
    for (uint32_t x = (uint32_t)plateau_x0; x < arena->width; x++)
      world_set_voxel(arena, x, y, (uint32_t)ceiling_z, VOXEL_STONE);
  world_refresh_occupancy_bitfield(arena);

  game_state_set_player_position(state, 20, 16, stand_z);
  state->controls.velocity_z = 0.0f;
  state->controls.move_up = true;
  float capped_apex = state->player_world_z;
  for (int i = 0; i < 120; i++)
  {
    player_controls_apply_vertical(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
    if (state->player_world_z > capped_apex)
      capped_apex = state->player_world_z;
    state->controls.move_up = false;
  }
  printf("     ceiling at z=%d capped the jump at %.2f\n", ceiling_z, capped_apex);
  report("a jump into a ceiling still leaves the ground", capped_apex > stand_world_z + 1e-3f);
  report("a jump stops below the ceiling instead of passing through it",
         capped_apex < (float)(ceiling_z - 1));
  report("the player is back on the ground after bumping their head",
         fabsf(state->player_world_z - stand_world_z) < 1e-3f);

  for (uint32_t y = 0; y < arena->height; y++)
    for (uint32_t x = (uint32_t)plateau_x0; x < arena->width; x++)
      world_set_voxel(arena, x, y, (uint32_t)ceiling_z, VOXEL_AIR);
  world_refresh_occupancy_bitfield(arena);

  // The lip of the drop. The voxel west of the plateau edge is air with air under it, which is
  // exactly what used to be refused.
  game_state_set_player_position(state, plateau_x0 + 1, 16, stand_z);
  state->controls.velocity_z = 0.0f;
  report("stepping off the edge into open air is allowed",
         game_state_can_move_to(state, plateau_x0 - 1, 16, stand_z));
  report("walking into a wall is still refused",
         !game_state_can_move_to(state, plateau_x0, 16, lower_top));

  // Walk west off the plateau and let gravity do the rest.
  state->controls.facing_yaw = (float)M_PI; // -x
  state->controls.aim_yaw = state->controls.facing_yaw;
  state->controls.move_forward = true;

  const float edge_x = (float)plateau_x0;
  float x_when_leaving_plateau = state->player_world_x;
  int descent_frames = 0;
  bool moved_horizontally_while_falling = false;
  float previous_x = state->player_world_x;
  float previous_fall_z = state->player_world_z;
  bool crossed_edge = false;

  for (int i = 0; i < 600; i++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);

    if (!crossed_edge && state->player_world_x < edge_x)
    {
      crossed_edge = true;
      x_when_leaving_plateau = state->player_world_x;
    }
    if (state->player_world_z < previous_fall_z - 1e-6f)
    {
      descent_frames++;
      if (state->player_world_x < previous_x - 1e-6f)
        moved_horizontally_while_falling = true;
    }
    previous_x = state->player_world_x;
    previous_fall_z = state->player_world_z;

    if (crossed_edge && state->controls.velocity_z == 0.0f &&
        state->player_world_z < stand_world_z - 1.0f)
      break;
  }
  state->controls.move_forward = false;

  const float landing_z = (float)(lower_top + 1) + 0.5f;
  printf("     left the plateau at x=%.2f, landed at z=%.2f over %d descending frames\n",
         x_when_leaving_plateau, state->player_world_z, descent_frames);
  report("the player walked off the plateau", crossed_edge);
  report("walking off the edge started a fall rather than stopping the player",
         descent_frames > 1);
  report("the fall landed on the lower floor",
         fabsf(state->player_world_z - landing_z) < 1e-3f);
  report("the player kept moving horizontally while falling", moved_horizontally_while_falling);
  report("the player is standing on the lower floor",
         game_state_player_is_grounded(state) && state->player_z == lower_top + 1);

  const int lower_stand_z = lower_top + 1;

  // -------------------------------------------------------------------------------------------
  // Shift run / Ctrl crouch on the ground, and Shift afterburner in spirit hover.
  printf("\n--- run, crouch, afterburner ---\n");
  {
    player_controls_init(&state->controls);
    state->player_flying = false;
    if (state->player)
    {
      state->player->is_flying = false;
      state->player->stamina = PLAYER_DEFAULT_STAMINA_MAX;
    }
    game_state_set_player_position(state, 5, 16, lower_stand_z);
    state->controls.facing_yaw = 0.0f;
    state->controls.aim_yaw = 0.0f;
    state->controls.move_forward = true;

    const float walk_start = state->player_world_x;
    for (int i = 0; i < 45; i++)
      player_controls_apply_wasd(state, &state->controls, dt);
    const float walk_dist = state->player_world_x - walk_start;

    game_state_set_player_position(state, 5, 16, lower_stand_z);
    state->controls.boost = true;
    if (state->player)
      state->player->stamina = PLAYER_DEFAULT_STAMINA_MAX;
    const float run_start = state->player_world_x;
    for (int i = 0; i < 45; i++)
      player_controls_apply_wasd(state, &state->controls, dt);
    const float run_dist = state->player_world_x - run_start;

    game_state_set_player_position(state, 5, 16, lower_stand_z);
    state->controls.boost = false;
    state->controls.move_down = true;
    const float crouch_start = state->player_world_x;
    for (int i = 0; i < 45; i++)
      player_controls_apply_wasd(state, &state->controls, dt);
    const float crouch_dist = state->player_world_x - crouch_start;

    printf("     walk=%.2f run=%.2f crouch=%.2f\n", walk_dist, run_dist, crouch_dist);
    report("Shift run covers more ground than a walk", run_dist > walk_dist * 1.2f);
    report("Ctrl crouch covers less ground than a walk", crouch_dist < walk_dist * 0.85f);
    report("Ctrl crouch while grounded is crouching",
           player_controls_is_crouching(state, &state->controls));
    report("crouch lowers FP eye height",
           player_controls_eye_height(state, &state->controls) < PLAYER_EYE_HEIGHT);

    // Afterburner: spirit hover with Shift moves faster than baseline hover.
    state->controls.move_down = false;
    state->controls.boost = false;
    player_controls_set_spirit_hover(state, true);
    game_state_set_player_position(state, 5, 16, lower_stand_z + 4);
    state->controls.move_forward = true;
    const float hover_start = state->player_world_x;
    for (int i = 0; i < 45; i++)
      player_controls_apply_wasd(state, &state->controls, dt);
    const float hover_dist = state->player_world_x - hover_start;

    game_state_set_player_position(state, 5, 16, lower_stand_z + 4);
    state->controls.boost = true;
    if (state->player)
      state->player->stamina = PLAYER_DEFAULT_STAMINA_MAX;
    const float burn_start = state->player_world_x;
    for (int i = 0; i < 45; i++)
      player_controls_apply_wasd(state, &state->controls, dt);
    const float burn_dist = state->player_world_x - burn_start;
    printf("     hover=%.2f afterburn=%.2f\n", hover_dist, burn_dist);
    report("Shift afterburner outpaces spirit hover", burn_dist > hover_dist * 1.2f);
    state->controls.move_down = true;
    report("Ctrl is not crouch while hovering",
           !player_controls_is_crouching(state, &state->controls));

    player_controls_set_spirit_hover(state, false);
    state->controls.move_forward = false;
    state->controls.boost = false;
    state->controls.move_down = false;
  }

  // -------------------------------------------------------------------------------------------
  // Solid obstacles. A tree trunk or a boulder is a solid voxel like any other, and the player is a
  // cylinder of PLAYER_RADIUS, so the test that matters is whether that cylinder ever ends up
  // overlapping a solid voxel's square. It used to, for any approach that clipped a voxel's edge
  // without passing near its centre, which is why some trees stopped the player and others did not.
  // Sweeping the lateral offset across a whole voxel width is what distinguishes the two.
  printf("\n--- obstacles ---\n");

  const int trunk_x = 8;
  const int trunk_y = 16;
  for (int z = lower_stand_z; z <= lower_stand_z + 3; z++)
    world_set_voxel(arena, (uint32_t)trunk_x, (uint32_t)trunk_y, (uint32_t)z, VOXEL_WOOD);
  world_refresh_occupancy_bitfield(arena);

  // Distance from the player's axis to the nearest point of the trunk's square. Below PLAYER_RADIUS
  // the player is inside the trunk. Computed here independently of the collision code so a mistake
  // in that code cannot also define what counts as passing.
  #define TRUNK_PENETRATION(px, py)                                              \
    (sqrtf(((px) - fminf(fmaxf((px), (float)trunk_x), (float)trunk_x + 1.0f)) *   \
               ((px) - fminf(fmaxf((px), (float)trunk_x), (float)trunk_x + 1.0f)) + \
           ((py) - fminf(fmaxf((py), (float)trunk_y), (float)trunk_y + 1.0f)) *   \
               ((py) - fminf(fmaxf((py), (float)trunk_y), (float)trunk_y + 1.0f))))

  float worst_penetration = 1e9f;
  float worst_offset = 0.0f;
  int approaches_that_passed_through = 0;

  // Offsets spanning the trunk's full width and a little beyond, so both the centre line and the
  // edges that the old check ignored are covered.
  for (int step_i = 0; step_i <= 20; step_i++)
  {
    const float offset = -1.0f + (float)step_i * 0.1f;
    const float approach_y = (float)trunk_y + 0.5f + offset;

    player_controls_init(&state->controls);
    state->controls.velocity_z = 0.0f;
    game_state_set_player_position(state, trunk_x + 6, trunk_y, lower_stand_z);
    state->player_world_y = approach_y;
    game_state_sync_positions(state);

    state->controls.facing_yaw = (float)M_PI; // walk west, straight at the trunk
    state->controls.aim_yaw = state->controls.facing_yaw;
    state->controls.move_forward = true;

    bool passed_through = false;
    for (int i = 0; i < 400; i++)
    {
      player_controls_apply_wasd(state, &state->controls, dt);
      game_state_apply_gravity(state, dt);

      const float pen = TRUNK_PENETRATION(state->player_world_x, state->player_world_y);
      if (pen < worst_penetration)
      {
        worst_penetration = pen;
        worst_offset = offset;
      }
      // West of the trunk with the trunk still between: only reachable by going through it, since
      // the walk never steers around.
      if (state->player_world_x < (float)trunk_x - 0.5f &&
          fabsf(state->player_world_y - ((float)trunk_y + 0.5f)) < 0.5f)
        passed_through = true;
    }
    state->controls.move_forward = false;
    if (passed_through)
      approaches_that_passed_through++;
  }

  printf("     closest approach to the trunk %.3f (radius %.2f) at lateral offset %+.1f\n",
         worst_penetration, PLAYER_RADIUS, worst_offset);
  report("the player never overlaps a solid voxel, from any approach line",
         worst_penetration >= PLAYER_RADIUS - 1e-3f);
  printf("     %d of 21 approaches walked through the trunk\n", approaches_that_passed_through);
  report("no approach line walks through the trunk", approaches_that_passed_through == 0);

  // A hitch. The client's delta time is unclamped, so a world-generation or remesh stall arrives here
  // as one enormous frame; at walking speed a quarter-second frame is already further than the
  // 1 + 2*PLAYER_RADIUS a one-voxel trunk needs in order to be stepped straight over.
  const float hitch_rates[] = {0.25f, 0.5f, 1.0f};
  for (size_t hi = 0; hi < sizeof(hitch_rates) / sizeof(hitch_rates[0]); hi++)
  {
    const float big_dt = hitch_rates[hi];

    player_controls_init(&state->controls);
    state->controls.velocity_z = 0.0f;
    game_state_set_player_position(state, trunk_x + 3, trunk_y, lower_stand_z);
    game_state_sync_positions(state);
    state->controls.facing_yaw = (float)M_PI;
    state->controls.aim_yaw = state->controls.facing_yaw;
    state->controls.move_forward = true;

    float hitch_penetration = 1e9f;
    for (int i = 0; i < 12; i++)
    {
      player_controls_apply_wasd(state, &state->controls, big_dt);
      game_state_apply_gravity(state, big_dt);
      const float pen = TRUNK_PENETRATION(state->player_world_x, state->player_world_y);
      if (pen < hitch_penetration)
        hitch_penetration = pen;
    }
    state->controls.move_forward = false;

    char hitch_label[112];
    snprintf(hitch_label, sizeof(hitch_label),
             "a %.0fms frame does not tunnel through the trunk (stopped %.2f away, x=%.2f)",
             big_dt * 1000.0f, hitch_penetration, state->player_world_x);
    report(hitch_label, hitch_penetration >= PLAYER_RADIUS - 1e-3f &&
                            state->player_world_x > (float)trunk_x);
  }

  // The other half of the same check: collision must not be so eager that the player cannot use a
  // gap they fit through. The player is 2*PLAYER_RADIUS across, so a one-voxel gap leaves room.
  for (int z = lower_stand_z; z <= lower_stand_z + 3; z++)
  {
    world_set_voxel(arena, (uint32_t)trunk_x, (uint32_t)(trunk_y - 1), (uint32_t)z, VOXEL_STONE);
    world_set_voxel(arena, (uint32_t)trunk_x, (uint32_t)(trunk_y + 1), (uint32_t)z, VOXEL_STONE);
    world_set_voxel(arena, (uint32_t)trunk_x, (uint32_t)trunk_y, (uint32_t)z, VOXEL_AIR);
  }
  world_refresh_occupancy_bitfield(arena);

  player_controls_init(&state->controls);
  state->controls.velocity_z = 0.0f;
  game_state_set_player_position(state, trunk_x + 6, trunk_y, lower_stand_z);
  game_state_sync_positions(state);
  state->controls.facing_yaw = (float)M_PI;
  state->controls.aim_yaw = state->controls.facing_yaw;
  state->controls.move_forward = true;
  for (int i = 0; i < 400; i++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
  }
  state->controls.move_forward = false;
  printf("     ended at x=%.2f after walking at a one-voxel gap\n", state->player_world_x);
  report("a gap the player fits through is still passable",
         state->player_world_x < (float)trunk_x - 0.5f);

  #undef TRUNK_PENETRATION

  state->current_world = saved_world;
  player_controls_init(&state->controls);
  world_destroy(arena);

  // -------------------------------------------------------------------------------------------
  // Residency: the shadow world cluster, world streaming, and eviction.
  //
  // This is where a mistake is expensive rather than merely wrong: eviction frees worlds, so a
  // retention region that is too small, or an ownership check that misses a reference, is a
  // use-after-free in the render path. Everything here runs against a real universe.
  // -------------------------------------------------------------------------------------------

  printf("\n--- residency ---\n");

  if (!universe_init(&state->universe, "0123456789abcdef", 0, 1))
  {
    report("universe initialised", false);
    free(state);
    world_destroy(home);
    return 1;
  }

  const uint64_t home_z = (uint64_t)UNIVERSE_HOME_Z;
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = home_z;
  universe_place(&state->universe, 0, 0, home_z, home);

  // A GameWorlds that holds the home world, exactly as the real one does, so the ownership check
  // has something to protect.
  GameWorlds *gw = (GameWorlds *)calloc(1, sizeof(GameWorlds));
  gw->home_world = home;
  gw->base_seed = strdup("0123456789abcdef");
  state->game_worlds = gw;

  game_state_sync_shadow_world(state);
  report("shadow world is created on first sync", state->shadow_world != NULL);
  report("the centre slot holds the world the player is in",
         shadow_world_slot_world(state->shadow_world, SHADOW_CENTRE_SLOT) == home);
  report("a cell that was never placed samples UNLOADED rather than empty",
         shadow_world_sample(state->shadow_world, 5, 5, 5) == SHADOW_UNLOADED);

  // A neighbour with one known voxel, to prove the cluster reads across a world boundary.
  World *east = world_create(WORLD_SIZE_CUBE);
  world_set_voxel(east, 3, 4, 5, VOXEL_STONE);
  world_refresh_occupancy_bitfield(east);
  universe_place(&state->universe, 1, 0, home_z, east);
  game_state_sync_shadow_world(state);

  const int east_slot = shadow_slot_index(1, 0, 0);
  report("the eastern neighbour lands in the eastern slot",
         shadow_world_slot_world(state->shadow_world, east_slot) == east);
  report("a voxel in the neighbour is visible through cluster coordinates",
         shadow_world_sample(state->shadow_world,
                             (SHADOW_CLUSTER_RADIUS + 1) * (int)east->width + 3,
                             SHADOW_CLUSTER_RADIUS * (int)east->height + 4,
                             SHADOW_CLUSTER_RADIUS * (int)east->depth + 5) == SHADOW_SOLID);

  // Eviction. One world inside the retention region and one well outside it. Retention has to cover
  // the renderer's furthest reach, so "inside" goes out to two layers.
  World *near_world = world_create(WORLD_SIZE_CUBE);
  World *far_world = world_create(WORLD_SIZE_CUBE);
  universe_place(&state->universe, 0, 0, home_z + 2, near_world); // dz +2: inside
  universe_place(&state->universe, 0, 0, home_z + 5, far_world);  // dz +5: outside

  const int evicted = game_state_evict_distant_worlds(state);
  report("the distant world is evicted", evicted == 1);
  report("the distant cell is gone from the universe",
         universe_get(&state->universe, 0, 0, home_z + 5) == NULL);
  report("a world inside the retention region is kept",
         universe_get(&state->universe, 0, 0, home_z + 2) == near_world);
  report("the world the player is standing in is never evicted",
         universe_get(&state->universe, 0, 0, home_z) == home);

  // The home world is both in the universe and owned by GameWorlds. Moving the player away from it
  // must not free it, or game_worlds_destroy would free it a second time.
  state->player_universe_z = home_z + 9;
  state->current_world = near_world;
  game_state_sync_shadow_world(state);
  game_state_evict_distant_worlds(state);
  report("a world owned by GameWorlds survives eviction even when far away",
         gw->home_world == home && home->voxels != NULL);
  report("an unowned world with nothing referencing it is evicted",
         universe_get(&state->universe, 1, 0, home_z) == NULL);

  state->player_universe_z = home_z;
  state->current_world = home;
  game_state_sync_shadow_world(state);

  // Streaming. Faces before corners, several jobs at a time but never more than the cap and never
  // two for the same cell, and each world arrives ready to query rather than needing a pass on the
  // main thread. Only a few are pumped: each is around 100MB, so filling the whole 3x3x3 would be
  // gigabytes for no extra coverage.
  //
  // A pool is started for this section specifically. Without one, cell_gen_job_start generates
  // inline and returns an already-finished job, so only ever one would be outstanding and the two
  // concurrency invariants below would hold without being tested.
  const bool pooled = task_scheduler_init(4);
  report("worker pool available for the streaming checks", pooled);

  int streamed = 0;
  bool all_ready = true;
  bool within_cap = true;
  bool no_duplicate_cells = true;

  // Paced, not spun: with a pool the generation is asynchronous and a 128^3 world takes a good
  // fraction of a second, so twenty thousand back-to-back pumps would all return before the first
  // world existed. One pump per millisecond is roughly a frame's rate and bounds this at 30s.
  for (int i = 0; i < 30000 && streamed < 3; i++)
  {
    game_state_pump_world_streaming(state);
    usleep(1000);

    int in_flight = 0;
    for (int a = 0; a < WORLD_STREAM_MAX_IN_FLIGHT; a++)
    {
      if (!state->cell_refill_jobs[a])
        continue;
      in_flight++;

      // Two jobs for one cell means one of the two generated worlds gets thrown away, which is a
      // whole world's worth of work wasted and a core taken from something useful.
      uint64_t ax, ay, az;
      cell_gen_job_cell(state->cell_refill_jobs[a], &ax, &ay, &az);
      for (int b = a + 1; b < WORLD_STREAM_MAX_IN_FLIGHT; b++)
      {
        if (!state->cell_refill_jobs[b])
          continue;
        uint64_t bx, by, bz;
        cell_gen_job_cell(state->cell_refill_jobs[b], &bx, &by, &bz);
        if (ax == bx && ay == by && az == bz)
          no_duplicate_cells = false;
      }
    }
    if (in_flight > WORLD_STREAM_MAX_IN_FLIGHT)
      within_cap = false;

    // The face neighbours of the player's cell, which is what the ordering promises comes first.
    static const int faces[6][3] = {{0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0},
                                    {1, 0, 0}, {-1, 0, 0}};
    int present = 0;
    for (int fi = 0; fi < 6; fi++)
    {
      World *w = universe_get(&state->universe,
                              (uint64_t)((int64_t)0 + faces[fi][0]),
                              (uint64_t)((int64_t)0 + faces[fi][1]),
                              (uint64_t)((int64_t)home_z + faces[fi][2]));
      if (!w)
        continue;
      present++;
      if (!w->occupancy_bits)
        all_ready = false;
    }
    streamed = present;
  }

  report("streaming fills the player's face neighbours first", streamed >= 3);
  report("a streamed world arrives with its occupancy bitfield already built", all_ready);
  report("never more generations in flight than the cap allows", within_cap);
  report("no two in-flight generations target the same cell", no_duplicate_cells);

  // Nothing left to do once the streaming region is served: a pump then starts no work at all.
  for (int i = 0; i < 30000 && game_state_stream_jobs_in_flight(state) > 0; i++)
  {
    game_state_pump_world_streaming(state);
    usleep(1000);
  }
  const int corners_before = (int)state->universe.count;
  state->player_universe_z = home_z + 40; // nowhere near anything, so every neighbour is missing
  game_state_pump_world_streaming(state);
  report("moving the player makes streaming target the new neighbourhood",
         game_state_stream_jobs_in_flight(state) > 0 ||
             (int)state->universe.count > corners_before);
  state->player_universe_z = home_z;

  // Tear down in the order the client does: the cluster borrows from the universe, so it goes first.
  shadow_world_destroy(state->shadow_world);
  state->shadow_world = NULL;
  for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
  {
    if (state->cell_refill_jobs[i])
    {
      cell_gen_job_destroy(state->cell_refill_jobs[i]);
      state->cell_refill_jobs[i] = NULL;
    }
  }
  task_scheduler_shutdown();

  for (size_t i = 0; i < state->universe.capacity; i++)
  {
    if (!state->universe.entries[i].used)
      continue;
    World *w = state->universe.entries[i].w;
    if (w && w != home)
      world_destroy(w);
  }
  universe_free(&state->universe);
  free(gw->base_seed);
  free(gw);
  state->game_worlds = NULL;

  free(state);
  world_destroy(home);

  printf("\n=== %s (%d failure%s) ===\n", test_failures == 0 ? "ALL PASSED" : "FAILURES",
         test_failures, test_failures == 1 ? "" : "s");
  return test_failures == 0 ? 0 : 1;
}
