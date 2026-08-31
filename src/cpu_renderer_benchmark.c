#include "cpu_renderer_optimized.h"
#include "isometric_renderer.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// High-resolution timer for performance measurement
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Convert nanoseconds to milliseconds
static double ns_to_ms(uint64_t ns) {
    return (double)ns / 1000000.0;
}

// Convert nanoseconds to FPS
static double ns_to_fps(uint64_t ns) {
    return ns > 0 ? 1000000000.0 / (double)ns : 0.0;
}

// Create a test world with various voxel types
static World* create_test_world(int width, int height, int depth) {
    World* world = world_create(width, height, depth);
    if (!world) return NULL;

    // Fill with a pattern of different voxel types
    for (uint32_t z = 0; z < world->depth; z++) {
        for (uint32_t y = 0; y < world->height; y++) {
            for (uint32_t x = 0; x < world->width; x++) {
                VoxelType type = VOXEL_AIR;

                // Create a varied pattern
                if (z < depth / 4) {
                    type = VOXEL_GRASS;
                } else if (z < depth / 2) {
                    type = VOXEL_SOIL;
                } else if (z < 3 * depth / 4) {
                    type = VOXEL_STONE;
                } else {
                    type = VOXEL_WOOD;
                }

                // Add some variation
                if ((x + y + z) % 7 == 0) {
                    type = VOXEL_WATER;
                } else if ((x + y + z) % 11 == 0) {
                    type = VOXEL_SAND;
                } else if ((x + y + z) % 13 == 0) {
                    type = VOXEL_LEAVES;
                }

                // Set voxel
                Voxel* voxel = world_voxel_ptr_fast(world, x, y, z);
                if (voxel) {
                    voxel->type = type;
                }
            }
        }
    }

    return world;
}

// Benchmark a single optimization configuration
static void benchmark_optimization(const char* name,
                                 CPURendererConfig* config,
                                 IsometricRenderer* renderer,
                                 SDL_Renderer* sdl_renderer,
                                 int frames) {
    printf("\n=== Benchmarking: %s ===\n", name);
    printf("Optimizations enabled: ");

    bool has_opt = false;
    if (config->flags & CPU_RENDER_OPT_CHUNK_CULLING) {
        printf("ChunkCulling ");
        has_opt = true;
    }
    if (config->flags & CPU_RENDER_OPT_OCCUPANCY_BITS) {
        printf("OccupancyBits ");
        has_opt = true;
    }
    if (config->flags & CPU_RENDER_OPT_SCREEN_CULLING) {
        printf("ScreenCulling ");
        has_opt = true;
    }
    if (config->flags & CPU_RENDER_OPT_BATCH_RENDERING) {
        printf("BatchRendering ");
        has_opt = true;
    }
    if (config->flags & CPU_RENDER_OPT_COLOR_TABLE) {
        printf("ColorTable ");
        has_opt = true;
    }
    if (config->flags & CPU_RENDER_OPT_LOOP_UNROLLING) {
        printf("LoopUnrolling ");
        has_opt = true;
    }
    if (config->flags & CPU_RENDER_OPT_MEMORY_ALIGNMENT) {
        printf("MemoryAlignment ");
        has_opt = true;
    }
    if (config->flags & CPU_RENDER_OPT_SIMD_HINTS) {
        printf("SIMDHints ");
        has_opt = true;
    }
    if (config->flags & CPU_RENDER_OPT_CACHE_OPTIMIZATION) {
        printf("CacheOptimization ");
        has_opt = true;
    }

    if (!has_opt) {
        printf("None");
    }
    printf("\n");

    // Warm up
    for (int i = 0; i < 5; i++) {
        cpu_renderer_render_optimized(renderer, sdl_renderer, config);
    }

    // Benchmark
    uint64_t total_time = 0;
    uint64_t min_time = UINT64_MAX;
    uint64_t max_time = 0;

    for (int i = 0; i < frames; i++) {
        uint64_t start = get_time_ns();
        cpu_renderer_render_optimized(renderer, sdl_renderer, config);
        uint64_t end = get_time_ns();

        uint64_t frame_time = end - start;
        total_time += frame_time;

        if (frame_time < min_time) min_time = frame_time;
        if (frame_time > max_time) max_time = frame_time;

        // Progress indicator
        if ((i + 1) % (frames / 10) == 0) {
            printf("  Frame %d/%d\n", i + 1, frames);
        }
    }

    // Calculate statistics
    double avg_time_ms = ns_to_ms(total_time / frames);
    double min_time_ms = ns_to_ms(min_time);
    double max_time_ms = ns_to_ms(max_time);
    double avg_fps = ns_to_fps(total_time / frames);

    printf("Results:\n");
    printf("  Frames rendered: %d\n", frames);
    printf("  Total time: %.2f ms\n", ns_to_ms(total_time));
    printf("  Average frame time: %.2f ms\n", avg_time_ms);
    printf("  Min frame time: %.2f ms\n", min_time_ms);
    printf("  Max frame time: %.2f ms\n", max_time_ms);
    printf("  Average FPS: %.2f\n", avg_fps);

    // Get CPU renderer statistics
    CPURendererStats stats = cpu_renderer_get_stats();
    printf("  Voxels processed: %lu\n", stats.voxels_processed);
    printf("  Voxels culled: %lu\n", stats.voxels_culled);
    printf("  Voxels rendered: %lu\n", stats.voxels_rendered);
    printf("  Chunks processed: %lu\n", stats.chunks_processed);
    printf("  Chunks culled: %lu\n", stats.chunks_culled);
    printf("  Culling efficiency: %.1f%%\n",
           stats.voxels_processed > 0 ?
           (100.0 * stats.voxels_culled) / stats.voxels_processed : 0.0);

    // Reset stats for next test
    cpu_renderer_reset_stats();
}

