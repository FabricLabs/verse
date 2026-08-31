// VERSE performance benchmark.
//
// A permanent tool for optimisation work, not a test: it never fails a build, it reports numbers.
// Every section prints a per-operation cost and, where it makes sense, how that cost compares to
// one frame's budget at the target frame rate. The point is that any claim about a change making
// things faster can be checked, and that a regression shows up as a number rather than as a
// complaint about the game feeling sluggish.
//
//   make verse-benchmark && ./verse-benchmark
//   ./verse-benchmark --target-fps 120 --reps 5
//   ./verse-benchmark --only physics
//   ./verse-benchmark --csv >> bench-history.csv
//
// Runs headless. The renderer section exercises the real isometric voxel scan, which is pure CPU
// work and needs no window, so this is safe in CI.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "constants.h"
#include "world.h"
#include "world_spawn.h"
#include "fluid_sim.h"
#include "fluid_surface.h"
#include "gpu_voxel_buffer.h"
#include "isometric_renderer.h"
#include "fog_of_war.h"
#include "shadow_world.h"
#include "task_scheduler.h"
#include "voxel_combat.h"
#include "voxel_fracture.h"
#include "voxel_debris_volume.h"
#include "poly_mesh.h"
#include "mob_models.h"
#include "mob_ai.h"
#include "actor.h"

// ---------------------------------------------------------------------------------------------
// Timing and reporting
// ---------------------------------------------------------------------------------------------

static double now_seconds(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static double g_target_fps = 120.0;
static int g_reps = 5;
static bool g_csv = false;
static bool g_fast = false;
static const char *g_only = NULL;
static const char *g_dump_frame = NULL;

static double frame_budget_ms(void) { return 1000.0 / g_target_fps; }

// One measured result. `work` is an optional count of things processed, used to derive a rate.
static void report(const char *section, const char *what, double ms, double work,
                   const char *work_unit)
{
  if (g_csv) {
    printf("%s,%s,%.4f,%.0f,%s\n", section, what, ms, work, work_unit ? work_unit : "");
    return;
  }

  const double budget = frame_budget_ms();
  char verdict[32];
  if (ms <= budget * 0.1)
    snprintf(verdict, sizeof(verdict), "%5.1f%% frame", ms / budget * 100.0);
  else if (ms <= budget)
    snprintf(verdict, sizeof(verdict), "%5.1f%% frame", ms / budget * 100.0);
  else
    snprintf(verdict, sizeof(verdict), "%5.1fx OVER", ms / budget);

  printf("  %-46s %9.3f ms  %s", what, ms, verdict);
  if (work > 0.0 && work_unit) {
    const double per_second = work / (ms / 1000.0);
    if (per_second >= 1e9)
      printf("   %6.2f G%s/s", per_second / 1e9, work_unit);
    else if (per_second >= 1e6)
      printf("   %6.2f M%s/s", per_second / 1e6, work_unit);
    else
      printf("   %6.2f K%s/s", per_second / 1e3, work_unit);
  }
  printf("\n");
}

static void section(const char *title)
{
  if (g_csv) return;
  printf("\n%s\n", title);
  for (const char *p = title; *p; p++) putchar('-');
  printf("\n");
}

static bool want(const char *name)
{
  return !g_only || strcmp(g_only, name) == 0;
}

// Best of `g_reps` runs.
//
// The best run, not the mean or the median. All three estimate the same thing when the machine is
// quiet, but only the best is stable when it is not: interference from other processes can only ever
// make a run slower, so the fastest run is the one least polluted by it. On a loaded machine the
// median of five moved by 40% between runs of identical code, which is enough to invent an
// improvement that is not there or hide one that is.
//
// The cost of this choice: these numbers are a floor, not what a player will see. A frame budget
// verdict of "fits" here means "fits when nothing else is competing".
#define MEASURE(out_ms, body)                          \
  do {                                                 \
    const int reps = (g_reps > 64) ? 64 : g_reps;       \
    double best_ = 0.0;                                 \
    for (int rep_ = 0; rep_ < reps; rep_++) {           \
      const double t_ = now_seconds();                  \
      body;                                             \
      const double e_ = (now_seconds() - t_) * 1000.0;  \
      if (rep_ == 0 || e_ < best_) best_ = e_;           \
    }                                                   \
    (out_ms) = best_;                                    \
  } while (0)

static uint32_t bench_mix(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
  uint32_t h = 2166136261u;
  h = (h ^ a) * 16777619u;
  h = (h ^ b) * 16777619u;
  h = (h ^ c) * 16777619u;
  h = (h ^ d) * 16777619u;
  h ^= h >> 15;
  return h;
}

// ---------------------------------------------------------------------------------------------
// World fixtures
// ---------------------------------------------------------------------------------------------

static World *make_empty_world(void)
{
  return world_create(WORLD_SIZE_CUBE);
}

static World *make_home_world(const char *seed)
{
  World *w = world_create(WORLD_SIZE_CUBE);
  if (w) world_generate_with_type(w, seed, WORLD_TYPE_HOME);
  return w;
}

// A world with a real body of water in it, so the fluid simulation has something to do. Without
// this the fluid numbers only describe fixed overhead.
static World *make_watery_world(void)
{
  World *w = world_create(WORLD_SIZE_CUBE);
  if (!w) return NULL;

  const uint32_t floor_top = w->depth / 2;
  for (uint32_t z = 0; z < floor_top; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);

  // A block of water sitting on the floor, off to one side so it has somewhere to spread.
  for (uint32_t z = floor_top; z < floor_top + 12 && z < w->depth; z++)
    for (uint32_t y = 24; y < 56; y++)
      for (uint32_t x = 24; x < 56; x++)
        world_set_voxel(w, x, y, z, VOXEL_WATER);

  return w;
}

// A floor with walls around a square of it, so fluid released inside has a level to find rather
// than an unbounded floor to thin out over.
static World *make_walled_floor(uint32_t floor_top, uint32_t radius, uint32_t wall_height)
{
  World *w = world_create(WORLD_SIZE_CUBE);
  if (!w) return NULL;

  for (uint32_t z = 0; z < floor_top; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);

  const uint32_t cx = w->width / 2, cy = w->height / 2;
  for (uint32_t z = floor_top; z < floor_top + wall_height && z < w->depth; z++)
    for (uint32_t y = cy - radius; y <= cy + radius; y++)
      for (uint32_t x = cx - radius; x <= cx + radius; x++)
        if (x == cx - radius || x == cx + radius || y == cy - radius || y == cy + radius)
          world_set_voxel(w, x, y, z, VOXEL_STONE);

  return w;
}

// A block of water in a basin, off-centre so it has to travel to level.
static World *make_basin_world(void)
{
  const uint32_t floor_top = 64, radius = 24;
  World *w = make_walled_floor(floor_top, radius, 20);
  if (!w) return NULL;

  const uint32_t cx = w->width / 2, cy = w->height / 2;
  for (uint32_t z = floor_top; z < floor_top + 12; z++)
    for (uint32_t y = cy - radius + 1; y < cy; y++)
      for (uint32_t x = cx - radius + 1; x < cx; x++)
        world_set_voxel(w, x, y, z, VOXEL_WATER);
  return w;
}

// A source at the top of a shaft, falling the depth of the world into a walled basin. Water that
// never settles: the shape of the load a world with running water in it puts on every tick.
static World *make_waterfall_world(void)
{
  World *w = make_walled_floor(8, 24, 16);
  if (!w) return NULL;
  world_set_voxel(w, w->width / 2, w->height / 2, w->depth - 2, VOXEL_WATER);
  return w;
}

// ---------------------------------------------------------------------------------------------
// Voxel access — the memory-bandwidth floor everything else sits on top of
// ---------------------------------------------------------------------------------------------

static void bench_voxel_access(void)
{
  if (!want("voxels")) return;
  section("Voxel access (the floor: no algorithm can beat these)");

  World *w = make_home_world("bench-access");
  if (!w) return;

  const double cells = (double)w->width * w->height * w->depth;
  const double megabytes = cells * (double)sizeof(Voxel) / (1024.0 * 1024.0);
  if (!g_csv) {
    printf("  world %ux%ux%u, sizeof(Voxel)=%zu bytes, %.0f MB of voxels\n\n",
           w->width, w->height, w->depth, sizeof(Voxel), megabytes);
  }

  volatile unsigned long long sink = 0;
  double ms;

  MEASURE(ms, {
    for (uint32_t z = 0; z < w->depth; z++)
      for (uint32_t y = 0; y < w->height; y++)
        for (uint32_t x = 0; x < w->width; x++) {
          Voxel *v = world_get_voxel(w, x, y, z);
          if (v) sink += (unsigned long long)v->type;
        }
  });
  report("voxels", "full volume via world_get_voxel()", ms, cells, "voxel");

  MEASURE(ms, {
    for (uint32_t z = 0; z < w->depth; z++)
      for (uint32_t y = 0; y < w->height; y++)
        for (uint32_t x = 0; x < w->width; x++)
          sink += (unsigned long long)world_voxel_ptr_fast(w, (int)x, (int)y, (int)z)->type;
  });
  report("voxels", "full volume via world_voxel_ptr_fast()", ms, cells, "voxel");

  MEASURE(ms, {
    const size_t total = (size_t)w->width * w->height * w->depth;
    for (size_t i = 0; i < total; i++) sink += (unsigned long long)w->voxels[i].type;
  });
  report("voxels", "full volume, flat linear scan", ms, cells, "voxel");

  // Column scans are what the surface passes in the fluid step and the height map do.
  MEASURE(ms, {
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
        for (int z = (int)w->depth - 1; z >= 0; z--)
          if (world_voxel_ptr_fast(w, (int)x, (int)y, z)->type != VOXEL_AIR) { sink++; break; }
  });
  report("voxels", "top-of-column scan, every column", ms,
         (double)w->width * w->height, "column");

  (void)sink;
  world_destroy(w);
}

// ---------------------------------------------------------------------------------------------
// World generation
// ---------------------------------------------------------------------------------------------

static void bench_generation(void)
{
  if (!want("generation")) return;
  section("World generation (once per world, not per frame)");

  struct { WorldGenerationType type; const char *name; bool slow; } kinds[] = {
      {WORLD_TYPE_HOME, "home island", false},
      {WORLD_TYPE_WILDERNESS, "wilderness", true},
      {WORLD_TYPE_FARM, "farm", true},
  };

  for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
    if (kinds[i].slow && g_fast) continue;
    double ms;
    MEASURE(ms, {
      World *w = world_create(WORLD_SIZE_CUBE);
      if (w) {
        world_generate_with_type(w, "bench-generation", kinds[i].type);
        world_destroy(w);
      }
    });
    char label[96];
    snprintf(label, sizeof(label), "generate %s", kinds[i].name);
    report("generation", label, ms, 0.0, NULL);
  }
}

