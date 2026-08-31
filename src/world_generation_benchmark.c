#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "world.h"

// Benchmark configuration
#define BENCHMARK_ITERATIONS 5
#define BENCHMARK_WORLD_SIZE 64 // 64x64x64 for reasonable test size
#define BENCHMARK_SEED "benchmark_test_seed_12345"

// Timing utilities
static double get_time_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

// Composition analysis utilities
static void analyze_world_composition(World *world)
{
  if (!world)
    return;

  printf("\nWorld Composition Analysis:\n");
  printf("===========================\n");
  printf("World Size: %dx%dx%d (%d total voxels)\n",
         world->width, world->height, world->depth,
         world->width * world->height * world->depth);

  // Count voxel types
  uint64_t voxel_counts[VOXEL_COUNT] = {0};
  uint64_t total_voxels = 0;

  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t y = 0; y < world->height; y++)
    {
      for (uint32_t x = 0; x < world->width; x++)
      {
        Voxel *voxel = world_get_voxel(world, x, y, z);
        if (voxel)
        {
          voxel_counts[voxel->type]++;
          total_voxels++;
        }
      }
    }
  }

  // Print composition report
  printf("\nVoxel Type Distribution:\n");
  printf("------------------------\n");

  // Group by category for better readability
  const char *categories[] = {
      "Air & Water", "Stone Types", "Soil Types", "Ore Types",
      "Crystal Types", "Magma", "Other"};

  // Air & Water
  printf("  %-20s: %8lu (%5.1f%%)\n", "Air", voxel_counts[VOXEL_AIR],
         (double)voxel_counts[VOXEL_AIR] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Water", voxel_counts[VOXEL_WATER],
         (double)voxel_counts[VOXEL_WATER] / total_voxels * 100.0);

  // Stone Types
  printf("\n  Stone Types:\n");
  printf("  %-20s: %8lu (%5.1f%%)\n", "Bedrock", voxel_counts[VOXEL_BEDROCK],
         (double)voxel_counts[VOXEL_BEDROCK] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Basalt", voxel_counts[VOXEL_STONE_BASALT],
         (double)voxel_counts[VOXEL_STONE_BASALT] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Granite", voxel_counts[VOXEL_STONE_GRANITE],
         (double)voxel_counts[VOXEL_STONE_GRANITE] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Limestone", voxel_counts[VOXEL_STONE_LIMESTONE],
         (double)voxel_counts[VOXEL_STONE_LIMESTONE] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Sandstone", voxel_counts[VOXEL_STONE_SANDSTONE],
         (double)voxel_counts[VOXEL_STONE_SANDSTONE] / total_voxels * 100.0);

  // Ore Types
  printf("\n  Ore Types:\n");
  uint64_t total_ores = 0;
  total_ores += voxel_counts[VOXEL_ORE_IRON];
  total_ores += voxel_counts[VOXEL_ORE_COPPER];
  total_ores += voxel_counts[VOXEL_ORE_SILVER];
  total_ores += voxel_counts[VOXEL_ORE_GOLD];
  total_ores += voxel_counts[VOXEL_ORE_COAL];

  printf("  %-20s: %8lu (%5.1f%%)\n", "Iron Ore", voxel_counts[VOXEL_ORE_IRON],
         (double)voxel_counts[VOXEL_ORE_IRON] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Copper Ore", voxel_counts[VOXEL_ORE_COPPER],
         (double)voxel_counts[VOXEL_ORE_COPPER] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Silver Ore", voxel_counts[VOXEL_ORE_SILVER],
         (double)voxel_counts[VOXEL_ORE_SILVER] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Gold Ore", voxel_counts[VOXEL_ORE_GOLD],
         (double)voxel_counts[VOXEL_ORE_GOLD] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Coal", voxel_counts[VOXEL_ORE_COAL],
         (double)voxel_counts[VOXEL_ORE_COAL] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Total Ores", total_ores,
         (double)total_ores / total_voxels * 100.0);

  // Crystal Types
  printf("\n  Crystal Types:\n");
  uint64_t total_crystals = 0;
  total_crystals += voxel_counts[VOXEL_CRYSTAL_RED];
  total_crystals += voxel_counts[VOXEL_CRYSTAL_GREEN];
  total_crystals += voxel_counts[VOXEL_CRYSTAL_BLUE];

  printf("  %-20s: %8lu (%5.1f%%)\n", "Red Crystal", voxel_counts[VOXEL_CRYSTAL_RED],
         (double)voxel_counts[VOXEL_CRYSTAL_RED] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Green Crystal", voxel_counts[VOXEL_CRYSTAL_GREEN],
         (double)voxel_counts[VOXEL_CRYSTAL_GREEN] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Blue Crystal", voxel_counts[VOXEL_CRYSTAL_BLUE],
         (double)voxel_counts[VOXEL_CRYSTAL_BLUE] / total_voxels * 100.0);
  printf("  %-20s: %8lu (%5.1f%%)\n", "Total Crystals", total_crystals,
         (double)total_crystals / total_voxels * 100.0);

  // Magma
  printf("\n  %-20s: %8lu (%5.1f%%)\n", "Magma", voxel_counts[VOXEL_MAGMA],
         (double)voxel_counts[VOXEL_MAGMA] / total_voxels * 100.0);

  // Summary
  uint64_t solid_voxels = total_voxels - voxel_counts[VOXEL_AIR];
  printf("\nSummary:\n");
  printf("--------\n");
  printf("  Total Voxels     : %8lu (100.0%%)\n", total_voxels);
  printf("  Air Voxels       : %8lu (%5.1f%%)\n", voxel_counts[VOXEL_AIR],
         (double)voxel_counts[VOXEL_AIR] / total_voxels * 100.0);
  printf("  Solid Voxels     : %8lu (%5.1f%%)\n", solid_voxels,
         (double)solid_voxels / total_voxels * 100.0);
  printf("  Ore Density      : %8lu ores per 1000 solid voxels\n",
         (total_ores * 1000) / (solid_voxels > 0 ? solid_voxels : 1));
  printf("  Crystal Density  : %8lu crystals per 1000 solid voxels\n",
         (total_crystals * 1000) / (solid_voxels > 0 ? solid_voxels : 1));
}

