// Fluid simulation tests.
//
// The properties asserted here are the ones the simulation is supposed to guarantee rather than
// the shapes any particular version happens to produce:
//
//   - conservation: no step creates or destroys fluid
//   - collapse: unsupported fluid ends up on the floor
//   - equilibrium: a puddle on a flat floor levels out, and stops
//   - communicating vessels: water rises up the far arm of a U-bend to meet its own level
//   - quiescence: settled fluid costs nothing to keep simulating
//   - determinism: the same world stepped twice from the same state lands identically
//
// Plus the sub-voxel surface field: a splash conserves the mean height, spreads outwards, and
// decays back to flat.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluid_sim.h"
#include "fluid_surface.h"
#include "gpu_voxel_buffer.h"
#include "particle_effects.h"
#include "water_erosion.h"
#include "world.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                                           \
  do                                                                                               \
  {                                                                                                \
    g_checks++;                                                                                    \
    if (!(cond))                                                                                   \
    {                                                                                              \
      g_failures++;                                                                                \
      printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                                \
      printf(__VA_ARGS__);                                                                         \
      printf("\n");                                                                                \
    }                                                                                              \
  } while (0)

// ---------------------------------------------------------------------------
// Helpers

static World *make_world(uint32_t w, uint32_t h, uint32_t d)
{
  World *world = world_create(w, h, d);
  if (!world)
    return NULL;
  for (uint32_t z = 0; z < d; z++)
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++)
        world_set_voxel(world, x, y, z, VOXEL_AIR);
  return world;
}

// A floor of bedrock at z=0, so fluid has something to sit on.
static void add_floor(World *world)
{
  for (uint32_t y = 0; y < world->height; y++)
    for (uint32_t x = 0; x < world->width; x++)
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
}

static void put_water(World *world, uint32_t x, uint32_t y, uint32_t z, int level)
{
  world_set_voxel(world, x, y, z, VOXEL_WATER);
  Voxel *v = world_get_voxel(world, x, y, z);
  if (v)
    voxel_set_quantity(v, (uint8_t)level);
  fluid_sim_touch(world, (int)x, (int)y, (int)z);
}

static int level_at(World *world, uint32_t x, uint32_t y, uint32_t z)
{
  return fluid_level_of(world_get_voxel(world, x, y, z));
}

// Step until nothing moves, or give up. Returns the number of steps taken.
static int run_until_rest(World *world, int max_steps)
{
  for (int i = 0; i < max_steps; i++)
  {
    const FluidStepStats stats = fluid_sim_step(world, 1 << 20);
    if (stats.at_rest)
      return i + 1;
  }
  return -1;
}

// ---------------------------------------------------------------------------

