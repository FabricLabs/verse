#include <inttypes.h>
#include "unified_renderer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// High-resolution timer for performance measurement
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Convert nanoseconds to seconds
static double ns_to_seconds(uint64_t ns) {
    return (double)ns / 1000000000.0;
}

// Convert seconds to FPS
static double seconds_to_fps(double seconds) {
    return seconds > 0.0 ? 1.0 / seconds : 0.0;
}

// Default configuration
static UnifiedRendererConfig default_config(void) {
    UnifiedRendererConfig config = {
        .preferred_type = RENDERER_AUTO,
        .auto_fallback = true,
        .performance_monitoring = true,
        .adaptive_switching = true,
        .cpu_config = cpu_renderer_config_default(),
        .gpu_memory_threshold = 512,  // 512MB
        .performance_threshold = 30.0  // 30 FPS
    };
    return config;
}

// Auto-detect best renderer for current system
RendererType unified_renderer_auto_detect(void) {
    // Check if SDL_RenderGeometry is available (GPU support)
    SDL_Window *test_window = SDL_CreateWindow("Test", 0, 0, 1, 1, SDL_WINDOW_HIDDEN);
    if (test_window) {
        SDL_Renderer *test_renderer = SDL_CreateRenderer(test_window, -1, SDL_RENDERER_ACCELERATED);
        if (test_renderer) {
            // Test if GPU rendering is available
            SDL_Vertex test_vertex = {{0, 0}, {255, 255, 255, 255}, {0, 0}};
            int result = SDL_RenderGeometry(test_renderer, NULL, &test_vertex, 1, NULL, 0);
            SDL_DestroyRenderer(test_renderer);
            SDL_DestroyWindow(test_window);

            if (result == 0) {
                return RENDERER_GPU;  // GPU rendering available
            }
        } else {
            SDL_DestroyWindow(test_window);
        }
    }

    // Fall back to optimized CPU renderer
    return RENDERER_CPU_OPTIMIZED;
}

// Check if renderer type is supported
bool unified_renderer_is_supported(RendererType type) {
    switch (type) {
        case RENDERER_AUTO:
            return true;  // Always supported
        case RENDERER_CPU_OPTIMIZED:
            return true;  // Always supported
        case RENDERER_GPU:
            return unified_renderer_auto_detect() == RENDERER_GPU;
        case RENDERER_CPU_LEGACY:
            return true;  // Always supported
        default:
            return false;
    }
}

// Get recommended renderer for current system
RendererType unified_renderer_get_recommended(void) {
    RendererType detected = unified_renderer_auto_detect();

    // For systems with GPU support, recommend GPU
    if (detected == RENDERER_GPU) {
        return RENDERER_GPU;
    }

    // For CPU-only systems, recommend optimized CPU
    return RENDERER_CPU_OPTIMIZED;
}

// Get renderer type name
const char* unified_renderer_type_name(RendererType type) {
    switch (type) {
        case RENDERER_AUTO: return "Auto";
        case RENDERER_CPU_OPTIMIZED: return "CPU Optimized";
        case RENDERER_GPU: return "GPU";
        case RENDERER_CPU_LEGACY: return "CPU Legacy";
        default: return "Unknown";
    }
}

// Create unified renderer
UnifiedRenderer* unified_renderer_create(void) {
    UnifiedRenderer* renderer = calloc(1, sizeof(UnifiedRenderer));
    if (!renderer) return NULL;

    // Initialize with default configuration
    renderer->config = default_config();

    // Auto-detect best renderer
    RendererType recommended = unified_renderer_get_recommended();
    renderer->config.preferred_type = recommended;
    renderer->current_type = recommended;
    renderer->fallback_type = RENDERER_CPU_OPTIMIZED;

    // Check GPU availability
    renderer->gpu_available = (recommended == RENDERER_GPU);
    renderer->gpu_failed = false;

    // Initialize performance tracking
    renderer->frame_count = 0;
    renderer->renderer_switches = 0;
    renderer->last_frame_time = 0.0;
    renderer->avg_frame_time = 0.0;
    renderer->cpu_performance = 0.0;
    renderer->gpu_performance = 0.0;

    return renderer;
}

// Destroy unified renderer
void unified_renderer_destroy(UnifiedRenderer* renderer) {
    if (renderer) {
        free(renderer);
    }
}

// Set renderer configuration
void unified_renderer_set_config(UnifiedRenderer* renderer, const UnifiedRendererConfig* config) {
    if (renderer && config) {
        renderer->config = *config;

        // Update current renderer if needed
        if (config->preferred_type != RENDERER_AUTO) {
            unified_renderer_switch_to(renderer, config->preferred_type);
        }
    }
}

// Get current configuration
const UnifiedRendererConfig* unified_renderer_get_config(const UnifiedRenderer* renderer) {
    return renderer ? &renderer->config : NULL;
}

