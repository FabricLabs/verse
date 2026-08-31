#include "world_editor_ui.h"
#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// INTERNAL CONSTANTS
// ============================================================================

// Tool button dimensions
#define TOOL_BUTTON_WIDTH 70
#define TOOL_BUTTON_HEIGHT 24
#define TOOL_BUTTON_SPACING 8
#define SHAPE_SUB_BUTTON_WIDTH 70
#define ADJACENT_BUTTON_WIDTH 46

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

// Forward declarations for callback functions
static void file_button_click_callback(int button_id, void *user_data);
static void new_world_button_callback(int button_id, void *user_data);
static void open_world_button_callback(int button_id, void *user_data);
static void close_file_browser_callback(int button_id, void *user_data);
static void close_file_menu(WorldEditorUI *editor_ui);
// Forward declaration for file browser population
extern void world_editor_populate_file_browser(const char *path);

// Forward declaration for world editor configuration
extern void world_editor_set_new_world_config(WorldGenerationType world_type, const char *seed, bool generate_neighbors);

// Callback for save as button
static void save_as_button_callback(int button_id, void *user_data);

// Callback for save as modal buttons
static void save_as_modal_save_callback(int button_id, void *user_data);
static void save_as_modal_cancel_callback(int button_id, void *user_data);

// Callback for unsaved changes modal buttons
static void unsaved_changes_modal_save_callback(int button_id, void *user_data);
static void unsaved_changes_modal_discard_callback(int button_id, void *user_data);
static void unsaved_changes_modal_cancel_callback(int button_id, void *user_data);

// Callback for new world modal buttons
static void new_world_modal_generate_callback(int button_id, void *user_data);
static void new_world_modal_cancel_callback(int button_id, void *user_data);
static void new_world_modal_dropdown_callback(int option_index, const char *option_text, void *user_data);
static void new_world_modal_neighbors_checkbox_callback(int button_id, void *user_data);

// Callbacks for actor movement buttons
static void actor_move_up_callback(int button_id, void *user_data);
static void actor_move_down_callback(int button_id, void *user_data);
static void actor_move_left_callback(int button_id, void *user_data);
static void actor_move_right_callback(int button_id, void *user_data);
static void actor_move_forward_callback(int button_id, void *user_data);
static void actor_move_backward_callback(int button_id, void *user_data);

// Callback for new world button
static void new_world_button_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  // Close the file menu when submenu item is selected
  close_file_menu(editor_ui);

  // Check if current world has unsaved changes
  // TODO: Get current world from world editor to check dirty state
  bool has_unsaved_changes = false; // Placeholder - will be implemented when we have world access

  if (has_unsaved_changes)
  {
    // Show unsaved changes confirmation modal
    printf("[editor] New world requested but current world has unsaved changes - showing confirmation modal\n");
    if (editor_ui->unsaved_changes_modal.panel)
    {
      editor_ui->unsaved_changes_modal.panel->visible = true;
      editor_ui->unsaved_changes_modal.title_text->visible = true;
      editor_ui->unsaved_changes_modal.message_text->visible = true;
      editor_ui->unsaved_changes_modal.save_button->visible = true;
      editor_ui->unsaved_changes_modal.discard_button->visible = true;
      editor_ui->unsaved_changes_modal.cancel_button->visible = true;
    }
  }
  else
  {
    // No unsaved changes, show the new world configuration modal
    printf("[editor] New world requested - showing configuration modal\n");
    if (editor_ui->new_world_modal.panel)
    {
      editor_ui->new_world_modal.panel->visible = true;
      editor_ui->new_world_modal.title_text->visible = true;
      editor_ui->new_world_modal.world_type_label->visible = true;
      editor_ui->new_world_modal.world_type_dropdown->visible = true;
      editor_ui->new_world_modal.seed_label->visible = true;
      editor_ui->new_world_modal.seed_input->visible = true;
      editor_ui->new_world_modal.generate_neighbors_checkbox->visible = true;
      editor_ui->new_world_modal.generate_neighbors_label->visible = true;
      editor_ui->new_world_modal.generate_button->visible = true;
      editor_ui->new_world_modal.cancel_button->visible = true;
    }
  }
}

// Callback for open world button
static void open_world_button_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  // Close the file menu when submenu item is selected
  close_file_menu(editor_ui);

  printf("[editor] Open World button clicked - showing file browser\n");

  // Show the file browser panel with comprehensive null checks
  if (editor_ui->file_browser_panel.panel)
  {
    printf("[editor] Making file browser panel visible\n");
    editor_ui->file_browser_panel.panel->visible = true;

    if (editor_ui->file_browser_panel.file_browser)
    {
      printf("[editor] Making file browser component visible\n");
      editor_ui->file_browser_panel.file_browser->visible = true;

      // Set the default path to worlds directory
      if (editor_ui->file_browser_panel.file_browser->current_path)
        free(editor_ui->file_browser_panel.file_browser->current_path);
      editor_ui->file_browser_panel.file_browser->current_path = strdup("worlds/");
      printf("[editor] Set file browser path to: worlds/\n");
    }
    else
    {
      printf("[editor] ERROR: File browser component is null!\n");
    }

    if (editor_ui->file_browser_panel.refresh_button)
    {
      printf("[editor] Making refresh button visible\n");
      editor_ui->file_browser_panel.refresh_button->visible = true;
    }
    else
    {
      printf("[editor] ERROR: Refresh button is null!\n");
    }

    if (editor_ui->file_browser_panel.home_button)
    {
      printf("[editor] Making home button visible\n");
      editor_ui->file_browser_panel.home_button->visible = true;
    }
    else
    {
      printf("[editor] ERROR: Home button is null!\n");
    }

    if (editor_ui->file_browser_panel.close_button)
    {
      printf("[editor] Making close button visible\n");
      editor_ui->file_browser_panel.close_button->visible = true;
    }
    else
    {
      printf("[editor] ERROR: Close button is null!\n");
    }

    printf("[editor] File browser panel opened successfully\n");
  }
  else
  {
    printf("[editor] ERROR: File browser panel is null!\n");
  }
}

static void close_file_browser_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[editor] Close file browser button clicked - hiding file browser\n");

  // Hide the file browser panel and all its components with comprehensive null checks
  if (editor_ui->file_browser_panel.panel)
  {
    printf("[editor] Hiding file browser panel\n");
    editor_ui->file_browser_panel.panel->visible = false;

    if (editor_ui->file_browser_panel.file_browser)
    {
      printf("[editor] Hiding file browser component\n");
      editor_ui->file_browser_panel.file_browser->visible = false;
    }
    else
    {
      printf("[editor] ERROR: File browser component is null!\n");
    }

    if (editor_ui->file_browser_panel.refresh_button)
    {
      printf("[editor] Hiding refresh button\n");
      editor_ui->file_browser_panel.refresh_button->visible = false;
    }
    else
    {
      printf("[editor] ERROR: Refresh button is null!\n");
    }

    if (editor_ui->file_browser_panel.home_button)
    {
      printf("[editor] Hiding home button\n");
      editor_ui->file_browser_panel.home_button->visible = false;
    }
    else
    {
      printf("[editor] ERROR: Home button is null!\n");
    }

    if (editor_ui->file_browser_panel.close_button)
    {
      printf("[editor] Hiding close button\n");
      editor_ui->file_browser_panel.close_button->visible = false;
    }
    else
    {
      printf("[editor] ERROR: Close button is null!\n");
    }

    printf("[editor] File browser panel closed successfully\n");
  }
  else
  {
    printf("[editor] ERROR: File browser panel is null!\n");
  }
}

static void exit_button_callback(int button_id, void *user_data)
{
  (void)button_id; // Unused parameter
  (void)user_data; // Unused parameter

  // Exit the application
  printf("[editor] Exit button clicked - quitting application\n");
  exit(0);
}

static void save_as_button_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  // Close the file menu when submenu item is selected
  close_file_menu(editor_ui);

  // Show the save as modal dialog
  printf("[editor] Save As button clicked - showing modal dialog\n");
  if (editor_ui->save_as_modal.panel)
  {
    editor_ui->save_as_modal.panel->visible = true;
    editor_ui->save_as_modal.filename_input->visible = true;
    editor_ui->save_as_modal.save_button->visible = true;
    editor_ui->save_as_modal.cancel_button->visible = true;

    // Set default filename based on current world
    if (editor_ui->save_as_modal.current_filename)
      free(editor_ui->save_as_modal.current_filename);

    // For now, use a generic default filename that matches the expected format
    // TODO: In the future, this should get the actual world seed and type
    // from the world editor through a proper callback system
    editor_ui->save_as_modal.current_filename = strdup("generated_world.HOME.world");

    // Set the input field text
    if (editor_ui->save_as_modal.filename_input)
    {
      if (editor_ui->save_as_modal.filename_input->text)
        free(editor_ui->save_as_modal.filename_input->text);
      editor_ui->save_as_modal.filename_input->text = strdup(editor_ui->save_as_modal.current_filename);
      editor_ui->save_as_modal.filename_input->text_length = strlen(editor_ui->save_as_modal.current_filename);
    }
  }
}

static void save_as_modal_save_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  // Get the filename from the input field
  const char *filename = NULL;
  if (editor_ui->save_as_modal.filename_input)
  {
    filename = editor_ui->save_as_modal.filename_input->text;
  }

  if (filename && strlen(filename) > 0)
  {
    printf("[editor] Save As modal: Saving with filename: %s\n", filename);

    // Hide the modal
    editor_ui->save_as_modal.panel->visible = false;
    editor_ui->save_as_modal.filename_input->visible = false;
    editor_ui->save_as_modal.save_button->visible = false;
    editor_ui->save_as_modal.cancel_button->visible = false;

    // Trigger the save as operation with the filename
    // TODO: Pass the filename to the save operation
    world_editor_ui_trigger_file_operation(editor_ui, WORLD_EDITOR_FILE_SAVE_AS);
  }
  else
  {
    printf("[editor] Save As modal: No filename entered\n");
  }
}

static void save_as_modal_cancel_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[editor] Save As modal: Cancelled\n");

  // Hide the modal
  if (editor_ui->save_as_modal.panel)
  {
    editor_ui->save_as_modal.panel->visible = false;
    editor_ui->save_as_modal.filename_input->visible = false;
    editor_ui->save_as_modal.save_button->visible = false;
    editor_ui->save_as_modal.cancel_button->visible = false;
  }
}

