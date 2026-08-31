// Naive Window - Simple Performance Test
//
// This program creates a basic window that renders a single world using the exact
// same code path as universe-ref, with FPS logging and performance stats.
// It's designed to test the performance and geometry generation of the voxel mesh system.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <jansson.h>
#include "world.h"
#include "isometric_renderer.h"
#include "fp_renderer.h"
#include "voxel_mesh.h"
#include "octree.h"

// Performance tracking
typedef struct
{
  Uint64 start_time;
  Uint64 frame_start;
  Uint64 frame_end;
  double freq;
  int frame_count;
  double total_render_ms;
  double first_frame_ms;
  double last_frame_ms;
  double min_frame_ms;
  double max_frame_ms;
} PerformanceTracker;

static void perf_init(PerformanceTracker *perf)
{
  perf->freq = (double)SDL_GetPerformanceFrequency();
  perf->start_time = SDL_GetPerformanceCounter();
  perf->frame_count = 0;
  perf->total_render_ms = 0.0;
  perf->first_frame_ms = 0.0;
  perf->last_frame_ms = 0.0;
  perf->min_frame_ms = 999999.0;
  perf->max_frame_ms = 0.0;
}

static void perf_frame_start(PerformanceTracker *perf)
{
  perf->frame_start = SDL_GetPerformanceCounter();
}

static void perf_frame_end(PerformanceTracker *perf)
{
  perf->frame_end = SDL_GetPerformanceCounter();
  perf->frame_count++;

  double frame_ms = (double)(perf->frame_end - perf->frame_start) * 1000.0 / perf->freq;
  perf->last_frame_ms = frame_ms;
  perf->total_render_ms += frame_ms;

  if (perf->frame_count == 1)
  {
    perf->first_frame_ms = frame_ms;
  }

  if (frame_ms < perf->min_frame_ms)
    perf->min_frame_ms = frame_ms;
  if (frame_ms > perf->max_frame_ms)
    perf->max_frame_ms = frame_ms;
}

static void perf_log_stats(PerformanceTracker *perf, const char *label)
{
  double avg_frame_ms = perf->total_render_ms / (double)perf->frame_count;
  double total_ms = (double)(perf->frame_end - perf->start_time) * 1000.0 / perf->freq;
  double fps = 1000.0 / avg_frame_ms;

  printf("[%s] Performance over %d frames:\n", label, perf->frame_count);
  printf("[%s]   First frame (cache miss): %.2fms\n", label, perf->first_frame_ms);
  printf("[%s]   Last frame: %.2fms\n", label, perf->last_frame_ms);
  printf("[%s]   Min frame: %.2fms\n", label, perf->min_frame_ms);
  printf("[%s]   Max frame: %.2fms\n", label, perf->max_frame_ms);
  printf("[%s]   Average frame: %.2fms\n", label, avg_frame_ms);
  printf("[%s]   Average FPS: %.1f\n", label, fps);
  printf("[%s]   Total time: %.2fms\n", label, total_ms);
  if (perf->first_frame_ms > 0)
  {
    printf("[%s]   Cache speedup: %.1fx\n", label, perf->first_frame_ms / avg_frame_ms);
  }
}

