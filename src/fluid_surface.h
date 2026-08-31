#ifndef FLUID_SURFACE_H
#define FLUID_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

#include "fluid_sim.h"
#include "world.h"

// The surface of a water voxel, at sub-voxel resolution.
//
// A voxel of water is one cell to the simulation in fluid_sim.h, which knows only how much of the
// cube is full. That is the right granularity for deciding where water goes and the wrong one for
// looking at it: a pond rendered from it is a flat plate. So the exposed face of a water voxel
// carries a second, finer field — a 32x32 grid of heights, one per sub-voxel column, the same
// resolution the material templates in material_worlds.h bake their faces at.
//
// The two are driven by the same idea. Cells exchange with their neighbours until they agree; the
// difference is that the coarse simulation exchanges volume and settles, while this one exchanges
// displacement and overshoots, which is what makes a disturbance travel outwards as a wave instead
// of flattening in place. When the simulation reports a unit of water arriving in a voxel, the
// sub-voxel column it landed on is pushed down and the ring around it pushed up by the same total,
// so the voxel's average height still comes from its level and only the shape of the surface moves.
//
// Waves cross voxel boundaries: a patch reads its neighbours' edge columns where those neighbours
// are also water, so a ripple travels over a lake rather than bouncing inside one cube. It
// reflects off the edges that border something other than water, which is what a shoreline does.
//
// Patches are allocated per exposed voxel and are far too expensive to keep for a whole world
// (a 128-deep world could expose a hundred thousand water faces). The cache holds the ones being
// looked at, retiring whichever has gone longest without being drawn.

#define FLUID_SURFACE_DIM 32
#define FLUID_SURFACE_CELLS (FLUID_SURFACE_DIM * FLUID_SURFACE_DIM)

// Heights are fixed point: this many units to one sub-voxel of height. Generous on purpose. The
// damping in fluid_surface.c cannot decay a ripple by less than one unit per step, so the unit
// size is what decides how long a small wave survives; at this resolution a ripple a third of a
// sub-voxel deep rings for several seconds. int16 still reaches +/-32 sub-voxels, far past
// anything that reads as water.
#define FLUID_SURFACE_UNIT 1024

// Simulation rate for the wave field. Fixed rather than tied to the frame rate: the update is only
// stable below a step size set by the propagation speed, and a dropped frame must not be allowed
// to turn ripples into noise.
#define FLUID_SURFACE_HZ 60

// Radius in sub-voxels of the crater an arrival leaves: water is pushed out of a dish in the middle
// and piled into a rim out to here. Nothing outside this is touched at the moment of the splash;
// everything beyond it is reached by the wave, if at all.
//
// The width is not cosmetic. A dent one column across is the shortest wavelength a 32x32 grid can
// represent, and the shortest wavelength is the one an explicit wave update carries worst — it
// barely travels and damps out almost at once, so a splash stamped that narrowly shows as a dark
// speck that vanishes rather than a ring that spreads.
#define FLUID_SPLASH_RIM 6

typedef struct
{
  // Two time levels of the height field. A damped wave needs the previous state as well as the
  // current one, and having both means no separate velocity array. `parity` selects which is
  // current; see fluid_surface_step.
  int16_t height[2][FLUID_SURFACE_CELLS];
} FluidSurfacePatch;

typedef struct FluidSurface FluidSurface;

// Create a cache holding at most max_patches surfaces. Returns NULL on allocation failure.
FluidSurface *fluid_surface_create(int max_patches);
void fluid_surface_destroy(FluidSurface *surface);

// Advance every live patch. Accumulates dt and runs whole steps at FLUID_SURFACE_HZ, so calling it
// once a frame at any frame rate gives the same waves.
void fluid_surface_step(FluidSurface *surface, float dt_seconds);

// Disturb the surface of a voxel where fluid arrived. Does nothing if the voxel has no patch and
// the cache is full — an unseen splash is not worth evicting a visible surface for.
void fluid_surface_splash(FluidSurface *surface, const World *world, const FluidSplash *splash);

// Take everything the simulation has recorded for a world and apply it. Call on the thread that
// owns the world, after its step has finished.
int fluid_surface_absorb_world(FluidSurface *surface, World *world);

// Draw a voxel's surface into 32x32 ARGB8888 texels, one per sub-voxel column. Returns false when
// the voxel has no surface — because nothing has disturbed it, or because its ripples have died
// away — in which case the caller should draw the face the way it would without one. That is the
// usual answer: water is normally still, and a still surface has nothing to show that a flat face
// does not.
//
// Colour comes from the base water colour lit by the slope of the surface, so a crest catches the
// light and a trough falls into shadow. Alpha comes from the voxel's level: a brim-full cube is as
// opaque as water gets and a shallow film is nearly clear, which is how depth reads from above.
bool fluid_surface_bake_voxel(FluidSurface *surface, const World *world, int x, int y, int z,
                              uint8_t base_r, uint8_t base_g, uint8_t base_b,
                              uint32_t *out_texels);

// The current height field for a voxel's surface, or NULL if it has no patch. FLUID_SURFACE_CELLS
// values in FLUID_SURFACE_UNIT-per-sub-voxel fixed point, row-major with x fastest. For tests and
// diagnostics; renderers want fluid_surface_bake_voxel.
const int16_t *fluid_surface_heights(FluidSurface *surface, const World *world,
                                     int x, int y, int z);

// Number of surfaces currently being kept. Diagnostics.
int fluid_surface_live_count(const FluidSurface *surface);

#endif // FLUID_SURFACE_H
