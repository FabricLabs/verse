#ifndef FLUID_Navier_Stokes_H
#define FLUID_Navier_Stokes_H

#include <stdint.h>
#include <stdbool.h>
#include "world.h"

// A minimal, voxel-aligned Navier–Stokes field container.
// Resolution: 1 voxel per cell, incompressible water model (stubs only).
typedef struct NSField {
  // Dimensions copied from world
  uint32_t width;
  uint32_t height;
  uint32_t depth;

  // Velocity components per voxel cell (centered). Single-precision for speed.
  // Layout: contiguous arrays of size width*height*depth
  float *velocity_x;
  float *velocity_y;
  float *velocity_z;

  // Pressure field (scalar)
  float *pressure;

  // Solid mask bit per voxel (1 = solid/blocked)
  // Packed as bytes for simplicity in stub; can be bit-packed later.
  uint8_t *solid_mask;

  // Water occupancy/density per voxel [0..1] in stub form mapped from world water voxels
  float *water_density;
} NSField;

// Create and destroy a Navier–Stokes field matching a world volume
NSField *ns_create_for_world(const World *world);
void ns_destroy(NSField *f);

// Refresh masks/densities from the world's current voxel data (stub mapping)
void ns_sync_from_world(NSField *f, const World *world);

// Advance one physics step at 1 Hz target. dt_seconds is expected ~1.0.
// Stubbed: updates nothing now, but keeps API stable.
void ns_step(NSField *f, double dt_seconds);

#endif // FLUID_Navier_Stokes_H


