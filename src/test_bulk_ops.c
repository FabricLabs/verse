#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "world.h"
#include "world_bulk_ops.h"

// Test world dimensions
#define TEST_WORLD_SIZE 32
#define TEST_ITERATIONS 3

// Performance measurement
static double get_time_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int test_failures = 0;

// Records the outcome as well as labelling it. Every result used to be printed and
// discarded, so this suite always exited 0 and could never fail the release gate.
static const char *outcome(bool ok)
{
  if (!ok)
    test_failures++;
  return ok ? "SUCCESS" : "FAILED";
}

/* Composite filter tests: sub-filters share one user_data blob (CompositeTestParams). */
typedef struct {
  VoxelType stone;
  uint32_t h_min;
  uint32_t h_max;
} CompositeTestParams;

static bool filter_stone_match(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)x;
  (void)y;
  (void)z;
  CompositeTestParams *p = (CompositeTestParams *)user_data;
  return voxel && voxel_get_type(voxel) == p->stone;
}

static bool filter_height_match(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  (void)voxel;
  (void)x;
  (void)y;
  CompositeTestParams *p = (CompositeTestParams *)user_data;
  return z >= p->h_min && z <= p->h_max;
}

typedef struct {
  const CompositeFilter *cf;
  CompositeTestParams *params;
} CompositeWrapCtx;

static bool composite_wrap_voxel_filter(const Voxel *voxel, uint32_t x, uint32_t y, uint32_t z, void *user_data)
{
  CompositeWrapCtx *w = (CompositeWrapCtx *)user_data;
  return composite_filter_evaluate(w->cf, voxel, x, y, z, w->params);
}

