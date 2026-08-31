#ifndef UI_SYSTEM_H
#define UI_SYSTEM_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// UI ELEMENT TYPES
// ============================================================================

// Button element with consistent styling and behavior
typedef struct
{
  int id;
  int x, y, width, height;
  int z_order;  // Z-order for layering (higher values render on top)
  const char *text;
  bool enabled;
  bool hovered;
  bool pressed;
  bool selected;
  bool visible;

  // Styling
  SDL_Color normal_color;
  SDL_Color hover_color;
  SDL_Color pressed_color;
  SDL_Color disabled_color;
  SDL_Color text_color;
  SDL_Color selected_color;

  // Callback
  void (*on_click)(int button_id, void *user_data);
  void *user_data;
} UIButton;

// Panel element for grouping UI elements
typedef struct
{
  int x, y, width, height;
  int z_order;  // Z-order for layering (higher values render on top)
  const char *title;
  bool visible;
  bool draggable;
  bool resizable;

  // Styling
  SDL_Color background_color;
  SDL_Color border_color;
  SDL_Color title_color;
  int border_thickness;
  int title_height;

  // Content
  UIButton **buttons;
  int button_count;
  int button_capacity;
} UIPanel;

// Text element with consistent rendering
typedef struct
{
  int x, y;
  int z_order;  // Z-order for layering (higher values render on top)
  const char *text;
  bool visible;
  bool centered;
  bool wrapped;
  int max_width;

  // Styling
  SDL_Color color;
  TTF_Font *font;
  int font_size;
} UIText;

// Input field for text entry
typedef struct
{
  int x, y, width, height;
  int z_order;  // Z-order for layering (higher values render on top)
  char *text;
  int text_length;
  int max_length;
  bool focused;
  bool visible;
  int cursor_pos;

  // Styling
  SDL_Color background_color;
  SDL_Color border_color;
  SDL_Color text_color;
  SDL_Color cursor_color;
  int border_thickness;

  // Placeholder text
  char *placeholder;

  // Callback
  void (*on_text_change)(const char *text, void *user_data);
  void (*on_enter)(const char *text, void *user_data);
  void *user_data;
} UIInputField;

// Dropdown/Combo box
typedef struct
{
  int x, y, width, height;
  int z_order;  // Z-order for layering (higher values render on top)
  char **options;
  int option_count;
  int selected_index;
  bool expanded;
  bool visible;

  // Styling
  SDL_Color background_color;
  SDL_Color border_color;
  SDL_Color text_color;
  SDL_Color selected_color;
  int border_thickness;

  // Callback
  void (*on_selection_change)(int index, const char *option, void *user_data);
  void *user_data;
} UIDropdown;

// File browser component for browsing directories and selecting files
typedef struct
{
  int z_order;  // Z-order for layering (higher values render on top)
  int x, y, width, height;
  char *current_path;
  char **file_list;
  int file_count;
  int selected_index;
  int scroll_offset;
  bool visible;
  bool show_hidden;

  // Filtering
  char *file_filter;  // e.g., "*.world" or "*.vox"

  // Styling
  SDL_Color background_color;
  SDL_Color border_color;
  SDL_Color text_color;
  SDL_Color selected_color;
  SDL_Color directory_color;
  int border_thickness;
  int item_height;

  // Callback
  void (*on_file_selected)(const char *filepath, void *user_data);
  void (*on_directory_changed)(const char *new_path, void *user_data);
  void *user_data;
} UIFileBrowser;

// Progress bar component for showing operation progress
typedef struct
{
  int z_order;  // Z-order for layering (higher values render on top)
  int x, y, width, height;
  bool visible;

  // Progress state
  float progress;  // 0.0 to 1.0
  char *message;   // Current status message

  // Styling
  SDL_Color background_color;
  SDL_Color progress_color;
  SDL_Color border_color;
  SDL_Color text_color;
  int border_thickness;
  TTF_Font *font;
} UIProgressBar;

// ============================================================================
// Z-ORDER CONSTANTS
// ============================================================================

// Predefined Z-order values for common UI layers
#define UI_Z_BACKGROUND    0      // Background elements
#define UI_Z_PANEL        100     // Panels and containers
#define UI_Z_CONTENT      200     // Regular content (buttons, text, etc.)
#define UI_Z_TOOLBAR      300     // Toolbars and tool panels
#define UI_Z_DROPDOWN     400     // Dropdown menus and popups
#define UI_Z_MODAL        500     // Modal dialogs and overlays
#define UI_Z_TOOLTIP      600     // Tooltips and help text
#define UI_Z_CURSOR       700     // Cursor and selection indicators

