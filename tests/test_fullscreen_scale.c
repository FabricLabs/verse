#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

// Resolution and scaling constants
#define BASE_RESOLUTION_WIDTH 256
#define BASE_RESOLUTION_HEIGHT 240
#define MIN_SCALE_FACTOR 1
#define MAX_SCALE_FACTOR 4
#define DEFAULT_SCALE_FACTOR 2

// Calculate the optimal scale factor for the current screen
int calculate_optimal_scale_factor() {
    SDL_DisplayMode display_mode;
    if (SDL_GetCurrentDisplayMode(0, &display_mode) != 0) {
        printf("Failed to get display mode, using default scale factor\n");
        return DEFAULT_SCALE_FACTOR;
    }

    // Calculate how many times the base resolution fits in the screen
    int scale_x = display_mode.w / BASE_RESOLUTION_WIDTH;
    int scale_y = display_mode.h / BASE_RESOLUTION_HEIGHT;

    // Use the smaller scale to ensure it fits completely
    int optimal_scale = (scale_x < scale_y) ? scale_x : scale_y;

    // Clamp to valid range
    if (optimal_scale < MIN_SCALE_FACTOR) {
        optimal_scale = MIN_SCALE_FACTOR;
    } else if (optimal_scale > MAX_SCALE_FACTOR) {
        optimal_scale = MAX_SCALE_FACTOR;
    }

    printf("Screen resolution: %dx%d\n", display_mode.w, display_mode.h);
    printf("Base resolution: %dx%d\n", BASE_RESOLUTION_WIDTH, BASE_RESOLUTION_HEIGHT);
    printf("Calculated optimal scale factor: %d\n", optimal_scale);

    return optimal_scale;
}

int main() {
    printf("=== Fullscreen Scale Factor Test ===\n");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() < 0) {
        printf("TTF initialization failed: %s\n", TTF_GetError());
        return 1;
    }

    printf("\n1. Testing optimal scale factor calculation...\n");
    int optimal_scale = calculate_optimal_scale_factor();
    printf("✓ Optimal scale factor: %d\n", optimal_scale);

    printf("\n2. Testing window creation with optimal scale...\n");
    int window_width = BASE_RESOLUTION_WIDTH * optimal_scale;
    int window_height = BASE_RESOLUTION_HEIGHT * optimal_scale;

    SDL_Window* window = SDL_CreateWindow(
        "Scale Test",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        window_width,
        window_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("✓ Window created with size %dx%d\n", window_width, window_height);

    printf("\n3. Testing fullscreen mode...\n");
    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
        printf("✓ Fullscreen mode activated\n");
    } else {
        printf("✗ Failed to activate fullscreen mode\n");
    }

    printf("\n4. Testing window cleanup...\n");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    printf("✓ Window system cleaned up successfully\n");

    printf("\n=== Fullscreen Scale Test Completed Successfully ===\n");
    return 0;
}
