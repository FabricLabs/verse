#include "fluid_surface.h"

#include <stdlib.h>
#include <string.h>

// Propagation speed, as c^2 in 8-bit fixed point. The explicit update below is only stable while
// c^2 stays under 0.5 for a five-point Laplacian; a quarter leaves headroom and still crosses a
// patch in under half a second at FLUID_SURFACE_HZ.
#define FLUID_WAVE_C2_Q8 64

// A ripple loses this fraction of its momentum per step, and its displacement this fraction of
// what remains. The first sets how long a disturbance rings, the second guarantees the surface
// returns to flat rather than settling at some offset that integer truncation left behind.
//
// The momentum figure has to be read against the propagation speed to mean anything: a ripple
// travels half a sub-voxel per step, so it needs about sixty steps to cross the voxel it started
// in. Losing a 64th of its momentum per step put its life and its journey at the same length, and a
// disturbance that dies as it arrives at the edge is one that is never seen to move. A 256th gives
// it four seconds, which is a few voxels of travel.
#define FLUID_WAVE_MOMENTUM_LOSS 256
#define FLUID_WAVE_HEIGHT_LOSS 512

// Furthest a column may be pushed from flat, in sub-voxels. Past a few sub-voxels the surface
// stops reading as water and starts reading as broken geometry.
#define FLUID_WAVE_MAX_SUBVOXELS 6

// Displacement per level of arriving fluid. A level is a 128th of a cube, so an eighth of a
// sub-voxel of volume, but a drop landing on water dents it far deeper than its own volume
// because the dent is paid back by the ring around it rather than by the water below.
#define FLUID_SPLASH_PER_LEVEL (FLUID_SURFACE_UNIT / 2)

// Inner radius of the crater. The outer one is FLUID_SPLASH_RIM, in the header, because how far a
// splash reaches is something a caller can reasonably want to know.
#define FLUID_SPLASH_DISH 3

#define FLUID_SURFACE_EMPTY (-1)
#define FLUID_SURFACE_TOMBSTONE (-2)

// Below this displacement a patch counts as flat and is retired.
//
// Still water is the common case by a wide margin — a lake is disturbed where something falls into
// it and level everywhere else — and a flat patch produces a tile indistinguishable from the plain
// water face. Keeping those patches meant stepping the wave equation and baking a tile for every
// water face on screen whether or not anything was happening on it, which cost a third of a frame
// to draw water that was not moving. So the cache holds disturbed surfaces only: what it can hold
// bounds how many ripples can be in flight, not how much water can be in view.
//
// A sixteenth of a sub-voxel, which is below what the shading in the bake can show.
#define FLUID_WAVE_FLAT (FLUID_SURFACE_UNIT / 16)

typedef struct
{
  const World *world;
  uint16_t x, y, z;
  bool used;
  uint64_t last_touch; // step count when last acquired; the retirement order
  int peak;            // largest displacement in the patch, from the last step
  FluidSurfacePatch patch;
} FluidSurfaceSlot;

struct FluidSurface
{
  FluidSurfaceSlot *slots;
  int capacity;
  int live;

  // Open addressing over slot indices. Small enough that the probe never leaves cache, and keyed
  // on the voxel a patch belongs to so a redraw finds the same surface it disturbed.
  int *table;
  int table_size;
  int table_used; // live entries plus tombstones, for deciding when to rebuild

  int parity;   // which of the two height buffers is current
  float accum;  // leftover frame time, so the wave runs at a fixed rate
  uint64_t clock;
  uint32_t splash_seq;
};

// ---------------------------------------------------------------------------
// Key lookup

static inline uint32_t fluid_surface_hash(const World *world, int x, int y, int z)
{
  uint64_t h = (uint64_t)(uintptr_t)world;
  h ^= (uint64_t)x * 0x9E3779B97F4A7C15ULL;
  h ^= (uint64_t)y * 0xC2B2AE3D27D4EB4FULL;
  h ^= (uint64_t)z * 0x165667B19E3779F9ULL;
  h ^= h >> 29;
  h *= 0xBF58476D1CE4E5B9ULL;
  h ^= h >> 32;
  return (uint32_t)h;
}