// ============================================================================
// UI SYSTEM CORE
// ============================================================================

typedef struct
{
  SDL_Renderer *renderer;
  TTF_Font *default_font;
  TTF_Font *title_font;
  TTF_Font *small_font;

  // Element storage
  UIButton *buttons;
  int button_count;
  int button_capacity;

  UIPanel *panels;
  int panel_count;
  int panel_capacity;

  UIText *texts;
  int text_count;
  int text_capacity;

  UIInputField *input_fields;
  int input_field_count;
  int input_field_capacity;

  UIDropdown *dropdowns;
  int dropdown_count;
  int dropdown_capacity;

  UIFileBrowser *file_browsers;
  int file_browser_count;
  int file_browser_capacity;

  UIProgressBar *progress_bars;
  int progress_bar_count;
  int progress_bar_capacity;

  // Input state
  int mouse_x, mouse_y;
  bool mouse_pressed;
  int mouse_button;
  bool key_pressed;
  int last_key;

  // Focus management
  UIInputField *focused_input;
  UIDropdown *focused_dropdown;

  // Callbacks
  void (*on_button_click)(int button_id, void *user_data);
  void (*on_text_change)(const char *text, void *user_data);
  void (*on_selection_change)(int index, const char *option, void *user_data);
  void *global_user_data;
} UISystem;

// ============================================================================
// INITIALIZATION & CLEANUP
// ============================================================================

// Initialize the UI system
UISystem *ui_system_create(SDL_Renderer *renderer, const char *font_path);
void ui_system_destroy(UISystem *ui);

// ============================================================================
// ELEMENT CREATION & MANAGEMENT
// ============================================================================

// Button management
UIButton *ui_create_button(UISystem *ui, int x, int y, int width, int height,
                           const char *text, int id);
void ui_destroy_button(UISystem *ui, UIButton *button);
void ui_button_set_callback(UIButton *button, void (*callback)(int, void *), void *user_data);

// Panel management
UIPanel *ui_create_panel(UISystem *ui, int x, int y, int width, int height, const char *title);
void ui_destroy_panel(UISystem *ui, UIPanel *panel);
void ui_panel_add_button(UIPanel *panel, UIButton *button);

// Text management
UIText *ui_create_text(UISystem *ui, int x, int y, const char *text);
void ui_destroy_text(UISystem *ui, UIText *text);

// Input field management
UIInputField *ui_create_input_field(UISystem *ui, int x, int y, int width, int height);
void ui_destroy_input_field(UISystem *ui, UIInputField *field);
void ui_input_field_set_callback(UIInputField *field, void (*on_change)(const char *, void *),
                                 void (*on_enter)(const char *, void *), void *user_data);
void ui_input_field_set_placeholder(UIInputField *field, const char *placeholder);

// Dropdown management
UIDropdown *ui_create_dropdown(UISystem *ui, int x, int y, int width, int height);
void ui_destroy_dropdown(UISystem *ui, UIDropdown *dropdown);
void ui_dropdown_add_option(UIDropdown *dropdown, const char *option);
void ui_dropdown_set_callback(UIDropdown *dropdown, void (*callback)(int, const char *, void *), void *user_data);

// File browser management
UIFileBrowser *ui_create_file_browser(UISystem *ui, int x, int y, int width, int height);
void ui_destroy_file_browser(UISystem *ui, UIFileBrowser *browser);
void ui_file_browser_set_path(UIFileBrowser *browser, const char *path);
void ui_file_browser_set_filter(UIFileBrowser *browser, const char *filter);
void ui_file_browser_set_callbacks(UIFileBrowser *browser,
                                   void (*on_file_selected)(const char *, void *),
                                   void (*on_directory_changed)(const char *, void *),
                                   void *user_data);
void ui_file_browser_refresh(UIFileBrowser *browser);

// Progress bar management
UIProgressBar *ui_create_progress_bar(UISystem *ui, int x, int y, int width, int height);
void ui_destroy_progress_bar(UISystem *ui, UIProgressBar *progress_bar);
void ui_progress_bar_set_progress(UIProgressBar *progress_bar, float progress);
void ui_progress_bar_set_message(UIProgressBar *progress_bar, const char *message);
void ui_progress_bar_set_visible(UIProgressBar *progress_bar, bool visible);

// ============================================================================
// Z-ORDER MANAGEMENT FUNCTIONS
// ============================================================================