// ---------------------------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------------------------

// Step until nothing is left to move, or until `max_ticks` is spent. Reports the first tick and the
// worst tick separately from the mean, because those are what a frame has to survive: fluid work is
// not evenly spread, it spikes when a body of water is released and decays as it settles.
typedef struct {
  int ticks;
  double first_ms;
  double worst_ms;
  double total_ms;
  bool settled;
  FluidStepStats worst; // what the expensive tick was doing, so a spike can be attributed
} FluidRun;

static FluidRun run_until_settled(World *w, int budget, int max_ticks)
{
  FluidRun r = {0, 0.0, 0.0, 0.0, false, {0, 0, 0, 0, false}};
  for (int i = 0; i < max_ticks; i++) {
    const double t = now_seconds();
    const FluidStepStats s = fluid_sim_step(w, budget);
    const double e = (now_seconds() - t) * 1000.0;

    r.ticks++;
    r.total_ms += e;
    if (i == 0) r.first_ms = e;
    if (e > r.worst_ms) {
      r.worst_ms = e;
      r.worst = s;
    }
    if (s.at_rest) {
      r.settled = true;
      break;
    }
  }
  return r;
}

static void bench_physics(void)
{
  if (!want("physics")) return;
  section("Physics (per simulation tick)");

  const int budget = (int)(WORLD_SIZE_X * WORLD_SIZE_Y);
  double ms;

  // The empty-world number is the important one: whatever it costs is pure overhead, because
  // there is nothing in the world to simulate.
  World *empty = make_empty_world();
  if (empty) {
    fluid_sim_step(empty, budget); // drain the initial seed, so this measures the steady state
    MEASURE(ms, { world_step_fluids(empty, budget); });
    report("physics", "fluid step, empty world (pure overhead)", ms, 0.0, NULL);
    world_destroy(empty);
  }

  World *home = make_home_world("bench-physics");
  if (home) {
    fluid_sim_step(home, budget);
    MEASURE(ms, { world_step_fluids(home, budget); });
    report("physics", "fluid step, home island (no fluid)", ms, 0.0, NULL);

    MEASURE(ms, { world_step_actors(home, 1.0f / 120.0f); });
    report("physics", "world_step_actors, home island", ms, 0.0, NULL);

    MEASURE(ms, { world_update_springs(home, 0); });
    report("physics", "world_update_springs, home island", ms, 0.0, NULL);
    world_destroy(home);
  }

  // Releasing a body of water onto an open floor: the worst tick the simulation has to survive, and
  // the one place it is asked to move a large amount of fluid at once.
  World *wet = make_watery_world();
  if (wet) {
    const FluidRun r = run_until_settled(wet, budget, 400);
    report("physics", "fluid step, 32x32x12 released (first tick)", r.first_ms, 0.0, NULL);
    report("physics", "fluid step, same body (worst of 400)", r.worst_ms, 0.0, NULL);
    report("physics", "fluid step, same body (mean of 400)", r.total_ms / r.ticks, 0.0, NULL);
    if (!g_csv)
      printf("  %-46s %d cells, %d transfers, %lld levels, %d left queued\n", "  worst tick did",
             r.worst.cells_visited, r.worst.transfers, r.worst.moved, r.worst.queued);

    // The cost of not knowing where the fluid is. Every bulk write that cannot enumerate what it
    // touched — world generation, a loaded save — buys one of these on the next step.
    MEASURE(ms, {
      fluid_sim_invalidate(wet);
      fluid_sim_step(wet, 1);
    });
    report("physics", "fluid reseed, full rescan after a bulk write", ms,
           (double)wet->width * wet->height * wet->depth, "cell");
    world_destroy(wet);
  }

  // Settling, measured where it can be measured: water in a basin has somewhere to level to, and
  // reaches it. A cheap tick that needs thousands of them to level a pool is not a fast simulation,
  // so this reports the whole cost of getting there and checks the result really is flat.
  World *basin = make_basin_world();
  if (basin) {
    const long long before = fluid_sim_total_level(basin);
    const FluidRun r = run_until_settled(basin, budget, 20000);
    const long long after = fluid_sim_total_level(basin);

    report("physics", "fluid step, basin settling (mean tick)", r.total_ms / r.ticks, 0.0, NULL);
    if (!g_csv) {
      int lo = 1 << 30, hi = 0;
      for (uint32_t y = 0; y < basin->height; y++)
        for (uint32_t x = 0; x < basin->width; x++) {
          int col = 0;
          for (uint32_t z = 0; z < basin->depth; z++)
            col += fluid_level_of(world_voxel_cptr_fast(basin, (int)x, (int)y, (int)z));
          if (col == 0) continue;
          if (col < lo) lo = col;
          if (col > hi) hi = col;
        }
      printf("  %-46s %s in %d tick%s, %.1f ms total, %s, surface flat to %.2f voxel\n",
             "  settling", r.settled ? "levelled" : "STILL MOVING", r.ticks,
             r.ticks == 1 ? "" : "s", r.total_ms,
             before == after ? "mass conserved" : "MASS LOST",
             (double)(hi - lo) / (double)FLUID_LEVEL_FULL);
    }

    // Now that it is level: this is what a world with a lake in it costs per tick, and it is the
    // number that decides how often the simulation can afford to run.
    MEASURE(ms, { world_step_fluids(basin, budget); });
    report("physics", "fluid step, settled lake (nothing to do)", ms, 0.0, NULL);
    world_destroy(basin);
  }

  // A spring feeding a basin: fluid in motion that never settles, which is the steady-state load
  // of any world with running water in it.
  World *fall = make_waterfall_world();
  if (fall) {
    for (int i = 0; i < 200; i++) fluid_sim_step(fall, budget); // let the stream reach the basin

    double total = 0.0, worst = 0.0;
    const int ticks = 60;
    for (int i = 0; i < ticks; i++) {
      world_set_voxel(fall, fall->width / 2, fall->height / 2, fall->depth - 2, VOXEL_WATER);
      const double t = now_seconds();
      fluid_sim_step(fall, budget);
      const double e = (now_seconds() - t) * 1000.0;
      total += e;
      if (e > worst) worst = e;
    }
    report("physics", "fluid step, waterfall running (mean)", total / ticks, 0.0, NULL);
    report("physics", "fluid step, waterfall running (worst)", worst, 0.0, NULL);
    world_destroy(fall);
  }

  // Editor-shaped load. The world editor used to pass the entire 128³ volume as the fluid budget
  // and then catch up missed simulated seconds in a tight loop. These numbers are why that froze:
  // one full-budget tick on a wet world is already past a frame, and a full occupancy rebuild sits
  // on top of it. The capped budget is what the editor uses now (and what the game has always used).
  //
  // Each trial rebuilds the queue from scratch: MEASURE keeps the best of several reps, and a
  // drained queue would make the "full volume" path look free.
  {
    const int editor_budget = 4096;
    World *wet = make_watery_world();
    if (wet) {
      const int volume = (int)((size_t)wet->width * wet->height * wet->depth);
      double best_full = 0.0, best_cap = 0.0;
      const int reps = (g_reps > 64) ? 64 : g_reps;

      // Cap first: full-volume trials spread the water and inflate the queued set.
      for (int rep = 0; rep < reps; rep++) {
        fluid_sim_invalidate(wet);
        const double t0 = now_seconds();
        fluid_sim_step(wet, editor_budget);
        const double e0 = (now_seconds() - t0) * 1000.0;
        if (rep == 0 || e0 < best_cap) best_cap = e0;
      }
      report("physics", "editor NEW: 4096-cell budget, first wet tick", best_cap, 0.0, NULL);

      World *wet_full = make_watery_world();
      if (wet_full) {
        for (int rep = 0; rep < reps; rep++) {
          fluid_sim_invalidate(wet_full);
          const double t0 = now_seconds();
          fluid_sim_step(wet_full, volume);
          const double e0 = (now_seconds() - t0) * 1000.0;
          if (rep == 0 || e0 < best_full) best_full = e0;
        }
        report("physics", "editor OLD: full-volume budget, first wet tick", best_full, 0.0, NULL);

        fluid_sim_invalidate(wet_full);
        const int catchup = 5;
        const double t_catch = now_seconds();
        for (int i = 0; i < catchup; i++)
          fluid_sim_step(wet_full, volume);
        const double catch_ms = (now_seconds() - t_catch) * 1000.0;
        report("physics", "editor OLD: 5-step catch-up (death spiral sample)", catch_ms, 0.0, NULL);
        world_destroy(wet_full);
      }

      // Continue from a fresh capped first-tick so the follow-on numbers reflect a live flood.
      fluid_sim_invalidate(wet);
      (void)fluid_sim_step(wet, editor_budget);

      FluidStepStats peak = {0};
      double worst = 0.0, sum = 0.0;
      const int ticks = 60;
      for (int i = 0; i < ticks; i++) {
        const double t = now_seconds();
        const FluidStepStats s = fluid_sim_step(wet, editor_budget);
        const double e = (now_seconds() - t) * 1000.0;
        sum += e;
        if (e > worst) { worst = e; peak = s; }
      }
      report("physics", "editor NEW: 4096-cell budget, next 60 ticks mean", sum / ticks, 0.0, NULL);
      report("physics", "editor NEW: 4096-cell budget, next 60 ticks worst", worst, 0.0, NULL);
      if (!g_csv)
        printf("  %-46s %d cells, %d transfers, %d left queued\n", "  worst capped tick did",
               peak.cells_visited, peak.transfers, peak.queued);

      GpuVoxelBuffer *bits = gpu_voxel_buffer_create_from_world(wet);
      if (bits) {
        MEASURE(ms, { gpu_voxel_buffer_update_from_world(bits, wet); });
        report("physics", "editor OLD: full GPU occupancy rebuild (128^3)", ms,
               (double)volume, "cell");
        gpu_voxel_buffer_destroy(bits);
      }
      world_destroy(wet);
    }
  }

  // Sub-voxel surface waves. Driven by the render clock rather than the simulation, so this is a
  // per-frame cost. Sized at what one frame can actually draw: the atlas carries 128 live tiles.
  const int live = 128;
  FluidSurface *surf = fluid_surface_create(live);
  World *pool = make_basin_world();
  if (surf && pool) {
    run_until_settled(pool, budget, 20000); // a level surface, so every patch is a visible face

    // The exposed water faces, found rather than assumed: where the surface ends up is the
    // simulation's business, and a splash on a voxel that turned out to be air does nothing.
    int px[128], py[128], pz[128];
    int found = 0;
    for (uint32_t y = 0; y < pool->height && found < live; y++)
      for (uint32_t x = 0; x < pool->width && found < live; x++)
        for (int z = (int)pool->depth - 2; z >= 0; z--) {
          if (world_voxel_cptr_fast(pool, (int)x, (int)y, z)->type != VOXEL_WATER)
            continue;
          if (world_voxel_cptr_fast(pool, (int)x, (int)y, z + 1)->type == VOXEL_AIR) {
            px[found] = (int)x;
            py[found] = (int)y;
            pz[found] = z;
            found++;
          }
          break;
        }

    // Still water first: this is the case that runs almost all the time, and it should cost nothing
    // at all, because a surface with no ripple on it has no patch and nothing to bake.
    static uint32_t texels[FLUID_SURFACE_CELLS];
    MEASURE(ms, {
      fluid_surface_step(surf, 1.0f / (float)FLUID_SURFACE_HZ);
      for (int i = 0; i < found; i++)
        fluid_surface_bake_voxel(surf, pool, px[i], py[i], pz[i], 40, 90, 160, texels);
    });
    report("physics", "waves, still water in view (step + bake)", ms, 0.0, NULL);

    // Then with every surface in view ringing at once, which is the ceiling.
    for (int i = 0; i < found; i++) {
      const FluidSplash s = {(uint16_t)px[i], (uint16_t)py[i], (uint16_t)pz[i], 0, 0, -1, 40};
      fluid_surface_splash(surf, pool, &s);
    }
    if (!g_csv)
      printf("  %-46s %d ringing, of %d exposed faces\n", "  water surfaces",
             fluid_surface_live_count(surf), found);

    MEASURE(ms, { fluid_surface_step(surf, 1.0f / (float)FLUID_SURFACE_HZ); });
    report("physics", "wave step, every surface in view ringing", ms,
           (double)fluid_surface_live_count(surf) * FLUID_SURFACE_CELLS, "cell");

    MEASURE(ms, {
      for (int i = 0; i < found; i++)
        fluid_surface_bake_voxel(surf, pool, px[i], py[i], pz[i], 40, 90, 160, texels);
    });
    report("physics", "wave bake to texels, same", ms, (double)found * FLUID_SURFACE_CELLS,
           "texel");
  }
  fluid_surface_destroy(surf);
  world_destroy(pool);
}

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------