static void unsaved_changes_modal_save_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[editor] Unsaved changes modal: User chose to save\n");

  // Hide the modal
  if (editor_ui->unsaved_changes_modal.panel)
  {
    editor_ui->unsaved_changes_modal.panel->visible = false;
    editor_ui->unsaved_changes_modal.title_text->visible = false;
    editor_ui->unsaved_changes_modal.message_text->visible = false;
    editor_ui->unsaved_changes_modal.save_button->visible = false;
    editor_ui->unsaved_changes_modal.discard_button->visible = false;
    editor_ui->unsaved_changes_modal.cancel_button->visible = false;
  }

  // Show the save as modal instead
  if (editor_ui->save_as_modal.panel)
  {
    editor_ui->save_as_modal.panel->visible = true;
    editor_ui->save_as_modal.filename_input->visible = true;
    editor_ui->save_as_modal.save_button->visible = true;
    editor_ui->save_as_modal.cancel_button->visible = true;

    // Set default filename
    if (editor_ui->save_as_modal.current_filename)
      free(editor_ui->save_as_modal.current_filename);
    editor_ui->save_as_modal.current_filename = strdup("generated_world.HOME.world");

    // Set the input field text
    if (editor_ui->save_as_modal.filename_input)
    {
      if (editor_ui->save_as_modal.filename_input->text)
        free(editor_ui->save_as_modal.filename_input->text);
      editor_ui->save_as_modal.filename_input->text = strdup(editor_ui->save_as_modal.current_filename);
      editor_ui->save_as_modal.filename_input->text_length = strlen(editor_ui->save_as_modal.current_filename);
    }
  }
}

static void unsaved_changes_modal_discard_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[editor] Unsaved changes modal: User chose to discard changes\n");

  // Hide the modal
  if (editor_ui->unsaved_changes_modal.panel)
  {
    editor_ui->unsaved_changes_modal.panel->visible = false;
    editor_ui->unsaved_changes_modal.title_text->visible = false;
    editor_ui->unsaved_changes_modal.message_text->visible = false;
    editor_ui->unsaved_changes_modal.save_button->visible = false;
    editor_ui->unsaved_changes_modal.discard_button->visible = false;
    editor_ui->unsaved_changes_modal.cancel_button->visible = false;
  }

  // Proceed with generating new world (discarding current changes)
  world_editor_ui_trigger_file_operation(editor_ui, WORLD_EDITOR_FILE_NEW);
}

static void unsaved_changes_modal_cancel_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[editor] Unsaved changes modal: User cancelled\n");

  // Hide the modal
  if (editor_ui->unsaved_changes_modal.panel)
  {
    editor_ui->unsaved_changes_modal.panel->visible = false;
    editor_ui->unsaved_changes_modal.title_text->visible = false;
    editor_ui->unsaved_changes_modal.message_text->visible = false;
    editor_ui->unsaved_changes_modal.save_button->visible = false;
    editor_ui->unsaved_changes_modal.discard_button->visible = false;
    editor_ui->unsaved_changes_modal.cancel_button->visible = false;
  }
}

// New world modal callbacks
static void new_world_modal_generate_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[editor] New world modal: User chose to generate world\n");

  // Get the selected world type and seed from the modal
  int world_type = editor_ui->new_world_modal.selected_world_type;

  // Convert dropdown selection to world type enum
  WorldGenerationType generation_type;
  switch (world_type) {
    case 0: generation_type = WORLD_TYPE_HOME; break;
    case 1: generation_type = WORLD_TYPE_FARM; break;
    case 2: generation_type = WORLD_TYPE_RANDOM; break;
    case 3: generation_type = WORLD_TYPE_WILDERNESS; break;
    case 4: generation_type = WORLD_TYPE_SOLID; break;
    case 5: generation_type = WORLD_TYPE_UNDERWORLD; break;
    case 6: generation_type = WORLD_TYPE_SCOURED; break;
    case 7: generation_type = WORLD_TYPE_LABYRINTH_SQUARE; break;
    case 8: generation_type = WORLD_TYPE_WFC_TOWN; break;
    case 9: generation_type = WORLD_TYPE_CLOUD; break;
    case 10: generation_type = WORLD_TYPE_ARENA; break;
    default: generation_type = WORLD_TYPE_HOME; break;
  }

  // Get the seed from the input field
  const char *seed_text = "";
  if (editor_ui->new_world_modal.seed_input && editor_ui->new_world_modal.seed_input->text) {
    seed_text = editor_ui->new_world_modal.seed_input->text;
  }

  // Hide the modal
  if (editor_ui->new_world_modal.panel)
  {
    editor_ui->new_world_modal.panel->visible = false;
    editor_ui->new_world_modal.title_text->visible = false;
    editor_ui->new_world_modal.world_type_label->visible = false;
    editor_ui->new_world_modal.world_type_dropdown->visible = false;
    editor_ui->new_world_modal.seed_label->visible = false;
    editor_ui->new_world_modal.seed_input->visible = false;
    editor_ui->new_world_modal.generate_neighbors_checkbox->visible = false;
    editor_ui->new_world_modal.generate_neighbors_label->visible = false;
    editor_ui->new_world_modal.generate_button->visible = false;
    editor_ui->new_world_modal.cancel_button->visible = false;
  }

  // Trigger the world generation with the configured parameters
  printf("[editor] Generating world type %d with seed: %s, neighbors: %s\n",
         generation_type, seed_text, editor_ui->new_world_modal.generate_neighbors ? "yes" : "no");

  // Set the world configuration in the world editor
  world_editor_set_new_world_config(generation_type, seed_text, editor_ui->new_world_modal.generate_neighbors);

  // Trigger the world generation operation
  world_editor_ui_trigger_file_operation(editor_ui, WORLD_EDITOR_FILE_NEW);
}

static void new_world_modal_cancel_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[editor] New world modal: User cancelled\n");

  // Hide the modal
  if (editor_ui->new_world_modal.panel)
  {
    editor_ui->new_world_modal.panel->visible = false;
    editor_ui->new_world_modal.title_text->visible = false;
    editor_ui->new_world_modal.world_type_label->visible = false;
    editor_ui->new_world_modal.world_type_dropdown->visible = false;
    editor_ui->new_world_modal.seed_label->visible = false;
    editor_ui->new_world_modal.seed_input->visible = false;
    editor_ui->new_world_modal.generate_neighbors_checkbox->visible = false;
    editor_ui->new_world_modal.generate_neighbors_label->visible = false;
    editor_ui->new_world_modal.generate_button->visible = false;
    editor_ui->new_world_modal.cancel_button->visible = false;
  }
}

static void new_world_modal_neighbors_checkbox_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  // Toggle the checkbox state
  editor_ui->new_world_modal.generate_neighbors = !editor_ui->new_world_modal.generate_neighbors;

  // Update button appearance and text
  if (editor_ui->new_world_modal.generate_neighbors_checkbox)
  {
    editor_ui->new_world_modal.generate_neighbors_checkbox->selected = editor_ui->new_world_modal.generate_neighbors;

    // Update button text to show "X" when checked, empty when unchecked
    if (editor_ui->new_world_modal.generate_neighbors)
    {
      // Cast away const to modify the text field
      *((char**)&editor_ui->new_world_modal.generate_neighbors_checkbox->text) = "X";
    }
    else
    {
      // Cast away const to modify the text field
      *((char**)&editor_ui->new_world_modal.generate_neighbors_checkbox->text) = "";
    }
  }

  printf("[editor] New world modal: Generate neighbors %s\n",
         editor_ui->new_world_modal.generate_neighbors ? "enabled" : "disabled");
}

// Dropdown callback for world type selection
static void new_world_modal_dropdown_callback(int option_index, const char *option_text, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[editor] New world modal: World type selected: %s (index: %d)\n", option_text, option_index);
  editor_ui->new_world_modal.selected_world_type = option_index;
}

// ============================================================================
// INITIALIZATION & CLEANUP
// ============================================================================

WorldEditorUI *world_editor_ui_create(SDL_Renderer *renderer, const char *font_path)
{
  if (!renderer || !font_path)
  {
    printf("[editor] Invalid parameters for world_editor_ui_create\n");
    return NULL;
  }

  printf("[editor] Creating world editor UI system\n");

  // Create the main editor UI structure
  WorldEditorUI *editor_ui = calloc(1, sizeof(WorldEditorUI));
  if (!editor_ui)
  {
    printf("[editor] Failed to allocate WorldEditorUI\n");
    return NULL;
  }

  // Create the core UI system
  editor_ui->ui_system = ui_system_create(renderer, font_path);
  if (!editor_ui->ui_system)
  {
    printf("[editor] Failed to create UI system\n");
    free(editor_ui);
    return NULL;
  }
  printf("[editor] UI system created successfully: %p\n", (void *)editor_ui->ui_system);

  // Initialize layout with default window dimensions (will be updated later)
  printf("[editor] Initializing default layout...\n");
  world_editor_ui_calculate_layout(editor_ui, 1200, 800);
  printf("[editor] Default layout initialized\n");

  printf("[editor] World editor UI system created successfully\n");
  return editor_ui;
}

// Create minimal UI panels for testing (just file panel and toolbar)
void world_editor_ui_create_minimal_panels(WorldEditorUI *editor_ui)
{
  if (!editor_ui)
    return;

  printf("[editor] Creating minimal UI panels for testing...\n");

  // Create just the essential panels
  printf("[editor] Creating file panel...\n");
  world_editor_ui_create_file_panel(editor_ui, 0, 0); // File menu at top left (first)
  printf("[editor] File panel created successfully\n");

  printf("[editor] Creating toolbar...\n");
  world_editor_ui_create_toolbar(editor_ui, 0, 0); // Toolbar below file menu
  printf("[editor] Toolbar created successfully\n");

  printf("[editor] Creating new world modal...\n");
  world_editor_ui_create_new_world_modal(editor_ui); // New world configuration modal
  printf("[editor] New world modal created successfully\n");

  printf("[editor] Creating progress bar...\n");
  world_editor_ui_create_progress_bar(editor_ui); // Progress bar for world generation
  printf("[editor] Progress bar created successfully\n");

  // Explicitly set all other panels to NULL to prevent any coverage
  editor_ui->file_browser_panel.panel = NULL;
  editor_ui->file_browser_panel.file_browser = NULL;
  editor_ui->save_as_modal.panel = NULL;
  editor_ui->unsaved_changes_modal.panel = NULL;

  printf("[editor] Minimal UI panels created successfully\n");
}

