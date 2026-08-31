// layer_viewer.c
// Simple program to explore a generated world layer-by-layer (Z is vertical)

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "world.h"

#define WINDOW_W 896
#define WINDOW_H 896

static SDL_Color color_for_voxel(VoxelType t) {
  switch (t) {
    case VOXEL_GRASS: return (SDL_Color){90, 170, 50, 255};
    case VOXEL_SOIL:  return (SDL_Color){120, 80, 40, 255};
    case VOXEL_STONE: return (SDL_Color){110, 110, 110, 255};
    case VOXEL_SAND:  return (SDL_Color){194, 178, 128, 255};
    case VOXEL_WATER: return (SDL_Color){50, 100, 200, 255};
    case VOXEL_WOOD:  return (SDL_Color){130, 80, 40, 255};
    case VOXEL_LEAVES:return (SDL_Color){60, 140, 60, 255};
    default:          return (SDL_Color){25, 25, 50, 255}; // Air / background
  }
}

static void render_slice(SDL_Renderer* r, World* w, int z) {
  SDL_SetRenderDrawColor(r, 20, 22, 35, 255);
  SDL_RenderClear(r);

  int cell_w = WINDOW_W / (int)w->width;
  int cell_h = WINDOW_H / (int)w->height;
  if (cell_w < 2) cell_w = 2;
  if (cell_h < 2) cell_h = 2;

  for (uint32_t y = 0; y < w->height; y++) {
    for (uint32_t x = 0; x < w->width; x++) {
      Voxel* v = world_get_voxel(w, x, y, (uint32_t)z);
      SDL_Color c = v ? color_for_voxel(v->type) : (SDL_Color){10, 10, 20, 255};
      SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
      SDL_Rect rect = { (int)(x * cell_w), (int)(y * cell_h), cell_w - 1, cell_h - 1 };
      SDL_RenderFillRect(r, &rect);
    }
  }

  // Draw grid lines for easier reading
  SDL_SetRenderDrawColor(r, 0, 0, 0, 40);
  for (uint32_t gx = 0; gx <= w->width; gx++) {
    int sx = (int)(gx * cell_w);
    SDL_RenderDrawLine(r, sx, 0, sx, WINDOW_H);
  }
  for (uint32_t gy = 0; gy <= w->height; gy++) {
    int sy = (int)(gy * cell_h);
    SDL_RenderDrawLine(r, 0, sy, WINDOW_W, sy);
  }

  SDL_RenderPresent(r);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  const char* seed = "layer_viewer_seed";

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window* win = SDL_CreateWindow("VERSE Layer Viewer (Z vertical)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
  if (!win) {
    printf("Window creation failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) {
    printf("Renderer creation failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  // Generate a world (use cubic to inspect vertical shape clearly)
  uint32_t W = 64, H = 64, D = 64; // X, Y, Z (Z is vertical)
  World* world = world_create(W, H, D);
  if (!world) {
    printf("Failed to create world\n");
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  world_generate(world, seed);

  // Start in middle Z by default
  int z = (int)(world->depth / 2);
  bool running = true;
  printf("Controls: PageUp/]/= increase Z, PageDown/[/- decrease Z, ESC to quit.\n");

  render_slice(ren, world, z);
  printf("Viewing Z layer: %d (0..%u)\n", z, world->depth - 1);

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN) {
        SDL_Keycode key = e.key.keysym.sym;
        if (key == SDLK_ESCAPE) running = false;
        if (key == SDLK_PAGEUP || key == SDLK_RIGHTBRACKET || key == SDLK_EQUALS || key == SDLK_PLUS) {
          if (z < (int)world->depth - 1) z++;
          render_slice(ren, world, z);
          printf("Viewing Z layer: %d\n", z);
        }
        if (key == SDLK_PAGEDOWN || key == SDLK_LEFTBRACKET || key == SDLK_MINUS) {
          if (z > 0) z--;
          render_slice(ren, world, z);
          printf("Viewing Z layer: %d\n", z);
        }
      }
    }
    SDL_Delay(10);
  }

  world_destroy(world);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
