#include "fluid_sim.h"

#include "water_erosion.h"
#include "water_table.h"

#include <stdlib.h>
#include <string.h>

// Visits an off-hotspot magma cell needs before it solidifies into basalt. Short enough that
// stranded halo magma sets within a few seconds of realtime stepping, long enough that a pool can
// still creep downhill under viscosity first.
#define FLUID_MAGMA_COOL_TICKS 6

#define FLUID_SPLASH_CAP 256

// One cell taken off the queue for this step. Coordinates are decoded once at drain time so the
// gravity and lateral passes do not each pay a div/mod pair per visit.
typedef struct
{
  uint32_t idx;
  uint16_t x, y, z;
} FluidVisit;

struct FluidSim
{
  // The dimensions the queue was sized for. A world can be cropped underneath us, and a queue
  // indexed for the old size would address the wrong cells rather than merely stale ones.
  uint32_t width, height, depth;
  size_t cell_count;
  size_t word_count;

  // One bit per cell: this cell may still have somewhere to send fluid. Cells enter when they or
  // a neighbour change and leave by reaching equilibrium, so this empties as a world settles.
  uint64_t *queue;
  size_t queued; // set bits, kept alongside so an empty queue is noticed in constant time

  // This step's cells, collected from the queue up front so the passes below iterate a stable
  // snapshot rather than a bitmap they are concurrently modifying.
  FluidVisit *list;
  size_t list_cap;

  // Where the last step stopped draining the queue.
  //
  // A step visits at most max_cells cells, and when more than that are queued the rest wait. Which
  // ones wait cannot be decided by address: scanning the bitmap from the start every time starves
  // the high end of the array forever, and since the array is ordered by z that end is the top of
  // the world. Water at the top of a pool would then never be told to fall, leaving voids under
  // fluid that the vertical rule had already drained. Resuming from here instead gives every
  // queued cell its turn within one sweep of the bitmap.
  size_t scan_word;

  uint64_t step_index;
  bool needs_seed;

  FluidSplash splashes[FLUID_SPLASH_CAP];
  int splash_head;
  int splash_count;
};

// ---------------------------------------------------------------------------
// Queue

static inline void fluid_queue_set(FluidSim *sim, size_t idx)
{
  const size_t word = idx >> 6u;
  const uint64_t bit = 1ULL << (idx & 63u);
  if (!(sim->queue[word] & bit))
  {
    sim->queue[word] |= bit;
    sim->queued++;
  }
}

static inline bool fluid_type_is_sim(VoxelType t)
{
  return t == VOXEL_WATER || t == VOXEL_MAGMA;
}

// Cells that can participate in a transfer: fluid, or air that a neighbour can pour into.
// Solids are never queued — bedrock under a lake was pure wasted work on every transfer.
//
// Air must stay eligible. Lateral spreading only probes +X and +Y from each visited cell; the
// opposite directions are handled by the neighbour looking back. An air cell beside a puddle is
// often that neighbour, and dropping it from the queue leaves a permanent slope on the -X/-Y side.
static inline void fluid_queue_if_active(const World *world, FluidSim *sim, size_t idx)
{
  const VoxelType t = world->voxels[idx].type;
  if (t == VOXEL_AIR || fluid_type_is_sim(t))
    fluid_queue_set(sim, idx);
}

// A cell whose level changed can unblock any of its six face neighbours, and may still have work
// of its own, so all seven go back on the queue (solids excepted).
static void fluid_queue_neighbourhood(const World *world, FluidSim *sim, int x, int y, int z,
                                      size_t idx, size_t plane, int w, int h, int d)
{
  fluid_queue_if_active(world, sim, idx);
  if (z > 0)
    fluid_queue_if_active(world, sim, idx - plane);
  if (z + 1 < d)
    fluid_queue_if_active(world, sim, idx + plane);
  if (x > 0)
    fluid_queue_if_active(world, sim, idx - 1u);
  if (x + 1 < w)
    fluid_queue_if_active(world, sim, idx + 1u);
  if (y > 0)
    fluid_queue_if_active(world, sim, idx - (size_t)w);
  if (y + 1 < h)
    fluid_queue_if_active(world, sim, idx + (size_t)w);
}

