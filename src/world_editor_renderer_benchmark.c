#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "world.h"
#include "unified_renderer.h"
#include "isometric_renderer.h"
#include "cpu_renderer_optimized.h"
#include "constants.h"
#include "universe.h"

// Benchmark configuration
static int BENCHMARK_FRAMES = 1000;
static int BENCHMARK_WARMUP_FRAMES = 100;
static int WORLD_SIZE = 64;
static int SCREEN_WIDTH = 1024;
static int SCREEN_HEIGHT = 768;

// Global state
static SDL_Window* g_window = NULL;
static SDL_Renderer* g_sdl_renderer = NULL;
static IsometricRenderer* g_iso_renderer = NULL;
static UnifiedRenderer* g_unified_renderer = NULL;
static World* g_world = NULL;
static Universe g_universe;
static bool g_universe_initialized = false;

// Performance tracking
typedef struct {
    double total_time;
    double min_frame_time;
    double max_frame_time;
    double avg_frame_time;
    int frame_count;
    double fps;
} BenchmarkStats;

// Initialize SDL and create renderers
static int init_benchmark_environment(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 0;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "Failed to initialize TTF: %s\n", TTF_GetError());
        return 0;
    }

    // Create window
    g_window = SDL_CreateWindow("World Editor Renderer Benchmark",
                               SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               SCREEN_WIDTH, SCREEN_HEIGHT,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (!g_window) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        return 0;
    }

    // Create SDL renderer
    g_sdl_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_sdl_renderer) {
        fprintf(stderr, "Failed to create SDL renderer: %s\n", SDL_GetError());
        return 0;
    }

    // Create isometric renderer
    g_iso_renderer = isometric_renderer_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!g_iso_renderer) {
        fprintf(stderr, "Failed to create isometric renderer\n");
        return 0;
    }

    // Configure isometric renderer
    isometric_renderer_set_screen_size(g_iso_renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
    isometric_renderer_set_camera(g_iso_renderer, WORLD_SIZE/2, WORLD_SIZE/2, WORLD_SIZE/2);

    // Create unified renderer
    g_unified_renderer = unified_renderer_create();
    if (!g_unified_renderer) {
        fprintf(stderr, "Failed to create unified renderer\n");
        return 0;
    }

    // Auto-configure unified renderer
    unified_renderer_auto_configure(g_unified_renderer);

    return 1;
}

// Create test world
static int create_test_world(void) {
    // Create universe
    if (!universe_init(&g_universe, "12345", 0, 1)) {
        fprintf(stderr, "Failed to create universe\n");
        return 0;
    }
    g_universe_initialized = true;

    // Generate a wilderness world
    g_world = world_create(WORLD_SIZE, WORLD_SIZE, WORLD_SIZE);
    if (!g_world) {
        fprintf(stderr, "Failed to create world\n");
        return 0;
    }

    // Generate wilderness world
    world_generate_with_type(g_world, "12345", WORLD_TYPE_WILDERNESS);

    // Seed the render buffer with one fully-visible voxel
    bool visible_faces[6] = { true, true, true, true, true, true };
    isometric_renderer_add_voxel(g_iso_renderer, 0, 0, 0, 0, VOXEL_STONE, visible_faces);

    return 1;
}

// Cleanup resources
static void cleanup_benchmark_environment(void) {
    if (g_unified_renderer) {
        unified_renderer_destroy(g_unified_renderer);
        g_unified_renderer = NULL;
    }

    if (g_iso_renderer) {
        isometric_renderer_destroy(g_iso_renderer);
        g_iso_renderer = NULL;
    }

    if (g_world) {
        world_destroy(g_world);
        g_world = NULL;
    }

    if (g_universe_initialized) {
        universe_free(&g_universe);
        g_universe_initialized = false;
    }

    if (g_sdl_renderer) {
        SDL_DestroyRenderer(g_sdl_renderer);
        g_sdl_renderer = NULL;
    }

    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }

    TTF_Quit();
    SDL_Quit();
}

// Get current time in milliseconds
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// Run benchmark for specific renderer type
static BenchmarkStats run_renderer_benchmark(RendererType renderer_type, int frames) {
    BenchmarkStats stats = {0};
    stats.min_frame_time = 999999.0;

    printf("Benchmarking %s renderer...\n", unified_renderer_type_name(renderer_type));

    // Set renderer type
    unified_renderer_set_type(g_unified_renderer, renderer_type);

    // Warmup frames
    for (int i = 0; i < BENCHMARK_WARMUP_FRAMES; i++) {
        unified_renderer_render(g_unified_renderer, g_iso_renderer, g_sdl_renderer);
        SDL_RenderPresent(g_sdl_renderer);
    }

    // Benchmark frames
    for (int i = 0; i < frames; i++) {
        double frame_start = get_time_ms();

        unified_renderer_render(g_unified_renderer, g_iso_renderer, g_sdl_renderer);
        SDL_RenderPresent(g_sdl_renderer);

        double frame_end = get_time_ms();
        double frame_time = frame_end - frame_start;

        stats.total_time += frame_time;
        if (frame_time < stats.min_frame_time) stats.min_frame_time = frame_time;
        if (frame_time > stats.max_frame_time) stats.max_frame_time = frame_time;
        stats.frame_count++;
    }

    // Calculate statistics
    stats.avg_frame_time = stats.total_time / stats.frame_count;
    stats.fps = 1000.0 / stats.avg_frame_time;

    return stats;
}