// Create all UI panels with calculated layout positions
void world_editor_ui_create_all_panels(WorldEditorUI *editor_ui)
{
  if (!editor_ui)
    return;

  printf("[editor] Creating all UI panels with calculated layout\n");

  // Create all panels using calculated positions in layout order
  printf("[editor] Creating file panel...\n");
  world_editor_ui_create_file_panel(editor_ui, 0, 0); // File menu at top left (first)
  printf("[editor] File panel created successfully\n");

  printf("[editor] Creating toolbar...\n");
  world_editor_ui_create_toolbar(editor_ui, 0, 0); // Toolbar below file menu
  printf("[editor] Toolbar created successfully\n");

#if ENABLE_NEW_UI_PANELS
  printf("[editor] Creating palette...\n");
  world_editor_ui_create_palette(editor_ui, 0, 0); // Palette below toolbar
  printf("[editor] Palette created successfully\n");

  printf("[editor] Creating recent panel...\n");
  world_editor_ui_create_recent_panel(editor_ui, 0, 0); // Recent panel below palette
  printf("[editor] Recent panel created successfully\n");

  printf("[editor] Creating Z-swatches...\n");
  world_editor_ui_create_z_swatches(editor_ui, 0, 0); // Z-swatches to the right of palette
  printf("[editor] Z-swatches created successfully\n");

  printf("[editor] Creating info panel...\n");
  world_editor_ui_create_info_panel(editor_ui, 0, 0); // Info panel at top right
  printf("[editor] Info panel created successfully\n");

  printf("[editor] Creating actor panel...\n");
  world_editor_ui_create_actor_panel(editor_ui, 0, 0); // Actor panel below info panel
  printf("[editor] Actor panel created successfully\n");

  printf("[editor] Creating settings panel...\n");
  world_editor_ui_create_settings_panel(editor_ui, 0, 0); // Settings panel below actor panel
  printf("[editor] Settings panel created successfully\n");
#else
  // When new UI panels are disabled, explicitly set panel pointers to NULL
  editor_ui->palette.panel = NULL;
  editor_ui->recent.panel = NULL;
  editor_ui->z_swatches.panel = NULL;
  editor_ui->info.panel = NULL;
  editor_ui->actor.panel = NULL;
  editor_ui->settings.panel = NULL;
  printf("[editor] New UI panels disabled - only File Menu and Toolbar available\n");
#endif

  printf("[editor] Creating file browser panel...\n");
  world_editor_ui_create_file_browser_panel(editor_ui, 0, 0); // File browser (hidden by default)
  printf("[editor] File browser panel created successfully\n");

  printf("[editor] Creating save as modal...\n");
  world_editor_ui_create_save_as_modal(editor_ui); // Save as modal
  printf("[editor] Save as modal created successfully\n");

  printf("[editor] Creating unsaved changes modal...\n");
  world_editor_ui_create_unsaved_changes_modal(editor_ui); // Unsaved changes modal
  printf("[editor] Unsaved changes modal created successfully\n");

  printf("[editor] Creating new world modal...\n");
  world_editor_ui_create_new_world_modal(editor_ui); // New world configuration modal
  printf("[editor] New world modal created successfully\n");

  printf("[editor] Creating progress bar...\n");
  world_editor_ui_create_progress_bar(editor_ui); // Progress bar for world generation
  printf("[editor] Progress bar created successfully\n");

  printf("[editor] All UI panels created successfully\n");
}

// Update layout for new window dimensions
void world_editor_ui_resize(WorldEditorUI *editor_ui, int new_width, int new_height)
{
  if (!editor_ui)
    return;

  printf("[editor] Updating layout for window size %dx%d\n", new_width, new_height);

  // Recalculate layout positions
  world_editor_ui_calculate_layout(editor_ui, new_width, new_height);

  // TODO: Reposition existing panels if needed
  // For now, this just updates the layout calculations
}

// ============================================================================
// DESTRUCTION
// ============================================================================

void world_editor_ui_destroy(WorldEditorUI *editor_ui)
{
  if (!editor_ui)
    return;

  // Destroy the core UI system (this will clean up all UI elements)
  if (editor_ui->ui_system)
  {
    ui_system_destroy(editor_ui->ui_system);
  }

  free(editor_ui);
}

// ============================================================================
// LAYOUT CALCULATION
// ============================================================================

void world_editor_ui_calculate_layout(WorldEditorUI *editor_ui, int window_width, int window_height)
{
  if (!editor_ui)
    return;

  // Calculate left panel positions (file menu, toolbar, palette, recent)
  int left_x = WORLD_EDITOR_MARGIN;
  int current_y = WORLD_EDITOR_MARGIN;

  // File menu at top left (first element) - just a row of buttons
  int file_panel_x = left_x;
  int file_panel_y = current_y;
  current_y += 20 + WORLD_EDITOR_PANEL_SPACING; // File menu button row height

  // Toolbar below file menu row (immediately below the button row)
  int toolbar_x = left_x;
  int toolbar_y = current_y;
  current_y += WORLD_EDITOR_TOOLBAR_HEIGHT + WORLD_EDITOR_PANEL_SPACING;

#if ENABLE_NEW_UI_PANELS
  // Palette below toolbar
  int palette_x = left_x;
  int palette_y = current_y;
  current_y += WORLD_EDITOR_PALETTE_HEIGHT + WORLD_EDITOR_PANEL_SPACING;

  // Recent panel below palette
  int recent_x = left_x;
  int recent_y = current_y;
  current_y += 80 + WORLD_EDITOR_PANEL_SPACING; // Recent panel height

  // Z-swatches to the right of palette
  int z_swatches_x = left_x + WORLD_EDITOR_LEFT_PANEL_WIDTH + WORLD_EDITOR_PANEL_SPACING;
  int z_swatches_y = palette_y;

  // File browser panel (initially hidden, positioned below Z-swatches)
  int file_browser_panel_x = z_swatches_x;
  int file_browser_panel_y = z_swatches_y + 180 + WORLD_EDITOR_PANEL_SPACING;

  // Calculate right panel positions (info, actor, settings)
  int right_x = window_width - WORLD_EDITOR_RIGHT_PANEL_WIDTH - WORLD_EDITOR_MARGIN;
  current_y = WORLD_EDITOR_MARGIN;

  // Info panel at top right
  int info_panel_x = right_x;
  int info_panel_y = current_y;
  current_y += WORLD_EDITOR_INFO_PANEL_HEIGHT + WORLD_EDITOR_PANEL_SPACING;

  // Actor panel below info panel
  int actor_panel_x = right_x;
  int actor_panel_y = current_y;
  current_y += WORLD_EDITOR_ACTOR_PANEL_HEIGHT + WORLD_EDITOR_PANEL_SPACING;

  // Settings panel below actor panel
  int settings_panel_x = right_x;
  int settings_panel_y = current_y;
#else
  // When new UI panels are disabled, only file panel and toolbar are positioned
  // Set unused panel positions to 0 to indicate they're not available
  int palette_x = 0, palette_y = 0;
  int recent_x = 0, recent_y = 0;
  int z_swatches_x = 0, z_swatches_y = 0;
  int file_browser_panel_x = 0, file_browser_panel_y = 0;
  int info_panel_x = 0, info_panel_y = 0;
  int actor_panel_x = 0, actor_panel_y = 0;
  int settings_panel_x = 0, settings_panel_y = 0;
#endif

  // Store calculated positions for use in panel creation
  editor_ui->layout.toolbar_x = toolbar_x;
  editor_ui->layout.toolbar_y = toolbar_y;
  editor_ui->layout.file_panel_x = file_panel_x;
  editor_ui->layout.file_panel_y = file_panel_y;
#if ENABLE_NEW_UI_PANELS
  editor_ui->layout.palette_x = palette_x;
  editor_ui->layout.palette_y = palette_y;
  editor_ui->layout.recent_x = recent_x;
  editor_ui->layout.recent_y = recent_y;
  editor_ui->layout.z_swatches_x = z_swatches_x;
  editor_ui->layout.z_swatches_y = z_swatches_y;
  editor_ui->layout.file_browser_panel_x = file_browser_panel_x;
  editor_ui->layout.file_browser_panel_y = file_browser_panel_y;
  editor_ui->layout.info_panel_x = info_panel_x;
  editor_ui->layout.info_panel_y = info_panel_y;
  editor_ui->layout.actor_panel_x = actor_panel_x;
  editor_ui->layout.actor_panel_y = actor_panel_y;
  editor_ui->layout.settings_panel_x = settings_panel_x;
  editor_ui->layout.settings_panel_y = settings_panel_y;
#endif
}

// ============================================================================
// TOOLBAR CREATION
// ============================================================================

void world_editor_ui_create_toolbar(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
  {
    printf("[toolbar] Failed to create toolbar - invalid editor_ui or ui_system\n");
    return;
  }

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int toolbar_x = (editor_ui->layout.toolbar_x > 0) ? editor_ui->layout.toolbar_x : x;
  int toolbar_y = (editor_ui->layout.toolbar_y > 0) ? editor_ui->layout.toolbar_y : y;

  printf("[toolbar] Creating toolbar at position (%d, %d)\n", toolbar_x, toolbar_y);

  // Create tool buttons
  editor_ui->toolbar.cursor_button = ui_create_button(
      editor_ui->ui_system, toolbar_x, toolbar_y, 70, 20, "Select", WORLD_EDITOR_TOOL_CURSOR);
  if (editor_ui->toolbar.cursor_button)
  {
    ui_button_style_world_editor(editor_ui->toolbar.cursor_button);
    printf("[toolbar] Created Select button at (%d, %d)\n", toolbar_x, toolbar_y);
  }
  else
  {
    printf("[toolbar] Failed to create Select button\n");
  }

  editor_ui->toolbar.shape_button = ui_create_button(
      editor_ui->ui_system, toolbar_x + 78, toolbar_y, 70, 20, "Draw", WORLD_EDITOR_TOOL_SHAPE);
  if (editor_ui->toolbar.shape_button)
  {
    ui_button_style_world_editor(editor_ui->toolbar.shape_button);
    printf("[toolbar] Created Draw button at (%d, %d)\n", toolbar_x + 78, toolbar_y);
  }
  else
  {
    printf("[toolbar] Failed to create Draw button\n");
  }

  editor_ui->toolbar.cube_button = ui_create_button(
      editor_ui->ui_system, toolbar_x + 156, toolbar_y, 70, 20, "Cube", WORLD_EDITOR_TOOL_CUBE);
  if (editor_ui->toolbar.cube_button)
  {
    ui_button_style_world_editor(editor_ui->toolbar.cube_button);
    printf("[toolbar] Created Cube button at (%d, %d)\n", toolbar_x + 156, toolbar_y);
  }
  else
  {
    printf("[toolbar] Failed to create Cube button\n");
  }

  editor_ui->toolbar.sphere_button = ui_create_button(
      editor_ui->ui_system, toolbar_x + 234, toolbar_y, 70, 20, "Sphere", WORLD_EDITOR_TOOL_SPHERE);
  if (editor_ui->toolbar.sphere_button)
  {
    ui_button_style_world_editor(editor_ui->toolbar.sphere_button);
    printf("[toolbar] Created Sphere button at (%d, %d)\n", toolbar_x + 234, toolbar_y);
  }
  else
  {
    printf("[toolbar] Failed to create Sphere button\n");
  }

  editor_ui->toolbar.adjacent_button = ui_create_button(
      editor_ui->ui_system, toolbar_x + 312, toolbar_y, 46, 20, "Adj", WORLD_EDITOR_TOOL_ADJACENT);
  if (editor_ui->toolbar.adjacent_button)
  {
    ui_button_style_world_editor(editor_ui->toolbar.adjacent_button);
    printf("[toolbar] Created Adj button at (%d, %d)\n", toolbar_x + 312, toolbar_y);
  }
  else
  {
    printf("[toolbar] Failed to create Adj button\n");
  }

  // Set Select tool as default selected
  if (editor_ui->toolbar.cursor_button)
  {
    editor_ui->toolbar.cursor_button->selected = true;
    printf("[toolbar] Set Select button as default selected\n");
  }

  // Set up button callbacks to trigger tool changes
  if (editor_ui->toolbar.cursor_button)
    ui_button_set_callback(editor_ui->toolbar.cursor_button, editor_ui->tool_callback, editor_ui->tool_user_data);
  if (editor_ui->toolbar.shape_button)
    ui_button_set_callback(editor_ui->toolbar.shape_button, editor_ui->tool_callback, editor_ui->tool_user_data);
  if (editor_ui->toolbar.cube_button)
    ui_button_set_callback(editor_ui->toolbar.cube_button, editor_ui->tool_callback, editor_ui->tool_user_data);
  if (editor_ui->toolbar.sphere_button)
    ui_button_set_callback(editor_ui->toolbar.sphere_button, editor_ui->tool_callback, editor_ui->tool_user_data);
  if (editor_ui->toolbar.adjacent_button)
    ui_button_set_callback(editor_ui->toolbar.adjacent_button, editor_ui->tool_callback, editor_ui->tool_user_data);

  printf("[toolbar] Toolbar creation completed with callbacks\n");

  // Set toolbar buttons to high Z-order so they render above panels but below dropdowns
  if (editor_ui->toolbar.cursor_button)
    ui_button_set_z_order(editor_ui->toolbar.cursor_button, UI_Z_TOOLBAR);
  if (editor_ui->toolbar.shape_button)
    ui_button_set_z_order(editor_ui->toolbar.shape_button, UI_Z_TOOLBAR);
  if (editor_ui->toolbar.cube_button)
    ui_button_set_z_order(editor_ui->toolbar.cube_button, UI_Z_TOOLBAR);
  if (editor_ui->toolbar.sphere_button)
    ui_button_set_z_order(editor_ui->toolbar.sphere_button, UI_Z_TOOLBAR);
  if (editor_ui->toolbar.adjacent_button)
    ui_button_set_z_order(editor_ui->toolbar.adjacent_button, UI_Z_TOOLBAR);
}

