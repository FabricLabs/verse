#ifndef FOG_VOLUME_H
#define FOG_VOLUME_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Volumetric fog inside a steam (or gas) voxel, at sub-voxel resolution.
//
// A steam voxel is one opaque-ish cube to the world: clouds are placed as VOXEL_STEAM and drawn as
// flat faces. That is the right granularity for "is there a cloud here" and the wrong one for
// walking through one — the body should leave a wake, and the fog should fill back in. So each
// disturbed steam voxel carries a second, finer field: a 32x32x32 density displacement, the same
// face resolution the material templates and water surfaces use, extended into the volume.
//
// Displacement, not absolute density. Zero means ambient steam; negative is a cleared cavity;
// positive is fog piled up ahead of a moving body. The field relaxes back toward zero, so a still
// cloud costs nothing once the wake has died.
//
// Patches are allocated only where something has disturbed the fog. A calm cloud has no patch and
// draws as a flat steam face. The cache retires whatever has gone longest without a touch when it
// is full — the same cost model as fluid_surface.h, which this deliberately mirrors.

#define FOG_VOLUME_DIM 32
#define FOG_VOLUME_CELLS (FOG_VOLUME_DIM * FOG_VOLUME_DIM * FOG_VOLUME_DIM)

// Fixed-point density: this many units to one "full" ambient cell of steam. Generous so a small
// wake can ring for a second or two before integer damping kills it.
#define FOG_VOLUME_UNIT 1024

// Simulation rate. Fixed rather than tied to the frame rate: the diffusion update is only stable
// below a step size set by the stencil, and a dropped frame must not turn a wake into noise.
#define FOG_VOLUME_HZ 30

// Faces a baker can project the volume onto. Order matches MATERIAL_FACE_TOP / LEFT / RIGHT so the
// isometric path can pass its face id through without a translation table.
typedef enum
{
  FOG_FACE_TOP = 0,
  FOG_FACE_LEFT = 1,
  FOG_FACE_RIGHT = 2
} FogFace;

typedef struct
{
  // Two time levels of the density field. Jacobi diffusion needs the previous state; `parity`
  // selects which is current. See fog_volume_step.
  int16_t density[2][FOG_VOLUME_CELLS];
} FogVolumePatch;

typedef struct FogVolume FogVolume;

// Create a cache holding at most max_patches volumes. Returns NULL on allocation failure.
FogVolume *fog_volume_create(int max_patches);
void fog_volume_destroy(FogVolume *volume);

// Advance every live patch. Accumulates dt and runs whole steps at FOG_VOLUME_HZ.
void fog_volume_step(FogVolume *volume, float dt_seconds);

// Disturb fog around a body moving through steam. Creates patches for any steam voxel the body
// overlaps. A stationary body still carves a cavity; velocity piles fog ahead into a bow wave.
// Does nothing for voxels that are not steam, and refuses to evict a live patch for an unseen wake
// when the cache is already full of ringing ones — same policy as fluid_surface_splash.
void fog_volume_body_wake(FogVolume *volume, const World *world, float x, float y, float z,
                          float radius, float vx, float vy, float vz);

// Project one face of a voxel's volume into 32x32 ARGB8888 texels. Returns false when the voxel
// has no patch — because nothing has disturbed it, or because its wake has died — in which case
// the caller should draw the face the flat steam way. Alpha thins where density is negative
// (cleared) and thickens where it is positive (piled).
bool fog_volume_bake_face(FogVolume *volume, const World *world, int x, int y, int z,
                          FogFace face, uint8_t base_r, uint8_t base_g, uint8_t base_b,
                          uint32_t *out_texels);

// Current density field for a voxel, or NULL if it has no patch. FOG_VOLUME_CELLS values in
// FOG_VOLUME_UNIT fixed point, layout (z * DIM + y) * DIM + x. For tests and diagnostics.
const int16_t *fog_volume_densities(FogVolume *volume, const World *world, int x, int y, int z);

// Number of volumes currently being kept. Diagnostics.
int fog_volume_live_count(const FogVolume *volume);

#endif // FOG_VOLUME_H
