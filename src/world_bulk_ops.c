#include "world_bulk_ops.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

// ============================================================================
// VOXEL FILTER IMPLEMENTATIONS
// ============================================================================

bool voxel_filter_air_only(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)x;
  (void)y;
  (void)z;
  (void)user_data;
  return voxel && voxel->type == VOXEL_AIR;
}

bool voxel_filter_solid_only(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)x;
  (void)y;
  (void)z;
  (void)user_data;
  return voxel && voxel->type != VOXEL_AIR;
}

bool voxel_filter_type(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)x;
  (void)y;
  (void)z;
  VoxelType target_type = *(VoxelType *)user_data;
  return voxel && voxel->type == target_type;
}

bool voxel_filter_height_range(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)voxel;
  (void)x;
  (void)z;
  uint32_t *range = (uint32_t *)user_data;
  uint32_t min_y = range[0];
  uint32_t max_y = range[1];
  return y >= min_y && y <= max_y;
}

bool voxel_filter_noise_based(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)voxel;
  float *params = (float *)user_data;
  float noise_scale = params[0];
  float threshold = params[1];
  float seed = params[2];

  // Simple hash-based noise for deterministic results
  uint32_t hash = (uint32_t)(x * 73856093u ^ y * 19349663u ^ z * 83492791u ^ (uint32_t)(seed * 1000000));
  float noise = (float)(hash & 0xFFFF) / 65535.0f;

  return noise > threshold;
}

bool voxel_filter_sphere(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)voxel;
  float *params = (float *)user_data;
  float center_x = params[0];
  float center_y = params[1];
  float center_z = params[2];
  float radius = params[3];

  float dx = (float)x - center_x;
  float dy = (float)y - center_y;
  float dz = (float)z - center_z;
  float distance_sq = dx * dx + dy * dy + dz * dz;

  return distance_sq <= radius * radius;
}

bool voxel_filter_cylinder(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)voxel;
  float *params = (float *)user_data;
  float center_x = params[0];
  float center_z = params[1];
  float y_start = params[2];
  float y_end = params[3];
  float radius = params[4];

  if (y < y_start || y > y_end)
    return false;

  float dx = (float)x - center_x;
  float dz = (float)z - center_z;
  float distance_sq = dx * dx + dz * dz;

  return distance_sq <= radius * radius;
}

// ============================================================================
// COMPOSITE FILTER IMPLEMENTATION
// ============================================================================

CompositeFilter *composite_filter_create(bool use_and_logic)
{
  CompositeFilter *cf = malloc(sizeof(CompositeFilter));
  if (!cf)
    return NULL;

  cf->filters = NULL;
  cf->filter_count = 0;
  cf->use_and_logic = use_and_logic;

  return cf;
}

void composite_filter_add(CompositeFilter *cf, VoxelFilter filter)
{
  if (!cf || !filter)
    return;

  VoxelFilter *new_filters = realloc(cf->filters, (cf->filter_count + 1) * sizeof(VoxelFilter));
  if (!new_filters)
    return;

  cf->filters = new_filters;
  cf->filters[cf->filter_count++] = filter;
}

bool composite_filter_evaluate(const CompositeFilter *cf, const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  if (!cf || cf->filter_count == 0)
    return true;

  if (cf->use_and_logic)
  {
    // AND logic: all filters must pass
    for (int i = 0; i < cf->filter_count; i++)
    {
      if (!cf->filters[i](voxel, x, y, z, user_data))
      {
        return false;
      }
    }
    return true;
  }
  else
  {
    // OR logic: at least one filter must pass
    for (int i = 0; i < cf->filter_count; i++)
    {
      if (cf->filters[i](voxel, x, y, z, user_data))
      {
        return true;
      }
    }
    return false;
  }
}

void composite_filter_destroy(CompositeFilter *cf)
{
  if (cf)
  {
    free(cf->filters);
    free(cf);
  }
}

// ============================================================================
// CORE BULK OPERATIONS IMPLEMENTATION
// ============================================================================

static bool should_modify_voxel(const Voxel *voxel, BulkOperationMode mode, VoxelFilter filter, void *filter_data)
{
  if (mode == BULK_OP_REPLACE)
  {
    return true;
  }

  if (mode == BULK_OP_ADDITIVE)
  {
    return voxel && voxel->type != VOXEL_AIR;
  }

  if (mode == BULK_OP_SUBTRACTIVE)
  {
    return voxel && voxel->type != VOXEL_AIR;
  }

  if (mode == BULK_OP_MASKED && filter)
  {
    return filter(voxel, 0, 0, 0, filter_data);
  }

  return true;
}

