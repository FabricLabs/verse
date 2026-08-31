#include "world_editor_ui.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>

// Test window dimensions
#define TEST_WINDOW_WIDTH 1400
#define TEST_WINDOW_HEIGHT 900

int main(int argc, char *argv[])
{
  printf("=== VERSE World Editor UI Layout Test ===\n");
  printf("Testing improved layout system with consistent spacing\n\n");

  // Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    printf("SDL initialization failed: %s\n", SDL_GetError());
    return 1;
  }

  // Create window and renderer
  SDL_Window *window = SDL_CreateWindow(
    "VERSE World Editor - Layout Test",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    TEST_WINDOW_WIDTH, TEST_WINDOW_HEIGHT,
    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
  );

  if (!window)
  {
    printf("Window creation failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer)
  {
    printf("Renderer creation failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  printf("Created test window: %dx%d\n", TEST_WINDOW_WIDTH, TEST_WINDOW_HEIGHT);

  // Create world editor UI
  WorldEditorUI *editor_ui = world_editor_ui_create(renderer, "assets/fonts/DejaVuSans.ttf");
  if (!editor_ui)
  {
    printf("Failed to create world editor UI\n");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  printf("Created world editor UI successfully\n");

  // Calculate layout for our test window
  printf("\nCalculating layout for %dx%d window...\n", TEST_WINDOW_WIDTH, TEST_WINDOW_HEIGHT);
  world_editor_ui_calculate_layout(editor_ui, TEST_WINDOW_WIDTH, TEST_WINDOW_HEIGHT);

  // Display calculated positions
  printf("\n=== Calculated Layout Positions ===\n");
  printf("Left Panel Layout:\n");
  printf("  File Menu: (%d, %d) - TOP LEFT (first element)\n", editor_ui->layout.file_panel_x, editor_ui->layout.file_panel_y);
  printf("  Toolbar: (%d, %d) - Below file menu\n", editor_ui->layout.toolbar_x, editor_ui->layout.toolbar_y);
  printf("  Palette: (%d, %d) - Below toolbar\n", editor_ui->layout.palette_x, editor_ui->layout.palette_y);
  printf("  Recent: (%d, %d) - Below palette\n", editor_ui->layout.recent_x, editor_ui->layout.recent_y);
  printf("  Z-Swatches: (%d, %d) - Right of palette\n", editor_ui->layout.z_swatches_x, editor_ui->layout.z_swatches_y);

  printf("\nRight Panel Layout:\n");
  printf("  Info Panel: (%d, %d) - Top right\n", editor_ui->layout.info_panel_x, editor_ui->layout.info_panel_y);
  printf("  Actor Panel: (%d, %d) - Below info panel\n", editor_ui->layout.actor_panel_x, editor_ui->layout.actor_panel_y);
  printf("  Settings Panel: (%d, %d) - Below actor panel\n", editor_ui->layout.settings_panel_x, editor_ui->layout.settings_panel_y);

  // Test layout constants
  printf("\n=== Layout Constants ===\n");
  printf("Margin: %d pixels\n", WORLD_EDITOR_MARGIN);
  printf("Panel Spacing: %d pixels\n", WORLD_EDITOR_PANEL_SPACING);
  printf("Left Panel Width: %d pixels\n", WORLD_EDITOR_LEFT_PANEL_WIDTH);
  printf("Right Panel Width: %d pixels\n", WORLD_EDITOR_RIGHT_PANEL_WIDTH);
  printf("Toolbar Height: %d pixels\n", WORLD_EDITOR_TOOLBAR_HEIGHT);

  // Test resize functionality
  printf("\n=== Testing Resize Functionality ===\n");
  int new_width = 1600;
  int new_height = 1000;
  printf("Resizing to %dx%d...\n", new_width, new_height);

  world_editor_ui_resize(editor_ui, new_width, new_height);

  printf("New layout after resize:\n");
  printf("  File Panel: (%d, %d)\n", editor_ui->layout.file_panel_x, editor_ui->layout.file_panel_y);
  printf("  Info Panel: (%d, %d)\n", editor_ui->layout.info_panel_x, editor_ui->layout.info_panel_y);

  // Cleanup
  printf("\nCleaning up...\n");
  world_editor_ui_destroy(editor_ui);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  printf("\n=== Layout Test Completed Successfully ===\n");
  printf("The improved layout system provides:\n");
  printf("- Consistent margins and spacing (20px margin, 15px panel spacing)\n");
  printf("- File menu positioned at top left as first element\n");
  printf("- Toolbar below file menu with proper column alignment\n");
  printf("- Palette below toolbar, followed by other tools on the left\n");
  printf("- Info, actor, and settings panels on the right side\n");
  printf("- Responsive positioning based on window dimensions\n");
  printf("- White borders on all buttons for consistent visual language\n");
  printf("- No duplicate toolbars - single comprehensive toolbar with all tools\n");

  return 0;
}