// Re-queue the cells a transfer can disturb. The two endpoints share a face, so their
// neighbourhoods overlap; walking the union once avoids a dozen redundant bit tests per move.
static void fluid_queue_after_transfer(const World *world, FluidSim *sim,
                                       int ax, int ay, int az, size_t a_idx,
                                       int bx, int by, int bz, size_t b_idx,
                                       size_t plane, int w, int h, int d)
{
  fluid_queue_if_active(world, sim, a_idx);
  fluid_queue_if_active(world, sim, b_idx);

  if (az > 0)
    fluid_queue_if_active(world, sim, a_idx - plane);
  if (az + 1 < d)
    fluid_queue_if_active(world, sim, a_idx + plane);
  if (bz > 0)
    fluid_queue_if_active(world, sim, b_idx - plane);
  if (bz + 1 < d)
    fluid_queue_if_active(world, sim, b_idx + plane);

  if (ax > 0)
    fluid_queue_if_active(world, sim, a_idx - 1u);
  if (ax + 1 < w)
    fluid_queue_if_active(world, sim, a_idx + 1u);
  if (ay > 0)
    fluid_queue_if_active(world, sim, a_idx - (size_t)w);
  if (ay + 1 < h)
    fluid_queue_if_active(world, sim, a_idx + (size_t)w);
  if (bx > 0)
    fluid_queue_if_active(world, sim, b_idx - 1u);
  if (bx + 1 < w)
    fluid_queue_if_active(world, sim, b_idx + 1u);
  if (by > 0)
    fluid_queue_if_active(world, sim, b_idx - (size_t)w);
  if (by + 1 < h)
    fluid_queue_if_active(world, sim, b_idx + (size_t)w);
}

// ---------------------------------------------------------------------------
// Lifetime

static FluidSim *fluid_sim_get(World *world, bool create)
{
  if (!world || !world->voxels)
    return NULL;

  FluidSim *sim = world->fluid_sim;
  if (sim && (sim->width != world->width || sim->height != world->height ||
              sim->depth != world->depth))
  {
    fluid_sim_destroy(sim);
    world->fluid_sim = NULL;
    sim = NULL;
  }
  if (sim || !create)
    return sim;

  const size_t cells = (size_t)world->width * world->height * world->depth;
  if (cells == 0)
    return NULL;

  sim = (FluidSim *)calloc(1, sizeof(FluidSim));
  if (!sim)
    return NULL;

  sim->width = world->width;
  sim->height = world->height;
  sim->depth = world->depth;
  sim->cell_count = cells;
  sim->word_count = (cells + 63u) / 64u;
  sim->queue = (uint64_t *)calloc(sim->word_count, sizeof(uint64_t));
  if (!sim->queue)
  {
    free(sim);
    return NULL;
  }
  sim->needs_seed = true;
  world->fluid_sim = sim;
  return sim;
}

void fluid_sim_destroy(FluidSim *sim)
{
  if (!sim)
    return;
  free(sim->queue);
  free(sim->list);
  free(sim);
}

void fluid_sim_invalidate(World *world)
{
  FluidSim *sim = world ? world->fluid_sim : NULL;
  if (sim)
    sim->needs_seed = true;
}

void fluid_sim_touch(World *world, int x, int y, int z)
{
  FluidSim *sim = fluid_sim_get(world, true);
  if (!sim)
    return;
  if (x < 0 || y < 0 || z < 0 || x >= (int)world->width || y >= (int)world->height ||
      z >= (int)world->depth)
    return;
  const size_t plane = (size_t)world->width * world->height;
  const size_t idx = ((size_t)z * (size_t)world->height + (size_t)y) * (size_t)world->width +
                     (size_t)x;
  fluid_queue_neighbourhood(world, sim, x, y, z, idx, plane, (int)world->width,
                            (int)world->height, (int)world->depth);
}