void world_editor_ui_create_palette(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int palette_x = (editor_ui->layout.palette_x > 0) ? editor_ui->layout.palette_x : x;
  int palette_y = (editor_ui->layout.palette_y > 0) ? editor_ui->layout.palette_y : y;

  printf("[palette] Creating palette at calculated position (%d, %d)\n", palette_x, palette_y);

  // Create palette panel
  editor_ui->palette.panel = ui_create_panel(editor_ui->ui_system, palette_x, palette_y, 180, 300, "Palette");
  if (!editor_ui->palette.panel)
  {
    printf("[palette] Failed to create palette panel\n");
    return;
  }
  ui_panel_style_world_editor(editor_ui->palette.panel);

  // Create voxel type buttons (simplified - just a few for testing)
  for (int i = 0; i < 8; i++)
  {
    char label[16];
    snprintf(label, sizeof(label), "Voxel%d", i);

    int row = i / 2;
    int col = i % 2;
    int btn_x = palette_x + 10 + col * 80;
    int btn_y = palette_y + 30 + row * 30; // Increased offset to avoid header overlap

    editor_ui->palette.voxel_buttons[i] = ui_create_button(
        editor_ui->ui_system, btn_x, btn_y, 70, 24, label, i);
    if (editor_ui->palette.voxel_buttons[i])
    {
      ui_button_style_world_editor(editor_ui->palette.voxel_buttons[i]);
    }
    else
    {
      printf("[palette] Failed to create voxel button %d\n", i);
    }
  }
}

void world_editor_ui_create_recent_panel(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int recent_x = (editor_ui->layout.recent_x > 0) ? editor_ui->layout.recent_x : x;
  int recent_y = (editor_ui->layout.recent_y > 0) ? editor_ui->layout.recent_y : y;

  // Create recent swatches panel
  editor_ui->recent.panel = ui_create_panel(editor_ui->ui_system, recent_x, recent_y, 200, 60, "Recent");
  if (!editor_ui->recent.panel)
  {
    printf("[recent] Failed to create recent panel\n");
    return;
  }
  ui_panel_style_world_editor(editor_ui->recent.panel);

  // Create recent swatch buttons
  for (int i = 0; i < 5; i++)
  {
    char label[16];
    snprintf(label, sizeof(label), "R%d", i);

    int btn_x = recent_x + 10 + i * 36;
    int btn_y = recent_y + 30; // Increased offset to avoid header overlap

    editor_ui->recent.swatch_buttons[i] = ui_create_button(
        editor_ui->ui_system, btn_x, btn_y, 30, 30, label, i);
    if (editor_ui->recent.swatch_buttons[i])
    {
      ui_button_style_world_editor(editor_ui->recent.swatch_buttons[i]);
    }
    else
    {
      printf("[recent] Failed to create swatch button %d\n", i);
    }
  }
}

void world_editor_ui_create_z_swatches(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int z_swatches_x = (editor_ui->layout.z_swatches_x > 0) ? editor_ui->layout.z_swatches_x : x;
  int z_swatches_y = (editor_ui->layout.z_swatches_y > 0) ? editor_ui->layout.z_swatches_y : y;

  // Create Z-level swatches panel
  editor_ui->z_swatches.panel = ui_create_panel(editor_ui->ui_system, z_swatches_x, z_swatches_y, 60, 180, "Z Levels");
  if (!editor_ui->z_swatches.panel)
  {
    printf("[z_swatches] Failed to create Z-swatches panel\n");
    return;
  }
  ui_panel_style_world_editor(editor_ui->z_swatches.panel);

  // Create Z-level swatch buttons (vertical column)
  for (int i = 0; i < 10; i++)
  {
    char label[16];
    snprintf(label, sizeof(label), "Z%d", i * 10);

    int btn_x = z_swatches_x + 5;
    int btn_y = z_swatches_y + 30 + i * 16; // Increased offset to avoid header overlap

    editor_ui->z_swatches.swatch_buttons[i] = ui_create_button(
        editor_ui->ui_system, btn_x, btn_y, 50, 14, label, i);
    if (editor_ui->z_swatches.swatch_buttons[i])
    {
      ui_button_style_world_editor(editor_ui->z_swatches.swatch_buttons[i]);
    }
    else
    {
      printf("[z_swatches] Failed to create Z-swatch button %d\n", i);
    }
  }
}

void world_editor_ui_create_actor_panel(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int actor_panel_x = (editor_ui->layout.actor_panel_x > 0) ? editor_ui->layout.actor_panel_x : x;
  int actor_panel_y = (editor_ui->layout.actor_panel_y > 0) ? editor_ui->layout.actor_panel_y : y;

  // Create actor panel
  editor_ui->actor.panel = ui_create_panel(editor_ui->ui_system, actor_panel_x, actor_panel_y, 200, 150, "Actors");
  if (!editor_ui->actor.panel)
  {
    printf("[actor] Failed to create actor panel\n");
    return;
  }
  ui_panel_style_world_editor(editor_ui->actor.panel);

  // Create actor management buttons
  editor_ui->actor.add_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 10, actor_panel_y + 30, 80, 24, "Add Actor", WORLD_EDITOR_ACTOR_ADD);
  if (editor_ui->actor.add_button)
  {
    ui_button_style_world_editor(editor_ui->actor.add_button);
  }
  else
  {
    printf("[actor] Failed to create add button\n");
  }

  editor_ui->actor.remove_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 100, actor_panel_y + 30, 80, 24, "Remove", WORLD_EDITOR_ACTOR_REMOVE);
  if (editor_ui->actor.remove_button)
  {
    ui_button_style_world_editor(editor_ui->actor.remove_button);
  }
  else
  {
    printf("[actor] Failed to create remove button\n");
  }

  editor_ui->actor.edit_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 10, actor_panel_y + 60, 80, 24, "Edit", WORLD_EDITOR_ACTOR_EDIT);
  if (editor_ui->actor.edit_button)
  {
    ui_button_style_world_editor(editor_ui->actor.edit_button);
  }
  else
  {
    printf("[actor] Failed to create edit button\n");
  }

  // Create actor movement buttons
  editor_ui->actor.move_up_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 10, actor_panel_y + 90, 30, 24, "↑", 0);
  if (editor_ui->actor.move_up_button)
  {
    ui_button_style_world_editor(editor_ui->actor.move_up_button);
    ui_button_set_callback(editor_ui->actor.move_up_button, actor_move_up_callback, editor_ui);
  }

  editor_ui->actor.move_down_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 10, actor_panel_y + 120, 30, 24, "↓", 0);
  if (editor_ui->actor.move_down_button)
  {
    ui_button_style_world_editor(editor_ui->actor.move_down_button);
    ui_button_set_callback(editor_ui->actor.move_down_button, actor_move_down_callback, editor_ui);
  }

  editor_ui->actor.move_left_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 50, actor_panel_y + 105, 30, 24, "←", 0);
  if (editor_ui->actor.move_left_button)
  {
    ui_button_style_world_editor(editor_ui->actor.move_left_button);
    ui_button_set_callback(editor_ui->actor.move_left_button, actor_move_left_callback, editor_ui);
  }

  editor_ui->actor.move_right_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 90, actor_panel_y + 105, 30, 24, "→", 0);
  if (editor_ui->actor.move_right_button)
  {
    ui_button_style_world_editor(editor_ui->actor.move_right_button);
    ui_button_set_callback(editor_ui->actor.move_right_button, actor_move_right_callback, editor_ui);
  }

  editor_ui->actor.move_forward_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 130, actor_panel_y + 90, 30, 24, "↗", 0);
  if (editor_ui->actor.move_forward_button)
  {
    ui_button_style_world_editor(editor_ui->actor.move_forward_button);
    ui_button_set_callback(editor_ui->actor.move_forward_button, actor_move_forward_callback, editor_ui);
  }

  editor_ui->actor.move_backward_button = ui_create_button(
      editor_ui->ui_system, actor_panel_x + 130, actor_panel_y + 120, 30, 24, "↙", 0);
  if (editor_ui->actor.move_backward_button)
  {
    ui_button_style_world_editor(editor_ui->actor.move_backward_button);
    ui_button_set_callback(editor_ui->actor.move_backward_button, actor_move_backward_callback, editor_ui);
  }
}