static inline bool fluid_surface_slot_matches(const FluidSurfaceSlot *slot, const World *world,
                                              int x, int y, int z)
{
  return slot->used && slot->world == world && slot->x == (uint16_t)x && slot->y == (uint16_t)y &&
         slot->z == (uint16_t)z;
}

static int fluid_surface_find(const FluidSurface *s, const World *world, int x, int y, int z)
{
  const uint32_t mask = (uint32_t)s->table_size - 1u;
  uint32_t i = fluid_surface_hash(world, x, y, z) & mask;
  for (int probe = 0; probe < s->table_size; probe++)
  {
    const int entry = s->table[i];
    if (entry == FLUID_SURFACE_EMPTY)
      return -1;
    if (entry >= 0 && fluid_surface_slot_matches(&s->slots[entry], world, x, y, z))
      return entry;
    i = (i + 1u) & mask;
  }
  return -1;
}

static void fluid_surface_table_insert(FluidSurface *s, int slot_index)
{
  const FluidSurfaceSlot *slot = &s->slots[slot_index];
  const uint32_t mask = (uint32_t)s->table_size - 1u;
  uint32_t i = fluid_surface_hash(slot->world, slot->x, slot->y, slot->z) & mask;
  for (int probe = 0; probe < s->table_size; probe++)
  {
    if (s->table[i] < 0)
    {
      if (s->table[i] == FLUID_SURFACE_EMPTY)
        s->table_used++;
      s->table[i] = slot_index;
      return;
    }
    i = (i + 1u) & mask;
  }
}

// Drop the tombstones a run of evictions leaves behind. Cheap at these sizes and simpler than
// getting incremental compaction right.
static void fluid_surface_table_rebuild(FluidSurface *s)
{
  for (int i = 0; i < s->table_size; i++)
    s->table[i] = FLUID_SURFACE_EMPTY;
  s->table_used = 0;
  for (int i = 0; i < s->capacity; i++)
    if (s->slots[i].used)
      fluid_surface_table_insert(s, i);
}

static void fluid_surface_table_remove(FluidSurface *s, const World *world, int x, int y, int z)
{
  const uint32_t mask = (uint32_t)s->table_size - 1u;
  uint32_t i = fluid_surface_hash(world, x, y, z) & mask;
  for (int probe = 0; probe < s->table_size; probe++)
  {
    const int entry = s->table[i];
    if (entry == FLUID_SURFACE_EMPTY)
      return;
    if (entry >= 0 && fluid_surface_slot_matches(&s->slots[entry], world, x, y, z))
    {
      s->table[i] = FLUID_SURFACE_TOMBSTONE;
      return;
    }
    i = (i + 1u) & mask;
  }
}

// ---------------------------------------------------------------------------
// Lifetime

FluidSurface *fluid_surface_create(int max_patches)
{
  if (max_patches <= 0)
    return NULL;

  FluidSurface *s = (FluidSurface *)calloc(1, sizeof(FluidSurface));
  if (!s)
    return NULL;

  s->capacity = max_patches;
  s->slots = (FluidSurfaceSlot *)calloc((size_t)max_patches, sizeof(FluidSurfaceSlot));

  int table_size = 1;
  while (table_size < max_patches * 2)
    table_size <<= 1;
  s->table_size = table_size;
  s->table = (int *)malloc((size_t)table_size * sizeof(int));

  if (!s->slots || !s->table)
  {
    fluid_surface_destroy(s);
    return NULL;
  }
  for (int i = 0; i < table_size; i++)
    s->table[i] = FLUID_SURFACE_EMPTY;
  return s;
}

void fluid_surface_destroy(FluidSurface *surface)
{
  if (!surface)
    return;
  free(surface->slots);
  free(surface->table);
  free(surface);
}

int fluid_surface_live_count(const FluidSurface *surface)
{
  return surface ? surface->live : 0;
}

static void fluid_surface_retire(FluidSurface *s, int slot_index)
{
  FluidSurfaceSlot *slot = &s->slots[slot_index];
  if (!slot->used)
    return;
  fluid_surface_table_remove(s, slot->world, slot->x, slot->y, slot->z);
  slot->used = false;
  slot->world = NULL;
  s->live--;
}

