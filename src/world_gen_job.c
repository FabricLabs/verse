#include "world_gen_job.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "task_scheduler.h"

#define WORLD_GEN_FULL_STEPS 54
// Home + cloud below + sky above, then a Chebyshev ring-2 wilderness plane under the island
// (center cell + two rings = 5x5 at universe z=0).
#define WORLD_GEN_WILDERNESS_RINGS 2
#define WORLD_GEN_WILDERNESS_PLANE \
  ((2 * WORLD_GEN_WILDERNESS_RINGS + 1) * (2 * WORLD_GEN_WILDERNESS_RINGS + 1))
#define WORLD_GEN_HOME_CORE_STEPS 3
#define WORLD_GEN_HOME_STEPS (WORLD_GEN_HOME_CORE_STEPS + WORLD_GEN_WILDERNESS_PLANE)
#define WORLD_GEN_MESSAGE_MAX 128

struct WorldGenJob
{
  WorldGenRequest request;

  TaskGroup *group;

  // Everything below is written by the worker and read by the main thread, so all access
  // goes through this lock. It is held only for the duration of a strcpy or an int store,
  // never across generation itself.
  pthread_mutex_t lock;
  int progress;
  char message[WORLD_GEN_MESSAGE_MAX];
  bool complete;
  World *primary_world;
};

static void job_publish(WorldGenJob *job, int progress, const char *message)
{
  pthread_mutex_lock(&job->lock);
  job->progress = progress;
  if (message)
  {
    strncpy(job->message, message, sizeof(job->message) - 1);
    job->message[sizeof(job->message) - 1] = '\0';
  }
  pthread_mutex_unlock(&job->lock);
}

// Bump the finished-world count by one. Several workers call this at once, so it is a read-modify-
// write under the same lock the snapshot uses rather than a store of a computed step number.
static void job_advance(WorldGenJob *job)
{
  pthread_mutex_lock(&job->lock);
  job->progress++;
  pthread_mutex_unlock(&job->lock);
}

static void job_finish(WorldGenJob *job, World *primary)
{
  pthread_mutex_lock(&job->lock);
  job->primary_world = primary;
  job->complete = true;
  pthread_mutex_unlock(&job->lock);
}

int world_gen_job_total_steps(const WorldGenRequest *request)
{
  if (!request)
    return 0;
  return request->home_only ? WORLD_GEN_HOME_STEPS : WORLD_GEN_FULL_STEPS;
}

// One world to generate. Filled in on the job thread, generated on whichever worker picks it up,
// and only then read back for placement — so nothing here is shared between two tasks.
typedef struct
{
  WorldGenJob *job;
  World *world;                // pre-allocated, so a worker never has to allocate under contention
  char *seed;                  // owned when derived per coordinate, NULL when the base seed is used
  const char *base_seed;       // used when seed is NULL
  WorldGenerationType type;
  // The universe cell this world will occupy, signed because the neighbourhood extends west and
  // south of the origin.
  int64_t cell_x, cell_y, cell_z;
  bool place_adjacent;         // place relative to the wilderness origin rather than at the cell
} PlanEntry;

static void plan_entry_generate(void *user_data)
{
  PlanEntry *entry = (PlanEntry *)user_data;
  if (!entry->world)
    return;

  // Where the world is, before it is generated. The generators sample noise in universe space, so
  // these decide both how one cell differs from the next and whether terrain lines up across the
  // boundary between them; generating first and placing afterwards produced a neighbourhood of
  // identical worlds. universe_place assigns the same values again when the plan is placed.
  entry->world->universe_x = (uint64_t)entry->cell_x;
  entry->world->universe_y = (uint64_t)entry->cell_y;
  entry->world->universe_z = (uint64_t)entry->cell_z;

  world_generate_with_type(entry->world, entry->seed ? entry->seed : entry->base_seed, entry->type);

  // Progress is a count of worlds finished, not a position in a sequence: with several running at
  // once there is no single "current" world, and a bar that only moves forward is what the loading
  // screen needs.
  job_advance(entry->job);
}

