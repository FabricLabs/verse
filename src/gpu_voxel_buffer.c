#include "gpu_voxel_buffer.h"
#include "world.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline void set_bit(uint8_t *bits, uint32_t i) {
  bits[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

static inline void clear_bit(uint8_t *bits, uint32_t i) {
  bits[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}

static inline void assign_bit(uint8_t *bits, uint32_t i, int value) {
  if (value) set_bit(bits, i); else clear_bit(bits, i);
}

static inline uint32_t voxel_index(uint32_t x, uint32_t y, uint32_t z, uint32_t w, uint32_t h) {
  return (z * h + y) * w + x;
}

GpuVoxelBuffer *gpu_voxel_buffer_create_from_world(const World *world) {
  if (!world) return NULL;
  GpuVoxelBuffer *b = (GpuVoxelBuffer *)calloc(1, sizeof(GpuVoxelBuffer));
  if (!b) return NULL;
  b->width = world->width;
  b->height = world->height;
  b->depth = world->depth;
  uint64_t total = (uint64_t)b->width * (uint64_t)b->height * (uint64_t)b->depth;
  b->bits_size = (uint32_t)((total + 7u) >> 3);
  b->bits = (uint8_t *)calloc(b->bits_size, 1);
  if (!b->bits) { free(b); return NULL; }
  gpu_voxel_buffer_update_from_world(b, world);
  return b;
}

void gpu_voxel_buffer_destroy(GpuVoxelBuffer *buf) {
  if (!buf) return;
  free(buf->bits);
  free(buf);
}

bool gpu_voxel_buffer_update_from_world(GpuVoxelBuffer *buf, const World *world) {
  if (!buf || !world) return false;
  if (buf->width != world->width || buf->height != world->height || buf->depth != world->depth)
    return false;
  memset(buf->bits, 0, buf->bits_size);
  uint32_t w = buf->width, h = buf->height, d = buf->depth;
  for (uint32_t z = 0; z < d; z++)
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++) {
        Voxel *v = world_get_voxel((World *)world, x, y, z);
        int solid = 0;
        if (v)
          solid = gpu_voxel_buffer_type_marks_occupancy(v->type);
        uint32_t i = voxel_index(x, y, z, w, h);
        assign_bit(buf->bits, i, solid);
      }
  return true;
}

bool gpu_voxel_buffer_upload(const GpuVoxelBuffer *buf) {
  // GL upload is gl_occupancy_upload(); this keeps older call sites compiling.
  return buf != NULL && buf->bits != NULL;
}

bool gpu_voxel_buffer_dispatch_binary_shader(uint32_t workgroup_x, uint32_t workgroup_y,
                                             uint32_t workgroup_z) {
  (void)workgroup_x; (void)workgroup_y; (void)workgroup_z;
  return true;
}

bool gpu_voxel_buffer_raycast(const GpuVoxelBuffer *buf, float ox, float oy, float oz, float dx,
                              float dy, float dz, int max_steps, int *out_x, int *out_y, int *out_z)
{
  if (!buf || !buf->bits || max_steps <= 0)
    return false;

  const int w = (int)buf->width;
  const int h = (int)buf->height;
  const int d = (int)buf->depth;

  // Start just outside the first cell along the ray so we do not immediately re-hit the origin.
  int x = (int)floorf(ox);
  int y = (int)floorf(oy);
  int z = (int)floorf(oz);

  const int step_x = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
  const int step_y = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);
  const int step_z = (dz > 0.0f) ? 1 : (dz < 0.0f ? -1 : 0);

  const float inv_dx = (step_x != 0) ? (1.0f / fabsf(dx)) : 1e30f;
  const float inv_dy = (step_y != 0) ? (1.0f / fabsf(dy)) : 1e30f;
  const float inv_dz = (step_z != 0) ? (1.0f / fabsf(dz)) : 1e30f;

  float t_max_x = (step_x > 0) ? ((floorf(ox) + 1.0f - ox) * inv_dx)
                               : ((step_x < 0) ? ((ox - floorf(ox)) * inv_dx) : 1e30f);
  float t_max_y = (step_y > 0) ? ((floorf(oy) + 1.0f - oy) * inv_dy)
                               : ((step_y < 0) ? ((oy - floorf(oy)) * inv_dy) : 1e30f);
  float t_max_z = (step_z > 0) ? ((floorf(oz) + 1.0f - oz) * inv_dz)
                               : ((step_z < 0) ? ((oz - floorf(oz)) * inv_dz) : 1e30f);

  const float t_delta_x = inv_dx;
  const float t_delta_y = inv_dy;
  const float t_delta_z = inv_dz;

  for (int i = 0; i < max_steps; i++)
  {
    if (x < 0 || y < 0 || z < 0 || x >= w || y >= h || z >= d)
      return false;
    if (gpu_voxel_buffer_occupied(buf, (uint32_t)x, (uint32_t)y, (uint32_t)z))
    {
      if (out_x) *out_x = x;
      if (out_y) *out_y = y;
      if (out_z) *out_z = z;
      return true;
    }

    if (t_max_x < t_max_y)
    {
      if (t_max_x < t_max_z)
      {
        x += step_x;
        t_max_x += t_delta_x;
      }
      else
      {
        z += step_z;
        t_max_z += t_delta_z;
      }
    }
    else
    {
      if (t_max_y < t_max_z)
      {
        y += step_y;
        t_max_y += t_delta_y;
      }
      else
      {
        z += step_z;
        t_max_z += t_delta_z;
      }
    }
  }
  return false;
}
