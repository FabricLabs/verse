#ifndef SPRITE_RENDERER_H
#define SPRITE_RENDERER_H

#include "isometric_renderer.h"
#include <SDL2/SDL.h>

// Sprite renderer optimization flags
typedef enum {
    SPRITE_RENDER_OPT_NONE = 0,
    SPRITE_RENDER_OPT_CHUNK_CULLING = 1 << 0,        // Chunk-based culling
    SPRITE_RENDER_OPT_SCREEN_CULLING = 1 << 1,        // Screen-space culling
    SPRITE_RENDER_OPT_SPRITE_CACHING = 1 << 2,        // Cache rendered sprites
    SPRITE_RENDER_OPT_BATCH_RENDERING = 1 << 3,       // Batch similar operations
    SPRITE_RENDER_OPT_DEPTH_SORTING = 1 << 4,         // Painter's algorithm sorting
    SPRITE_RENDER_OPT_OCCUPANCY_BITS = 1 << 5,        // Use occupancy bitfield
    SPRITE_RENDER_OPT_ALL = 0x3F                      // All optimizations
} SpriteRenderOptimizations;

// Sprite renderer configuration
typedef struct {
    SpriteRenderOptimizations flags;       // Which optimizations to enable
    int batch_size;                        // Maximum batch size for rendering
    int chunk_size_x, chunk_size_y;        // Chunk dimensions for culling
    bool enable_grid;                      // Whether to render grid lines
    bool enable_debug_overlay;             // Whether to show debug info
    int max_visible_voxels;                // Maximum voxels to render per frame
    float culling_margin;                  // Extra margin for culling (pixels)
    int sprite_size;                       // Size of each sprite (pixels)
    bool use_alpha_blending;               // Enable alpha blending for sprites
} SpriteRendererConfig;

// Sprite renderer performance statistics
typedef struct {
    uint64_t voxels_processed;             // Total voxels examined
    uint64_t voxels_culled;                // Voxels culled by optimization
    uint64_t voxels_rendered;              // Voxels actually drawn
    uint64_t chunks_processed;             // Chunks examined
    uint64_t chunks_culled;                // Chunks culled
    uint64_t batches_flushed;              // Render batches executed
    uint64_t sprites_generated;            // Sprites created this frame
    uint64_t sprites_cached;               // Sprites reused from cache
    uint64_t render_time_ns;               // Total render time in nanoseconds
    uint64_t cull_time_ns;                 // Culling time in nanoseconds
    uint64_t draw_time_ns;                 // Drawing time in nanoseconds
    uint64_t sprite_gen_time_ns;           // Sprite generation time in nanoseconds
} SpriteRendererStats;

// Sprite cache entry
typedef struct {
    VoxelType voxel_type;                  // Type of voxel this sprite represents
    SDL_Surface* surface;                  // Rendered sprite surface
    SDL_Texture* texture;                  // Cached texture (if using GPU)
    uint32_t last_used;                    // Frame number when last used
    bool is_valid;                         // Whether this sprite is still valid
} SpriteCacheEntry;

// Sprite renderer state
typedef struct {
    SpriteRendererConfig config;           // Current configuration
    SpriteRendererStats stats;             // Performance statistics
    SpriteCacheEntry* sprite_cache;        // Cache of rendered sprites
    int sprite_cache_size;                 // Number of cached sprites
    int sprite_cache_capacity;             // Maximum cache capacity
    SDL_Renderer* sdl_renderer;           // SDL renderer for texture creation
    uint32_t current_frame;                // Current frame number for cache management
} SpriteRenderer;

// Initialize sprite renderer configuration
SpriteRendererConfig sprite_renderer_config_default(void);

// Set specific optimization flags
void sprite_renderer_set_optimizations(SpriteRendererConfig *config, SpriteRenderOptimizations flags);

// Enable/disable specific optimizations
void sprite_renderer_enable_optimization(SpriteRendererConfig *config, SpriteRenderOptimizations opt);
void sprite_renderer_disable_optimization(SpriteRendererConfig *config, SpriteRenderOptimizations opt);

// Create and initialize sprite renderer
SpriteRenderer* sprite_renderer_create(SDL_Renderer* sdl_renderer, const SpriteRendererConfig* config);

// Destroy sprite renderer and free resources
void sprite_renderer_destroy(SpriteRenderer* renderer);

// Get current renderer statistics
SpriteRendererStats sprite_renderer_get_stats(const SpriteRenderer* renderer);

// Reset performance statistics
void sprite_renderer_reset_stats(SpriteRenderer* renderer);

// Print performance statistics to console
void sprite_renderer_print_stats(const SpriteRenderer* renderer);

// Main sprite rendering function (interchangeable with CPU/GPU renderers)
void sprite_renderer_render(SpriteRenderer* renderer,
                           IsometricRenderer* iso_renderer,
                           SDL_Renderer* sdl_renderer,
                           World* world);

// Generate a sprite for a specific voxel type
SDL_Surface* sprite_renderer_generate_sprite(SpriteRenderer* renderer, VoxelType voxel_type);

// Get cached sprite (creates if not cached)
SDL_Texture* sprite_renderer_get_sprite(SpriteRenderer* renderer, VoxelType voxel_type);

// Clear sprite cache
void sprite_renderer_clear_cache(SpriteRenderer* renderer);

// Pre-generate sprites for all voxel types
void sprite_renderer_preload_sprites(SpriteRenderer* renderer);

#endif // SPRITE_RENDERER_H
