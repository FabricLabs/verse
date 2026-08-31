#ifndef GPU_VOXEL_BUFFER_H
#define GPU_VOXEL_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "world.h"

// CPU-side binary occupancy for lighting and face culling. One bit per voxel: 1 = drawable /
// occluding for the bitfield walkers, 0 = empty. Named "gpu" because a GL_TEXTURE_3D upload path
// exists in gl_occupancy.c; this file stays free of SDL/GL so WORLD_CORE tests can link it
// headlessly.
//
// Water is marked occupied. The isometric emitter only visits set bits; clearing water made lakes
// invisible after every occupancy rebuild even though the voxels and fluid sim were fine. Face
// exposure still treats water as transparent, so ground under a lake stays drawable. Generic
// VOXEL_LEAVES stays clear for under-canopy faces; leaf species remain marked.
typedef struct GpuVoxelBuffer {
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  // Bit-packed occupancy: size = ceil(w*h*d / 8)
  uint8_t *bits;
  uint32_t bits_size;
} GpuVoxelBuffer;

// Shared by full rebuilds and incremental writes — keep them in lockstep.
static inline int gpu_voxel_buffer_type_marks_occupancy(VoxelType t)
{
  return (t != VOXEL_AIR && t != VOXEL_LEAVES) ? 1 : 0;
}

GpuVoxelBuffer *gpu_voxel_buffer_create_from_world(const World *world);
void gpu_voxel_buffer_destroy(GpuVoxelBuffer *buf);

bool gpu_voxel_buffer_update_from_world(GpuVoxelBuffer *buf, const World *world);

// Legacy stub kept for callers that only need a success flag. Real GL upload lives in
// gl_occupancy_upload(); this returns true when the bitfield is non-null.
bool gpu_voxel_buffer_upload(const GpuVoxelBuffer *buf);
bool gpu_voxel_buffer_dispatch_binary_shader(uint32_t workgroup_x, uint32_t workgroup_y,
                                             uint32_t workgroup_z);

static inline bool gpu_voxel_buffer_occupied(const GpuVoxelBuffer *buf, uint32_t x, uint32_t y,
                                             uint32_t z)
{
  if (!buf || !buf->bits || x >= buf->width || y >= buf->height || z >= buf->depth)
    return false;
  const uint32_t i = (z * buf->height + y) * buf->width + x;
  return (buf->bits[i >> 3] & (uint8_t)(1u << (i & 7u))) != 0;
}

// Occupancy-only DDA (Amanatides & Woo). Returns true and writes the first solid cell, or false
// on miss / leaving the volume. Used by single-world lighting when no ShadowWorld cluster is bound.
bool gpu_voxel_buffer_raycast(const GpuVoxelBuffer *buf, float ox, float oy, float oz, float dx,
                              float dy, float dz, int max_steps, int *out_x, int *out_y, int *out_z);

// True when a sun/AO ray escapes without hitting an opaque bit.
static inline float gpu_voxel_buffer_visible(const GpuVoxelBuffer *buf, float ox, float oy, float oz,
                                             float dx, float dy, float dz, int max_steps)
{
  int hx = -1, hy = -1, hz = -1;
  if (gpu_voxel_buffer_raycast(buf, ox, oy, oz, dx, dy, dz, max_steps, &hx, &hy, &hz))
    return 0.0f;
  return 1.0f;
}

#endif // GPU_VOXEL_BUFFER_H
