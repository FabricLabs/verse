#ifndef WORLD_BULK_OPS_H
#define WORLD_BULK_OPS_H

#include "world.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// VOXEL FILTER SYSTEM
// ============================================================================

// Filter function type for voxel operations
typedef bool (*VoxelFilter)(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);

// Common filter functions
bool voxel_filter_air_only(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);
bool voxel_filter_solid_only(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);
bool voxel_filter_type(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);
bool voxel_filter_height_range(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);
bool voxel_filter_noise_based(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);
bool voxel_filter_sphere(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);
bool voxel_filter_cylinder(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);

// Composite filter that combines multiple filters with AND/OR logic
typedef struct {
    VoxelFilter* filters;
    int filter_count;
    bool use_and_logic; // true = AND, false = OR
} CompositeFilter;

CompositeFilter* composite_filter_create(bool use_and_logic);
void composite_filter_add(CompositeFilter* cf, VoxelFilter filter);
bool composite_filter_evaluate(const CompositeFilter* cf, const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);
void composite_filter_destroy(CompositeFilter* cf);

// ============================================================================
// BULK OPERATION TYPES
// ============================================================================

// Operation mode for bulk operations
typedef enum {
    BULK_OP_REPLACE,      // Replace all voxels in region
    BULK_OP_ADDITIVE,     // Only modify non-air voxels
    BULK_OP_SUBTRACTIVE,  // Only modify non-air voxels, replace with air
    BULK_OP_MERGE,        // Blend/combine voxel properties
    BULK_OP_MASKED        // Use filter to determine which voxels to modify
} BulkOperationMode;

// ============================================================================
// CORE BULK OPERATIONS
// ============================================================================

// Fill a rectangular region with a voxel type
bool world_fill_region(World* world,
                      uint32_t x0, uint32_t y0, uint32_t z0,
                      uint32_t width, uint32_t height, uint32_t depth,
                      VoxelType type,
                      BulkOperationMode mode,
                      VoxelFilter filter,
                      void* filter_data);

// Copy a region from one world to another with optional transformation
bool world_copy_region(const World* src,
                      uint32_t src_x, uint32_t src_y, uint32_t src_z,
                      World* dst,
                      uint32_t dst_x, uint32_t dst_y, uint32_t dst_z,
                      uint32_t width, uint32_t height, uint32_t depth,
                      BulkOperationMode mode,
                      VoxelFilter filter,
                      void* filter_data);

// Merge two worlds with configurable blending
bool world_merge_worlds(World* target,
                       const World* source,
                       uint32_t offset_x, uint32_t offset_y, uint32_t offset_z,
                       BulkOperationMode mode,
                       VoxelFilter filter,
                       void* filter_data);

// ============================================================================
// SHAPE-BASED OPERATIONS
// ============================================================================

// Fill a sphere with optional falloff
bool world_fill_sphere(World* world,
                      uint32_t center_x, uint32_t center_y, uint32_t center_z,
                      uint32_t radius,
                      VoxelType type,
                      float falloff_power,
                      BulkOperationMode mode,
                      VoxelFilter filter,
                      void* filter_data);

// Fill a cylinder with optional falloff
bool world_fill_cylinder(World* world,
                        uint32_t center_x, uint32_t center_z,
                        uint32_t y_start, uint32_t y_end,
                        uint32_t radius,
                        VoxelType type,
                        float falloff_power,
                        BulkOperationMode mode,
                        VoxelFilter filter,
                        void* filter_data);

// Fill an ellipsoid with optional falloff
bool world_fill_ellipsoid(World* world,
                         uint32_t center_x, uint32_t center_y, uint32_t center_z,
                         uint32_t radius_x, uint32_t radius_y, uint32_t radius_z,
                         VoxelType type,
                         float falloff_power,
                         BulkOperationMode mode,
                         VoxelFilter filter,
                         void* filter_data);

// Fill a half-sphere (dome) with a voxel type
bool world_fill_half_sphere(World* world,
                           uint32_t center_x, uint32_t center_y, uint32_t center_z,
                           uint32_t radius,
                           VoxelType type,
                           bool upper_half,  // true for upper half, false for lower half
                           float falloff_power,
                           BulkOperationMode mode,
                           VoxelFilter filter,
                           void* filter_data);