// Set preferred renderer type
void unified_renderer_set_type(UnifiedRenderer* renderer, RendererType type) {
    if (renderer) {
        renderer->config.preferred_type = type;
        unified_renderer_switch_to(renderer, type);
    }
}

// Get current renderer type
RendererType unified_renderer_get_type(const UnifiedRenderer* renderer) {
    return renderer ? renderer->current_type : RENDERER_AUTO;
}

// Check GPU availability
bool unified_renderer_is_gpu_available(const UnifiedRenderer* renderer) {
    return renderer ? renderer->gpu_available && !renderer->gpu_failed : false;
}

// Force renderer switch
bool unified_renderer_switch_to(UnifiedRenderer* renderer, RendererType type) {
    if (!renderer) return false;

    // Check if the requested type is supported
    if (!unified_renderer_is_supported(type)) {
        return false;
    }

    // Don't switch if already using the requested type
    if (renderer->current_type == type) {
        return true;
    }

    // Perform the switch
    RendererType old_type = renderer->current_type;
    renderer->current_type = type;
    renderer->renderer_switches++;

    // Update GPU availability status
    if (type == RENDERER_GPU) {
        renderer->gpu_available = true;
        renderer->gpu_failed = false;
    }

    printf("[UnifiedRenderer] Switched from %s to %s\n",
           unified_renderer_type_name(old_type),
           unified_renderer_type_name(type));

    return true;
}

// Main rendering function
void unified_renderer_render(UnifiedRenderer* renderer,
                            IsometricRenderer* iso_renderer,
                            SDL_Renderer* sdl_renderer) {
    if (!renderer || !iso_renderer || !sdl_renderer) return;

    uint64_t start_time = get_time_ns();

    // Determine which renderer to use
    RendererType render_type = renderer->current_type;

    // Handle AUTO mode
    if (render_type == RENDERER_AUTO) {
        if (renderer->gpu_available && !renderer->gpu_failed) {
            render_type = RENDERER_GPU;
        } else {
            render_type = RENDERER_CPU_OPTIMIZED;
        }
    }

    // Perform rendering with selected renderer
    bool render_success = false;

    switch (render_type) {
        case RENDERER_GPU:
            // Try GPU rendering first
            if (renderer->gpu_available && !renderer->gpu_failed) {
                isometric_renderer_render_gpu(iso_renderer, sdl_renderer);
                render_success = true;
            }

            // Fall back to CPU if GPU fails
            if (!render_success && renderer->config.auto_fallback) {
                renderer->gpu_failed = true;
                printf("[UnifiedRenderer] GPU rendering failed, falling back to CPU\n");
                cpu_renderer_render_optimized(iso_renderer, sdl_renderer, &renderer->config.cpu_config);
                render_success = true;
            }
            break;

        case RENDERER_CPU_OPTIMIZED:
            cpu_renderer_render_optimized(iso_renderer, sdl_renderer, &renderer->config.cpu_config);
            render_success = true;
            break;

        case RENDERER_CPU_LEGACY:
            isometric_renderer_render(iso_renderer, sdl_renderer);
            render_success = true;
            break;

        default:
            // Fall back to optimized CPU
            cpu_renderer_render_optimized(iso_renderer, sdl_renderer, &renderer->config.cpu_config);
            render_success = true;
            break;
    }

    // Update performance tracking
    if (render_success) {
        renderer->frame_count++;
        renderer->last_frame_time = ns_to_seconds(get_time_ns() - start_time);

        // Update average frame time (exponential moving average)
        if (renderer->avg_frame_time == 0.0) {
            renderer->avg_frame_time = renderer->last_frame_time;
        } else {
            renderer->avg_frame_time = 0.9 * renderer->avg_frame_time + 0.1 * renderer->last_frame_time;
        }

        // Update renderer-specific performance
        double current_fps = seconds_to_fps(renderer->last_frame_time);
        switch (render_type) {
            case RENDERER_GPU:
                renderer->gpu_performance = current_fps;
                break;
            case RENDERER_CPU_OPTIMIZED:
            case RENDERER_CPU_LEGACY:
                renderer->cpu_performance = current_fps;
                break;
            default:
                break;
        }
    }
}

// Get performance statistics
void unified_renderer_get_stats(const UnifiedRenderer* renderer,
                               CPURendererStats* cpu_stats,
                               double* gpu_fps,
                               double* cpu_fps) {
    if (!renderer) return;

    if (cpu_stats) {
        *cpu_stats = cpu_renderer_get_stats();
    }

    if (gpu_fps) {
        *gpu_fps = renderer->gpu_performance;
    }

    if (cpu_fps) {
        *cpu_fps = renderer->cpu_performance;
    }
}

