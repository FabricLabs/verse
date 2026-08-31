// Tests for the worker pool and the jobs built on it.
//
// The claim worth testing is that stepping many worlds at once keeps them isolated from each
// other. Note that it cannot be tested by comparing snapshots against a serial run: the fluid
// simulation has a wall-clock budget, so it is nondeterministic with or without threads. See
// the comment above the physics test.

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "constants.h"
#include "task_scheduler.h"
#include "world.h"
#include "world_gen_job.h"
#include "world_physics_jobs.h"

static int test_failures = 0;

static void report(const char *name, bool passed)
{
  printf("%s %s\n", passed ? "PASS" : "FAILED", name);
  if (!passed)
    test_failures++;
}

// ---------------------------------------------------------------------------
// Pool mechanics
// ---------------------------------------------------------------------------

#define COUNTER_TASKS 64

typedef struct
{
  pthread_mutex_t lock;
  int runs;
  pthread_t thread_ids[COUNTER_TASKS];
} CounterState;

static void counter_task(void *user_data)
{
  CounterState *counter = (CounterState *)user_data;
  pthread_mutex_lock(&counter->lock);
  if (counter->runs < COUNTER_TASKS)
    counter->thread_ids[counter->runs] = pthread_self();
  counter->runs++;
  pthread_mutex_unlock(&counter->lock);
}

// Without a pool, submit must still run the work before returning.
static void test_inline_fallback(void)
{
  CounterState counter;
  memset(&counter, 0, sizeof(counter));
  pthread_mutex_init(&counter.lock, NULL);

  TaskGroup *group = task_group_create("inline");
  bool submitted = task_group_submit(group, counter_task, &counter);

  report("inline submit accepted without a pool", submitted);
  report("inline task ran before submit returned", counter.runs == 1);
  report("inline group reports complete", task_group_is_complete(group));

  task_group_destroy(group);
  pthread_mutex_destroy(&counter.lock);
}

static void test_pool_runs_every_task(void)
{
  CounterState counter;
  memset(&counter, 0, sizeof(counter));
  pthread_mutex_init(&counter.lock, NULL);

  TaskGroup *group = task_group_create("counter");
  for (int i = 0; i < COUNTER_TASKS; i++)
    task_group_submit(group, counter_task, &counter);

  // Polled rather than waited on, because task_group_wait helps drain the queue and this test is
  // about who else runs the work. A waiting thread can legitimately take every task itself if it
  // gets there before the workers wake, which made the assertion below fail about one run in eight.
  // Polling is also what the client does with these jobs, so it is the more representative shape.
  while (!task_group_is_complete(group))
    ;

  report("every submitted task ran exactly once", counter.runs == COUNTER_TASKS);
  report("group drains to zero pending", task_group_pending(group) == 0);

  // At least one task should have run somewhere other than this thread, otherwise the pool
  // is not actually being used and the rest of these tests prove nothing about concurrency.
  bool ran_off_main = false;
  pthread_t self = pthread_self();
  for (int i = 0; i < counter.runs && i < COUNTER_TASKS; i++)
  {
    if (!pthread_equal(counter.thread_ids[i], self))
    {
      ran_off_main = true;
      break;
    }
  }
  report("work ran off the submitting thread", ran_off_main);

  task_group_destroy(group);
  pthread_mutex_destroy(&counter.lock);
}

// More tasks than the queue holds must not be dropped; the overflow runs inline.
static void test_queue_overflow_runs_inline(void)
{
  CounterState counter;
  memset(&counter, 0, sizeof(counter));
  pthread_mutex_init(&counter.lock, NULL);

  const int flood = 4096;
  TaskGroup *group = task_group_create("flood");
  for (int i = 0; i < flood; i++)
    task_group_submit(group, counter_task, &counter);
  task_group_wait(group);

  report("no task lost when the queue overflows", counter.runs == flood);

  task_group_destroy(group);
  pthread_mutex_destroy(&counter.lock);
}

static void test_independent_groups(void)
{
  CounterState a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  pthread_mutex_init(&a.lock, NULL);
  pthread_mutex_init(&b.lock, NULL);

  TaskGroup *group_a = task_group_create("a");
  TaskGroup *group_b = task_group_create("b");

  for (int i = 0; i < 8; i++)
    task_group_submit(group_a, counter_task, &a);
  for (int i = 0; i < 3; i++)
    task_group_submit(group_b, counter_task, &b);

  task_group_wait(group_a);
  task_group_wait(group_b);

  report("groups track their own tasks", a.runs == 8 && b.runs == 3);

  task_group_destroy(group_a);
  task_group_destroy(group_b);
  pthread_mutex_destroy(&a.lock);
  pthread_mutex_destroy(&b.lock);
}

