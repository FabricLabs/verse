#ifndef FOLIAGE_BEND_H
#define FOLIAGE_BEND_H

#include <stdbool.h>

#include "voxel.h"

// Visual push-aside for foliage the player (or an inhabited body) walks through.
//
// The mesh stays a unit cube for collision and face culling; only the drawn corners shear. A body
// nearby leans the top of a tall-grass or leaf voxel away from it and squashes the stem, so the
// canopy looks parted without rewriting the world. Ground grass is left alone: shearing the lawn
// opens gaps between tiles that read as sky/cloud shining through the surface.
//
// Shared by the isometric and first-person paths so both views agree on what is bent.

#define FOLIAGE_BEND_MAX_BODIES 12

typedef struct
{
  float x, y, z; // body centre in world voxel coordinates
  float radius;  // horizontal influence, typically ~1.5–2
} FoliageBendBody;

typedef struct
{
  FoliageBendBody bodies[FOLIAGE_BEND_MAX_BODIES];
  int count;
} FoliageBendField;

void foliage_bend_field_clear(FoliageBendField *field);
bool foliage_bend_field_add(FoliageBendField *field, float x, float y, float z, float radius);

// Passable canopy (tall grass, leaves) and bushes. Ground lawn is excluded.
bool foliage_bend_affects(VoxelType type);

// Lean (horizontal voxels) and height scale for the voxel centred at (vx, vy, vz).
// lean is zero and squash is 1 when nothing is nearby.
void foliage_bend_sample(const FoliageBendField *field, VoxelType type, float vx, float vy,
                         float vz, float *out_lean_x, float *out_lean_y, float *out_squash);

// Shear one corner of a unit voxel at integer origin (ox, oy, oz). Height fraction within the
// voxel controls how much lean applies (base stays planted, tip swings away).
void foliage_bend_apply_corner(const FoliageBendField *field, VoxelType type, int ox, int oy,
                               int oz, float *cx, float *cy, float *cz);

#endif // FOLIAGE_BEND_H
