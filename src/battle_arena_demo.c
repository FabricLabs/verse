// battle_arena_demo.c
// Demonstration of the improved voxel-based battle arena with mouse controls

#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "world.h"
#include "isometric_renderer.h"
#include "input_manager.h"
#include "constants.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
#define DEMO_SEED "battle_arena_demo"

typedef struct {
    SDL_Window* window;
    SDL_Renderer* sdl_renderer;
    IsometricRenderer* iso_renderer;
    InputManager* input_manager;
    GameWorlds* game_worlds;
    bool running;

    // Player state
    int player_x, player_y, player_z;
    bool player_moving;
    float move_start_time;
    float move_duration;
    int move_start_x, move_start_y, move_start_z;
    int move_target_x, move_target_y, move_target_z;
} BattleArenaDemo;

// Initialize the demo
BattleArenaDemo* battle_arena_demo_create() {
    BattleArenaDemo* demo = calloc(1, sizeof(BattleArenaDemo));
    if (!demo) return NULL;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        free(demo);
        return NULL;
    }

    // Create window
    demo->window = SDL_CreateWindow("VERSE - Battle Arena Demo",
                                  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  SCREEN_WIDTH, SCREEN_HEIGHT,
                                  SDL_WINDOW_SHOWN);
    if (!demo->window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        free(demo);
        return NULL;
    }

    // Create renderer
    demo->sdl_renderer = SDL_CreateRenderer(demo->window, -1, SDL_RENDERER_ACCELERATED);
    if (!demo->sdl_renderer) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(demo->window);
        SDL_Quit();
        free(demo);
        return NULL;
    }

    // Create isometric renderer
    demo->iso_renderer = isometric_renderer_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!demo->iso_renderer) {
        printf("Failed to create isometric renderer\n");
        SDL_DestroyRenderer(demo->sdl_renderer);
        SDL_DestroyWindow(demo->window);
        SDL_Quit();
        free(demo);
        return NULL;
    }

    // Create input manager
    demo->input_manager = malloc(sizeof(InputManager));
    if (!demo->input_manager) {
        printf("Failed to create input manager\n");
        isometric_renderer_destroy(demo->iso_renderer);
        SDL_DestroyRenderer(demo->sdl_renderer);
        SDL_DestroyWindow(demo->window);
        SDL_Quit();
        free(demo);
        return NULL;
    }
    input_manager_init(demo->input_manager);
    input_manager_set_state(demo->input_manager, INPUT_STATE_GAME_WORLD);

    // Create game worlds
    demo->game_worlds = game_worlds_create(DEMO_SEED);
    if (!demo->game_worlds) {
        printf("Failed to create game worlds\n");
        free(demo->input_manager);
        isometric_renderer_destroy(demo->iso_renderer);
        SDL_DestroyRenderer(demo->sdl_renderer);
        SDL_DestroyWindow(demo->window);
        SDL_Quit();
        free(demo);
        return NULL;
    }

    // Generate worlds
    if (!game_worlds_generate_all(demo->game_worlds, DEMO_SEED)) {
        printf("Failed to generate game worlds\n");
        game_worlds_destroy(demo->game_worlds);
        free(demo->input_manager);
        isometric_renderer_destroy(demo->iso_renderer);
        SDL_DestroyRenderer(demo->sdl_renderer);
        SDL_DestroyWindow(demo->window);
        SDL_Quit();
        free(demo);
        return NULL;
    }

    // Set up renderer with game worlds
    isometric_renderer_set_game_worlds(demo->iso_renderer, demo->game_worlds);

    // Initialize player position at center of home world
    demo->player_x = WORLD_SIZE_X / 2;
    demo->player_y = WORLD_SIZE_Y / 2;
    demo->player_z = WORLD_SIZE_Z * 0.75f + 1; // On top of the floating island
    demo->player_moving = false;

    // Set camera to follow player
    isometric_renderer_set_camera(demo->iso_renderer, demo->player_x, demo->player_y, demo->player_z);

    demo->running = true;
    return demo;
}

// Destroy the demo
void battle_arena_demo_destroy(BattleArenaDemo* demo) {
    if (!demo) return;

    if (demo->game_worlds) {
        game_worlds_destroy(demo->game_worlds);
    }
    if (demo->input_manager) {
        free(demo->input_manager);
    }
    if (demo->iso_renderer) {
        isometric_renderer_destroy(demo->iso_renderer);
    }
    if (demo->sdl_renderer) {
        SDL_DestroyRenderer(demo->sdl_renderer);
    }
    if (demo->window) {
        SDL_DestroyWindow(demo->window);
    }

    SDL_Quit();
    free(demo);
}

