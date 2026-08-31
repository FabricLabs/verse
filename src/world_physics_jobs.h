#ifndef VERSE_WORLD_PHYSICS_JOBS_H
#define VERSE_WORLD_PHYSICS_JOBS_H

#include <stdbool.h>

#include "universe.h"
#include "world.h"

// Running world simulation off the main thread.
//
// A batch steps one or more worlds concurrently, one task per world. This is safe because a
// world's simulation reads and writes only its own voxel array, so two worlds never touch
// the same memory — but that guarantee is the caller's to keep. Adding the same World twice,
// or adding a world the render thread is meshing, is a data race.
//
// Deliberately excluded: world_update_springs. It keeps a function-local static cooldown
// shared by every world and every thread, so running it concurrently both races on that
// value and lets one world suppress another's springs. Call world_physics_step_springs from
// the owning thread instead until that static becomes per-world state.

typedef struct WorldPhysicsBatch WorldPhysicsBatch;

// delta_seconds drives actor movement; fluid_budget caps how many fluid cells each world may
// process this step, which is how the existing callers bound the cost per frame.
WorldPhysicsBatch *world_physics_batch_begin(float delta_seconds, int fluid_budget);

// Queues one world. Returns false if the batch is full or the world is NULL.
bool world_physics_batch_add(WorldPhysicsBatch *batch, World *world);

int world_physics_batch_pending(WorldPhysicsBatch *batch);
bool world_physics_batch_is_complete(WorldPhysicsBatch *batch);

// Blocks until every world in the batch has been stepped.
void world_physics_batch_wait(WorldPhysicsBatch *batch);

// Waits for outstanding work, then frees the batch.
void world_physics_batch_end(WorldPhysicsBatch *batch);

// Convenience for the common single-world case: step one world and wait for it. Threaded
// when a worker is available, inline otherwise; either way the world is fully stepped when
// this returns, so it is a drop-in for a synchronous physics tick.
void world_physics_step_world(World *world, float delta_seconds, int fluid_budget);

// Steps `center` together with every loaded world in the 3x3x3 universe neighbourhood around
// Steps the centre world on the calling thread and its loaded face neighbours on workers, then
// waits. Wall time is ~max(centre, slowest face). Returns how many worlds were stepped.
// Worlds not placed in the universe are stepped alone.
int world_physics_step_neighbourhood(const Universe *universe, World *center,
                                     float delta_seconds, int fluid_budget);

// Springs, kept on the calling thread for the reason described above.
void world_physics_step_springs(World *world, unsigned long long now_microseconds);

#endif // VERSE_WORLD_PHYSICS_JOBS_H