// Run a plan across the pool and wait for it. Falls back to running the entries in order when there
// is no pool, which is what the headless tools and tests get.
static void plan_run(PlanEntry *plan, int count)
{
  TaskGroup *group = task_group_create("world-gen-fanout");
  if (!group)
  {
    for (int i = 0; i < count; i++)
      plan_entry_generate(&plan[i]);
    return;
  }

  for (int i = 0; i < count; i++)
    task_group_submit(group, plan_entry_generate, &plan[i]);
  task_group_wait(group);
  task_group_destroy(group);
}

// The player's home island, the cloud layers above/below it, and two Chebyshev rings of wilderness
// on the ground plane under the island. The loading screen waits for all of these so the sky island
// can look out over distance terrain immediately — not only after streaming catches up.
static World *generate_home_island(WorldGenJob *job)
{
  GameWorlds *worlds = job->request.worlds;
  Universe *universe = job->request.universe;

  world_generation_prepare_shared_state();
  job_publish(job, 0, "Generating home world...");

  const int64_t cloud_z = (int64_t)UNIVERSE_HOME_Z - 1;
  const int64_t sky_z = (int64_t)UNIVERSE_HOME_Z + 1;
  const int64_t wild_z = 0;

  worlds->home_world = world_create(WORLD_SIZE_CUBE);
  if (!worlds->home_world)
    return NULL;

  PlanEntry plan[WORLD_GEN_HOME_STEPS];
  int count = 0;

  // The home island keeps the character's own seed rather than a per-coordinate one. It is the world
  // the save is named for, and unlike its neighbours it is owned by GameWorlds and never evicted, so
  // it is never regenerated and cannot disagree with itself.
  plan[count++] = (PlanEntry){.job = job, .world = worlds->home_world,
                              .base_seed = worlds->base_seed, .type = WORLD_TYPE_HOME,
                              .cell_z = (int64_t)UNIVERSE_HOME_Z};

  for (int i = 0; i < 2; i++)
  {
    const int64_t z = (i == 0) ? cloud_z : sky_z;
    if (universe_has(universe, 0, 0, (uint64_t)z))
      continue;
    World *cloud = world_create(WORLD_SIZE_CUBE);
    if (!cloud)
      continue;
    // The same per-coordinate seed the streamer would derive for this cell. Clouds are not owned by
    // GameWorlds, so walking away and coming back regenerates them through the streaming path; using
    // one rule for the seed is what makes that produce the same sky twice.
    WorldCoord coord = {0, 0, (int)z};
    plan[count++] = (PlanEntry){.job = job, .world = cloud,
                                .seed = world_generate_seed_for_coord(worlds->base_seed, coord),
                                .base_seed = worlds->base_seed, .type = WORLD_TYPE_CLOUD,
                                .cell_z = z};
  }

  // Wilderness plane under the island: the cell the player lands in, plus two rings around it.
  // Types come from the same WFC decision streaming uses, so a town under the drop matches later.
  for (int dy = -WORLD_GEN_WILDERNESS_RINGS; dy <= WORLD_GEN_WILDERNESS_RINGS; dy++)
  {
    for (int dx = -WORLD_GEN_WILDERNESS_RINGS; dx <= WORLD_GEN_WILDERNESS_RINGS; dx++)
    {
      if (universe_has(universe, (uint64_t)(int64_t)dx, (uint64_t)(int64_t)dy, (uint64_t)wild_z))
        continue;

      WorldGenerationType gtype;
      VoxelType fill = VOXEL_AIR;
      if (!universe_wfc_decide_cell(UNIVERSE_GEN_GAMEWORLD, dx, dy, (int)wild_z, &gtype, &fill))
        continue;
      (void)fill; // wilderness plane is never SOLID; streaming would apply fill if it were

      World *cell = world_create(WORLD_SIZE_CUBE);
      if (!cell)
        continue;

      WorldCoord coord = {dx, dy, (int)wild_z};
      plan[count++] = (PlanEntry){.job = job, .world = cell,
                                  .seed = world_generate_seed_for_coord(worlds->base_seed, coord),
                                  .base_seed = worlds->base_seed, .type = gtype,
                                  .cell_x = dx, .cell_y = dy, .cell_z = wild_z};
      if (dx == 0 && dy == 0)
        worlds->wilderness_world = cell;
    }
  }

  char message[WORLD_GEN_MESSAGE_MAX];
  snprintf(message, sizeof(message), "Generating %d worlds across %d threads...", count,
           task_scheduler_worker_count() > 0 ? task_scheduler_worker_count() : 1);
  job_publish(job, 0, message);

  plan_run(plan, count);

  for (int i = 0; i < count; i++)
  {
    const uint64_t x = (uint64_t)plan[i].cell_x;
    const uint64_t y = (uint64_t)plan[i].cell_y;
    const uint64_t z = (uint64_t)plan[i].cell_z;
    if (!universe_has(universe, x, y, z))
      universe_place(universe, x, y, z, plan[i].world);
    free(plan[i].seed);
  }

  job_publish(job, count, "World generation complete!");
  return worlds->home_world;
}

