// toolbar_test.c
// Simple test program to verify the toolbar migration works correctly

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
static int g_selected_tool = WORLD_EDITOR_TOOL_SELECT;

// ============================================================================
// CALLBACK FUNCTIONS
// ============================================================================

static void on_tool_change(int tool_id, void* user_data) {
    printf("Tool changed to: %d\n", tool_id);
    g_selected_tool = tool_id;

    // Update the UI to reflect the new selection
    if (g_editor_ui) {
        world_editor_ui_set_selected_tool(g_editor_ui, tool_id);
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
        "Toolbar Test",
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

    // Set up tool change callback
    world_editor_ui_set_tool_callback(g_editor_ui, on_tool_change, NULL);

    // Create the toolbar
    world_editor_ui_create_toolbar(g_editor_ui, 10, 10);

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

                case SDLK_1:
                    printf("Keyboard shortcut: Select tool\n");
                    on_tool_change(WORLD_EDITOR_TOOL_SELECT, NULL);
                    break;

                case SDLK_2:
                    printf("Keyboard shortcut: Draw tool\n");
                    on_tool_change(WORLD_EDITOR_TOOL_DRAW, NULL);
                    break;

                case SDLK_3:
                    printf("Keyboard shortcut: Cube tool\n");
                    on_tool_change(WORLD_EDITOR_TOOL_CUBE, NULL);
                    break;

                case SDLK_4:
                    printf("Keyboard shortcut: Sphere tool\n");
                    on_tool_change(WORLD_EDITOR_TOOL_SPHERE, NULL);
                    break;

                case SDLK_5:
                    printf("Keyboard shortcut: Adjacent tool\n");
                    on_tool_change(WORLD_EDITOR_TOOL_ADJACENT, NULL);
                    break;

                case SDLK_6:
                    printf("Keyboard shortcut: Fill tool\n");
                    on_tool_change(WORLD_EDITOR_TOOL_FILL, NULL);
                    break;

                case SDLK_7:
                    printf("Keyboard shortcut: Erase tool\n");
                    on_tool_change(WORLD_EDITOR_TOOL_ERASE, NULL);
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
    printf("Toolbar Test - Testing the migrated toolbar\n");

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

    printf("Toolbar test initialized successfully\n");
    printf("Controls:\n");
    printf("  1-7: Select tools\n");
    printf("  Mouse: Click toolbar buttons\n");
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

    printf("Toolbar test closed\n");
    return 0;
}
