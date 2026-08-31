#ifndef CPU_RENDERER_OPTIMIZED_H
#define CPU_RENDERER_OPTIMIZED_H

#include "isometric_renderer.h"
#include <SDL2/SDL.h>

// CPU renderer optimization flags
typedef enum {
    CPU_RENDER_OPT_NONE = 0,
    CPU_RENDER_OPT_CHUNK_CULLING = 1 << 0,        // Chunk-based culling
    CPU_RENDER_OPT_OCCUPANCY_BITS = 1 << 1,       // Use occupancy bitfield
    CPU_RENDER_OPT_SCREEN_CULLING = 1 << 2,       // Screen-space culling
    CPU_RENDER_OPT_FACE_CULLING = 1 << 3,         // Aggressive face culling (performance vs quality)
    CPU_RENDER_OPT_BATCH_RENDERING = 1 << 4,      // Batch similar operations
    CPU_RENDER_OPT_COLOR_TABLE = 1 << 5,          // Precomputed color lookup
    CPU_RENDER_OPT_LOOP_UNROLLING = 1 << 6,       // Manual loop unrolling
    CPU_RENDER_OPT_MEMORY_ALIGNMENT = 1 << 7,     // Memory-aligned access
    CPU_RENDER_OPT_SIMD_HINTS = 1 << 8,           // SIMD-friendly data layout
    CPU_RENDER_OPT_CACHE_OPTIMIZATION = 1 << 9,   // Cache-friendly iteration
    CPU_RENDER_OPT_ALL = 0x3FF                    // All optimizations
} CPURenderOptimizations;

// Optimized CPU renderer configuration
typedef struct {
    CPURenderOptimizations flags;           // Which optimizations to enable
    int batch_size;                         // Maximum batch size for rendering
    int chunk_size_x, chunk_size_y, chunk_size_z;        // Chunk dimensions for culling
    bool enable_grid;                       // Whether to render grid lines
    bool enable_debug_overlay;              // Whether to show debug info
    int max_visible_voxels;                 // Maximum voxels to render per frame
    float culling_margin;                   // Extra margin for culling (pixels)
} CPURendererConfig;

// CPU renderer performance statistics
typedef struct {
    uint64_t voxels_processed;              // Total voxels examined
    uint64_t voxels_culled;                 // Voxels culled by optimization
    uint64_t voxels_rendered;               // Voxels actually drawn
    uint64_t chunks_processed;              // Chunks examined
    uint64_t chunks_culled;                 // Chunks culled
    uint64_t batches_flushed;               // Render batches executed
    uint64_t render_time_ns;                // Total render time in nanoseconds
    uint64_t cull_time_ns;                  // Culling time in nanoseconds
    uint64_t draw_time_ns;                  // Drawing time in nanoseconds
} CPURendererStats;

// Initialize optimized CPU renderer configuration
CPURendererConfig cpu_renderer_config_default(void);

// Set specific optimization flags
void cpu_renderer_set_optimizations(CPURendererConfig *config, CPURenderOptimizations flags);

// Enable/disable specific optimizations
void cpu_renderer_enable_optimization(CPURendererConfig *config, CPURenderOptimizations opt);
void cpu_renderer_disable_optimization(CPURendererConfig *config, CPURenderOptimizations opt);

// Get current renderer statistics
CPURendererStats cpu_renderer_get_stats(void);

// Reset performance statistics
void cpu_renderer_reset_stats(void);

// Print performance statistics to console
void cpu_renderer_print_stats(void);

// Optimized CPU rendering function (interchangeable with GPU renderer)
void cpu_renderer_render_optimized(IsometricRenderer *renderer,
                                  SDL_Renderer *sdl_renderer,
                                  const CPURendererConfig *config);

// Memory-aligned voxel batch structure for SIMD optimization
typedef struct {
    SDL_Rect rects[64];                    // Aligned to 256-byte boundary
    uint32_t colors[64];                   // Packed RGBA colors
    uint8_t count;                         // Number of items in batch
    uint8_t padding[3];                    // Padding for alignment
} __attribute__((aligned(32))) VoxelBatch;

// Precomputed color lookup table (cache-friendly)
extern SDL_Color g_cpu_color_table[VOXEL_COUNT];

// Initialize the global color table
void cpu_renderer_init_color_table(void);

// Fast voxel type to color conversion
static inline SDL_Color cpu_renderer_get_color(VoxelType type) {
    return g_cpu_color_table[type];
}

// Fast bounds checking with branch prediction hints
static inline bool cpu_renderer_bounds_check(int x, int y, int z, int width, int height, int depth) {
    return __builtin_expect((x >= 0) && (y >= 0) && (z >= 0) &&
                           (x < width) && (y < height) && (z < depth), 1);
}

// Fast screen-space culling with early exit
static inline bool cpu_renderer_screen_cull(int sx, int sy, int sw, int sh, int screen_w, int screen_h) {
    return __builtin_expect((sx + sw > 0) && (sy + sh > 0) &&
                           (sx < screen_w) && (sy < screen_h), 1);
}

#endif // CPU_RENDERER_OPTIMIZED_H