static void print_timing_stats(const char *test_name, double *times, int count)
{
  if (count <= 0)
    return;

  // Calculate statistics
  double min_time = times[0];
  double max_time = times[0];
  double total_time = times[0];

  for (int i = 1; i < count; i++)
  {
    if (times[i] < min_time)
      min_time = times[i];
    if (times[i] > max_time)
      max_time = times[i];
    total_time += times[i];
  }

  double avg_time = total_time / count;
  double variance = 0.0;
  for (int i = 0; i < count; i++)
  {
    double diff = times[i] - avg_time;
    variance += diff * diff;
  }
  variance /= count;
  double std_dev = sqrt(variance);

  printf("  %-20s: %6.2f ± %5.2f ms (min: %6.2f, max: %6.2f)\n",
         test_name, avg_time, std_dev, min_time, max_time);
}

// Global CSV output flag
static bool g_csv_output = false;

// Benchmark individual world type
static void benchmark_world_type(WorldGenerationType type, const char *type_name, const char *seed)
{
  if (!g_csv_output)
  {
    printf("\nBenchmarking %s:\n", type_name);
  }

  double generation_times[BENCHMARK_ITERATIONS];
  double memory_usage[BENCHMARK_ITERATIONS];

  for (int i = 0; i < BENCHMARK_ITERATIONS; i++)
  {
    // Create world
    World *world = world_create(BENCHMARK_WORLD_SIZE, BENCHMARK_WORLD_SIZE, BENCHMARK_WORLD_SIZE);
    if (!world)
    {
      if (!g_csv_output)
      {
        printf("  Failed to create world for iteration %d\n", i);
      }
      continue;
    }

    // Measure generation time
    double start_time = get_time_ms();
    world_generate_with_type(world, seed, type);
    double end_time = get_time_ms();

    generation_times[i] = end_time - start_time;

    // Calculate memory usage (rough estimate)
    size_t voxel_count = BENCHMARK_WORLD_SIZE * BENCHMARK_WORLD_SIZE * BENCHMARK_WORLD_SIZE;
    size_t voxel_memory = voxel_count * sizeof(Voxel);
    size_t world_memory = sizeof(World) + voxel_memory;
    memory_usage[i] = (double)world_memory / (1024.0 * 1024.0); // Convert to MB

    // Cleanup
    world_destroy(world);

    // CSV output
    if (g_csv_output)
    {
      printf("%s,%d,%.2f,%.2f\n", type_name, i, generation_times[i], memory_usage[i]);
    }
  }

  // Print results (non-CSV mode)
  if (!g_csv_output)
  {
    print_timing_stats("Generation", generation_times, BENCHMARK_ITERATIONS);

    double avg_memory = 0.0;
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++)
    {
      avg_memory += memory_usage[i];
    }
    avg_memory /= BENCHMARK_ITERATIONS;
    printf("  Memory Usage        : %6.2f MB\n", avg_memory);
  }
}

