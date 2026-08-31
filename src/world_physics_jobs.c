#include "world_physics_jobs.h"

#include <stdlib.h>

#include "fire_sim.h"
#include "task_scheduler.h"
#include "voxel_fracture.h"

#define WORLD_PHYSICS_BATCH_MAX 64

typedef struct
{
  World *world;
  float delta_seconds;
  int fluid_budget;
  bool full_step; // actors + fracture; false = fluids/fire only (face neighbours)
} WorldPhysicsTask;

struct WorldPhysicsBatch
{
  TaskGroup *group;
  WorldPhysicsTask tasks[WORLD_PHYSICS_BATCH_MAX];
  int task_count;
  float delta_seconds;
  int fluid_budget;
};

// One world's simulation step. Touches only the world it was handed.
static void world_physics_task_run(void *user_data)
{
  WorldPhysicsTask *task = (WorldPhysicsTask *)user_data;
  if (!task || !task->world)
    return;

  world_step_fluids(task->world, task->fluid_budget);
  fire_sim_step(task->world, task->delta_seconds);
  if (task->full_step)
  {
    world_step_actors(task->world, task->delta_seconds);
    voxel_fracture_step_falling(task->world);
  }
}

WorldPhysicsBatch *world_physics_batch_begin(float delta_seconds, int fluid_budget)
{
  WorldPhysicsBatch *batch = (WorldPhysicsBatch *)calloc(1, sizeof(WorldPhysicsBatch));
  if (!batch)
    return NULL;

  batch->group = task_group_create("world-physics");
  if (!batch->group)
  {
    free(batch);
    return NULL;
  }

  batch->delta_seconds = delta_seconds;
  batch->fluid_budget = fluid_budget;
  return batch;
}

static bool world_physics_batch_add_full(WorldPhysicsBatch *batch, World *world, bool full_step)
{
  if (!batch || !world || batch->task_count >= WORLD_PHYSICS_BATCH_MAX)
    return false;

  // The task struct lives in the batch, so it stays valid until the batch is waited on.
  WorldPhysicsTask *task = &batch->tasks[batch->task_count++];
  task->world = world;
  task->delta_seconds = batch->delta_seconds;
  task->fluid_budget = batch->fluid_budget;
  task->full_step = full_step;

  return task_group_submit(batch->group, world_physics_task_run, task);
}

bool world_physics_batch_add(WorldPhysicsBatch *batch, World *world)
{
  return world_physics_batch_add_full(batch, world, true);
}

int world_physics_batch_pending(WorldPhysicsBatch *batch)
{
  return batch ? task_group_pending(batch->group) : 0;
}

bool world_physics_batch_is_complete(WorldPhysicsBatch *batch)
{
  return batch ? task_group_is_complete(batch->group) : true;
}

void world_physics_batch_wait(WorldPhysicsBatch *batch)
{
  if (batch)
    task_group_wait(batch->group);
}

void world_physics_batch_end(WorldPhysicsBatch *batch)
{
  if (!batch)
    return;
  task_group_destroy(batch->group);
  free(batch);
}

void world_physics_step_world(World *world, float delta_seconds, int fluid_budget)
{
  if (!world)
    return;

  WorldPhysicsBatch *batch = world_physics_batch_begin(delta_seconds, fluid_budget);
  if (!batch)
  {
    // Allocation failed; stepping inline still beats dropping the frame's simulation.
    world_step_fluids(world, fluid_budget);
    fire_sim_step(world, delta_seconds);
    world_step_actors(world, delta_seconds);
    voxel_fracture_step_falling(world);
    return;
  }

  world_physics_batch_add(batch, world);
  world_physics_batch_end(batch);
}

// Locates a world in the universe by pointer. The universe is a hash map keyed by coordinate,
// so there is no reverse lookup and the entry table has to be scanned; it holds one entry per
// loaded world, so this is cheap at the scale the client runs at.
static bool universe_find_world(const Universe *universe, const World *world, uint64_t *out_x,
                                uint64_t *out_y, uint64_t *out_z)
{
  if (!universe || !universe->entries || !world)
    return false;

  for (size_t i = 0; i < universe->capacity; i++)
  {
    if (universe->entries[i].used && universe->entries[i].w == world)
    {
      *out_x = universe->entries[i].x;
      *out_y = universe->entries[i].y;
      *out_z = universe->entries[i].z;
      return true;
    }
  }
  return false;
}

int world_physics_step_neighbourhood(const Universe *universe, World *center,
                                     float delta_seconds, int fluid_budget)
{
  if (!center)
    return 0;

  uint64_t cx = 0, cy = 0, cz = 0;
  if (!universe_find_world(universe, center, &cx, &cy, &cz))
  {
    world_physics_step_world(center, delta_seconds, fluid_budget);
    return 1;
  }

  // Face offsets only (no centre). Centre runs on this thread while faces run on workers so
  // wall time is ~max(centre, slowest face) instead of waiting on a batch that also includes
  // the centre. Corners/edge-diagonals stay out: they rarely exchange fluid with the player cell.
  static const int k_faces[6][3] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
  };

  World *faces[6];
  int face_count = 0;
  for (int i = 0; i < 6; i++)
  {
    const uint64_t nx = (uint64_t)((int64_t)cx + k_faces[i][0]);
    const uint64_t ny = (uint64_t)((int64_t)cy + k_faces[i][1]);
    const uint64_t nz = (uint64_t)((int64_t)cz + k_faces[i][2]);
    World *neighbour = universe_get(universe, nx, ny, nz);
    if (!neighbour || neighbour == center)
      continue;
    bool dup = false;
    for (int j = 0; j < face_count; j++)
      if (faces[j] == neighbour)
        dup = true;
    if (dup)
      continue;
    faces[face_count++] = neighbour;
  }

  WorldPhysicsBatch *batch = NULL;
  int queued = 0;
  if (face_count > 0)
  {
    batch = world_physics_batch_begin(delta_seconds, fluid_budget);
    if (batch)
    {
      for (int i = 0; i < face_count; i++)
      {
        // Faces keep fluid/fire continuity; actors and fracture stay on the centre world so a
        // populated wilderness ring does not multiply mob AI cost every physics tick.
        if (world_physics_batch_add_full(batch, faces[i], false))
          queued++;
      }
    }
  }

  world_step_fluids(center, fluid_budget);
  fire_sim_step(center, delta_seconds);
  world_step_actors(center, delta_seconds);
  voxel_fracture_step_falling(center);

  if (queued > 0)
  {
    world_physics_batch_wait(batch);
    world_physics_batch_end(batch);
    return 1 + queued;
  }

  if (batch)
    world_physics_batch_end(batch);

  // No worker pool (or batch alloc failed): step faces inline after the centre.
  for (int i = 0; i < face_count; i++)
  {
    world_step_fluids(faces[i], fluid_budget);
    fire_sim_step(faces[i], delta_seconds);
  }
  return 1 + face_count;
}

void world_physics_step_springs(World *world, unsigned long long now_microseconds)
{
  if (!world)
    return;
  world_update_springs(world, (uint64_t)now_microseconds);
}