// A free slot, or the one that has gone longest without being asked for.
static int fluid_surface_claim(FluidSurface *s)
{
  for (int i = 0; i < s->capacity; i++)
    if (!s->slots[i].used)
      return i;

  int oldest = 0;
  for (int i = 1; i < s->capacity; i++)
    if (s->slots[i].last_touch < s->slots[oldest].last_touch)
      oldest = i;
  fluid_surface_retire(s, oldest);
  return oldest;
}

static int fluid_surface_slot_for(FluidSurface *s, const World *world, int x, int y, int z,
                                 bool create)
{
  // Every request advances the clock, so last_touch orders slots by how recently they were asked
  // for. Retirement then falls on whatever nothing has looked at for the longest.
  s->clock++;

  int found = fluid_surface_find(s, world, x, y, z);
  if (found >= 0)
  {
    s->slots[found].last_touch = s->clock;
    return found;
  }
  if (!create)
    return -1;

  const int index = fluid_surface_claim(s);
  FluidSurfaceSlot *slot = &s->slots[index];
  memset(&slot->patch, 0, sizeof(slot->patch));
  slot->world = world;
  slot->x = (uint16_t)x;
  slot->y = (uint16_t)y;
  slot->z = (uint16_t)z;
  slot->used = true;
  slot->last_touch = s->clock;
  s->live++;

  if (s->table_used * 4 >= s->table_size * 3)
    fluid_surface_table_rebuild(s);
  else
    fluid_surface_table_insert(s, index);
  return index;
}

const int16_t *fluid_surface_heights(FluidSurface *surface, const World *world, int x, int y, int z)
{
  if (!surface || !world)
    return NULL;
  const int index = fluid_surface_find(surface, world, x, y, z);
  if (index < 0)
    return NULL;
  return surface->slots[index].patch.height[surface->parity];
}

// ---------------------------------------------------------------------------
// Splashes

// Raise or lower one sub-voxel column, returning how much of `delta` was actually applied: nothing
// for a column outside the patch, and less than asked for where the clamp bites. Callers that need
// to displace a fixed volume have to know what got through, or the patch gains or loses height.
static inline int fluid_surface_add(int16_t *cur, int i, int j, int delta)
{
  if (i < 0 || j < 0 || i >= FLUID_SURFACE_DIM || j >= FLUID_SURFACE_DIM)
    return 0;
  const int limit = FLUID_WAVE_MAX_SUBVOXELS * FLUID_SURFACE_UNIT;
  const int before = (int)cur[j * FLUID_SURFACE_DIM + i];
  int v = before + delta;
  if (v > limit)
    v = limit;
  if (v < -limit)
    v = -limit;
  cur[j * FLUID_SURFACE_DIM + i] = (int16_t)v;
  return v - before;
}

