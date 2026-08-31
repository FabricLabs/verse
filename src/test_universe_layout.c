// The shape of the game's universe, and the fall that depends on it.
//
// The layout is specific: exactly one home island on its layer, cloud worlds on all twenty-six sides
// of it, and two layers down a wilderness plane made entirely of wilderness worlds. Falling off the
// island has to carry the player down through the cloud layer and land them on the wilderness below.
//
// The layout half is decided by universe_wfc_decide_cell, which is pure, so it is checked directly.
// The fall is driven through the real game_state_apply_gravity against hand-built worlds small enough
// to place several of, which keeps this suite well under a second.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "constants.h"
#include "game_state.h"
#include "shadow_world.h"
#include "universe.h"
#include "world.h"
#include "world_gen_job.h"

// Stand-ins for the client's UI hooks, as in the other headless suites. Nothing here renders.
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

// The layer indices the game is built around, named so the assertions below read as the requirement
// rather than as arithmetic.
#define HOME_LAYER (UNIVERSE_HOME_Z)
#define CLOUD_LAYER (UNIVERSE_HOME_Z - 1)
#define WILDERNESS_LAYER (UNIVERSE_HOME_Z - 2)

static WorldGenerationType type_at(int gx, int gy, int gz)
{
  WorldGenerationType type;
  VoxelType fill;
  if (!universe_wfc_decide_cell(UNIVERSE_GEN_GAMEWORLD, gx, gy, gz, &type, &fill))
    return WORLD_TYPE_SOLID; // never expected, and distinct from every type asserted below
  return type;
}

static const char *type_name(WorldGenerationType t)
{
  switch (t)
  {
  case WORLD_TYPE_HOME: return "home";
  case WORLD_TYPE_CLOUD: return "cloud";
  case WORLD_TYPE_WILDERNESS: return "wilderness";
  case WORLD_TYPE_WFC_TOWN: return "settlement";
  case WORLD_TYPE_SOLID: return "solid";
  case WORLD_TYPE_UNDERWORLD: return "underworld";
  default: return "other";
  }
}

// ---------------------------------------------------------------------------------------------
// The layout

static void test_home_is_alone_on_its_layer(void)
{
  printf("\n-- one home island, cloud on every side --\n");

  report("the origin of the home layer is the home island",
         type_at(0, 0, HOME_LAYER) == WORLD_TYPE_HOME);

  // All twenty-six cells touching home. The eight beside it are the ones that used to be home too,
  // which is what made the island a plateau in a plane of islands rather than one island in the sky.
  int neighbours = 0, cloud_neighbours = 0;
  bool have_exception = false;
  WorldGenerationType exception_type = WORLD_TYPE_CLOUD;
  int exception_x = 0, exception_y = 0, exception_z = 0;

  for (int dz = -1; dz <= 1; dz++)
  {
    for (int dy = -1; dy <= 1; dy++)
    {
      for (int dx = -1; dx <= 1; dx++)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        neighbours++;
        const WorldGenerationType t = type_at(dx, dy, HOME_LAYER + dz);
        if (t == WORLD_TYPE_CLOUD)
        {
          cloud_neighbours++;
          continue;
        }
        if (!have_exception)
        {
          have_exception = true;
          exception_type = t;
          exception_x = dx;
          exception_y = dy;
          exception_z = HOME_LAYER + dz;
        }
      }
    }
  }

  printf("       %d of %d cells touching home are cloud\n", cloud_neighbours, neighbours);
  if (have_exception)
    printf("       first exception: (%d,%d,%d) is %s\n", exception_x, exception_y, exception_z,
           type_name(exception_type));
  report("all twenty-six cells touching home are cloud", cloud_neighbours == neighbours);

  // Being alone has to hold further out than the immediate ring, or walking two cells from the
  // island would find another one.
  int homes_on_layer = 0;
  for (int gy = -4; gy <= 4; gy++)
    for (int gx = -4; gx <= 4; gx++)
      if (type_at(gx, gy, HOME_LAYER) == WORLD_TYPE_HOME)
        homes_on_layer++;
  printf("       %d home worlds in the 9x9 around the origin\n", homes_on_layer);
  report("home is the only home world on its layer", homes_on_layer == 1);
}

