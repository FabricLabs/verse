#ifndef FLUID_SIM_H
#define FLUID_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Fluid simulation: how much fluid a voxel holds, and where it goes next.
//
// A fluid voxel is not full or empty, it holds a *level*: how much of the cube is occupied,
// recorded in the voxel's quantity field. FLUID_LEVEL_FULL is a brim-full cube. Levels above that
// are not overfull cubes, they are pressure — the extra a cube holds because there is water
// stacked on top of it — and that is what lets water climb back up the far side of a U-bend
// instead of only ever running downhill. FLUID_LEVEL_MAX is the ceiling the field can encode, and
// the two are chosen so a column of full cells from the floor of a 128-deep world to its ceiling
// still fits: one unit of pressure per cell above, 128 + 127 < 256.
//
// Every transfer is a signed integer moved from one cell to another, so the total across a world
// is conserved exactly rather than approximately: nothing is created by a rounding error and
// nothing evaporates into one. Cells reach equilibrium when no neighbouring pair differs by more
// than one level, i.e. to within 1/128th of a cube.
//
// The cost model matters as much as the rules. Simulating fluid by sweeping the whole voxel array
// costs the same whether the world is a flooded cave system or bone dry, which is why the previous
// implementation could only afford to run once a second. This one keeps a queue of the cells that
// might still have somewhere to go: a cell enters the queue when it or a neighbour changes, and
// leaves it by reaching equilibrium. Only air and fluid are queued — solids cannot move — so a
// lake on bedrock does not re-visit the floor on every ripple. Settled water therefore costs
// nothing to simulate, and a world with no fluid at all costs one scan over a bitmap.
//
// Real-time on CPU is the design point: a few thousand cells per tick (see WORLD_FLUID_CELLS_PER_TICK
// / the world editor's PHYSICS_FLUIDS_BUDGET_PER_STEP) keeps a 128³ world inside a frame. A GPU
// compute path would only pay off for "drain the whole volume this frame" cinematic floods; the
// cellular rules here are memory-bound neighbour gathers, and the queue already skips quiet cells.
#define FLUID_LEVEL_FULL 128
#define FLUID_LEVEL_MAX 255

// Pressure gained per cell of fluid stacked above. One unit is the smallest gradient the level
// field can represent, and it is what makes hydrostatic equilibrium reachable at every depth this
// world size allows.
#define FLUID_PRESSURE_PER_CELL 1

// Magma is the same simulation run slower: a transfer is divided by this before it is applied, so
// it creeps where water pours. Water divides by 1.
#define FLUID_MAGMA_VISCOSITY 6

typedef struct FluidSim FluidSim;

// One arrival of fluid into a cell, recorded so the surface of that cell can be disturbed where
// it landed. See fluid_surface.h — the simulation itself has no use for these.
typedef struct
{
  uint16_t x, y, z;
  int8_t from_dx, from_dy, from_dz; // unit step from the donor cell towards this one
  uint8_t units;                    // levels that arrived
} FluidSplash;

typedef struct
{
  int cells_visited;   // cells taken off the queue this step
  int transfers;       // pairs of cells that exchanged fluid
  long long moved;     // total levels moved, summed over transfers
  int queued;          // cells left on the queue for the next step
  bool at_rest;        // nothing moved and nothing is queued: the world is in equilibrium
} FluidStepStats;

// Advance one step, visiting at most max_cells queued cells. Safe to call on a world with no
// fluid; that case costs O(1) once the queue has drained.
FluidStepStats fluid_sim_step(World *world, int max_cells);

// Queue a cell and the neighbours its contents can reach. Anything that writes a fluid voxel
// outside the simulation — placing water, a spring producing it, an explosion clearing a wall —
// must say so this way, or the water will sit still because nobody asked it to move.
void fluid_sim_touch(World *world, int x, int y, int z);

// Forget the queue and rebuild it from a full scan on the next step. For bulk writes that cannot
// enumerate what they touched; world_invalidate_fluid_presence already calls this.
void fluid_sim_invalidate(World *world);

// Release a world's simulation state. Called by world_destroy.
void fluid_sim_destroy(FluidSim *sim);

// Take up to max recorded arrivals, oldest first, and return how many were written. Drains the
// buffer. Call from the thread that owns the world; the simulation writes these during a step.
int fluid_sim_drain_splashes(World *world, FluidSplash *out, int max);

// How much fluid a voxel holds, 0 for anything that is not fluid.
//
// A fluid voxel whose level reads 0 is data written before levels existed — by a generator, an
// older save, or a plain world_set_voxel — and means a full cube, not an empty one. Reading the
// quantity field directly on fluid will get that case wrong.
static inline int fluid_level_of(const Voxel *v)
{
  if (!v || (v->type != VOXEL_WATER && v->type != VOXEL_MAGMA))
    return 0;
  const int q = (int)voxel_get_quantity(v);
  return q == 0 ? FLUID_LEVEL_FULL : q;
}

// The fluid a voxel holds as a fraction of a full cube, 0..1 and slightly above 1 under pressure.
// For renderers: this is the height of the fluid surface within the cube.
static inline float fluid_fill_fraction(const Voxel *v)
{
  return (float)fluid_level_of(v) / (float)FLUID_LEVEL_FULL;
}

// Total fluid in a world, in levels. O(volume); for tests and diagnostics that need to prove
// nothing was created or lost.
long long fluid_sim_total_level(const World *world);

#endif // FLUID_SIM_H