// Benchmark world creation and destruction
static void benchmark_world_creation(void)
{
  if (!g_csv_output)
  {
    printf("\nBenchmarking World Creation/Destruction:\n");
  }

  double creation_times[BENCHMARK_ITERATIONS];
  double destruction_times[BENCHMARK_ITERATIONS];

  for (int i = 0; i < BENCHMARK_ITERATIONS; i++)
  {
    // Measure creation time
    double start_time = get_time_ms();
    World *world = world_create(BENCHMARK_WORLD_SIZE, BENCHMARK_WORLD_SIZE, BENCHMARK_WORLD_SIZE);
    double end_time = get_time_ms();

    if (world)
    {
      creation_times[i] = end_time - start_time;

      // Measure destruction time
      start_time = get_time_ms();
      world_destroy(world);
      end_time = get_time_ms();

      destruction_times[i] = end_time - start_time;
    }
    else
    {
      creation_times[i] = 0.0;
      destruction_times[i] = 0.0;
    }
  }

  if (!g_csv_output)
  {
    print_timing_stats("Creation", creation_times, BENCHMARK_ITERATIONS);
    print_timing_stats("Destruction", destruction_times, BENCHMARK_ITERATIONS);
  }
}

// Benchmark seed generation
static void benchmark_seed_generation(void)
{
  if (!g_csv_output)
  {
    printf("\nBenchmarking Seed Generation:\n");
  }

  double hash_times[BENCHMARK_ITERATIONS];
  char test_seeds[BENCHMARK_ITERATIONS][65];

  for (int i = 0; i < BENCHMARK_ITERATIONS; i++)
  {
    // Generate test seed
    snprintf(test_seeds[i], sizeof(test_seeds[i]), "test_seed_%d_%ld", i, time(NULL));

    // Measure string processing time (simulating seed preparation)
    double start_time = get_time_ms();
    char processed_seed[128];
    snprintf(processed_seed, sizeof(processed_seed), "processed_%s", test_seeds[i]);
    // Simulate some processing time
    size_t seed_len = strlen(processed_seed);
    if (seed_len > 0)
    {
      for (int j = 0; j < 1000; j++)
      {
        processed_seed[j % seed_len] = (char)(j % 128);
      }
    }
    double end_time = get_time_ms();

    hash_times[i] = end_time - start_time;
  }

  if (!g_csv_output)
  {
    print_timing_stats("Seed Processing", hash_times, BENCHMARK_ITERATIONS);
  }
}

