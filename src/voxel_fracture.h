#ifndef VERSE_VOXEL_FRACTURE_H
#define VERSE_VOXEL_FRACTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

struct DebrisVolumeSystem;

// Geometric crack stamps and the neighborhood disconnector (Devlog #28 pass 1).
//
// Stamping deletes voxels. The disconnector then floods 6-connected structural cells from each
// deleted neighbour: anything that cannot reach bedrock or the world floor falls as one cluster.
// DebrisSystem stays loot particles; this module is the connected-body path.
//
// Pass 1 falling is marked with momentum.z < 0 and stepped by voxel_fracture_step_falling.
// Pass 2 (optional): when a DebrisVolumeSystem is bound, unsupported components extract into
// independently transformed grids (batched at DEBRIS_VOLUME_CELL_CAP) instead of on-grid falling.

#define FRACTURE_STAMP_RADIUS_MAX 3
#define FRACTURE_COMPONENT_CAP 8192

// True for voxels the disconnector treats as solid structure (stone, wood, leaves, soil, …).
// Fluids and air are never structural; bedrock is structural and never falls.
bool voxel_fracture_is_structural(VoxelType type);

// Bind the optional pass-2 volume system. NULL restores on-grid falling for every disconnect.
// Borrowed pointer; must outlive the worlds that extract into it.
void voxel_fracture_set_debris_volumes(struct DebrisVolumeSystem *sys);

// Boolean crack around (cx,cy,cz). Deletes destructible solids near the impact; bedrock and fluids
// are untouched. `speed_vox_s` picks stamp size and Worley feature count; `dir` orients three-plane
// cuts. Mass biases shard vs chunk (stone keeps more cells). Returns how many cells were turned
// to AIR / scraped. Always runs the disconnector on the affected neighbourhood afterward.
int voxel_fracture_apply_crack(World *world, int cx, int cy, int cz,
                               float dir_x, float dir_y, float dir_z,
                               float speed_vox_s, uint32_t seed);

// Flood from support after deletions. Seeds are every solid 6-neighbour of the deleted cells in
// [x0..x1]×[y0..y1]×[z0..z1]. Unsupported structural components are marked falling (or extracted).
// Returns how many cells were marked or extracted.
int voxel_fracture_disconnect_region(World *world, int x0, int y0, int z0, int x1, int y1, int z1);

// Convenience: disconnect around a single deleted cell (expands one voxel).
int voxel_fracture_disconnect_at(World *world, int x, int y, int z);

// Move every falling cluster down one voxel when the space below is free (or part of the same
// cluster). Settles cells that rest on supported ground. Returns true if anything moved.
bool voxel_fracture_step_falling(World *world);

// How many cells are currently marked falling.
int voxel_fracture_falling_count(const World *world);

#endif // VERSE_VOXEL_FRACTURE_H