void fluid_surface_splash(FluidSurface *surface, const World *world, const FluidSplash *splash)
{
  if (!surface || !world || !splash || splash->units == 0)
    return;

  // Nothing to disturb if the cell is not exposed water any more; the arrival may have been
  // recorded a step before the cell drained or got covered.
  const Voxel *v = world_pos_in_bounds_fast(world, splash->x, splash->y, splash->z)
                       ? world_voxel_cptr_fast(world, splash->x, splash->y, splash->z)
                       : NULL;
  if (!v || v->type != VOXEL_WATER)
    return;

  // An arrival always gets a surface, taking the least recently disturbed one if none are spare.
  // Since a patch now lives only as long as its ripples do, a full cache means more ripples are in
  // flight at once than it can hold, and the right one to give up is the one that has been ringing
  // longest — not the one that is about to start.
  const int index = fluid_surface_slot_for(surface, world, splash->x, splash->y, splash->z, true);
  if (index < 0)
    return;

  // Where it landed. Fluid arriving across a face enters at that face; fluid falling from above
  // lands somewhere in the middle, scattered so a steady drip does not drill the same column.
  uint32_t noise = fluid_surface_hash(world, splash->x, splash->y, splash->z) + surface->splash_seq++;
  noise ^= noise >> 13;
  int i = (int)(noise % FLUID_SURFACE_DIM);
  int j = (int)((noise >> 8) % FLUID_SURFACE_DIM);
  if (splash->from_dx > 0)
    i = 0;
  else if (splash->from_dx < 0)
    i = FLUID_SURFACE_DIM - 1;
  else if (splash->from_dy > 0)
    j = 0;
  else if (splash->from_dy < 0)
    j = FLUID_SURFACE_DIM - 1;

  int amplitude = (int)splash->units * FLUID_SPLASH_PER_LEVEL;
  const int limit = FLUID_WAVE_MAX_SUBVOXELS * FLUID_SURFACE_UNIT;
  if (amplitude > limit)
    amplitude = limit;

  int16_t *cur = surface->slots[index].patch.height[surface->parity];
  const int dish2 = FLUID_SPLASH_DISH * FLUID_SPLASH_DISH;
  const int rim2 = FLUID_SPLASH_RIM * FLUID_SPLASH_RIM;

  // The dish, `amplitude` deep in the middle and shallowing to nothing at its edge. Its depth is
  // what the arrival is worth; the rim below is whatever balances it.
  int removed = 0;
  for (int dj = -FLUID_SPLASH_DISH; dj <= FLUID_SPLASH_DISH; dj++)
    for (int di = -FLUID_SPLASH_DISH; di <= FLUID_SPLASH_DISH; di++)
    {
      const int d2 = di * di + dj * dj;
      if (d2 > dish2)
        continue;
      const int depth = (amplitude * (dish2 - d2)) / dish2;
      removed -= fluid_surface_add(cur, i + di, j + dj, -depth);
    }
  if (removed <= 0)
    return;

  // Zero sum: the rim rises by exactly the volume the dish lost, so the patch's mean height stays
  // where the voxel's level put it and only the shape of the surface changes.
  //
  // The share is recomputed from what is still owed at every column rather than divided once, which
  // makes the total exact however much the patch edges and the clamp took. That matters on an
  // arrival across a face, where the crater is centred on the edge of the patch and half of it
  // falls in the neighbouring voxel's, which is not this one's to write.
  int rim_cells = 0;
  for (int dj = -FLUID_SPLASH_RIM; dj <= FLUID_SPLASH_RIM; dj++)
    for (int di = -FLUID_SPLASH_RIM; di <= FLUID_SPLASH_RIM; di++)
    {
      const int d2 = di * di + dj * dj;
      if (d2 > dish2 && d2 <= rim2 && i + di >= 0 && j + dj >= 0 &&
          i + di < FLUID_SURFACE_DIM && j + dj < FLUID_SURFACE_DIM)
        rim_cells++;
    }

  int owed = removed;
  int left = rim_cells;
  for (int dj = -FLUID_SPLASH_RIM; dj <= FLUID_SPLASH_RIM && left > 0; dj++)
    for (int di = -FLUID_SPLASH_RIM; di <= FLUID_SPLASH_RIM && left > 0; di++)
    {
      const int d2 = di * di + dj * dj;
      if (d2 <= dish2 || d2 > rim2)
        continue;
      if (i + di < 0 || j + dj < 0 || i + di >= FLUID_SURFACE_DIM || j + dj >= FLUID_SURFACE_DIM)
        continue;
      owed -= fluid_surface_add(cur, i + di, j + dj, owed / left);
      left--;
    }

  // Anything the rim could not take goes back into the dish, so the patch does not gain height.
  if (owed != 0)
    fluid_surface_add(cur, i, j, -owed);

  // So the patch is not mistaken for flat and retired before it has been stepped once.
  if (amplitude > surface->slots[index].peak)
    surface->slots[index].peak = amplitude;
}

int fluid_surface_absorb_world(FluidSurface *surface, World *world)
{
  if (!surface || !world)
    return 0;

  FluidSplash batch[64];
  int applied = 0;
  for (;;)
  {
    const int got = fluid_sim_drain_splashes(world, batch, 64);
    if (got <= 0)
      break;
    for (int i = 0; i < got; i++)
      fluid_surface_splash(surface, world, &batch[i]);
    applied += got;
    if (got < 64)
      break;
  }
  return applied;
}

// ---------------------------------------------------------------------------
// Wave step

// The column across a patch boundary, or the edge column itself where there is no neighbour.
// Reusing the edge value makes the boundary reflect, which is what a shoreline does to a ripple.
static inline int fluid_surface_edge(const int16_t *neighbour, int index, int fallback)
{
  return neighbour ? (int)neighbour[index] : fallback;
}

