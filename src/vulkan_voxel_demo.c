#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "vulkan_fp_renderer.h"
#include "simple_world.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define TARGET_FPS 60

// Demo state
typedef struct
{
  SDL_Window *window;
  VulkanFPRenderer *vulkan_renderer;
  SimpleWorld *world;
  bool running;
  bool vulkan_available;

  // Camera
  float camera_x, camera_y, camera_z;
  float camera_yaw, camera_pitch;
  float camera_fov;

  // Input
  bool keys[SDL_NUM_SCANCODES];
  bool mouse_buttons[3];
  int mouse_x, mouse_y;
  int mouse_rel_x, mouse_rel_y;

  // Performance
  Uint64 last_frame_time;
  float frame_time_ms;
  uint32_t fps_counter;
  uint32_t fps;
} DemoState;

// Function prototypes
static bool init_demo(DemoState *demo);
static void cleanup_demo(DemoState *demo);
static void handle_events(DemoState *demo);
static void update_demo(DemoState *demo);
static void render_demo(DemoState *demo);
static void update_camera(DemoState *demo, float delta_time);
static void create_test_world(DemoState *demo);
static void print_help(void);

int main(int argc, char *argv[])
{
  printf("Vulkan Voxel Renderer Demo\n");
  printf("==========================\n\n");

  // Check command line arguments
  if (argc > 1 && strcmp(argv[1], "--help") == 0)
  {
    print_help();
    return 0;
  }

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
  {
    fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
    return 1;
  }

  // Create demo state
  DemoState demo = {0};

  // Initialize demo
  if (!init_demo(&demo))
  {
    fprintf(stderr, "Demo initialization failed\n");
    cleanup_demo(&demo);
    SDL_Quit();
    return 1;
  }

  printf("Demo initialized successfully\n");
  printf("Controls:\n");
  printf("  WASD - Move camera\n");
  printf("  Mouse - Look around\n");
  printf("  ESC - Quit\n");
  printf("  F1 - Toggle Vulkan renderer\n");
  printf("  F2 - Toggle rendering mode\n");
  printf("  F3 - Toggle debug rendering\n");
  printf("  F4 - Show performance stats\n");
  printf("\n");

  // Main loop
  demo.running = true;
  while (demo.running)
  {
    handle_events(&demo);
    update_demo(&demo);
    render_demo(&demo);

    // Cap frame rate
    Uint64 current_time = SDL_GetTicks();
    Uint64 elapsed = current_time - demo.last_frame_time;
    if (elapsed < (1000 / TARGET_FPS))
    {
      SDL_Delay((1000 / TARGET_FPS) - elapsed);
    }
    demo.last_frame_time = current_time;
  }

  // Cleanup
  cleanup_demo(&demo);
  SDL_Quit();

  printf("Demo completed successfully\n");
  return 0;
}