static void test_conservation_and_collapse(void)
{
  printf("collapse and conservation\n");

  World *world = make_world(8, 8, 16);
  add_floor(world);

  // A cube of water hanging in mid-air.
  for (uint32_t z = 10; z < 14; z++)
    for (uint32_t y = 3; y < 5; y++)
      for (uint32_t x = 3; x < 5; x++)
        put_water(world, x, y, z, FLUID_LEVEL_FULL);

  const long long before = fluid_sim_total_level(world);
  CHECK(before == 16 * FLUID_LEVEL_FULL, "expected %d levels placed, got %lld",
        16 * FLUID_LEVEL_FULL, before);

  // Conservation has to hold after every single step, not just at the end, or a step that both
  // creates and destroys fluid would pass unnoticed.
  for (int i = 0; i < 200; i++)
  {
    fluid_sim_step(world, 1 << 20);
    const long long now = fluid_sim_total_level(world);
    if (now != before)
    {
      CHECK(false, "step %d changed the total from %lld to %lld", i, before, now);
      break;
    }
  }

  const int steps = run_until_rest(world, 400);
  CHECK(steps > 0, "never came to rest");
  CHECK(fluid_sim_total_level(world) == before, "total drifted to %lld from %lld",
        fluid_sim_total_level(world), before);

  // Nothing may be left floating: every fluid cell needs something under it.
  int floating = 0;
  for (uint32_t z = 1; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
      {
        if (level_at(world, x, y, z) <= 0)
          continue;
        if (world_get_voxel(world, x, y, z - 1)->type == VOXEL_AIR)
          floating++;
      }
  CHECK(floating == 0, "%d fluid cells left unsupported", floating);

  // And it should have reached the floor rather than stalling partway.
  CHECK(level_at(world, 3, 3, 1) > 0, "water did not reach the floor");

  world_destroy(world);
}

static void test_flat_equilibrium(void)
{
  printf("levelling on a flat floor\n");

  World *world = make_world(16, 16, 8);
  add_floor(world);

  // One tall column dropped in a corner. It should end up as an even sheet.
  const int placed = 6;
  for (int i = 0; i < placed; i++)
    put_water(world, 2, 2, (uint32_t)(1 + i), FLUID_LEVEL_FULL);

  const long long total = fluid_sim_total_level(world);
  const int steps = run_until_rest(world, 4000);
  CHECK(steps > 0, "never came to rest");
  CHECK(fluid_sim_total_level(world) == total, "total changed from %lld to %lld", total,
        fluid_sim_total_level(world));

  // Equilibrium means no two neighbouring columns differ by more than the one level the integer
  // representation cannot split.
  int worst = 0;
  for (uint32_t y = 0; y < world->height; y++)
    for (uint32_t x = 0; x < world->width; x++)
    {
      int here = 0;
      for (uint32_t z = 1; z < world->depth; z++)
        here += level_at(world, x, y, z);
      if (here == 0)
        continue;
      for (int d = 0; d < 2; d++)
      {
        const uint32_t nx = x + (d == 0 ? 1 : 0), ny = y + (d == 1 ? 1 : 0);
        if (nx >= world->width || ny >= world->height)
          continue;
        int there = 0;
        for (uint32_t z = 1; z < world->depth; z++)
          there += level_at(world, nx, ny, z);
        if (there == 0)
          continue;
        const int diff = here > there ? here - there : there - here;
        if (diff > worst)
          worst = diff;
      }
    }
  CHECK(worst <= 2, "wet columns still differ by %d levels", worst);

  // The sheet should be wide and shallow rather than a tower that never spread.
  int wet_columns = 0;
  for (uint32_t y = 0; y < world->height; y++)
    for (uint32_t x = 0; x < world->width; x++)
      if (level_at(world, x, y, 1) > 0)
        wet_columns++;
  CHECK(wet_columns > 16, "water spread over only %d columns", wet_columns);

  world_destroy(world);
}

static void test_communicating_vessels(void)
{
  printf("U-bend equalisation\n");

  // Two shafts joined by a channel at the bottom, cut out of solid rock:
  //
  //   x=1 shaft   x=2..5 rock with a channel at z=1   x=6 shaft
  const uint32_t W = 8, H = 3, D = 12;
  World *world = make_world(W, H, D);
  for (uint32_t z = 0; z < D; z++)
    for (uint32_t y = 0; y < H; y++)
      for (uint32_t x = 0; x < W; x++)
        world_set_voxel(world, x, y, z, VOXEL_BEDROCK);

  const uint32_t y = 1;
  for (uint32_t z = 1; z < D - 1; z++)
  {
    world_set_voxel(world, 1, y, z, VOXEL_AIR); // left shaft
    world_set_voxel(world, 6, y, z, VOXEL_AIR); // right shaft
  }
  for (uint32_t x = 1; x <= 6; x++)
    world_set_voxel(world, x, y, 1, VOXEL_AIR); // channel joining them

  // Fill the left shaft. Its water has to travel along the channel and climb the right shaft.
  const int filled = 6;
  for (int i = 0; i < filled; i++)
    put_water(world, 1, y, (uint32_t)(1 + i), FLUID_LEVEL_FULL);

  const long long total = fluid_sim_total_level(world);
  const int steps = run_until_rest(world, 20000);
  CHECK(steps > 0, "never came to rest");
  CHECK(fluid_sim_total_level(world) == total, "total changed from %lld to %lld", total,
        fluid_sim_total_level(world));

  // Both shafts should end up holding about the same depth. The channel holds a share too, so
  // compare the shafts to each other rather than to a computed ideal.
  int left = 0, right = 0;
  for (uint32_t z = 1; z < D - 1; z++)
  {
    left += level_at(world, 1, y, z);
    right += level_at(world, 6, y, z);
  }
  CHECK(right > 0, "water never reached the far shaft");
  const int gap = left > right ? left - right : right - left;
  CHECK(gap <= FLUID_LEVEL_FULL, "shafts hold %d and %d levels, a gap of %d", left, right, gap);

  // And the surface heights should match: find the topmost wet cell in each shaft.
  int left_top = 0, right_top = 0;
  for (uint32_t z = 1; z < D - 1; z++)
  {
    if (level_at(world, 1, y, z) > 0)
      left_top = (int)z;
    if (level_at(world, 6, y, z) > 0)
      right_top = (int)z;
  }
  const int height_gap = left_top > right_top ? left_top - right_top : right_top - left_top;
  CHECK(height_gap <= 1, "surfaces sit at z=%d and z=%d", left_top, right_top);

  world_destroy(world);
}

static void test_quiescence(void)
{
  printf("settled fluid stops costing anything\n");

  World *world = make_world(24, 24, 6);
  add_floor(world);

  // A pool with nowhere left to go: it covers the whole floor at an even depth, so it is already
  // at equilibrium and the first step should confirm that and stop.
  for (uint32_t y = 0; y < world->height; y++)
    for (uint32_t x = 0; x < world->width; x++)
      put_water(world, x, y, 1, FLUID_LEVEL_FULL);

  const int steps = run_until_rest(world, 200);
  CHECK(steps == 1, "took %d steps to recognise a pool that was already level", steps);

  // Once at rest, further steps must visit nothing at all. This is the property the whole cost
  // model rests on: a world full of settled water is as cheap as an empty one.
  for (int i = 0; i < 50; i++)
  {
    const FluidStepStats stats = fluid_sim_step(world, 1 << 20);
    CHECK(stats.cells_visited == 0, "step %d still visited %d cells", i, stats.cells_visited);
    CHECK(stats.transfers == 0, "step %d still moved fluid", i);
    if (stats.cells_visited != 0)
      break;
  }

  // Disturbing it has to wake it up again, or nothing would ever move after the first settle.
  put_water(world, 12, 12, 3, FLUID_LEVEL_FULL);
  const FluidStepStats woken = fluid_sim_step(world, 1 << 20);
  CHECK(woken.cells_visited > 0, "placing water did not wake the simulation");

  world_destroy(world);
}

static void test_determinism(void)
{
  printf("determinism\n");

  long long signature[2] = {0, 0};
  for (int run = 0; run < 2; run++)
  {
    World *world = make_world(12, 12, 8);
    add_floor(world);
    for (uint32_t z = 3; z < 7; z++)
      put_water(world, 5, 6, z, FLUID_LEVEL_FULL);
    put_water(world, 8, 3, 4, FLUID_LEVEL_FULL / 2);

    for (int i = 0; i < 60; i++)
      fluid_sim_step(world, 64); // a budget small enough that the work is spread over steps

    // Position-weighted so a rearrangement shows up, not just a change in the total.
    long long sig = 0;
    for (uint32_t z = 0; z < world->depth; z++)
      for (uint32_t y = 0; y < world->height; y++)
        for (uint32_t x = 0; x < world->width; x++)
          sig += (long long)level_at(world, x, y, z) * (long long)(1 + x + y * 13 + z * 101);
    signature[run] = sig;
    world_destroy(world);
  }
  CHECK(signature[0] == signature[1], "two identical runs diverged: %lld vs %lld", signature[0],
        signature[1]);
}

static void test_budget_is_respected(void)
{
  printf("work budget\n");

  World *world = make_world(32, 32, 8);
  add_floor(world);
  for (uint32_t y = 0; y < 32; y++)
    for (uint32_t x = 0; x < 32; x++)
      put_water(world, x, y, 4, FLUID_LEVEL_FULL);

  const long long total = fluid_sim_total_level(world);
  for (int i = 0; i < 40; i++)
  {
    const FluidStepStats stats = fluid_sim_step(world, 100);
    CHECK(stats.cells_visited <= 100, "visited %d cells on a budget of 100", stats.cells_visited);
    if (stats.cells_visited > 100)
      break;
  }
  // Truncating a step must defer work, not drop it.
  CHECK(fluid_sim_total_level(world) == total, "budgeted stepping lost fluid: %lld vs %lld",
        fluid_sim_total_level(world), total);

  world_destroy(world);
}

// A step's budget decides how much work it does, never which cells the work is done to.
//
// This is the regression that matters most in the file. Draining the queue by scanning its bitmap
// from the start looks harmless and is not: the bitmap is indexed by z * plane + y * width + x, so
// the low addresses it favours are the bottom of the world, and once more cells are queued than one
// step can visit, the top of a body of water is never reached. The visible result is water hanging
// in mid-air over a void that the cells below it have already drained into — the upper cells were
// waiting for a turn that never came. So this checks the shape of the result under a budget far too
// small to settle in one step, not just that the budget was obeyed.
static void test_budget_does_not_starve(void)
{
  printf("a tight budget delays work without distorting it\n");

  const uint32_t W = 24, H = 24, D = 24;
  World *world = make_world(W, H, D);
  add_floor(world);

  // Released mid-air, so every cell has to fall before anything can spread: the case that needs the
  // whole height of the world to get its turn.
  for (uint32_t z = 12; z < 20; z++)
    for (uint32_t y = 8; y < 16; y++)
      for (uint32_t x = 8; x < 16; x++)
        put_water(world, x, y, z, FLUID_LEVEL_FULL);

  const long long total = fluid_sim_total_level(world);

  // A twentieth of what a settled step would ask for, so the queue is over budget throughout.
  const int budget = 32;
  int steps = 0;
  bool rested = false;
  for (; steps < 200000; steps++)
  {
    if (fluid_sim_step(world, budget).at_rest)
    {
      rested = true;
      break;
    }
  }
  CHECK(rested, "never came to rest on a budget of %d", budget);
  CHECK(fluid_sim_total_level(world) == total, "lost fluid: %lld of %lld",
        fluid_sim_total_level(world), total);

  // Nothing may be left floating. A fluid cell either sits on the floor, on something solid, or on
  // more fluid; air underneath means it was skipped.
  int floating = 0;
  for (uint32_t z = 1; z < D; z++)
    for (uint32_t y = 0; y < H; y++)
      for (uint32_t x = 0; x < W; x++)
      {
        if (fluid_level_of(world_voxel_cptr_fast(world, (int)x, (int)y, (int)z)) <= 0)
          continue;
        if (world_voxel_cptr_fast(world, (int)x, (int)y, (int)z - 1)->type == VOXEL_AIR)
          floating++;
      }
  CHECK(floating == 0, "%d fluid cells came to rest above a void", floating);

  // And the surface must be level. Column totals are the water depth of each column; the deadband
  // that stops neighbouring cells trading a single level forever also allows a residual slope of
  // one level per cell, so the tolerance is that slope across the world, not zero.
  int min_col = 1 << 30, max_col = 0;
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
    {
      int col = 0;
      for (uint32_t z = 0; z < D; z++)
        col += fluid_level_of(world_voxel_cptr_fast(world, (int)x, (int)y, (int)z));
      if (col < min_col)
        min_col = col;
      if (col > max_col)
        max_col = col;
    }
  const int slack = (int)(W + H);
  CHECK(max_col - min_col <= slack, "surface is not level: columns hold %d..%d, allowed spread %d",
        min_col, max_col, slack);

  world_destroy(world);
}

static void test_no_uphill_flow(void)
{
  printf("water does not climb\n");

  // A step in the floor. Water poured on the low side must not walk up onto the high side.
  World *world = make_world(8, 4, 8);
  add_floor(world);
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 4; x < 8; x++)
    {
      world_set_voxel(world, x, y, 1, VOXEL_BEDROCK);
      world_set_voxel(world, x, y, 2, VOXEL_BEDROCK);
    }

  put_water(world, 1, 2, 1, FLUID_LEVEL_FULL);
  put_water(world, 1, 2, 2, FLUID_LEVEL_FULL);
  run_until_rest(world, 2000);

  int on_the_shelf = 0;
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 4; x < 8; x++)
      for (uint32_t z = 3; z < 8; z++)
        on_the_shelf += level_at(world, x, y, z);
  CHECK(on_the_shelf == 0, "%d levels ended up on the raised shelf", on_the_shelf);

  world_destroy(world);
}