// ---------------------------------------------------------------------------
// Threaded physics over a group of worlds
// ---------------------------------------------------------------------------

#define PHYSICS_WORLD_SIZE 16
#define PHYSICS_WORLD_COUNT 8

// world_step_fluids stops sweeping after 8ms of wall clock (src/world.c, the
// `now_ms() - t0 > 8ULL` break in the lateral fill). How much fluid moves in one call
// therefore depends on how fast the machine is at that instant, so two identical worlds
// stepped identically can legitimately end up in different states. That is a property of the
// simulation, not of threading — it reproduces with no threads involved at all — so these
// tests assert invariants that hold however many sweeps ran, rather than comparing snapshots.

// A stone floor with a column of water on top, offset per world so the worlds differ and any
// mix-up between them shows up as water in a column that world never had.
static World *build_fluid_world(int index)
{
  World *world = world_create(PHYSICS_WORLD_SIZE, PHYSICS_WORLD_SIZE, PHYSICS_WORLD_SIZE);
  if (!world)
    return NULL;

  for (uint32_t x = 0; x < world->width; x++)
    for (uint32_t y = 0; y < world->height; y++)
      world_set_voxel(world, x, y, 0, VOXEL_STONE);

  uint32_t cx = (uint32_t)(1 + index % (PHYSICS_WORLD_SIZE - 2));
  uint32_t cy = (uint32_t)(1 + (index * 3) % (PHYSICS_WORLD_SIZE - 2));
  for (uint32_t z = 1; z < world->depth - 1; z++)
    world_set_voxel(world, cx, cy, z, VOXEL_WATER);

  return world;
}