static bool init_demo(DemoState *demo)
{
  // Create window
  demo->window = SDL_CreateWindow(
      "Vulkan Voxel Renderer Demo",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      WINDOW_WIDTH, WINDOW_HEIGHT,
      SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

  if (!demo->window)
  {
    fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
    return false;
  }

  // Try to create Vulkan renderer directly
  printf("Attempting to create Vulkan renderer...\n");

  VulkanRendererConfig vulkan_config = VULKAN_RENDERER_DEFAULT_CONFIG_VALUE;
  VulkanFPRendererConfig fp_config = VULKAN_FP_RENDERER_DEFAULT_CONFIG_VALUE;

  demo->vulkan_renderer = vulkan_fp_renderer_create(demo->window, &vulkan_config, &fp_config);
  if (demo->vulkan_renderer)
  {
    printf("Vulkan renderer created successfully\n");
    demo->vulkan_available = true;
  }
  else
  {
    fprintf(stderr, "Failed to create Vulkan renderer, falling back to software rendering\n");
    demo->vulkan_available = false;
  }

  // Create test world
  create_test_world(demo);

  // Initialize camera
  demo->camera_x = 1.0f;
  demo->camera_y = 1.0f;
  demo->camera_z = 2.0f;
  demo->camera_yaw = 0.0f;
  demo->camera_pitch = 0.0f;
  demo->camera_fov = 70.0f;

  // Initialize input
  memset(demo->keys, 0, sizeof(demo->keys));
  memset(demo->mouse_buttons, 0, sizeof(demo->mouse_buttons));

  // Initialize performance tracking
  demo->last_frame_time = SDL_GetTicks();
  demo->frame_time_ms = 0.0f;
  demo->fps_counter = 0;
  demo->fps = 0;

  return true;
}

static void cleanup_demo(DemoState *demo)
{
  if (demo->vulkan_renderer)
  {
    vulkan_fp_renderer_destroy(demo->vulkan_renderer);
    demo->vulkan_renderer = NULL;
  }

  if (demo->world)
  {
    simple_world_destroy(demo->world);
    demo->world = NULL;
  }

  if (demo->window)
  {
    SDL_DestroyWindow(demo->window);
    demo->window = NULL;
  }
}

static void handle_events(DemoState *demo)
{
  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    switch (event.type)
    {
    case SDL_QUIT:
      demo->running = false;
      break;

    case SDL_KEYDOWN:
      if (event.key.keysym.sym == SDLK_ESCAPE)
      {
        demo->running = false;
      }
      else if (event.key.keysym.sym == SDLK_F1)
      {
        // Toggle Vulkan renderer
        if (demo->vulkan_available)
        {
          if (demo->vulkan_renderer)
          {
            vulkan_fp_renderer_destroy(demo->vulkan_renderer);
            demo->vulkan_renderer = NULL;
            printf("Vulkan renderer disabled\n");
          }
          else
          {
            VulkanRendererConfig vulkan_config = VULKAN_RENDERER_DEFAULT_CONFIG_VALUE;
            VulkanFPRendererConfig fp_config = VULKAN_FP_RENDERER_DEFAULT_CONFIG_VALUE;
            demo->vulkan_renderer = vulkan_fp_renderer_create(demo->window, &vulkan_config, &fp_config);
            if (demo->vulkan_renderer)
            {
              printf("Vulkan renderer enabled\n");
            }
          }
        }
      }
      else if (event.key.keysym.sym == SDLK_F2)
      {
        // Toggle rendering mode
        if (demo->vulkan_renderer)
        {
          static int mode_index = 0;
          VulkanFPMode modes[] = {VULKAN_FP_MODE_MESH, VULKAN_FP_MODE_RAY_MARCH, VULKAN_FP_MODE_HYBRID};
          mode_index = (mode_index + 1) % 3;
          vulkan_fp_renderer_set_mode(demo->vulkan_renderer, modes[mode_index]);
          printf("Rendering mode: %d\n", modes[mode_index]);
        }
      }
      else if (event.key.keysym.sym == SDLK_F3)
      {
        // Toggle debug rendering
        if (demo->vulkan_renderer)
        {
          static bool debug_enabled = false;
          debug_enabled = !debug_enabled;
          vulkan_fp_renderer_enable_debug_rendering(demo->vulkan_renderer, debug_enabled);
          printf("Debug rendering: %s\n", debug_enabled ? "enabled" : "disabled");
        }
      }
      else if (event.key.keysym.sym == SDLK_F4)
      {
        // Show performance stats
        if (demo->vulkan_renderer)
        {
          uint32_t draw_calls, triangles_rendered;
          float frame_time;
          vulkan_fp_renderer_get_stats(demo->vulkan_renderer, &draw_calls, &triangles_rendered, &frame_time);
          printf("Performance: %u draw calls, %u triangles, %.2f ms frame time\n",
                 draw_calls, triangles_rendered, frame_time);
        }
      }
      demo->keys[event.key.keysym.scancode] = true;
      break;

    case SDL_KEYUP:
      demo->keys[event.key.keysym.scancode] = false;
      break;

    case SDL_MOUSEBUTTONDOWN:
      if (event.button.button <= 3)
      {
        demo->mouse_buttons[event.button.button - 1] = true;
      }
      break;

    case SDL_MOUSEBUTTONUP:
      if (event.button.button <= 3)
      {
        demo->mouse_buttons[event.button.button - 1] = false;
      }
      break;

    case SDL_MOUSEMOTION:
      demo->mouse_x = event.motion.x;
      demo->mouse_y = event.motion.y;
      demo->mouse_rel_x = event.motion.xrel;
      demo->mouse_rel_y = event.motion.yrel;
      break;

    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_RESIZED)
      {
        // Handle window resize
        if (demo->vulkan_renderer)
        {
          // Recreate swap chain for new window size
          // This would be handled by the Vulkan renderer
        }
      }
      break;
    }
  }
}