void world_editor_ui_create_info_panel(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int info_panel_x = (editor_ui->layout.info_panel_x > 0) ? editor_ui->layout.info_panel_x : x;
  int info_panel_y = (editor_ui->layout.info_panel_y > 0) ? editor_ui->layout.info_panel_y : y;

  // Create info panel
  editor_ui->info.panel = ui_create_panel(editor_ui->ui_system, info_panel_x, info_panel_y, 250, 120, "Info");
  if (!editor_ui->info.panel)
  {
    printf("[info] Failed to create info panel\n");
    return;
  }
  ui_panel_style_world_editor(editor_ui->info.panel);

  // Create info text elements
  editor_ui->info.world_name_text = ui_create_text(editor_ui->ui_system, info_panel_x + 10, info_panel_y + 10, "World: Test World");
  if (!editor_ui->info.world_name_text)
  {
    printf("[info] Failed to create world name text\n");
  }

  editor_ui->info.world_size_text = ui_create_text(editor_ui->ui_system, info_panel_x + 10, info_panel_y + 30, "Size: 100x100x100");
  if (!editor_ui->info.world_size_text)
  {
    printf("[info] Failed to create world size text\n");
  }

  editor_ui->info.voxel_count_text = ui_create_text(editor_ui->ui_system, info_panel_x + 10, info_panel_y + 50, "Voxels: 1,000,000");
  if (!editor_ui->info.voxel_count_text)
  {
    printf("[info] Failed to create voxel count text\n");
  }

  editor_ui->info.actor_count_text = ui_create_text(editor_ui->ui_system, info_panel_x + 10, info_panel_y + 70, "Actors: 5");
  if (!editor_ui->info.actor_count_text)
  {
    printf("[info] Failed to create actor count text\n");
  }
}

void world_editor_ui_create_settings_panel(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int settings_panel_x = (editor_ui->layout.settings_panel_x > 0) ? editor_ui->layout.settings_panel_x : x;
  int settings_panel_y = (editor_ui->layout.settings_panel_y > 0) ? editor_ui->layout.settings_panel_y : y;

  // Create settings panel
  editor_ui->settings.panel = ui_create_panel(editor_ui->ui_system, settings_panel_x, settings_panel_y, 250, 150, "Settings");
  if (!editor_ui->settings.panel)
  {
    printf("[settings] Failed to create settings panel\n");
    return;
  }
  ui_panel_style_world_editor(editor_ui->settings.panel);

  // Create settings controls
  editor_ui->settings.debug_toggle = ui_create_button(
      editor_ui->ui_system, settings_panel_x + 10, settings_panel_y + 10, 100, 24, "Debug: ON", WORLD_EDITOR_SETTING_DEBUG);
  if (editor_ui->settings.debug_toggle)
  {
    ui_button_style_world_editor(editor_ui->settings.debug_toggle);
  }
  else
  {
    printf("[settings] Failed to create debug toggle button\n");
  }

  editor_ui->settings.grid_toggle = ui_create_button(
      editor_ui->ui_system, settings_panel_x + 120, settings_panel_y + 10, 100, 24, "Grid: ON", WORLD_EDITOR_SETTING_GRID);
  if (editor_ui->settings.grid_toggle)
  {
    ui_button_style_world_editor(editor_ui->settings.grid_toggle);
  }
  else
  {
    printf("[settings] Failed to create grid toggle button\n");
  }

  editor_ui->settings.physics_toggle = ui_create_button(
      editor_ui->ui_system, settings_panel_x + 10, settings_panel_y + 40, 100, 24, "Physics: ON", WORLD_EDITOR_SETTING_PHYSICS);
  if (editor_ui->settings.physics_toggle)
  {
    ui_button_style_world_editor(editor_ui->settings.physics_toggle);
  }
  else
  {
    printf("[settings] Failed to create physics toggle button\n");
  }

  editor_ui->settings.rendering_toggle = ui_create_button(
      editor_ui->ui_system, settings_panel_x + 120, settings_panel_y + 40, 100, 24, "Render: ON", WORLD_EDITOR_SETTING_RENDERING);
  if (editor_ui->settings.rendering_toggle)
  {
    ui_button_style_world_editor(editor_ui->settings.rendering_toggle);
  }
  else
  {
    printf("[settings] Failed to create rendering toggle button\n");
  }
}

void world_editor_ui_create_file_browser_panel(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int file_browser_x = (editor_ui->layout.file_browser_panel_x > 0) ? editor_ui->layout.file_browser_panel_x : x;
  int file_browser_y = (editor_ui->layout.file_browser_panel_y > 0) ? editor_ui->layout.file_browser_panel_y : y;

  // Create file browser panel
  editor_ui->file_browser_panel.panel = ui_create_panel(editor_ui->ui_system, file_browser_x, file_browser_y, 300, 400, "File Browser");
  if (!editor_ui->file_browser_panel.panel)
  {
    printf("[file_browser] Failed to create file browser panel\n");
    return;
  }
  ui_panel_style_world_editor(editor_ui->file_browser_panel.panel);

  // Initially hide the file browser panel
  editor_ui->file_browser_panel.panel->visible = false;

  // Create file browser component
  editor_ui->file_browser_panel.file_browser = ui_create_file_browser(
      editor_ui->ui_system, file_browser_x + 10, file_browser_y + 30, 280, 320);
  if (!editor_ui->file_browser_panel.file_browser)
  {
    printf("[file_browser] Failed to create file browser component\n");
    return;
  }

  // Set default path to worlds directory
  ui_file_browser_set_path(editor_ui->file_browser_panel.file_browser, "worlds/");

  // Set filter for world files
  ui_file_browser_set_filter(editor_ui->file_browser_panel.file_browser, "*.world");

  // Ensure file browser component is also hidden by default
  editor_ui->file_browser_panel.file_browser->visible = false;

  // Create navigation buttons
  editor_ui->file_browser_panel.refresh_button = ui_create_button(
      editor_ui->ui_system, file_browser_x + 10, file_browser_y + 360, 80, 24, "Refresh", 100);
  if (editor_ui->file_browser_panel.refresh_button)
  {
    ui_button_style_world_editor(editor_ui->file_browser_panel.refresh_button);
    editor_ui->file_browser_panel.refresh_button->visible = false;
  }
  else
  {
    printf("[file_browser] Failed to create refresh button\n");
  }

  editor_ui->file_browser_panel.home_button = ui_create_button(
      editor_ui->ui_system, file_browser_x + 100, file_browser_y + 360, 80, 24, "Home", 102);
  if (editor_ui->file_browser_panel.home_button)
  {
    ui_button_style_world_editor(editor_ui->file_browser_panel.home_button);
    editor_ui->file_browser_panel.home_button->visible = false;
  }
  else
  {
    printf("[file_browser] Failed to create home button\n");
  }

  editor_ui->file_browser_panel.close_button = ui_create_button(
      editor_ui->ui_system, file_browser_x + 190, file_browser_y + 360, 80, 24, "Close", 103);
  if (editor_ui->file_browser_panel.close_button)
  {
    ui_button_style_world_editor(editor_ui->file_browser_panel.close_button);
    editor_ui->file_browser_panel.close_button->visible = false;

    // Set callback for close button
    ui_button_set_callback(editor_ui->file_browser_panel.close_button, close_file_browser_callback, editor_ui);
  }
  else
  {
    printf("[file_browser] Failed to create close button\n");
  }
}

// ============================================================================
// SAVE AS MODAL CREATION
// ============================================================================