// The full new-game neighbourhood: 1 wilderness + 1 home + 1 farm + 25 adjacent farms + 26
// contiguous wilderness worlds. Two quirks are preserved from the original step machine because
// saves and callers depend on them: adjacent_farm_worlds[0] is never filled (the index starts at 1),
// and the centre of the 3x3x3 wilderness ring is skipped because it is the player's own world.
//
// All of these are generated concurrently. Placement into the Universe is not: universe_place
// rehashes a table shared by every cell, so the workers only fill their own World and this thread
// places them afterwards.
static World *generate_full_neighbourhood(WorldGenJob *job)
{
  GameWorlds *worlds = job->request.worlds;
  Universe *universe = job->request.universe;

  // Built once here rather than raced for by the first worker to sample noise.
  world_generation_prepare_shared_state();

  PlanEntry *plan = (PlanEntry *)calloc(WORLD_GEN_FULL_STEPS, sizeof(PlanEntry));
  if (!plan)
    return NULL;

  int count = 0;

  worlds->wilderness_world = world_create(WORLD_SIZE_CUBE);
  plan[count++] = (PlanEntry){.job = job, .world = worlds->wilderness_world,
                              .base_seed = worlds->base_seed, .type = WORLD_TYPE_WILDERNESS};

  worlds->home_world = world_create(WORLD_SIZE_CUBE);
  plan[count++] = (PlanEntry){.job = job, .world = worlds->home_world,
                              .base_seed = worlds->base_seed, .type = WORLD_TYPE_HOME};

  worlds->farm_world = world_create(WORLD_SIZE_CUBE);
  plan[count++] = (PlanEntry){.job = job, .world = worlds->farm_world,
                              .base_seed = worlds->base_seed, .type = WORLD_TYPE_FARM};

  for (int world_index = 1; world_index <= 25; world_index++)
  {
    worlds->adjacent_farm_worlds[world_index] = world_create(WORLD_SIZE_CUBE);
    plan[count++] = (PlanEntry){.job = job, .world = worlds->adjacent_farm_worlds[world_index],
                                .base_seed = worlds->base_seed, .type = WORLD_TYPE_FARM};
  }

  for (int widx = 0; widx < 27; widx++)
  {
    const int xi = (widx % 3) - 1;
    const int yi = ((widx / 3) % 3) - 1;
    const int zi = (widx / 9) - 1;
    if (xi == 0 && yi == 0 && zi == 0)
      continue; // the centre is the player's own world

    WorldCoord coord = {xi, yi, zi};
    char *seed = world_generate_seed_for_coord(worlds->base_seed, coord);
    if (!seed)
      continue;

    World *adjacent = world_create(WORLD_SIZE_CUBE);
    if (!adjacent)
    {
      free(seed);
      continue;
    }
    plan[count++] = (PlanEntry){.job = job, .world = adjacent, .seed = seed,
                                .base_seed = worlds->base_seed, .type = WORLD_TYPE_WILDERNESS,
                                .cell_x = xi, .cell_y = yi, .cell_z = zi,
                                .place_adjacent = true};
  }

  char message[WORLD_GEN_MESSAGE_MAX];
  snprintf(message, sizeof(message), "Generating %d worlds across %d threads...", count,
           task_scheduler_worker_count() > 0 ? task_scheduler_worker_count() : 1);
  job_publish(job, 0, message);

  plan_run(plan, count);

  // Placement, on this thread only.
  if (worlds->wilderness_world && !universe_has(universe, 0, 0, 0))
    universe_place(universe, 0, 0, 0, worlds->wilderness_world);

  for (int i = 0; i < count; i++)
  {
    if (plan[i].place_adjacent && plan[i].world)
      universe_place_adjacent(universe, 0, 0, 0, (int)plan[i].cell_x, (int)plan[i].cell_y,
                              (int)plan[i].cell_z, plan[i].world);
    free(plan[i].seed);
  }
  free(plan);

  job_publish(job, WORLD_GEN_FULL_STEPS, "World generation complete!");
  return worlds->home_world ? worlds->home_world : worlds->wilderness_world;
}

