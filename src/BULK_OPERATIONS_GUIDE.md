# World Bulk Operations System

## Overview

The World Bulk Operations System provides a comprehensive, composable framework for performing large-scale voxel modifications on worlds. It's designed to be both performant and flexible, allowing for complex operations with simple, reusable components.

## Key Features

### 1. **Composable Design**
- **Filters**: Reusable voxel selection criteria
- **Operations**: Standardized bulk modification patterns
- **Modes**: Different behavior types (replace, additive, subtractive, masked)
- **Combination**: Filters can be combined with AND/OR logic

### 2. **Performance Optimized**
- **Batch Operations**: Group multiple operations for efficiency
- **Efficient Iteration**: Optimized loops for large regions
- **Memory Management**: Smart buffer handling for large operations

### 3. **Flexible Filtering**
- **Type-based**: Select voxels by type
- **Position-based**: Select by height, coordinates, or shapes
- **Condition-based**: Select by voxel properties
- **Noise-based**: Select using deterministic noise patterns

## Core Concepts

### VoxelFilter
A function pointer that determines whether a voxel should be included in an operation:

```c
typedef bool (*VoxelFilter)(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data);
```

### BulkOperationMode
Defines how the operation should behave:

- **`BULK_OP_REPLACE`**: Replace all voxels in the region
- **`BULK_OP_ADDITIVE`**: Only modify non-air voxels
- **`BULK_OP_SUBTRACTIVE`**: Remove voxels (set to air)
- **`BULK_OP_MASKED`**: Use filter to determine which voxels to modify

## Built-in Filters

### Basic Filters
```c
// Select only air voxels
voxel_filter_air_only(voxel, x, y, z, user_data)

// Select only solid (non-air) voxels
voxel_filter_solid_only(voxel, x, y, z, user_data)

// Select voxels of a specific type
voxel_filter_type(voxel, x, y, z, user_data)
```

### Position-based Filters
```c
// Select voxels within a height range
uint32_t range[2] = {min_y, max_y};
voxel_filter_height_range(voxel, x, y, z, range)

// Select voxels within a sphere
float params[4] = {center_x, center_y, center_z, radius};
voxel_filter_sphere(voxel, x, y, z, params)

// Select voxels within a cylinder
float params[5] = {center_x, center_z, y_start, y_end, radius};
voxel_filter_cylinder(voxel, x, y, z, params)
```

### Noise-based Filters
```c
// Select voxels based on noise threshold
float params[3] = {noise_scale, threshold, seed};
voxel_filter_noise_based(voxel, x, y, z, params)
```

## Composite Filters

Combine multiple filters with logical operations:

```c
// Create AND filter (all conditions must be true)
CompositeFilter* and_filter = composite_filter_create(true);
composite_filter_add(and_filter, voxel_filter_type);
composite_filter_add(and_filter, voxel_filter_height_range);

// Create OR filter (any condition can be true)
CompositeFilter* or_filter = composite_filter_create(false);
composite_filter_add(or_filter, voxel_filter_type);
composite_filter_add(or_filter, voxel_filter_height_range);

// Use the composite filter
bool should_include = composite_filter_evaluate(and_filter, voxel, x, y, z, user_data);
```

## Core Operations

### Region Filling
```c
// Fill a rectangular region
bool success = world_fill_region(
    world,           // Target world
    8, 8, 8,        // Start coordinates (x, y, z)
    16, 16, 16,     // Dimensions (width, height, depth)
    VOXEL_STONE,    // Voxel type to fill with
    BULK_OP_REPLACE, // Operation mode
    NULL,            // Filter (NULL = no filter)
    NULL             // Filter data
);
```

### Shape-based Operations
```c
// Fill a sphere
world_fill_sphere(world, 16, 16, 16, 12, VOXEL_GRASS, 1.0f, BULK_OP_REPLACE, NULL, NULL);

// Fill a cylinder
world_fill_cylinder(world, 16, 16, 8, 24, 8, VOXEL_WOOD, 1.0f, BULK_OP_REPLACE, NULL, NULL);

// Fill an ellipsoid
world_fill_ellipsoid(world, 16, 16, 16, 10, 6, 10, VOXEL_WATER, 1.0f, BULK_OP_REPLACE, NULL, NULL);
```

### World Merging
```c
// Merge source world into target at offset
world_merge_worlds(
    target,          // Target world
    source,          // Source world
    8, 8, 8,        // Offset in target world
    BULK_OP_ADDITIVE, // Only add to non-air voxels
    NULL,            // Filter
    NULL             // Filter data
);
```

### Pattern Application
```c
// Apply noise-based pattern
world_apply_noise_pattern(
    world,           // Target world
    0, 0, 0,        // Start coordinates
    32, 32, 32,     // Region size
    VOXEL_CRYSTAL,  // Voxel type
    1.0f,           // Noise scale
    0.7f,           // Threshold
    BULK_OP_MASKED, // Use filter
    filter,          // Filter function
    filter_data      // Filter parameters
);
```

