// render_test.c
// Test program for the rendering system fixes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "window.h"
#include "isometric_renderer.h"
#include "world.h"
#include "game_state.h"

int main(int argc, char *argv[]) {
    printf("RENDER TEST: Starting rendering system test\n");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("RENDER TEST: SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() < 0) {
        printf("RENDER TEST: TTF initialization failed: %s\n", TTF_GetError());
        return 1;
    }

    printf("RENDER TEST: SDL initialized successfully\n");

    // Create window
    SDL_Window *window = SDL_CreateWindow(
        "VERSE Render Test",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        1024, 768,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!window) {
        printf("RENDER TEST: Window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create renderer
    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!renderer) {
        printf("RENDER TEST: Renderer creation failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("RENDER TEST: Window and renderer created successfully\n");

    // Create isometric renderer
    IsometricRenderer *iso_renderer = isometric_renderer_create(1024, 768);
    if (!iso_renderer) {
        printf("RENDER TEST: Failed to create isometric renderer\n");
        return 1;
    }

    printf("RENDER TEST: Isometric renderer created successfully\n");

    // Create a test world
    World *world = world_create(32, 32, 32);
    if (!world) {
        printf("RENDER TEST: Failed to create test world\n");
        return 1;
    }

    printf("RENDER TEST: Test world created (32x32x32)\n");

    // Generate the world with a simple pattern for testing
    printf("RENDER TEST: Generating test world pattern...\n");

    // Create a simple test pattern - a cube of stone in the center
    for (int x = 14; x < 18; x++) {
        for (int y = 14; y < 18; y++) {
            for (int z = 14; z < 18; z++) {
                Voxel *voxel = world_get_voxel(world, x, y, z);
                if (voxel) {
                    voxel->type = VOXEL_STONE;
                }
            }
        }
    }

    // Add some grass on top
    for (int x = 14; x < 18; x++) {
        for (int z = 14; z < 18; z++) {
            Voxel *voxel = world_get_voxel(world, x, 18, z);
            if (voxel) {
                voxel->type = VOXEL_GRASS;
            }
        }
    }

    // Add some dirt around the base
    for (int x = 13; x < 19; x++) {
        for (int z = 13; z < 19; z++) {
            Voxel *voxel = world_get_voxel(world, x, 13, z);
            if (voxel) {
                voxel->type = VOXEL_SOIL;
            }
        }
    }

    printf("RENDER TEST: Test world pattern generated\n");

    // Set camera position
    isometric_renderer_set_camera(iso_renderer, 16, 16, 16);
    printf("RENDER TEST: Camera set to position (16, 16, 16)\n");

    // Create game worlds structure for the renderer
    GameWorlds *game_worlds = malloc(sizeof(GameWorlds));
    if (!game_worlds) {
        printf("RENDER TEST: Failed to allocate game worlds\n");
        return 1;
    }

    // Initialize game worlds
    memset(game_worlds, 0, sizeof(GameWorlds));
    game_worlds->home_world = world;

    // Set game worlds for renderer
    isometric_renderer_set_game_worlds(iso_renderer, game_worlds);
    printf("RENDER TEST: Game worlds set for renderer\n");

    // Main render loop
    printf("RENDER TEST: Starting render loop (press ESC to exit)\n");

    SDL_Event event;
    bool running = true;
    int frame_count = 0;

    while (running) {
        frame_count++;

        // Handle events
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(renderer, 25, 25, 50, 255);
        SDL_RenderClear(renderer);

        // Render the world
        printf("RENDER TEST: Frame %d - Rendering world\n", frame_count);
        isometric_renderer_render(iso_renderer, renderer);

        // Present the frame
        SDL_RenderPresent(renderer);

        // Limit to 30 FPS for debugging
        SDL_Delay(33);
    }

    printf("RENDER TEST: Render loop ended\n");

    // Cleanup
    printf("RENDER TEST: Cleaning up...\n");

    isometric_renderer_destroy(iso_renderer);
    world_destroy(world);
    free(game_worlds);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    printf("RENDER TEST: Test completed successfully\n");
    return 0;
}