static void world_gen_job_run(void *user_data)
{
  WorldGenJob *job = (WorldGenJob *)user_data;
  if (!job)
    return;

  World *primary = job->request.home_only ? generate_home_island(job)
                                          : generate_full_neighbourhood(job);
  job_finish(job, primary);
}

WorldGenJob *world_gen_job_start(const WorldGenRequest *request)
{
  if (!request || !request->worlds || !request->universe || !request->worlds->base_seed)
    return NULL;

  WorldGenJob *job = (WorldGenJob *)calloc(1, sizeof(WorldGenJob));
  if (!job)
    return NULL;

  job->request = *request;
  if (pthread_mutex_init(&job->lock, NULL) != 0)
  {
    free(job);
    return NULL;
  }
  strncpy(job->message, "Initializing world generation...", sizeof(job->message) - 1);

  job->group = task_group_create("world-gen");
  if (!job->group)
  {
    pthread_mutex_destroy(&job->lock);
    free(job);
    return NULL;
  }

  // With no worker pool this runs inline and returns already complete, which is what the
  // headless tools want.
  task_group_submit(job->group, world_gen_job_run, job);
  return job;
}

bool world_gen_job_snapshot(WorldGenJob *job, int *out_progress, char *out_message,
                           size_t message_size)
{
  if (!job)
    return false;

  pthread_mutex_lock(&job->lock);
  if (out_progress)
    *out_progress = job->progress;
  if (out_message && message_size > 0)
  {
    strncpy(out_message, job->message, message_size - 1);
    out_message[message_size - 1] = '\0';
  }
  bool complete = job->complete;
  pthread_mutex_unlock(&job->lock);
  return complete;
}

bool world_gen_job_is_complete(WorldGenJob *job)
{
  return world_gen_job_snapshot(job, NULL, NULL, 0);
}

World *world_gen_job_primary_world(WorldGenJob *job)
{
  if (!job)
    return NULL;
  pthread_mutex_lock(&job->lock);
  World *world = job->complete ? job->primary_world : NULL;
  pthread_mutex_unlock(&job->lock);
  return world;
}

void world_gen_job_destroy(WorldGenJob *job)
{
  if (!job)
    return;
  task_group_destroy(job->group); // waits for the worker before freeing anything it touches
  pthread_mutex_destroy(&job->lock);
  free(job);
}

// ---------------------------------------------------------------------------
// Single-cell generation, for streaming

struct CellGenJob
{
  TaskGroup *group;
  // Wide enough for a 64-character universe seed plus the coordinate suffix
  // world_generate_seed_for_coord appends; that helper caps its own output at 128.
  char seed[128];
  uint64_t ux, uy, uz;

  pthread_mutex_t lock;
  bool complete;
  World *world;
};