// Fill a layered terrain with height-based material distribution
bool world_fill_layered_terrain(World* world,
                               uint32_t x0, uint32_t y0, uint32_t z0,
                               uint32_t width, uint32_t height, uint32_t depth,
                               const VoxelType* layer_types,
                               const float* layer_heights,
                               uint32_t layer_count,
                               BulkOperationMode mode,
                               VoxelFilter filter,
                               void* filter_data);

// Fill a terrain with noise-based height variation
bool world_fill_noise_terrain(World* world,
                             uint32_t x0, uint32_t y0, uint32_t z0,
                             uint32_t width, uint32_t height, uint32_t depth,
                             float noise_scale,
                             float base_height,
                             float height_variation,
                             VoxelType type,
                             BulkOperationMode mode,
                             VoxelFilter filter,
                             void* filter_data);

// ============================================================================
// PATTERN-BASED OPERATIONS
// ============================================================================

// Apply a noise-based pattern to a region
bool world_apply_noise_pattern(World* world,
                              uint32_t x0, uint32_t y0, uint32_t z0,
                              uint32_t width, uint32_t height, uint32_t depth,
                              VoxelType type,
                              float noise_scale,
                              float threshold,
                              BulkOperationMode mode,
                              VoxelFilter filter,
                              void* filter_data);

// Apply a cellular automaton pattern
bool world_apply_cellular_pattern(World* world,
                                 uint32_t x0, uint32_t y0, uint32_t z0,
                                 uint32_t width, uint32_t height, uint32_t depth,
                                 VoxelType type,
                                 int iterations,
                                 float birth_threshold,
                                 float survival_threshold,
                                 BulkOperationMode mode,
                                 VoxelFilter filter,
                                 void* filter_data);

// ============================================================================
// TRANSFORMATION OPERATIONS
// ============================================================================

// Rotate a region around a center point
bool world_rotate_region(World* world,
                        uint32_t center_x, uint32_t center_y, uint32_t center_z,
                        uint32_t width, uint32_t height, uint32_t depth,
                        float angle_radians,
                        BulkOperationMode mode,
                        VoxelFilter filter,
                        void* filter_data);

// Scale a region (expand/contract)
bool world_scale_region(World* world,
                       uint32_t center_x, uint32_t center_y, uint32_t center_z,
                       uint32_t width, uint32_t height, uint32_t depth,
                       float scale_x, float scale_y, float scale_z,
                       BulkOperationMode mode,
                       VoxelFilter filter,
                       void* filter_data);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Count voxels matching a filter in a region
uint32_t world_count_filtered_voxels(const World* world,
                                    uint32_t x0, uint32_t y0, uint32_t z0,
                                    uint32_t width, uint32_t height, uint32_t depth,
                                    VoxelFilter filter,
                                    void* filter_data);

// Get bounding box of voxels matching a filter
bool world_get_filtered_bounds(const World* world,
                              uint32_t x0, uint32_t y0, uint32_t z0,
                              uint32_t width, uint32_t height, uint32_t depth,
                              VoxelFilter filter,
                              void* filter_data,
                              uint32_t* min_x, uint32_t* min_y, uint32_t* min_z,
                              uint32_t* max_x, uint32_t* max_y, uint32_t* max_z);

// Apply erosion/dilation morphological operations
bool world_morphological_operation(World* world,
                                 uint32_t x0, uint32_t y0, uint32_t z0,
                                 uint32_t width, uint32_t height, uint32_t depth,
                                 bool is_erosion, // true = erosion, false = dilation
                                 uint32_t kernel_size,
                                 VoxelFilter filter,
                                 void* filter_data);

// ============================================================================
// PERFORMANCE OPTIMIZATION
// ============================================================================

// Batch multiple operations for better performance
typedef struct {
    World* world;
    VoxelFilter filter;
    void* filter_data;
    BulkOperationMode mode;
    uint32_t batch_size;
    Voxel* voxel_buffer;
} BulkOperationBatch;

BulkOperationBatch* bulk_operation_batch_create(World* world, VoxelFilter filter, void* filter_data, BulkOperationMode mode, uint32_t batch_size);
void bulk_operation_batch_add_operation(BulkOperationBatch* batch, uint32_t x, uint32_t y, uint32_t z, VoxelType type);
bool bulk_operation_batch_execute(BulkOperationBatch* batch);
void bulk_operation_batch_destroy(BulkOperationBatch* batch);

#endif // WORLD_BULK_OPS_H