static void test_the_layers_below_home(void)
{
  printf("\n-- cloud below home, wilderness below that --\n");

  report("the cell directly below home is cloud",
         type_at(0, 0, CLOUD_LAYER) == WORLD_TYPE_CLOUD);
  report("the wilderness cell under home is always a settlement",
         type_at(0, 0, WILDERNESS_LAYER) == WORLD_TYPE_WFC_TOWN);

  // The wilderness plane is the floor of the playable universe. Cells are wilderness terrain, with
  // occasional settlement (WFC_TOWN) tiles. Nothing else belongs here — a cloud or bedrock cell would
  // be a hole in the ground to fall through.
  int cells = 0, plane_cells = 0;
  for (int gy = -4; gy <= 4; gy++)
  {
    for (int gx = -4; gx <= 4; gx++)
    {
      cells++;
      const WorldGenerationType t = type_at(gx, gy, WILDERNESS_LAYER);
      if (t == WORLD_TYPE_WILDERNESS || t == WORLD_TYPE_WFC_TOWN)
        plane_cells++;
    }
  }
  printf("       %d of %d cells in the 9x9 wilderness plane are wilderness/settlement\n", plane_cells,
         cells);
  report("the whole wilderness plane is wilderness or settlement", plane_cells == cells);

  report("the wilderness plane is universe z=0", WILDERNESS_LAYER == 0);
}

static void test_settlement_adjacency(void)
{
  printf("\n-- settlements never share an edge or corner --\n");

  int settlements = 0;
  int adjacent_pairs = 0;
  const int R = 64;
  for (int gy = -R; gy <= R; gy++)
  {
    for (int gx = -R; gx <= R; gx++)
    {
      if (type_at(gx, gy, WILDERNESS_LAYER) != WORLD_TYPE_WFC_TOWN)
        continue;
      settlements++;
      for (int dy = -1; dy <= 1; dy++)
      {
        for (int dx = -1; dx <= 1; dx++)
        {
          if (dx == 0 && dy == 0)
            continue;
          if (type_at(gx + dx, gy + dy, WILDERNESS_LAYER) == WORLD_TYPE_WFC_TOWN)
            adjacent_pairs++;
        }
      }
    }
  }

  printf("       %d settlements in the %dx%d wilderness sample, %d adjacent neighbour hits\n",
         settlements, 2 * R + 1, 2 * R + 1, adjacent_pairs);
  report("the wilderness sample contains at least one settlement", settlements >= 1);
  report("no two settlements are Chebyshev-adjacent", adjacent_pairs == 0);
}

// ---------------------------------------------------------------------------------------------
// Neighbouring cells have to differ

// Cloud is the cheap generator and the one the layout leans on hardest, so it is what the
// per-coordinate checks use.
static World *cloud_at(int ux, int uy, int uz, const char *seed)
{
  World *w = world_create(32, 32, 32);
  if (!w)
    return NULL;
  // What the generator reads to place its blobs. Set before generating, not after placing, which is
  // the whole point of the check.
  w->universe_x = ux;
  w->universe_y = uy;
  w->universe_z = uz;
  world_generate_with_type(w, seed, WORLD_TYPE_CLOUD);
  return w;
}

static bool worlds_identical(const World *a, const World *b)
{
  if (!a || !b || a->width != b->width || a->height != b->height || a->depth != b->depth)
    return false;
  for (uint32_t z = 0; z < a->depth; z++)
    for (uint32_t y = 0; y < a->height; y++)
      for (uint32_t x = 0; x < a->width; x++)
      {
        Voxel *va = world_get_voxel((World *)a, x, y, z);
        Voxel *vb = world_get_voxel((World *)b, x, y, z);
        if (!va || !vb || va->type != vb->type)
          return false;
      }
  return true;
}

static void test_cells_differ_by_coordinate(void)
{
  printf("\n-- neighbouring cells are different worlds --\n");

  World *origin = cloud_at(0, 0, CLOUD_LAYER, "layout-test");
  World *east = cloud_at(1, 0, CLOUD_LAYER, "layout-test");
  World *above = cloud_at(0, 0, HOME_LAYER + 1, "layout-test");

  if (!origin || !east || !above)
  {
    report("cloud worlds allocated", false);
    world_destroy(origin);
    world_destroy(east);
    world_destroy(above);
    return;
  }

  // Same seed, different cell. The generator samples noise in universe space, so the coordinates
  // alone have to be enough to tell two cells apart; if they are not, every cloud world in the sky
  // is the same cloud world.
  report("the cell east of the origin is not a copy of it", !worlds_identical(origin, east));
  report("the cloud layer above home is not a copy of the one below it",
         !worlds_identical(origin, above));

  world_destroy(origin);
  world_destroy(east);
  world_destroy(above);
}

