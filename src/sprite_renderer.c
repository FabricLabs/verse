#include <inttypes.h>
#include "sprite_renderer.h"
#include "chunk_config.h"
#include "world.h"
#include <string.h>
#include <math.h>
#include <time.h>

// High-resolution timer for performance measurement
static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Default configuration with all optimizations enabled
SpriteRendererConfig sprite_renderer_config_default(void)
{
    SpriteRendererConfig config = {
        .flags = SPRITE_RENDER_OPT_ALL,
        .batch_size = 1024,
        .chunk_size_x = CHUNK_SIZE_X,
        .chunk_size_y = CHUNK_SIZE_Y,
        .enable_grid = true,
        .enable_debug_overlay = false,
        .max_visible_voxels = 100000,
        .culling_margin = 32.0f,
        .sprite_size = 32,
        .use_alpha_blending = true
    };
    return config;
}

// Set optimization flags
void sprite_renderer_set_optimizations(SpriteRendererConfig *config, SpriteRenderOptimizations flags)
{
    if (config) {
        config->flags = flags;
    }
}

// Enable specific optimization
void sprite_renderer_enable_optimization(SpriteRendererConfig *config, SpriteRenderOptimizations opt)
{
    if (config) {
        config->flags |= opt;
    }
}

// Disable specific optimization
void sprite_renderer_disable_optimization(SpriteRendererConfig *config, SpriteRenderOptimizations opt)
{
    if (config) {
        config->flags &= ~opt;
    }
}

// Create and initialize sprite renderer
SpriteRenderer* sprite_renderer_create(SDL_Renderer* sdl_renderer, const SpriteRendererConfig* config)
{
    if (!sdl_renderer) return NULL;

    SpriteRenderer* renderer = malloc(sizeof(SpriteRenderer));
    if (!renderer) return NULL;

    // Initialize with default config if none provided
    if (config) {
        renderer->config = *config;
    } else {
        renderer->config = sprite_renderer_config_default();
    }

    // Initialize statistics
    memset(&renderer->stats, 0, sizeof(SpriteRendererStats));

    // Initialize sprite cache
    renderer->sprite_cache_capacity = VOXEL_COUNT;
    renderer->sprite_cache = calloc(renderer->sprite_cache_capacity, sizeof(SpriteCacheEntry));
    if (!renderer->sprite_cache) {
        free(renderer);
        return NULL;
    }

    renderer->sprite_cache_size = 0;
    renderer->sdl_renderer = sdl_renderer;
    renderer->current_frame = 0;

    // Pre-generate sprites for all voxel types
    if (renderer->config.flags & SPRITE_RENDER_OPT_SPRITE_CACHING) {
        sprite_renderer_preload_sprites(renderer);
    }

    return renderer;
}

// Destroy sprite renderer and free resources
void sprite_renderer_destroy(SpriteRenderer* renderer)
{
    if (!renderer) return;

    // Free sprite cache
    if (renderer->sprite_cache) {
        for (int i = 0; i < renderer->sprite_cache_size; i++) {
            if (renderer->sprite_cache[i].surface) {
                SDL_FreeSurface(renderer->sprite_cache[i].surface);
            }
            if (renderer->sprite_cache[i].texture) {
                SDL_DestroyTexture(renderer->sprite_cache[i].texture);
            }
        }
        free(renderer->sprite_cache);
    }

    free(renderer);
}

// Get current renderer statistics
SpriteRendererStats sprite_renderer_get_stats(const SpriteRenderer* renderer)
{
    return renderer ? renderer->stats : (SpriteRendererStats){0};
}

// Reset performance statistics
void sprite_renderer_reset_stats(SpriteRenderer* renderer)
{
    if (renderer) {
        memset(&renderer->stats, 0, sizeof(SpriteRendererStats));
    }
}