// Queue every fluid cell in the world. The fallback for callers that changed the voxel array
// without saying where; costs one linear pass, after which the queue is maintained incrementally.
static void fluid_seed(const World *world, FluidSim *sim)
{
  memset(sim->queue, 0, sim->word_count * sizeof(uint64_t));
  sim->queued = 0;

  const Voxel *voxels = world->voxels;
  for (size_t i = 0; i < sim->cell_count; i++)
  {
    if (fluid_type_is_sim(voxels[i].type))
      fluid_queue_set(sim, i);
  }
  sim->needs_seed = false;
}

// ---------------------------------------------------------------------------
// Cell mechanics

// How much of a vertical pair's total belongs in the lower cell.
//
// Below a full cube the answer is all of it: fluid falls. Above that the lower cell keeps a little
// more than full and hands the rest up, which is the whole of the pressure model — the surplus a
// submerged cell carries is exactly what pushes fluid up the far side of a U-bend, and it settles
// into a gradient of one level per cell of depth.
static inline int fluid_stable_lower(int total)
{
  if (total <= FLUID_LEVEL_FULL)
    return total;
  // FLUID_PRESSURE_PER_CELL is 1 and FLUID_LEVEL_FULL is 128: the mid branch is (16384 + total)/129.
  if (total < 2 * FLUID_LEVEL_FULL + FLUID_PRESSURE_PER_CELL)
    return (FLUID_LEVEL_FULL * FLUID_LEVEL_FULL + total * FLUID_PRESSURE_PER_CELL) /
           (FLUID_LEVEL_FULL + FLUID_PRESSURE_PER_CELL);
  return (total + FLUID_PRESSURE_PER_CELL) / 2;
}

static void fluid_record_splash(FluidSim *sim, const World *world, int x, int y, int z,
                                int dx, int dy, int dz, int units)
{
  // Only a surface disturbs visibly, and the buffer is small enough that filling it with arrivals
  // nothing can see would push out the ones that matter.
  if (z + 1 < (int)world->depth)
  {
    const Voxel *above = &world->voxels[((size_t)(z + 1) * (size_t)world->height + (size_t)y) *
                                            (size_t)world->width +
                                        (size_t)x];
    if (above->type != VOXEL_AIR)
      return;
  }

  int slot;
  if (sim->splash_count < FLUID_SPLASH_CAP)
  {
    slot = (sim->splash_head + sim->splash_count) % FLUID_SPLASH_CAP;
    sim->splash_count++;
  }
  else
  {
    // Full: drop the oldest. A consumer that has fallen this far behind wants the recent ones.
    slot = sim->splash_head;
    sim->splash_head = (sim->splash_head + 1) % FLUID_SPLASH_CAP;
  }

  sim->splashes[slot] = (FluidSplash){.x = (uint16_t)x,
                                      .y = (uint16_t)y,
                                      .z = (uint16_t)z,
                                      .from_dx = (int8_t)dx,
                                      .from_dy = (int8_t)dy,
                                      .from_dz = (int8_t)dz,
                                      .units = (uint8_t)(units > 255 ? 255 : units)};
}

int fluid_sim_drain_splashes(World *world, FluidSplash *out, int max)
{
  FluidSim *sim = world ? world->fluid_sim : NULL;
  if (!sim || !out || max <= 0)
    return 0;

  int written = 0;
  while (written < max && sim->splash_count > 0)
  {
    out[written++] = sim->splashes[sim->splash_head];
    sim->splash_head = (sim->splash_head + 1) % FLUID_SPLASH_CAP;
    sim->splash_count--;
  }
  return written;
}

