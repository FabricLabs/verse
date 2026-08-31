#ifndef UNIFIED_RENDERER_H
#define UNIFIED_RENDERER_H

#include "isometric_renderer.h"
#include "cpu_renderer_optimized.h"
#include <SDL2/SDL.h>

// Renderer type enumeration
typedef enum {
    RENDERER_AUTO,           // Automatically choose best available
    RENDERER_CPU_OPTIMIZED,  // Use extremely optimized CPU renderer
    RENDERER_GPU,            // Use GPU renderer with CPU fallback
    RENDERER_CPU_LEGACY      // Use legacy CPU renderer (fallback)
} RendererType;

// Unified renderer configuration
typedef struct {
    RendererType preferred_type;     // Preferred renderer type
    bool auto_fallback;              // Automatically fallback if preferred fails
    bool performance_monitoring;     // Enable performance statistics
    bool adaptive_switching;         // Switch renderers based on performance
    CPURendererConfig cpu_config;    // CPU renderer configuration
    int gpu_memory_threshold;        // Memory threshold for GPU fallback (MB)
    float performance_threshold;      // Performance threshold for switching (FPS)
} UnifiedRendererConfig;

// Unified renderer state
typedef struct {
    RendererType current_type;       // Currently active renderer
    RendererType fallback_type;      // Fallback renderer if primary fails
    UnifiedRendererConfig config;    // Current configuration
    bool gpu_available;              // Whether GPU rendering is available
    bool gpu_failed;                 // Whether GPU rendering has failed
    uint64_t frame_count;            // Total frames rendered
    uint64_t renderer_switches;      // Number of renderer switches
    double last_frame_time;          // Last frame render time (seconds)
    double avg_frame_time;           // Average frame render time (seconds)
    double cpu_performance;          // CPU renderer performance (FPS)
    double gpu_performance;          // GPU renderer performance (FPS)
} UnifiedRenderer;

// Initialize unified renderer with default configuration
UnifiedRenderer* unified_renderer_create(void);

// Destroy unified renderer
void unified_renderer_destroy(UnifiedRenderer* renderer);

// Set renderer configuration
void unified_renderer_set_config(UnifiedRenderer* renderer, const UnifiedRendererConfig* config);

// Get current renderer configuration
const UnifiedRendererConfig* unified_renderer_get_config(const UnifiedRenderer* renderer);

// Set preferred renderer type
void unified_renderer_set_type(UnifiedRenderer* renderer, RendererType type);

// Get current renderer type
RendererType unified_renderer_get_type(const UnifiedRenderer* renderer);

// Check if GPU rendering is available
bool unified_renderer_is_gpu_available(const UnifiedRenderer* renderer);

// Force renderer switch (for testing/debugging)
bool unified_renderer_switch_to(UnifiedRenderer* renderer, RendererType type);

// Main rendering function - automatically chooses best renderer
void unified_renderer_render(UnifiedRenderer* renderer,
                            IsometricRenderer* iso_renderer,
                            SDL_Renderer* sdl_renderer);

// Get performance statistics
void unified_renderer_get_stats(const UnifiedRenderer* renderer,
                               CPURendererStats* cpu_stats,
                               double* gpu_fps,
                               double* cpu_fps);

// Print performance summary
void unified_renderer_print_summary(const UnifiedRenderer* renderer);

// Reset performance counters
void unified_renderer_reset_stats(UnifiedRenderer* renderer);

// Auto-detect best renderer for current system
RendererType unified_renderer_auto_detect(void);

// Test renderer performance and return FPS
double unified_renderer_benchmark(UnifiedRenderer* renderer,
                                 IsometricRenderer* iso_renderer,
                                 SDL_Renderer* sdl_renderer,
                                 RendererType type,
                                 int frames);

// Adaptive renderer switching based on performance
void unified_renderer_adaptive_update(UnifiedRenderer* renderer,
                                     double current_fps,
                                     double target_fps);

// Get renderer type name for display
const char* unified_renderer_type_name(RendererType type);

// Check if renderer type is supported on current system
bool unified_renderer_is_supported(RendererType type);

// Get recommended renderer for current system
RendererType unified_renderer_get_recommended(void);

// Initialize renderer with optimal settings for current system
void unified_renderer_auto_configure(UnifiedRenderer* renderer);

#endif // UNIFIED_RENDERER_H
