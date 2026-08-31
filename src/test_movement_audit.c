// Whether a body can move where it looks like it can, and is never trapped where it should not be.
//
// The complaint this exists for is "characters get stuck in the mesh or otherwise cannot move". That
// is not one bug, so this does not test one thing. It drives the real movement code — the same
// player_controls_apply_wasd and game_state_apply_gravity the client calls every frame — across
// terrain built to contain the shapes a voxel world actually produces, and asserts two invariants
// after every single frame:
//
//   1. The player is never inside a voxel that blocks movement. Once that happens every candidate
//      position overlaps the same solid voxel, so nothing can move and the only way out is a reload.
//   2. A player who is not walled in can always get somewhere. Being blocked in the direction you
//      are pushing is fine; having no reachable position at all is the bug.
//
// The terrain is small and hand-built rather than generated, so the whole suite runs in well under a
// second and a failure names a shape rather than a seed.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "constants.h"
#include "game_state.h"
#include "player_controls.h"
#include "shadow_world.h"
#include "universe.h"
#include "world.h"

// The client's UI hooks, unused here. Nothing in this suite renders.
GameState *g_game_state = NULL;
int g_show_exit_prompt = 0;
Console *g_console = NULL;
void handle_button_click(int button_id) { (void)button_id; }
void play_highlight_sound(void) {}

static int test_failures = 0;

static void report(const char *name, bool passed)
{
  printf("  %s %s\n", passed ? "ok  " : "FAIL", name);
  if (!passed)
    test_failures++;
}

// ---------------------------------------------------------------------------------------------
// Scaffolding

#define W 32
#define H 32
#define D 32
#define GROUND_TOP 6 // solid through z=6, so a standing player's feet are in voxel 7

static World *make_ground(void)
{
  World *w = world_create(W, H, D);
  if (!w)
    return NULL;
  for (uint32_t z = 0; z <= GROUND_TOP; z++)
    for (uint32_t y = 0; y < H; y++)
      for (uint32_t x = 0; x < W; x++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);
  return w;
}

static void column(World *w, uint32_t x, uint32_t y, uint32_t from_z, uint32_t to_z, VoxelType t)
{
  for (uint32_t z = from_z; z <= to_z; z++)
    world_set_voxel(w, x, y, z, t);
}

static GameState *make_state(World *world)
{
  GameState *state = (GameState *)calloc(1, sizeof(GameState));
  if (!state)
    return NULL;
  if (!universe_init(&state->universe, "0123456789abcdef", 0, 1))
  {
    free(state);
    return NULL;
  }
  GameWorlds *gw = (GameWorlds *)calloc(1, sizeof(GameWorlds));
  gw->home_world = world;
  gw->base_seed = strdup("0123456789abcdef");
  state->game_worlds = gw;
  state->current_world = world;
  state->game_started = true;
  state->player_flying = false;
  // Keeps the streamer from generating full-size neighbours for terrain this suite hand-builds.
  state->world_generation_active = true;
  strncpy(state->player_name, "walker", sizeof(state->player_name) - 1);
  player_controls_init(&state->controls);
  return state;
}

static void destroy_state(GameState *state)
{
  if (!state)
    return;
  if (state->shadow_world)
    shadow_world_destroy(state->shadow_world);
  universe_free(&state->universe);
  if (state->game_worlds)
  {
    free(state->game_worlds->base_seed);
    free(state->game_worlds);
  }
  free(state);
}

// The failure mode that matters most: the player's own body overlapping something solid.
static bool embedded(GameState *state)
{
  World *world = state->current_world;
  if (!world)
    return false;
  Voxel *v = world_get_voxel(world, state->player_voxel_x, state->player_voxel_y,
                             state->player_voxel_z);
  return v && world_voxel_type_blocks_movement(v->type);
}

// Whether any of the eight compass directions is open, at the player's height or a step above it. A
// player who answers no to all of them cannot move at all, whatever they press.
//
// The step up counts, because on a height field it is how most ground is reached: a body that can
// only ever move level is trapped nearly everywhere out there, and asking the question without it
// would call a working player stuck.
static bool has_anywhere_to_go(GameState *state)
{
  static const float dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                   {0.707f, 0.707f}, {0.707f, -0.707f},
                                   {-0.707f, 0.707f}, {-0.707f, -0.707f}};
  const float probe = 0.12f; // shorter than one collision substep, so this is a real next position
  for (int i = 0; i < 8; i++)
  {
    const float tx = state->player_world_x + dirs[i][0] * probe;
    const float ty = state->player_world_y + dirs[i][1] * probe;
    if (player_controls_can_occupy(state, tx, ty, state->player_world_z))
      return true;
    if (player_controls_can_occupy(state, tx, ty, state->player_world_z + PLAYER_STEP_UP_VOXELS) &&
        player_controls_can_occupy(state, state->player_world_x, state->player_world_y,
                                   state->player_world_z + PLAYER_STEP_UP_VOXELS))
      return true;
  }
  return false;
}

static void clear_input(PlayerControls *c)
{
  c->move_forward = c->move_backward = c->move_left = c->move_right = false;
  c->move_up = c->move_down = c->boost = false;
}

// ---------------------------------------------------------------------------------------------
// Landing

