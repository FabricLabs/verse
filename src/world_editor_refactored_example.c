// world_editor_refactored_example.c
// This file demonstrates how world_editor.c would look after refactoring
// to use the new unified UI system. This eliminates all the boilerplate
// UI rendering code and nested input handling.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "world.h"
#include "world_editor_ui.h"
#include "isometric_renderer.h"
#include "fp_renderer.h"
#include "universe.h"
#include "gpu_voxel_buffer.h"

#define WINDOW_W 1024
#define WINDOW_H 768

// ============================================================================
// GLOBAL STATE
// ============================================================================

static World *g_world = NULL;
static WorldEditorUI *g_editor_ui = NULL;
static bool g_running = true;
static int g_selected_tool = WORLD_EDITOR_TOOL_SELECT;
static VoxelType g_selected_voxel = VOXEL_GRASS;

// ============================================================================
// CALLBACK FUNCTIONS
// ============================================================================

static void on_tool_change(int tool_id, void *user_data)
{
  printf("Tool changed to: %d\n", tool_id);
  g_selected_tool = tool_id;
}

static void on_voxel_change(VoxelType voxel_type, void *user_data)
{
  printf("Voxel type changed to: %d\n", voxel_type);
  g_selected_voxel = voxel_type;
}

static void on_file_operation(int operation_id, const char *filename, void *user_data)
{
  printf("File operation: %d, filename: %s\n", operation_id, filename ? filename : "none");

  switch (operation_id)
  {
  case WORLD_EDITOR_FILE_NEW:
    // Create new world
    if (g_world)
      world_destroy(g_world);
    g_world = world_create(32, 32, 32);
    world_generate(g_world, "new_world");
    world_editor_ui_update_world_info(g_editor_ui, g_world);
    break;

  case WORLD_EDITOR_FILE_OPEN:
    // Load world from file
    if (filename && g_world)
    {
      // Implementation for loading world
      printf("Loading world from: %s\n", filename);
    }
    break;

  case WORLD_EDITOR_FILE_SAVE:
    // Save world
    if (g_world)
    {
      // Implementation for saving world
      printf("Saving world\n");
    }
    break;
  }
}

static void on_actor_operation(int operation_id, int actor_id, void *user_data)
{
  printf("Actor operation: %d, actor_id: %d\n", operation_id, actor_id);

  switch (operation_id)
  {
  case WORLD_EDITOR_ACTOR_ADD:
    // Add new actor
    printf("Adding new actor\n");
    break;

  case WORLD_EDITOR_ACTOR_REMOVE:
    // Remove actor
    printf("Removing actor %d\n", actor_id);
    break;

  case WORLD_EDITOR_ACTOR_SELECT:
    // Select actor
    printf("Selecting actor %d\n", actor_id);
    break;
  }
}