bool world_fill_region(World *world,
                       uint32_t x0, uint32_t y0, uint32_t z0,
                       uint32_t width, uint32_t height, uint32_t depth,
                       VoxelType type,
                       BulkOperationMode mode,
                       VoxelFilter filter,
                       void *filter_data)
{
  if (!world || !world->voxels)
    return false;

  // Bounds checking
  if (x0 + width > world->width || y0 + height > world->height || z0 + depth > world->depth)
  {
    return false;
  }

  uint32_t modified_count = 0;

  for (uint32_t z = z0; z < z0 + depth; z++)
  {
    for (uint32_t y = y0; y < y0 + height; y++)
    {
      for (uint32_t x = x0; x < x0 + width; x++)
      {
        Voxel *voxel = world_get_voxel(world, x, y, z);
        if (!voxel)
          continue;

        // Check if we should modify this voxel
        if (should_modify_voxel(voxel, mode, filter, filter_data))
        {
          if (mode == BULK_OP_SUBTRACTIVE)
          {
            voxel->type = VOXEL_AIR;
          }
          else
          {
            voxel->type = type;
          }
          modified_count++;
        }
      }
    }
  }

  return modified_count > 0;
}

bool world_copy_region(const World *src,
                       uint32_t src_x, uint32_t src_y, uint32_t src_z,
                       World *dst,
                       uint32_t dst_x, uint32_t dst_y, uint32_t dst_z,
                       uint32_t width, uint32_t height, uint32_t depth,
                       BulkOperationMode mode,
                       VoxelFilter filter,
                       void *filter_data)
{
  if (!src || !dst || !src->voxels || !dst->voxels)
    return false;

  // Bounds checking
  if (src_x + width > src->width || src_y + height > src->height || src_z + depth > src->depth)
  {
    return false;
  }
  if (dst_x + width > dst->width || dst_y + height > dst->height || dst_z + depth > dst->depth)
  {
    return false;
  }

  uint32_t copied_count = 0;

  for (uint32_t z = 0; z < depth; z++)
  {
    for (uint32_t y = 0; y < height; y++)
    {
      for (uint32_t x = 0; x < width; x++)
      {
        const Voxel *src_voxel = world_get_voxel((World *)src, src_x + x, src_y + y, src_z + z);
        Voxel *dst_voxel = world_get_voxel(dst, dst_x + x, dst_y + y, dst_z + z);

        if (!src_voxel || !dst_voxel)
          continue;

        // Check if we should copy this voxel
        if (should_modify_voxel(dst_voxel, mode, filter, filter_data))
        {
          dst_voxel->type = src_voxel->type;
          dst_voxel->condition_mask = src_voxel->condition_mask;
          dst_voxel->data8 = src_voxel->data8;
          copied_count++;
        }
      }
    }
  }

  return copied_count > 0;
}

bool world_merge_worlds(World *target,
                        const World *source,
                        uint32_t offset_x, uint32_t offset_y, uint32_t offset_z,
                        BulkOperationMode mode,
                        VoxelFilter filter,
                        void *filter_data)
{
  if (!target || !source || !target->voxels || !source->voxels)
    return false;

  uint32_t width = source->width;
  uint32_t height = source->height;
  uint32_t depth = source->depth;

  // Check if source world fits within target bounds
  if (offset_x + width > target->width ||
      offset_y + height > target->height ||
      offset_z + depth > target->depth)
  {
    return false;
  }

  return world_copy_region(source, 0, 0, 0, target, offset_x, offset_y, offset_z,
                           width, height, depth, mode, filter, filter_data);
}

// ============================================================================
// SHAPE-BASED OPERATIONS IMPLEMENTATION
// ============================================================================

bool world_fill_sphere(World *world,
                       uint32_t center_x, uint32_t center_y, uint32_t center_z,
                       uint32_t radius,
                       VoxelType type,
                       float falloff_power,
                       BulkOperationMode mode,
                       VoxelFilter filter,
                       void *filter_data)
{
  if (!world || !world->voxels)
    return false;

  uint32_t modified_count = 0;
  float radius_sq = (float)(radius * radius);

  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t y = 0; y < world->height; y++)
    {
      for (uint32_t x = 0; x < world->width; x++)
      {
        float dx = (float)x - (float)center_x;
        float dy = (float)y - (float)center_y;
        float dz = (float)z - (float)center_z;
        float distance_sq = dx * dx + dy * dy + dz * dz;

        if (distance_sq <= radius_sq)
        {
          Voxel *voxel = world_get_voxel(world, x, y, z);
          if (!voxel)
            continue;

          // Check if we should modify this voxel
          if (should_modify_voxel(voxel, mode, filter, filter_data))
          {
            if (mode == BULK_OP_SUBTRACTIVE)
            {
              voxel->type = VOXEL_AIR;
            }
            else
            {
              voxel->type = type;
            }
            modified_count++;
          }
        }
      }
    }
  }

  return modified_count > 0;
}