// A jump on flat ground, which is the least exotic thing a player can do. The fall steps downward a
// voxel at a time to avoid passing through a floor, and the step is taken from wherever the player
// happens to be rather than from a voxel boundary — so this checks the landing height exactly, not
// just that the player stopped.
static void test_jump_and_land_on_flat_ground(void)
{
  printf("\n-- a jump lands on the ground, not in it --\n");

  World *world = make_ground();
  GameState *state = world ? make_state(world) : NULL;
  if (!state)
  {
    report("flat ground scenario built", false);
    world_destroy(world);
    return;
  }
  g_game_state = state;

  game_state_set_player_position(state, 16, 16, GROUND_TOP + 1);
  state->controls.velocity_z = 0.0f;

  report("the player starts standing", game_state_player_is_grounded(state));

  const float dt = 1.0f / 60.0f;
  state->controls.velocity_z = game_state_player_jump_velocity(state);

  bool ever_embedded = false;
  bool left_the_ground = false;
  float lowest_z = state->player_world_z;
  for (int frame = 0; frame < 400; frame++)
  {
    game_state_apply_gravity(state, dt);
    if (state->player_world_z > (float)GROUND_TOP + 1.5f + 0.05f)
      left_the_ground = true;
    if (state->player_world_z < lowest_z)
      lowest_z = state->player_world_z;
    if (embedded(state))
      ever_embedded = true;
  }

  printf("       came to rest at z=%.3f (expected %.3f), lowest z reached %.3f\n",
         state->player_world_z, (float)GROUND_TOP + 1.5f, lowest_z);
  report("the jump left the ground", left_the_ground);
  report("the player never ends up inside the floor", !ever_embedded);
  report("the player comes to rest on top of the ground",
         fabsf(state->player_world_z - ((float)GROUND_TOP + 1.5f)) < 1e-3f);
  report("the player can still move after landing", has_anywhere_to_go(state));

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(world);
}

// The same landing, but reached at speed from high up, which is what falling off the island is. A
// fast frame moves the feet more than a voxel, so the descent takes several steps rather than one,
// and each step has to ask about the voxel the feet are actually crossing.
static void test_fast_fall_lands_on_the_surface(void)
{
  printf("\n-- a fast fall stops on the surface it crosses --\n");

  // Every starting height in the sweep is a different sub-voxel offset, because the bug this is
  // looking for depends on the fractional part of the player's height, not on the height itself.
  const float offsets[] = {0.0f, 0.1f, 0.25f, 0.4f, 0.5f, 0.6f, 0.75f, 0.9f, 0.99f};
  const int n_offsets = (int)(sizeof(offsets) / sizeof(offsets[0]));
  const float dt = 1.0f / 20.0f; // a bad frame, which is when a fall covers several voxels at once

  int embedded_count = 0, wrong_rest = 0;
  float first_bad_offset = -1.0f, first_bad_z = 0.0f;

  for (int i = 0; i < n_offsets; i++)
  {
    World *world = make_ground();
    GameState *state = world ? make_state(world) : NULL;
    if (!state)
    {
      report("fast fall scenario built", false);
      world_destroy(world);
      return;
    }
    g_game_state = state;

    game_state_set_player_position(state, 16, 16, 26);
    state->player_world_z = 26.0f + offsets[i];
    game_state_sync_positions(state);
    state->controls.velocity_z = -30.0f; // already moving, so the first frame covers ~1.5 voxels

    bool ever_embedded = false;
    for (int frame = 0; frame < 200; frame++)
    {
      game_state_apply_gravity(state, dt);
      if (embedded(state))
        ever_embedded = true;
    }

    const bool rest_ok = fabsf(state->player_world_z - ((float)GROUND_TOP + 1.5f)) < 1e-3f;
    if (ever_embedded)
      embedded_count++;
    if (!rest_ok)
      wrong_rest++;
    if ((ever_embedded || !rest_ok) && first_bad_offset < 0.0f)
    {
      first_bad_offset = offsets[i];
      first_bad_z = state->player_world_z;
    }

    g_game_state = NULL;
    destroy_state(state);
    world_destroy(world);
  }

  if (first_bad_offset >= 0.0f)
    printf("       first failure from start height 26 + %.2f: came to rest at z=%.3f (expected %.3f)\n",
           first_bad_offset, first_bad_z, (float)GROUND_TOP + 1.5f);
  printf("       %d of %d sub-voxel start heights ended inside the floor\n", embedded_count,
         n_offsets);
  report("a fast fall never ends inside the floor", embedded_count == 0);
  report("a fast fall rests on top of the ground from every start height", wrong_rest == 0);
}

// ---------------------------------------------------------------------------------------------
// Walking

// Terrain with the shapes that a generated world puts in the player's way: a tree trunk, a boulder,
// a wall with a doorway, a one-voxel step, and an overhang low enough to matter.
static World *make_obstacle_course(void)
{
  World *w = make_ground();
  if (!w)
    return NULL;

  const uint32_t feet = GROUND_TOP + 1;

  // A tree: one-voxel trunk with a canopy the player is meant to walk through.
  column(w, 10, 16, feet, feet + 4, VOXEL_WOOD);
  for (uint32_t y = 15; y <= 17; y++)
    for (uint32_t x = 9; x <= 11; x++)
      column(w, x, y, feet + 5, feet + 6, VOXEL_LEAVES_OAK);

  // A boulder two voxels across, so its far side is not reachable by clipping a corner.
  for (uint32_t y = 20; y <= 21; y++)
    for (uint32_t x = 14; x <= 15; x++)
      column(w, x, y, feet, feet + 1, VOXEL_STONE);

  // A wall across the world with a two-voxel doorway, which is what a player has to fit through.
  for (uint32_t x = 0; x < W; x++)
    if (x < 20 || x > 21)
      column(w, x, 25, feet, feet + 3, VOXEL_STONE);

  // A one-voxel step up, the height a body should be able to get onto.
  for (uint32_t y = 4; y <= 6; y++)
    for (uint32_t x = 20; x <= 26; x++)
      world_set_voxel(w, x, y, feet, VOXEL_STONE);

  // Tall grass and water, both of which are meant to be walked through rather than into.
  column(w, 6, 10, feet, feet, VOXEL_GRASS_TALL);
  column(w, 7, 10, feet, feet, VOXEL_WATER);

  world_refresh_occupancy_bitfield(w);
  return w;
}