// Print benchmark results
static void print_benchmark_results(const char* renderer_name, const BenchmarkStats* stats) {
    printf("\n=== %s Renderer Results ===\n", renderer_name);
    printf("Frames: %d\n", stats->frame_count);
    printf("Total Time: %.2f ms\n", stats->total_time);
    printf("Average Frame Time: %.2f ms\n", stats->avg_frame_time);
    printf("Min Frame Time: %.2f ms\n", stats->min_frame_time);
    printf("Max Frame Time: %.2f ms\n", stats->max_frame_time);
    printf("Average FPS: %.2f\n", stats->fps);
    printf("===========================\n");
}

// Print unified renderer statistics
static void print_unified_renderer_stats(void) {
    CPURendererStats cpu_stats;
    double gpu_fps, cpu_fps;

    unified_renderer_get_stats(g_unified_renderer, &cpu_stats, &gpu_fps, &cpu_fps);

    printf("\n=== Unified Renderer Statistics ===\n");
    printf("Current Renderer: %s\n", unified_renderer_type_name(unified_renderer_get_type(g_unified_renderer)));
    printf("GPU Available: %s\n", unified_renderer_is_gpu_available(g_unified_renderer) ? "Yes" : "No");
    printf("GPU FPS: %.2f\n", gpu_fps);
    printf("CPU FPS: %.2f\n", cpu_fps);
    printf("Renderer Switches: %llu\n", (unsigned long long)g_unified_renderer->renderer_switches);
    printf("====================================\n");
}

// Main benchmark function
static void run_comprehensive_benchmark(void) {
    printf("World Editor Renderer Benchmark\n");
    printf("===============================\n");
    printf("World Size: %dx%dx%d\n", WORLD_SIZE, WORLD_SIZE, WORLD_SIZE);
    printf("Screen Size: %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("Benchmark Frames: %d\n", BENCHMARK_FRAMES);
    printf("Warmup Frames: %d\n", BENCHMARK_WARMUP_FRAMES);
    printf("\n");

    // Test different renderer types
    RendererType renderer_types[] = {
        RENDERER_AUTO,
        RENDERER_CPU_OPTIMIZED,
        RENDERER_GPU,
        RENDERER_CPU_LEGACY
    };

    const char* renderer_names[] = {
        "Auto",
        "CPU Optimized",
        "GPU",
        "CPU Legacy"
    };

    BenchmarkStats results[4];

    for (int i = 0; i < 4; i++) {
        if (unified_renderer_is_supported(renderer_types[i])) {
            results[i] = run_renderer_benchmark(renderer_types[i], BENCHMARK_FRAMES);
            print_benchmark_results(renderer_names[i], &results[i]);
        } else {
            printf("\n%s renderer not supported on this system\n", renderer_names[i]);
        }
    }

    // Print unified renderer statistics
    print_unified_renderer_stats();

    // Performance comparison
    printf("\n=== Performance Comparison ===\n");
    for (int i = 0; i < 4; i++) {
        if (unified_renderer_is_supported(renderer_types[i])) {
            printf("%s: %.2f FPS\n", renderer_names[i], results[i].fps);
        }
    }
    printf("=============================\n");
}

// Command line argument parsing
static void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --frames N        Number of benchmark frames (default: %d)\n", BENCHMARK_FRAMES);
    printf("  --warmup N        Number of warmup frames (default: %d)\n", BENCHMARK_WARMUP_FRAMES);
    printf("  --size N          World size (default: %d)\n", WORLD_SIZE);
    printf("  --width N         Screen width (default: %d)\n", SCREEN_WIDTH);
    printf("  --height N        Screen height (default: %d)\n", SCREEN_HEIGHT);
    printf("  --help            Show this help message\n");
}

int main(int argc, char* argv[]) {
    int benchmark_frames = BENCHMARK_FRAMES;
    int warmup_frames = BENCHMARK_WARMUP_FRAMES;
    int world_size = WORLD_SIZE;
    int screen_width = SCREEN_WIDTH;
    int screen_height = SCREEN_HEIGHT;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            benchmark_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            world_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            screen_width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            screen_height = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Update global constants
    BENCHMARK_FRAMES = benchmark_frames;
    BENCHMARK_WARMUP_FRAMES = warmup_frames;
    WORLD_SIZE = world_size;
    SCREEN_WIDTH = screen_width;
    SCREEN_HEIGHT = screen_height;

    // Initialize benchmark environment
    if (!init_benchmark_environment()) {
        fprintf(stderr, "Failed to initialize benchmark environment\n");
        return 1;
    }

    // Create test world
    if (!create_test_world()) {
        fprintf(stderr, "Failed to create test world\n");
        cleanup_benchmark_environment();
        return 1;
    }

    // Run comprehensive benchmark
    run_comprehensive_benchmark();

    // Cleanup
    cleanup_benchmark_environment();

    printf("\nBenchmark completed successfully!\n");
    return 0;
}