bool world_fill_cylinder(World *world,
                         uint32_t center_x, uint32_t center_z,
                         uint32_t y_start, uint32_t y_end,
                         uint32_t radius,
                         VoxelType type,
                         float falloff_power,
                         BulkOperationMode mode,
                         VoxelFilter filter,
                         void *filter_data)
{
  if (!world || !world->voxels)
    return false;

  uint32_t modified_count = 0;
  float radius_sq = (float)(radius * radius);

  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t y = y_start; y <= y_end && y < world->height; y++)
    {
      for (uint32_t x = 0; x < world->width; x++)
      {
        float dx = (float)x - (float)center_x;
        float dz = (float)z - (float)center_z;
        float distance_sq = dx * dx + dz * dz;

        if (distance_sq <= radius_sq)
        {
          Voxel *voxel = world_get_voxel(world, x, y, z);
          if (!voxel)
            continue;

          // Check if we should modify this voxel
          if (should_modify_voxel(voxel, mode, filter, filter_data))
          {
            if (mode == BULK_OP_SUBTRACTIVE)
            {
              voxel->type = VOXEL_AIR;
            }
            else
            {
              voxel->type = type;
            }
            modified_count++;
          }
        }
      }
    }
  }

  return modified_count > 0;
}

bool world_fill_ellipsoid(World *world,
                          uint32_t center_x, uint32_t center_y, uint32_t center_z,
                          uint32_t radius_x, uint32_t radius_y, uint32_t radius_z,
                          VoxelType type,
                          float falloff_power,
                          BulkOperationMode mode,
                          VoxelFilter filter,
                          void *filter_data)
{
  if (!world || !world->voxels)
    return false;

  uint32_t modified_count = 0;
  float rx_sq = (float)(radius_x * radius_x);
  float ry_sq = (float)(radius_y * radius_y);
  float rz_sq = (float)(radius_z * radius_z);

  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t y = 0; y < world->height; y++)
    {
      for (uint32_t x = 0; x < world->width; x++)
      {
        float dx = (float)x - (float)center_x;
        float dy = (float)y - (float)center_y;
        float dz = (float)z - (float)center_z;
        float normalized_dist = (dx * dx / rx_sq) + (dy * dy / ry_sq) + (dz * dz / rz_sq);

        if (normalized_dist <= 1.0f)
        {
          Voxel *voxel = world_get_voxel(world, x, y, z);
          if (!voxel)
            continue;

          // Check if we should modify this voxel
          if (should_modify_voxel(voxel, mode, filter, filter_data))
          {
            if (mode == BULK_OP_SUBTRACTIVE)
            {
              voxel->type = VOXEL_AIR;
            }
            else
            {
              voxel->type = type;
            }
            modified_count++;
          }
        }
      }
    }
  }

  return modified_count > 0;
}

// ============================================================================
// PATTERN-BASED OPERATIONS IMPLEMENTATION
// ============================================================================

bool world_apply_noise_pattern(World *world,
                               uint32_t x0, uint32_t y0, uint32_t z0,
                               uint32_t width, uint32_t height, uint32_t depth,
                               VoxelType type,
                               float noise_scale,
                               float threshold,
                               BulkOperationMode mode,
                               VoxelFilter filter,
                               void *filter_data)
{
  if (!world || !world->voxels)
    return false;

  uint32_t modified_count = 0;

  for (uint32_t z = z0; z < z0 + depth; z++)
  {
    for (uint32_t y = y0; y < y0 + height; y++)
    {
      for (uint32_t x = x0; x < x0 + width; x++)
      {
        // Simple hash-based noise
        uint32_t hash = (uint32_t)(x * 73856093u ^ y * 19349663u ^ z * 83492791u);
        float noise = (float)(hash & 0xFFFF) / 65535.0f;

        if (noise > threshold)
        {
          Voxel *voxel = world_get_voxel(world, x, y, z);
          if (!voxel)
            continue;

          // Check if we should modify this voxel
          if (should_modify_voxel(voxel, mode, filter, filter_data))
          {
            if (mode == BULK_OP_SUBTRACTIVE)
            {
              voxel->type = VOXEL_AIR;
            }
            else
            {
              voxel->type = type;
            }
            modified_count++;
          }
        }
      }
    }
  }

  return modified_count > 0;
}