// Pushes the player around the obstacle course for a long time, changing direction and jumping on a
// fixed schedule, and watches the two invariants every frame. Deterministic: the same sequence of
// inputs every run, so a failure is reproducible.
static void test_walking_never_traps_the_player(void)
{
  printf("\n-- walking the obstacle course never traps the player --\n");

  World *world = make_obstacle_course();
  GameState *state = world ? make_state(world) : NULL;
  if (!state)
  {
    report("obstacle course built", false);
    world_destroy(world);
    return;
  }
  g_game_state = state;

  game_state_set_player_position(state, 16, 12, GROUND_TOP + 1);
  state->controls.velocity_z = 0.0f;

  const float dt = 1.0f / 60.0f;
  const int frames = 4000;

  int embedded_frames = 0;
  int nowhere_to_go_frames = 0;
  float first_embedded[3] = {0, 0, 0};
  bool have_embedded = false;
  float first_trapped[3] = {0, 0, 0};
  bool have_trapped = false;

  // A cheap deterministic walk: hold a heading for a stretch, then turn. Covers approaching every
  // obstacle from a spread of angles rather than only along the axes.
  uint32_t rng = 0x9E3779B9u;
  for (int frame = 0; frame < frames; frame++)
  {
    if (frame % 37 == 0)
    {
      rng = rng * 1664525u + 1013904223u;
      state->controls.facing_yaw = (float)((rng >> 8) % 360) * (3.14159265f / 180.0f);
      clear_input(&state->controls);
      state->controls.move_forward = true;
    }
    if (frame % 211 == 0)
      state->controls.velocity_z = game_state_player_jump_velocity(state);

    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);

    if (embedded(state))
    {
      embedded_frames++;
      if (!have_embedded)
      {
        have_embedded = true;
        first_embedded[0] = state->player_world_x;
        first_embedded[1] = state->player_world_y;
        first_embedded[2] = state->player_world_z;
      }
    }
    if (!has_anywhere_to_go(state))
    {
      nowhere_to_go_frames++;
      if (!have_trapped)
      {
        have_trapped = true;
        first_trapped[0] = state->player_world_x;
        first_trapped[1] = state->player_world_y;
        first_trapped[2] = state->player_world_z;
      }
    }
  }

  printf("       %d frames simulated, ended at (%.2f, %.2f, %.2f)\n", frames,
         state->player_world_x, state->player_world_y, state->player_world_z);
  if (have_embedded)
    printf("       first embedded at (%.2f, %.2f, %.2f)\n", first_embedded[0], first_embedded[1],
           first_embedded[2]);
  if (have_trapped)
    printf("       first with nowhere to go at (%.2f, %.2f, %.2f)\n", first_trapped[0],
           first_trapped[1], first_trapped[2]);
  printf("       %d frames inside solid, %d frames with no open direction\n", embedded_frames,
         nowhere_to_go_frames);

  report("the player is never inside a solid voxel", embedded_frames == 0);
  report("the player always has somewhere to go", nowhere_to_go_frames == 0);

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(world);
}

// Walking through what is meant to be walked through, and not through what is not. These are the
// cases where collision and the renderer have to agree, or the world lies about itself.
static void test_what_blocks_and_what_does_not(void)
{
  printf("\n-- foliage and fluid let a body through, solids do not --\n");

  World *world = make_obstacle_course();
  GameState *state = world ? make_state(world) : NULL;
  if (!state)
  {
    report("obstacle course built", false);
    world_destroy(world);
    return;
  }
  g_game_state = state;

  const float feet = (float)GROUND_TOP + 1.5f;

  report("tall grass is passable",
         player_controls_can_occupy(state, 6.5f, 10.5f, feet));
  report("water is passable",
         player_controls_can_occupy(state, 7.5f, 10.5f, feet));
  report("a leaf canopy is passable",
         game_state_can_move_to(state, 10, 16, GROUND_TOP + 6));
  report("a tree trunk is not passable",
         !player_controls_can_occupy(state, 10.5f, 16.5f, feet));
  report("a boulder is not passable",
         !player_controls_can_occupy(state, 14.5f, 20.5f, feet));
  report("a wall is not passable",
         !player_controls_can_occupy(state, 8.5f, 25.5f, feet));
  report("the doorway in that wall is passable",
         player_controls_can_occupy(state, 20.5f, 25.5f, feet) ||
             player_controls_can_occupy(state, 21.5f, 25.5f, feet));

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(world);
}

// The edge of the world. The island is meant to be walkable to its rim and then fallen off, so a
// player must be able to stand on the last voxel of a world rather than being fenced short of it.
static void test_the_player_can_reach_the_world_edge(void)
{
  printf("\n-- the last voxel of a world is standable --\n");

  World *world = make_ground();
  GameState *state = world ? make_state(world) : NULL;
  if (!state)
  {
    report("edge scenario built", false);
    world_destroy(world);
    return;
  }
  g_game_state = state;

  const float feet = (float)GROUND_TOP + 1.5f;
  report("the centre of the last voxel in x is standable",
         player_controls_can_occupy(state, (float)W - 0.5f, 16.5f, feet));
  report("the centre of the last voxel in y is standable",
         player_controls_can_occupy(state, 16.5f, (float)H - 0.5f, feet));
  report("the centre of the first voxel in x is standable",
         player_controls_can_occupy(state, 0.5f, 16.5f, feet));
  report("the centre of the first voxel in y is standable",
         player_controls_can_occupy(state, 16.5f, 0.5f, feet));

  // Walk east into the boundary for long enough to arrive, and check where the player ends up.
  game_state_set_player_position(state, W - 5, 16, GROUND_TOP + 1);
  state->controls.facing_yaw = 0.0f; // +x
  clear_input(&state->controls);
  state->controls.move_forward = true;
  const float dt = 1.0f / 60.0f;
  for (int frame = 0; frame < 600; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
  }
  printf("       walking east ended at x=%.3f (last voxel centre is %.1f)\n",
         state->player_world_x, (float)W - 0.5f);
  report("walking east reaches the last voxel", state->player_world_x >= (float)W - 0.5f);

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(world);
}