static void on_setting_change(int setting_id, const char *value, void *user_data)
{
  printf("Setting changed: %d, value: %s\n", setting_id, value ? value : "none");

  switch (setting_id)
  {
  case WORLD_EDITOR_SETTING_RENDER_MODE:
    // Change render mode
    printf("Render mode: %s\n", value);
    break;

  case WORLD_EDITOR_SETTING_GRID_VISIBLE:
    // Toggle grid visibility
    printf("Grid visible: %s\n", value);
    break;
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

static bool init_sdl(void)
{
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
  {
    printf("SDL initialization failed: %s\n", SDL_GetError());
    return false;
  }

  if (TTF_Init() < 0)
  {
    printf("TTF initialization failed: %s\n", TTF_GetError());
    return false;
  }

  return true;
}

static SDL_Window *create_window(void)
{
  SDL_Window *window = SDL_CreateWindow(
      "VERSE World Editor",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      WINDOW_W, WINDOW_H,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

  if (!window)
  {
    printf("Window creation failed: %s\n", SDL_GetError());
    return NULL;
  }

  return window;
}

static SDL_Renderer *create_renderer(SDL_Window *window)
{
  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (!renderer)
  {
    printf("Renderer creation failed: %s\n", SDL_GetError());
    return NULL;
  }

  return renderer;
}

static bool init_world_editor_ui(SDL_Renderer *renderer)
{
  g_editor_ui = world_editor_ui_create(renderer, NULL);
  if (!g_editor_ui)
  {
    printf("Failed to create world editor UI\n");
    return false;
  }

  // Set up callbacks
  world_editor_ui_set_tool_callback(g_editor_ui, on_tool_change, NULL);
  world_editor_ui_set_voxel_callback(g_editor_ui, on_voxel_change, NULL);
  world_editor_ui_set_file_callback(g_editor_ui, on_file_operation, NULL);
  world_editor_ui_set_actor_callback(g_editor_ui, on_actor_operation, NULL);
  world_editor_ui_set_setting_callback(g_editor_ui, on_setting_change, NULL);

  // Create UI panels
  world_editor_ui_create_toolbar(g_editor_ui, 10, 10);
  world_editor_ui_create_palette(g_editor_ui, 10, 80);
  world_editor_ui_create_file_panel(g_editor_ui, WINDOW_W - 200, 10);
  world_editor_ui_create_info_panel(g_editor_ui, WINDOW_W - 200, 200);
  world_editor_ui_create_actor_panel(g_editor_ui, WINDOW_W - 200, 400);
  world_editor_ui_create_settings_panel(g_editor_ui, WINDOW_W - 200, 600);

  return true;
}

static bool init_world(void)
{
  g_world = world_create(32, 32, 32);
  if (!g_world)
  {
    printf("Failed to create world\n");
    return false;
  }

  world_generate(g_world, "example_world");
  world_editor_ui_update_world_info(g_editor_ui, g_world);

  return true;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

static void handle_events(void)
{
  SDL_Event event;

  while (SDL_PollEvent(&event))
  {
    // Handle quit event
    if (event.type == SDL_QUIT)
    {
      g_running = false;
      continue;
    }

    // Handle window resize
    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED)
    {
      // Handle window resize if needed
      continue;
    }

    // Let the UI system handle the event
    if (world_editor_ui_handle_event(g_editor_ui, &event))
    {
      // Event was handled by UI system
      continue;
    }

    // Handle other events (camera movement, world editing, etc.)
    switch (event.type)
    {
    case SDL_MOUSEBUTTONDOWN:
      if (event.button.button == SDL_BUTTON_LEFT)
      {
        // Handle left mouse button for world editing
        handle_world_editing(event.button.x, event.button.y);
      }
      break;

    case SDL_KEYDOWN:
      handle_key_press(event.key.keysym.sym);
      break;
    }
  }
}

static void handle_world_editing(int mouse_x, int mouse_y)
{
  if (!g_world || g_selected_tool == WORLD_EDITOR_TOOL_SELECT)
  {
    return;
  }

  // Convert screen coordinates to world coordinates
  // This would use the camera/view matrix
  int world_x = mouse_x; // Simplified - would need proper conversion
  int world_y = mouse_y;
  int world_z = 0;

  switch (g_selected_tool)
  {
  case WORLD_EDITOR_TOOL_DRAW:
    // Place single voxel
    if (world_is_position_valid(g_world, world_x, world_y, world_z))
    {
      Voxel voxel = {g_selected_voxel, 0, 0, {0, 0, 0}, {0, 0, 0}};
      world_set_voxel(g_world, world_x, world_y, world_z, &voxel);
    }
    break;

  case WORLD_EDITOR_TOOL_CUBE:
    // Place 3x3x3 cube
    for (int dx = -1; dx <= 1; dx++)
    {
      for (int dy = -1; dy <= 1; dy++)
      {
        for (int dz = -1; dz <= 1; dz++)
        {
          int x = world_x + dx;
          int y = world_y + dy;
          int z = world_z + dz;
          if (world_is_position_valid(g_world, x, y, z))
          {
            Voxel voxel = {g_selected_voxel, 0, 0, {0, 0, 0}, {0, 0, 0}};
            world_set_voxel(g_world, x, y, z, &voxel);
          }
        }
      }
    }
    break;

  case WORLD_EDITOR_TOOL_ERASE:
    // Remove voxel
    if (world_is_position_valid(g_world, world_x, world_y, world_z))
    {
      Voxel voxel = {VOXEL_AIR, 0, 0, {0, 0, 0}, {0, 0, 0}};
      world_set_voxel(g_world, world_x, world_y, world_z, &voxel);
    }
    break;
  }
}

static void handle_key_press(int key)
{
  switch (key)
  {
  case SDLK_ESCAPE:
    g_running = false;
    break;

  case SDLK_1:
    g_selected_tool = WORLD_EDITOR_TOOL_SELECT;
    world_editor_ui_set_selected_tool(g_editor_ui, g_selected_tool);
    break;

  case SDLK_2:
    g_selected_tool = WORLD_EDITOR_TOOL_DRAW;
    world_editor_ui_set_selected_tool(g_editor_ui, g_selected_tool);
    break;

  case SDLK_3:
    g_selected_tool = WORLD_EDITOR_TOOL_CUBE;
    world_editor_ui_set_selected_tool(g_editor_ui, g_selected_tool);
    break;

  case SDLK_4:
    g_selected_tool = WORLD_EDITOR_TOOL_ERASE;
    world_editor_ui_set_selected_tool(g_editor_ui, g_selected_tool);
    break;

  case SDLK_p:
    // Toggle palette visibility
    g_editor_ui->show_palette = !g_editor_ui->show_palette;
    break;
  }
}

static void render(void)
{
  // Clear screen
  SDL_SetRenderDrawColor(g_editor_ui->ui->renderer, 50, 50, 50, 255);
  SDL_RenderClear(g_editor_ui->ui->renderer);

  // Render world (this would use the existing rendering code)
  if (g_world)
  {
    // render_world(g_world); // This would call the existing renderer
  }

  // Render UI
  world_editor_ui_render(g_editor_ui);

  // Present renderer
  SDL_RenderPresent(g_editor_ui->ui->renderer);
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char *argv[])
{
  printf("VERSE World Editor - Refactored Version\n");

  // Initialize SDL
  if (!init_sdl())
  {
    return 1;
  }

  // Create window
  SDL_Window *window = create_window();
  if (!window)
  {
    SDL_Quit();
    return 1;
  }

  // Create renderer
  SDL_Renderer *renderer = create_renderer(window);
  if (!renderer)
  {
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Initialize world editor UI
  if (!init_world_editor_ui(renderer))
  {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  // Initialize world
  if (!init_world())
  {
    world_editor_ui_destroy(g_editor_ui);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  printf("World Editor initialized successfully\n");
  printf("Controls:\n");
  printf("  1-4: Select tools\n");
  printf("  P: Toggle palette\n");
  printf("  ESC: Quit\n");

  // Main loop
  while (g_running)
  {
    handle_events();
    render();
    SDL_Delay(16); // ~60 FPS
  }

  // Cleanup
  if (g_world)
    world_destroy(g_world);
  if (g_editor_ui)
    world_editor_ui_destroy(g_editor_ui);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  printf("World Editor closed\n");
  return 0;
}