// ============================================================================
// UTILITY FUNCTIONS IMPLEMENTATION
// ============================================================================

uint32_t world_count_filtered_voxels(const World *world,
                                     uint32_t x0, uint32_t y0, uint32_t z0,
                                     uint32_t width, uint32_t height, uint32_t depth,
                                     VoxelFilter filter,
                                     void *filter_data)
{
  if (!world || !world->voxels || !filter)
    return 0;

  uint32_t count = 0;

  for (uint32_t z = z0; z < z0 + depth; z++)
  {
    for (uint32_t y = y0; y < y0 + height; y++)
    {
      for (uint32_t x = x0; x < x0 + width; x++)
      {
        const Voxel *voxel = world_get_voxel((World *)world, x, y, z);
        if (filter(voxel, x, y, z, filter_data))
        {
          count++;
        }
      }
    }
  }

  return count;
}

bool world_get_filtered_bounds(const World *world,
                               uint32_t x0, uint32_t y0, uint32_t z0,
                               uint32_t width, uint32_t height, uint32_t depth,
                               VoxelFilter filter,
                               void *filter_data,
                               uint32_t *min_x, uint32_t *min_y, uint32_t *min_z,
                               uint32_t *max_x, uint32_t *max_y, uint32_t *max_z)
{
  if (!world || !world->voxels || !filter || !min_x || !min_y || !min_z || !max_x || !max_y || !max_z)
  {
    return false;
  }

  bool found_any = false;
  *min_x = *min_y = *min_z = UINT32_MAX;
  *max_x = *max_y = *max_z = 0;

  for (uint32_t z = z0; z < z0 + depth; z++)
  {
    for (uint32_t y = y0; y < y0 + height; y++)
    {
      for (uint32_t x = x0; x < x0 + width; x++)
      {
        const Voxel *voxel = world_get_voxel((World *)world, x, y, z);
        if (filter(voxel, x, y, z, filter_data))
        {
          if (!found_any)
          {
            *min_x = *max_x = x;
            *min_y = *max_y = y;
            *min_z = *max_z = z;
            found_any = true;
          }
          else
          {
            if (x < *min_x)
              *min_x = x;
            if (y < *min_y)
              *min_y = y;
            if (z < *min_z)
              *min_z = z;
            if (x > *max_x)
              *max_x = x;
            if (y > *max_y)
              *max_y = y;
            if (z > *max_z)
              *max_z = z;
          }
        }
      }
    }
  }

  return found_any;
}

// ============================================================================
// PERFORMANCE OPTIMIZATION IMPLEMENTATION
// ============================================================================

BulkOperationBatch *bulk_operation_batch_create(World *world, VoxelFilter filter, void *filter_data, BulkOperationMode mode, uint32_t batch_size)
{
  BulkOperationBatch *batch = malloc(sizeof(BulkOperationBatch));
  if (!batch)
    return NULL;

  batch->world = world;
  batch->filter = filter;
  batch->filter_data = filter_data;
  batch->mode = mode;
  batch->batch_size = batch_size;
  batch->voxel_buffer = malloc(batch_size * sizeof(Voxel));

  if (!batch->voxel_buffer)
  {
    free(batch);
    return NULL;
  }

  return batch;
}

void bulk_operation_batch_add_operation(BulkOperationBatch *batch, uint32_t x, uint32_t y, uint32_t z, VoxelType type)
{
  if (!batch || !batch->voxel_buffer)
    return;

  // This is a simplified version - in practice you'd want to store operations
  // and execute them in batches for better performance
  Voxel *voxel = world_get_voxel(batch->world, x, y, z);
  if (voxel && should_modify_voxel(voxel, batch->mode, batch->filter, batch->filter_data))
  {
    voxel->type = type;
  }
}

bool bulk_operation_batch_execute(BulkOperationBatch *batch)
{
  // This is a placeholder - in practice you'd want to batch multiple operations
  // and execute them together for better performance
  (void)batch;
  return true;
}

void bulk_operation_batch_destroy(BulkOperationBatch *batch)
{
  if (batch)
  {
    free(batch->voxel_buffer);
    free(batch);
  }
}

// ============================================================================
// ADVANCED SHAPE OPERATIONS IMPLEMENTATION
// ============================================================================