// ---------------------------------------------------------------------------------------------
// Falling off the island, all the way down

#define ISLAND_TOP 12

// A plateau in the middle of an otherwise empty world, which is the shape of the home island: solid
// in the middle, open air all around and below it.
static World *make_island(void)
{
  World *w = world_create(W, H, D);
  if (!w)
    return NULL;
  w->generation_type = WORLD_TYPE_HOME;
  for (uint32_t y = 12; y <= 19; y++)
    for (uint32_t x = 12; x <= 19; x++)
      for (uint32_t z = 0; z <= ISLAND_TOP; z++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);
  world_refresh_occupancy_bitfield(w);
  return w;
}

static World *make_cloud(void)
{
  World *w = world_create(W, H, D);
  if (!w)
    return NULL;
  w->generation_type = WORLD_TYPE_CLOUD;
  // Wisps at several heights, including one in the very lowest layer, which is the case that used to
  // read as a solid floor and strand a falling player on the clouds.
  for (uint32_t z = 0; z < D; z += 5)
    for (uint32_t y = 0; y < H; y++)
      for (uint32_t x = 0; x < W; x++)
        world_set_voxel(w, x, y, z, VOXEL_STEAM);
  world_refresh_occupancy_bitfield(w);
  return w;
}

static World *make_wilderness(void)
{
  World *w = world_create(W, H, D);
  if (!w)
    return NULL;
  w->generation_type = WORLD_TYPE_WILDERNESS;
  for (uint32_t z = 0; z <= GROUND_TOP; z++)
    for (uint32_t y = 0; y < H; y++)
      for (uint32_t x = 0; x < W; x++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);
  world_refresh_occupancy_bitfield(w);
  return w;
}

// The wilderness is meant to be a plane of wilderness worlds, so walking across it has to cross from
// one world into the next. Before this the player was fenced at the bounds of whichever world they
// stood in, which on a plane that is supposed to go on is an invisible wall a fraction of a voxel
// short of the rim.
static void test_walking_from_one_world_into_the_next(void)
{
  printf("\n-- walking east crosses into the eastern world --\n");

  World *centre = make_ground();
  World *east = make_ground();
  World *north = make_ground();
  GameState *state = centre ? make_state(centre) : NULL;
  if (!centre || !east || !north || !state)
  {
    report("neighbour scenario built", false);
    world_destroy(centre);
    world_destroy(east);
    world_destroy(north);
    destroy_state(state);
    return;
  }
  g_game_state = state;

  // A marker in the eastern world, so arriving there is checked by reading its terrain rather than
  // only by trusting the bookkeeping.
  world_set_voxel(east, 2, 16, GROUND_TOP + 1, VOXEL_STONE);
  world_refresh_occupancy_bitfield(east);

  const uint64_t hz = (uint64_t)UNIVERSE_HOME_Z;
  universe_place(&state->universe, 0, 0, hz, centre);
  universe_place(&state->universe, 1, 0, hz, east);
  universe_place(&state->universe, 0, 1, hz, north);
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = hz;

  const float dt = 1.0f / 60.0f;

  // Collision has to see the neighbour's terrain through the seam, before any crossing happens.
  game_state_set_player_position(state, W - 2, 16, GROUND_TOP + 1);
  report("the neighbour's ground is walkable through the seam",
         game_state_can_move_to(state, W + 1, 16, GROUND_TOP + 1));
  report("the neighbour's marker blocks through the seam",
         !game_state_can_move_to(state, W + 2, 16, GROUND_TOP + 1));

  // Now walk east across the boundary.
  state->controls.facing_yaw = 0.0f;
  clear_input(&state->controls);
  state->controls.move_forward = true;
  state->controls.velocity_z = 0.0f;

  bool embedded_anywhere = false;
  for (int frame = 0; frame < 600; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
    if (embedded(state))
      embedded_anywhere = true;
    if (state->player_universe_x == 1)
      break;
  }

  printf("       ended in universe cell (%llu, %llu) at x=%.2f\n",
         (unsigned long long)state->player_universe_x,
         (unsigned long long)state->player_universe_y, state->player_world_x);
  report("the player crossed into the eastern cell", state->player_universe_x == 1);
  report("the player's world is now the eastern world", state->current_world == east);
  report("the position was rebased into the new world's coordinates",
         state->player_world_x >= 0.0f && state->player_world_x < (float)W);
  report("the player is still standing after crossing", game_state_player_is_grounded(state));
  report("the player was never embedded while crossing", !embedded_anywhere);
  report("the player can keep moving in the new world", has_anywhere_to_go(state));

  // And back the way they came, which is the seam in the other direction.
  state->controls.facing_yaw = 3.14159265f; // -x
  clear_input(&state->controls);
  state->controls.move_forward = true;
  for (int frame = 0; frame < 900; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
    if (state->player_universe_x == 0)
      break;
  }
  printf("       walking back ended in cell (%llu, %llu) at x=%.2f\n",
         (unsigned long long)state->player_universe_x,
         (unsigned long long)state->player_universe_y, state->player_world_x);
  report("walking west crosses back into the centre cell", state->player_universe_x == 0);
  report("the player's world is the centre world again", state->current_world == centre);

  // North, to check the other axis is wired the same way.
  state->controls.facing_yaw = 1.57079633f; // +y
  clear_input(&state->controls);
  state->controls.move_forward = true;
  for (int frame = 0; frame < 900; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
    if (state->player_universe_y == 1)
      break;
  }
  printf("       walking north ended in cell (%llu, %llu) at y=%.2f\n",
         (unsigned long long)state->player_universe_x,
         (unsigned long long)state->player_universe_y, state->player_world_y);
  report("walking north crosses into the northern cell", state->player_universe_y == 1);
  report("the player's world is the northern world", state->current_world == north);

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(centre);
  world_destroy(east);
  world_destroy(north);
}