// Set Z-order for UI elements
void ui_button_set_z_order(UIButton *button, int z_order);
void ui_panel_set_z_order(UIPanel *panel, int z_order);
void ui_text_set_z_order(UIText *text, int z_order);
void ui_input_field_set_z_order(UIInputField *input, int z_order);
void ui_dropdown_set_z_order(UIDropdown *dropdown, int z_order);
void ui_file_browser_set_z_order(UIFileBrowser *browser, int z_order);
void ui_progress_bar_set_z_order(UIProgressBar *progress_bar, int z_order);

// Get Z-order for UI elements
int ui_button_get_z_order(const UIButton *button);
int ui_panel_get_z_order(const UIPanel *panel);
int ui_text_get_z_order(const UIText *text);
int ui_input_field_get_z_order(const UIInputField *input);
int ui_dropdown_get_z_order(const UIDropdown *dropdown);
int ui_file_browser_get_z_order(const UIFileBrowser *browser);
int ui_progress_bar_get_z_order(const UIProgressBar *progress_bar);

// ============================================================================
// RENDERING
// ============================================================================

// Render all UI elements
void ui_system_render(UISystem *ui);

// Render individual elements
void ui_render_button(UISystem *ui, UIButton *button);
void ui_render_panel(UISystem *ui, UIPanel *panel);
void ui_render_text(UISystem *ui, UIText *text);
void ui_render_input_field(UISystem *ui, UIInputField *field);
void ui_render_dropdown(UISystem *ui, UIDropdown *dropdown);
void ui_render_file_browser(UISystem *ui, UIFileBrowser *browser);
void ui_render_progress_bar(UISystem *ui, UIProgressBar *progress_bar);

// ============================================================================
// INPUT HANDLING
// ============================================================================

// Handle SDL events and update UI state
bool ui_system_handle_event(UISystem *ui, SDL_Event *event);

// Handle mouse movement
void ui_system_handle_mouse_motion(UISystem *ui, int x, int y);

// Handle mouse clicks
void ui_system_handle_mouse_click(UISystem *ui, int x, int y, int button);

// Handle keyboard input
void ui_system_handle_key(UISystem *ui, int key, bool pressed);

// Handle file browser specific events
bool ui_file_browser_handle_event(UIFileBrowser *browser, SDL_Event *event);

// Check if there's a clickable UI element at the given coordinates
bool ui_system_has_clickable_element_at(UISystem *ui, int x, int y);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Check if a point is inside a rectangle
bool ui_point_in_rect(int x, int y, SDL_Rect rect);

// Get text dimensions
void ui_get_text_size(TTF_Font *font, const char *text, int *width, int *height);

// Draw text with consistent styling
void ui_draw_text(UISystem *ui, TTF_Font *font, const char *text, int x, int y, SDL_Color color);

// Draw a rectangle with optional border
void ui_draw_rect(UISystem *ui, SDL_Rect rect, SDL_Color fill_color, SDL_Color border_color, int border_thickness);

// ============================================================================
// PRESET STYLES
// ============================================================================

// Common color schemes
extern const SDL_Color UI_COLOR_BLACK;
extern const SDL_Color UI_COLOR_WHITE;
extern const SDL_Color UI_COLOR_GRAY;
extern const SDL_Color UI_COLOR_LIGHT_GRAY;
extern const SDL_Color UI_COLOR_DARK_GRAY;
extern const SDL_Color UI_COLOR_BLUE;
extern const SDL_Color UI_COLOR_LIGHT_BLUE;
extern const SDL_Color UI_COLOR_DARK_BLUE;
extern const SDL_Color UI_COLOR_GREEN;
extern const SDL_Color UI_COLOR_RED;
extern const SDL_Color UI_COLOR_YELLOW;

// Common button styles
void ui_button_style_default(UIButton *button);
void ui_button_style_primary(UIButton *button);
void ui_button_style_secondary(UIButton *button);
void ui_button_style_danger(UIButton *button);
void ui_button_style_world_editor(UIButton *button);
void ui_dropdown_style_default(UIDropdown *dropdown);
void ui_dropdown_style_world_editor(UIDropdown *dropdown);

// Common panel styles
void ui_panel_style_default(UIPanel *panel);
void ui_panel_style_modal(UIPanel *panel);
void ui_panel_style_toolbar(UIPanel *panel);
void ui_panel_style_world_editor(UIPanel *panel);

#endif // UI_SYSTEM_H