// ---------------------------------------------------------------------------
// Sub-voxel surface

static long long surface_sum(const int16_t *h)
{
  long long sum = 0;
  for (int i = 0; i < FLUID_SURFACE_CELLS; i++)
    sum += h[i];
  return sum;
}

static long long surface_energy(const int16_t *h)
{
  long long e = 0;
  for (int i = 0; i < FLUID_SURFACE_CELLS; i++)
    e += (long long)h[i] * (long long)h[i];
  return e;
}

static void test_surface_wave(void)
{
  printf("sub-voxel surface wave\n");

  World *world = make_world(8, 8, 4);
  add_floor(world);
  for (uint32_t y = 0; y < 8; y++)
    for (uint32_t x = 0; x < 8; x++)
      put_water(world, x, y, 1, FLUID_LEVEL_FULL);
  run_until_rest(world, 200);

  FluidSurface *surface = fluid_surface_create(64);
  CHECK(surface != NULL, "could not create the surface cache");
  if (!surface)
  {
    world_destroy(world);
    return;
  }

  // A drop landing in the middle of one voxel's surface.
  const FluidSplash drop = {.x = 4, .y = 4, .z = 1, .from_dx = 0, .from_dy = 0, .from_dz = -1,
                            .units = 8};
  fluid_surface_splash(surface, world, &drop);

  const int16_t *h = fluid_surface_heights(surface, world, 4, 4, 1);
  CHECK(h != NULL, "no surface after a splash");
  if (!h)
  {
    fluid_surface_destroy(surface);
    world_destroy(world);
    return;
  }

  // The impulse displaces the surface without adding to it: what one column loses, the ring
  // around it gains. That is what keeps the average height the business of the coarse level.
  CHECK(surface_sum(h) == 0, "splash changed the mean height by %lld", surface_sum(h));
  const long long struck = surface_energy(h);
  CHECK(struck > 0, "splash left the surface flat");

  // It has to travel outwards rather than just sink in place. A drop from above lands wherever
  // the scatter put it, so find the impact first and then watch the disturbance leave it.
  int impact = 0;
  for (int i = 1; i < FLUID_SURFACE_CELLS; i++)
    if (abs(h[i]) > abs(h[impact]))
      impact = i;
  const int impact_i = impact % FLUID_SURFACE_DIM, impact_j = impact / FLUID_SURFACE_DIM;

  // The splash touches nothing beyond the crater, so the energy outside it can only be energy the
  // wave carried there. A ring one sub-voxel past the rim is far enough to be sure of that and
  // close enough that the wave reaches it soon.
  const int beyond = (FLUID_SPLASH_RIM + 1) * (FLUID_SPLASH_RIM + 1);
  long long away_before = 0;
  for (int j = 0; j < FLUID_SURFACE_DIM; j++)
    for (int i = 0; i < FLUID_SURFACE_DIM; i++)
    {
      const int di = i - impact_i, dj = j - impact_j;
      if (di * di + dj * dj > beyond)
        away_before += (long long)h[j * FLUID_SURFACE_DIM + i] * h[j * FLUID_SURFACE_DIM + i];
    }
  CHECK(away_before == 0, "the splash reached past its own crater: %lld", away_before);

  for (int i = 0; i < 60; i++)
    fluid_surface_step(surface, 1.0f / (float)FLUID_SURFACE_HZ);

  h = fluid_surface_heights(surface, world, 4, 4, 1);
  CHECK(h != NULL, "surface retired while still water");
  if (h)
  {
    long long away_after = 0;
    for (int j = 0; j < FLUID_SURFACE_DIM; j++)
      for (int i = 0; i < FLUID_SURFACE_DIM; i++)
      {
        const int di = i - impact_i, dj = j - impact_j;
        if (di * di + dj * dj > beyond)
          away_after += (long long)h[j * FLUID_SURFACE_DIM + i] * h[j * FLUID_SURFACE_DIM + i];
      }
    CHECK(away_after > 0, "the disturbance never left the crater it started in");
    CHECK(surface_sum(h) * surface_sum(h) < struck,
          "propagation moved the mean height to %lld", surface_sum(h));
  }

  // And it must die out, or a single drop would ring forever. Going flat and being retired are the
  // same event, so an empty cache is the proof: nothing is still moving anywhere in the pool.
  for (int i = 0; i < 4000; i++)
    fluid_surface_step(surface, 1.0f / (float)FLUID_SURFACE_HZ);
  CHECK(fluid_surface_live_count(surface) == 0, "%d surfaces are still ringing after a single drop",
        fluid_surface_live_count(surface));

  fluid_surface_destroy(surface);
  world_destroy(world);
}