static void bench_rendering(void)
{
  if (!want("rendering")) return;
  section("Rendering (per frame — this is the 120 FPS budget)");

  World *w = make_home_world("bench-render");
  if (!w) return;

  // The client renders into a 256x240 internal framebuffer and upscales, so measure at that size.
  //
  // A software renderer over a plain surface is used rather than a window, which keeps this
  // headless while still going through isometric_renderer_render_gpu — the function the client
  // actually calls. Driving isometric_renderer_render_world directly would measure nothing
  // useful: render_gpu computes the tile size and screen centre first, and without that setup the
  // culling rejects every voxel and the scan reports an empty buffer.
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 256, 240, 32, SDL_PIXELFORMAT_RGBA8888);
  SDL_Renderer *sdl = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
  if (!sdl) {
    if (!g_csv)
      printf("  (skipped: no software renderer available — %s)\n", SDL_GetError());
    if (surface) SDL_FreeSurface(surface);
    world_destroy(w);
    return;
  }

  IsometricRenderer *r = isometric_renderer_create(256, 240);
  if (!r) {
    SDL_DestroyRenderer(sdl);
    SDL_FreeSurface(surface);
    world_destroy(w);
    return;
  }

  SpawnPosition spawn = world_find_best_spawn_position(w);
  const int cam_x = spawn.is_safe ? spawn.x : (int)(w->width / 2);
  const int cam_y = spawn.is_safe ? spawn.y : (int)(w->height / 2);
  const int cam_z = spawn.is_safe ? spawn.z : (int)(w->depth / 2);
  free(spawn.spawn_reason);

  // A pond next to the camera, so the frame contains water and the water is in motion. Every water
  // face carries its own baked wave tile, which nothing in a timing number can vouch for — a
  // dumped frame can. Cut into the terrain and filled, then settled, so it sits at its own level
  // rather than at whatever height it was poured in at.
  const int pond_r = 7;
  int pond_x = 0, pond_y = 0, pond_z = 0;
  {
    // Clear of the camera, so the player sprite drawn at the spawn does not sit on the water and
    // hide the thing the dump exists to show.
    const int px0 = cam_x + 12, py0 = cam_y + 12;
    int top = -1;
    for (int z = (int)w->depth - 1; z >= 0; z--)
      if (world_voxel_cptr_fast(w, px0, py0, z)->type != VOXEL_AIR) { top = z; break; }
    if (top > 2) {
      pond_x = px0;
      pond_y = py0;
      pond_z = top;
      for (int y = py0 - pond_r; y <= py0 + pond_r; y++)
        for (int x = px0 - pond_r; x <= px0 + pond_r; x++) {
          if (x < 1 || y < 1 || x >= (int)w->width - 1 || y >= (int)w->height - 1) continue;
          const bool rim = (x == px0 - pond_r || x == px0 + pond_r ||
                            y == py0 - pond_r || y == py0 + pond_r);
          for (int z = top - 2; z <= top + 2; z++)
            world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z,
                            rim ? VOXEL_STONE : VOXEL_AIR);
          if (!rim)
            for (int z = top - 2; z <= top; z++)
              world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_WATER);
        }
      for (int i = 0; i < 400; i++)
        if (fluid_sim_step(w, 1 << 20).at_rest) break;
    }
  }

  GameWorlds worlds = {0};
  worlds.home_world = w;

  isometric_renderer_set_game_worlds(r, &worlds);
  isometric_renderer_set_auto_center(r, false);
  isometric_renderer_set_camera(r, cam_x, cam_y, cam_z);
  isometric_renderer_set_camera_world(r, (float)cam_x, (float)cam_y, (float)cam_z);

  double ms;
  long emitted = 0;

  // One frame first, to let render_gpu establish the tile size and screen centre that the scan
  // and sort below depend on.
  isometric_renderer_render_gpu(r, sdl);
  emitted = r->render_buffer_size;
  if (!g_csv)
  {
    printf("  camera on the spawn, %ld voxels reach the draw buffer\n", emitted);
    // The scan's efficiency, which is the thing to watch: visited counts every cell the inner loop
    // touched, and anything much larger than emitted is the scan walking air it could have skipped.
    const long visited = r->scan_visited;
    printf("  scan visited %ld cells (%ld air, %ld solid) — %.0f visited per voxel emitted\n\n",
           visited, r->scan_air, r->scan_solid,
           emitted > 0 ? (double)visited / (double)emitted : 0.0);
  }

  // These two are the per-frame CPU cost, and they are what has to fit the frame budget. The GPU
  // does not help with either: both run before a single triangle is submitted.
  MEASURE(ms, {
    isometric_renderer_clear_buffer(r);
    isometric_renderer_render_world(r, w, 0, 0, 0, 0);
  });
  report("rendering", "CPU: voxel scan into the draw buffer", ms, (double)emitted, "voxel");

  MEASURE(ms, { isometric_renderer_sort_buffer(r); });
  report("rendering", "CPU: draw-order sort", ms, (double)emitted, "voxel");

  // Whole-frame figures. Careful reading these: this benchmark rasterises on the CPU through a
  // software renderer, whereas the client hands the triangles to the GPU. So the difference
  // between these and the scan above is an upper bound on the client's cost, not its cost.
  MEASURE(ms, {
    isometric_renderer_render_gpu(r, sdl);
    emitted = r->render_buffer_size;
  });
  report("rendering", "whole frame incl. software raster (GPU-bound in client)", ms,
         (double)emitted, "voxel");

  // Zoomed out puts more of the world on screen, the worst case for the scan.
  r->zoom_scale = 0.5f;
  isometric_renderer_render_gpu(r, sdl);
  MEASURE(ms, {
    isometric_renderer_clear_buffer(r);
    isometric_renderer_render_world(r, w, 0, 0, 0, 0);
    emitted = r->render_buffer_size;
  });
  report("rendering", "CPU: voxel scan, zoomed out 2x", ms, (double)emitted, "voxel");
  r->zoom_scale = 1.0f;
  isometric_renderer_render_gpu(r, sdl);

  // Culling off is not a mode the game uses; it is the ceiling the culling has to beat.
  isometric_renderer_set_disable_culling(r, true);
  MEASURE(ms, {
    isometric_renderer_clear_buffer(r);
    isometric_renderer_render_world(r, w, 0, 0, 0, 0);
    emitted = r->render_buffer_size;
  });
  report("rendering", "CPU: voxel scan, culling disabled (ceiling)", ms, (double)emitted, "voxel");
  isometric_renderer_set_disable_culling(r, false);

  // Optional frame dump. The software renderer has already drawn a real frame into `surface`, so
  // writing it out costs nothing and makes the rendering visually checkable — which matters for
  // changes like sub-voxel texturing, where a timing number cannot tell you the faces came out
  // right. Scaled up on the way out, because 256x240 of 32x32 tiles is hard to read at 1:1.
  if (g_dump_frame) {
    // Disturb the pond and let the ripples develop, so the dump shows a wave rather than a plate.
    // One stone in the middle of the pond, then a second of wave time. Long enough for the ripple
    // to leave the voxel it started in, which is the part worth looking at: a wave that stops at
    // the voxel boundary is a bug that reads as a grid of squares.
    const int drops = getenv("VERSE_BENCH_DROPS") ? atoi(getenv("VERSE_BENCH_DROPS")) : 1;
    const int wave_frames = getenv("VERSE_BENCH_WAVE") ? atoi(getenv("VERSE_BENCH_WAVE")) : 60;
    for (int drop = 0; drop < drops; drop++) {
      const FluidSplash s = {(uint16_t)pond_x, (uint16_t)pond_y, (uint16_t)pond_z, 0, 0, -1, 255};
      fluid_surface_splash(r->water_surface, w, &s);
    }
    // Stepped here rather than left to the renderer, which advances the field on wall-clock time:
    // correct in a game and useless in a benchmark, where it would make the frame depend on how
    // long the measurements above happened to take. Resetting the renderer's clock each frame keeps
    // its own step at zero.
    for (int f = 0; f < wave_frames; f++) {
      fluid_surface_step(r->water_surface, 1.0f / (float)FLUID_SURFACE_HZ);
      r->water_surface_last_ms = SDL_GetTicks();
      isometric_renderer_render_gpu(r, sdl);
    }
    if (!g_csv) {
      // The amplitude left in the field and how far it has spread, so a flat-looking pond can be
      // told apart from one whose waves the shading failed to show.
      int peak = 0, disturbed = 0;
      for (int dy = -pond_r; dy <= pond_r; dy++)
        for (int dx = -pond_r; dx <= pond_r; dx++) {
          const int16_t *hf =
              fluid_surface_heights(r->water_surface, w, pond_x + dx, pond_y + dy, pond_z);
          if (!hf) continue;
          int local = 0;
          for (int c = 0; c < FLUID_SURFACE_CELLS; c++)
            if (abs((int)hf[c]) > local) local = abs((int)hf[c]);
          if (local > FLUID_SURFACE_UNIT / 32) disturbed++;
          if (local > peak) peak = local;
        }
      printf("  %ld water surfaces drawn; wave peaks at %.2f sub-voxels across %d voxels\n",
             (long)r->water_faces_drawn, (double)peak / (double)FLUID_SURFACE_UNIT, disturbed);
    }

    const int scale = 4;
    // RGB888, not RGBA: the render target's alpha is whatever the geometry left behind, and saving
    // that produces a BMP that image tools read as transparent rather than as the frame.
    SDL_Surface *big = SDL_CreateRGBSurfaceWithFormat(0, surface->w * scale, surface->h * scale, 24,
                                                      SDL_PIXELFORMAT_RGB24);
    if (big && SDL_BlitScaled(surface, NULL, big, NULL) == 0 &&
        SDL_SaveBMP(big, g_dump_frame) == 0) {
      if (!g_csv) printf("\n  frame written to %s (%dx%d)\n", g_dump_frame, big->w, big->h);
    } else if (!g_csv) {
      printf("\n  frame dump failed: %s\n", SDL_GetError());
    }
    if (big) SDL_FreeSurface(big);
  }

  isometric_renderer_set_game_worlds(r, NULL);
  isometric_renderer_destroy(r);
  SDL_DestroyRenderer(sdl);
  SDL_FreeSurface(surface);
  world_destroy(w);
}