// Shrink a value towards zero by a fraction, and by at least one unit.
//
// The floor is the point. Dividing an integer by 64 gives nothing back below 64, so a ripple that
// has decayed to a few units of amplitude stops being damped at all and rings forever — this
// scheme is only marginally stable without damping, so nothing else brings it down. One unit a
// step puts a definite end to it while leaving the decay of a visible wave governed by the
// fraction.
static inline int fluid_decay(int value, int divisor)
{
  if (value == 0)
    return 0;
  const int magnitude = value > 0 ? value : -value;
  const int loss = magnitude / divisor + 1;
  if (loss >= magnitude)
    return 0;
  return value > 0 ? value - loss : value + loss;
}

void fluid_surface_step(FluidSurface *surface, float dt_seconds)
{
  if (!surface || surface->live == 0)
    return;

  surface->accum += dt_seconds > 0.0f ? dt_seconds : 0.0f;
  const float period = 1.0f / (float)FLUID_SURFACE_HZ;
  int steps = (int)(surface->accum / period);
  if (steps <= 0)
    return;
  // A long stall must not be paid back all at once; the waves would be ahead of where the player
  // last saw them either way, and catching up would cost a spike of work.
  if (steps > 4)
    steps = 4;
  surface->accum -= (float)steps * period;

  // Retire patches whose voxel is no longer exposed water. Done here rather than on lookup so a
  // drained pond stops costing anything even if nothing asks about it again.
  for (int i = 0; i < surface->capacity; i++)
  {
    FluidSurfaceSlot *slot = &surface->slots[i];
    if (!slot->used)
      continue;
    const Voxel *v = world_pos_in_bounds_fast(slot->world, slot->x, slot->y, slot->z)
                         ? world_voxel_cptr_fast(slot->world, slot->x, slot->y, slot->z)
                         : NULL;
    if (!v || v->type != VOXEL_WATER)
      fluid_surface_retire(surface, i);
  }

  const int limit = FLUID_WAVE_MAX_SUBVOXELS * FLUID_SURFACE_UNIT;

  for (int step = 0; step < steps; step++)
  {
    const int cur_buf = surface->parity;
    const int next_buf = 1 - cur_buf;

    // Give a ripple somewhere to go.
    //
    // A patch reads its neighbours' edge columns, and where there is no neighbour patch it reads
    // its own edge instead, which is a wall: the wave reflects. That is right at a shoreline and
    // wrong in the middle of a lake, where it would trap every ripple inside the voxel it started
    // in and make the water look tiled. Since patches now exist only where there is something to
    // simulate, the wave has to bring them into being ahead of itself — one voxel at a time, as it
    // arrives at each boundary.
    for (int si = 0; si < surface->capacity && surface->live < surface->capacity; si++)
    {
      const FluidSurfaceSlot *slot = &surface->slots[si];
      if (!slot->used || slot->peak < FLUID_WAVE_FLAT)
        continue;

      const int16_t *h = slot->patch.height[cur_buf];
      static const int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
      for (int d = 0; d < 4; d++)
      {
        // The column of this patch that borders that neighbour.
        int reach = 0;
        for (int k = 0; k < FLUID_SURFACE_DIM; k++)
        {
          const int at = (d == 0)   ? k * FLUID_SURFACE_DIM
                         : (d == 1) ? k * FLUID_SURFACE_DIM + (FLUID_SURFACE_DIM - 1)
                         : (d == 2) ? k
                                    : (FLUID_SURFACE_DIM - 1) * FLUID_SURFACE_DIM + k;
          const int v = h[at] < 0 ? -(int)h[at] : (int)h[at];
          if (v > reach)
            reach = v;
        }
        if (reach < FLUID_WAVE_FLAT)
          continue;

        const int nx = (int)slot->x + offsets[d][0], ny = (int)slot->y + offsets[d][1];
        const int nz = (int)slot->z;
        if (!world_pos_in_bounds_fast(slot->world, nx, ny, nz))
          continue;
        if (world_voxel_cptr_fast(slot->world, nx, ny, nz)->type != VOXEL_WATER)
          continue; // a shore, and reflecting off it is correct
        if (surface->live >= surface->capacity)
          break; // and so is reflecting off the edge of the cache, which is the gentler failure:
                 // one wave stops spreading rather than another being evicted mid-ring
        fluid_surface_slot_for(surface, slot->world, nx, ny, nz, true);
      }
    }

    for (int si = 0; si < surface->capacity; si++)
    {
      FluidSurfaceSlot *slot = &surface->slots[si];
      if (!slot->used)
        continue;

      // Neighbours resolved once per patch. Every patch reads the same time level and writes the
      // other, so the result does not depend on the order the patches happen to sit in.
      const int16_t *side[4] = {NULL, NULL, NULL, NULL}; // -X, +X, -Y, +Y
      static const int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
      for (int d = 0; d < 4; d++)
      {
        const int n = fluid_surface_find(surface, slot->world, slot->x + offsets[d][0],
                                        slot->y + offsets[d][1], slot->z);
        if (n >= 0)
          side[d] = surface->slots[n].patch.height[cur_buf];
      }

      // The buffer about to be written is the one holding t-1, which is exactly what the update
      // needs; each column reads its own previous value and then overwrites it in place.
      const int16_t *cur = slot->patch.height[cur_buf];
      int16_t *next = slot->patch.height[next_buf];
      long long mean_error = 0;
      int peak = 0;

      for (int j = 0; j < FLUID_SURFACE_DIM; j++)
      {
        for (int i = 0; i < FLUID_SURFACE_DIM; i++)
        {
          const int at = j * FLUID_SURFACE_DIM + i;
          const int c = (int)cur[at];

          const int west = i > 0 ? (int)cur[at - 1]
                                 : fluid_surface_edge(side[0], at + (FLUID_SURFACE_DIM - 1), c);
          const int east = i < FLUID_SURFACE_DIM - 1
                               ? (int)cur[at + 1]
                               : fluid_surface_edge(side[1], at - (FLUID_SURFACE_DIM - 1), c);
          const int south = j > 0 ? (int)cur[at - FLUID_SURFACE_DIM]
                                  : fluid_surface_edge(side[2],
                                                       at + (FLUID_SURFACE_DIM - 1) *
                                                                FLUID_SURFACE_DIM,
                                                       c);
          const int north = j < FLUID_SURFACE_DIM - 1
                                ? (int)cur[at + FLUID_SURFACE_DIM]
                                : fluid_surface_edge(side[3],
                                                     at - (FLUID_SURFACE_DIM - 1) *
                                                              FLUID_SURFACE_DIM,
                                                     c);

          // Damped wave equation, leapfrogged over the two stored time levels. The momentum term
          // is what makes this a wave rather than the smoothing the coarse simulation does: the
          // surface carries on past flat and comes back.
          const int laplacian = west + east + south + north - 4 * c;
          const int momentum = fluid_decay(c - (int)next[at], FLUID_WAVE_MOMENTUM_LOSS);
          int value = c + momentum + (laplacian * FLUID_WAVE_C2_Q8) / 256;
          value = fluid_decay(value, FLUID_WAVE_HEIGHT_LOSS);

          if (value > limit)
            value = limit;
          else if (value < -limit)
            value = -limit;
          next[at] = (int16_t)value;
          mean_error += value;
          if (value > peak)
            peak = value;
          else if (-value > peak)
            peak = -value;
        }
      }

      // Hold the field to zero mean.
      //
      // This is not a cosmetic correction. The height field carries displacement only — how far
      // the surface is from where the voxel's level puts it — and the terms above are integer
      // divisions that truncate towards zero, which biases every crest and trough by a fraction
      // of a unit. Those fractions do not cancel: a ripple has more cells on one side of flat
      // than the other, so the patch drifts, and since a uniform offset is the one shape the wave
      // equation exerts no restoring force on, nothing pulls it back. Left alone, a single drop
      // walks a whole pond upwards.
      const long long half = FLUID_SURFACE_CELLS / 2;
      const int drift =
          (int)((mean_error >= 0 ? mean_error + half : mean_error - half) / FLUID_SURFACE_CELLS);
      if (drift != 0)
      {
        for (int at = 0; at < FLUID_SURFACE_CELLS; at++)
          next[at] = (int16_t)(next[at] - drift);
      }

      slot->peak = peak;
    }
    surface->parity = next_buf;
  }

  // Retire what has gone flat, after the last step rather than during it: a patch that is about to
  // be handed a ripple by its neighbour has to still be here to receive it, and that only becomes
  // clear once the sweep it is part of has finished.
  if (steps > 0)
  {
    for (int si = 0; si < surface->capacity; si++)
    {
      if (surface->slots[si].used && surface->slots[si].peak < FLUID_WAVE_FLAT)
        fluid_surface_retire(surface, si);
    }
  }
}