static void update_demo(DemoState *demo)
{
  // Calculate delta time
  Uint64 current_time = SDL_GetTicks();
  demo->frame_time_ms = (float)(current_time - demo->last_frame_time);
  demo->last_frame_time = current_time;

  // Update FPS counter
  demo->fps_counter++;
  if (demo->fps_counter >= 60)
  {
    demo->fps = (uint32_t)(1000.0f / demo->frame_time_ms);
    demo->fps_counter = 0;
  }

  // Update camera
  update_camera(demo, demo->frame_time_ms / 1000.0f);

  // Update world (if needed)
  // For now, the world is static
}

static void render_demo(DemoState *demo)
{
  if (demo->vulkan_renderer && demo->vulkan_available)
  {
    // Use Vulkan renderer
    if (vulkan_fp_renderer_begin_frame(demo->vulkan_renderer))
    {
      // Create a simple FPCamera structure for rendering
      FPCamera camera = {
          .x = demo->camera_x,
          .y = demo->camera_y,
          .z = demo->camera_z,
          .yaw = demo->camera_yaw,
          .pitch = demo->camera_pitch,
          .fov_deg = demo->camera_fov};

      // Render using Vulkan
      vulkan_fp_renderer_render_basic(demo->vulkan_renderer, &camera);

      vulkan_fp_renderer_end_frame(demo->vulkan_renderer);
    }
  }
  else
  {
    // Fallback to software rendering or clear screen
    // This would use the existing fp_renderer or a simple SDL renderer
    SDL_Surface *surface = SDL_GetWindowSurface(demo->window);
    if (surface)
    {
      SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 0x20, 0x30, 0x80));
      SDL_UpdateWindowSurface(demo->window);
    }
  }
}

static void update_camera(DemoState *demo, float delta_time)
{
  const float move_speed = 5.0f;
  const float mouse_sensitivity = 0.002f;

  // Handle keyboard input for movement
  if (demo->keys[SDL_SCANCODE_W])
  {
    demo->camera_z -= cos(demo->camera_yaw) * move_speed * delta_time;
    demo->camera_x -= sin(demo->camera_yaw) * move_speed * delta_time;
  }
  if (demo->keys[SDL_SCANCODE_S])
  {
    demo->camera_z += cos(demo->camera_yaw) * move_speed * delta_time;
    demo->camera_x += sin(demo->camera_yaw) * move_speed * delta_time;
  }
  if (demo->keys[SDL_SCANCODE_A])
  {
    demo->camera_x -= cos(demo->camera_yaw) * move_speed * delta_time;
    demo->camera_z += sin(demo->camera_yaw) * move_speed * delta_time;
  }
  if (demo->keys[SDL_SCANCODE_D])
  {
    demo->camera_x += cos(demo->camera_yaw) * move_speed * delta_time;
    demo->camera_z -= sin(demo->camera_yaw) * move_speed * delta_time;
  }
  if (demo->keys[SDL_SCANCODE_SPACE])
  {
    demo->camera_y += move_speed * delta_time;
  }
  if (demo->keys[SDL_SCANCODE_LSHIFT])
  {
    demo->camera_y -= move_speed * delta_time;
  }

  // Handle mouse input for looking around
  if (demo->mouse_buttons[1])
  { // Right mouse button
    demo->camera_yaw -= demo->mouse_rel_x * mouse_sensitivity;
    demo->camera_pitch -= demo->mouse_rel_y * mouse_sensitivity;

    // Clamp pitch
    if (demo->camera_pitch > M_PI / 2.0f)
      demo->camera_pitch = M_PI / 2.0f;
    if (demo->camera_pitch < -M_PI / 2.0f)
      demo->camera_pitch = -M_PI / 2.0f;

    // Reset relative mouse movement
    demo->mouse_rel_x = 0;
    demo->mouse_rel_y = 0;
  }
}