// A ripple has to cross the boundary between two water voxels.
//
// Each surface is a separate 32x32 patch, and a patch that reads its own edge where a neighbour
// should be has a wall there. Get that wrong and every ripple stays inside the cube it started in,
// which does not look like water — it looks like a grid. The failure is easy to reintroduce because
// the boundary is also what makes a shoreline reflect, and because a patch only exists while it has
// something on it, so the wave has to bring its neighbours into existence as it reaches them.
static void test_surface_crosses_voxels(void)
{
  printf("waves cross voxel boundaries\n");

  World *world = make_world(16, 16, 4);
  add_floor(world);
  for (uint32_t y = 0; y < 16; y++)
    for (uint32_t x = 0; x < 16; x++)
      put_water(world, x, y, 1, FLUID_LEVEL_FULL);
  run_until_rest(world, 400);

  FluidSurface *surface = fluid_surface_create(64);
  CHECK(surface != NULL, "could not create the surface cache");
  if (!surface)
  {
    world_destroy(world);
    return;
  }

  // One drop, in the middle, hard enough to be worth following.
  const FluidSplash drop = {.x = 8, .y = 8, .z = 1, .from_dx = 0, .from_dy = 0, .from_dz = -1,
                            .units = 64};
  fluid_surface_splash(surface, world, &drop);
  CHECK(fluid_surface_live_count(surface) == 1, "a single drop made %d surfaces",
        fluid_surface_live_count(surface));

  // Only the struck voxel has anything on it to begin with.
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if (dx || dy)
        CHECK(fluid_surface_heights(surface, world, 8 + dx, 8 + dy, 1) == NULL,
              "the splash itself reached (%d,%d)", dx, dy);

  // A second of it. The wave travels about a sub-voxel a step, so this is long enough to leave the
  // voxel it started in and reach the ones beyond that.
  for (int i = 0; i < FLUID_SURFACE_HZ; i++)
    fluid_surface_step(surface, 1.0f / (float)FLUID_SURFACE_HZ);

  int reached = 0;
  int reached_diagonally = 0;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++)
    {
      if (!dx && !dy)
        continue;
      const int16_t *n = fluid_surface_heights(surface, world, 8 + dx, 8 + dy, 1);
      if (!n || surface_energy(n) == 0)
        continue;
      reached++;
      if (dx && dy)
        reached_diagonally++;
    }
  CHECK(reached >= 4, "the ripple reached %d neighbouring voxels, expected at least the four sides",
        reached);

  // Diagonals are reached the same way water reaches them — through the sides, in two hops — so
  // finding them wet is a sign the wave is really being carried rather than stamped outwards.
  CHECK(reached_diagonally > 0, "the ripple spread along the axes only");

  // The mean stays put: crossing a boundary moves the disturbance between patches without either
  // side gaining or losing water, which is the coarse simulation's business alone.
  long long total = 0;
  for (int dy = -3; dy <= 3; dy++)
    for (int dx = -3; dx <= 3; dx++)
    {
      const int16_t *n = fluid_surface_heights(surface, world, 8 + dx, 8 + dy, 1);
      if (n)
        total += surface_sum(n);
    }
  CHECK(total > -FLUID_SURFACE_UNIT && total < FLUID_SURFACE_UNIT,
        "propagation gained or lost %lld, more than a sub-voxel across the pool", total);

  fluid_surface_destroy(surface);
  world_destroy(world);
}