// Main benchmark function
int main(int argc, char* argv[]) {
    printf("=== CPU Renderer Optimization Benchmark ===\n");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create a hidden window for rendering
    SDL_Window* window = SDL_CreateWindow("Benchmark",
                                         SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                         1024, 768, SDL_WINDOW_HIDDEN);
    if (!window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create test world
    printf("Creating test world (64x64x64)...\n");
    World* test_world = create_test_world(64, 64, 64);
    if (!test_world) {
        printf("Failed to create test world\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create isometric renderer
    IsometricRenderer* iso_renderer = isometric_renderer_create(1024, 768);
    if (!iso_renderer) {
        printf("Failed to create isometric renderer\n");
        world_destroy(test_world);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Set up the test world
    GameWorlds game_worlds = {0};
    game_worlds.home_world = test_world;
    iso_renderer->game_worlds = &game_worlds;
    iso_renderer->camera_z = 32; // Middle of the world

    // Initialize CPU renderer
    cpu_renderer_init_color_table();

    // Number of frames to render for each test
    int frames = 100;
    if (argc > 1) {
        frames = atoi(argv[1]);
    }

    printf("Benchmarking %d frames per optimization...\n", frames);

    // Test 1: No optimizations (baseline)
    CPURendererConfig config = {
        .flags = CPU_RENDER_OPT_NONE,
        .batch_size = 1024,
        .chunk_size_x = 16,
        .chunk_size_y = 16,
        .chunk_size_z = 256,
        .enable_grid = false,
        .enable_debug_overlay = false,
        .max_visible_voxels = 100000,
        .culling_margin = 32.0f
    };
    benchmark_optimization("No Optimizations (Baseline)", &config, iso_renderer, renderer, frames);

    // Test 2: Chunk culling only
    config.flags = CPU_RENDER_OPT_CHUNK_CULLING;
    benchmark_optimization("Chunk Culling Only", &config, iso_renderer, renderer, frames);

    // Test 3: Add screen culling
    config.flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING;
    benchmark_optimization("Chunk + Screen Culling", &config, iso_renderer, renderer, frames);

    // Test 4: Add color table
    config.flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING | CPU_RENDER_OPT_COLOR_TABLE;
    benchmark_optimization("Chunk + Screen + Color Table", &config, iso_renderer, renderer, frames);

    // Test 5: Add batch rendering
    config.flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING |
                   CPU_RENDER_OPT_COLOR_TABLE | CPU_RENDER_OPT_BATCH_RENDERING;
    benchmark_optimization("Chunk + Screen + Color + Batch", &config, iso_renderer, renderer, frames);

    // Test 6: Add loop unrolling
    config.flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING |
                   CPU_RENDER_OPT_COLOR_TABLE | CPU_RENDER_OPT_BATCH_RENDERING |
                   CPU_RENDER_OPT_LOOP_UNROLLING;
    benchmark_optimization("Chunk + Screen + Color + Batch + Loop Unroll", &config, iso_renderer, renderer, frames);

    // Test 7: Add memory alignment
    config.flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING |
                   CPU_RENDER_OPT_COLOR_TABLE | CPU_RENDER_OPT_BATCH_RENDERING |
                   CPU_RENDER_OPT_LOOP_UNROLLING | CPU_RENDER_OPT_MEMORY_ALIGNMENT;
    benchmark_optimization("Chunk + Screen + Color + Batch + Loop Unroll + Memory Align", &config, iso_renderer, renderer, frames);

    // Test 8: Add SIMD hints
    config.flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING |
                   CPU_RENDER_OPT_COLOR_TABLE | CPU_RENDER_OPT_BATCH_RENDERING |
                   CPU_RENDER_OPT_LOOP_UNROLLING | CPU_RENDER_OPT_MEMORY_ALIGNMENT |
                   CPU_RENDER_OPT_SIMD_HINTS;
    benchmark_optimization("Chunk + Screen + Color + Batch + Loop Unroll + Memory Align + SIMD", &config, iso_renderer, renderer, frames);

    // Test 9: Add cache optimization
    config.flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING |
                   CPU_RENDER_OPT_COLOR_TABLE | CPU_RENDER_OPT_BATCH_RENDERING |
                   CPU_RENDER_OPT_LOOP_UNROLLING | CPU_RENDER_OPT_MEMORY_ALIGNMENT |
                   CPU_RENDER_OPT_SIMD_HINTS | CPU_RENDER_OPT_CACHE_OPTIMIZATION;
    benchmark_optimization("Chunk + Screen + Color + Batch + Loop Unroll + Memory Align + SIMD + Cache", &config, iso_renderer, renderer, frames);

    // Test 10: All optimizations
    config.flags = CPU_RENDER_OPT_ALL;
    benchmark_optimization("All Optimizations", &config, iso_renderer, renderer, frames);

    // Test 11: Different batch sizes
    printf("\n=== Batch Size Impact Test ===\n");
    config.flags = CPU_RENDER_OPT_ALL;

    int batch_sizes[] = {64, 128, 256, 512, 1024, 2048, 4096};
    for (int i = 0; i < 7; i++) {
        config.batch_size = batch_sizes[i];
        char name[128];
        snprintf(name, sizeof(name), "All Opts + Batch Size %d", batch_sizes[i]);
        benchmark_optimization(name, &config, iso_renderer, renderer, frames / 2);
    }

    // Test 12: Different chunk sizes
    printf("\n=== Chunk Size Impact Test ===\n");
    config.flags = CPU_RENDER_OPT_ALL;
    config.batch_size = 1024;

    int chunk_sizes[] = {8, 16, 32, 64};
    for (int i = 0; i < 4; i++) {
        config.chunk_size_x = chunk_sizes[i];
        config.chunk_size_y = chunk_sizes[i];
        config.chunk_size_z = 256; // Keep Z chunks at full height for Minecraft compatibility
        char name[128];
        snprintf(name, sizeof(name), "All Opts + Chunk Size %dx%dx256", chunk_sizes[i], chunk_sizes[i]);
        benchmark_optimization(name, &config, iso_renderer, renderer, frames / 2);
    }

    printf("\n=== Benchmark Complete ===\n");
    printf("Results show the performance impact of each optimization technique.\n");
    printf("Look for:\n");
    printf("  - FPS improvements\n");
    printf("  - Culling efficiency gains\n");
    printf("  - Optimal batch and chunk sizes\n");

    // Cleanup
    isometric_renderer_destroy(iso_renderer);
    world_destroy(test_world);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