// Test basic region filling
static void test_region_filling(void)
{
  printf("\n=== Testing Region Filling ===\n");

  World *world = world_create(TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  if (!world)
  {
    printf("Failed to create test world\n");
    return;
  }

  // Test 1: Simple region fill
  double start_time = get_time_ms();
  bool success = world_fill_region(world, 8, 8, 8, 16, 16, 16, VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
  double end_time = get_time_ms();

  printf("Simple region fill: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  // Test 2: Additive fill (only modify non-air voxels)
  start_time = get_time_ms();
  success = world_fill_region(world, 12, 12, 12, 8, 8, 8, VOXEL_SOIL, BULK_OP_ADDITIVE, NULL, NULL);
  end_time = get_time_ms();

  printf("Additive region fill: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  // Test 3: Subtractive fill (remove voxels)
  start_time = get_time_ms();
  success = world_fill_region(world, 10, 10, 10, 12, 12, 12, VOXEL_AIR, BULK_OP_SUBTRACTIVE, NULL, NULL);
  end_time = get_time_ms();

  printf("Subtractive region fill: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  world_destroy(world);
}

// Test shape-based operations
static void test_shape_operations(void)
{
  printf("\n=== Testing Shape Operations ===\n");

  World *world = world_create(TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  if (!world)
  {
    printf("Failed to create test world\n");
    return;
  }

  // Test 1: Sphere fill
  double start_time = get_time_ms();
  bool success = world_fill_sphere(world, 16, 16, 16, 12, VOXEL_GRASS, 1.0f, BULK_OP_REPLACE, NULL, NULL);
  double end_time = get_time_ms();

  printf("Sphere fill: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  // Test 2: Cylinder fill
  start_time = get_time_ms();
  success = world_fill_cylinder(world, 16, 16, 8, 24, 8, VOXEL_WOOD, 1.0f, BULK_OP_REPLACE, NULL, NULL);
  end_time = get_time_ms();

  printf("Cylinder fill: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  // Test 3: Ellipsoid fill
  start_time = get_time_ms();
  success = world_fill_ellipsoid(world, 16, 16, 16, 10, 6, 10, VOXEL_WATER, 1.0f, BULK_OP_REPLACE, NULL, NULL);
  end_time = get_time_ms();

  printf("Ellipsoid fill: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  world_destroy(world);
}

// Test filtering operations
static void test_filtering_operations(void)
{
  printf("\n=== Testing Filtering Operations ===\n");

  World *world = world_create(TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  if (!world)
  {
    printf("Failed to create test world\n");
    return;
  }

  // Fill world with some content first
  world_fill_region(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE, VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
  world_fill_region(world, 8, 8, 8, 16, 16, 16, VOXEL_AIR, BULK_OP_REPLACE, NULL, NULL);

  // Test 1: Count voxels with type filter
  VoxelType target_type = VOXEL_STONE;
  double start_time = get_time_ms();
  uint32_t count = world_count_filtered_voxels(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE,
                                               voxel_filter_type, &target_type);
  double end_time = get_time_ms();

  printf("Type filter count: %u stone voxels (%.2f ms)\n", count, end_time - start_time);

  // Test 2: Height range filter
  uint32_t height_range[2] = {0, 15};
  start_time = get_time_ms();
  count = world_count_filtered_voxels(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE,
                                      voxel_filter_height_range, height_range);
  end_time = get_time_ms();

  printf("Height filter count: %u voxels in range 0-15 (%.2f ms)\n", count, end_time - start_time);

  // Test 3: Noise-based filter
  float noise_params[3] = {1.0f, 0.5f, 123.0f};
  start_time = get_time_ms();
  count = world_count_filtered_voxels(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE,
                                      voxel_filter_noise_based, noise_params);
  end_time = get_time_ms();

  printf("Noise filter count: %u voxels above threshold (%.2f ms)\n", count, end_time - start_time);

  world_destroy(world);
}

// Test composite filters
static void test_composite_filters(void)
{
  printf("\n=== Testing Composite Filters ===\n");

  World *world = world_create(TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  if (!world)
  {
    printf("Failed to create test world\n");
    return;
  }

  // Fill world with some content
  world_fill_region(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE, VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
  world_fill_region(world, 8, 8, 8, 16, 16, 16, VOXEL_AIR, BULK_OP_REPLACE, NULL, NULL);

  // Create AND composite filter (must be stone AND in height range 0-15)
  CompositeFilter *and_filter = composite_filter_create(true);
  composite_filter_add(and_filter, filter_stone_match);
  composite_filter_add(and_filter, filter_height_match);

  CompositeTestParams params = {.stone = VOXEL_STONE, .h_min = 0, .h_max = 15};
  CompositeWrapCtx and_ctx = {.cf = and_filter, .params = &params};

  // Test AND filter
  double start_time = get_time_ms();
  uint32_t count = world_count_filtered_voxels(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE,
                                               composite_wrap_voxel_filter, &and_ctx);
  double end_time = get_time_ms();

  printf("AND composite filter count: %u voxels (%.2f ms)\n", count, end_time - start_time);

  // Create OR composite filter (must be stone OR in height range 0-15)
  CompositeFilter *or_filter = composite_filter_create(false);
  composite_filter_add(or_filter, filter_stone_match);
  composite_filter_add(or_filter, filter_height_match);

  CompositeWrapCtx or_ctx = {.cf = or_filter, .params = &params};

  // Test OR filter
  start_time = get_time_ms();
  count = world_count_filtered_voxels(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE,
                                      composite_wrap_voxel_filter, &or_ctx);
  end_time = get_time_ms();

  printf("OR composite filter count: %u voxels (%.2f ms)\n", count, end_time - start_time);

  // Cleanup
  composite_filter_destroy(and_filter);
  composite_filter_destroy(or_filter);
  world_destroy(world);
}

// Test world merging
static void test_world_merging(void)
{
  printf("\n=== Testing World Merging ===\n");

  World *target = world_create(TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  World *source = world_create(16, 16, 16);

  if (!target || !source)
  {
    printf("Failed to create test worlds\n");
    if (target)
      world_destroy(target);
    if (source)
      world_destroy(source);
    return;
  }

  // Fill source world with a pattern
  world_fill_region(source, 0, 0, 0, 16, 16, 16, VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
  world_fill_region(source, 4, 4, 4, 8, 8, 8, VOXEL_GRASS, BULK_OP_REPLACE, NULL, NULL);

  // Test 1: Simple merge
  double start_time = get_time_ms();
  bool success = world_merge_worlds(target, source, 8, 8, 8, BULK_OP_REPLACE, NULL, NULL);
  double end_time = get_time_ms();

  printf("Simple world merge: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  // Test 2: Additive merge
  start_time = get_time_ms();
  success = world_merge_worlds(target, source, 0, 0, 0, BULK_OP_ADDITIVE, NULL, NULL);
  end_time = get_time_ms();

  printf("Additive world merge: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  // Test 3: Filtered merge
  VoxelType grass_type = VOXEL_GRASS;
  start_time = get_time_ms();
  success = world_merge_worlds(target, source, 4, 4, 4, BULK_OP_MASKED, voxel_filter_type, &grass_type);
  end_time = get_time_ms();

  printf("Filtered world merge: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  world_destroy(target);
  world_destroy(source);
}

// Test pattern operations
static void test_pattern_operations(void)
{
  printf("\n=== Testing Pattern Operations ===\n");

  World *world = world_create(TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  if (!world)
  {
    printf("Failed to create test world\n");
    return;
  }

  // Test 1: Noise pattern
  double start_time = get_time_ms();
  bool success = world_apply_noise_pattern(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE,
                                           VOXEL_CRYSTAL, 1.0f, 0.7f, BULK_OP_REPLACE, NULL, NULL);
  double end_time = get_time_ms();

  printf("Noise pattern: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  // Test 2: Noise pattern with filter (only on stone)
  VoxelType stone_type = VOXEL_STONE;
  // Ensure region is stone so masked writes have eligible voxels
  world_fill_region(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE, VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
  start_time = get_time_ms();
  success = world_apply_noise_pattern(world, 0, 0, 0, TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE,
                                      VOXEL_ORE_COPPER, 1.0f, 0.8f, BULK_OP_MASKED, voxel_filter_type, &stone_type);
  end_time = get_time_ms();

  printf("Filtered noise pattern: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  world_destroy(world);
}

// Test performance optimization
static void test_performance_optimization(void)
{
  printf("\n=== Testing Performance Optimization ===\n");

  World *world = world_create(TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  if (!world)
  {
    printf("Failed to create test world\n");
    return;
  }

  // Test batch operations
  BulkOperationBatch *batch = bulk_operation_batch_create(world, NULL, NULL, BULK_OP_REPLACE, 1000);
  if (!batch)
  {
    printf("Failed to create batch\n");
    world_destroy(world);
    return;
  }

  double start_time = get_time_ms();

  // Add many operations
  for (int i = 0; i < 1000; i++)
  {
    uint32_t x = (i * 7) % TEST_WORLD_SIZE;
    uint32_t y = (i * 11) % TEST_WORLD_SIZE;
    uint32_t z = (i * 13) % TEST_WORLD_SIZE;
    VoxelType type = (VoxelType)((i % 5) + 1); // Vary types

    bulk_operation_batch_add_operation(batch, x, y, z, type);
  }

  bool success = bulk_operation_batch_execute(batch);
  double end_time = get_time_ms();

  printf("Batch operations: %s (%.2f ms)\n", outcome(success), end_time - start_time);

  bulk_operation_batch_destroy(batch);
  world_destroy(world);
}

// Benchmark comparison with existing functions
static void benchmark_comparison(void)
{
  printf("\n=== Benchmark Comparison ===\n");

  World *world = world_create(TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  if (!world)
  {
    printf("Failed to create test world\n");
    return;
  }

  // Test 1: Existing world_set_voxel approach
  double start_time = get_time_ms();
  for (int i = 0; i < TEST_ITERATIONS; i++)
  {
    for (uint32_t z = 8; z < 24; z++)
    {
      for (uint32_t y = 8; y < 24; y++)
      {
        for (uint32_t x = 8; x < 24; x++)
        {
          world_set_voxel(world, x, y, z, VOXEL_STONE);
        }
      }
    }
  }
  double end_time = get_time_ms();

  printf("Existing world_set_voxel: %.2f ms\n", end_time - start_time);

  // Test 2: New bulk operation approach
  start_time = get_time_ms();
  for (int i = 0; i < TEST_ITERATIONS; i++)
  {
    world_fill_region(world, 8, 8, 8, 16, 16, 16, VOXEL_SOIL, BULK_OP_REPLACE, NULL, NULL);
  }
  end_time = get_time_ms();

  printf("New bulk operation: %.2f ms\n", end_time - start_time);

  // Test 3: Filtered bulk operation
  VoxelType air_type = VOXEL_AIR;
  start_time = get_time_ms();
  for (int i = 0; i < TEST_ITERATIONS; i++)
  {
    world_fill_region(world, 8, 8, 8, 16, 16, 16, VOXEL_WATER, BULK_OP_MASKED, voxel_filter_type, &air_type);
  }
  end_time = get_time_ms();

  printf("Filtered bulk operation: %.2f ms\n", end_time - start_time);

  world_destroy(world);
}

int main(void)
{
  printf("World Bulk Operations Test Suite\n");
  printf("================================\n");
  printf("World Size: %dx%dx%d\n", TEST_WORLD_SIZE, TEST_WORLD_SIZE, TEST_WORLD_SIZE);
  printf("Test Iterations: %d\n", TEST_ITERATIONS);

  // Run all tests
  test_region_filling();
  test_shape_operations();
  test_filtering_operations();
  test_composite_filters();
  test_world_merging();
  test_pattern_operations();
  test_performance_optimization();
  benchmark_comparison();

  if (test_failures > 0)
  {
    printf("\n=== %d operation(s) FAILED ===\n", test_failures);
    return 1;
  }

  printf("\n=== All Tests Complete ===\n");
  return 0;
}