// Update player movement
void battle_arena_demo_update_movement(BattleArenaDemo* demo, float delta_time) {
    if (!demo->player_moving) return;

    // Update move progress
    float progress = (SDL_GetTicks() / 1000.0f - demo->move_start_time) / demo->move_duration;
    if (progress >= 1.0f) {
        // Movement complete
        demo->player_x = demo->move_target_x;
        demo->player_y = demo->move_target_y;
        demo->player_z = demo->move_target_z;
        demo->player_moving = false;

        // Clear movement target
        demo->iso_renderer->movement_target_set = false;
        demo->iso_renderer->show_movement_preview = false;

        printf("Player moved to: (%d, %d, %d)\n", demo->player_x, demo->player_y, demo->player_z);
    } else {
        // Interpolate position
        demo->player_x = demo->move_start_x + (demo->move_target_x - demo->move_start_x) * progress;
        demo->player_y = demo->move_start_y + (demo->move_target_y - demo->move_start_y) * progress;
        demo->player_z = demo->move_start_z + (demo->move_target_z - demo->move_start_z) * progress;
    }

    // Update camera to follow player
    isometric_renderer_set_camera(demo->iso_renderer, demo->player_x, demo->player_y, demo->player_z);
}

// Handle events
void battle_arena_demo_handle_events(BattleArenaDemo* demo) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                demo->running = false;
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    demo->running = false;
                } else {
                    input_manager_handle_key(demo->input_manager, event.key.keysym.sym);
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                // Handle mouse input through isometric renderer
                if (isometric_renderer_handle_mouse_click(demo->iso_renderer,
                                                        event.button.x, event.button.y,
                                                        event.button.button)) {
                    // Check if a movement target was set
                    if (demo->iso_renderer->movement_target_set && !demo->player_moving) {
                        // Start player movement
                        demo->move_start_x = demo->player_x;
                        demo->move_start_y = demo->player_y;
                        demo->move_start_z = demo->player_z;
                        demo->move_target_x = demo->iso_renderer->movement_target_x;
                        demo->move_target_y = demo->iso_renderer->movement_target_y;
                        demo->move_target_z = demo->iso_renderer->movement_target_z;
                        demo->move_start_time = SDL_GetTicks() / 1000.0f;
                        demo->move_duration = 1.0f; // 1 second movement
                        demo->player_moving = true;

                        printf("Player moving from (%d, %d, %d) to (%d, %d, %d)\n",
                               demo->move_start_x, demo->move_start_y, demo->move_start_z,
                               demo->move_target_x, demo->move_target_y, demo->move_target_z);
                    }
                }
                break;
        }
    }
}

// Render the demo
void battle_arena_demo_render(BattleArenaDemo* demo) {
    // Clear screen
    SDL_SetRenderDrawColor(demo->sdl_renderer, 135, 206, 235, 255); // Sky blue
    SDL_RenderClear(demo->sdl_renderer);

    // Render the voxel world
    isometric_renderer_render(demo->iso_renderer, demo->sdl_renderer);

    // Draw UI text
    // Note: For simplicity, this demo doesn't include text rendering
    // In a full implementation, you would use SDL_ttf or similar

    // Present the frame
    SDL_RenderPresent(demo->sdl_renderer);
}

// Main demo loop
void battle_arena_demo_run(BattleArenaDemo* demo) {
    Uint32 last_time = SDL_GetTicks();

    printf("Battle Arena Demo Controls:\n");
    printf("- Left click: Select player or voxels\n");
    printf("- Right click: Move player to location\n");
    printf("- ESC: Exit demo\n\n");

    while (demo->running) {
        Uint32 current_time = SDL_GetTicks();
        float delta_time = (current_time - last_time) / 1000.0f;
        last_time = current_time;

        // Handle events
        battle_arena_demo_handle_events(demo);

        // Update
        battle_arena_demo_update_movement(demo, delta_time);

        // Render
        battle_arena_demo_render(demo);

        // Cap to ~60 FPS
        SDL_Delay(16);
    }
}

// Main function
int main(int argc, char* argv[]) {
    (void)argc; (void)argv; // Suppress unused parameter warnings

    printf("VERSE Battle Arena Demo\n");
    printf("=======================\n\n");

    BattleArenaDemo* demo = battle_arena_demo_create();
    if (!demo) {
        printf("Failed to create demo\n");
        return 1;
    }

    battle_arena_demo_run(demo);
    battle_arena_demo_destroy(demo);

    printf("Demo finished. Thanks for playing!\n");
    return 0;
}