// A seam with nothing beyond it yet has to read as a wall, not as a hole: losing the streaming race
// should stop the player at the rim rather than drop them outside every world.
static void test_an_unstreamed_seam_is_a_wall(void)
{
  printf("\n-- a seam with no world beyond it stops the player --\n");

  World *centre = make_ground();
  GameState *state = centre ? make_state(centre) : NULL;
  if (!centre || !state)
  {
    report("lone world scenario built", false);
    world_destroy(centre);
    destroy_state(state);
    return;
  }
  g_game_state = state;

  const uint64_t hz = (uint64_t)UNIVERSE_HOME_Z;
  universe_place(&state->universe, 0, 0, hz, centre);
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = hz;
  // No base seed, so nothing can be generated to fill the gap on demand.
  free(state->game_worlds->base_seed);
  state->game_worlds->base_seed = NULL;

  game_state_set_player_position(state, W - 3, 16, GROUND_TOP + 1);
  state->controls.facing_yaw = 0.0f;
  clear_input(&state->controls);
  state->controls.move_forward = true;

  const float dt = 1.0f / 60.0f;
  for (int frame = 0; frame < 600; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
  }

  printf("       stopped at x=%.3f (world is %d wide), still in cell (%llu, %llu)\n",
         state->player_world_x, W, (unsigned long long)state->player_universe_x,
         (unsigned long long)state->player_universe_y);
  report("the player stayed in the only world there is", state->player_universe_x == 0);
  report("the player is still inside that world's bounds",
         state->player_world_x >= 0.0f && state->player_world_x < (float)W);
  report("the player is still standing", game_state_player_is_grounded(state));
  report("the player is not embedded", !embedded(state));

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(centre);
}

// The whole point of the layout: walk off the island under your own steam and end up on the
// wilderness two layers below, having passed through the clouds without stopping on them.
static void test_walking_off_the_island_falls_to_the_wilderness(void)
{
  printf("\n-- walking off the island falls through cloud onto wilderness --\n");

  World *home = make_island();
  World *cloud = make_cloud();
  World *wild = make_wilderness();
  GameState *state = home ? make_state(home) : NULL;
  if (!home || !cloud || !wild || !state)
  {
    report("fall scenario built", false);
    world_destroy(home);
    world_destroy(cloud);
    world_destroy(wild);
    destroy_state(state);
    return;
  }
  g_game_state = state;

  universe_place(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z, home);
  universe_place(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z - 1, cloud);
  universe_place(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z - 2, wild);
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = (uint64_t)UNIVERSE_HOME_Z;

  // Standing on the plateau, two voxels in from its eastern rim, facing out over the edge.
  game_state_set_player_position(state, 18, 16, ISLAND_TOP + 1);
  state->controls.velocity_z = 0.0f;
  state->controls.facing_yaw = 0.0f; // +x, toward the rim at x=19
  clear_input(&state->controls);
  state->controls.move_forward = true;

  report("the player starts standing on the island", game_state_player_is_grounded(state));

  const float dt = 1.0f / 60.0f;
  bool reached_cloud = false, embedded_anywhere = false;
  int frames_to_wilderness = -1;

  for (int frame = 0; frame < 1200; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);

    if (embedded(state))
      embedded_anywhere = true;
    if (state->player_universe_z == (uint64_t)UNIVERSE_HOME_Z - 1)
      reached_cloud = true;
    if (state->player_universe_z == (uint64_t)UNIVERSE_HOME_Z - 2)
    {
      frames_to_wilderness = frame + 1;
      break;
    }
  }

  printf("       reached the wilderness layer after %d frames, at (%.2f, %.2f, %.2f)\n",
         frames_to_wilderness, state->player_world_x, state->player_world_y,
         state->player_world_z);
  report("walking forward carried the player off the island", state->player_universe_z !=
                                                                  (uint64_t)UNIVERSE_HOME_Z);
  report("the fall passed through the cloud layer", reached_cloud);
  report("the fall ended on the wilderness layer", frames_to_wilderness > 0);
  report("the player is in the wilderness world", state->current_world == wild);
  report("the player was never inside solid on the way down", !embedded_anywhere);

  // And having arrived, they are standing and can walk.
  for (int frame = 0; frame < 240; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
  }
  printf("       resting at z=%.3f on wilderness whose surface is z=%d\n", state->player_world_z,
         GROUND_TOP);
  report("the player comes to rest on the wilderness surface",
         state->player_voxel_z == GROUND_TOP + 1);
  report("the player is grounded there", game_state_player_is_grounded(state));
  report("the player can move on arrival", has_anywhere_to_go(state));
  report("the player is not embedded on arrival", !embedded(state));

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(home);
  world_destroy(cloud);
  world_destroy(wild);
}