static void bench_rendering_neighbors(void)
{
  if (!want("rendering")) return;
  section("Rendering (neighbor ring — home + clouds above/below)");

  World *home = make_home_world("bench-render-neighbors");
  if (!home) return;

  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 256, 240, 32, SDL_PIXELFORMAT_RGBA8888);
  SDL_Renderer *sdl = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
  if (!sdl) {
    if (!g_csv)
      printf("  (skipped: no software renderer available — %s)\n", SDL_GetError());
    if (surface) SDL_FreeSurface(surface);
    world_destroy(home);
    return;
  }

  IsometricRenderer *r = isometric_renderer_create(256, 240);
  if (!r) {
    SDL_DestroyRenderer(sdl);
    SDL_FreeSurface(surface);
    world_destroy(home);
    return;
  }

  SpawnPosition spawn = world_find_best_spawn_position(home);
  const int cam_x = spawn.is_safe ? spawn.x : (int)(home->width / 2);
  const int cam_y = spawn.is_safe ? spawn.y : (int)(home->height / 2);
  const int cam_z = spawn.is_safe ? spawn.z : (int)(home->depth / 2);
  free(spawn.spawn_reason);

  int edge_count = 0;
  const int radius = r->neighbor_inclusion_radius;
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      for (int dz = -2; dz <= 2; dz++) {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        if (dz == -2)
          continue;
        int idx = isometric_renderer_offset_index(r, dx, dy, dz);
        if (idx <= 0)
          continue;
        World *ew = world_create(WORLD_SIZE_CUBE);
        if (!ew)
          continue;
        if (dz != 0)
          world_generate_with_type(ew, "bench-cloud", WORLD_TYPE_CLOUD);
        else
          world_generate_with_type_and_fill(ew, "bench-air", WORLD_TYPE_SOLID, VOXEL_AIR);
        r->edge_worlds[idx] = ew;
        edge_count++;
      }
    }
  }

  GameWorlds worlds = {0};
  worlds.home_world = home;

  isometric_renderer_set_game_worlds(r, &worlds);
  isometric_renderer_set_auto_center(r, false);
  isometric_renderer_set_camera(r, cam_x, cam_y, cam_z);
  isometric_renderer_set_camera_world(r, (float)cam_x, (float)cam_y, (float)cam_z);
  isometric_renderer_set_universe_layer(r, (uint64_t)UNIVERSE_HOME_Z, true);

  double ms;
  long emitted = 0;

  isometric_renderer_render_gpu(r, sdl);
  emitted = r->render_buffer_size;
  if (!g_csv) {
    printf("  %d edge worlds + center home, %ld voxels in draw buffer\n", edge_count, emitted);
    printf("  scan visited %ld cells (%ld air, %ld solid)\n\n",
           r->scan_visited, r->scan_air, r->scan_solid);
  }

  MEASURE(ms, {
    isometric_renderer_clear_buffer(r);
    isometric_renderer_render_world(r, home, 0, 0, 0, 0);
    for (int idx = 1; idx < 125; idx++) {
      World *ew = r->edge_worlds[idx];
      if (!ew)
        continue;
      WorldOffset o = r->world_offsets[idx];
      int horiz = abs(o.dx);
      if (abs(o.dy) > horiz)
        horiz = abs(o.dy);
      if (horiz > r->neighbor_inclusion_radius)
        continue;
      const bool vertical_cloud = (ew->generation_type == WORLD_TYPE_CLOUD) && abs(o.dz) == 1;
      if (!vertical_cloud) {
        int cheb = horiz;
        if (abs(o.dz) > cheb)
          cheb = abs(o.dz);
        if (cheb > r->neighbor_inclusion_radius)
          continue;
      }
      const bool is_cloud = (ew->generation_type == WORLD_TYPE_CLOUD);
      if (is_cloud || o.dz != 0)
        r->world_alpha[idx] = 1.0f;
      else
        r->world_alpha[idx] = 0.5f;
      int offx = o.dx * (int)ew->width;
      int offy = o.dy * (int)ew->height;
      int offz = o.dz * (int)ew->depth;
      isometric_renderer_render_world(r, ew, idx, offx, offy, offz);
      r->world_alpha[idx] = 1.0f;
    }
    emitted = r->render_buffer_size;
  });
  report("rendering", "CPU: voxel scan, home + neighbor ring", ms, (double)emitted, "voxel");

  MEASURE(ms, { isometric_renderer_sort_buffer(r); });
  report("rendering", "CPU: draw-order sort, neighbor ring", ms, (double)emitted, "voxel");

  MEASURE(ms, {
    isometric_renderer_render_gpu(r, sdl);
    emitted = r->render_buffer_size;
  });
  report("rendering", "whole frame incl. software raster, neighbor ring", ms,
         (double)emitted, "voxel");

  isometric_renderer_set_game_worlds(r, NULL);
  for (int i = 1; i < 125; i++) {
    if (r->edge_worlds[i])
      world_destroy(r->edge_worlds[i]);
    r->edge_worlds[i] = NULL;
  }
  isometric_renderer_destroy(r);
  SDL_DestroyRenderer(sdl);
  SDL_FreeSurface(surface);
  world_destroy(home);
}

// ---------------------------------------------------------------------------------------------
// Shadow world cluster — the shared search layer
//
// Everything here is priced against the unoptimised alternative, because the rejection ladder is
// only worth its complexity if it beats a triple loop by a wide margin. Where a search has a naive
// equivalent, both are measured, so a change that quietly removes a rejection shows up as the gap
// closing rather than as nothing at all.
// ---------------------------------------------------------------------------------------------

// 64 on a side rather than the 128 the game ships: a full cluster of 128-cubes is 2.6GB, which is
// not something a benchmark should allocate. The per-slot rebuild figure is reported alongside the
// full one so the 128-cube cost can be read off directly — it is eight times the volume per slot.
#define BENCH_CLUSTER_EDGE 64

// Terrain-shaped rather than uniform noise: a heightfield with caves under it, so the pyramid has
// both empty regions to skip and full ones it cannot. Uniform noise would leave every block
// occupied and flatter the ladder.
static World *bench_cluster_world(uint32_t salt, int layer)
{
  World *w = world_create(BENCH_CLUSTER_EDGE, BENCH_CLUSTER_EDGE, BENCH_CLUSTER_EDGE);
  if (!w)
    return NULL;

  // The top layer of the cluster is mostly sky, the bottom mostly rock: that vertical structure is
  // what the occupied-z band and the coarse pyramid levels exist to exploit.
  const int base = (layer == 1) ? 4 : (layer == 0 ? BENCH_CLUSTER_EDGE / 2 : BENCH_CLUSTER_EDGE);

  for (uint32_t y = 0; y < w->height; y++)
  {
    for (uint32_t x = 0; x < w->width; x++)
    {
      const uint32_t h = bench_mix(x >> 2, y >> 2, salt, 1u);
      int top = base + (int)(h % 9u) - 4;
      if (top > (int)w->depth)
        top = (int)w->depth;

      for (int z = 0; z < top; z++)
      {
        // Caves, so the volume is not one solid block below the surface.
        if ((bench_mix(x, y, (uint32_t)z, salt) & 15u) < 2u)
          continue;
        VoxelType t = VOXEL_STONE;
        if (z == top - 1)
          t = VOXEL_GRASS;
        else if (z > top - 4)
          t = VOXEL_SOIL;
        else if ((bench_mix(x >> 1, y >> 1, (uint32_t)z >> 1, salt ^ 99u) & 31u) == 0u)
          t = VOXEL_SAND;
        world_set_voxel(w, x, y, (uint32_t)z, t);
      }
    }
  }

  world_refresh_occupancy_bitfield(w);
  world_build_heightmap(w);
  return w;
}