// The streaming path is where the coordinates were being assigned too late: worlds were generated
// first and only given their cell afterwards, so every streamed world was generated as if it sat at
// the origin. This drives the real job.
static void test_streamed_cells_know_where_they_are(void)
{
  printf("\n-- streamed cells are generated in place --\n");

  CellGenJob *a = cell_gen_job_start("layout-test", 0, 0, (uint64_t)CLOUD_LAYER);
  CellGenJob *b = cell_gen_job_start("layout-test", 1, 0, (uint64_t)CLOUD_LAYER);
  if (!a || !b)
  {
    report("streaming jobs started", false);
    if (a) cell_gen_job_destroy(a);
    if (b) cell_gen_job_destroy(b);
    return;
  }

  // With no worker pool these run inline and are already finished here, which is what the headless
  // suites get.
  while (!cell_gen_job_is_complete(a) || !cell_gen_job_is_complete(b))
    ;

  World *wa = cell_gen_job_take_world(a);
  World *wb = cell_gen_job_take_world(b);
  cell_gen_job_destroy(a);
  cell_gen_job_destroy(b);

  if (!wa || !wb)
  {
    report("streaming produced two worlds", false);
    world_destroy(wa);
    world_destroy(wb);
    return;
  }

  report("a streamed world is generated as a cloud world",
         wa->generation_type == WORLD_TYPE_CLOUD);
  report("a streamed world carries the cell it was generated for",
         wa->universe_x == 0 && wa->universe_y == 0 && wa->universe_z == CLOUD_LAYER &&
             wb->universe_x == 1 && wb->universe_y == 0 && wb->universe_z == CLOUD_LAYER);
  report("two streamed cells are not the same world", !worlds_identical(wa, wb));

  world_destroy(wa);
  world_destroy(wb);
}

// ---------------------------------------------------------------------------------------------
// The fall

#define FALL_SIZE 16
#define FALL_DEPTH 16
#define ISLAND_TOP 8
#define WILDERNESS_TOP 4

static World *make_home_island(void)
{
  World *w = world_create(FALL_SIZE, FALL_SIZE, FALL_DEPTH);
  if (!w)
    return NULL;
  // A platform in the middle with open air all around it, which is the shape that matters: there has
  // to be somewhere to fall off.
  for (uint32_t y = 5; y <= 10; y++)
    for (uint32_t x = 5; x <= 10; x++)
      for (uint32_t z = 0; z <= ISLAND_TOP; z++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);
  world_refresh_occupancy_bitfield(w);
  return w;
}

// Steam, including a voxel directly under the falling player at the very bottom of the world. That
// bottom voxel is the case that used to strand the player here: the fall only left a world through
// air, so a wisp of cloud in the wrong cell was a solid floor.
static World *make_cloud_layer(int column_x, int column_y)
{
  World *w = world_create(FALL_SIZE, FALL_SIZE, FALL_DEPTH);
  if (!w)
    return NULL;
  for (uint32_t z = 0; z < FALL_DEPTH; z += 4)
    for (uint32_t y = 0; y < FALL_SIZE; y++)
      for (uint32_t x = 0; x < FALL_SIZE; x++)
        world_set_voxel(w, x, y, z, VOXEL_STEAM);
  world_set_voxel(w, (uint32_t)column_x, (uint32_t)column_y, 0, VOXEL_STEAM);
  world_refresh_occupancy_bitfield(w);
  return w;
}

static World *make_wilderness_floor(void)
{
  World *w = world_create(FALL_SIZE, FALL_SIZE, FALL_DEPTH);
  if (!w)
    return NULL;
  for (uint32_t z = 0; z <= WILDERNESS_TOP; z++)
    for (uint32_t y = 0; y < FALL_SIZE; y++)
      for (uint32_t x = 0; x < FALL_SIZE; x++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);
  world_refresh_occupancy_bitfield(w);
  return w;
}

