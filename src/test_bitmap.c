#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    // Create a small test bitmap
    SDL_Surface *surface = SDL_CreateRGBSurface(0, 100, 100, 32,
                                               0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surface) {
        fprintf(stderr, "Failed to create SDL surface: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Fill with red
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 255, 0, 0));

    // Save as BMP
    if (SDL_SaveBMP(surface, "test.bmp") < 0) {
        fprintf(stderr, "Failed to save bitmap: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 1;
    }

    printf("Test bitmap saved: test.bmp\n");

    SDL_FreeSurface(surface);
    SDL_Quit();
    return 0;
}