static void test_surface_cache_bounds(void)
{
  printf("surface cache stays within its budget\n");

  World *world = make_world(32, 32, 4);
  add_floor(world);
  for (uint32_t y = 0; y < 32; y++)
    for (uint32_t x = 0; x < 32; x++)
      put_water(world, x, y, 1, FLUID_LEVEL_FULL);
  run_until_rest(world, 400);

  const int cap = 16;
  FluidSurface *surface = fluid_surface_create(cap);
  CHECK(surface != NULL, "could not create the surface cache");
  if (!surface)
  {
    world_destroy(world);
    return;
  }

  // Still water has no surface to draw: that is what keeps a lake free, and it is worth pinning
  // because the alternative — a patch per visible face — is the obvious implementation.
  uint32_t texels[FLUID_SURFACE_CELLS];
  CHECK(!fluid_surface_bake_voxel(surface, world, 4, 4, 1, 50, 100, 200, texels),
        "an undisturbed surface was baked");
  CHECK(fluid_surface_live_count(surface) == 0, "drawing still water allocated %d patches",
        fluid_surface_live_count(surface));

  // Disturb far more voxels than the cache holds, twice over, so eviction and reuse both run.
  for (int pass = 0; pass < 2; pass++)
    for (uint32_t y = 0; y < 32; y++)
      for (uint32_t x = 0; x < 32; x++)
      {
        const FluidSplash drop = {(uint16_t)x, (uint16_t)y, 1, 0, 0, -1, 8};
        fluid_surface_splash(surface, world, &drop);
        CHECK(fluid_surface_live_count(surface) <= cap, "cache grew to %d past its cap of %d",
              fluid_surface_live_count(surface), cap);
        if (fluid_surface_live_count(surface) > cap)
        {
          pass = 2;
          y = 32;
          break;
        }
      }

  // The most recently disturbed surface has to still be there; that is the whole point of retiring
  // the least recent.
  CHECK(fluid_surface_heights(surface, world, 31, 31, 1) != NULL,
        "the surface just disturbed was retired");
  CHECK(fluid_surface_bake_voxel(surface, world, 31, 31, 1, 50, 100, 200, texels),
        "a disturbed surface would not bake");

  // A voxel that is not water has no surface to bake.
  CHECK(!fluid_surface_bake_voxel(surface, world, 0, 0, 0, 50, 100, 200, texels),
        "baked a surface for bedrock");

  // And once the ripples die the patches go with them, so the cache does not fill up with flat
  // water and refuse the next splash.
  for (int i = 0; i < 6000; i++)
    fluid_surface_step(surface, 1.0f / (float)FLUID_SURFACE_HZ);
  CHECK(fluid_surface_live_count(surface) == 0, "%d flat surfaces were still being kept",
        fluid_surface_live_count(surface));

  fluid_surface_destroy(surface);
  world_destroy(world);
}