// Detailed Wilderness generation benchmark with stage timing
static void benchmark_wilderness_detailed(const char *seed, int iterations, int world_size)
{
  printf("\nDetailed Wilderness Generation Benchmark:\n");
  printf("==========================================\n");
  printf("World Size: %dx%dx%d\n", world_size, world_size, world_size);
  printf("Iterations: %d\n", iterations);
  printf("Seed: %s\n", seed);
  printf("Date: %s", ctime(&(time_t){time(NULL)}));

  // Stage timing arrays
  double scoured_times[iterations];
  double wilderness_times[iterations];
  double total_times[iterations];

  for (int i = 0; i < iterations; i++)
  {
    double total_start = get_time_ms();

    // Stage 1: Scoured Base Generation (for comparison)
    World *world1 = world_create(world_size, world_size, world_size);
    if (!world1)
    {
      printf("  Failed to create world for iteration %d\n", i);
      continue;
    }

    double stage1_start = get_time_ms();
    world_generate_with_type(world1, seed, WORLD_TYPE_SCOURED);
    double stage1_end = get_time_ms();
    scoured_times[i] = stage1_end - stage1_start;
    world_destroy(world1);

    // Stage 2: Full Wilderness Generation
    World *world2 = world_create(world_size, world_size, world_size);
    if (!world2)
    {
      printf("  Failed to create world for iteration %d\n", i);
      continue;
    }

    double stage2_start = get_time_ms();
    world_generate_with_type(world2, seed, WORLD_TYPE_WILDERNESS);
    double stage2_end = get_time_ms();
    wilderness_times[i] = stage2_end - stage2_start;
    world_destroy(world2);

    double total_end = get_time_ms();
    total_times[i] = total_end - total_start;
  }

  // Print detailed results
  printf("\nStage Timing Results:\n");
  printf("--------------------\n");
  print_timing_stats("1. Scoured Base", scoured_times, iterations);
  print_timing_stats("2. Wilderness Total", wilderness_times, iterations);
  print_timing_stats("Total Generation", total_times, iterations);

  // Calculate percentages and additional analysis
  double avg_scoured = 0, avg_wilderness = 0, avg_total = 0;
  for (int i = 0; i < iterations; i++)
  {
    avg_scoured += scoured_times[i];
    avg_wilderness += wilderness_times[i];
    avg_total += total_times[i];
  }
  avg_scoured /= iterations;
  avg_wilderness /= iterations;
  avg_total /= iterations;

  double wilderness_overhead = avg_wilderness - avg_scoured;

  printf("\nStage Breakdown (%% of total time):\n");
  printf("----------------------------------\n");
  printf("  Scoured Base     : %6.2f ms (%5.1f%%)\n", avg_scoured, (avg_scoured / avg_total) * 100);
  printf("  Wilderness Total : %6.2f ms (%5.1f%%)\n", avg_wilderness, (avg_wilderness / avg_total) * 100);
  printf("  Wilderness Overhead: %6.2f ms (%5.1f%%)\n", wilderness_overhead, (wilderness_overhead / avg_total) * 100);
  printf("  Total            : %6.2f ms (100.0%%)\n", avg_total);

  printf("\nPerformance Analysis:\n");
  printf("---------------------\n");
  printf("  Wilderness is %.1fx slower than Scoured\n", avg_wilderness / avg_scoured);
  printf("  Wilderness overhead: %.1f%% of total time\n", (wilderness_overhead / avg_total) * 100);
  printf("  Estimated strata time: %.1f%% (major bottleneck)\n", (wilderness_overhead / avg_wilderness) * 100);

  // Generate a final world for composition analysis
  printf("\nGenerating world for composition analysis...\n");
  World *analysis_world = world_create(world_size, world_size, world_size);
  if (analysis_world)
  {
    world_generate_with_type(analysis_world, seed, WORLD_TYPE_WILDERNESS);
    analyze_world_composition(analysis_world);
    world_destroy(analysis_world);
  }
  else
  {
    printf("Failed to create world for composition analysis\n");
  }
}

// Main benchmark function
static void run_all_benchmarks(void)
{
  if (!g_csv_output)
  {
    printf("World Generation Benchmark Suite\n");
    printf("================================\n");
    printf("World Size: %dx%dx%d\n", BENCHMARK_WORLD_SIZE, BENCHMARK_WORLD_SIZE, BENCHMARK_WORLD_SIZE);
    printf("Iterations: %d\n", BENCHMARK_ITERATIONS);
    printf("Seed: %s\n", BENCHMARK_SEED);
    printf("Date: %s", ctime(&(time_t){time(NULL)}));

    // Benchmark world creation/destruction
    benchmark_world_creation();

    // Benchmark seed generation
    benchmark_seed_generation();
  }

  // Benchmark each world type
  benchmark_world_type(WORLD_TYPE_HOME, "HOME", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_FARM, "FARM", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_RANDOM, "RANDOM", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_WILDERNESS, "WILDERNESS", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_SOLID, "SOLID", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_UNDERWORLD, "UNDERWORLD", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_SCOURED, "SCOURED", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_CLOUD, "CLOUD", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_ARENA, "ARENA", BENCHMARK_SEED);
  benchmark_world_type(WORLD_TYPE_LABYRINTH_SQUARE, "LABYRINTH_SQUARE", BENCHMARK_SEED);

  if (!g_csv_output)
  {
    printf("\nBenchmark Complete!\n");
  }
}