// Wilderness neighbours have to be on the renderer's edge list while standing on the sky island
// (distance terrain) and while falling toward that plane. Landing keeps same-layer neighbours too.
static void test_wilderness_neighbours_are_drawn_while_falling(void)
{
  printf("\n-- wilderness neighbours are drawn from the sky island and while falling --\n");

  World *home = make_island();
  World *cloud = make_cloud();
  World *wild = make_wilderness();
  World *east = make_wilderness();
  GameState *state = home ? make_state(home) : NULL;
  IsometricRenderer *renderer = isometric_renderer_create(64, 64);
  if (!home || !cloud || !wild || !east || !state || !renderer)
  {
    report("fall-visibility scenario built", false);
    world_destroy(home);
    world_destroy(cloud);
    world_destroy(wild);
    world_destroy(east);
    destroy_state(state);
    if (renderer)
      isometric_renderer_destroy(renderer);
    return;
  }
  g_game_state = state;

  const uint64_t hz = (uint64_t)UNIVERSE_HOME_Z;
  const uint64_t wz = hz - 2;
  universe_place(&state->universe, 0, 0, hz, home);
  universe_place(&state->universe, 0, 0, hz - 1, cloud);
  universe_place(&state->universe, 0, 0, wz, wild);
  universe_place(&state->universe, 1, 0, wz, east);
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = hz;

  game_state_set_player_position(state, 16, 16, ISLAND_TOP + 1);
  game_state_sync_isometric_renderer(state, renderer);

  const int below = isometric_renderer_offset_index(renderer, 0, 0, -2);
  const int below_east = isometric_renderer_offset_index(renderer, 1, 0, -2);
  report("wilderness is two layers below home in the offset table", below > 0 && below_east > 0);
  report("wilderness scenery is not hidden while standing on the island",
         !renderer->hide_wilderness_below_home);
  report("the wilderness under the island is attached while standing",
         below > 0 && renderer->edge_worlds[below] == wild);
  report("the eastern wilderness is attached while standing",
         below_east > 0 && renderer->edge_worlds[below_east] == east);

  // Off the plateau, still in the home cell, falling through empty air.
  game_state_set_player_position(state, 25, 16, ISLAND_TOP + 1);
  game_state_sync_isometric_renderer(state, renderer);

  report("wilderness stays shown once the player is falling off the island",
         !renderer->hide_wilderness_below_home);
  report("the wilderness under the island is attached while falling",
         below > 0 && renderer->edge_worlds[below] == wild);
  report("the eastern wilderness is attached while falling",
         below_east > 0 && renderer->edge_worlds[below_east] == east);

  // And after landing, same-layer neighbours stay on the edge list so walking toward them is
  // walking toward something that is already drawn.
  state->current_world = wild;
  state->player_universe_z = wz;
  game_state_set_player_position(state, 16, 16, GROUND_TOP + 1);
  game_state_sync_isometric_renderer(state, renderer);
  const int east_idx = isometric_renderer_offset_index(renderer, 1, 0, 0);
  report("the eastern wilderness is attached after landing",
         east_idx > 0 && renderer->edge_worlds[east_idx] == east);
  report("the landed wilderness neighbour is walkable through the seam",
         game_state_can_move_to(state, W + 1, 16, GROUND_TOP + 1));

  g_game_state = NULL;
  isometric_renderer_destroy(renderer);
  destroy_state(state);
  world_destroy(home);
  world_destroy(cloud);
  world_destroy(wild);
  world_destroy(east);
}

// The wilderness is a plane. Arriving on one cell and walking east has to put the player in the
// next wilderness world, not against an invisible wall at the rim of the one they landed on.
static void test_walking_from_landed_wilderness_into_the_next(void)
{
  printf("\n-- walking east from landed wilderness crosses into the next wilderness --\n");

  World *home = make_island();
  World *cloud = make_cloud();
  World *wild = make_wilderness();
  World *east = make_wilderness();
  GameState *state = home ? make_state(home) : NULL;
  if (!home || !cloud || !wild || !east || !state)
  {
    report("wilderness crossing scenario built", false);
    world_destroy(home);
    world_destroy(cloud);
    world_destroy(wild);
    world_destroy(east);
    destroy_state(state);
    return;
  }
  g_game_state = state;

  const uint64_t hz = (uint64_t)UNIVERSE_HOME_Z;
  const uint64_t wz = hz - 2;
  universe_place(&state->universe, 0, 0, hz, home);
  universe_place(&state->universe, 0, 0, hz - 1, cloud);
  universe_place(&state->universe, 0, 0, wz, wild);
  universe_place(&state->universe, 1, 0, wz, east);
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = hz;

  game_state_set_player_position(state, 18, 16, ISLAND_TOP + 1);
  state->controls.velocity_z = 0.0f;
  state->controls.facing_yaw = 0.0f;
  clear_input(&state->controls);
  state->controls.move_forward = true;

  const float dt = 1.0f / 60.0f;
  bool landed = false;
  for (int frame = 0; frame < 1200; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
    if (state->player_universe_z == wz)
    {
      landed = true;
      break;
    }
  }
  report("the fall ended on the wilderness layer", landed && state->current_world == wild);

  // Stand near the eastern rim and walk into the neighbour that was already placed — the same
  // neighbour the renderer attached while falling.
  clear_input(&state->controls);
  state->controls.velocity_z = 0.0f;
  game_state_set_player_position(state, W - 2, 16, GROUND_TOP + 1);
  state->controls.facing_yaw = 0.0f;
  state->controls.move_forward = true;

  report("the eastern wilderness ground is walkable through the seam",
         game_state_can_move_to(state, W + 1, 16, GROUND_TOP + 1));

  for (int frame = 0; frame < 900; frame++)
  {
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);
    if (state->player_universe_x == 1)
      break;
  }
  printf("       walking east ended in cell (%llu, %llu, %llu) at x=%.2f\n",
         (unsigned long long)state->player_universe_x,
         (unsigned long long)state->player_universe_y,
         (unsigned long long)state->player_universe_z, state->player_world_x);
  report("walking east crosses into the eastern wilderness", state->player_universe_x == 1);
  report("the player is in the eastern wilderness world", state->current_world == east);
  report("the player is still on the wilderness layer", state->player_universe_z == wz);
  report("the player is not embedded after the crossing", !embedded(state));
  report("the player is standing after the crossing", game_state_player_is_grounded(state));

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(home);
  world_destroy(cloud);
  world_destroy(wild);
  world_destroy(east);
}