// Write a cell's level, promoting air to fluid and demoting empty fluid back to air. The type
// change has to be announced because this writes the voxel array directly, and the occupancy
// bitfield the renderer culls against is derived from it.
static void fluid_write_level(World *world, size_t idx, int x, int y, int z, VoxelType type,
                              int level)
{
  Voxel *v = &world->voxels[idx];
  if (level <= 0)
  {
    if (v->type != VOXEL_AIR)
    {
      v->type = VOXEL_AIR;
      voxel_set_quantity(v, 0);
      world_voxel_type_written(world, x, y, z, VOXEL_AIR);
    }
    return;
  }
  if (level > FLUID_LEVEL_MAX)
    level = FLUID_LEVEL_MAX;
  if (v->type != type)
  {
    v->type = type;
    world_voxel_type_written(world, x, y, z, type);
  }
  voxel_set_quantity(v, (uint8_t)level);
}

typedef struct
{
  World *world;
  FluidSim *sim;
  int w, h, d;
  size_t plane; // cells per z layer
  int transfers;
  long long moved;
} FluidPass;

// Move `amount` levels from the cell at (ax,ay,az) to the one at (bx,by,bz). The only place either
// cell's level is written, so conservation is a property of the code rather than of each caller.
static void fluid_transfer(FluidPass *p, VoxelType type,
                           size_t a_idx, int ax, int ay, int az, int a_level,
                           size_t b_idx, int bx, int by, int bz, int b_level,
                           int amount)
{
  if (amount <= 0)
    return;
  if (amount > a_level)
    amount = a_level;
  if (b_level + amount > FLUID_LEVEL_MAX)
    amount = FLUID_LEVEL_MAX - b_level;
  if (amount <= 0)
    return;

  fluid_write_level(p->world, a_idx, ax, ay, az, type, a_level - amount);
  fluid_write_level(p->world, b_idx, bx, by, bz, type, b_level + amount);

  fluid_queue_after_transfer(p->world, p->sim, ax, ay, az, a_idx, bx, by, bz, b_idx, p->plane, p->w,
                             p->h, p->d);
  fluid_record_splash(p->sim, p->world, bx, by, bz, bx - ax, by - ay, bz - az, amount);

  // Flowing water scours the bed under its path (10–60 min per voxel at realtime scale).
  if (type == VOXEL_WATER)
  {
    water_erosion_on_water_transfer(p->world, ax, ay, az, bx, by, bz, amount, p->sim->step_index);
    water_table_on_water_transfer(p->world, ax, ay, az, bx, by, bz, amount);
  }

  p->transfers++;
  p->moved += amount;
}

// Magma creeps; water pours. Dividing the transfer rather than skipping steps keeps the flow
// smooth instead of stuttering, and the floor of one level stops it setting solid.
static inline int fluid_rate_limit(VoxelType type, int amount)
{
  if (type != VOXEL_MAGMA || amount <= 0)
    return amount;
  const int limited = amount / FLUID_MAGMA_VISCOSITY;
  return limited > 0 ? limited : 1;
}

// True when a cell can hold this fluid: empty space, or more of the same.
static inline bool fluid_can_enter(const Voxel *v, VoxelType type)
{
  return v && (v->type == VOXEL_AIR || v->type == type);
}

// Settle a cell against the one below it, in whichever direction the pair is out of balance:
// downwards when the lower cell has room, upwards when it is carrying more than its share and the
// surplus has nowhere else to go.
static void fluid_settle_vertical(FluidPass *p, size_t idx, int x, int y, int z)
{
  if (z <= 0)
    return;

  Voxel *v = &p->world->voxels[idx];
  const int level = fluid_level_of(v);
  if (level <= 0)
    return;

  const size_t below_idx = idx - p->plane;
  Voxel *below = &p->world->voxels[below_idx];
  if (!fluid_can_enter(below, v->type))
    return;

  const int below_level = fluid_level_of(below);
  const int want_below = fluid_stable_lower(level + below_level);
  const int delta = want_below - below_level;

  if (delta > 0)
    fluid_transfer(p, v->type, idx, x, y, z, level, below_idx, x, y, z - 1, below_level,
                   fluid_rate_limit(v->type, delta));
  else if (delta < 0)
    fluid_transfer(p, below->type, below_idx, x, y, z - 1, below_level, idx, x, y, z, level,
                   fluid_rate_limit(below->type, -delta));
}

