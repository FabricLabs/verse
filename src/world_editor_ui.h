#ifndef WORLD_EDITOR_UI_H
#define WORLD_EDITOR_UI_H

#include "ui_system.h"
#include "world.h"
#include "actor.h"
#include <stdbool.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// Forward declarations to avoid circular includes
struct Actor;

// ============================================================================
// CONSTANTS
// ============================================================================

// Tool constants
#define WORLD_EDITOR_TOOL_CURSOR 10
#define WORLD_EDITOR_TOOL_SHAPE 11
#define WORLD_EDITOR_TOOL_CUBE 12
#define WORLD_EDITOR_TOOL_SPHERE 13
#define WORLD_EDITOR_TOOL_ADJACENT 14

// Actor constants
#define WORLD_EDITOR_ACTOR_ADD 20
#define WORLD_EDITOR_ACTOR_REMOVE 21
#define WORLD_EDITOR_ACTOR_EDIT 22

// Setting constants
#define WORLD_EDITOR_SETTING_DEBUG 30
#define WORLD_EDITOR_SETTING_GRID 31
#define WORLD_EDITOR_SETTING_PHYSICS 32
#define WORLD_EDITOR_SETTING_RENDERING 33

// Callback types
typedef void (*WorldEditorFileCallback)(int operation_id, const char *filename, void *user_data);
typedef void (*WorldEditorToolCallback)(int tool_id, void *user_data);
typedef void (*WorldEditorShapeCallback)(int mode_id, void *user_data);
typedef void (*WorldEditorAdjacentCallback)(int direction_id, void *user_data);
typedef void (*WorldEditorVoxelCallback)(int voxel_id, void *user_data);
typedef void (*WorldEditorActorMoveCallback)(int dx, int dy, int dz, void *user_data);

// File operation constants
#define WORLD_EDITOR_FILE_NEW 0
#define WORLD_EDITOR_FILE_OPEN 1
#define WORLD_EDITOR_FILE_SAVE 2
#define WORLD_EDITOR_FILE_SAVE_AS 3
#define WORLD_EDITOR_FILE_EXPORT 4
#define WORLD_EDITOR_FILE_IMPORT 5
#define WORLD_EDITOR_FILE_RECENT 6
#define WORLD_EDITOR_FILE_EXIT 7
#define WORLD_EDITOR_FILE_BUTTON 8
#define WORLD_EDITOR_EDIT_BUTTON 9

// ============================================================================
// INTERNAL CONSTANTS
// ============================================================================

// Layout constants for consistent spacing and positioning
#define WORLD_EDITOR_MARGIN 10
#define WORLD_EDITOR_PANEL_SPACING 8
#define WORLD_EDITOR_LEFT_PANEL_WIDTH 250
#define WORLD_EDITOR_RIGHT_PANEL_WIDTH 280
#define WORLD_EDITOR_TOOLBAR_HEIGHT 40
#define WORLD_EDITOR_PALETTE_HEIGHT 320
#define WORLD_EDITOR_INFO_PANEL_HEIGHT 140
#define WORLD_EDITOR_ACTOR_PANEL_HEIGHT 170
#define WORLD_EDITOR_SETTINGS_PANEL_HEIGHT 170

// Tool button dimensions
#define TOOL_BUTTON_WIDTH 70
#define TOOL_BUTTON_HEIGHT 24
#define TOOL_BUTTON_SPACING 8
#define SHAPE_SUB_BUTTON_WIDTH 70
#define ADJACENT_BUTTON_WIDTH 46

// Feature flags
// Set ENABLE_NEW_UI_PANELS to 0 to disable all new UI panels except File Menu and Toolbar
// Set to 1 to enable all new UI panels (palette, recent, z-swatches, info, actor, settings)
#define ENABLE_NEW_UI_PANELS 0

// ============================================================================
// WORLD EDITOR UI COMPONENTS
// ============================================================================

// Toolbar for editing tools
typedef struct
{
  UIPanel *panel;
  UIButton *file_button;
  UIButton *edit_button;
  UIButton *new_world_button;
  UIButton *open_world_button;
  UIButton *save_world_button;
  UIButton *save_as_button;
  UIButton *export_button;
  UIButton *import_button;
  UIButton *recent_files_button;
  UIButton *exit_button;
} WorldEditorFilePanel;

typedef struct
{
  UIButton *cursor_button;
  UIButton *shape_button;
  UIButton *cube_button;
  UIButton *sphere_button;
  UIButton *adjacent_button;
} WorldEditorToolbar;

typedef struct
{
  UIPanel *panel;
  UIButton *voxel_buttons[8];
} WorldEditorPalette;