// The unoptimised reference: no bitfield, no pyramid, no z band. What the ladder has to beat.
static long bench_brute_box(const ShadowWorld *sw, int x0, int y0, int z0, int x1, int y1, int z1,
                            VoxelType only, bool filter_type)
{
  long found = 0;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    World *w = shadow_world_slot_world(sw, slot);
    if (!w)
      continue;
    int ox = 0, oy = 0, oz = 0;
    shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);

    for (int lz = 0; lz < (int)w->depth; lz++)
      for (int ly = 0; ly < (int)w->height; ly++)
        for (int lx = 0; lx < (int)w->width; lx++)
        {
          const int cx = ox + lx, cy = oy + ly, cz = oz + lz;
          if (cx < x0 || cx > x1 || cy < y0 || cy > y1 || cz < z0 || cz > z1)
            continue;
          const Voxel *v = world_get_voxel(w, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz);
          if (!v || v->type == VOXEL_AIR)
            continue;
          if (filter_type && v->type != only)
            continue;
          found++;
        }
  }
  return found;
}

static void bench_shadow_world(void)
{
  if (!want("shadow")) return;
  section("Shadow world cluster (search and filter over 3x3x3 worlds)");

  ShadowWorld *sw = shadow_world_create(BENCH_CLUSTER_EDGE, BENCH_CLUSTER_EDGE,
                                        BENCH_CLUSTER_EDGE);
  if (!sw) return;
  shadow_world_set_centre(sw, 0, 0, 1);

  World *worlds[SHADOW_SLOT_COUNT];
  memset(worlds, 0, sizeof(worlds));
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    int dx = 0, dy = 0, dz = 0;
    shadow_slot_offsets(slot, &dx, &dy, &dz);
    worlds[slot] = bench_cluster_world((uint32_t)slot * 2654435761u + 7u, dz);
    if (!worlds[slot])
      goto done;
    shadow_world_attach(sw, dx, dy, dz, worlds[slot]);
  }

  const int extent = 3 * BENCH_CLUSTER_EDGE;
  const double cluster_cells = (double)extent * (double)extent * (double)extent;

  if (!g_csv) {
    const double voxel_mb = cluster_cells * (double)sizeof(Voxel) / (1024.0 * 1024.0);
    printf("  cluster %dx%dx%d over 27 worlds of %d^3, %.0f MB of voxels, "
           "%.1f KB of pyramid (%.4f%%)\n\n",
           extent, extent, extent, BENCH_CLUSTER_EDGE, voxel_mb,
           (double)shadow_world_pyramid_bytes(sw) / 1024.0,
           (double)shadow_world_pyramid_bytes(sw) * 100.0 /
               (cluster_cells * (double)sizeof(Voxel)));
  }

  double ms;
  long work;

  // --- Pyramid maintenance -------------------------------------------------------------------

  MEASURE(ms, {
    for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
      shadow_world_invalidate_slot(sw, slot);
    shadow_world_refresh(sw);
  });
  report("shadow", "pyramid rebuild, all 27 slots", ms, cluster_cells, "cell");

  const double slot_cells = (double)BENCH_CLUSTER_EDGE * BENCH_CLUSTER_EDGE * BENCH_CLUSTER_EDGE;
  MEASURE(ms, {
    shadow_world_invalidate_slot(sw, SHADOW_CENTRE_SLOT);
    shadow_world_refresh(sw);
  });
  report("shadow", "pyramid rebuild, one slot (x8 for a 128-cube)", ms, slot_cells, "cell");

  MEASURE(ms, { shadow_world_refresh(sw); });
  report("shadow", "pyramid refresh, nothing changed", ms, 0, NULL);

  // --- Point queries -------------------------------------------------------------------------

  MEASURE(ms, {
    long acc = 0;
    for (int i = 0; i < 100000; i++) {
      const uint32_t h = bench_mix((uint32_t)i, 1u, 2u, 3u);
      acc += shadow_world_sample(sw, (int)(h % (uint32_t)extent),
                                 (int)((h >> 8) % (uint32_t)extent),
                                 (int)((h >> 16) % (uint32_t)extent));
    }
    work = acc;
  });
  report("shadow", "100k random point samples", ms, 100000, "sample");
  (void)work;

  MEASURE(ms, {
    long acc = 0;
    for (int y = 0; y < extent; y++)
      for (int x = 0; x < extent; x++)
        acc += shadow_world_surface_height(sw, x, y);
    work = acc;
  });
  report("shadow", "surface height, every column in the cluster", ms,
         (double)extent * extent, "column");

  // --- Box search, ladder versus brute force -------------------------------------------------

  const int qx0 = extent / 4, qx1 = extent * 3 / 4;
  const int qy0 = extent / 4, qy1 = extent * 3 / 4;
  const int qz0 = extent / 4, qz1 = extent * 3 / 4;
  const double box_cells = (double)(qx1 - qx0 + 1) * (qy1 - qy0 + 1) * (qz1 - qz0 + 1);

  ShadowFilter f = shadow_filter_all();
  f.region = SHADOW_REGION_BOX;
  f.r.box.x0 = qx0; f.r.box.y0 = qy0; f.r.box.z0 = qz0;
  f.r.box.x1 = qx1; f.r.box.y1 = qy1; f.r.box.z1 = qz1;

  long brute_count = 0;
  MEASURE(ms, { brute_count = bench_brute_box(sw, qx0, qy0, qz0, qx1, qy1, qz1, VOXEL_AIR, false); });
  report("shadow", "box count, brute-force triple loop", ms, box_cells, "cell");

  f.solid_only = true;
  long ladder_count = 0;
  MEASURE(ms, { ladder_count = shadow_world_count(sw, &f); });
  report("shadow", "box count, ladder with solid_only", ms, box_cells, "cell");

  f.solid_only = false;
  MEASURE(ms, { ladder_count = shadow_world_count(sw, &f); });
  report("shadow", "box count, ladder reading each Voxel", ms, box_cells, "cell");

  if (!g_csv && ladder_count != brute_count) {
    printf("  !! ladder found %ld, brute force found %ld — the two disagree\n",
           ladder_count, brute_count);
  }

  const VoxelType sand[1] = {VOXEL_SAND};
  f.types = sand;
  f.type_count = 1;
  MEASURE(ms, { ladder_count = shadow_world_count(sw, &f); });
  report("shadow", "box count, ladder with a type filter", ms, box_cells, "cell");

  MEASURE(ms, { brute_count = bench_brute_box(sw, qx0, qy0, qz0, qx1, qy1, qz1, VOXEL_SAND, true); });
  report("shadow", "box count, brute force with a type filter", ms, box_cells, "cell");
  f.types = NULL;
  f.type_count = 0;

  // The by-world filter: the cheapest rejection in the ladder, one bit test per skipped world.
  f.slot_mask = SHADOW_SLOT_BIT(SHADOW_CENTRE_SLOT);
  f.region = SHADOW_REGION_ALL;
  MEASURE(ms, { ladder_count = shadow_world_count(sw, &f); });
  report("shadow", "whole-cluster search restricted to one world", ms, slot_cells, "cell");
  f.slot_mask = 0;

  // --- Sphere and nearest --------------------------------------------------------------------

  const float mid = (float)extent * 0.5f;
  f.region = SHADOW_REGION_SPHERE;
  f.r.sphere.cx = mid; f.r.sphere.cy = mid; f.r.sphere.cz = mid;
  f.r.sphere.radius = 24.0f;
  MEASURE(ms, { ladder_count = shadow_world_count(sw, &f); });
  report("shadow", "sphere r=24 at the eight-world corner", ms, 0, NULL);

  ShadowFilter near_filter = shadow_filter_all();
  near_filter.types = sand;
  near_filter.type_count = 1;
  ShadowHit hit;
  MEASURE(ms, {
    shadow_world_find_nearest(sw, (int)mid, (int)mid, (int)mid, 48, &near_filter, &hit);
  });
  report("shadow", "nearest matching voxel, expanding shells", ms, 0, NULL);

  MEASURE(ms, {
    // What nearest replaces: scan the whole box the shells could reach and keep the minimum.
    long best = -1;
    for (int z = (int)mid - 48; z <= (int)mid + 48; z++)
      for (int y = (int)mid - 48; y <= (int)mid + 48; y++)
        for (int x = (int)mid - 48; x <= (int)mid + 48; x++) {
          if (shadow_world_type_at(sw, x, y, z) != VOXEL_SAND) continue;
          const long dx = x - (int)mid;
          const long dy = y - (int)mid;
          const long dz = z - (int)mid;
          const long d2 = dx * dx + dy * dy + dz * dz;
          if (best < 0 || d2 < best) best = d2;
        }
    work = best;
  });
  report("shadow", "nearest by scanning the whole search box", ms, 0, NULL);

  // --- Raycast -------------------------------------------------------------------------------

  const int ray_count = 2000;
  MEASURE(ms, {
    long hits = 0;
    for (int i = 0; i < ray_count; i++) {
      const uint32_t h = bench_mix((uint32_t)i, 5u, 7u, 11u);
      ShadowRayResult res;
      if (shadow_world_raycast(sw, 1.5f, (float)(h % (uint32_t)extent) + 0.5f,
                               (float)((h >> 9) % (uint32_t)extent) + 0.5f,
                               1.0f, 0.0f, 0.0f, extent, 0u, &res))
        hits++;
    }
    work = hits;
  });
  report("shadow", "2k cluster raycasts across all three worlds", ms, ray_count, "ray");

  MEASURE(ms, {
    // The naive equivalent: restart a single-world DDA in each world along the ray's path.
    long hits = 0;
    for (int i = 0; i < ray_count; i++) {
      const uint32_t h = bench_mix((uint32_t)i, 5u, 7u, 11u);
      const int cy = (int)(h % (uint32_t)extent);
      const int cz = (int)((h >> 9) % (uint32_t)extent);
      for (int sx = 0; sx < SHADOW_CLUSTER_DIM; sx++) {
        const int slot = ((cz / BENCH_CLUSTER_EDGE) * SHADOW_CLUSTER_DIM +
                          (cy / BENCH_CLUSTER_EDGE)) * SHADOW_CLUSTER_DIM + sx;
        World *w = shadow_world_slot_world(sw, slot);
        if (!w) break;
        int hx = 0;
        int hy = 0;
        int hz = 0;
        if (world_raycast_first_hit(w, 0.5f, (float)(cy % BENCH_CLUSTER_EDGE) + 0.5f,
                                    (float)(cz % BENCH_CLUSTER_EDGE) + 0.5f,
                                    1.0f, 0.0f, 0.0f, BENCH_CLUSTER_EDGE, &hx, &hy, &hz)) {
          hits++;
          break;
        }
      }
    }
    work = hits;
  });
  report("shadow", "2k raycasts, naive per-world loop", ms, ray_count, "ray");

  MEASURE(ms, {
    long hits = 0;
    for (int i = 0; i < ray_count; i++) {
      const uint32_t h = bench_mix((uint32_t)i, 5u, 7u, 11u);
      ShadowRayResult res;
      if (shadow_world_raycast_occupancy(sw, 1.5f, (float)(h % (uint32_t)extent) + 0.5f,
                                         (float)((h >> 9) % (uint32_t)extent) + 0.5f,
                                         1.0f, 0.0f, 0.0f, extent, 0u, &res))
        hits++;
    }
    work = hits;
  });
  report("shadow", "2k occupancy-only lighting raycasts", ms, ray_count, "ray");

  // Proxy for the live lighting pass: 2 soft-sun + 2 AO + 1 specular-occlusion ray per unique voxel.
  MEASURE(ms, {
    for (int i = 0; i < 2000; i++) {
      const uint32_t h = bench_mix((uint32_t)i, 11, 13, 17);
      const float ox = (float)(h % (uint32_t)extent) + 0.5f;
      const float oy = (float)((h >> 8) % (uint32_t)extent) + 0.5f;
      const float oz = (float)((h >> 16) % (uint32_t)extent) + 0.5f;
      for (int r = 0; r < 5; r++) {
        const float ang = (float)r * 0.9f;
        float dx = 0.35f + 0.1f * cosf(ang);
        float dy = 0.15f + 0.1f * sinf(ang);
        float dz = (r < 2) ? 0.92f : (r == 4) ? 0.75f : 0.55f;
        const float len = sqrtf(dx * dx + dy * dy + dz * dz);
        ShadowRayResult res;
        (void)shadow_world_raycast_occupancy(sw, ox, oy, oz, dx / len, dy / len, dz / len,
                                             (r < 2) ? extent : 22, 0u, &res);
      }
    }
  });
  report("shadow", "2k voxels x (2 soft-sun + 2 AO + 1 spec) lighting", ms,
         2000.0 * 5.0, "ray");

  // --- Condition search, index versus traversal ----------------------------------------------

  const uint64_t cond_bit = condition_bit_from_name("WET");
  long tagged = 0;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++) {
    World *w = worlds[slot];
    if (!w) continue;
    for (uint32_t z = 0; z < w->depth; z += 8)
      for (uint32_t y = 0; y < w->height; y += 8)
        for (uint32_t x = 0; x < w->width; x += 8) {
          if (!world_is_solid_fast(w, (int)x, (int)y, (int)z)) continue;
          world_add_voxel_condition(w, x, y, z, "WET");
          tagged++;
        }
    world_refresh_condition_index(w);
  }

  ShadowFilter cf = shadow_filter_all();
  cf.condition_any = cond_bit;

  cf.index_policy = SHADOW_INDEX_FORCE_TRAVERSAL;
  long cond_traversal = 0;
  MEASURE(ms, { cond_traversal = shadow_world_count(sw, &cf); });
  report("shadow", "condition search by traversing the cluster", ms, cluster_cells, "cell");

  cf.index_policy = SHADOW_INDEX_FORCE_INDEX;
  long cond_indexed = 0;
  MEASURE(ms, { cond_indexed = shadow_world_count(sw, &cf); });
  report("shadow", "condition search via the inverted index", ms, (double)tagged, "match");

  if (!g_csv) {
    printf("  %ld voxels tagged; traversal found %ld, index found %ld%s\n",
           tagged, cond_traversal, cond_indexed,
           cond_traversal == cond_indexed ? "" : "  !! MISMATCH");
    cf.index_policy = SHADOW_INDEX_AUTO;
    shadow_world_reset_stats();
    shadow_world_count(sw, &cf);
    const ShadowSearchStats st = shadow_world_stats();
    printf("  with AUTO the whole-cluster search chose %s\n",
           st.index_searches ? "the index" : "traversal");
  }

  // --- Parallel fan-out ----------------------------------------------------------------------

  ShadowFilter pf = shadow_filter_all();
  pf.solid_only = true;
  MEASURE(ms, { shadow_world_count(sw, &pf); });
  report("shadow", "whole cluster, solid_only, serial", ms, cluster_cells, "cell");

  if (task_scheduler_init(0)) {
    MEASURE(ms, { shadow_world_find_parallel(sw, &pf, NULL, 0, NULL); });
    report("shadow", "whole cluster, solid_only, one task per world", ms, cluster_cells, "cell");
    task_scheduler_shutdown();
  }