// Level a cell against one lateral neighbour. Half the difference moves, which brings the pair to
// equal in a single visit and leaves nothing to move once they are within one level of each other
// — that last part is what lets the queue drain instead of jittering forever.
static void fluid_level_lateral(FluidPass *p, size_t idx, int x, int y, int z, int dx, int dy)
{
  const int nx = x + dx, ny = y + dy;
  if (nx < 0 || ny < 0 || nx >= p->w || ny >= p->h)
    return;

  Voxel *v = &p->world->voxels[idx];
  const int level = fluid_level_of(v);
  const size_t n_idx = idx + (size_t)dy * (size_t)p->w + (size_t)dx;
  Voxel *n = &p->world->voxels[n_idx];
  const int n_level = fluid_level_of(n);

  // One of the pair must be fluid to have anything to share, and the other must be able to take
  // it. Reading both levels first means the same code handles either side being the higher.
  if (level >= n_level)
  {
    if (level - n_level < 2 || !fluid_can_enter(n, v->type))
      return;
    fluid_transfer(p, v->type, idx, x, y, z, level, n_idx, nx, ny, z, n_level,
                   fluid_rate_limit(v->type, (level - n_level) / 2));
  }
  else
  {
    if (n_level - level < 2 || !fluid_can_enter(v, n->type))
      return;
    fluid_transfer(p, n->type, n_idx, nx, ny, z, n_level, idx, x, y, z, level,
                   fluid_rate_limit(n->type, (n_level - level) / 2));
  }
}

// Magma warms what it touches. Radiated on visit rather than by sweeping the world for magma every
// step: heat only climbs and saturates at 6, nothing reads it back down, and a full-volume sweep
// to keep incrementing a saturated counter was most of what the old fluid step cost.
static void fluid_radiate_heat(const FluidPass *p, size_t idx, int x, int y, int z)
{
  Voxel *voxels = p->world->voxels;
  const size_t plane = p->plane;
  const int w = p->w;

  if (x + 1 < p->w)
  {
    const uint8_t heat = voxel_get_heat(&voxels[idx + 1u]);
    if (heat < 6)
      voxel_set_heat(&voxels[idx + 1u], (uint8_t)(heat + 1));
  }
  if (x > 0)
  {
    const uint8_t heat = voxel_get_heat(&voxels[idx - 1u]);
    if (heat < 6)
      voxel_set_heat(&voxels[idx - 1u], (uint8_t)(heat + 1));
  }
  if (y + 1 < p->h)
  {
    const uint8_t heat = voxel_get_heat(&voxels[idx + (size_t)w]);
    if (heat < 6)
      voxel_set_heat(&voxels[idx + (size_t)w], (uint8_t)(heat + 1));
  }
  if (y > 0)
  {
    const uint8_t heat = voxel_get_heat(&voxels[idx - (size_t)w]);
    if (heat < 6)
      voxel_set_heat(&voxels[idx - (size_t)w], (uint8_t)(heat + 1));
  }
  if (z + 1 < p->d)
  {
    const uint8_t heat = voxel_get_heat(&voxels[idx + plane]);
    if (heat < 6)
      voxel_set_heat(&voxels[idx + plane], (uint8_t)(heat + 1));
  }
  if (z > 0)
  {
    const uint8_t heat = voxel_get_heat(&voxels[idx - plane]);
    if (heat < 6)
      voxel_set_heat(&voxels[idx - plane], (uint8_t)(heat + 1));
  }
}

