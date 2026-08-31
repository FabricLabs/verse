// world_generation_profile - attribute the cost of world generation by phase.
//
// Build:  make world-generation-profile     (compiles the world core with -DWORLD_GEN_PROFILE)
// Usage:  ./world-generation-profile [--size N] [--type wilderness|home|farm] [--new-game]
//
// --new-game runs the same 54-world sequence the client performs when a player starts a
// new game, which is the case that actually matters and the one that takes minutes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "world.h"
#include "constants.h"
#include "world_gen_profile.h"

static double now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static WorldGenerationType parse_type(const char *name)
{
  if (!name) return WORLD_TYPE_WILDERNESS;
  if (strcmp(name, "home") == 0) return WORLD_TYPE_HOME;
  if (strcmp(name, "farm") == 0) return WORLD_TYPE_FARM;
  if (strcmp(name, "scoured") == 0) return WORLD_TYPE_SCOURED;
  return WORLD_TYPE_WILDERNESS;
}

static const char *type_name(WorldGenerationType t)
{
  switch (t)
  {
  case WORLD_TYPE_HOME: return "home";
  case WORLD_TYPE_FARM: return "farm";
  case WORLD_TYPE_SCOURED: return "scoured";
  case WORLD_TYPE_WILDERNESS: return "wilderness";
  default: return "other";
  }
}

// One world, full phase breakdown.
static double profile_one(uint32_t size, WorldGenerationType type, const char *seed, bool report)
{
  world_gen_profile_reset();

  double t0 = now_ms();
  World *world = world_create(size, size, size);
  double t1 = now_ms();
  if (!world)
  {
    fprintf(stderr, "world_create(%u^3) failed\n", size);
    return 0.0;
  }
  world_gen_profile_record(WGEN_PHASE_CREATE, (uint64_t)((t1 - t0) * 1000000.0));

  world_generate_with_type(world, seed, type);
  double t2 = now_ms();

  if (report)
  {
    char label[128];
    snprintf(label, sizeof(label), "%s %ux%ux%u", type_name(type), size, size, size);
    world_gen_profile_report(label, (size_t)size * size * size);
    printf("wall clock for this world: %.1f ms (create %.1f + generate %.1f)\n",
           t2 - t0, t1 - t0, t2 - t1);
  }

  world_destroy(world);
  return t2 - t0;
}

// The client's new-game sequence: 1 wilderness + 1 home + 1 farm + 25 adjacent farm +
// 26 adjacent wilderness = 54 worlds at WORLD_SIZE_CUBE. See game_state.c:
// game_state_update_world_generation().
static void profile_new_game(uint32_t size)
{
  struct { WorldGenerationType type; int count; const char *what; } steps[] = {
      {WORLD_TYPE_WILDERNESS, 1, "player start wilderness"},
      {WORLD_TYPE_HOME, 1, "home"},
      {WORLD_TYPE_FARM, 1, "farm"},
      {WORLD_TYPE_FARM, 25, "adjacent farms"},
      {WORLD_TYPE_WILDERNESS, 26, "adjacent wilderness"},
  };

  printf("\n=== simulating a new game: 54 worlds at %u^3 ===\n", size);
  printf("%-26s %6s %12s %12s\n", "step", "worlds", "total ms", "ms/world");
  printf("%-26s %6s %12s %12s\n", "--------------------------", "------", "------------",
         "------------");

  double grand_total = 0.0;
  int world_index = 0;

  for (size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++)
  {
    double step_total = 0.0;
    for (int i = 0; i < steps[s].count; i++)
    {
      char seed[64];
      snprintf(seed, sizeof(seed), "new_game_probe_%d", world_index++);
      step_total += profile_one(size, steps[s].type, seed, false);
    }
    grand_total += step_total;
    printf("%-26s %6d %12.1f %12.1f\n", steps[s].what, steps[s].count, step_total,
           step_total / steps[s].count);
  }

  printf("%-26s %6d %12.1f %12.1f\n", "TOTAL", 54, grand_total, grand_total / 54.0);
  printf("\nnew game world generation: %.1f seconds (%.1f minutes)\n", grand_total / 1000.0,
         grand_total / 60000.0);
  printf("plus the loader's 100 ms minimum per step: +%.1f s\n", 54 * 0.1);
  printf("=========================================================\n");
}

int main(int argc, char **argv)
{
  uint32_t size = WORLD_SIZE_X;
  WorldGenerationType type = WORLD_TYPE_WILDERNESS;
  bool new_game = false;
  bool all_types = false;

  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
      size = (uint32_t)atoi(argv[++i]);
    else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc)
      type = parse_type(argv[++i]);
    else if (strcmp(argv[i], "--new-game") == 0)
      new_game = true;
    else if (strcmp(argv[i], "--all-types") == 0)
      all_types = true;
    else
    {
      printf("usage: %s [--size N] [--type wilderness|home|farm|scoured] [--all-types] [--new-game]\n",
             argv[0]);
      return 1;
    }
  }

  if (size == 0 || size > 512)
  {
    fprintf(stderr, "size must be 1..512\n");
    return 1;
  }

  printf("world generation profile\n");
  printf("client new-game world size is %d^3 (%s)\n", WORLD_SIZE_X, "constants.h WORLD_SIZE_CUBE");

  if (new_game)
  {
    profile_new_game(size);
    return 0;
  }

  if (all_types)
  {
    WorldGenerationType types[] = {WORLD_TYPE_WILDERNESS, WORLD_TYPE_HOME, WORLD_TYPE_FARM};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
      profile_one(size, types[i], "profile_seed", true);
    return 0;
  }

  profile_one(size, type, "profile_seed", true);
  return 0;
}
