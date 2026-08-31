// file_menu_test.c
// Simple test program to verify the file menu migration works correctly

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "world_editor_ui.h"

#define WINDOW_W 800
#define WINDOW_H 600

// Global state
static bool g_running = true;
static WorldEditorUI* g_editor_ui = NULL;

// ============================================================================
// CALLBACK FUNCTIONS
// ============================================================================

static void on_file_operation(int operation_id, const char* filename, void* user_data) {
    (void)user_data; // Unused parameter

    printf("File operation: %d", operation_id);
    if (filename) {
        printf(", filename: %s", filename);
    }
    printf("\n");

    switch (operation_id) {
        case WORLD_EDITOR_FILE_NEW:
            printf("-> New world file\n");
            break;
        case WORLD_EDITOR_FILE_OPEN:
            printf("-> Open world file\n");
            break;
        case WORLD_EDITOR_FILE_SAVE:
            printf("-> Save world file\n");
            break;
        case WORLD_EDITOR_FILE_SAVE_AS:
            printf("-> Save world file as...\n");
            break;
        case WORLD_EDITOR_FILE_EXPORT:
            printf("-> Export world file\n");
            break;
        default:
            printf("-> Unknown file operation\n");
            break;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

static bool init_sdl(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }

    if (TTF_Init() < 0) {
        printf("TTF initialization failed: %s\n", TTF_GetError());
        return false;
    }

    return true;
}

static SDL_Window* create_window(void) {
    SDL_Window* window = SDL_CreateWindow(
        "File Menu Test",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        return NULL;
    }

    return window;
}

static SDL_Renderer* create_renderer(SDL_Window* window) {
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return NULL;
    }

    return renderer;
}

static bool init_world_editor_ui(SDL_Renderer* renderer) {
    g_editor_ui = world_editor_ui_create(renderer, NULL);
    if (!g_editor_ui) {
        printf("Failed to create world editor UI\n");
        return false;
    }

    // Set up file operation callback
    world_editor_ui_set_file_callback(g_editor_ui, on_file_operation, NULL);

    // Create the file panel
    world_editor_ui_create_file_panel(g_editor_ui, 10, 10);

    return true;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

static void handle_events(void) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        // Handle quit event
        if (event.type == SDL_QUIT) {
            g_running = false;
            continue;
        }

        // Handle window resize
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
            // Handle window resize if needed
            continue;
        }

        // Let the UI system handle the event
        if (world_editor_ui_handle_event(g_editor_ui, &event)) {
            // Event was handled by UI system
            continue;
        }

        // Handle keyboard shortcuts for testing
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    g_running = false;
                    break;

                case SDLK_n:
                    printf("Keyboard shortcut: New file\n");
                    on_file_operation(WORLD_EDITOR_FILE_NEW, NULL, NULL);
                    break;

                case SDLK_o:
                    printf("Keyboard shortcut: Open file\n");
                    on_file_operation(WORLD_EDITOR_FILE_OPEN, "test.world", NULL);
                    break;

                case SDLK_s:
                    printf("Keyboard shortcut: Save file\n");
                    on_file_operation(WORLD_EDITOR_FILE_SAVE, "current.world", NULL);
                    break;

                case SDLK_e:
                    printf("Keyboard shortcut: Export file\n");
                    on_file_operation(WORLD_EDITOR_FILE_EXPORT, "export.obj", NULL);
                    break;
            }
        }
    }
}

static void render(void) {
    // Clear screen
    SDL_SetRenderDrawColor(g_editor_ui->ui->renderer, 50, 50, 50, 255);
    SDL_RenderClear(g_editor_ui->ui->renderer);

    // Render UI
    world_editor_ui_render(g_editor_ui);

    // Present renderer
    SDL_RenderPresent(g_editor_ui->ui->renderer);
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc; (void)argv; // Suppress unused parameter warnings

    printf("File Menu Test - Testing the migrated file menu\n");

    // Initialize SDL
    if (!init_sdl()) {
        return 1;
    }

    // Create window
    SDL_Window* window = create_window();
    if (!window) {
        SDL_Quit();
        return 1;
    }

    // Create renderer
    SDL_Renderer* renderer = create_renderer(window);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Initialize world editor UI
    if (!init_world_editor_ui(renderer)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("File menu test initialized successfully\n");
    printf("Controls:\n");
    printf("  N: New file\n");
    printf("  O: Open file\n");
    printf("  S: Save file\n");
    printf("  E: Export file\n");
    printf("  Mouse: Click file menu buttons\n");
    printf("  ESC: Quit\n");

    // Main loop
    while (g_running) {
        handle_events();
        render();
        SDL_Delay(16); // ~60 FPS
    }

    // Cleanup
    if (g_editor_ui) world_editor_ui_destroy(g_editor_ui);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("File menu test closed\n");
    return 0;
}
