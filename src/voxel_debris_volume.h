#ifndef VERSE_VOXEL_DEBRIS_VOLUME_H
#define VERSE_VOXEL_DEBRIS_VOLUME_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Pass-2 Teardown objects: unsupported components lifted out of the dense World into small
// independently transformed grids. Occupancy lighting stays on the static World; movers use the
// actor/dynamic shadow path. DebrisSystem (loot particles) stays separate.
//
// Step: gravity → vol–vol TGS → world integrate → secondary shatter on hard impacts →
// writeback when resting.

#define DEBRIS_VOLUME_MAX 48
#define DEBRIS_VOLUME_CELL_CAP 1024
#define DEBRIS_VOLUME_CONTACT_CAP 768
#define DEBRIS_VOLUME_TGS_ITERS 5
#define DEBRIS_VOLUME_FRICTION 0.45f // fallback μ when a volume has no material average yet

typedef struct
{
  int8_t lx, ly, lz; // local coords relative to the volume origin
  VoxelType type;
  uint8_t damage;
} DebrisVolumeCell;

typedef struct
{
  bool active;
  World *home; // world the cells were cut from; collision and writeback target
  float x, y, z; // world-space position of local (0,0,0)
  float vx, vy, vz;
  // Intrinsic Tait–Bryan ZYX (yaw about +Z, pitch about +Y, roll about +X), radians.
  float yaw, pitch, roll;
  float wx, wy, wz; // angular velocity (rad/s) about world X/Y/Z
  float com_lx, com_ly, com_lz; // local COM in cell units
  float inv_mass; // 1 / Σ material mass_kg (heavier chunks accelerate less from the same impulse)
  float inv_ix, inv_iy, inv_iz; // diagonal inertia inverse about local COM
  float friction; // Coulomb μ from cell materials (stone grippy, ice slippery)
  float restitution; // bounce 0..~0.35; only applies on hard impacts
  int cell_count;
  DebrisVolumeCell cells[DEBRIS_VOLUME_CELL_CAP];
  uint8_t rest_ticks; // consecutive supported frames before writeback
  uint8_t shatter_cool; // frames before this volume may secondary-fracture again
} DebrisVolume;

// Warm-started non-penetration contact between two volume voxel AABBs (Devlog #26 style key).
typedef struct
{
  uint8_t a, b; // volume indices
  int8_t axis; // 0=X 1=Y 2=Z, sign encoded in normal
  float nx, ny, nz;
  float impulse_n;
  float impulse_t; // accumulated friction along the contact tangent
  bool live;
} DebrisVolumeContact;

typedef struct DebrisVolumeSystem
{
  DebrisVolume items[DEBRIS_VOLUME_MAX];
  DebrisVolumeContact contacts[DEBRIS_VOLUME_CONTACT_CAP];
  int contact_count;
} DebrisVolumeSystem;

void voxel_debris_volume_reset(DebrisVolumeSystem *sys);

int voxel_debris_volume_active_count(const DebrisVolumeSystem *sys);

// Copy structural cells into a free volume slot and clear them from the world. Returns how many
// cells were extracted, or 0 if the component was too large / no free slot / invalid.
// If out_vol is non-NULL, it receives the new volume (for seeding tumble impulses).
int voxel_debris_volume_extract(DebrisVolumeSystem *sys, World *world, const int *xs, const int *ys,
                                const int *zs, int count);
int voxel_debris_volume_extract_ex(DebrisVolumeSystem *sys, World *world, const int *xs,
                                   const int *ys, const int *zs, int count, DebrisVolume **out_vol);

// Apply a world-space linear impulse at a world hit point. Torque τ = r × J tumbles the volume
// about its COM. Hit point may be anywhere; if far from the body the lever arm still applies.
void voxel_debris_volume_apply_impulse(DebrisVolume *vol, float jx, float jy, float jz, float hit_x,
                                       float hit_y, float hit_z);

// World-space lower corner of a cell after the volume's multi-axis rotation about its COM.
void voxel_debris_volume_cell_world(const DebrisVolume *vol, const DebrisVolumeCell *c, float *wx,
                                    float *wy, float *wz);

// Integrate active volumes whose home is `world`. Settled volumes write back into occupancy.
void voxel_debris_volume_step(DebrisVolumeSystem *sys, World *world, float dt, float gravity);

// Iterate helpers for rendering / shadows. out_n is capped by cap.
int voxel_debris_volume_gather(const DebrisVolumeSystem *sys, const World *world,
                               const DebrisVolume **out, int cap);

#endif // VERSE_VOXEL_DEBRIS_VOLUME_H