done:
  shadow_world_destroy(sw);
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
    if (worlds[slot]) world_destroy(worlds[slot]);
}

// ---------------------------------------------------------------------------------------------
// Fog of war (map exploration)
// ---------------------------------------------------------------------------------------------

static void bench_fog(void)
{
  if (!want("fog") && !want("map"))
    return;
  section("Fog of war (map exploration per moving frame)");

  World *w = world_create(64, 64, 64);
  if (!w)
    return;
  for (uint32_t z = 0; z < 8; z++)
    for (uint32_t y = 0; y < 64; y++)
      for (uint32_t x = 0; x < 64; x++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);

  ShadowWorld *sw = shadow_world_create(64, 64, 64);
  if (!sw)
  {
    world_destroy(w);
    return;
  }
  shadow_world_attach(sw, 0, 0, 0, w);
  shadow_world_refresh(sw);

  FogAtlas *fog = fog_atlas_create();
  double ms;

  // The view-cone path used to run every gameplay frame; keep it measured so a regression to it
  // shows up as several ms rather than a vague hitch.
  fog_atlas_clear(fog);
  MEASURE(ms, {
    for (int i = 0; i < 5; i++)
      fog_reveal_from_view(fog, sw, 16.0f + (float)i * 0.5f, 32.0f, 10.0f, 0.1f * (float)i, 0.0f,
                           72.0f, 1.0f, 192.0f);
  });
  report("fog", "view-cone reveal x5 (legacy FP path)", ms / 5.0, 5.0, "reveal");

  fog_atlas_clear(fog);
  MEASURE(ms, {
    for (int i = 0; i < 20; i++)
      fog_reveal_around(fog, sw, 16.0f + (float)i * 0.5f, 32.0f, 10.0f, 24);
  });
  report("fog", "sphere r=24 reveal x20 (map path)", ms / 20.0, 20.0, "reveal");

  MEASURE(ms, {
    for (int i = 0; i < 100; i++)
      fog_reveal_around(fog, sw, 26.0f, 32.0f, 10.0f, 24);
  });
  report("fog", "sphere r=24 standing still x100", ms / 100.0, 100.0, "reveal");

  fog_atlas_destroy(fog);
  shadow_world_destroy(sw);
  world_destroy(w);
}

// ---------------------------------------------------------------------------------------------
// Fracture / disconnect
// ---------------------------------------------------------------------------------------------

static void bench_disconnect(void)
{
  if (!want("disconnect") && !want("combat")) return;
  section("Fracture and neighborhood disconnect");

  World *w = world_create(64, 64, 64);
  if (!w) return;

  // Solid floor plus a grove of wood columns with leaf caps — enough structure that a chop
  // floods a real component, not a toy.
  for (uint32_t z = 0; z < 4; z++)
    for (uint32_t y = 0; y < 64; y++)
      for (uint32_t x = 0; x < 64; x++)
        world_set_voxel(w, x, y, z, VOXEL_STONE);

  int trees = 0;
  for (int ty = 8; ty < 56; ty += 8)
    for (int tx = 8; tx < 56; tx += 8)
    {
      for (int z = 4; z < 12; z++)
        world_set_voxel(w, (uint32_t)tx, (uint32_t)ty, (uint32_t)z, VOXEL_WOOD);
      for (int dz = 0; dz < 3; dz++)
        for (int dy = -2; dy <= 2; dy++)
          for (int dx = -2; dx <= 2; dx++)
            world_set_voxel(w, (uint32_t)(tx + dx), (uint32_t)(ty + dy),
                            (uint32_t)(12 + dz), VOXEL_LEAVES);
      trees++;
    }

  double ms;
  long marked = 0;
  MEASURE(ms, {
    long acc = 0;
    for (int ty = 8; ty < 56; ty += 8)
      for (int tx = 8; tx < 56; tx += 8)
      {
        world_set_voxel(w, (uint32_t)tx, (uint32_t)ty, 4, VOXEL_AIR);
        acc += voxel_fracture_disconnect_at(w, tx, ty, 4);
        // Replant the trunk base so the next chop is independent.
        world_set_voxel(w, (uint32_t)tx, (uint32_t)ty, 4, VOXEL_WOOD);
        // Clear falling marks from the previous chop.
        const size_t total = (size_t)w->width * w->height * w->depth;
        for (size_t i = 0; i < total; i++)
          if (w->voxels[i].momentum[2] < -0.5f)
          {
            w->voxels[i].momentum[0] = 0;
            w->voxels[i].momentum[1] = 0;
            w->voxels[i].momentum[2] = 0;
          }
      }
    marked = acc;
  });
  report("combat", "chop every tree and disconnect", ms, (double)trees, "tree");
  (void)marked;

  // One real fall of a single canopy.
  world_set_voxel(w, 16, 16, 4, VOXEL_AIR);
  voxel_fracture_disconnect_at(w, 16, 16, 4);
  MEASURE(ms, {
    for (int i = 0; i < 20; i++)
      voxel_fracture_step_falling(w);
  });
  report("combat", "20 falling ticks for one canopy", ms, 20, "tick");

  MEASURE(ms, {
    int deleted = 0;
    for (int i = 0; i < 100; i++)
      deleted += voxel_fracture_apply_crack(w, 32, 32, 8, 1.0f, 0.0f, 0.0f, 14.0f,
                                           (uint32_t)i * 2654435761u);
    (void)deleted;
  });
  report("combat", "100 crack stamps at fireball speed", ms, 100, "stamp");

  // Pass-2 volume path: extract one canopy and settle it under continuous gravity.
  {
    DebrisVolumeSystem vols;
    voxel_debris_volume_reset(&vols);
    voxel_fracture_set_debris_volumes(&vols);
    for (int z = 4; z < 12; z++)
      world_set_voxel(w, 24, 24, (uint32_t)z, VOXEL_WOOD);
    for (int dz = 0; dz < 3; dz++)
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
          world_set_voxel(w, (uint32_t)(24 + dx), (uint32_t)(24 + dy),
                          (uint32_t)(12 + dz), VOXEL_LEAVES);
    world_set_voxel(w, 24, 24, 4, VOXEL_AIR);
    long settle_steps = 0;
    MEASURE(ms, {
      voxel_fracture_disconnect_at(w, 24, 24, 4);
      int guard = 0;
      while (voxel_debris_volume_active_count(&vols) > 0 && guard++ < 180)
        voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
      settle_steps = guard;
    });
    report("combat", "extract+settle one canopy as a debris volume", ms, 1.0, "canopy");
    (void)settle_steps;
    voxel_fracture_set_debris_volumes(NULL);
  }

  // Oversized island: batched extracts across multiple volume slots.
  {
    DebrisVolumeSystem vols;
    voxel_debris_volume_reset(&vols);
    voxel_fracture_set_debris_volumes(&vols);
    const int cx = 40, cy = 40, z0 = 6;
    int cells = 0;
    for (int dz = 0; dz < 10; dz++)
      for (int dy = 0; dy < 10; dy++)
        for (int dx = 0; dx < 10; dx++)
        {
          world_set_voxel(w, (uint32_t)(cx + dx - 5), (uint32_t)(cy + dy - 5),
                          (uint32_t)(z0 + dz), VOXEL_STONE);
          cells++;
        }
    long extracted = 0;
    long volumes = 0;
    MEASURE(ms, {
      extracted = voxel_fracture_disconnect_at(w, cx, cy, z0 - 1);
      volumes = voxel_debris_volume_active_count(&vols);
      int guard = 0;
      while (voxel_debris_volume_active_count(&vols) > 0 && guard++ < 400)
        voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
    });
    report("combat", "batch-extract+settle ~1k-cell island", ms, (double)cells, "cell");
    (void)extracted;
    (void)volumes;
    voxel_fracture_set_debris_volumes(NULL);
  }

  world_destroy(w);
}