static void test_rain_surface_splashes(void)
{
  printf("rain ripples on water surfaces\n");

  World *world = make_world(16, 16, 4);
  add_floor(world);
  for (uint32_t y = 0; y < 16; y++)
    for (uint32_t x = 0; x < 16; x++)
      put_water(world, x, y, 1, FLUID_LEVEL_FULL);
  run_until_rest(world, 200);

  particle_effects_init_for_world(world);
  particle_effects_set_heavy_rain_enabled(world, true);

  // One second of heavy rain over a full pool, centred on the middle.
  for (int i = 0; i < 60; i++)
    particle_effects_update(world, 1.0f / 60.0f, 8.0f, 8.0f, 2.0f, 64, NULL);

  FluidSplash batch[256];
  int queued = 0;
  for (;;)
  {
    const int got = particle_effects_drain_surface_splashes(world, batch + queued, 64);
    if (got <= 0)
      break;
    queued += got;
    if (queued >= 256)
      break;
  }
  CHECK(queued > 0, "heavy rain queued no surface splashes");

  FluidSurface *surface = fluid_surface_create(64);
  CHECK(surface != NULL, "could not create the surface cache");
  if (surface)
  {
    for (int i = 0; i < queued; i++)
      fluid_surface_splash(surface, world, &batch[i]);
    CHECK(fluid_surface_live_count(surface) > 0, "rain splashes did not disturb any surface");
    fluid_surface_destroy(surface);
  }

  world_destroy(world);
}