void world_editor_ui_create_save_as_modal(WorldEditorUI *editor_ui)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Create modal panel (centered on screen)
  int modal_x = 400;
  int modal_y = 300;
  int modal_w = 300;
  int modal_h = 150;

  editor_ui->save_as_modal.panel = ui_create_panel(editor_ui->ui_system, modal_x, modal_y, modal_w, modal_h, "Save As");
  if (!editor_ui->save_as_modal.panel)
  {
    printf("[save_as_modal] Failed to create modal panel\n");
    return;
  }
  ui_panel_style_modal(editor_ui->save_as_modal.panel);
  editor_ui->save_as_modal.panel->visible = false;

  // Create title text
  editor_ui->save_as_modal.title_text = ui_create_text(editor_ui->ui_system, modal_x + 10, modal_y + 10, "Enter filename:");
  if (!editor_ui->save_as_modal.title_text)
  {
    printf("[save_as_modal] Failed to create title text\n");
    return;
  }
  editor_ui->save_as_modal.title_text->visible = false;

  // Create filename input field
  editor_ui->save_as_modal.filename_input = ui_create_input_field(editor_ui->ui_system, modal_x + 10, modal_y + 40, modal_w - 20, 25);
  if (!editor_ui->save_as_modal.filename_input)
  {
    printf("[save_as_modal] Failed to create filename input field\n");
    return;
  }
  editor_ui->save_as_modal.filename_input->visible = false;
  if (editor_ui->save_as_modal.filename_input->text)
    free(editor_ui->save_as_modal.filename_input->text);
  editor_ui->save_as_modal.filename_input->text = strdup("new_world.world");
  editor_ui->save_as_modal.filename_input->text_length = strlen("new_world.world");

  // Create save button
  editor_ui->save_as_modal.save_button = ui_create_button(editor_ui->ui_system, modal_x + 10, modal_y + 80, 80, 25, "Save", 200);
  if (!editor_ui->save_as_modal.save_button)
  {
    printf("[save_as_modal] Failed to create save button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->save_as_modal.save_button);
  editor_ui->save_as_modal.save_button->visible = false;
  ui_button_set_callback(editor_ui->save_as_modal.save_button, save_as_modal_save_callback, editor_ui);

  // Create cancel button
  editor_ui->save_as_modal.cancel_button = ui_create_button(editor_ui->ui_system, modal_x + 110, modal_y + 80, 80, 25, "Cancel", 201);
  if (!editor_ui->save_as_modal.cancel_button)
  {
    printf("[save_as_modal] Failed to create cancel button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->save_as_modal.cancel_button);
  editor_ui->save_as_modal.cancel_button->visible = false;
  ui_button_set_callback(editor_ui->save_as_modal.cancel_button, save_as_modal_cancel_callback, editor_ui);

  // Initialize current filename (will be updated when modal is shown)
  editor_ui->save_as_modal.current_filename = strdup("new_world.world");
  if (!editor_ui->save_as_modal.current_filename)
  {
    printf("[save_as_modal] Failed to allocate current filename\n");
    return;
  }

  printf("[save_as_modal] Save as modal created successfully\n");
}

// ============================================================================
// UNSAVED CHANGES MODAL CREATION
// ============================================================================

void world_editor_ui_create_unsaved_changes_modal(WorldEditorUI *editor_ui)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Create modal panel (centered on screen)
  int modal_x = 350;
  int modal_y = 250;
  int modal_w = 400;
  int modal_h = 200;

  editor_ui->unsaved_changes_modal.panel = ui_create_panel(editor_ui->ui_system, modal_x, modal_y, modal_w, modal_h, "Unsaved Changes");
  if (!editor_ui->unsaved_changes_modal.panel)
  {
    printf("[unsaved_changes_modal] Failed to create modal panel\n");
    return;
  }
  ui_panel_style_modal(editor_ui->unsaved_changes_modal.panel);
  editor_ui->unsaved_changes_modal.panel->visible = false;

  // Create title text
  editor_ui->unsaved_changes_modal.title_text = ui_create_text(editor_ui->ui_system, modal_x + 10, modal_y + 10, "Unsaved Changes Detected");
  if (!editor_ui->unsaved_changes_modal.title_text)
  {
    printf("[unsaved_changes_modal] Failed to create title text\n");
    return;
  }
  editor_ui->unsaved_changes_modal.title_text->visible = false;

  // Create message text
  editor_ui->unsaved_changes_modal.message_text = ui_create_text(editor_ui->ui_system, modal_x + 10, modal_y + 40, "The current world has unsaved changes. What would you like to do?");
  if (!editor_ui->unsaved_changes_modal.message_text)
  {
    printf("[unsaved_changes_modal] Failed to create message text\n");
    return;
  }
  editor_ui->unsaved_changes_modal.message_text->visible = false;

  // Create save button
  editor_ui->unsaved_changes_modal.save_button = ui_create_button(editor_ui->ui_system, modal_x + 10, modal_y + 80, 100, 30, "Save World", 300);
  if (!editor_ui->unsaved_changes_modal.save_button)
  {
    printf("[unsaved_changes_modal] Failed to create save button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->unsaved_changes_modal.save_button);
  editor_ui->unsaved_changes_modal.save_button->visible = false;
  ui_button_set_callback(editor_ui->unsaved_changes_modal.save_button, unsaved_changes_modal_save_callback, editor_ui);

  // Create discard button
  editor_ui->unsaved_changes_modal.discard_button = ui_create_button(editor_ui->ui_system, modal_x + 130, modal_y + 80, 100, 30, "Discard Changes", 301);
  if (!editor_ui->unsaved_changes_modal.discard_button)
  {
    printf("[unsaved_changes_modal] Failed to create discard button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->unsaved_changes_modal.discard_button);
  editor_ui->unsaved_changes_modal.discard_button->visible = false;
  ui_button_set_callback(editor_ui->unsaved_changes_modal.discard_button, unsaved_changes_modal_discard_callback, editor_ui);

  // Create cancel button
  editor_ui->unsaved_changes_modal.cancel_button = ui_create_button(editor_ui->ui_system, modal_x + 250, modal_y + 80, 100, 30, "Cancel", 302);
  if (!editor_ui->unsaved_changes_modal.cancel_button)
  {
    printf("[unsaved_changes_modal] Failed to create cancel button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->unsaved_changes_modal.cancel_button);
  editor_ui->unsaved_changes_modal.cancel_button->visible = false;
  ui_button_set_callback(editor_ui->unsaved_changes_modal.cancel_button, unsaved_changes_modal_cancel_callback, editor_ui);

  printf("[unsaved_changes_modal] Unsaved changes modal created successfully\n");
}

// ============================================================================
// FILE OPERATION TRIGGERING
// ============================================================================

void world_editor_ui_trigger_file_operation(WorldEditorUI *editor_ui, int operation_id)
{
  if (!editor_ui || !editor_ui->file_callback)
    return;

  // Call the file operation callback
  editor_ui->file_callback(operation_id, NULL, editor_ui->file_user_data);
}

// ============================================================================
// PROGRESS BAR MANAGEMENT
// ============================================================================

void world_editor_ui_create_progress_bar(WorldEditorUI *editor_ui)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Create progress bar centered on screen
  int window_width = 1024;  // Default window width
  int window_height = 768;  // Default window height
  int progress_width = 400;
  int progress_height = 40;
  int progress_x = (window_width - progress_width) / 2;
  int progress_y = (window_height - progress_height) / 2;

  editor_ui->world_generation_progress = ui_create_progress_bar(
    editor_ui->ui_system, progress_x, progress_y, progress_width, progress_height);

  if (editor_ui->world_generation_progress)
  {
    // Set high Z-order to appear above everything
    ui_progress_bar_set_z_order(editor_ui->world_generation_progress, UI_Z_MODAL + 100);

    // Initially hidden
    ui_progress_bar_set_visible(editor_ui->world_generation_progress, false);

    printf("[progress_bar] World generation progress bar created\n");
  }
  else
  {
    printf("[progress_bar] ERROR: Failed to create world generation progress bar\n");
  }
}

void world_editor_ui_show_progress_bar(WorldEditorUI *editor_ui, const char *message)
{
  if (!editor_ui || !editor_ui->world_generation_progress)
    return;

  ui_progress_bar_set_progress(editor_ui->world_generation_progress, 0.0f);
  ui_progress_bar_set_message(editor_ui->world_generation_progress, message);
  ui_progress_bar_set_visible(editor_ui->world_generation_progress, true);

  printf("[progress_bar] Showing progress bar: %s\n", message ? message : "No message");
}

void world_editor_ui_update_progress_bar(WorldEditorUI *editor_ui, float progress, const char *message)
{
  if (!editor_ui || !editor_ui->world_generation_progress)
    return;

  ui_progress_bar_set_progress(editor_ui->world_generation_progress, progress);
  if (message)
  {
    ui_progress_bar_set_message(editor_ui->world_generation_progress, message);
  }

  printf("[progress_bar] Updated progress: %.1f%% - %s\n",
         progress * 100.0f, message ? message : "No message");
}

void world_editor_ui_hide_progress_bar(WorldEditorUI *editor_ui)
{
  if (!editor_ui || !editor_ui->world_generation_progress)
    return;

  ui_progress_bar_set_visible(editor_ui->world_generation_progress, false);
  printf("[progress_bar] Hiding progress bar\n");
}

// ============================================================================
// FILE PANEL CREATION
// ============================================================================

void world_editor_ui_create_file_panel(WorldEditorUI *editor_ui, int x, int y)
{
  if (!editor_ui || !editor_ui->ui_system)
  {
    printf("[file_panel] ERROR: Invalid editor_ui or ui_system\n");
    return;
  }

  // Use calculated layout positions if available, otherwise fall back to provided coordinates
  int file_panel_x = (editor_ui->layout.file_panel_x > 0) ? editor_ui->layout.file_panel_x : x;
  int file_panel_y = (editor_ui->layout.file_panel_y > 0) ? editor_ui->layout.file_panel_y : y;

  printf("[file_panel] Creating file panel at position (%d, %d)\n", file_panel_x, file_panel_y);

  // Create File menu button (toggles dropdown)
  editor_ui->file_panel.file_button = ui_create_button(
      editor_ui->ui_system, file_panel_x, file_panel_y, 60, 20, "File", WORLD_EDITOR_FILE_BUTTON);
  if (!editor_ui->file_panel.file_button)
  {
    printf("[file_panel] ERROR: Failed to create file button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.file_button);
  ui_button_set_z_order(editor_ui->file_panel.file_button, UI_Z_DROPDOWN); // High priority
  ui_button_set_callback(editor_ui->file_panel.file_button, file_button_click_callback, editor_ui);

  // Create Edit menu button (toggles dropdown)
  editor_ui->file_panel.edit_button = ui_create_button(
      editor_ui->ui_system, file_panel_x + 80, file_panel_y, 60, 20, "Edit", WORLD_EDITOR_EDIT_BUTTON);
  if (!editor_ui->file_panel.edit_button)
  {
    printf("[file_panel] ERROR: Failed to create edit button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.edit_button);
  ui_button_set_z_order(editor_ui->file_panel.edit_button, UI_Z_DROPDOWN); // High priority

  // Create file menu buttons (initially hidden)
  editor_ui->file_panel.new_world_button = ui_create_button(
      editor_ui->ui_system, file_panel_x, file_panel_y + 25, 220, 20, "New World", WORLD_EDITOR_FILE_NEW);
  if (!editor_ui->file_panel.new_world_button)
  {
    printf("[file_panel] ERROR: Failed to create new world button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.new_world_button);
  ui_button_set_z_order(editor_ui->file_panel.new_world_button, UI_Z_DROPDOWN); // High priority
  editor_ui->file_panel.new_world_button->visible = false;
  ui_button_set_callback(editor_ui->file_panel.new_world_button, new_world_button_callback, editor_ui);

  editor_ui->file_panel.open_world_button = ui_create_button(
      editor_ui->ui_system, file_panel_x, file_panel_y + 45, 220, 20, "Open World", WORLD_EDITOR_FILE_OPEN);
  if (!editor_ui->file_panel.open_world_button)
  {
    printf("[file_panel] ERROR: Failed to create open world button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.open_world_button);
  ui_button_set_z_order(editor_ui->file_panel.open_world_button, UI_Z_DROPDOWN); // High priority
  editor_ui->file_panel.open_world_button->visible = false;
  ui_button_set_callback(editor_ui->file_panel.open_world_button, open_world_button_callback, editor_ui);

  editor_ui->file_panel.save_world_button = ui_create_button(
      editor_ui->ui_system, file_panel_x, file_panel_y + 65, 220, 20, "Save World", WORLD_EDITOR_FILE_SAVE);
  if (!editor_ui->file_panel.save_world_button)
  {
    printf("[file_panel] ERROR: Failed to create save world button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.save_world_button);
  ui_button_set_z_order(editor_ui->file_panel.save_world_button, UI_Z_DROPDOWN); // High priority
  editor_ui->file_panel.save_world_button->visible = false;

  editor_ui->file_panel.save_as_button = ui_create_button(
      editor_ui->ui_system, file_panel_x, file_panel_y + 85, 220, 20, "Save As...", WORLD_EDITOR_FILE_SAVE_AS);
  if (!editor_ui->file_panel.save_as_button)
  {
    printf("[file_panel] ERROR: Failed to create save as button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.save_as_button);
  ui_button_set_z_order(editor_ui->file_panel.save_as_button, UI_Z_DROPDOWN); // High priority
  editor_ui->file_panel.save_as_button->visible = false;
  ui_button_set_callback(editor_ui->file_panel.save_as_button, save_as_button_callback, editor_ui);

  editor_ui->file_panel.export_button = ui_create_button(
      editor_ui->ui_system, file_panel_x, file_panel_y + 105, 220, 20, "Export...", WORLD_EDITOR_FILE_EXPORT);
  if (!editor_ui->file_panel.export_button)
  {
    printf("[file_panel] ERROR: Failed to create export button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.export_button);
  ui_button_set_z_order(editor_ui->file_panel.export_button, UI_Z_DROPDOWN); // High priority
  editor_ui->file_panel.export_button->visible = false;

  editor_ui->file_panel.import_button = ui_create_button(
      editor_ui->ui_system, file_panel_x, file_panel_y + 125, 220, 20, "Import...", WORLD_EDITOR_FILE_IMPORT);
  if (!editor_ui->file_panel.import_button)
  {
    printf("[file_panel] ERROR: Failed to create import button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.import_button);
  ui_button_set_z_order(editor_ui->file_panel.import_button, UI_Z_DROPDOWN); // High priority
  editor_ui->file_panel.import_button->visible = false;

  editor_ui->file_panel.exit_button = ui_create_button(
      editor_ui->ui_system, file_panel_x, file_panel_y + 145, 220, 20, "Exit", WORLD_EDITOR_FILE_EXIT);
  if (!editor_ui->file_panel.exit_button)
  {
    printf("[file_panel] ERROR: Failed to create exit button\n");
    return;
  }
  ui_button_style_world_editor(editor_ui->file_panel.exit_button);
  ui_button_set_z_order(editor_ui->file_panel.exit_button, UI_Z_DROPDOWN); // High priority
  editor_ui->file_panel.exit_button->visible = false;
  ui_button_set_callback(editor_ui->file_panel.exit_button, exit_button_callback, editor_ui);

  printf("[file_panel] File panel created successfully with all buttons\n");
}

// ============================================================================
// FILE PANEL
// ============================================================================

// Helper function to close the file menu
static void close_file_menu(WorldEditorUI *editor_ui)
{
  if (!editor_ui)
    return;

  printf("[file_panel] Closing file menu\n");

  editor_ui->file_panel.new_world_button->visible = false;
  editor_ui->file_panel.open_world_button->visible = false;
  editor_ui->file_panel.save_world_button->visible = false;
  editor_ui->file_panel.save_as_button->visible = false;
  editor_ui->file_panel.export_button->visible = false;
  editor_ui->file_panel.import_button->visible = false;
  editor_ui->file_panel.exit_button->visible = false;
}

static void file_button_click_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui)
    return;

  printf("[file_panel] File button clicked - toggling menu\n");

  // Check if all required buttons exist before toggling
  if (!editor_ui->file_panel.new_world_button ||
      !editor_ui->file_panel.open_world_button ||
      !editor_ui->file_panel.save_world_button ||
      !editor_ui->file_panel.save_as_button ||
      !editor_ui->file_panel.export_button ||
      !editor_ui->file_panel.import_button ||
      !editor_ui->file_panel.exit_button)
  {
    printf("[file_panel] ERROR: One or more file menu buttons are null!\n");
    return;
  }

  // Toggle file menu visibility
  bool show_menu = !editor_ui->file_panel.new_world_button->visible;
  printf("[file_panel] Setting menu visibility to: %s\n", show_menu ? "visible" : "hidden");

  editor_ui->file_panel.new_world_button->visible = show_menu;
  editor_ui->file_panel.open_world_button->visible = show_menu;
  editor_ui->file_panel.save_world_button->visible = show_menu;
  editor_ui->file_panel.save_as_button->visible = show_menu;
  editor_ui->file_panel.export_button->visible = show_menu;
  editor_ui->file_panel.import_button->visible = show_menu;
  editor_ui->file_panel.exit_button->visible = show_menu;
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void world_editor_ui_update_world_info(WorldEditorUI *editor_ui, World *world)
{
#if ENABLE_NEW_UI_PANELS
  // TODO: Implement world info update
  (void)editor_ui;
  (void)world;
#else
  // When new UI panels are disabled, these functions do nothing
  (void)editor_ui;
  (void)world;
#endif
}

void world_editor_ui_update_actor_list(WorldEditorUI *editor_ui, struct Actor *actors, int actor_count)
{
#if ENABLE_NEW_UI_PANELS
  // TODO: Implement actor list update
  (void)editor_ui;
  (void)actors;
  (void)actor_count;
#else
  // When new UI panels are disabled, these functions do nothing
  (void)editor_ui;
  (void)actors;
  (void)actor_count;
#endif
}

void world_editor_ui_update_recent_files(WorldEditorUI *editor_ui, const char **files, int file_count)
{
  // TODO: Implement recent files update
  (void)editor_ui;
  (void)files;
  (void)file_count;
}

void world_editor_ui_set_selected_tool(WorldEditorUI *editor_ui, int tool_id)
{
  if (!editor_ui)
    return;

  // Deselect all tool buttons first
  if (editor_ui->toolbar.cursor_button)
    editor_ui->toolbar.cursor_button->selected = false;
  if (editor_ui->toolbar.shape_button)
    editor_ui->toolbar.shape_button->selected = false;
  if (editor_ui->toolbar.cube_button)
    editor_ui->toolbar.cube_button->selected = false;
  if (editor_ui->toolbar.sphere_button)
    editor_ui->toolbar.sphere_button->selected = false;
  if (editor_ui->toolbar.adjacent_button)
    editor_ui->toolbar.adjacent_button->selected = false;

  // Select the appropriate tool button
  switch (tool_id)
  {
  case WORLD_EDITOR_TOOL_CURSOR:
    if (editor_ui->toolbar.cursor_button)
      editor_ui->toolbar.cursor_button->selected = true;
    break;
  case WORLD_EDITOR_TOOL_SHAPE:
    if (editor_ui->toolbar.shape_button)
      editor_ui->toolbar.shape_button->selected = true;
    break;
  case WORLD_EDITOR_TOOL_CUBE:
    if (editor_ui->toolbar.cube_button)
      editor_ui->toolbar.cube_button->selected = true;
    break;
  case WORLD_EDITOR_TOOL_SPHERE:
    if (editor_ui->toolbar.sphere_button)
      editor_ui->toolbar.sphere_button->selected = true;
    break;
  case WORLD_EDITOR_TOOL_ADJACENT:
    if (editor_ui->toolbar.adjacent_button)
      editor_ui->toolbar.adjacent_button->selected = true;
    break;
  }
}

void world_editor_ui_set_selected_voxel(WorldEditorUI *editor_ui, int voxel_type)
{
  if (!editor_ui)
    return;

  // TODO: Update palette selection

  // Call the callback if set
  if (editor_ui->voxel_callback)
  {
    editor_ui->voxel_callback(voxel_type, editor_ui->voxel_user_data);
  }
}

// ============================================================================
// CALLBACK SETUP
// ============================================================================

void world_editor_ui_set_tool_callback(WorldEditorUI *editor_ui,
                                       void (*callback)(int tool_id, void *user_data), void *user_data)
{
  if (!editor_ui)
    return;
  editor_ui->tool_callback = callback;
  editor_ui->tool_user_data = user_data;

  // Update toolbar button callbacks if toolbar exists
  if (editor_ui->toolbar.cursor_button && callback)
  {
    ui_button_set_callback(editor_ui->toolbar.cursor_button, callback, editor_ui);
    ui_button_set_callback(editor_ui->toolbar.shape_button, callback, editor_ui);
    ui_button_set_callback(editor_ui->toolbar.cube_button, callback, editor_ui);
    ui_button_set_callback(editor_ui->toolbar.sphere_button, callback, editor_ui);
    ui_button_set_callback(editor_ui->toolbar.adjacent_button, callback, editor_ui);
  }
}

void world_editor_ui_set_voxel_callback(WorldEditorUI *editor_ui, WorldEditorVoxelCallback callback, void *user_data)
{
  if (editor_ui)
  {
    editor_ui->voxel_callback = callback;
    editor_ui->voxel_user_data = user_data;
  }
}

void world_editor_ui_set_camera_callback(WorldEditorUI *editor_ui, void (*callback)(int dx, int dy, int dz, void *user_data), void *user_data)
{
  if (editor_ui)
  {
    editor_ui->on_camera_move = callback;
    editor_ui->camera_user_data = user_data;
  }
}

void world_editor_ui_set_actor_move_callback(WorldEditorUI *editor_ui, WorldEditorActorMoveCallback callback, void *user_data)
{
  if (editor_ui)
  {
    editor_ui->actor_move_callback = callback;
    editor_ui->actor_move_user_data = user_data;
  }
}

void world_editor_ui_set_shape_callback(WorldEditorUI *editor_ui, WorldEditorShapeCallback callback, void *user_data)
{
  if (editor_ui)
  {
    editor_ui->shape_callback = callback;
    editor_ui->shape_user_data = user_data;
  }
}

void world_editor_ui_set_adjacent_callback(WorldEditorUI *editor_ui, WorldEditorAdjacentCallback callback, void *user_data)
{
  if (editor_ui)
  {
    editor_ui->adjacent_callback = callback;
    editor_ui->adjacent_user_data = user_data;
  }
}

void world_editor_ui_set_file_callback(WorldEditorUI *editor_ui,
                                       void (*callback)(int operation_id, const char *filename, void *user_data), void *user_data)
{
  if (!editor_ui)
    return;
  editor_ui->file_callback = callback;
  editor_ui->file_user_data = user_data;
}

void world_editor_ui_set_actor_callback(WorldEditorUI *editor_ui,
                                        void (*callback)(int operation_id, int actor_id, void *user_data), void *user_data)
{
  if (!editor_ui)
    return;
  // This callback is not directly managed by WorldEditorUI struct members
  // as it's a placeholder for a more complex actor management system.
  // For now, we'll just ignore it.
  (void)callback;
  (void)user_data;
}

void world_editor_ui_set_setting_callback(WorldEditorUI *editor_ui,
                                          void (*callback)(int setting_id, const char *value, void *user_data), void *user_data)
{
  if (!editor_ui)
    return;
  // This callback is not directly managed by WorldEditorUI struct members
  // as it's a placeholder for a more complex settings system.
  // For now, we'll just ignore it.
  (void)callback;
  (void)user_data;
}

// ============================================================================
// ACTOR MOVEMENT CALLBACKS
// ============================================================================

static void actor_move_up_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui || !editor_ui->actor_move_callback)
    return;

  printf("[actor] Move up button clicked\n");
  editor_ui->actor_move_callback(0, 0, 1, editor_ui->actor_move_user_data);
}

static void actor_move_down_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui || !editor_ui->actor_move_callback)
    return;

  printf("[actor] Move down button clicked\n");
  editor_ui->actor_move_callback(0, 0, -1, editor_ui->actor_move_user_data);
}

static void actor_move_left_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui || !editor_ui->actor_move_callback)
    return;

  printf("[actor] Move left button clicked\n");
  editor_ui->actor_move_callback(-1, 0, 0, editor_ui->actor_move_user_data);
}

static void actor_move_right_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui || !editor_ui->actor_move_callback)
    return;

  printf("[actor] Move right button clicked\n");
  editor_ui->actor_move_callback(1, 0, 0, editor_ui->actor_move_user_data);
}

static void actor_move_forward_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui || !editor_ui->actor_move_callback)
    return;

  printf("[actor] Move forward button clicked\n");
  editor_ui->actor_move_callback(0, -1, 0, editor_ui->actor_move_user_data);
}

static void actor_move_backward_callback(int button_id, void *user_data)
{
  WorldEditorUI *editor_ui = (WorldEditorUI *)user_data;
  if (!editor_ui || !editor_ui->actor_move_callback)
    return;

  printf("[actor] Move backward button clicked\n");
  editor_ui->actor_move_callback(0, 1, 0, editor_ui->actor_move_user_data);
}

// ============================================================================
// NEW WORLD MODAL CREATION
// ============================================================================

void world_editor_ui_create_new_world_modal(WorldEditorUI *editor_ui)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Create modal panel (centered on screen)
  int modal_x = 300;
  int modal_y = 200;
  int modal_w = 400;
  int modal_h = 300;

  editor_ui->new_world_modal.panel = ui_create_panel(editor_ui->ui_system, modal_x, modal_y, modal_w, modal_h, "New World");
  if (!editor_ui->new_world_modal.panel)
    return;

  ui_panel_style_modal(editor_ui->new_world_modal.panel);
  editor_ui->new_world_modal.panel->visible = false;

  // Create title text
  editor_ui->new_world_modal.title_text = ui_create_text(editor_ui->ui_system, modal_x + 20, modal_y + 20, "Create New World");
  if (editor_ui->new_world_modal.title_text)
  {
    editor_ui->new_world_modal.title_text->color = (SDL_Color){255, 255, 255, 255};
    editor_ui->new_world_modal.title_text->visible = false;
  }

  // Create world type label
  editor_ui->new_world_modal.world_type_label = ui_create_text(editor_ui->ui_system, modal_x + 20, modal_y + 70, "World Type:");
  if (editor_ui->new_world_modal.world_type_label)
  {
    editor_ui->new_world_modal.world_type_label->color = (SDL_Color){200, 200, 200, 255};
    editor_ui->new_world_modal.world_type_label->visible = false;
  }

  // Create world type dropdown
  editor_ui->new_world_modal.world_type_dropdown = ui_create_dropdown(editor_ui->ui_system, modal_x + 20, modal_y + 95, 200, 25);
  if (editor_ui->new_world_modal.world_type_dropdown)
  {
    editor_ui->new_world_modal.world_type_dropdown->visible = false;
    ui_dropdown_style_world_editor(editor_ui->new_world_modal.world_type_dropdown);

    // Add world type options
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "HOME");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "FARM");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "RANDOM");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "WILDERNESS");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "SOLID");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "UNDERWORLD");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "SCOURED");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "LABYRINTH");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "WFC_TOWN");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "CLOUD");
    ui_dropdown_add_option(editor_ui->new_world_modal.world_type_dropdown, "ARENA");

    // Set default selection to HOME
    editor_ui->new_world_modal.selected_world_type = 0;
  }

  // Create seed label
  editor_ui->new_world_modal.seed_label = ui_create_text(editor_ui->ui_system, modal_x + 20, modal_y + 140, "Seed (optional):");
  if (editor_ui->new_world_modal.seed_label)
  {
    editor_ui->new_world_modal.seed_label->color = (SDL_Color){200, 200, 200, 255};
    editor_ui->new_world_modal.seed_label->visible = false;
  }

  // Create seed input field
  editor_ui->new_world_modal.seed_input = ui_create_input_field(editor_ui->ui_system, modal_x + 20, modal_y + 165, 200, 25);
  if (editor_ui->new_world_modal.seed_input)
  {
    editor_ui->new_world_modal.seed_input->visible = false;
    ui_input_field_set_placeholder(editor_ui->new_world_modal.seed_input, "Enter seed or leave empty for random");
  }

  // Create generate neighbors checkbox
  editor_ui->new_world_modal.generate_neighbors_checkbox = ui_create_button(editor_ui->ui_system, modal_x + 20, modal_y + 200, 20, 20, "", 1003);
  if (editor_ui->new_world_modal.generate_neighbors_checkbox)
  {
    editor_ui->new_world_modal.generate_neighbors_checkbox->visible = false;
    ui_button_style_world_editor(editor_ui->new_world_modal.generate_neighbors_checkbox);
    editor_ui->new_world_modal.generate_neighbors_checkbox->normal_color = (SDL_Color){60, 60, 60, 255};
    editor_ui->new_world_modal.generate_neighbors_checkbox->selected_color = (SDL_Color){0, 200, 0, 255};
    editor_ui->new_world_modal.generate_neighbors_checkbox->text_color = (SDL_Color){255, 255, 255, 255}; // White text
    editor_ui->new_world_modal.generate_neighbors_checkbox->selected = false;

    // Ensure text is empty initially (unchecked state)
    *((char**)&editor_ui->new_world_modal.generate_neighbors_checkbox->text) = "";
  }

  // Create generate neighbors label
  editor_ui->new_world_modal.generate_neighbors_label = ui_create_text(editor_ui->ui_system, modal_x + 50, modal_y + 205, "Generate neighboring worlds (3x3x3 grid)");
  if (editor_ui->new_world_modal.generate_neighbors_label)
  {
    editor_ui->new_world_modal.generate_neighbors_label->color = (SDL_Color){200, 200, 200, 255};
    editor_ui->new_world_modal.generate_neighbors_label->visible = false;
  }

  // Create generate button (positioned relative to panel)
  editor_ui->new_world_modal.generate_button = ui_create_button(editor_ui->ui_system, modal_x + 20, modal_y + 240, 100, 30, "Generate", 1001);
  if (editor_ui->new_world_modal.generate_button)
  {
    editor_ui->new_world_modal.generate_button->visible = false;
    ui_button_style_world_editor(editor_ui->new_world_modal.generate_button);
    editor_ui->new_world_modal.generate_button->normal_color = (SDL_Color){0, 120, 0, 255};
    editor_ui->new_world_modal.generate_button->hover_color = (SDL_Color){0, 150, 0, 255};
    editor_ui->new_world_modal.generate_button->selected_color = (SDL_Color){0, 100, 0, 255};
  }

  // Create cancel button (positioned relative to panel)
  editor_ui->new_world_modal.cancel_button = ui_create_button(editor_ui->ui_system, modal_x + 140, modal_y + 240, 100, 30, "Cancel", 1002);
  if (editor_ui->new_world_modal.cancel_button)
  {
    editor_ui->new_world_modal.cancel_button->visible = false;
    ui_button_style_world_editor(editor_ui->new_world_modal.cancel_button);
    editor_ui->new_world_modal.cancel_button->normal_color = (SDL_Color){120, 0, 0, 255};
    editor_ui->new_world_modal.cancel_button->hover_color = (SDL_Color){150, 0, 0, 255};
    editor_ui->new_world_modal.cancel_button->selected_color = (SDL_Color){100, 0, 0, 255};
  }

  // Initialize current seed and neighbors flag
  editor_ui->new_world_modal.current_seed = NULL;
  editor_ui->new_world_modal.generate_neighbors = false; // Default to unchecked

  // Set button callbacks
  ui_button_set_callback(editor_ui->new_world_modal.generate_button, new_world_modal_generate_callback, editor_ui);
  ui_button_set_callback(editor_ui->new_world_modal.cancel_button, new_world_modal_cancel_callback, editor_ui);
  ui_button_set_callback(editor_ui->new_world_modal.generate_neighbors_checkbox, new_world_modal_neighbors_checkbox_callback, editor_ui);

  // Set dropdown callback to track selection
  ui_dropdown_set_callback(editor_ui->new_world_modal.world_type_dropdown, new_world_modal_dropdown_callback, editor_ui);

  // Add buttons to the panel
  ui_panel_add_button(editor_ui->new_world_modal.panel, editor_ui->new_world_modal.generate_button);
  ui_panel_add_button(editor_ui->new_world_modal.panel, editor_ui->new_world_modal.cancel_button);
  ui_panel_add_button(editor_ui->new_world_modal.panel, editor_ui->new_world_modal.generate_neighbors_checkbox);

  printf("[new_world_modal] New world modal created successfully\n");
}

// ============================================================================
// RENDERING
// ============================================================================

void world_editor_ui_render(WorldEditorUI *editor_ui)
{
  if (!editor_ui || !editor_ui->ui_system)
    return;

  // Render all UI elements using the core UI system
  ui_system_render(editor_ui->ui_system);
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

bool world_editor_ui_handle_event(WorldEditorUI *editor_ui, SDL_Event *event)
{
  if (!editor_ui || !editor_ui->ui_system)
    return false;

  // Handle arrow keys for camera movement when no UI element is focused
  if (event->type == SDL_KEYDOWN)
  {
    SDL_Keycode key = event->key.keysym.sym;
    if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_LEFT || key == SDLK_RIGHT)
    {
      // Check if any UI element is focused
      bool ui_focused = false;
      if (editor_ui->ui_system->focused_input || editor_ui->ui_system->focused_dropdown)
      {
        ui_focused = true;
      }

      // If no UI element is focused, handle camera movement
      if (!ui_focused)
      {
        int dx = 0, dy = 0;
        if (key == SDLK_LEFT)
        {
          dx = -1; // Left = west
        }
        else if (key == SDLK_RIGHT)
        {
          dx = +1; // Right = east
        }
        else if (key == SDLK_UP)
        {
          dy = +1; // Up = forward (north)
        }
        else if (key == SDLK_DOWN)
        {
          dy = -1; // Down = backward (south)
        }

        // Move camera directly
        extern void *g_isometric_renderer; // Forward declare global renderer
        typedef struct {
          int camera_x, camera_y, camera_z;
          // ... other fields exist but we only need these
        } IsometricRenderer;

        IsometricRenderer *renderer = (IsometricRenderer*)g_isometric_renderer;
        if (renderer)
        {
          renderer->camera_x += dx;
          renderer->camera_y += dy;
          printf("[debug] Camera moved to: (%d, %d)\n", renderer->camera_x, renderer->camera_y);
        }

        return true; // Event handled
      }
    }
  }

  // Let the core UI system handle the event
  return ui_system_handle_event(editor_ui->ui_system, event);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char *world_editor_ui_get_voxel_name(int voxel_type)
{
  // TODO: Implement voxel name lookup
  (void)voxel_type;
  return "Unknown";
}

SDL_Color world_editor_ui_get_voxel_color(int voxel_type)
{
  // TODO: Implement voxel color lookup
  (void)voxel_type;
  return (SDL_Color){128, 128, 128, 255};
}

UIButton *world_editor_ui_create_voxel_button(WorldEditorUI *editor_ui, int x, int y,
                                              int voxel_type, int button_id)
{
  if (!editor_ui || !editor_ui->ui_system)
    return NULL;

  // Create a button with voxel-specific styling
  UIButton *button = ui_create_button(editor_ui->ui_system, x, y, 30, 30, "", button_id);
  if (button)
  {
    // Apply voxel-specific styling
    button->normal_color = world_editor_ui_get_voxel_color(voxel_type);
    button->hover_color = (SDL_Color){
        (Uint8)(button->normal_color.r * 1.2),
        (Uint8)(button->normal_color.g * 1.2),
        (Uint8)(button->normal_color.b * 1.2),
        255};
    button->selected_color = (SDL_Color){
        (Uint8)(button->normal_color.r * 0.8),
        (Uint8)(button->normal_color.g * 0.8),
        (Uint8)(button->normal_color.b * 0.8),
        255};
  }

  return button;
}
