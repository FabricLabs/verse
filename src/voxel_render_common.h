#ifndef VOXEL_RENDER_COMMON_H
#define VOXEL_RENDER_COMMON_H

#include <stdbool.h>
#include "world.h"
#include "gpu_voxel_buffer.h"

// Shared helpers for high-performance voxel rendering across renderers.

// Face indices mirror isometric renderer mapping:
// 0: top (+Z), 1: bottom (-Z), 2: +Y, 3: +X, 4: -Y, 5: -X
// Keep this consistent across all renderers to reuse face-visibility code.

// Types that do not occlude a neighbour's face for rendering.
//
// Foliage and sparse settlement fittings (angled roofs, hung doors, glass panes, hollow props) bake
// with transparent gaps. If face culling treats those cells as solid, the ground under a tuft or the
// interior under a thatch slope is never emitted, and the gaps show sky instead of what sits behind.
static inline bool voxel_is_transparent_type(VoxelType t)
{
  if (t == VOXEL_AIR || t == VOXEL_WATER || t == VOXEL_STEAM || t == VOXEL_GAS || t == VOXEL_ICE ||
      t == VOXEL_OIL)
    return true;
  return world_voxel_type_has_material_gaps(t);
}

// Fast face exposure test using optional occupancy bitfield when available.
// Returns true if the face on voxel (x,y,z) is exposed to air/transparent or
// the neighbor is out-of-bounds. Does not consider camera slice; callers may
// apply additional view-dependent logic.
static inline bool voxel_face_exposed_fast(const World *world, int x, int y, int z, int face_index)
{
  if (!world) return false;

  int ax = x, ay = y, az = z;
  switch (face_index)
  {
    case 0: az += 1; break; // top (+Z)
    case 1: az -= 1; break; // bottom (-Z)
    case 2: ay += 1; break; // +Y
    case 3: ax += 1; break; // +X
    case 4: ay -= 1; break; // -Y
    case 5: ax -= 1; break; // -X
    default: return false;
  }

  // Out-of-bounds neighbor exposes the face
  if (!world_pos_in_bounds_fast(world, ax, ay, az))
    return true;

  // Use occupancy bitfield if dimensions match
  if (world->occupancy_bits && world->occupancy_bits->bits &&
      world->occupancy_bits->width == world->width &&
      world->occupancy_bits->height == world->height &&
      world->occupancy_bits->depth == world->depth)
  {
    const uint32_t W = world->occupancy_bits->width;
    const uint32_t H = world->occupancy_bits->height;
    const uint8_t *bits = world->occupancy_bits->bits;
    uint32_t lin = ((uint32_t)az * H + (uint32_t)ay) * W + (uint32_t)ax;
    uint8_t occupied = (bits[lin >> 3u] >> (lin & 7u)) & 1u;
    return occupied == 0u;
  }

  // Fallback to direct neighbor type check
  const Voxel *adj = world_voxel_cptr_fast(world, ax, ay, az);
  VoxelType t = adj ? adj->type : VOXEL_AIR;
  return voxel_is_transparent_type(t);
}

// Similar to voxel_face_exposed_fast, but if a neighbor cell is occupied in the
// bitfield, performs a lightweight voxel-type check to treat transparent types
// (e.g., WATER, LEAVES, GLASS, ICE) as exposing the face.
static inline bool voxel_face_exposed_transparency_aware(const World *world, int x, int y, int z, int face_index)
{
  if (!world) return false;

  int ax = x, ay = y, az = z;
  switch (face_index)
  {
    case 0: az += 1; break;
    case 1: az -= 1; break;
    case 2: ay += 1; break;
    case 3: ax += 1; break;
    case 4: ay -= 1; break;
    case 5: ax -= 1; break;
    default: return false;
  }

  if (!world_pos_in_bounds_fast(world, ax, ay, az))
    return true;

  if (world->occupancy_bits && world->occupancy_bits->bits &&
      world->occupancy_bits->width == world->width &&
      world->occupancy_bits->height == world->height &&
      world->occupancy_bits->depth == world->depth)
  {
    const uint32_t W = world->occupancy_bits->width;
    const uint32_t H = world->occupancy_bits->height;
    const uint8_t *bits = world->occupancy_bits->bits;
    uint32_t lin = ((uint32_t)az * H + (uint32_t)ay) * W + (uint32_t)ax;
    uint8_t occupied = (bits[lin >> 3u] >> (lin & 7u)) & 1u;
    if (!occupied) return true;
    // Occupied: check actual type for transparency
    const Voxel *adj = world_voxel_cptr_fast(world, ax, ay, az);
    return adj ? voxel_is_transparent_type(adj->type) : true;
  }

  const Voxel *adj = world_voxel_cptr_fast(world, ax, ay, az);
  VoxelType t = adj ? adj->type : VOXEL_AIR;
  return voxel_is_transparent_type(t);
}

#endif // VOXEL_RENDER_COMMON_H