// ---------------------------------------------------------------------------------------------
// Polygon mesh animation (sample + software draw cost)
//
// Meshed actors (goleling, pigeon, sheep, …) sample a baked vertex-frame clip every frame, then
// the FP / iso paths transform and rasterise every triangle in software. That is the animation
// budget: clip lerp is usually cheap; triangle setup+fill is not once a few actors share the view.
// ---------------------------------------------------------------------------------------------

static void anim_fill_tri(float *depth, uint32_t *color, int w, int h,
                          float x0, float y0, float z0,
                          float x1, float y1, float z1,
                          float x2, float y2, float z2, uint32_t rgba)
{
  const float minx = fminf(x0, fminf(x1, x2));
  const float maxx = fmaxf(x0, fmaxf(x1, x2));
  const float miny = fminf(y0, fminf(y1, y2));
  const float maxy = fmaxf(y0, fmaxf(y1, y2));
  int x_lo = (int)floorf(minx), x_hi = (int)ceilf(maxx);
  int y_lo = (int)floorf(miny), y_hi = (int)ceilf(maxy);
  if (x_lo < 0) x_lo = 0;
  if (y_lo < 0) y_lo = 0;
  if (x_hi >= w) x_hi = w - 1;
  if (y_hi >= h) y_hi = h - 1;
  const float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
  if (fabsf(area) < 1e-4f)
    return;
  const float inv_a = 1.0f / area;
  for (int y = y_lo; y <= y_hi; y++)
  {
    for (int x = x_lo; x <= x_hi; x++)
    {
      const float px = (float)x + 0.5f, py = (float)y + 0.5f;
      const float w0 = ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) * inv_a;
      const float w1 = ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) * inv_a;
      const float w2 = 1.0f - w0 - w1;
      if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
        continue;
      const float z = w0 * z0 + w1 * z1 + w2 * z2;
      const int i = y * w + x;
      if (z < depth[i])
      {
        depth[i] = z;
        color[i] = rgba;
      }
    }
  }
}

// Mirrors fp_draw_actor_poly's transform + near-clip-free projection + fill, without fog/lighting.
static int anim_draw_mesh_sw(const PolyMesh *mesh, const float *xyz, float yaw, float roll,
                             float ox, float oy, float oz, int lod,
                             float *depth, uint32_t *color, int tw, int th)
{
  if (!mesh || !xyz || !depth || !color || lod <= 0)
    return 0;
  const float c = cosf(yaw);
  const float s = sinf(yaw);
  const float cr = cosf(roll);
  const float sr = sinf(roll);
  const float fov = 60.0f * (float)M_PI / 180.0f;
  const float half_tan = tanf(fov * 0.5f);
  const float aspect = (th > 0) ? ((float)tw / (float)th) : 1.0f;
  const uint32_t tri_step = 3u * (uint32_t)lod;
  int filled = 0;
  for (uint32_t t = 0; t + 2 < mesh->index_count; t += tri_step)
  {
    const uint32_t i0 = mesh->indices[t];
    const uint32_t i1 = mesh->indices[t + 1];
    const uint32_t i2 = mesh->indices[t + 2];
    if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
      continue;
    float sx[3], sy[3], sz[3];
    const uint32_t idx[3] = {i0, i1, i2};
    bool behind = false;
    for (int k = 0; k < 3; k++)
    {
      const float lx = xyz[idx[k] * 3];
      const float ly0 = xyz[idx[k] * 3 + 1];
      const float lz0 = xyz[idx[k] * 3 + 2];
      const float ly = ly0 * cr + lz0 * sr;
      const float lz = -ly0 * sr + lz0 * cr;
      const float wx = ox + lx * c - ly * s;
      const float wy = oy + lx * s + ly * c;
      const float wz = oz + lz;
      // Camera at origin looking +X for the bench (actor placed on +X).
      const float cam_z = wx; // depth
      const float cam_x = -wy;
      const float cam_y = wz;
      if (cam_z < 0.2f)
      {
        behind = true;
        break;
      }
      sx[k] = (cam_x / (cam_z * half_tan * aspect) * 0.5f + 0.5f) * (float)tw;
      sy[k] = (0.5f - cam_y / (cam_z * half_tan) * 0.5f) * (float)th;
      sz[k] = cam_z;
    }
    if (behind)
      continue;
    const uint8_t *col = &mesh->colors[i0 * 3];
    const uint32_t rgba = ((uint32_t)col[0] << 24) | ((uint32_t)col[1] << 16) |
                          ((uint32_t)col[2] << 8) | 255u;
    anim_fill_tri(depth, color, tw, th, sx[0], sy[0], sz[0], sx[1], sy[1], sz[1], sx[2], sy[2],
                  sz[2], rgba);
    filled++;
  }
  return filled;
}