// Print performance statistics to console
void sprite_renderer_print_stats(const SpriteRenderer* renderer)
{
    if (!renderer) return;

    printf("=== Sprite Renderer Performance Statistics ===\n");
    printf("Voxels processed: %" PRIu64 "\n", renderer->stats.voxels_processed);
    printf("Voxels culled: %" PRIu64 "\n", renderer->stats.voxels_culled);
    printf("Voxels rendered: %" PRIu64 "\n", renderer->stats.voxels_rendered);
    printf("Chunks processed: %" PRIu64 "\n", renderer->stats.chunks_processed);
    printf("Chunks culled: %" PRIu64 "\n", renderer->stats.chunks_culled);
    printf("Batches flushed: %" PRIu64 "\n", renderer->stats.batches_flushed);
    printf("Sprites generated: %" PRIu64 "\n", renderer->stats.sprites_generated);
    printf("Sprites cached: %" PRIu64 "\n", renderer->stats.sprites_cached);
    printf("Render time: %.3f ms\n", renderer->stats.render_time_ns / 1000000.0);
    printf("Cull time: %.3f ms\n", renderer->stats.cull_time_ns / 1000000.0);
    printf("Draw time: %.3f ms\n", renderer->stats.draw_time_ns / 1000000.0);
    printf("Sprite gen time: %.3f ms\n", renderer->stats.sprite_gen_time_ns / 1000000.0);
    printf("Culling efficiency: %.1f%%\n",
           renderer->stats.voxels_processed > 0 ? (100.0 * renderer->stats.voxels_culled) / renderer->stats.voxels_processed : 0.0);
    printf("=============================================\n");
}

// Generate a sprite for a specific voxel type
SDL_Surface* sprite_renderer_generate_sprite(SpriteRenderer* renderer, VoxelType voxel_type)
{
    if (!renderer) return NULL;

    uint64_t start_time = get_time_ns();

    // Create surface for the sprite
    SDL_Surface* surface = SDL_CreateRGBSurface(0, renderer->config.sprite_size, renderer->config.sprite_size, 32, 0, 0, 0, 0);
    if (!surface) return NULL;

    // Get voxel color
    uint8_t r, g, b;
    world_voxel_type_color(voxel_type, &r, &g, &b);

    // Fill surface with voxel color
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, r, g, b));

    // Add simple shading for 3D effect (top lighter, sides darker)
    uint32_t* pixels = (uint32_t*)surface->pixels;
    int size = renderer->config.sprite_size;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            uint32_t pixel = pixels[y * size + x];
            uint8_t pr, pg, pb, pa;
            SDL_GetRGBA(pixel, surface->format, &pr, &pg, &pb, &pa);

            // Simple shading: top lighter, bottom darker
            float shade = 1.0f;
            if (y < size / 3) {
                shade = 1.2f;  // Top lighter
            } else if (y > 2 * size / 3) {
                shade = 0.8f;  // Bottom darker
            }

            pr = (uint8_t)fmin(255, pr * shade);
            pg = (uint8_t)fmin(255, pg * shade);
            pb = (uint8_t)fmin(255, pb * shade);

            pixels[y * size + x] = SDL_MapRGBA(surface->format, pr, pg, pb, pa);
        }
    }

    renderer->stats.sprite_gen_time_ns += get_time_ns() - start_time;
    renderer->stats.sprites_generated++;

    return surface;
}