static void test_flowing_water_erodes_soil(void)
{
  printf("flowing water erodes soil\n");

  World *world = make_world(8, 4, 6);
  // Bedrock floor, soil ledge, water that must spill sideways over the soil.
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 0; x < 8; x++)
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 0; x < 5; x++)
      world_set_voxel(world, x, y, 1, VOXEL_SOIL);

  water_erosion_set_rate_scale(20000);
  put_water(world, 1, 1, 2, FLUID_LEVEL_FULL);
  put_water(world, 2, 1, 2, FLUID_LEVEL_FULL);
  put_water(world, 3, 1, 2, FLUID_LEVEL_FULL);

  int eroded = 0;
  for (int i = 0; i < 400; i++)
  {
    fluid_sim_step(world, 1 << 20);
    eroded = 0;
    for (uint32_t y = 0; y < 4; y++)
      for (uint32_t x = 0; x < 5; x++)
      {
        Voxel *v = world_get_voxel(world, x, y, 1);
        if (v && v->type != VOXEL_SOIL)
          eroded++;
      }
    if (eroded > 0)
      break;
  }
  water_erosion_set_rate_scale(1);

  CHECK(eroded > 0, "accelerated flowing water did not abrade the soil bed");
  world_destroy(world);
}

static void test_weather_leaves_surface_water(void)
{
  printf("mountain rivulets leave surface water\n");

  World *world = make_world(24, 24, 16);
  int *tops = (int *)malloc((size_t)24 * 24 * sizeof(int));
  CHECK(tops != NULL, "could not allocate tops");
  if (!tops)
  {
    world_destroy(world);
    return;
  }

  // Mountain face draining into a low shelf: high ridge at y=0, unit drop each row to a basin.
  for (uint32_t y = 0; y < 24; y++)
  {
    for (uint32_t x = 0; x < 24; x++)
    {
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
      int h = 12 - (int)y;
      if (h < 3)
        h = 3;
      for (int z = 1; z <= h; z++)
        world_set_voxel(world, x, y, (uint32_t)z, (z == h) ? VOXEL_STONE_GRANITE : VOXEL_STONE);
      tops[(size_t)y * 24 + x] = h;
    }
  }

  world->universe_x = 3;
  world->universe_y = 7;
  world->universe_z = 0;
  water_erosion_simulate_weather(world, 0xC0FFEE01u, tops);

  int water_cells = 0;
  int carved = 0;
  for (uint32_t y = 0; y < 24; y++)
    for (uint32_t x = 0; x < 24; x++)
    {
      const int expected = (12 - (int)y < 3) ? 3 : (12 - (int)y);
      if (tops[(size_t)y * 24 + x] < expected)
        carved++;
      for (uint32_t z = 0; z < world->depth; z++)
      {
        Voxel *v = world_get_voxel(world, x, y, z);
        if (v && v->type == VOXEL_WATER)
          water_cells++;
      }
    }

  CHECK(carved > 0, "rivulet pass carved no mountain grooves");
  CHECK(water_cells > 0, "rivulet pass left no surface water");
  free(tops);
  world_destroy(world);
}

// The isometric emitter only visits occupancy bits. Water used to be cleared on every full
// rebuild (while incremental writes set it), so lakes vanished after generation/load even though
// the voxels and fluid sim were fine. Refresh must leave water marked.
static void test_water_occupancy_survives_refresh(void)
{
  printf("water occupancy survives refresh\n");

  World *world = make_world(8, 8, 8);
  add_floor(world);
  put_water(world, 3, 3, 1, FLUID_LEVEL_FULL);
  put_water(world, 4, 3, 1, FLUID_LEVEL_FULL);

  CHECK(world_refresh_occupancy_bitfield(world), "occupancy refresh builds");
  CHECK(world->occupancy_bits != NULL, "occupancy bitfield present");
  CHECK(gpu_voxel_buffer_occupied(world->occupancy_bits, 3, 3, 1),
        "water cell marked occupied after refresh");
  CHECK(gpu_voxel_buffer_occupied(world->occupancy_bits, 4, 3, 1),
        "second water cell marked occupied after refresh");
  CHECK(!gpu_voxel_buffer_occupied(world->occupancy_bits, 3, 3, 2),
        "air above water stays clear");
  CHECK(gpu_voxel_buffer_occupied(world->occupancy_bits, 3, 3, 0),
        "bedrock under water stays occupied");

  // A second refresh must not drop water again (the old clear-on-rebuild bug).
  CHECK(world_refresh_occupancy_bitfield(world), "second occupancy refresh");
  CHECK(gpu_voxel_buffer_occupied(world->occupancy_bits, 3, 3, 1),
        "water still occupied after a second refresh");

  world_destroy(world);
}