static void test_falling_off_the_island(void)
{
  printf("\n-- falling off the island --\n");

  // Just off the platform edge, so the first frame of gravity has nothing underneath.
  const int fall_x = 12, fall_y = 8;

  World *home = make_home_island();
  World *cloud = make_cloud_layer(fall_x, fall_y);
  World *wilderness = make_wilderness_floor();

  GameState *state = (GameState *)calloc(1, sizeof(GameState));
  if (!home || !cloud || !wilderness || !state)
  {
    report("fall scenario built", false);
    world_destroy(home);
    world_destroy(cloud);
    world_destroy(wilderness);
    free(state);
    return;
  }

  if (!universe_init(&state->universe, "0123456789abcdef", 0, 1))
  {
    report("universe initialised", false);
    world_destroy(home);
    world_destroy(cloud);
    world_destroy(wilderness);
    free(state);
    return;
  }

  universe_place(&state->universe, 0, 0, (uint64_t)HOME_LAYER, home);
  universe_place(&state->universe, 0, 0, (uint64_t)CLOUD_LAYER, cloud);
  universe_place(&state->universe, 0, 0, (uint64_t)WILDERNESS_LAYER, wilderness);

  GameWorlds *gw = (GameWorlds *)calloc(1, sizeof(GameWorlds));
  gw->home_world = home;
  gw->base_seed = strdup("0123456789abcdef");
  state->game_worlds = gw;

  state->current_world = home;
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = (uint64_t)HOME_LAYER;
  state->game_started = true;
  state->player_flying = false;
  // Gravity ignores this flag; streaming and eviction do not. Holding it set keeps the three worlds
  // placed above from being evicted mid-fall and stops the streamer generating full-size worlds for
  // the rest of the neighbourhood, neither of which this is testing.
  state->world_generation_active = true;
  strncpy(state->player_name, "faller", sizeof(state->player_name) - 1);

  game_state_set_player_position(state, fall_x, fall_y, ISLAND_TOP + 1);
  state->controls.velocity_z = 0.0f;

  report("the player starts on the home layer",
         state->player_universe_z == (uint64_t)HOME_LAYER && state->current_world == home);
  report("the player starts over open air",
         !game_state_player_is_grounded(state));

  // Long enough to cross three worlds at 60fps, and no longer: a fall that has not finished by then
  // is stuck, which is the failure this is looking for.
  const float dt = 1.0f / 60.0f;
  bool reached_cloud = false;
  int frames_to_wilderness = -1;

  for (int frame = 0; frame < 600; frame++)
  {
    game_state_apply_gravity(state, dt);
    if (state->player_universe_z == (uint64_t)CLOUD_LAYER)
      reached_cloud = true;
    if (state->player_universe_z == (uint64_t)WILDERNESS_LAYER)
    {
      frames_to_wilderness = frame + 1;
      break;
    }
  }

  printf("       reached the wilderness layer after %d frames\n", frames_to_wilderness);
  report("the fall passes through the cloud layer", reached_cloud);
  report("the fall ends on the wilderness layer", frames_to_wilderness > 0);
  report("the player is in the wilderness world", state->current_world == wilderness);
  report("the player did not stop on the clouds", state->current_world != cloud);

  // Landed, not still falling and not sunk into the floor.
  for (int frame = 0; frame < 120; frame++)
    game_state_apply_gravity(state, dt);
  printf("       resting at z=%.2f on a floor whose top voxel is %d\n", state->player_world_z,
         WILDERNESS_TOP);
  report("the player comes to rest on the wilderness floor",
         state->player_voxel_z == WILDERNESS_TOP + 1);
  report("the player is at rest", state->controls.velocity_z == 0.0f);
  report("the player stayed in the wilderness world",
         state->player_universe_z == (uint64_t)WILDERNESS_LAYER);

  if (state->shadow_world)
    shadow_world_destroy(state->shadow_world);
  universe_free(&state->universe);
  free(gw->base_seed);
  free(gw);
  free(state);
}

int main(void)
{
  printf("=== Universe Layout Tests ===\n");

  test_home_is_alone_on_its_layer();
  test_the_layers_below_home();
  test_settlement_adjacency();
  test_cells_differ_by_coordinate();
  test_streamed_cells_know_where_they_are();
  test_falling_off_the_island();

  printf("\n%s (%d failure%s)\n", test_failures == 0 ? "PASS" : "FAIL", test_failures,
         test_failures == 1 ? "" : "s");
  return test_failures == 0 ? 0 : 1;
}