// ---------------------------------------------------------------------------------------------
// Actors
//
// Actors move through world_step_actors, which is a completely separate implementation from the
// player's move-and-slide. Nothing was checking that it agrees with the player's about what a body
// can walk through, or that it recovers when it cannot.

static bool place_actor(World *w, double x, double y, double z, double vx, double vy)
{
  Actor a;
  memset(&a, 0, sizeof(a));
  a.x = x;
  a.y = y;
  a.z = z;
  a.velocity_x = vx;
  a.velocity_y = vy;
  a.velocity_z = 0.0;
  a.is_active = true;
  a.is_controlled = false;
  a.is_flying = false;
  a.health = 100;
  strncpy(a.name, "walker", sizeof(a.name) - 1);
  return world_add_runtime_actor(w, &a);
}

// The engine has one answer for what stops a body — world_voxel_type_blocks_movement — and the
// player uses it. Actors were asking a different question, so a patch of tall grass or a puddle was
// a wall to them and not to the player.
static void test_actors_walk_through_what_the_player_walks_through(void)
{
  printf("\n-- actors are not fenced in by grass and water --\n");

  const uint32_t feet = GROUND_TOP + 1;
  struct
  {
    const char *what;
    VoxelType type;
  } cases[] = {
      {"tall grass", VOXEL_GRASS_TALL},
      {"water", VOXEL_WATER},
      {"steam", VOXEL_STEAM},
      {"a leaf canopy", VOXEL_LEAVES_OAK},
  };

  for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
  {
    World *w = make_ground();
    if (!w)
    {
      report("actor scenario built", false);
      return;
    }
    // A band of the voxel under test straight across the actor's path.
    for (uint32_t y = 0; y < H; y++)
      world_set_voxel(w, 18, y, feet, cases[i].type);
    world_refresh_occupancy_bitfield(w);

    if (!place_actor(w, 16.5, 16.5, (double)feet, 4.0, 0.0))
    {
      report("actor placed", false);
      world_destroy(w);
      return;
    }

    for (int frame = 0; frame < 240; frame++)
      world_step_actors(w, 1.0f / 60.0f);

    const double x = w->runtime_actors[0].x;
    char label[96];
    snprintf(label, sizeof(label), "an actor walks through %s", cases[i].what);
    printf("       %s: actor reached x=%.2f (band at x=18, started at 16.5)\n", cases[i].what, x);
    report(label, x > 19.0);

    world_destroy(w);
  }
}

// An actor pushed at an angle into a wall should keep the component of its motion that is not
// blocked, exactly as the player does. Zeroing both axes instead leaves it pinned to the wall for as
// long as its AI keeps aiming through it.
static void test_actors_slide_along_a_wall(void)
{
  printf("\n-- actors slide along a wall instead of sticking to it --\n");

  const uint32_t feet = GROUND_TOP + 1;
  World *w = make_ground();
  if (!w)
  {
    report("wall scenario built", false);
    return;
  }
  for (uint32_t y = 0; y < H; y++)
    column(w, 18, y, feet, feet + 3, VOXEL_STONE);
  world_refresh_occupancy_bitfield(w);

  // Heading north-east into a wall that runs north-south: x is blocked, y is not.
  if (!place_actor(w, 16.5, 8.5, (double)feet, 3.0, 3.0))
  {
    report("actor placed", false);
    world_destroy(w);
    return;
  }

  const double start_y = w->runtime_actors[0].y;
  for (int frame = 0; frame < 240; frame++)
    world_step_actors(w, 1.0f / 60.0f);

  const double x = w->runtime_actors[0].x;
  const double y = w->runtime_actors[0].y;
  printf("       actor ended at (%.2f, %.2f); wall at x=18, started at (16.50, %.2f)\n", x, y,
         start_y);
  report("the wall stopped the actor's advance", x < 18.0);
  report("the actor kept sliding along the wall", y > start_y + 2.0);

  world_destroy(w);
}

// ---------------------------------------------------------------------------------------------
// Real terrain
//
// Hand-built terrain is flat, and flat terrain hides the thing that makes a generated world feel
// like glue: it is a height field, so almost every step is onto a voxel one higher or lower. A body
// that cannot get onto a one-voxel rise can barely move out there, however well it does on a plane.