// Get cached sprite (creates if not cached)
SDL_Texture* sprite_renderer_get_sprite(SpriteRenderer* renderer, VoxelType voxel_type)
{
    if (!renderer) return NULL;

    // Check if sprite is already cached
    for (int i = 0; i < renderer->sprite_cache_size; i++) {
        if (renderer->sprite_cache[i].voxel_type == voxel_type &&
            renderer->sprite_cache[i].is_valid) {
            renderer->sprite_cache[i].last_used = renderer->current_frame;
            renderer->stats.sprites_cached++;
            return renderer->sprite_cache[i].texture;
        }
    }

    // Generate new sprite
    SDL_Surface* surface = sprite_renderer_generate_sprite(renderer, voxel_type);
    if (!surface) return NULL;

    // Create texture from surface
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer->sdl_renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return NULL;
    }

    // Add to cache
    if (renderer->sprite_cache_size < renderer->sprite_cache_capacity) {
        int index = renderer->sprite_cache_size++;
        renderer->sprite_cache[index].voxel_type = voxel_type;
        renderer->sprite_cache[index].surface = surface;
        renderer->sprite_cache[index].texture = texture;
        renderer->sprite_cache[index].last_used = renderer->current_frame;
        renderer->sprite_cache[index].is_valid = true;
    }

    return texture;
}

// Clear sprite cache
void sprite_renderer_clear_cache(SpriteRenderer* renderer)
{
    if (!renderer) return;

    for (int i = 0; i < renderer->sprite_cache_size; i++) {
        if (renderer->sprite_cache[i].surface) {
            SDL_FreeSurface(renderer->sprite_cache[i].surface);
            renderer->sprite_cache[i].surface = NULL;
        }
        if (renderer->sprite_cache[i].texture) {
            SDL_DestroyTexture(renderer->sprite_cache[i].texture);
            renderer->sprite_cache[i].texture = NULL;
        }
        renderer->sprite_cache[i].is_valid = false;
    }
    renderer->sprite_cache_size = 0;
}

// Pre-generate sprites for all voxel types
void sprite_renderer_preload_sprites(SpriteRenderer* renderer)
{
    if (!renderer) return;

    printf("[SPRITE] Pre-generating sprites for %d voxel types...\n", VOXEL_COUNT);

    for (int type = 0; type < VOXEL_COUNT; type++) {
        sprite_renderer_get_sprite(renderer, (VoxelType)type);
    }

    printf("[SPRITE] Pre-generated %d sprites\n", renderer->sprite_cache_size);
}