## Advanced Usage Examples

### 1. **Terrain Modification with Filters**
```c
// Remove only stone voxels above height 20
uint32_t height_range[2] = {20, 255};
world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                 VOXEL_AIR, BULK_OP_MASKED, voxel_filter_height_range, height_range);
```

### 2. **Selective Ore Generation**
```c
// Create composite filter: must be stone AND in specific height range AND pass noise test
CompositeFilter* ore_filter = composite_filter_create(true);
composite_filter_add(ore_filter, voxel_filter_type);

VoxelType stone_type = VOXEL_STONE;
uint32_t ore_height_range[2] = {10, 30};
float noise_params[3] = {2.0f, 0.8f, 42.0f};

// Apply ore only where all conditions are met
world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                 VOXEL_ORE_COPPER, BULK_OP_MASKED, composite_filter_evaluate, ore_filter);
```

### 3. **Cave System Generation**
```c
// Create cave system by removing voxels in noise-based pattern
float cave_params[3] = {3.0f, 0.6f, 123.0f};
world_apply_noise_pattern(world, 0, 0, 0, world->width, world->height, world->depth,
                         VOXEL_AIR, 1.0f, 0.6f, BULK_OP_MASKED,
                         voxel_filter_noise_based, cave_params);
```

### 4. **World Blending**
```c
// Blend two worlds with custom filter
bool blend_filter(const Voxel* voxel, uint32_t x, uint32_t y, uint32_t z, void* user_data) {
    // Only blend if source voxel is not air and target is air
    Voxel* target_voxel = world_get_voxel((World*)user_data, x, y, z);
    return target_voxel && target_voxel->type == VOXEL_AIR;
}

world_merge_worlds(target, source, 0, 0, 0, BULK_OP_MASKED, blend_filter, target);
```

## Performance Considerations

### 1. **Batch Operations**
For multiple small operations, use the batch system:

```c
BulkOperationBatch* batch = bulk_operation_batch_create(world, NULL, NULL, BULK_OP_REPLACE, 1000);

// Add operations
for (int i = 0; i < 1000; i++) {
    bulk_operation_batch_add_operation(batch, x[i], y[i], z[i], type[i]);
}

// Execute all at once
bulk_operation_batch_execute(batch);
bulk_operation_batch_destroy(batch);
```

### 2. **Filter Efficiency**
- **Simple filters** (type, height) are very fast
- **Complex filters** (noise, composite) have higher overhead
- **Composite filters** with many conditions can be slow

### 3. **Operation Size**
- **Small regions** (< 1000 voxels): Use individual operations
- **Medium regions** (1000-10000 voxels): Use bulk operations
- **Large regions** (> 10000 voxels): Consider batching or chunking

## Best Practices

### 1. **Filter Design**
- Keep filters simple and fast
- Use composite filters sparingly
- Cache filter results when possible

### 2. **Operation Planning**
- Plan operations to minimize world iterations
- Use appropriate operation modes
- Consider the order of operations

### 3. **Memory Management**
- Clean up composite filters after use
- Use appropriate batch sizes
- Monitor memory usage for large operations

## Integration with Existing Code

The bulk operations system is designed to work alongside existing world generation code:

```c
// In world generation functions
void world_generate_cave_system(World* world, const char* seed) {
    // Use existing noise functions for cave shape
    // Then use bulk operations for voxel modification

    // Remove cave air
    world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                     VOXEL_AIR, BULK_OP_MASKED, cave_shape_filter, cave_params);

    // Add cave decorations
    world_apply_noise_pattern(world, 0, 0, 0, world->width, world->height, world->depth,
                             VOXEL_CRYSTAL, 1.0f, 0.9f, BULK_OP_MASKED,
                             voxel_filter_solid_only, NULL);
}
```

## Testing and Validation

Use the provided test suite to validate operations:

```bash
# Build and run tests
make test-bulk-ops
./test-bulk-ops

# Run specific tests
make run-bulk-ops-test
```

## Future Enhancements

### Planned Features
- **GPU Acceleration**: OpenCL/CUDA support for large operations
- **Undo/Redo**: Operation history and reversal
- **Spatial Indexing**: Octree-based optimization for large worlds
- **Custom Shaders**: User-defined voxel modification patterns

### Extension Points
- **Custom Filters**: User-defined filter functions
- **Operation Chaining**: Pipeline multiple operations
- **Progress Callbacks**: Monitor long-running operations
- **Async Operations**: Non-blocking bulk operations

## Conclusion

The World Bulk Operations System provides a powerful, flexible foundation for complex voxel world modifications. By combining simple, composable components, it enables sophisticated operations while maintaining performance and usability.

The system is designed to grow with your needs - start with simple operations and gradually build more complex workflows as required.