static uint64_t world_hash(const World *world)
{
  uint64_t hash = 1469598103934665603ULL; // FNV-1a
  size_t count = (size_t)world->width * world->height * world->depth;
  for (size_t i = 0; i < count; i++)
  {
    uint64_t value = (uint64_t)world->voxels[i].type ^
                     ((uint64_t)world->voxels[i].condition_mask << 8) ^
                     ((uint64_t)world->voxels[i].data8 << 16);
    for (int byte = 0; byte < 8; byte++)
    {
      hash ^= (value >> (byte * 8)) & 0xFF;
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

static int count_water_cells(const World *world)
{
  int cells = 0;
  size_t count = (size_t)world->width * world->height * world->depth;
  for (size_t i = 0; i < count; i++)
    if (world->voxels[i].type == VOXEL_WATER)
      cells++;
  return cells;
}

// Water settling onto the floor is the visible outcome of a step, so this is a per-world
// progress check rather than a snapshot comparison.
static bool water_reached_floor(const World *world)
{
  for (uint32_t y = 0; y < world->height; y++)
    for (uint32_t x = 0; x < world->width; x++)
    {
      const Voxel *v = &world->voxels[(size_t)(1 * world->height + y) * world->width + x];
      if (v->type == VOXEL_WATER)
        return true;
    }
  return false;
}

#define PHYSICS_CANARY_COUNT 4

static void test_threaded_physics_isolates_worlds(void)
{
  World *worlds[PHYSICS_WORLD_COUNT] = {0};
  World *canaries[PHYSICS_CANARY_COUNT] = {0};
  uint64_t hash_before[PHYSICS_WORLD_COUNT];
  uint64_t canary_before[PHYSICS_CANARY_COUNT];
  int water_before[PHYSICS_WORLD_COUNT];
  bool built = true;

  for (int i = 0; i < PHYSICS_WORLD_COUNT; i++)
  {
    worlds[i] = build_fluid_world(i);
    if (!worlds[i])
      built = false;
  }

  // Worlds with no water at all. Nothing in a correct run can add any, so a physics task that
  // writes through the wrong World pointer shows up here as a changed canary. Their sizes vary
  // so a stray write is also likely to land out of bounds and be caught by a sanitizer build.
  for (int i = 0; i < PHYSICS_CANARY_COUNT; i++)
  {
    canaries[i] = world_create((uint32_t)(8 + i * 4), (uint32_t)(8 + i * 4), (uint32_t)(8 + i * 4));
    if (!canaries[i])
    {
      built = false;
      continue;
    }
    for (uint32_t x = 0; x < canaries[i]->width; x++)
      for (uint32_t y = 0; y < canaries[i]->height; y++)
        world_set_voxel(canaries[i], x, y, 0, VOXEL_STONE);
  }

  report("physics test worlds allocated", built);
  if (!built)
    goto cleanup;

  for (int i = 0; i < PHYSICS_WORLD_COUNT; i++)
  {
    hash_before[i] = world_hash(worlds[i]);
    water_before[i] = count_water_cells(worlds[i]);
  }
  for (int i = 0; i < PHYSICS_CANARY_COUNT; i++)
    canary_before[i] = world_hash(canaries[i]);

  const float dt = 1.0f / 60.0f;
  const int budget = 4096;

  for (int step = 0; step < 8; step++)
  {
    WorldPhysicsBatch *batch = world_physics_batch_begin(dt, budget);
    // Interleaved so active worlds and canaries are in flight at the same time.
    for (int i = 0; i < PHYSICS_WORLD_COUNT; i++)
    {
      world_physics_batch_add(batch, worlds[i]);
      if (i < PHYSICS_CANARY_COUNT)
        world_physics_batch_add(batch, canaries[i]);
    }
    world_physics_batch_wait(batch);
    if (!world_physics_batch_is_complete(batch))
      report("batch drains before wait returns", false);
    world_physics_batch_end(batch);
  }

  bool canaries_clean = true;
  for (int i = 0; i < PHYSICS_CANARY_COUNT; i++)
    if (world_hash(canaries[i]) != canary_before[i])
    {
      printf("  canary %d (%ux%ux%u) was modified\n", i, canaries[i]->width,
             canaries[i]->height, canaries[i]->depth);
      canaries_clean = false;
    }
  report("worlds with no fluid are untouched by a concurrent batch", canaries_clean);

  bool all_advanced = true;
  bool all_settled = true;
  bool all_kept_water = true;
  for (int i = 0; i < PHYSICS_WORLD_COUNT; i++)
  {
    if (world_hash(worlds[i]) == hash_before[i])
    {
      printf("  world %d was never stepped\n", i);
      all_advanced = false;
    }
    if (!water_reached_floor(worlds[i]))
    {
      printf("  world %d has no water on the floor\n", i);
      all_settled = false;
    }
    if (count_water_cells(worlds[i]) < water_before[i])
    {
      printf("  world %d lost water cells: %d -> %d\n", i, water_before[i],
             count_water_cells(worlds[i]));
      all_kept_water = false;
    }
  }

  report("every world in the batch was stepped", all_advanced);
  report("fluid settled in every world stepped concurrently", all_settled);
  report("no world lost fluid to a concurrent step", all_kept_water);

cleanup:
  for (int i = 0; i < PHYSICS_WORLD_COUNT; i++)
    if (worlds[i])
      world_destroy(worlds[i]);
  for (int i = 0; i < PHYSICS_CANARY_COUNT; i++)
    if (canaries[i])
      world_destroy(canaries[i]);
}

// The client steps the world the player is in together with its loaded neighbours, so the set
// of worlds is derived from the universe rather than passed in. These worlds are small: the
// point is which worlds get picked, not how long a step takes.
static void test_neighbourhood_stepping(void)
{
  Universe universe;
  if (!universe_init(&universe, UNIVERSE_SEED_HEX, WORLD_AFFINITY, 10))
  {
    report("neighbourhood universe initialized", false);
    return;
  }

  World *cells[27] = {0};
  int placed = 0;
  const uint64_t base = 1000;

  for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
      {
        World *world = world_create(8, 8, 8);
        if (!world)
          continue;
        for (uint32_t x = 0; x < world->width; x++)
          for (uint32_t y = 0; y < world->height; y++)
            world_set_voxel(world, x, y, 0, VOXEL_STONE);
        world_set_voxel(world, 4, 4, 5, VOXEL_WATER);

        if (universe_place(&universe, (uint64_t)((int64_t)base + dx),
                           (uint64_t)((int64_t)base + dy), (uint64_t)((int64_t)base + dz), world))
          cells[placed++] = world;
        else
          world_destroy(world);
      }

  report("3x3x3 neighbourhood placed", placed == 27);

  World *center = universe_get(&universe, base, base, base);
  report("centre world found in the universe", center != NULL);

  int stepped = world_physics_step_neighbourhood(&universe, center, 1.0f / 60.0f, 4096);
  report("a tick steps the centre and its loaded face neighbours", stepped == 7);

  // An unplaced world has no neighbourhood, so it must still be stepped on its own.
  World *orphan = world_create(8, 8, 8);
  if (orphan)
  {
    int orphan_stepped = world_physics_step_neighbourhood(&universe, orphan, 1.0f / 60.0f, 4096);
    report("a world outside the universe is stepped alone", orphan_stepped == 1);
    world_destroy(orphan);
  }

  report("a NULL centre steps nothing",
         world_physics_step_neighbourhood(&universe, NULL, 1.0f / 60.0f, 4096) == 0);

  // The same world placed at two coordinates must not be handed to two tasks at once.
  World *shared = cells[0];
  universe_place(&universe, base + 50, base + 50, base + 50, shared);
  universe_place(&universe, base + 50, base + 50, base + 51, shared);
  World *shared_center = universe_get(&universe, base + 50, base + 50, base + 50);
  report("an aliased world is only stepped once",
         world_physics_step_neighbourhood(&universe, shared_center, 1.0f / 60.0f, 4096) == 1);

  for (int i = 0; i < placed; i++)
    if (cells[i])
      world_destroy(cells[i]);
  universe_free(&universe);
}

// ---------------------------------------------------------------------------
// Threaded world generation
// ---------------------------------------------------------------------------

// The point of moving generation off the main thread is that the caller can keep working
// while it runs, so this checks the job really does report progress from another thread and
// hands back a finished world, rather than blocking until it is done.
static void test_threaded_world_generation(void)
{
  GameWorlds *worlds = game_worlds_create("scheduler-test");
  report("game worlds created", worlds != NULL);
  if (!worlds)
    return;

  Universe universe;
  report("universe initialized",
         universe_init(&universe, UNIVERSE_SEED_HEX, WORLD_AFFINITY, 10));

  WorldGenRequest request = {
      .worlds = worlds,
      .universe = &universe,
      .home_only = true,
  };
  // Home + clouds + 5x5 wilderness plane under the island (landing cell + two rings).
  report("home-only request counts home, clouds, and wilderness rings",
         world_gen_job_total_steps(&request) == 28);

  WorldGenJob *job = world_gen_job_start(&request);
  report("generation job started", job != NULL);
  if (!job)
  {
    game_worlds_destroy(worlds);
    return;
  }

  // Poll the way the loading screen does. If the worker never yielded control the loop below
  // would not run at all, so a poll count of zero means generation blocked the caller.
  // Home-only startup now builds 28 worlds (5x5 wilderness + home/clouds), so a raw spin
  // cap is not a useful timeout — bound by wall clock instead.
  int polls = 0;
  char message[128] = {0};
  int progress = -1;
  const time_t wait_started = time(NULL);
  while (!world_gen_job_snapshot(job, &progress, message, sizeof(message)))
  {
    polls++;
    if (time(NULL) - wait_started > 600)
      break;
  }

  report("caller kept running while generation worked", polls > 0);
  report("job reports a progress message", message[0] != '\0');
  report("job reports full progress on completion", progress == 28);
  report("wilderness plane under the island was generated",
         universe_has(&universe, 0, 0, 0) && worlds->wilderness_world != NULL);
  report("wilderness ring-2 corner was generated",
         universe_has(&universe, 2, 2, 0));

  World *home = world_gen_job_primary_world(job);
  report("generation produced a home world", home != NULL);
  report("home world is the one in the game worlds", home == worlds->home_world);
  report("generated world has voxels", home && home->voxels != NULL);
  report("generated world was placed in the universe",
         universe_has(&universe, 0, 0, UNIVERSE_HOME_Z));

  // The island must have something solid in it, otherwise there is nothing to spawn onto.
  bool has_solid = false;
  if (home)
  {
    size_t count = (size_t)home->width * home->height * home->depth;
    for (size_t i = 0; i < count; i++)
      if (home->voxels[i].type != VOXEL_AIR)
      {
        has_solid = true;
        break;
      }
  }
  report("generated island contains solid ground", has_solid);

  world_gen_job_destroy(job);
  game_worlds_destroy(worlds);
}

// Moving generation to a worker is only safe if a world does not depend on which thread built
// it. It used to: the seed_id and rarity derivation hashed a raw digest through a strlen-based
// helper, so both picked up whatever sat on the stack behind the buffer, which differs between
// a worker stack and the main thread's.
static void test_generation_is_thread_independent(void)
{
  const char *seed = "thread-independence-seed";

  World *on_main = world_create(WORLD_SIZE_CUBE);
  report("main-thread world allocated", on_main != NULL);
  if (!on_main)
    return;
  world_generate_with_type(on_main, seed, WORLD_TYPE_HOME);

  GameWorlds *worlds = game_worlds_create("thread-independence");
  Universe universe;
  if (!worlds || !universe_init(&universe, UNIVERSE_SEED_HEX, WORLD_AFFINITY, 10))
  {
    report("worker-thread world allocated", false);
    world_destroy(on_main);
    if (worlds)
      game_worlds_destroy(worlds);
    return;
  }

  // game_worlds_create derives its own seed, so match it to the world above for the comparison.
  free(worlds->base_seed);
  worlds->base_seed = strdup(seed);

  WorldGenRequest request = {.worlds = worlds, .universe = &universe, .home_only = true};
  WorldGenJob *job = world_gen_job_start(&request);
  report("worker-thread world allocated", job != NULL);
  if (!job)
  {
    world_destroy(on_main);
    game_worlds_destroy(worlds);
    return;
  }
  while (!world_gen_job_is_complete(job))
    ;

  World *on_worker = world_gen_job_primary_world(job);
  if (on_worker)
  {
    report("same seed_id whichever thread generated it",
           strcmp(on_main->seed_id, on_worker->seed_id) == 0);
    report("same rarity whichever thread generated it",
           on_main->rarity == on_worker->rarity);
    report("same gravity whichever thread generated it",
           on_main->gravity == on_worker->gravity);
    report("same voxels whichever thread generated it",
           world_hash(on_main) == world_hash(on_worker));
  }
  else
  {
    report("same voxels whichever thread generated it", false);
  }

  world_gen_job_destroy(job);
  world_destroy(on_main);
  game_worlds_destroy(worlds);
}

// ---------------------------------------------------------------------------
// Nested waiting
// ---------------------------------------------------------------------------

static int g_nested_leaf_runs = 0;

static void nested_leaf(void *user_data)
{
  (void)user_data;
  __sync_fetch_and_add(&g_nested_leaf_runs, 1);
}

#define NESTED_LEAVES 53
// Small enough that generating 18 of them is a test rather than a wait, large enough that the
// decoration passes still run and draw from the seeded RNG.
#define DETERMINISM_SIZE 48

static void nested_parent(void *user_data)
{
  (void)user_data;
  TaskGroup *group = task_group_create("nested-leaves");
  for (int i = 0; i < NESTED_LEAVES; i++)
    task_group_submit(group, nested_leaf, NULL);
  task_group_wait(group);
  task_group_destroy(group);
}

// World generation submits one task per world from inside a job that is itself a task, so a waiter
// has to be able to make progress on the work it is waiting for. When task_group_wait only slept,
// this hung outright on a pool with no spare worker — which is what a single-core machine gets,
// since the default worker count is one less than the CPU count.
//
// Run on a deliberately undersized pool: one worker, 53 leaves. If waiting did not help, the one
// worker would be parked inside nested_parent and nothing would ever drain the queue.
static void test_nested_wait_on_undersized_pool(void)
{
  task_scheduler_shutdown();
  report("pool restarted with a single worker", task_scheduler_init(1));
  report("pool really has one worker", task_scheduler_worker_count() == 1);

  g_nested_leaf_runs = 0;
  TaskGroup *outer = task_group_create("nested-parent");
  task_group_submit(outer, nested_parent, NULL);
  task_group_wait(outer);
  task_group_destroy(outer);

  report("a task waiting on its own tasks still finishes them",
         g_nested_leaf_runs == NESTED_LEAVES);
}

// ---------------------------------------------------------------------------
// Determinism across worker counts
// ---------------------------------------------------------------------------

typedef struct
{
  World *world;
  char seed[64];
  WorldGenerationType type;
} GenPlan;

static void gen_plan_run(void *user_data)
{
  GenPlan *plan = (GenPlan *)user_data;
  world_generate_with_type(plan->world, plan->seed, plan->type);
}

#define DETERMINISM_WORLDS 6

// Generate the same set of worlds at a given pool size and hash each one.
static void generate_set_hashes(int workers, uint64_t *out_hashes)
{
  task_scheduler_shutdown();
  if (workers > 0)
    task_scheduler_init(workers);

  world_generation_prepare_shared_state();

  GenPlan plans[DETERMINISM_WORLDS];
  for (int i = 0; i < DETERMINISM_WORLDS; i++)
  {
    snprintf(plans[i].seed, sizeof(plans[i].seed), "determinism-%d", i);
    // Home worlds are the ones that matter here: their decoration passes draw thousands of values
    // from the seeded RNG, which is exactly the state that used to be shared between threads.
    plans[i].type = (i % 2) ? WORLD_TYPE_HOME : WORLD_TYPE_WILDERNESS;
    plans[i].world = world_create(DETERMINISM_SIZE, DETERMINISM_SIZE, DETERMINISM_SIZE);
  }

  TaskGroup *group = task_group_create("determinism");
  for (int i = 0; i < DETERMINISM_WORLDS; i++)
    task_group_submit(group, gen_plan_run, &plans[i]);
  task_group_wait(group);
  task_group_destroy(group);

  for (int i = 0; i < DETERMINISM_WORLDS; i++)
  {
    out_hashes[i] = plans[i].world ? world_hash(plans[i].world) : 0;
    if (plans[i].world)
      world_destroy(plans[i].world);
  }
}

// Generating several worlds at once must produce the same worlds as generating them one at a time.
// It did not: the seeded RNG the decoration passes draw from was one process-wide variable, so two
// concurrent home worlds interleaved their draws and neither got the sequence its seed asks for.
// Trees and bushes landed somewhere different on every run. Making it thread-local is exact rather
// than approximate, because one world is generated start to finish by one task on one thread.
static void test_generation_is_worker_count_independent(void)
{
  uint64_t serial[DETERMINISM_WORLDS] = {0};
  uint64_t parallel[DETERMINISM_WORLDS] = {0};
  uint64_t crowded[DETERMINISM_WORLDS] = {0};

  generate_set_hashes(1, serial);
  generate_set_hashes(4, parallel);
  // More workers than worlds, so every world starts at about the same moment — the arrangement most
  // likely to interleave anything still shared.
  generate_set_hashes(DETERMINISM_WORLDS * 2, crowded);

  bool all_match = true;
  bool any_nonzero = false;
  for (int i = 0; i < DETERMINISM_WORLDS; i++)
  {
    if (serial[i] != parallel[i] || serial[i] != crowded[i])
      all_match = false;
    if (serial[i] != 0)
      any_nonzero = true;
  }

  report("worlds actually generated", any_nonzero);
  report("the same worlds come out at 1, 4 and 12 workers", all_match);

  // A guard on the comparison itself: if every world hashed the same, the check above would pass
  // without proving anything. Home and wilderness worlds must differ from each other.
  report("the compared worlds are not all identical", serial[0] != serial[1]);
}

int main(void)
{
  printf("=== Task Scheduler Tests ===\n\n");

  printf("-- inline fallback (no pool) --\n");
  test_inline_fallback();

  printf("\n-- worker pool --\n");
  if (!task_scheduler_init(4))
  {
    printf("FAILED could not start worker pool\n");
    return 1;
  }
  report("pool reports running", task_scheduler_is_running());
  report("pool reports its worker count", task_scheduler_worker_count() == 4);

  test_pool_runs_every_task();
  test_queue_overflow_runs_inline();
  test_independent_groups();

  printf("\n-- threaded world physics --\n");
  test_threaded_physics_isolates_worlds();

  printf("\n-- neighbourhood stepping --\n");
  test_neighbourhood_stepping();

  printf("\n-- threaded world generation --\n");
  test_threaded_world_generation();
  test_generation_is_thread_independent();

  printf("\n-- nested waiting --\n");
  test_nested_wait_on_undersized_pool();

  printf("\n-- determinism across worker counts --\n");
  test_generation_is_worker_count_independent();

  task_scheduler_shutdown();
  report("pool stops cleanly", !task_scheduler_is_running());

  printf("\n=== %s (%d failure%s) ===\n", test_failures == 0 ? "ALL PASSED" : "FAILURES",
         test_failures, test_failures == 1 ? "" : "s");
  return test_failures == 0 ? 0 : 1;
}