static void create_test_world(DemoState *demo)
{
  // Create a larger test world for better demonstration
  demo->world = simple_world_create(16, 16, 16);
  if (!demo->world)
  {
    fprintf(stderr, "Failed to create test world\n");
    return;
  }

  // Fill with air first
  for (uint32_t x = 0; x < demo->world->width; x++)
  {
    for (uint32_t y = 0; y < demo->world->height; y++)
    {
      for (uint32_t z = 0; z < demo->world->depth; z++)
      {
        simple_world_set_voxel(demo->world, x, y, z, VOXEL_AIR);
      }
    }
  }

  // Create some interesting structures
  // Ground plane
  for (uint32_t x = 0; x < demo->world->width; x++)
  {
    for (uint32_t z = 0; z < demo->world->depth; z++)
    {
      simple_world_set_voxel(demo->world, x, 0, z, VOXEL_GRASS);
    }
  }

  // Some hills
  for (uint32_t x = 2; x < 6; x++)
  {
    for (uint32_t z = 2; z < 6; z++)
    {
      uint32_t height = 3 + (x - 2) + (z - 2);
      for (uint32_t y = 1; y < height && y < demo->world->height; y++)
      {
        simple_world_set_voxel(demo->world, x, y, z, VOXEL_SOIL);
      }
    }
  }

  // A small house
  for (uint32_t x = 10; x < 14; x++)
  {
    for (uint32_t z = 10; z < 14; z++)
    {
      // Foundation
      simple_world_set_voxel(demo->world, x, 1, z, VOXEL_STONE);
      // Walls
      for (uint32_t y = 2; y < 5; y++)
      {
        if (x == 10 || x == 13 || z == 10 || z == 13)
        {
          simple_world_set_voxel(demo->world, x, y, z, VOXEL_BRICK);
        }
      }
      // Roof
      if (x >= 11 && x <= 12 && z >= 11 && z <= 12)
      {
        simple_world_set_voxel(demo->world, x, 5, z, VOXEL_WOOD);
      }
    }
  }

  printf("Test world created: %ux%ux%u\n", demo->world->width, demo->world->height, demo->world->depth);
}

static void print_help(void)
{
  printf("Vulkan Voxel Renderer Demo\n");
  printf("Usage: vulkan_voxel_demo [options]\n\n");
  printf("Options:\n");
  printf("  --help     Show this help message\n\n");
  printf("Controls:\n");
  printf("  WASD       Move camera\n");
  printf("  Mouse      Look around (hold right button)\n");
  printf("  Space      Move up\n");
  printf("  Shift      Move down\n");
  printf("  ESC        Quit\n");
  printf("  F1         Toggle Vulkan renderer\n");
  printf("  F2         Toggle rendering mode\n");
  printf("  F3         Toggle debug rendering\n");
  printf("  F4         Show performance stats\n\n");
  printf("Features:\n");
  printf("  - Real-time voxel rendering with Vulkan\n");
  printf("  - Multiple rendering modes (mesh, ray marching, hybrid)\n");
  printf("  - Interactive first-person camera\n");
  printf("  - Performance monitoring\n");
  printf("  - Fallback to software rendering if Vulkan unavailable\n");
}
