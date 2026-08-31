// What a new game actually costs, measured through the real job path rather than a reimplementation
// of it.
//
// Two phases matter, and they are very different sizes:
//
//   startup  - what the loading screen waits for. With VERSE_HOME_ONLY_STARTUP this is the home
//              island, cloud layers, and a 5x5 wilderness plane (two rings) under the island.
//   ring     - what is generated in the background immediately afterwards, which the player never
//              waits for but which competes for cores and memory for as long as it runs.
//
// Usage: ./world-gen-startup-profile [--workers N] [--phase startup|ring|both]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "constants.h"
#include "task_scheduler.h"
#include "universe.h"
#include "world.h"
#include "world_gen_job.h"

// Mirrors the streamer's region and in-flight cap. Kept here rather than included from game_state.h,
// which pulls in SDL and the whole client for two integers.
#define SHADOW_STREAM_XY 2
#define SHADOW_STREAM_Z 2
#define WORLD_STREAM_MAX_IN_FLIGHT 6

static double now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

static int count_cells(const Universe *u)
{
  int n = 0;
  for (size_t i = 0; i < u->capacity; i++)
    if (u->entries[i].used)
      n++;
  return n;
}

// Worlds per layer, so it is obvious which layer the time went into.
static void report_layers(const Universe *u)
{
  for (int z = -3; z <= 4; z++)
  {
    int n = 0;
    for (size_t i = 0; i < u->capacity; i++)
      if (u->entries[i].used && (int64_t)u->entries[i].z == (int64_t)z)
        n++;
    if (n)
      printf("      universe z=%-2d  %2d world(s)%s\n", z, n,
             z == UNIVERSE_HOME_Z ? "   <- the player is here" : "");
  }
}

int main(int argc, char **argv)
{
  int workers = 0;
  const char *phase = "both";

  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc)
      workers = atoi(argv[++i]);
    else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc)
      phase = argv[++i];
    else
    {
      printf("usage: %s [--workers N] [--phase startup|ring|both]\n", argv[0]);
      return 1;
    }
  }

  if (!task_scheduler_init(workers))
    printf("no worker pool; everything runs inline\n");

  printf("world size %d^3, %d worker(s), home-only startup %s\n\n", WORLD_SIZE_X,
         task_scheduler_worker_count(), VERSE_HOME_ONLY_STARTUP ? "on" : "off");

  Universe universe;
  universe_init(&universe, UNIVERSE_SEED_HEX, WORLD_AFFINITY, 10);
  GameWorlds *worlds = game_worlds_create("startup-profile");
  if (!worlds)
    return 1;

  const bool do_startup = strcmp(phase, "ring") != 0;
  const bool do_ring = strcmp(phase, "startup") != 0;

  double startup_ms = 0.0;
  int after_startup = 0;

  if (do_startup)
  {
    WorldGenRequest request = {
        .worlds = worlds, .universe = &universe, .home_only = (VERSE_HOME_ONLY_STARTUP != 0)};

    const double t0 = now_ms();
    WorldGenJob *job = world_gen_job_start(&request);
    if (!job)
      return 1;
    while (!world_gen_job_is_complete(job))
      ;
    startup_ms = now_ms() - t0;
    world_gen_job_destroy(job);
    after_startup = count_cells(&universe);

    printf("[startup] what the loading screen waits for\n");
    printf("      %.0f ms, %d world(s) resident\n", startup_ms, after_startup);
    report_layers(&universe);
    printf("\n");
  }

  if (do_ring)
  {
    if (!worlds->home_world)
    {
      printf("[neighbourhood] skipped: needs a home world from the startup phase\n");
      return 1;
    }

    // What game_state_pump_world_streaming does, driven the same way it drives it: the 3x3x3 around
    // the player, through cell_gen_job, no more than WORLD_STREAM_MAX_IN_FLIGHT at a time. The
    // eager radius-2 ring this replaced is gone, so what is measured here is the whole of the
    // background generation a new game now performs.
    CellGenJob *in_flight[WORLD_STREAM_MAX_IN_FLIGHT] = {0};
    int added = 0;
    const double t0 = now_ms();

    for (int dz = -SHADOW_STREAM_Z; dz <= SHADOW_STREAM_Z; dz++)
    {
      const int64_t az = (int64_t)UNIVERSE_HOME_Z + dz;
      if (az < 0)
        continue;
      for (int dy = -SHADOW_STREAM_XY; dy <= SHADOW_STREAM_XY; dy++)
      {
        for (int dx = -SHADOW_STREAM_XY; dx <= SHADOW_STREAM_XY; dx++)
        {
          if (universe_has(&universe, (uint64_t)dx, (uint64_t)dy, (uint64_t)az))
            continue;

          int slot = -1;
          while (slot < 0)
          {
            for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
            {
              if (!in_flight[i])
              {
                slot = i;
                break;
              }
              if (cell_gen_job_is_complete(in_flight[i]))
              {
                uint64_t jx, jy, jz;
                cell_gen_job_cell(in_flight[i], &jx, &jy, &jz);
                World *made = cell_gen_job_take_world(in_flight[i]);
                cell_gen_job_destroy(in_flight[i]);
                in_flight[i] = NULL;
                if (made && !universe_place(&universe, jx, jy, jz, made))
                  world_destroy(made);
                else if (made)
                  added++;
                slot = i;
                break;
              }
            }
          }
          in_flight[slot] = cell_gen_job_start(worlds->base_seed, (uint64_t)dx, (uint64_t)dy,
                                               (uint64_t)az);
        }
      }
    }

    for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
    {
      if (!in_flight[i])
        continue;
      while (!cell_gen_job_is_complete(in_flight[i]))
        ;
      uint64_t jx, jy, jz;
      cell_gen_job_cell(in_flight[i], &jx, &jy, &jz);
      World *made = cell_gen_job_take_world(in_flight[i]);
      cell_gen_job_destroy(in_flight[i]);
      in_flight[i] = NULL;
      if (made && !universe_place(&universe, jx, jy, jz, made))
        world_destroy(made);
      else if (made)
        added++;
    }

    const double ring_ms = now_ms() - t0;
    const int after_ring = count_cells(&universe);
    printf("[neighbourhood] what streams in around the player afterwards\n");
    printf("      %.0f ms, %d world(s) added (%d resident), %d at a time\n", ring_ms, added,
           after_ring, WORLD_STREAM_MAX_IN_FLIGHT);
    report_layers(&universe);
    printf("\n");

    if (startup_ms > 0.0)
      printf("background work is %.1fx the work the player waits for\n", ring_ms / startup_ms);
  }

  universe_free(&universe);
  game_worlds_destroy(worlds);
  task_scheduler_shutdown();
  return 0;
}
