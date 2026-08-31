#ifndef VOXEL_SHAPE_H
#define VOXEL_SHAPE_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel.h"

struct World;

// Per-voxel geometry modifier: the material stays a VoxelType; the silhouette is shape × orient.
//
// Packed into data8 byte VOXEL_FIELD_SHAPE_ORIENT (formerly RESERVED_5):
//   bits 0..2  shape   (VoxelShape, 0 = full cube — current default)
//   bits 3..7  orient  (0..23; wedge/slab use a documented subset)
//
// Zero byte ⇒ FULL + orient 0, so existing VOX2 saves stay full cubes with no migration.

typedef enum
{
  VOXEL_SHAPE_FULL = 0,   // sealed unit cube (default)
  VOXEL_SHAPE_SLAB = 1,   // half-cell slab
  VOXEL_SHAPE_WEDGE = 2,  // 45° triangular prism / ramp
  VOXEL_SHAPE_CORNER = 3, // tetrahedral outer corner (two ramps meeting)
  VOXEL_SHAPE_COUNT
} VoxelShape;

// Wedge / slab orientation conventions (orients 0..11 used; 12..23 reserved):
//
// WEDGE floor ramps (solid below the diagonal plane):
//   0 rise +X   1 rise +Y   2 rise -X   3 rise -Y
// WEDGE ceiling ramps (solid above the plane):
//   4..7 same cardinal directions
// WEDGE vertical (diagonal in XY, full height — wall corner fillets):
//   8..11
//
// SLAB:
//   0 bottom half   1 top half
//   2..5 side slabs flush to ±X / ±Y faces
//
// CORNER floor: solid under both diagonals; yaw by orient&3.

#define VOXEL_ORIENT_MAX 24

static inline uint8_t voxel_pack_shape_orient(VoxelShape shape, uint8_t orient)
{
  if (shape < 0 || shape >= VOXEL_SHAPE_COUNT)
    shape = VOXEL_SHAPE_FULL;
  orient = (uint8_t)(orient % VOXEL_ORIENT_MAX);
  return (uint8_t)(((orient & 0x1Fu) << 3) | ((uint8_t)shape & 0x07u));
}

static inline VoxelShape voxel_unpack_shape(uint8_t packed)
{
  const uint8_t s = (uint8_t)(packed & 0x07u);
  return (s < (uint8_t)VOXEL_SHAPE_COUNT) ? (VoxelShape)s : VOXEL_SHAPE_FULL;
}

static inline uint8_t voxel_unpack_orient(uint8_t packed)
{
  return (uint8_t)(((packed >> 3) & 0x1Fu) % VOXEL_ORIENT_MAX);
}

static inline VoxelShape voxel_get_shape(const Voxel *voxel)
{
  return voxel_unpack_shape(voxel_get_field(voxel, VOXEL_FIELD_SHAPE_ORIENT));
}

static inline uint8_t voxel_get_orient(const Voxel *voxel)
{
  return voxel_unpack_orient(voxel_get_field(voxel, VOXEL_FIELD_SHAPE_ORIENT));
}

static inline void voxel_set_shape_orient(Voxel *voxel, VoxelShape shape, uint8_t orient)
{
  voxel_set_field(voxel, VOXEL_FIELD_SHAPE_ORIENT, voxel_pack_shape_orient(shape, orient));
}

static inline void voxel_clear_shape(Voxel *voxel)
{
  voxel_set_field(voxel, VOXEL_FIELD_SHAPE_ORIENT, 0);
}

static inline bool voxel_has_nontrivial_shape(const Voxel *voxel)
{
  return voxel && voxel_get_shape(voxel) != VOXEL_SHAPE_FULL;
}

// Decoration yaw: stored as a byte so VOX2 sparse data8 round-trips. Nested silhouettes
// (bushes, roofs, shaped terrain) re-project this angle when drawn.
#ifndef VOXEL_YAW_TAU
#define VOXEL_YAW_TAU 6.283185307179586f
#endif

static inline uint8_t voxel_yaw_u8_from_radians(float yaw_rad)
{
  // Wrap into [0, 2π).
  while (yaw_rad < 0.0f)
    yaw_rad += VOXEL_YAW_TAU;
  while (yaw_rad >= VOXEL_YAW_TAU)
    yaw_rad -= VOXEL_YAW_TAU;
  return (uint8_t)((yaw_rad / VOXEL_YAW_TAU) * 255.0f + 0.5f);
}

static inline float voxel_yaw_radians_from_u8(uint8_t yaw_u8)
{
  return ((float)yaw_u8 / 255.0f) * VOXEL_YAW_TAU;
}

static inline uint8_t voxel_get_yaw_u8(const Voxel *voxel)
{
  return voxel_get_field(voxel, VOXEL_FIELD_YAW);
}

static inline float voxel_get_yaw_radians(const Voxel *voxel)
{
  return voxel_yaw_radians_from_u8(voxel_get_yaw_u8(voxel));
}

static inline void voxel_set_yaw_u8(Voxel *voxel, uint8_t yaw_u8)
{
  voxel_set_field(voxel, VOXEL_FIELD_YAW, yaw_u8);
}

static inline void voxel_set_yaw_radians(Voxel *voxel, float yaw_rad)
{
  voxel_set_yaw_u8(voxel, voxel_yaw_u8_from_radians(yaw_rad));
}

// Blend a hash-derived random yaw with a direction toward (tx,ty) from (x,y).
// town_weight 0 = pure random, 1 = fully face the target.
float decoration_yaw_blend(uint32_t hash, int x, int y, int tx, int ty, float town_weight);

// Apply the same yaw to every sparse/foliage cell in a box (tree canopy coherence).
void world_paint_decoration_yaw(struct World *world, int x0, int y0, int z0, int x1, int y1, int z1,
                                uint8_t yaw_u8);

const char *voxel_shape_name(VoxelShape shape);

// Sub-voxel occupancy in a 32³ lattice after applying orientation.
bool voxel_shape_occupies(VoxelShape shape, uint8_t orient, int sx, int sy, int sz);

// Fraction of the parent cube that is solid (0..1).
float voxel_shape_fill_ratio(VoxelShape shape, uint8_t orient);

// Surface height (0..1) of a floor wedge/slab at local (u,v) in [0,1]² for walk hints.
float voxel_shape_floor_height(VoxelShape shape, uint8_t orient, float u, float v);

// Clear every subvoxel the shape does not occupy (for nested mesh builds).
void voxel_shape_carve_world(struct World *w, VoxelShape shape, uint8_t orient);

// Fill a 32³ world with `fill` wherever the shape is solid (flat shaped props / terrain).
void voxel_shape_fill_world(struct World *w, VoxelShape shape, uint8_t orient, VoxelType fill);

// Place type with shape+orient (preserves other data8 fields when overwriting a cell).
bool world_set_voxel_shaped(struct World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType type,
                            VoxelShape shape, uint8_t orient);

#endif // VOXEL_SHAPE_H
