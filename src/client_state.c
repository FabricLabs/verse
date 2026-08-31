/*
 * client_state.c - Client state management implementation
 */

#include "client_state.h"
#include <SDL2/SDL.h>
#include <stdio.h>

// Global state variables
static GameState *g_game_state = NULL;
static bool g_should_exit = false;
static bool g_show_title_screen = true;
static float g_title_fade_alpha = 0.0f;
static Uint32 g_title_start_time = 0;

// Global variables expected by window system
int g_show_exit_prompt = 0;

// Initialize client state
bool client_state_init(void) {
    if (g_game_state) {
        return true; // Already initialized
    }

    g_game_state = game_state_create();
    if (!g_game_state) {
        printf("Failed to create game state\n");
        return false;
    }

    g_should_exit = false;
    g_show_title_screen = true;
    g_title_fade_alpha = 0.0f;
    g_title_start_time = SDL_GetTicks();
    g_show_exit_prompt = 0;

    return true;
}

// Shutdown client state
void client_state_shutdown(void) {
    if (g_game_state) {
        game_state_destroy(g_game_state);
        g_game_state = NULL;
    }
}

// Get game state
GameState* client_state_get_game(void) {
    return g_game_state;
}

// Title screen state
bool client_state_is_title_screen(void) {
    return g_show_title_screen;
}

void client_state_set_title_screen(bool show) {
    g_show_title_screen = show;
}

float client_state_get_title_fade(void) {
    return g_title_fade_alpha;
}

void client_state_set_title_fade(float alpha) {
    g_title_fade_alpha = alpha;
}

unsigned int client_state_get_title_start_time(void) {
    return g_title_start_time;
}

void client_state_set_title_start_time(unsigned int time) {
    g_title_start_time = time;
}

// Exit handling
bool client_state_should_exit(void) {
    return g_should_exit;
}

void client_state_request_exit(void) {
    g_should_exit = true;
}

void client_state_cancel_exit(void) {
    g_should_exit = false;
}

// Window state updates
void client_state_sync_with_window(void) {
    if (g_game_state) {
        g_show_exit_prompt = g_game_state->show_exit_prompt ? 1 : 0;
    }
}