// Bedrock hotspots keep a column molten; everywhere else magma is a viscous fluid that cools to
// basalt. Heat on the magma cell itself is the remaining molten budget (neighbours use the same
// field for radiated warmth — that is fine: vents refresh to 6, and off-vent magma only counts
// down its own cell).
static void fluid_magma_sustain_or_cool(FluidPass *p, size_t idx, int x, int y, int z)
{
  Voxel *v = &p->world->voxels[idx];
  if (v->type != VOXEL_MAGMA)
    return;

  if (world_magma_hotspot_at(p->world, x, y))
  {
    voxel_set_heat(v, 6);
    return;
  }

  uint8_t heat = voxel_get_heat(v);
  if (heat == 0)
    heat = FLUID_MAGMA_COOL_TICKS; // freshly placed / never visited: start the cool clock

  if (heat <= 1)
  {
    // Set to basalt — the cooled companion already used by wilderness gen around magma pools.
    v->type = VOXEL_STONE_BASALT;
    voxel_set_quantity(v, 0);
    voxel_set_heat(v, 0);
    world_voxel_type_written(p->world, x, y, z, VOXEL_STONE_BASALT);
    fluid_queue_neighbourhood(p->world, p->sim, x, y, z, idx, p->plane, p->w, p->h, p->d);
    return;
  }

  voxel_set_heat(v, (uint8_t)(heat - 1));
  // Keep cooling even after flow has settled, or stranded halo magma would freeze mid-clock.
  fluid_queue_set(p->sim, idx);
}

// ---------------------------------------------------------------------------
// Step