typedef struct
{
  UIPanel *panel;
  UIButton *swatch_buttons[5];
} WorldEditorRecentPanel;

typedef struct
{
  UIPanel *panel;
  UIButton *swatch_buttons[10];
} WorldEditorZSwatches;

typedef struct
{
  UIPanel *panel;
  UIButton *add_button;
  UIButton *remove_button;
  UIButton *edit_button;

  // Actor movement controls
  UIButton *move_up_button;
  UIButton *move_down_button;
  UIButton *move_left_button;
  UIButton *move_right_button;
  UIButton *move_forward_button;
  UIButton *move_backward_button;
} WorldEditorActorPanel;

typedef struct
{
  UIPanel *panel;
  UIText *world_name_text;
  UIText *world_size_text;
  UIText *voxel_count_text;
  UIText *actor_count_text;
} WorldEditorInfoPanel;

typedef struct
{
  UIPanel *panel;
  UIButton *debug_toggle;
  UIButton *grid_toggle;
  UIButton *physics_toggle;
  UIButton *rendering_toggle;
} WorldEditorSettingsPanel;

// File browser panel for browsing and selecting world files
typedef struct
{
  UIPanel *panel;
  UIFileBrowser *file_browser;
  UIButton *refresh_button;
  UIButton *up_button;
  UIButton *home_button;
  UIButton *close_button;
} WorldEditorFileBrowserPanel;

// Save As modal dialog
typedef struct
{
  UIPanel *panel;
  UIText *title_text;
  UIInputField *filename_input;
  UIButton *save_button;
  UIButton *cancel_button;
  char *current_filename;
} WorldEditorSaveAsModal;

// Unsaved changes confirmation modal
typedef struct
{
  UIPanel *panel;
  UIText *title_text;
  UIText *message_text;
  UIButton *save_button;
  UIButton *discard_button;
  UIButton *cancel_button;
} WorldEditorUnsavedChangesModal;

// World configuration modal for new world generation
typedef struct
{
  UIPanel *panel;
  UIText *title_text;
  UIText *world_type_label;
  UIDropdown *world_type_dropdown;
  UIText *seed_label;
  UIInputField *seed_input;
  UIButton *generate_neighbors_checkbox;
  UIText *generate_neighbors_label;
  UIButton *generate_button;
  UIButton *cancel_button;
  char *current_seed;
  int selected_world_type;
  bool generate_neighbors;
} WorldEditorNewWorldModal;

// Main world editor UI system
typedef struct
{
  UISystem *ui_system;
  WorldEditorFilePanel file_panel;
  WorldEditorToolbar toolbar;
  WorldEditorPalette palette;
  WorldEditorRecentPanel recent;
  WorldEditorZSwatches z_swatches;
  WorldEditorActorPanel actor;
  WorldEditorInfoPanel info;
  WorldEditorSettingsPanel settings;
  WorldEditorFileBrowserPanel file_browser_panel;
  WorldEditorSaveAsModal save_as_modal;
  WorldEditorUnsavedChangesModal unsaved_changes_modal;
  WorldEditorNewWorldModal new_world_modal;

  // Progress bar for world generation
  UIProgressBar *world_generation_progress;

  // Layout information for consistent positioning
  struct {
    int toolbar_x, toolbar_y;
    int palette_x, palette_y;
    int recent_x, recent_y;
    int z_swatches_x, z_swatches_y;
    int file_browser_panel_x, file_browser_panel_y;
    int file_panel_x, file_panel_y;
    int info_panel_x, info_panel_y;
    int actor_panel_x, actor_panel_y;
    int settings_panel_x, settings_panel_y;
  } layout;

  // Callbacks
  WorldEditorFileCallback file_callback;
  void *file_user_data;
  WorldEditorToolCallback tool_callback;
  void *tool_user_data;
  WorldEditorShapeCallback shape_callback;
  void *shape_user_data;
  WorldEditorAdjacentCallback adjacent_callback;
  void *adjacent_user_data;
  WorldEditorVoxelCallback voxel_callback;
  void *voxel_user_data;
  WorldEditorActorMoveCallback actor_move_callback;
  void *actor_move_user_data;

  // Camera movement callback
  void (*on_camera_move)(int dx, int dy, int dz, void *user_data);
  void *camera_user_data;
} WorldEditorUI;

// ============================================================================
// INITIALIZATION & CLEANUP
// ============================================================================

// Create the world editor UI system
WorldEditorUI *world_editor_ui_create(SDL_Renderer *renderer, const char *font_path);

// Destroy the world editor UI system
void world_editor_ui_destroy(WorldEditorUI *editor_ui);

// Calculate optimal layout positions based on window dimensions
void world_editor_ui_calculate_layout(WorldEditorUI *editor_ui, int window_width, int window_height);