// Print performance summary
void unified_renderer_print_summary(const UnifiedRenderer* renderer) {
    if (!renderer) return;

    printf("=== Unified Renderer Performance Summary ===\n");
    printf("Current renderer: %s\n", unified_renderer_type_name(renderer->current_type));
    printf("GPU available: %s\n", renderer->gpu_available ? "Yes" : "No");
    printf("GPU failed: %s\n", renderer->gpu_failed ? "Yes" : "No");
    printf("Total frames: %" PRIu64 "\n", renderer->frame_count);
    printf("Renderer switches: %" PRIu64 "\n", renderer->renderer_switches);
    printf("Last frame time: %.3f ms (%.1f FPS)\n",
           renderer->last_frame_time * 1000.0,
           seconds_to_fps(renderer->last_frame_time));
    printf("Average frame time: %.3f ms (%.1f FPS)\n",
           renderer->avg_frame_time * 1000.0,
           seconds_to_fps(renderer->avg_frame_time));
    printf("GPU performance: %.1f FPS\n", renderer->gpu_performance);
    printf("CPU performance: %.1f FPS\n", renderer->cpu_performance);
    printf("============================================\n");
}

// Reset performance counters
void unified_renderer_reset_stats(UnifiedRenderer* renderer) {
    if (!renderer) return;

    renderer->frame_count = 0;
    renderer->renderer_switches = 0;
    renderer->last_frame_time = 0.0;
    renderer->avg_frame_time = 0.0;
    renderer->cpu_performance = 0.0;
    renderer->gpu_performance = 0.0;

    // Reset CPU renderer stats
    cpu_renderer_reset_stats();
}

// Benchmark renderer performance
double unified_renderer_benchmark(UnifiedRenderer* renderer,
                                 IsometricRenderer* iso_renderer,
                                 SDL_Renderer* sdl_renderer,
                                 RendererType type,
                                 int frames) {
    if (!renderer || !iso_renderer || !sdl_renderer || frames <= 0) return 0.0;

    // Temporarily switch to requested renderer
    RendererType old_type = renderer->current_type;
    unified_renderer_switch_to(renderer, type);

    // Warm up
    for (int i = 0; i < 10; i++) {
        unified_renderer_render(renderer, iso_renderer, sdl_renderer);
    }

    // Benchmark
    uint64_t start_time = get_time_ns();
    for (int i = 0; i < frames; i++) {
        unified_renderer_render(renderer, iso_renderer, sdl_renderer);
    }
    uint64_t end_time = get_time_ns();

    // Restore original renderer
    unified_renderer_switch_to(renderer, old_type);

    // Calculate FPS
    double total_time = ns_to_seconds(end_time - start_time);
    double fps = frames / total_time;

    printf("[Benchmark] %s: %d frames in %.3f seconds = %.1f FPS\n",
           unified_renderer_type_name(type), frames, total_time, fps);

    return fps;
}

// Adaptive renderer switching
void unified_renderer_adaptive_update(UnifiedRenderer* renderer,
                                     double current_fps,
                                     double target_fps) {
    if (!renderer || !renderer->config.adaptive_switching) return;

    // If performance is below threshold, consider switching
    if (current_fps < renderer->config.performance_threshold) {
        RendererType current = renderer->current_type;
        RendererType alternative = RENDERER_CPU_OPTIMIZED;

        // If currently using CPU, try GPU
        if (current == RENDERER_CPU_OPTIMIZED || current == RENDERER_CPU_LEGACY) {
            if (renderer->gpu_available && !renderer->gpu_failed) {
                alternative = RENDERER_GPU;
            }
        }
        // If currently using GPU, try CPU
        else if (current == RENDERER_GPU) {
            alternative = RENDERER_CPU_OPTIMIZED;
        }

        // Switch if alternative might be better
        if (alternative != current) {
            printf("[UnifiedRenderer] Performance below threshold (%.1f < %.1f FPS), switching to %s\n",
                   current_fps, renderer->config.performance_threshold,
                   unified_renderer_type_name(alternative));
            unified_renderer_switch_to(renderer, alternative);
        }
    }
}

// Auto-configure renderer for current system
void unified_renderer_auto_configure(UnifiedRenderer* renderer) {
    if (!renderer) return;

    // Auto-detect best renderer
    RendererType recommended = unified_renderer_get_recommended();

    // Set optimal configuration based on system capabilities
    if (recommended == RENDERER_GPU) {
        // GPU system - enable all optimizations
        renderer->config.cpu_config.flags = CPU_RENDER_OPT_ALL;
        renderer->config.cpu_config.batch_size = 2048;
        renderer->config.performance_threshold = 60.0;  // Target 60 FPS
    } else {
        // CPU-only system - optimize for CPU
        renderer->config.cpu_config.flags = CPU_RENDER_OPT_ALL;
        renderer->config.cpu_config.batch_size = 512;   // Smaller batches for CPU
        renderer->config.performance_threshold = 30.0;  // Target 30 FPS
    }

    // Set renderer type
    unified_renderer_set_type(renderer, recommended);

    printf("[UnifiedRenderer] Auto-configured for %s system\n",
           recommended == RENDERER_GPU ? "GPU" : "CPU-only");
}