FluidStepStats fluid_sim_step(World *world, int max_cells)
{
  FluidStepStats stats = {0, 0, 0, 0, true};
  if (!world || !world->voxels || max_cells <= 0)
    return stats;

  // A world that has never held fluid should not pay for a queue, and asking is one scan that the
  // world then caches.
  if (!world->fluid_sim && world_fluid_presence(world) == WORLD_FLUID_NONE)
    return stats;

  FluidSim *sim = fluid_sim_get(world, true);
  if (!sim)
    return stats;

  if (sim->needs_seed)
    fluid_seed(world, sim);

  if (sim->queued == 0)
    return stats;

  // Collect this step's cells up front. The passes below move fluid, which re-queues cells; taking
  // a snapshot first keeps each cell to one visit per step and keeps the result independent of the
  // order the queue happened to be modified in.
  const size_t want = sim->queued < (size_t)max_cells ? sim->queued : (size_t)max_cells;
  if (sim->list_cap < want)
  {
    FluidVisit *grown = (FluidVisit *)realloc(sim->list, want * sizeof(FluidVisit));
    if (!grown)
      return stats;
    sim->list = grown;
    sim->list_cap = want;
  }

  const size_t plane = (size_t)world->width * world->height;
  const int w = (int)world->width;
  const int h = (int)world->height;

  // Drain from where the last step left off, wrapping once. `split` marks where the wrap happened:
  // the tail of the list holds the lower addresses, so [split, count) followed by [0, split) is the
  // whole list in ascending order, which is what the gravity pass below needs to cascade a column.
  size_t count = 0;
  size_t split = 0;
  size_t word = sim->scan_word < sim->word_count ? sim->scan_word : 0;
  for (size_t scanned = 0; scanned < sim->word_count && count < want; scanned++)
  {
    uint64_t bits = sim->queue[word];
    while (bits && count < want)
    {
      const int bit = __builtin_ctzll(bits);
      bits &= bits - 1;
      sim->queue[word] &= ~(1ULL << bit);
      sim->queued--;

      const size_t idx = word * 64u + (size_t)bit;
      FluidVisit *v = &sim->list[count++];
      v->idx = (uint32_t)idx;
      if (idx < sim->cell_count)
      {
        const int z = (int)(idx / plane);
        const size_t rem = idx - (size_t)z * plane;
        const int y = (int)(rem / (size_t)w);
        const int x = (int)(rem - (size_t)y * (size_t)w);
        v->x = (uint16_t)x;
        v->y = (uint16_t)y;
        v->z = (uint16_t)z;
      }
      else
      {
        v->x = v->y = v->z = 0;
      }
    }
    // Stop on the word itself rather than after it when the budget ran out mid-word, so the
    // remainder of that word is taken first next time.
    if (count >= want && bits)
      break;
    if (++word == sim->word_count)
    {
      word = 0;
      split = count;
    }
  }
  sim->scan_word = word;

  FluidPass pass = {.world = world,
                    .sim = sim,
                    .w = w,
                    .h = h,
                    .d = (int)world->depth,
                    .plane = plane,
                    .transfers = 0,
                    .moved = 0};

  // Gravity first, so a cell with somewhere to fall empties downwards before it is asked to
  // spread. Ascending order cascades a whole column in this one pass.
  for (size_t n = 0; n < count; n++)
  {
    const size_t i = (n < count - split) ? split + n : n - (count - split);
    const FluidVisit *cell = &sim->list[i];
    const size_t idx = cell->idx;
    if (idx >= sim->cell_count)
      continue;
    const int x = (int)cell->x, y = (int)cell->y, z = (int)cell->z;

    if (world->voxels[idx].type == VOXEL_MAGMA)
    {
      fluid_radiate_heat(&pass, idx, x, y, z);
      fluid_magma_sustain_or_cool(&pass, idx, x, y, z);
      // Solidified this visit — do not try to settle basalt as a fluid.
      if (world->voxels[idx].type != VOXEL_MAGMA)
        continue;
    }
    fluid_settle_vertical(&pass, idx, x, y, z);
  }

  // Then spreading. Levelling a pair propagates along the direction of travel within a single
  // pass, so the direction is reversed on alternate steps and the axis order rotated, or water
  // would visibly prefer running one way.
  const bool reverse = (sim->step_index & 1u) != 0;
  const bool y_first = (sim->step_index & 2u) != 0;
  for (size_t n = 0; n < count; n++)
  {
    const size_t i = reverse ? count - 1 - n : n;
    const FluidVisit *cell = &sim->list[i];
    const size_t idx = cell->idx;
    if (idx >= sim->cell_count)
      continue;

    const VoxelType type = world->voxels[idx].type;
    // Solids never move fluid; air stays so a lone queued void can pull from a neighbour.
    if (type != VOXEL_AIR && !fluid_type_is_sim(type))
      continue;

    const int x = (int)cell->x, y = (int)cell->y, z = (int)cell->z;

    // Fluid in mid-air is falling, not spreading. Without this a stream pouring off a ledge would
    // smear sideways on the way down instead of dropping.
    if (fluid_type_is_sim(type) && z > 0 && world->voxels[idx - plane].type == VOXEL_AIR)
      continue;

    if (y_first)
    {
      fluid_level_lateral(&pass, idx, x, y, z, 0, 1);
      fluid_level_lateral(&pass, idx, x, y, z, 1, 0);
    }
    else
    {
      fluid_level_lateral(&pass, idx, x, y, z, 1, 0);
      fluid_level_lateral(&pass, idx, x, y, z, 0, 1);
    }
  }

  sim->step_index++;

  stats.cells_visited = (int)count;
  stats.transfers = pass.transfers;
  stats.moved = pass.moved;
  stats.queued = (int)sim->queued;
  stats.at_rest = (pass.transfers == 0 && sim->queued == 0);
  return stats;
}

long long fluid_sim_total_level(const World *world)
{
  if (!world || !world->voxels)
    return 0;
  const size_t cells = (size_t)world->width * world->height * world->depth;
  long long total = 0;
  for (size_t i = 0; i < cells; i++)
    total += fluid_level_of(&world->voxels[i]);
  return total;
}