// ---------------------------------------------------------------------------
// Bake

bool fluid_surface_bake_voxel(FluidSurface *surface, const World *world, int x, int y, int z,
                              uint8_t base_r, uint8_t base_g, uint8_t base_b,
                              uint32_t *out_texels)
{
  if (!surface || !world || !out_texels)
    return false;
  if (!world_pos_in_bounds_fast(world, x, y, z))
    return false;

  const Voxel *voxel = world_voxel_cptr_fast(world, x, y, z);
  if (!voxel || voxel->type != VOXEL_WATER)
    return false;

  // Found, never created. A voxel has a surface because something disturbed it, and one that has
  // not been disturbed has nothing to draw that the plain water face does not already show — so
  // reporting no surface here is what lets a still lake cost nothing. See FLUID_WAVE_FLAT.
  const int index = fluid_surface_slot_for(surface, world, x, y, z, false);
  if (index < 0)
    return false;

  const int16_t *h = surface->slots[index].patch.height[surface->parity];

  // Depth sets how much of the water you see through. A brim-full cube is as opaque as water gets
  // here; a film left behind by a receding puddle is nearly clear.
  const int level = fluid_level_of(voxel);
  int alpha = 90 + (level * 130) / FLUID_LEVEL_FULL;
  if (alpha > 235)
    alpha = 235;

  for (int j = 0; j < FLUID_SURFACE_DIM; j++)
  {
    for (int i = 0; i < FLUID_SURFACE_DIM; i++)
    {
      const int at = j * FLUID_SURFACE_DIM + i;

      // Slope of the surface, from the neighbouring columns. Light arrives from -X/-Y, so a face
      // tilted towards it brightens and one tilted away falls into shadow: the shape of the wave
      // shows up as shading on a face the renderer draws flat.
      const int west = i > 0 ? (int)h[at - 1] : (int)h[at];
      const int east = i < FLUID_SURFACE_DIM - 1 ? (int)h[at + 1] : (int)h[at];
      const int south = j > 0 ? (int)h[at - FLUID_SURFACE_DIM] : (int)h[at];
      const int north = j < FLUID_SURFACE_DIM - 1 ? (int)h[at + FLUID_SURFACE_DIM] : (int)h[at];

      const int slope = (west - east) + (south - north);

      // Full scale at a sixteenth of a sub-voxel of slope.
      //
      // That is a much smaller slope than it sounds, and deliberately so. A ring spreading from a
      // splash loses amplitude as it goes — its energy is spread around a growing circle — so by
      // the time it is a voxel or two out it is a hundredth of a sub-voxel deep. Scaled to the
      // slope a fresh splash makes, everything but the impact itself renders as flat water. The
      // ceiling and floor below are what keep the impact from blowing out at this sensitivity.
      int shade = 256 + (slope * 192) / (FLUID_SURFACE_UNIT / 16 + 1);
      if (shade < 96)
        shade = 96;
      if (shade > 416)
        shade = 416;

      // Crests also pick up a little more light than troughs regardless of slope, which keeps a
      // standing wave legible when it is momentarily flat-topped.
      shade += ((int)h[at] * 48) / (FLUID_SURFACE_UNIT / 2 + 1);

      int r = (base_r * shade) / 256;
      int g = (base_g * shade) / 256;
      int b = (base_b * shade) / 256;
      if (r > 255) r = 255;
      if (g > 255) g = 255;
      if (b > 255) b = 255;
      if (r < 0) r = 0;
      if (g < 0) g = 0;
      if (b < 0) b = 0;

      out_texels[at] = ((uint32_t)alpha << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) |
                       (uint32_t)b;
    }
  }
  return true;
}
