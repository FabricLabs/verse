#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <SDL2/SDL.h>

#define ZONE_SIZE 32
#define MAX_NAME_LENGTH 20
#define VIEW_DISTANCE 2
#define NUM_ZONES 10
#define MOVE_STEP 1.0f

typedef struct {
    int seed;
    int voxels[ZONE_SIZE][ZONE_SIZE][ZONE_SIZE];
    int neighbors[6]; // For connected zones (N, S, E, W, U, D)
} Zone;

typedef struct {
    char name[MAX_NAME_LENGTH + 1];
    int currentZone;
    float posX, posY, posZ;
} Player;

Zone gameWorld[NUM_ZONES];

void generateZone(Zone *zone, int seed);
void savePlayerData(Player *player);
void loadPlayerData(Player *player);
void startNewGame();
void loadGame();
void renderMenu();
void renderGame(Player *player);
void initializeGameWorld();
void teleportPlayerToSpawn(Player *player);
void movePlayer(Player *player, SDL_Keycode key);
void visualizeZone3D(Zone *zone);
void launchUI();

void generateZone(Zone *zone, int seed) {
    srand(seed);
    for (int x = 0; x < ZONE_SIZE; x++) {
        for (int y = 0; y < ZONE_SIZE; y++) {
            for (int z = 0; z < ZONE_SIZE; z++) {
                zone->voxels[x][y][z] = (x > 10 && x < 20 && y > 10 && y < 20 && z > 10 && z < 20) ? 1 : 0;
            }
        }
    }
    zone->seed = seed;
    for (int i = 0; i < 6; i++) {
        zone->neighbors[i] = -1;
    }
}

void launchUI() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("Error initializing SDL: %s\n", SDL_GetError());
        return;
    }

    SDL_Window *window = SDL_CreateWindow("Voxel Game", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Error creating window: %s\n", SDL_GetError());
        SDL_Quit();
        return;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Event event;
    int running = 1;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void initializeGameWorld() {
    srand(time(NULL));
    for (int i = 0; i < NUM_ZONES; i++) {
        generateZone(&gameWorld[i], rand());
    }
    printf("Game world initialized with %d zones.\n", NUM_ZONES);
}

int main() {
    initializeGameWorld();
    launchUI();
    return 0;
}