static void cell_gen_run(void *user_data)
{
  CellGenJob *job = (CellGenJob *)user_data;

  WorldGenerationType gtype;
  VoxelType fill;
  World *made = NULL;

  // The same WFC decision the synchronous path makes, so a cell streamed in matches what would have
  // been generated for it eagerly. Generation is seeded, so this is reproducible.
  if (universe_wfc_decide_cell(UNIVERSE_GEN_GAMEWORLD, (int)job->ux, (int)job->uy, (int)job->uz,
                              &gtype, &fill))
  {
    made = world_create(WORLD_SIZE_CUBE);
    if (made)
    {
      // Tell the world where it is *before* generating it. The generators sample their noise in
      // universe space (sample_field_noise converts through world->universe_*), so these three
      // fields are what make one cell differ from the next and what makes terrain line up across a
      // boundary. They used to be assigned by the caller after placement, which is after generation
      // — so every streamed world in the game was generated as if it sat at the origin, and a layer
      // came out as the same world repeated. universe_place sets them again to the same values.
      made->universe_x = job->ux;
      made->universe_y = job->uy;
      made->universe_z = job->uz;

      if (gtype == WORLD_TYPE_SOLID)
        world_generate_with_type_and_fill(made, job->seed, gtype, fill);
      else
        world_generate_with_type(made, job->seed, gtype);

      // Built here rather than on the main thread: this is the expensive part, and the world is not
      // reachable by anything else until the caller takes it.
      world_refresh_occupancy_bitfield(made);
      world_build_heightmap(made);
    }
  }

  pthread_mutex_lock(&job->lock);
  job->world = made;
  job->complete = true;
  pthread_mutex_unlock(&job->lock);
}

CellGenJob *cell_gen_job_start(const char *base_seed, uint64_t ux, uint64_t uy, uint64_t uz)
{
  if (!base_seed)
    return NULL;

  CellGenJob *job = (CellGenJob *)calloc(1, sizeof(CellGenJob));
  if (!job)
    return NULL;

  job->ux = ux;
  job->uy = uy;
  job->uz = uz;

  // Each cell gets its own seed derived from the universe's. Universe-space noise already varies by
  // coordinate, but the decoration passes draw from seed_random instead, so sharing one seed across a
  // layer gave every world on it the same trees in the same places. The cast to int is what
  // world_generate_seed_for_coord takes and is exact for cells this close to the origin, negative
  // ones included.
  WorldCoord coord = {(int)(int64_t)ux, (int)(int64_t)uy, (int)(int64_t)uz};
  char *derived = world_generate_seed_for_coord(base_seed, coord);
  strncpy(job->seed, derived ? derived : base_seed, sizeof(job->seed) - 1);
  free(derived);

  if (pthread_mutex_init(&job->lock, NULL) != 0)
  {
    free(job);
    return NULL;
  }

  job->group = task_group_create("cell-gen");
  if (!job->group)
  {
    pthread_mutex_destroy(&job->lock);
    free(job);
    return NULL;
  }

  // With no worker pool this runs inline and the job is already complete on return, which is what
  // lets the headless tools use the same code path.
  task_group_submit(job->group, cell_gen_run, job);
  return job;
}

bool cell_gen_job_is_complete(const CellGenJob *job)
{
  if (!job)
    return true;
  CellGenJob *mutable_job = (CellGenJob *)job;
  pthread_mutex_lock(&mutable_job->lock);
  const bool complete = mutable_job->complete;
  pthread_mutex_unlock(&mutable_job->lock);
  return complete;
}

void cell_gen_job_cell(const CellGenJob *job, uint64_t *ux, uint64_t *uy, uint64_t *uz)
{
  if (!job)
    return;
  if (ux)
    *ux = job->ux;
  if (uy)
    *uy = job->uy;
  if (uz)
    *uz = job->uz;
}

World *cell_gen_job_take_world(CellGenJob *job)
{
  if (!job)
    return NULL;
  pthread_mutex_lock(&job->lock);
  World *world = job->complete ? job->world : NULL;
  if (world)
    job->world = NULL;
  pthread_mutex_unlock(&job->lock);
  return world;
}

void cell_gen_job_destroy(CellGenJob *job)
{
  if (!job)
    return;
  task_group_destroy(job->group); // waits, so the worker cannot still be writing
  // A world still held here was never taken, so this job owns it.
  if (job->world)
    world_destroy(job->world);
  pthread_mutex_destroy(&job->lock);
  free(job);
}