static void bench_animation(void)
{
  if (!want("animation") && !want("anim") && !want("load"))
    return;
  section("Polygon mesh animation");

  double ms;
  // Init only registers the directory — cold load cost moved to the first poly_mesh_get.
  poly_mesh_shutdown();
  {
    const double t0 = now_seconds();
    poly_mesh_init("models/poly");
    ms = (now_seconds() - t0) * 1000.0;
  }
  report("animation", "poly_mesh_init (dir only, no I/O)", ms, 0.0, NULL);

  {
    const double t0 = now_seconds();
    const PolyMesh *golem = poly_mesh_get("goleling");
    ms = (now_seconds() - t0) * 1000.0;
    if (!golem)
    {
      if (!g_csv)
        printf("  (no models/poly/*.vmesh — skip animation benches)\n");
      return;
    }
  }
  report("animation", "poly_mesh_get goleling (cold .vmesh)", ms, 0.0, NULL);

  MEASURE(ms, { (void)poly_mesh_get("goleling"); });
  report("animation", "poly_mesh_get goleling (warm)", ms, 0.0, NULL);

  MEASURE(ms, { poly_mesh_init("models/poly"); });
  report("animation", "poly_mesh_init warm (idempotent)", ms, 0.0, NULL);

  // Worst-case: pull every baked mesh (what the old eager init did on every world enter).
  poly_mesh_shutdown();
  poly_mesh_init("models/poly");
  {
    static const char *all[] = {"goleling", "pigeon", "sheep", "chick", "bat", NULL};
    const double t0 = now_seconds();
    for (int i = 0; all[i]; i++)
      (void)poly_mesh_get(all[i]);
    ms = (now_seconds() - t0) * 1000.0;
  }
  report("animation", "poly_mesh_get all 5 .vmesh (eager worst case)", ms, 0.0, NULL);

  // Voxel mob models — same lazy pattern.
  mob_models_shutdown();
  {
    const double t0 = now_seconds();
    mob_models_init("models");
    ms = (now_seconds() - t0) * 1000.0;
  }
  report("animation", "mob_models_init (dir only, no I/O)", ms, 0.0, NULL);
  {
    const double t0 = now_seconds();
    (void)mob_models_get(MOB_MODEL_BIRD);
    ms = (now_seconds() - t0) * 1000.0;
  }
  report("animation", "mob_models_get bird (cold .world)", ms, 0.0, NULL);
  MEASURE(ms, { (void)mob_models_get(MOB_MODEL_BIRD); });
  report("animation", "mob_models_get bird (warm)", ms, 0.0, NULL);
  {
    const double t0 = now_seconds();
    for (int k = 0; k < MOB_MODEL_COUNT; k++)
      (void)mob_models_get((MobModelKind)k);
    ms = (now_seconds() - t0) * 1000.0;
  }
  report("animation", "mob_models_get all slots (eager worst case)", ms, 0.0, NULL);
  mob_models_shutdown();

  static const char *names[] = {"goleling", "pigeon", "sheep", "chick", "bat", NULL};
  float *scratch = (float *)malloc(8192 * 3 * sizeof(float));
  if (!scratch)
    return;

  for (int n = 0; names[n]; n++)
  {
    const PolyMesh *mesh = poly_mesh_get(names[n]);
    if (!mesh || !mesh->loaded)
      continue;
    const char *clip = mesh->clips[0].name;
    for (int c = 0; c < mesh->clip_count; c++)
      if (mesh->clips[c].looping)
      {
        clip = mesh->clips[c].name;
        break;
      }

    char label[96];
    snprintf(label, sizeof(label), "sample %s/%s (1k frames)", names[n], clip);
    const int iters = 1000;
    MEASURE(ms, {
      for (int i = 0; i < iters; i++)
        poly_mesh_sample(mesh, clip, (float)i * 0.016f, scratch);
    });
    report("animation", label, ms, (double)iters, "sample");
    if (!g_csv)
      printf("       %u verts, %u tris, %d clips\n", mesh->vertex_count,
             mesh->index_count / 3u, mesh->clip_count);
  }

  // Transform-only: what FP does before raster (yaw/roll + world pose per vertex).
  {
    const PolyMesh *mesh = poly_mesh_get("goleling");
    poly_mesh_sample(mesh, "Flying_Idle", 0.3f, scratch);
    const int iters = 500;
    volatile float sink = 0.0f;
    MEASURE(ms, {
      for (int i = 0; i < iters; i++)
      {
        const float yaw = (float)i * 0.01f;
        const float c = cosf(yaw);
        const float s = sinf(yaw);
        for (uint32_t v = 0; v < mesh->vertex_count; v++)
        {
          const float lx = scratch[v * 3];
          const float ly = scratch[v * 3 + 1];
          const float lz = scratch[v * 3 + 2];
          sink += lx * c - ly * s + lz;
        }
      }
    });
    report("animation", "goleling: yaw-transform all verts x500", ms, (double)iters, "pose");
    (void)sink;
  }

  // Software fill matching the live FP poly path size (256x240 panel).
  {
    const int tw = 256, th = 240;
    float *depth = (float *)malloc((size_t)tw * th * sizeof(float));
    uint32_t *color = (uint32_t *)malloc((size_t)tw * th * sizeof(uint32_t));
    if (depth && color)
    {
      const PolyMesh *mesh = poly_mesh_get("goleling");
      poly_mesh_sample(mesh, "Flying_Idle", 0.25f, scratch);
      int tris = 0;
      MEASURE(ms, {
        for (int i = 0; i < (int)tw * th; i++)
        {
          depth[i] = 1e30f;
          color[i] = 0;
        }
        tris = anim_draw_mesh_sw(mesh, scratch, 0.4f, 0.1f, 4.0f, 0.0f, 1.2f, 1, depth, color, tw,
                                 th);
      });
      report("animation", "goleling: sample+SW raster one actor 256x240", ms, (double)tris, "tri");

      // Sample-only for eight actors (draw skipped) — proves fill, not lerp, is the cost.
      MEASURE(ms, {
        for (int a = 0; a < 8; a++)
          poly_mesh_sample(mesh, "Flying_Idle", 0.1f * (float)a, scratch);
      });
      report("animation", "8 goleling sample only (no raster)", ms, 8.0, "sample");

      // Flock at full LOD (worst case — all near camera).
      MEASURE(ms, {
        for (int i = 0; i < (int)tw * th; i++)
        {
          depth[i] = 1e30f;
          color[i] = 0;
        }
        for (int a = 0; a < 8; a++)
        {
          poly_mesh_sample(mesh, "Flying_Idle", 0.1f * (float)a, scratch);
          anim_draw_mesh_sw(mesh, scratch, 0.2f * (float)a, 0.0f, 3.0f + 0.4f * (float)a,
                            (float)(a - 4) * 0.6f, 1.0f, 1, depth, color, tw, th);
        }
      });
      report("animation", "8 goleling full LOD (all near)", ms, 8.0, "actor");

      // Mixed distances using poly_mesh_lod_stride — matches live FP/iso policy.
      {
        static const float dists[8] = {8.0f, 12.0f, 24.0f, 30.0f, 35.0f, 50.0f, 60.0f, 110.0f};
        MEASURE(ms, {
          for (int i = 0; i < (int)tw * th; i++)
          {
            depth[i] = 1e30f;
            color[i] = 0;
          }
          for (int a = 0; a < 8; a++)
          {
            const int lod = poly_mesh_lod_stride(dists[a]);
            if (lod <= 0)
              continue;
            poly_mesh_sample_ex(mesh, "Flying_Idle", 0.1f * (float)a, scratch, lod > 1);
            anim_draw_mesh_sw(mesh, scratch, 0.2f * (float)a, 0.0f, dists[a],
                              (float)(a - 4) * 0.6f, 1.0f, lod, depth, color, tw, th);
          }
        });
        report("animation", "8 goleling mixed LOD (live policy)", ms, 8.0, "actor");
      }
      // Setup-only: transform + project + backface, no pixel fill.
      {
        volatile int sink_tris = 0;
        MEASURE(ms, {
          const float c = cosf(0.4f);
          const float s = sinf(0.4f);
          const float cr = cosf(0.1f);
          const float sr = sinf(0.1f);
          int kept = 0;
          for (uint32_t t = 0; t + 2 < mesh->index_count; t += 3)
          {
            const uint32_t i0 = mesh->indices[t];
            const uint32_t i1 = mesh->indices[t + 1];
            const uint32_t i2 = mesh->indices[t + 2];
            float w0x = scratch[i0 * 3] * c - scratch[i0 * 3 + 1] * s;
            float w1x = scratch[i1 * 3] * c - scratch[i1 * 3 + 1] * s;
            float w2x = scratch[i2 * 3] * c - scratch[i2 * 3 + 1] * s;
            (void)cr;
            (void)sr;
            if (w0x + w1x + w2x > 0.0f)
              kept++;
          }
          sink_tris = kept;
        });
        report("animation", "goleling: triangle setup only (no fill)", ms, (double)sink_tris, "tri");
      }

      const PolyMesh *sheep = poly_mesh_get("sheep");
      if (sheep)
      {
        poly_mesh_sample(sheep, "Walk", 0.2f, scratch);
        MEASURE(ms, {
          for (int i = 0; i < (int)tw * th; i++)
          {
            depth[i] = 1e30f;
            color[i] = 0;
          }
          anim_draw_mesh_sw(sheep, scratch, 0.0f, 0.0f, 4.0f, 0.0f, 0.5f, 1, depth, color, tw, th);
        });
        report("animation", "sheep: sample+SW raster one actor 256x240", ms, 0.0, NULL);
      }
    }
    free(depth);
    free(color);
  }

  // AI tick that advances clips (no draw).
  {
    MobActor *mobs[16];
    int n = 0;
    mobs[n++] = mob_actor_create("Goleling", MOB_TYPE_WANDERER, 10, 10, 5);
    if (mobs[n - 1])
      mob_actor_bind_mesh(mobs[n - 1], "goleling");
    mobs[n++] = mob_actor_create_bird(BIRD_KIND_CROW, 12, 10, 8);
    if (mobs[n - 1])
      mob_actor_bind_mesh(mobs[n - 1], "pigeon");
    mobs[n++] = mob_actor_create_sheep(14, 10, 5);
    mobs[n++] = mob_actor_create_chicken(16, 10, 5);
    mobs[n++] = mob_actor_create_bat(18, 10, 9);
    int live = 0;
    for (int i = 0; i < n; i++)
      if (mobs[i])
      {
        mobs[i]->base.velocity_x = 1.5;
        mobs[i]->base.is_flying = (i == 1 || i == 4);
        live++;
      }
    const int ticks = 600;
    MEASURE(ms, {
      for (int t = 0; t < ticks; t++)
        for (int i = 0; i < n; i++)
          if (mobs[i])
            mob_actor_tick_animation(mobs[i], 1.0f / 60.0f);
    });
    report("animation", "tick_animation 5 meshed mobs x600", ms, (double)ticks * live, "tick");
    for (int i = 0; i < n; i++)
      if (mobs[i])
        mob_actor_destroy(mobs[i]);
  }

  free(scratch);
  poly_mesh_shutdown();
}

// ---------------------------------------------------------------------------------------------

static void usage(const char *argv0)
{
  printf("usage: %s [--target-fps N] [--reps N] [--csv] [--fast] [--only SECTION]\n", argv0);
  printf("               [--dump-frame PATH.bmp]\n");
  printf("  --fast         skip the slow generators (wilderness takes seconds per world)\n");
  printf("  --dump-frame   save the rendered frame, to check the render looks right\n");
  printf("  SECTION is one of: voxels, generation, physics, rendering, shadow, combat,\n");
  printf("                     disconnect, animation, load\n");
  printf("  Full storyline playthrough (startup→quests→world tour) is a separate binary:\n");
  printf("    make run-storyline-playthrough-bench\n");
  printf("    ./test-storyline-playthrough --csv\n");
  printf("  Material-worlds FP framerate (nested templates / sparse silhouettes):\n");
  printf("    make run-material-fp-benchmark\n");
  printf("    ./material-fp-benchmark --only frame --reps 5\n");
}

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--target-fps") && i + 1 < argc) {
      g_target_fps = atof(argv[++i]);
      if (g_target_fps <= 0.0) g_target_fps = 120.0;
    } else if (!strcmp(argv[i], "--reps") && i + 1 < argc) {
      g_reps = atoi(argv[++i]);
      if (g_reps < 1) g_reps = 1;
    } else if (!strcmp(argv[i], "--csv")) {
      g_csv = true;
    } else if (!strcmp(argv[i], "--fast")) {
      g_fast = true;
    } else if (!strcmp(argv[i], "--only") && i + 1 < argc) {
      g_only = argv[++i];
    } else if (!strcmp(argv[i], "--dump-frame") && i + 1 < argc) {
      g_dump_frame = argv[++i];
    } else {
      usage(argv[0]);
      return (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) ? 0 : 1;
    }
  }

  if (g_csv) {
    printf("section,measurement,ms,work,work_unit\n");
  } else {
    printf("VERSE Performance Benchmark\n");
    printf("===========================\n");
    printf("target %.0f FPS = %.3f ms per frame, best of %d run(s)\n",
           g_target_fps, frame_budget_ms(), g_reps);
  }

  bench_voxel_access();
  bench_generation();
  bench_physics();
  bench_rendering();
  bench_rendering_neighbors();
  bench_shadow_world();
  bench_fog();
  bench_disconnect();
  bench_animation();

  if (!g_csv) printf("\n");
  return 0;
}