static void test_walking_across_generated_terrain(void)
{
  printf("\n-- a player crosses real generated terrain --\n");

  World *world = world_create(48, 48, 48);
  if (!world)
  {
    report("terrain generated", false);
    return;
  }
  world_generate_with_type(world, "movement-audit", WORLD_TYPE_WILDERNESS);
  world_refresh_occupancy_bitfield(world);

  GameState *state = make_state(world);
  if (!state)
  {
    report("state built", false);
    world_destroy(world);
    return;
  }
  g_game_state = state;

  // Stand on whatever the surface is at the middle of the world.
  int surface = -1;
  for (int z = (int)world->depth - 2; z >= 0; z--)
  {
    Voxel *v = world_get_voxel(world, 24, 24, (uint32_t)z);
    if (v && world_voxel_type_blocks_movement(v->type))
    {
      surface = z;
      break;
    }
  }
  if (surface < 0)
  {
    report("a surface was found to stand on", false);
    g_game_state = NULL;
    destroy_state(state);
    world_destroy(world);
    return;
  }
  game_state_set_player_position(state, 24, 24, surface + 1);
  state->controls.velocity_z = 0.0f;

  const float dt = 1.0f / 60.0f;
  const float start_x = state->player_world_x, start_y = state->player_world_y;
  float travelled = 0.0f;
  float prev_x = start_x, prev_y = start_y;
  int embedded_frames = 0, trapped_frames = 0;

  uint32_t rng = 0x12345678u;
  for (int frame = 0; frame < 3600; frame++)
  {
    if (frame % 60 == 0)
    {
      rng = rng * 1664525u + 1013904223u;
      state->controls.facing_yaw = (float)((rng >> 8) % 360) * (3.14159265f / 180.0f);
      clear_input(&state->controls);
      state->controls.move_forward = true;
    }
    player_controls_apply_wasd(state, &state->controls, dt);
    game_state_apply_gravity(state, dt);

    const float ddx = state->player_world_x - prev_x, ddy = state->player_world_y - prev_y;
    travelled += sqrtf(ddx * ddx + ddy * ddy);
    prev_x = state->player_world_x;
    prev_y = state->player_world_y;

    if (embedded(state))
      embedded_frames++;
    if (!has_anywhere_to_go(state))
      trapped_frames++;
  }

  // 3600 frames is a minute of walking. Walking speed is a few voxels a second, so a body that can
  // actually get around covers well over a hundred voxels of ground; one that is stopped by every
  // rise in a height field covers almost none. The bound is deliberately loose — this is measuring
  // "can move at all", not a speed.
  printf("       travelled %.1f voxels over 3600 frames, ended at (%.1f, %.1f, %.1f)\n", travelled,
         state->player_world_x, state->player_world_y, state->player_world_z);
  printf("       %d frames inside solid, %d frames with no open direction\n", embedded_frames,
         trapped_frames);
  report("the player is never inside solid on generated terrain", embedded_frames == 0);
  report("the player always has somewhere to go on generated terrain", trapped_frames == 0);
  report("the player covers real ground rather than being stopped by every rise",
         travelled > 60.0f);

  g_game_state = NULL;
  destroy_state(state);
  world_destroy(world);
}

// The cell two layers below home is the wilderness plane, and nothing else may be squatting on it.
//
// The menu backdrop used to be parked at (0,0,0) — the wilderness cell directly under the home column
// — and it is generated as WORLD_TYPE_HOME so the title screen comes up fast. So a player who walked
// off the island fell through the cloud layer and landed on a home island, and the real wilderness
// world could never be placed there either, because universe_place refuses an occupied cell.
static void test_the_layer_below_the_clouds_is_wilderness(void)
{
  printf("\n-- the cell two layers below home is left for the wilderness --\n");

  GameState *state = (GameState *)calloc(1, sizeof(GameState));
  if (!state || !game_state_init(state))
  {
    report("game state initialised", false);
    free(state);
    return;
  }
  g_game_state = state;

  // Home is at UNIVERSE_HOME_Z, the cloud below it one down, the wilderness plane one below that.
  const uint64_t wilderness_z = (uint64_t)UNIVERSE_HOME_Z - 2;
  World *squatter = universe_get(&state->universe, 0, 0, wilderness_z);

  if (squatter)
    printf("       cell (0,0,%llu) holds a world of generation type %d\n",
           (unsigned long long)wilderness_z, (int)squatter->generation_type);
  else
    printf("       cell (0,0,%llu) is empty, to be generated on demand\n",
           (unsigned long long)wilderness_z);

  report("the wilderness cell is not occupied by the menu backdrop",
         squatter != state->main_menu_world);
  report("nothing home-shaped is sitting on the wilderness cell",
         !squatter || squatter->generation_type == WORLD_TYPE_WILDERNESS);

  // And the layout itself agrees about what belongs there, whoever asks. Settlements are allowed
  // on this plane (WORLD_TYPE_WFC_TOWN); everything else is wilderness.
  WorldGenerationType type;
  VoxelType fill;
  const bool decided = universe_wfc_decide_cell(UNIVERSE_GEN_GAMEWORLD, 0, 0,
                                                (int)UNIVERSE_HOME_Z - 2, &type, &fill);
  report("the layout decides that cell is wilderness",
         decided && (type == WORLD_TYPE_WILDERNESS || type == WORLD_TYPE_WFC_TOWN));

  g_game_state = NULL;
  game_state_destroy(state); // frees the state itself, so no free() here
}

int main(void)
{
  printf("=== Movement and Collision Audit ===\n");

  test_the_layer_below_the_clouds_is_wilderness();

  test_jump_and_land_on_flat_ground();
  test_fast_fall_lands_on_the_surface();
  test_what_blocks_and_what_does_not();
  test_walking_never_traps_the_player();
  test_the_player_can_reach_the_world_edge();
  test_actors_walk_through_what_the_player_walks_through();
  test_actors_slide_along_a_wall();
  test_walking_across_generated_terrain();
  test_walking_from_one_world_into_the_next();
  test_an_unstreamed_seam_is_a_wall();
  test_walking_off_the_island_falls_to_the_wilderness();
  test_wilderness_neighbours_are_drawn_while_falling();
  test_walking_from_landed_wilderness_into_the_next();

  printf("\n%s (%d failure%s)\n", test_failures == 0 ? "PASS" : "FAIL", test_failures,
         test_failures == 1 ? "" : "s");
  return test_failures == 0 ? 0 : 1;
}