// Command line interface
static void print_usage(const char *program_name)
{
  printf("Usage: %s [options]\n", program_name);
  printf("Options:\n");
  printf("  --help              Show this help message\n");
  printf("  --iterations N      Number of benchmark iterations (default: %d)\n", BENCHMARK_ITERATIONS);
  printf("  --size N            World size for benchmark (default: %d)\n", BENCHMARK_WORLD_SIZE);
  printf("  --seed SEED         Seed to use for generation (default: %s)\n", BENCHMARK_SEED);
  printf("  --type TYPE         Benchmark only specific world type\n");
  printf("  --memory            Include detailed memory analysis\n");
  printf("  --csv               Output results in CSV format\n");
  printf("  --detailed-wilderness Run detailed Wilderness generation benchmark with stage timing\n");
}

int main(int argc, char *argv[])
{
  int iterations = BENCHMARK_ITERATIONS;
  int world_size = BENCHMARK_WORLD_SIZE;
  char *seed = BENCHMARK_SEED;
  char *specific_type = NULL;
  bool csv_output = false;
  bool memory_analysis = false;
  bool detailed_wilderness = false;

  // Parse command line arguments
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--help") == 0)
    {
      print_usage(argv[0]);
      return 0;
    }
    else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
    {
      iterations = atoi(argv[++i]);
    }
    else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
    {
      world_size = atoi(argv[++i]);
    }
    else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
    {
      seed = argv[++i];
    }
    else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc)
    {
      specific_type = argv[++i];
    }
    else if (strcmp(argv[i], "--csv") == 0)
    {
      csv_output = true;
    }
    else if (strcmp(argv[i], "--memory") == 0)
    {
      memory_analysis = true;
    }
    else if (strcmp(argv[i], "--detailed-wilderness") == 0)
    {
      detailed_wilderness = true;
    }
    else
    {
      printf("Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  // Validate parameters
  if (iterations <= 0)
  {
    printf("Error: iterations must be positive\n");
    return 1;
  }

  if (world_size <= 0)
  {
    printf("Error: world size must be positive\n");
    return 1;
  }

// Update global constants
#undef BENCHMARK_ITERATIONS
#undef BENCHMARK_WORLD_SIZE
#define BENCHMARK_ITERATIONS iterations
#define BENCHMARK_WORLD_SIZE world_size

  // Set global CSV flag
  g_csv_output = csv_output;

  if (csv_output)
  {
    printf("Type,Iteration,GenerationTime,MemoryUsage\n");
  }

  // Run detailed wilderness benchmark if requested
  if (detailed_wilderness)
  {
    benchmark_wilderness_detailed(seed, iterations, world_size);
    return 0;
  }

  // Run benchmarks
  if (specific_type)
  {
    // Benchmark only specific type
    WorldGenerationType type = WORLD_TYPE_HOME; // Default
    if (strcmp(specific_type, "HOME") == 0)
      type = WORLD_TYPE_HOME;
    else if (strcmp(specific_type, "FARM") == 0)
      type = WORLD_TYPE_FARM;
    else if (strcmp(specific_type, "RANDOM") == 0)
      type = WORLD_TYPE_RANDOM;
    else if (strcmp(specific_type, "WILDERNESS") == 0)
      type = WORLD_TYPE_WILDERNESS;
    else if (strcmp(specific_type, "SOLID") == 0)
      type = WORLD_TYPE_SOLID;
    else if (strcmp(specific_type, "UNDERWORLD") == 0)
      type = WORLD_TYPE_UNDERWORLD;
    else if (strcmp(specific_type, "SCOURED") == 0)
      type = WORLD_TYPE_SCOURED;
    else if (strcmp(specific_type, "CLOUD") == 0)
      type = WORLD_TYPE_CLOUD;
    else if (strcmp(specific_type, "ARENA") == 0)
      type = WORLD_TYPE_ARENA;
    else if (strcmp(specific_type, "LABYRINTH_SQUARE") == 0)
      type = WORLD_TYPE_LABYRINTH_SQUARE;
    else
    {
      printf("Unknown world type: %s\n", specific_type);
      return 1;
    }

    benchmark_world_type(type, specific_type, seed);
  }
  else
  {
    // Run all benchmarks
    run_all_benchmarks();
  }

  return 0;
}