// Create all UI panels with calculated layout positions
void world_editor_ui_create_all_panels(WorldEditorUI *editor_ui);

// Create minimal UI panels for testing (just file panel and toolbar)
void world_editor_ui_create_minimal_panels(WorldEditorUI *editor_ui);

// Update layout for new window dimensions
void world_editor_ui_resize(WorldEditorUI *editor_ui, int new_width, int new_height);

// ============================================================================
// PANEL CREATION FUNCTIONS
// ============================================================================

void world_editor_ui_create_file_panel(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_toolbar(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_palette(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_recent_panel(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_z_swatches(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_actor_panel(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_info_panel(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_settings_panel(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_file_browser_panel(WorldEditorUI *editor_ui, int x, int y);
void world_editor_ui_create_save_as_modal(WorldEditorUI *editor_ui);
void world_editor_ui_create_unsaved_changes_modal(WorldEditorUI *editor_ui);
void world_editor_ui_create_new_world_modal(WorldEditorUI *editor_ui);

// File operation triggering
void world_editor_ui_trigger_file_operation(WorldEditorUI *editor_ui, int operation_id);

// Progress bar management
void world_editor_ui_create_progress_bar(WorldEditorUI *editor_ui);
void world_editor_ui_show_progress_bar(WorldEditorUI *editor_ui, const char *message);
void world_editor_ui_update_progress_bar(WorldEditorUI *editor_ui, float progress, const char *message);
void world_editor_ui_hide_progress_bar(WorldEditorUI *editor_ui);

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

// Update world information display
void world_editor_ui_update_world_info(WorldEditorUI *editor_ui, World *world);

// Update actor list
void world_editor_ui_update_actor_list(WorldEditorUI *editor_ui, struct Actor *actors, int actor_count);

// Update recent files list
void world_editor_ui_update_recent_files(WorldEditorUI *editor_ui, const char **files, int file_count);

// Set selected tool
void world_editor_ui_set_selected_tool(WorldEditorUI *editor_ui, int tool_id);

// Set selected voxel type
void world_editor_ui_set_selected_voxel(WorldEditorUI *editor_ui, int voxel_type);

// ============================================================================
// CALLBACK SETTERS
// ============================================================================

void world_editor_ui_set_file_callback(WorldEditorUI *editor_ui, WorldEditorFileCallback callback, void *user_data);
void world_editor_ui_set_tool_callback(WorldEditorUI *editor_ui, WorldEditorToolCallback callback, void *user_data);
void world_editor_ui_set_shape_callback(WorldEditorUI *editor_ui, WorldEditorShapeCallback callback, void *user_data);
void world_editor_ui_set_adjacent_callback(WorldEditorUI *editor_ui, WorldEditorAdjacentCallback callback, void *user_data);
void world_editor_ui_set_voxel_callback(WorldEditorUI *editor_ui, WorldEditorVoxelCallback callback, void *user_data);
void world_editor_ui_set_actor_move_callback(WorldEditorUI *editor_ui, WorldEditorActorMoveCallback callback, void *user_data);

// Camera movement callback
void world_editor_ui_set_camera_callback(WorldEditorUI *editor_ui, void (*callback)(int dx, int dy, int dz, void *user_data), void *user_data);

// Set actor operation callback
void world_editor_ui_set_actor_callback(WorldEditorUI *editor_ui,
                                        void (*callback)(int operation_id, int actor_id, void *user_data), void *user_data);

// Set setting change callback
void world_editor_ui_set_setting_callback(WorldEditorUI *editor_ui,
                                          void (*callback)(int setting_id, const char *value, void *user_data), void *user_data);

// ============================================================================
// RENDERING & INPUT
// ============================================================================

// Render all world editor UI elements
void world_editor_ui_render(WorldEditorUI *editor_ui);

// ============================================================================
// EVENT HANDLING
// ============================================================================

// Handle SDL events for the world editor UI
bool world_editor_ui_handle_event(WorldEditorUI *editor_ui, SDL_Event *event);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Get voxel type name for display
const char *world_editor_ui_get_voxel_name(int voxel_type);

// ============================================================================
// WORLD EDITOR INTEGRATION
// ============================================================================

// Set new world configuration for generation
void world_editor_set_new_world_config(WorldGenerationType world_type, const char *seed, bool generate_neighbors);

// Get voxel color for display
SDL_Color world_editor_ui_get_voxel_color(int voxel_type);

// Create a voxel button with proper styling
UIButton *world_editor_ui_create_voxel_button(WorldEditorUI *editor_ui, int x, int y,
                                              int voxel_type, int button_id);

#endif // WORLD_EDITOR_UI_H
