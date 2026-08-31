#include "cpu_renderer_optimized.h"
#include "isometric_renderer.h"
#include "world.h"
#include "universe.h"
#include "constants.h"
#include "game_state.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test program to generate bitmaps for different CPU render phases
int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
    printf("=== CPU Render Phases Test ===\n");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create window and renderer
    SDL_Window *window = SDL_CreateWindow("CPU Render Phases Test",
                                         SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                         800, 600, SDL_WINDOW_HIDDEN);
    if (!window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create universe with home world (same as universe-ref)
    GameWorlds *gw = game_worlds_create("reference");
    if (!gw || !game_worlds_generate_all(gw, "reference")) {
        printf("Failed to create/generate game worlds\n");
        if (gw) game_worlds_destroy(gw);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("Generated test world: %dx%dx%d\n", (int)gw->home_world->width, (int)gw->home_world->height, (int)gw->home_world->depth);

    // Create isometric renderer (same dimensions as universe-ref)
    IsometricRenderer *iso_renderer = isometric_renderer_create(800, 600);
    if (!iso_renderer) {
        printf("Isometric renderer creation failed\n");
        game_worlds_destroy(gw);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize CPU renderer color table (same as universe-ref)
    cpu_renderer_init_color_table();

    // Set up game worlds (same as universe-ref)
    isometric_renderer_set_game_worlds(iso_renderer, gw);

    // Configure isometric renderer (same as universe-ref)
    isometric_renderer_set_screen_size(iso_renderer, 800, 600);
    isometric_renderer_set_auto_center(iso_renderer, false);
    iso_renderer->neighbor_inclusion_radius = 0;

    // Set camera position to match universe-ref
    if (gw->home_world) {
        iso_renderer->camera_x = (int)gw->home_world->width / 2;
        iso_renderer->camera_y = (int)gw->home_world->height / 2;
        iso_renderer->camera_z = (int)gw->home_world->depth - 1; // render full depth up to max Z
    }

    // Test different optimization configurations
    CPURendererConfig configs[] = {
        // Phase 1: No optimizations (baseline) - matches universe-ref
        {
            .flags = CPU_RENDER_OPT_NONE,
            .batch_size = 1024,
            .chunk_size_x = 16,
            .chunk_size_y = 16,
            .chunk_size_z = 256,
            .enable_grid = false,
            .enable_debug_overlay = false,
            .max_visible_voxels = 100000,
            .culling_margin = 32.0f
        },
        // Phase 2: Chunk culling only - matches universe-ref
        {
            .flags = CPU_RENDER_OPT_CHUNK_CULLING,
            .batch_size = 1024,
            .chunk_size_x = 16,
            .chunk_size_y = 16,
            .chunk_size_z = 256,
            .enable_grid = false,
            .enable_debug_overlay = false,
            .max_visible_voxels = 100000,
            .culling_margin = 32.0f
        },
        // Phase 3: Screen Culling - matches universe-ref
        {
            .flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING,
            .batch_size = 1024,
            .chunk_size_x = 16,
            .chunk_size_y = 16,
            .chunk_size_z = 256,
            .enable_grid = false,
            .enable_debug_overlay = false,
            .max_visible_voxels = 100000,
            .culling_margin = 32.0f
        },
        // Phase 4: No Face Culling (Quality) - matches universe-ref
        {
            .flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING | CPU_RENDER_OPT_COLOR_TABLE,
            .batch_size = 1024,
            .chunk_size_x = 16,
            .chunk_size_y = 16,
            .chunk_size_z = 256,
            .enable_grid = false,
            .enable_debug_overlay = false,
            .max_visible_voxels = 100000,
            .culling_margin = 32.0f
        },
        // Phase 5: With Face Culling (Performance) - matches universe-ref
        {
            .flags = CPU_RENDER_OPT_CHUNK_CULLING | CPU_RENDER_OPT_SCREEN_CULLING | CPU_RENDER_OPT_COLOR_TABLE | CPU_RENDER_OPT_FACE_CULLING,
            .batch_size = 1024,
            .chunk_size_x = 16,
            .chunk_size_y = 16,
            .chunk_size_z = 256,
            .enable_grid = false,
            .enable_debug_overlay = false,
            .max_visible_voxels = 100000,
            .culling_margin = 32.0f
        },
        // Phase 6: All Optimizations - matches universe-ref
        {
            .flags = CPU_RENDER_OPT_ALL,
            .batch_size = 1024,
            .chunk_size_x = 16,
            .chunk_size_y = 16,
            .chunk_size_z = 256,
            .enable_grid = false,
            .enable_debug_overlay = false,
            .max_visible_voxels = 100000,
            .culling_margin = 32.0f
        }
    };

    const char* phase_names[] = {
        "none",
        "chunk",
        "screen",
        "quality",
        "performance",
        "batch",
        "loop",
        "memory",
        "simd",
        "cache",
        "all"
    };

    // Render each phase and save bitmap
    for (int phase = 0; phase < 11; phase++) {
        printf("Rendering phase %d: %s\n", phase + 1, phase_names[phase]);

        // Clear renderer
        SDL_SetRenderDrawColor(renderer, 20, 22, 35, 255);
        SDL_RenderClear(renderer);

        // Reset stats
        cpu_renderer_reset_stats();

        // Render with current config
        cpu_renderer_render_optimized(iso_renderer, renderer, &configs[phase]);

        // Get stats
        CPURendererStats stats = cpu_renderer_get_stats();

        // Present frame
        SDL_RenderPresent(renderer);

        // Save bitmap (same approach as universe-ref)
        char filename[256];
        snprintf(filename, sizeof(filename), "assets/cpu_render_phase_%d_%s.bmp",
                phase + 1, phase_names[phase]);

        // Export bitmap using the same approach as universe-ref
        SDL_Surface *surf32 = SDL_CreateRGBSurfaceWithFormat(0, 800, 600, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf32) {
            if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surf32->pixels, surf32->pitch) == 0) {
                SDL_Surface *rgb_surf = SDL_ConvertSurfaceFormat(surf32, SDL_PIXELFORMAT_RGB24, 0);
                if (rgb_surf) {
                    if (SDL_SaveBMP(rgb_surf, filename) == 0) {
                        printf("  Saved: %s\n", filename);
                    } else {
                        printf("  Failed to save: %s\n", filename);
                    }
                    SDL_FreeSurface(rgb_surf);
                }
            }
            SDL_FreeSurface(surf32);
        }

        // Print performance stats
        printf("  Performance: %.2f ms, %.1f FPS\n",
               stats.render_time_ns / 1000000.0,
               1000.0 / (stats.render_time_ns / 1000000.0));
        printf("  Voxels: processed=%llu, culled=%llu, rendered=%llu\n",
               stats.voxels_processed, stats.voxels_culled, stats.voxels_rendered);
        printf("  Chunks: processed=%llu, culled=%llu\n",
               stats.chunks_processed, stats.chunks_culled);
        printf("  Culling efficiency: %.1f%%\n",
               stats.voxels_processed > 0 ? (100.0 * stats.voxels_culled) / stats.voxels_processed : 0.0);
        printf("\n");
    }

    // Cleanup
    isometric_renderer_destroy(iso_renderer);
    game_worlds_destroy(gw);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("=== CPU Render Phases Test Complete ===\n");
    printf("Generated bitmaps in assets/ directory:\n");
    for (int phase = 0; phase < 11; phase++) {
        printf("  - cpu_render_phase_%d_%s.bmp\n", phase + 1, phase_names[phase]);
    }

    return 0;
}