static void put_magma(World *world, uint32_t x, uint32_t y, uint32_t z, int level)
{
  world_set_voxel(world, x, y, z, VOXEL_MAGMA);
  Voxel *v = world_get_voxel(world, x, y, z);
  if (v)
    voxel_set_quantity(v, (uint8_t)level);
  fluid_sim_touch(world, (int)x, (int)y, (int)z);
}

static void test_magma_hotspot_sustain_and_cool(void)
{
  printf("magma hotspots sustain; off-vent magma cools to basalt\n");

  World *world = make_world(32, 32, 4);
  add_floor(world);

  int hot_x = -1, hot_y = -1, cold_x = -1, cold_y = -1;
  for (int y = 1; y < 31; y++)
  {
    for (int x = 1; x < 31; x++)
    {
      if (world_magma_hotspot_at(world, x, y))
      {
        if (hot_x < 0)
        {
          hot_x = x;
          hot_y = y;
        }
      }
      else if (cold_x < 0)
      {
        cold_x = x;
        cold_y = y;
      }
      if (hot_x >= 0 && cold_x >= 0)
        break;
    }
    if (hot_x >= 0 && cold_x >= 0)
      break;
  }

  CHECK(hot_x >= 0, "found at least one bedrock hotspot in a 32x32 slice");
  CHECK(cold_x >= 0, "found at least one non-hotspot column");
  if (hot_x < 0 || cold_x < 0)
  {
    world_destroy(world);
    return;
  }

  // Stone walls so magma cannot creep into neighbouring columns before the cool clock finishes.
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
      if (dx == 0 && dy == 0)
        continue;
      world_set_voxel(world, (uint32_t)(hot_x + dx), (uint32_t)(hot_y + dy), 1, VOXEL_STONE);
      world_set_voxel(world, (uint32_t)(cold_x + dx), (uint32_t)(cold_y + dy), 1, VOXEL_STONE);
    }

  put_magma(world, (uint32_t)hot_x, (uint32_t)hot_y, 1, FLUID_LEVEL_FULL);
  put_magma(world, (uint32_t)cold_x, (uint32_t)cold_y, 1, FLUID_LEVEL_FULL);

  for (int i = 0; i < 64; i++)
    fluid_sim_step(world, 4096);

  Voxel *on_vent = world_get_voxel(world, (uint32_t)hot_x, (uint32_t)hot_y, 1);
  Voxel *off_vent = world_get_voxel(world, (uint32_t)cold_x, (uint32_t)cold_y, 1);
  CHECK(on_vent && on_vent->type == VOXEL_MAGMA, "magma above a hotspot stays molten");
  CHECK(off_vent && off_vent->type == VOXEL_STONE_BASALT,
        "magma off a hotspot cools to basalt");

  world_destroy(world);
}

static void test_magma_collapses_off_ledge(void)
{
  printf("unsupported magma collapses under viscosity\n");

  World *world = make_world(12, 6, 6);
  for (uint32_t y = 0; y < 6; y++)
    for (uint32_t x = 0; x < 12; x++)
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);

  // Narrow shelf: magma at z=2 over air should fall onto bedrock at z=1.
  for (uint32_t y = 0; y < 6; y++)
    for (uint32_t x = 0; x < 4; x++)
      world_set_voxel(world, x, y, 1, VOXEL_STONE);

  put_magma(world, 6, 3, 2, FLUID_LEVEL_FULL);

  for (int i = 0; i < 40; i++)
    fluid_sim_step(world, 4096);

  Voxel *air_cell = world_get_voxel(world, 6, 3, 2);
  CHECK(air_cell && air_cell->type != VOXEL_MAGMA,
        "unsupported magma left the air cell (fell or spread)");

  int magma_on_floor = 0;
  int basalt_on_floor = 0;
  for (uint32_t y = 0; y < 6; y++)
    for (uint32_t x = 4; x < 12; x++)
    {
      Voxel *v = world_get_voxel(world, x, y, 1);
      if (!v)
        continue;
      if (v->type == VOXEL_MAGMA)
        magma_on_floor++;
      if (v->type == VOXEL_STONE_BASALT)
        basalt_on_floor++;
    }
  CHECK(magma_on_floor + basalt_on_floor > 0,
        "fallen magma pooled on the bedrock floor (still molten or already basalt)");

  world_destroy(world);
}

int main(void)
{
  printf("=== fluid simulation ===\n");
  test_conservation_and_collapse();
  test_flat_equilibrium();
  test_communicating_vessels();
  test_quiescence();
  test_determinism();
  test_budget_is_respected();
  test_budget_does_not_starve();
  test_no_uphill_flow();
  test_surface_wave();
  test_surface_crosses_voxels();
  test_surface_cache_bounds();
  test_rain_surface_splashes();
  test_flowing_water_erodes_soil();
  test_weather_leaves_surface_water();
  test_water_occupancy_survives_refresh();
  test_magma_hotspot_sustain_and_cool();
  test_magma_collapses_off_ledge();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