// Main sprite rendering function
void sprite_renderer_render(SpriteRenderer* renderer,
                           IsometricRenderer* iso_renderer,
                           SDL_Renderer* sdl_renderer,
                           World* world)
{
    if (!renderer || !iso_renderer || !sdl_renderer || !world) return;

    uint64_t total_start_time = get_time_ns();
    uint64_t cull_start_time = get_time_ns();

    // Reset frame statistics
    renderer->stats.voxels_processed = 0;
    renderer->stats.voxels_culled = 0;
    renderer->stats.voxels_rendered = 0;
    renderer->stats.chunks_processed = 0;
    renderer->stats.chunks_culled = 0;
    renderer->stats.batches_flushed = 0;
    renderer->stats.sprites_generated = 0;
    renderer->stats.sprites_cached = 0;

    // Get world dimensions
    uint32_t world_width = world->width;
    uint32_t world_height = world->height;
    uint32_t world_depth = world->depth;

    // Get screen dimensions
    int screen_w = iso_renderer->screen_width;
    int screen_h = iso_renderer->screen_height;
    int screen_center_x = iso_renderer->screen_center_x;
    int screen_center_y = iso_renderer->screen_center_y;

    // Calculate cell dimensions
    int cell_w = renderer->config.sprite_size;
    int cell_h = renderer->config.sprite_size;

    // Calculate world center for camera positioning
    int world_center_x = world_width / 2;
    int world_center_y = world_height / 2;
    int world_center_z = world_depth / 2;

    // Use the same fixed isometric angles for consistency
    const float iso_x_mult = 0.866f;  // cos(30°) for consistent X-Y projection
    const float iso_y_mult = 0.5f;    // sin(30°) for consistent X-Y projection
    const float iso_z_mult = 0.7f;    // Z depth multiplier for 3D separation

    // Calculate where the world center projects to in screen space
    int center_iso_x = (int)(((world_center_x - world_center_y) * iso_x_mult * cell_w) + 0.5f);
    int center_iso_y = (int)(((world_center_x + world_center_y) * iso_y_mult * cell_h) - (world_center_z * iso_z_mult * cell_h) + 0.5f);

    // Adjust screen center so the world center appears at screen center
    int adjusted_screen_center_x = screen_center_x - center_iso_x;
    int adjusted_screen_center_y = screen_center_y - center_iso_y;

    // Debug output
    printf("[SPRITE-DEBUG] World dimensions: %dx%dx%d\n", world_width, world_height, world_depth);
    printf("[SPRITE-DEBUG] World center (%d,%d,%d): %s\n",
           world_center_x, world_center_y, world_center_z,
           world_voxel_type_name(world_get_voxel(world, world_center_x, world_center_y, world_center_z)->type));
    printf("[SPRITE-DEBUG] World center projects to: (%d, %d)\n", center_iso_x, center_iso_y);
    printf("[SPRITE-DEBUG] Screen center adjusted: (%d, %d)\n", adjusted_screen_center_x, adjusted_screen_center_y);

    renderer->stats.cull_time_ns = get_time_ns() - cull_start_time;
    uint64_t draw_start_time = get_time_ns();

    // Render voxels using sprites
    for (uint32_t z = 0; z < world_depth; z++) {
        for (uint32_t y = 0; y < world_height; y++) {
            for (uint32_t x = 0; x < world_width; x++) {
                renderer->stats.voxels_processed++;

                // Get voxel
                const Voxel* voxel = world_voxel_cptr_fast(world, x, y, z);
                if (!voxel || voxel->type == VOXEL_AIR) {
                    renderer->stats.voxels_culled++;
                    continue;
                }

                // Calculate screen position using fixed isometric angles
                int screen_x = adjusted_screen_center_x + (int)(((x - y) * iso_x_mult * cell_w) + 0.5f);
                int screen_y = adjusted_screen_center_y + (int)(((x + y) * iso_y_mult * cell_h) - (z * iso_z_mult * cell_h) + 0.5f);

                // Screen-space culling
                if (renderer->config.flags & SPRITE_RENDER_OPT_SCREEN_CULLING) {
                    if (screen_x + cell_w < 0 || screen_y + cell_h < 0 ||
                        screen_x > screen_w || screen_y > screen_h) {
                        renderer->stats.voxels_culled++;
                        continue;
                    }
                }

                // Get sprite for this voxel type
                SDL_Texture* sprite = sprite_renderer_get_sprite(renderer, voxel->type);
                if (!sprite) continue;

                // Render sprite
                SDL_Rect dest_rect = {screen_x, screen_y, cell_w, cell_h};
                SDL_RenderCopy(sdl_renderer, sprite, NULL, &dest_rect);

                renderer->stats.voxels_rendered++;
            }
        }
    }

    // Render grid if enabled
    if (renderer->config.enable_grid) {
        SDL_SetRenderDrawColor(sdl_renderer, 100, 100, 100, 128);

        // X-axis grid lines (vertical lines in isometric view)
        for (uint32_t gx = 0; gx <= world_width; gx++) {
            int sx = adjusted_screen_center_x + (int)(gx * iso_x_mult * cell_w);
            SDL_RenderDrawLine(sdl_renderer, sx, 0, sx, screen_h);
        }

        // Y-axis grid lines (horizontal lines in isometric view)
        for (uint32_t gy = 0; gy <= world_height; gy++) {
            int sy = adjusted_screen_center_y + (int)(gy * iso_y_mult * cell_h);
            SDL_RenderDrawLine(sdl_renderer, 0, sy, screen_w, sy);
        }
    }

    renderer->stats.draw_time_ns = get_time_ns() - draw_start_time;
    renderer->stats.render_time_ns = get_time_ns() - total_start_time;

    // Increment frame counter
    renderer->current_frame++;
}
