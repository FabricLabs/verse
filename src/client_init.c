/*
 * client_init.c - Initialization and cleanup implementation
 */

#include "client_init.h"
#include "client_state.h"
#include "client_audio.h"
#include "client_input.h"
#include "client_render.h"
#include "window.h"
#include "game_state.h"
#include "isometric_renderer.h"
#include "background_music.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

static bool g_initialized = false;
static IsometricRenderer* g_renderer = NULL;

// Initialize all client systems
bool client_init(int argc, char* argv[]) {
    if (g_initialized) {
        return true;
    }

    printf("Verse client starting...\n");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }

    // Create window
    if (!window_init("VERSE", 1200, 800)) {
        printf("Failed to create window\n");
        SDL_Quit();
        return false;
    }

    // Initialize isometric renderer
    g_renderer = isometric_renderer_create(1200, 800);
    if (!g_renderer) {
        printf("Failed to initialize isometric renderer\n");
        window_cleanup();
        SDL_Quit();
        return false;
    }

    // Initialize client state
    if (!client_state_init()) {
        printf("Failed to initialize client state\n");
        isometric_renderer_destroy(g_renderer);
        window_cleanup();
        SDL_Quit();
        return false;
    }

    // Initialize audio systems
    if (!client_audio_init()) {
        printf("Failed to initialize audio systems\n");
        client_state_shutdown();
        isometric_renderer_destroy(g_renderer);
        window_cleanup();
        SDL_Quit();
        return false;
    }

    // Initialize input system
    client_input_init();

    // Initialize rendering system
    client_render_init();

    // Get game state and initialize
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) {
        printf("Failed to get game state\n");
        client_shutdown();
        return false;
    }

    // Set initial window callbacks
    window_set_key_callback(handle_key_press);
    window_set_button_callback(handle_button_click);
    window_set_mouse_callback(handle_mouse_click);
    window_set_mouse_motion_callback(handle_mouse_motion);
    // Note: mouse wheel and text input callbacks not available in current window.h

    // Initialize with empty main menu world
    g_game_state->main_menu_world = world_create(32, 32, 16);
    if (!g_game_state->main_menu_world) {
        printf("Failed to create main menu world\n");
        client_shutdown();
        return false;
    }

    // Generate a simple world for the main menu background
    world_generate_with_type(g_game_state->main_menu_world, "menu_world", WORLD_TYPE_ARENA);

    // Set initial player position for menu
    g_game_state->player_x = 16;
    g_game_state->player_y = 16;
    g_game_state->player_z = 5;

    // Sync window audio settings
    window_sync_settings_with_audio((BackgroundMusicSystem*)client_audio_get_music_system());

    g_initialized = true;
    printf("Client initialization complete\n");
    return true;
}

// Shutdown all client systems
void client_shutdown(void) {
    if (!g_initialized) {
        return;
    }

    printf("Shutting down client...\n");

    // Get game state for cleanup
    GameState* g_game_state = client_state_get_game();

    // Clean up main menu world
    if (g_game_state && g_game_state->main_menu_world) {
        world_destroy(g_game_state->main_menu_world);
        g_game_state->main_menu_world = NULL;
    }

    // Shutdown subsystems in reverse order
    client_audio_shutdown();
    client_state_shutdown();
    isometric_renderer_destroy(g_renderer);
    window_cleanup();
    SDL_Quit();

    g_initialized = false;
    printf("Client shutdown complete\n");
}

// Check if client is initialized
bool client_is_initialized(void) {
    return g_initialized;
}
