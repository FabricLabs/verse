// One-off dump of isometric and first-person mob drawing, with and without
// VERSE_MOB_MODELS. Not part of the game; used to capture comparison screenshots.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "fp_renderer.h"
#include "isometric_renderer.h"
#include "mob_ai.h"
#include "mob_models.h"
#include "world.h"

static void save_bmp(SDL_Renderer *ren, int w, int h, const char *path)
{
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s)
    return;
  if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0)
  {
    SDL_Surface *rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
    if (rgb)
    {
      printf("%s %s\n", SDL_SaveBMP(rgb, path) == 0 ? "wrote" : "failed", path);
      SDL_FreeSurface(rgb);
    }
  }
  SDL_FreeSurface(s);
}

static bool spawn(World *w, MobActor *mob)
{
  if (!w || !mob)
    return false;
  Actor copy = mob->base;
  copy.extra_data = mob;
  if (!world_add_runtime_actor(w, &copy))
  {
    mob_actor_destroy(mob);
    return false;
  }
  return true;
}

int main(int argc, char **argv)
{
  const char *prefix = (argc > 1) ? argv[1] : ".fpshots/mobs";

  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  const int W = 640, H = 480;
  SDL_Window *win = SDL_CreateWindow("mob dump", 0, 0, W, H, SDL_WINDOW_HIDDEN);
  SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
  if (!ren)
  {
    fprintf(stderr, "renderer: %s\n", SDL_GetError());
    return 1;
  }

  World *world = world_create(32, 32, 16);
  if (!world)
    return 1;
  for (uint32_t y = 0; y < world->height; y++)
    for (uint32_t x = 0; x < world->width; x++)
      for (uint32_t z = 0; z < 4; z++)
        world_set_voxel(world, x, y, z, z == 3 ? VOXEL_GRASS : VOXEL_SOIL);
  world_refresh_occupancy_bitfield(world);

  MobActor *golem = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER, 10.5, 14.5, 4.0);
  // Gull ColorMod is near-white so the shrike silhouette stays readable in a dump;
  // crows multiply toward black and vanish on the navy clear colour.
  MobActor *bird = mob_actor_create_bird(BIRD_KIND_GULL, 20.5, 18.5, 6.0);
  if (golem)
    golem->base.velocity_x = 0.2;
  if (bird)
  {
    bird->base.is_flying = true;
    bird->base.velocity_x = 0.4;
  }
  if (!spawn(world, golem) || !spawn(world, bird))
  {
    fprintf(stderr, "spawn failed\n");
    return 1;
  }

  GameWorlds gw = {0};
  gw.home_world = world;

  printf("VERSE_MOB_MODELS enabled=%d\n", mob_models_enabled() ? 1 : 0);

  IsometricRenderer *iso = isometric_renderer_create(W, H);
  isometric_renderer_set_game_worlds(iso, &gw);
  isometric_renderer_set_screen_size(iso, W, H);
  isometric_renderer_set_auto_center(iso, false);
  iso->neighbor_inclusion_radius = 0;
  iso->show_player_avatar = false;
  iso->show_grid = false;
  iso->camera_x = 15;
  iso->camera_y = 16;
  iso->camera_z = 8;
  iso->camera_world_x = 15.0f;
  iso->camera_world_y = 16.0f;
  iso->camera_world_z = 8.0f;
  iso->zoom_scale = 2.0f;

  SDL_SetRenderDrawColor(ren, 20, 22, 35, 255);
  SDL_RenderClear(ren);
  isometric_renderer_render_gpu(iso, ren);
  {
    char path[256];
    snprintf(path, sizeof(path), "%s-iso.bmp", prefix);
    save_bmp(ren, W, H, path);
  }

  fp_renderer_set_mode(FP_MODE_MESH);
  fp_renderer_enable_gpu_mesh(false);
  fp_renderer_invalidate_cache();
  FPCamera cam = {.x = 8.0f, .y = 16.5f, .z = 6.5f, .yaw = 0.15f, .pitch = -0.12f, .fov_deg = 70.0f};
  SDL_SetRenderDrawColor(ren, 20, 22, 35, 255);
  SDL_RenderClear(ren);
  fp_renderer_render(ren, world, &cam, 0, 0, W, H, NULL);
  {
    char path[256];
    snprintf(path, sizeof(path), "%s-fp.bmp", prefix);
    save_bmp(ren, W, H, path);
  }

  isometric_renderer_destroy(iso);
  world_destroy(world);
  fp_renderer_release_renderer();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