int main(int argc, char **argv)
{
  printf("[naive-window] Starting naive window performance test\n");

  // Parse command line arguments
  bool use_mesh_mode = false;
  bool use_gpu_mesh = false;
  int target_frames = 100;
  bool stay_open = false;

  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--mesh") == 0)
    {
      use_mesh_mode = true;
      printf("[naive-window] Mesh mode enabled\n");
    }
    else if (strcmp(argv[i], "--gpu-mesh") == 0)
    {
      use_gpu_mesh = true;
      use_mesh_mode = true;
      printf("[naive-window] GPU mesh mode enabled\n");
    }
    else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
    {
      target_frames = atoi(argv[i + 1]);
      printf("[naive-window] Target frames: %d\n", target_frames);
      i++; // Skip next argument
    }
    else if (strcmp(argv[i], "--stay-open") == 0)
    {
      stay_open = true;
      printf("[naive-window] Window will stay open for visual inspection\n");
    }
    else if (strcmp(argv[i], "--help") == 0)
    {
      printf("Usage: %s [options]\n", argv[0]);
      printf("Options:\n");
      printf("  --mesh              Use mesh rendering mode\n");
      printf("  --gpu-mesh          Use GPU mesh rendering mode\n");
      printf("  --frames <count>    Number of frames to render (default: 100)\n");
      printf("  --stay-open         Keep window open for visual inspection\n");
      printf("  --help              Show this help message\n");
      return 0;
    }
  }

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  if (TTF_Init() != 0)
  {
    fprintf(stderr, "TTF_Init failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  // Create window and renderer - use same flags as universe-ref
  const int W = 800, H = 600;
  SDL_Window *win = SDL_CreateWindow("Naive Window - Performance Test",
                                     SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     W, H, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
  if (!win)
  {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  // Show the window after creation (like universe-ref)
  SDL_ShowWindow(win);
  printf("[naive-window] Window created with OpenGL support: %dx%d\n", W, H);

  // Create OpenGL context (required for rendering like universe-ref)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_GLContext glctx = SDL_GL_CreateContext(win);
  if (!glctx)
  {
    fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  // Make OpenGL context current (critical for rendering)
  SDL_GL_MakeCurrent(win, glctx);
  printf("[naive-window] OpenGL context created and made current\n");

  // Initialize FP renderer OpenGL state
  fp_renderer_gl_init();

  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
  if (!ren)
  {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    if (glctx)
      SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  // Create a minimal 1x1x1 world with a single voxel
  printf("[naive-window] Creating minimal 1x1x1 world...\n");

  World *world = (World *)malloc(sizeof(World));
  world->width = 1;
  world->height = 1;
  world->depth = 1;
  world->voxels = (Voxel *)malloc(sizeof(Voxel));

  // Set the single voxel to stone
  world->voxels[0].type = 1; // STONE

  // Create a minimal GameWorlds structure
  GameWorlds *gw = (GameWorlds *)malloc(sizeof(GameWorlds));
  gw->home_world = world;

  printf("[naive-window] Minimal world created: 1x1x1 with 1 stone voxel\n");

  // Build voxel mesh if using mesh mode
  VoxelMesh mesh = {0};
  if (use_mesh_mode)
  {
    printf("[naive-window] Building voxel mesh...\n");
    voxel_mesh_init(&mesh);
    voxel_mesh_build_all_faces_greedy(gw->home_world, &mesh);
    printf("[naive-window] Mesh built: %d quads\n", mesh.count);

    // Set the cached mesh in the renderer
    fp_renderer_set_cached_mesh(gw->home_world, &mesh);

    // Set mesh mode for both CPU and GPU mesh rendering
    fp_renderer_set_mode(FP_MODE_MESH);

    if (use_gpu_mesh)
    {
      SDL_GL_MakeCurrent(win, glctx);
      fp_renderer_enable_gpu_mesh(true);
      printf("[naive-window] GPU mesh mode enabled\n");
    }
    else
    {
      printf("[naive-window] CPU mesh mode enabled\n");
    }
  }

  // Setup camera for 1x1x1 world
  FPCamera cam = {0};
  cam.fov_deg = 60.0f;

  // Position camera to look at the single voxel at (0,0,0)
  cam.x = 0.5f;     // Center of voxel
  cam.y = -2.0f;    // Back away from voxel
  cam.z = 0.5f;     // Same height as voxel
  cam.yaw = 0.0f;   // Face forward
  cam.pitch = 0.0f; // Look horizontally
  cam.roll = 0.0f;

  printf("[naive-window] Camera position: (%.1f, %.1f, %.1f)\n", cam.x, cam.y, cam.z);

  // Performance tracking
  PerformanceTracker perf;
  perf_init(&perf);

  // Main rendering loop
  printf("[naive-window] Starting rendering loop for %d frames...\n", target_frames);

  bool running = true;
  while (running && perf.frame_count < target_frames)
  {
    // Handle events
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_QUIT)
      {
        running = false;
      }
      else if (event.type == SDL_KEYDOWN)
      {
        if (event.key.keysym.sym == SDLK_ESCAPE)
        {
          running = false;
        }
      }
    }

    // Start frame timing
    perf_frame_start(&perf);

    // Clear renderer
    SDL_SetRenderDrawColor(ren, 20, 22, 35, 255);
    SDL_RenderClear(ren);

    // FINAL TEST: Just fill the entire screen with bright red
    printf("[naive-window] FINAL TEST: Filling entire screen with bright red...\n");

    // Fill entire screen with bright red
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_RenderClear(ren);

    // Force multiple render presents
    SDL_RenderPresent(ren);
    SDL_Delay(100); // Wait a bit
    SDL_RenderPresent(ren);

    printf("[naive-window] ENTIRE SCREEN SHOULD BE BRIGHT RED NOW!\n");

    // Present renderer
    SDL_RenderPresent(ren);

    // End frame timing
    perf_frame_end(&perf);

    // Log progress every 10 frames
    if (perf.frame_count % 10 == 0)
    {
      double current_fps = 1000.0 / perf.last_frame_ms;
      printf("[naive-window] Frame %d: %.2fms (%.1f FPS)\n",
             perf.frame_count, perf.last_frame_ms, current_fps);
    }

    // Small delay to make it visible
    SDL_Delay(16); // ~60 FPS cap for visibility
  }

  // Log final performance stats
  printf("\n[naive-window] === FINAL PERFORMANCE STATS ===\n");
  perf_log_stats(&perf, "naive-window");

  // Calculate and log geometry stats
  if (use_mesh_mode)
  {
    printf("\n[naive-window] === GEOMETRY STATS ===\n");
    printf("[naive-window] Total quads: %d\n", mesh.count);
    printf("[naive-window] World dimensions: %dx%dx%d\n",
           (int)gw->home_world->width,
           (int)gw->home_world->height,
           (int)gw->home_world->depth);

    // Count quads per face direction
    int face_counts[6] = {0};
    for (int i = 0; i < mesh.count; i++)
    {
      face_counts[mesh.quads[i].face]++;
    }

    printf("[naive-window] Quads per face:\n");
    const char *face_names[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    for (int f = 0; f < 6; f++)
    {
      printf("[naive-window]   %s: %d quads\n", face_names[f], face_counts[f]);
    }
  }

  // If stay-open flag is set, keep window open for visual inspection
  if (stay_open)
  {
    printf("\n[naive-window] Window staying open for visual inspection...\n");
    printf("[naive-window] Press ESC or close window to exit\n");

    // Keep the last frame visible and wait for user input
    SDL_RenderPresent(ren);

    bool inspection_running = true;
    while (inspection_running)
    {
      SDL_Event event;
      while (SDL_PollEvent(&event))
      {
        if (event.type == SDL_QUIT)
        {
          inspection_running = false;
        }
        else if (event.type == SDL_KEYDOWN)
        {
          if (event.key.keysym.sym == SDLK_ESCAPE)
          {
            inspection_running = false;
          }
        }
      }
      SDL_Delay(16); // Small delay to prevent busy waiting
    }

    printf("[naive-window] Visual inspection complete\n");
  }

  // Cleanup
  if (use_mesh_mode)
  {
    voxel_mesh_free(&mesh);
  }

  // Clean up our minimal world
  free(gw->home_world->voxels);
  free(gw->home_world);
  free(gw);

  SDL_DestroyRenderer(ren);
  SDL_GL_DeleteContext(glctx);
  SDL_DestroyWindow(win);
  TTF_Quit();
  SDL_Quit();

  printf("[naive-window] Cleanup complete\n");
  return 0;
}
