#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "ui_system.h"
#include "world_editor_ui.h"

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800

// Test state
static bool g_running = true;
static int g_current_tool = 0;
static int g_shape_mode = 0;
static bool g_adj_menu_open = false;
static int g_selected_voxel = 1;
static int g_current_z = 50;

// Callbacks
static void on_tool_change(int tool_id, void* user_data) {
    printf("Tool changed to: %d\n", tool_id);
    g_current_tool = tool_id;
}

static void on_shape_mode_change(int mode_id, void* user_data) {
    printf("Shape mode changed to: %d\n", mode_id);
    g_shape_mode = mode_id;
}

static void on_voxel_selection(int voxel_id, void* user_data) {
    printf("Voxel selected: %d\n", voxel_id);
    g_selected_voxel = voxel_id;
}

static void on_file_operation(int operation_id, const char* filename, void* user_data) {
    const char* operations[] = {"New", "Open", "Save", "Save As", "Export"};
    printf("File operation: %s", operations[operation_id]);
    if (filename) printf(" - %s", filename);
    printf("\n");
}

static void on_adjacent_direction(int direction_id, void* user_data) {
    const char* directions[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    printf("Adjacent direction: %s\n", directions[direction_id]);
}

int main() {
    printf("Comprehensive UI Test - Testing all World Editor UI elements\n");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() < 0) {
        printf("TTF initialization failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    // Create window and renderer
    SDL_Window* window = SDL_CreateWindow(
        "Comprehensive UI Test - World Editor",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    printf("Window and renderer created successfully\n");

    // Create the unified UI system
    WorldEditorUI* editor_ui = world_editor_ui_create(renderer, NULL);
    if (!editor_ui) {
        printf("Failed to create World Editor UI\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    printf("World Editor UI created successfully\n");

    // Create all UI panels
    printf("Creating UI panels...\n");

    // File panel (top menu)
    world_editor_ui_create_file_panel(editor_ui, 10, 10);
    world_editor_ui_set_file_callback(editor_ui, on_file_operation, NULL);
    printf("  - File panel created\n");

    // Tools panel (left side)
    world_editor_ui_create_toolbar(editor_ui, 10, 50);
    world_editor_ui_set_tool_callback(editor_ui, on_tool_change, NULL);
    world_editor_ui_set_shape_callback(editor_ui, on_shape_mode_change, NULL);
    world_editor_ui_set_adjacent_callback(editor_ui, on_adjacent_direction, NULL);
    printf("  - Tools panel created\n");

    // Palette panel (right side)
    world_editor_ui_create_palette(editor_ui, WINDOW_WIDTH - 200, 50);
    world_editor_ui_set_voxel_callback(editor_ui, on_voxel_selection, NULL);
    printf("  - Palette panel created\n");

    // Recent swatches panel
    world_editor_ui_create_recent_panel(editor_ui, 10, 120);
    printf("  - Recent swatches panel created\n");

    // Z-level swatches panel (left side)
    world_editor_ui_create_z_swatches(editor_ui, 10, 200);
    printf("  - Z-level swatches panel created\n");

    // Actor panel
    world_editor_ui_create_actor_panel(editor_ui, 10, 400);
    printf("  - Actor panel created\n");

    // Info panel
    world_editor_ui_create_info_panel(editor_ui, WINDOW_WIDTH - 300, 300);
    printf("  - Info panel created\n");

    // Settings panel
    world_editor_ui_create_settings_panel(editor_ui, WINDOW_WIDTH - 300, 500);
    printf("  - Settings panel created\n");

    printf("All UI panels created successfully\n");
    printf("\nControls:\n");
    printf("  Mouse: Click UI elements\n");
    printf("  F: Toggle file menu\n");
    printf("  T: Cycle through tools\n");
    printf("  S: Toggle shape mode\n");
    printf("  A: Toggle adjacent menu\n");
    printf("  V: Cycle through voxel types\n");
    printf("  Z: Change Z level\n");
    printf("  ESC: Quit\n\n");

    // Main loop
    while (g_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                g_running = false;
            }
            else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        g_running = false;
                        break;
                    case SDLK_f:
                        printf("F key pressed - toggling file menu\n");
                        break;
                    case SDLK_t:
                        g_current_tool = (g_current_tool + 1) % 3;
                        printf("T key pressed - tool changed to %d\n", g_current_tool);
                        break;
                    case SDLK_s:
                        g_shape_mode = (g_shape_mode + 1) % 2;
                        printf("S key pressed - shape mode changed to %d\n", g_shape_mode);
                        break;
                    case SDLK_a:
                        g_adj_menu_open = !g_adj_menu_open;
                        printf("A key pressed - adjacent menu %s\n", g_adj_menu_open ? "opened" : "closed");
                        break;
                    case SDLK_v:
                        g_selected_voxel = (g_selected_voxel + 1) % 10;
                        printf("V key pressed - voxel type changed to %d\n", g_selected_voxel);
                        break;
                    case SDLK_z:
                        g_current_z = (g_current_z + 5) % 100;
                        printf("Z key pressed - Z level changed to %d\n", g_current_z);
                        break;
                }
            }

            // Handle UI events
            if (world_editor_ui_handle_event(editor_ui, &event)) {
                // Event was handled by UI system
                continue;
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
        SDL_RenderClear(renderer);

        // Render all UI elements
        world_editor_ui_render(editor_ui);

        // Render some test content to show the UI is working
        SDL_SetRenderDrawColor(renderer, 100, 100, 120, 255);
        SDL_Rect test_rect = {400, 200, 200, 150};
        SDL_RenderFillRect(renderer, &test_rect);

        // Render current state info
        char info_text[256];
        snprintf(info_text, sizeof(info_text),
                "Tool: %d | Shape: %d | Voxel: %d | Z: %d | Adj: %s",
                g_current_tool, g_shape_mode, g_selected_voxel, g_current_z,
                g_adj_menu_open ? "ON" : "OFF");

        // Note: We'd need to implement text rendering here for the test info
        // For now, just show it in the console

        // Present renderer
        SDL_RenderPresent(renderer);

        // Cap frame rate
        SDL_Delay(16); // ~60 FPS
    }

    printf("\nShutting down...\n");

    // Cleanup
    world_editor_ui_destroy(editor_ui);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    printf("Comprehensive UI test completed successfully\n");
    return 0;
}