// Fill a half-sphere (dome) with a voxel type
bool world_fill_half_sphere(World *world,
                           uint32_t center_x, uint32_t center_y, uint32_t center_z,
                           uint32_t radius,
                           VoxelType type,
                           bool upper_half,  // true for upper half, false for lower half
                           float falloff_power,
                           BulkOperationMode mode,
                           VoxelFilter filter,
                           void *filter_data)
{
  if (!world || !world->voxels)
    return false;

  uint32_t modified_count = 0;
  float radius_sq = (float)(radius * radius);

  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t y = 0; y < world->height; y++)
    {
      for (uint32_t x = 0; x < world->width; x++)
      {
        float dx = (float)x - (float)center_x;
        float dy = (float)y - (float)center_y;
        float dz = (float)z - (float)center_z;
        float distance_sq = dx * dx + dy * dy + dz * dz;

        // Check if point is within sphere
        if (distance_sq <= radius_sq)
        {
          // Check if point is in the correct half
          bool in_correct_half = upper_half ? (dz >= 0) : (dz <= 0);
          if (in_correct_half)
          {
            Voxel *voxel = world_get_voxel(world, x, y, z);
            if (!voxel)
              continue;

            // Check if we should modify this voxel
            if (should_modify_voxel(voxel, mode, filter, filter_data))
            {
              if (mode == BULK_OP_SUBTRACTIVE)
              {
                voxel->type = VOXEL_AIR;
              }
              else
              {
                voxel->type = type;
              }
              modified_count++;
            }
          }
        }
      }
    }
  }

  return modified_count > 0;
}

// Fill a layered terrain with height-based material distribution
bool world_fill_layered_terrain(World *world,
                               uint32_t x0, uint32_t y0, uint32_t z0,
                               uint32_t width, uint32_t height, uint32_t depth,
                               const VoxelType *layer_types,
                               const float *layer_heights,
                               uint32_t layer_count,
                               BulkOperationMode mode,
                               VoxelFilter filter,
                               void *filter_data)
{
  if (!world || !world->voxels || !layer_types || !layer_heights || layer_count == 0)
    return false;

  uint32_t modified_count = 0;

  for (uint32_t z = z0; z < z0 + depth; z++)
  {
    for (uint32_t y = y0; y < y0 + height; y++)
    {
      for (uint32_t x = x0; x < x0 + width; x++)
      {
        // Determine which layer this height belongs to
        VoxelType target_type = VOXEL_AIR;
        for (uint32_t i = 0; i < layer_count; i++)
        {
          if (y < (uint32_t)layer_heights[i])
          {
            target_type = layer_types[i];
            break;
          }
        }

        if (target_type != VOXEL_AIR)
        {
          Voxel *voxel = world_get_voxel(world, x, y, z);
          if (!voxel)
            continue;

                   // Check if we should modify this voxel
         if (should_modify_voxel(voxel, mode, filter, filter_data))
          {
            if (mode == BULK_OP_SUBTRACTIVE)
            {
              voxel->type = VOXEL_AIR;
            }
            else
            {
              voxel->type = target_type;
            }
            modified_count++;
          }
        }
      }
    }
  }

  return modified_count > 0;
}

// Fill a terrain with noise-based height variation
bool world_fill_noise_terrain(World *world,
                             uint32_t x0, uint32_t y0, uint32_t z0,
                             uint32_t width, uint32_t height, uint32_t depth,
                             float noise_scale,
                             float base_height,
                             float height_variation,
                             VoxelType type,
                             BulkOperationMode mode,
                             VoxelFilter filter,
                             void *filter_data)
{
  if (!world || !world->voxels)
    return false;

  uint32_t modified_count = 0;

  for (uint32_t z = z0; z < z0 + depth; z++)
  {
    for (uint32_t y = y0; y < y0 + height; y++)
    {
      for (uint32_t x = x0; x < x0 + width; x++)
      {
        // Simple hash-based noise for deterministic height
        uint32_t hash = (uint32_t)(x * 73856093u ^ y * 19349663u ^ z * 83492791u);
        float noise = (float)(hash & 0xFFFF) / 65535.0f; // 0.0 to 1.0

        // Calculate terrain height at this position
        float terrain_height = base_height + (noise - 0.5f) * height_variation;

        // Fill below terrain height
        if ((float)y <= terrain_height)
        {
          Voxel *voxel = world_get_voxel(world, x, y, z);
          if (!voxel)
            continue;

          // Check if we should modify this voxel
          if (should_modify_voxel(voxel, mode, filter, filter_data))
          {
            if (mode == BULK_OP_SUBTRACTIVE)
            {
              voxel->type = VOXEL_AIR;
            }
              else
            {
              voxel->type = type;
            }
            modified_count++;
          }
        }
      }
    }
  }

  return modified_count > 0;
}
