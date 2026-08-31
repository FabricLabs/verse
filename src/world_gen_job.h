#ifndef VERSE_WORLD_GEN_JOB_H
#define VERSE_WORLD_GEN_JOB_H

#include <stdbool.h>
#include <stddef.h>

#include "world.h"
#include "universe.h"

// Background world generation.
//
// Generation is seconds of pure computation per world, so it runs on a worker thread while
// the main thread keeps drawing the loading screen. The division of labour is strict:
//
//   worker thread  - world_create, world_generate_with_type, universe_place. Writes only
//                    into the GameWorlds and Universe handed to it, which nothing else may
//                    touch until the job reports complete.
//   main thread    - reads progress through world_gen_job_snapshot, and does everything
//                    that touches player or render state once the job is complete.
//
// The loading screen does not render any of the worlds being generated, which is what makes
// this safe. If that changes, this arrangement needs revisiting.

typedef struct
{
  GameWorlds *worlds; // populated by the worker; must already exist
  Universe *universe; // worlds are placed into it by the worker
  bool home_only;     // home + clouds + wilderness rings under the island (see world_gen_job.c)
} WorldGenRequest;

typedef struct WorldGenJob WorldGenJob;

// How many worlds the request will generate, for the loading bar's denominator.
int world_gen_job_total_steps(const WorldGenRequest *request);

// Starts generation. Returns NULL if the request is malformed or allocation fails. When the
// task scheduler is not running the generation happens inline, so the returned job is
// already complete — callers do not need a separate path for that.
WorldGenJob *world_gen_job_start(const WorldGenRequest *request);

// Copies the worker's current progress out under lock. message may be NULL. Returns true
// once the worker has finished, at which point the caller owns the generated worlds again.
bool world_gen_job_snapshot(WorldGenJob *job, int *out_progress, char *out_message,
                            size_t message_size);

bool world_gen_job_is_complete(WorldGenJob *job);

// The world the player should start in, valid only once the job is complete.
World *world_gen_job_primary_world(WorldGenJob *job);

// Waits for the worker if it is still running, then frees the job. Does not free the
// generated worlds, which belong to the caller's GameWorlds.
void world_gen_job_destroy(WorldGenJob *job);

// New-game (home_only) startup now includes the wilderness plane under the island: the landing cell
// plus two Chebyshev rings (5x5 at universe z=0), generated on the loading screen alongside home and
// the cloud layers. Those worlds are drawn as distance terrain from the sky island.
//
// game_state_pump_world_streaming still fills other layers and cells beyond that plane on demand.

// Background generation of a single universe cell, for streaming worlds in around a moving player.
//
// game_state_ensure_universe_cell does the same work synchronously on the main thread, which the
// benchmark measures at around 100ms for a home island — most of a second's worth of stall if
// several cells are needed at once. This exists so a caller can ask for one and keep drawing.
//
// The generated world is deliberately *not* placed into the universe by the worker. universe_place
// can rehash the whole map, and the render path calls universe_get on it every frame; the caller
// takes the finished world on the main thread and places it there.
typedef struct CellGenJob CellGenJob;

// base_seed is the universe's seed, not the cell's: the job derives a per-coordinate seed from it and
// tells the world which cell it occupies before generating, so neighbouring cells come out as
// different worlds that still line up along their shared boundary.
CellGenJob *cell_gen_job_start(const char *base_seed, uint64_t ux, uint64_t uy, uint64_t uz);
bool cell_gen_job_is_complete(const CellGenJob *job);

// Which cell this job was started for, so the caller does not have to remember.
void cell_gen_job_cell(const CellGenJob *job, uint64_t *ux, uint64_t *uy, uint64_t *uz);

// Transfers ownership of the finished world to the caller. NULL when the job has not finished or
// generation failed. Calling it twice returns NULL the second time.
World *cell_gen_job_take_world(CellGenJob *job);

void cell_gen_job_destroy(CellGenJob *job);

#endif // VERSE_WORLD_GEN_JOB_H
