#include "ui_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// PRESET COLORS
// ============================================================================

const SDL_Color UI_COLOR_BLACK = {0, 0, 0, 255};
const SDL_Color UI_COLOR_WHITE = {255, 255, 255, 255};
const SDL_Color UI_COLOR_GRAY = {128, 128, 128, 255};
const SDL_Color UI_COLOR_LIGHT_GRAY = {192, 192, 192, 255};
const SDL_Color UI_COLOR_DARK_GRAY = {64, 64, 64, 255};
const SDL_Color UI_COLOR_BLUE = {0, 100, 255, 255};
const SDL_Color UI_COLOR_LIGHT_BLUE = {100, 150, 255, 255};
const SDL_Color UI_COLOR_DARK_BLUE = {0, 50, 128, 255};
const SDL_Color UI_COLOR_GREEN = {0, 200, 0, 255};
const SDL_Color UI_COLOR_RED = {255, 0, 0, 255};
const SDL_Color UI_COLOR_YELLOW = {255, 255, 0, 255};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

bool ui_point_in_rect(int x, int y, SDL_Rect rect)
{
  return (x >= rect.x && x < rect.x + rect.w &&
          y >= rect.y && y < rect.y + rect.h);
}

void ui_get_text_size(TTF_Font *font, const char *text, int *width, int *height)
{
  if (!font || !text)
  {
    *width = *height = 0;
    return;
  }
  TTF_SizeText(font, text, width, height);
}

void ui_draw_text(UISystem *ui, TTF_Font *font, const char *text, int x, int y, SDL_Color color)
{
  if (!ui || !font || !text)
    return;

  SDL_Surface *surface = TTF_RenderText_Solid(font, text, color);
  if (!surface)
  {
    printf("DEBUG: TTF_RenderText_Solid failed: %s\n", TTF_GetError());
    return;
  }

  SDL_Texture *texture = SDL_CreateTextureFromSurface(ui->renderer, surface);
  if (texture)
  {
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(ui->renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
  }
  else
  {
    printf("DEBUG: Failed to create texture from surface: %s\n", SDL_GetError());
  }
  SDL_FreeSurface(surface);
}

void ui_draw_rect(UISystem *ui, SDL_Rect rect, SDL_Color fill_color, SDL_Color border_color, int border_thickness)
{
  if (!ui)
    return;

  // Fill rectangle
  SDL_SetRenderDrawColor(ui->renderer, fill_color.r, fill_color.g, fill_color.b, fill_color.a);
  SDL_RenderFillRect(ui->renderer, &rect);

  // Draw border if specified
  if (border_thickness > 0)
  {
    SDL_SetRenderDrawColor(ui->renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    for (int i = 0; i < border_thickness; i++)
    {
      SDL_Rect border_rect = {rect.x - i, rect.y - i, rect.w + 2 * i, rect.h + 2 * i};
      SDL_RenderDrawRect(ui->renderer, &border_rect);
    }
  }
}

// ============================================================================
// INITIALIZATION & CLEANUP
// ============================================================================

UISystem *ui_system_create(SDL_Renderer *renderer, const char *font_path)
{
  if (!renderer)
    return NULL;

  UISystem *ui = calloc(1, sizeof(UISystem));
  if (!ui)
    return NULL;

  ui->renderer = renderer;

  // Load fonts - use the same font as the World Editor
  ui->default_font = TTF_OpenFont(font_path ? font_path : "../assets/fonts/visitor-tt2-brk.ttf", 14);
  ui->title_font = TTF_OpenFont(font_path ? font_path : "../assets/fonts/visitor-tt2-brk.ttf", 18);
  ui->small_font = TTF_OpenFont(font_path ? font_path : "../assets/fonts/visitor-tt2-brk.ttf", 12);

  printf("DEBUG: Font loading - default_font=%p, title_font=%p, small_font=%p\n",
         (void *)ui->default_font, (void *)ui->title_font, (void *)ui->small_font);

  if (!ui->default_font)
  {
    printf("DEBUG: Project font failed, trying system fallback\n");
    // Simple fallback to system font if the project font fails
    ui->default_font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 14);
    ui->title_font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 18);
    ui->small_font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 12);

    printf("DEBUG: Fallback font loading - default_font=%p, title_font=%p, small_font=%p\n",
           (void *)ui->default_font, (void *)ui->title_font, (void *)ui->small_font);
  }
  else
  {
    printf("DEBUG: Successfully loaded project font: assets/fonts/visitor-tt2-brk.ttf\n");
  }

  // If still no fonts, create a fallback using the first available system font
  if (!ui->default_font)
  {
    // This is a last resort - we'll need to handle text rendering differently
    // For now, let's try to create a minimal font or use a different approach
  }

  // Initialize element storage
  ui->button_capacity = 32;
  ui->panel_capacity = 16;
  ui->text_capacity = 64;
  ui->input_field_capacity = 16;
  ui->dropdown_capacity = 16;
  ui->file_browser_capacity = 1; // Assuming one file browser for now
  ui->progress_bar_capacity = 4; // Allow multiple progress bars

  ui->buttons = calloc(ui->button_capacity, sizeof(UIButton));
  ui->panels = calloc(ui->panel_capacity, sizeof(UIPanel));
  ui->texts = calloc(ui->text_capacity, sizeof(UIText));
  ui->input_fields = calloc(ui->input_field_capacity, sizeof(UIInputField));
  ui->dropdowns = calloc(ui->dropdown_capacity, sizeof(UIDropdown));
  ui->file_browsers = calloc(ui->file_browser_capacity, sizeof(UIFileBrowser));
  ui->progress_bars = calloc(ui->progress_bar_capacity, sizeof(UIProgressBar));

  return ui;
}

void ui_system_destroy(UISystem *ui)
{
  if (!ui)
    return;

  // Free fonts
  if (ui->default_font)
    TTF_CloseFont(ui->default_font);
  if (ui->title_font)
    TTF_CloseFont(ui->title_font);
  if (ui->small_font)
    TTF_CloseFont(ui->small_font);

  // Free element storage
  if (ui->buttons)
    free(ui->buttons);
  if (ui->panels)
    free(ui->panels);
  if (ui->texts)
    free(ui->texts);
  if (ui->input_fields)
    free(ui->input_fields);
  if (ui->dropdowns)
    free(ui->dropdowns);
  if (ui->file_browsers)
    free(ui->file_browsers);
  if (ui->progress_bars)
    free(ui->progress_bars);

  free(ui);
}

// ============================================================================
// ELEMENT CREATION & MANAGEMENT
// ============================================================================

UIButton *ui_create_button(UISystem *ui, int x, int y, int width, int height,
                           const char *text, int id)
{
  if (!ui || ui->button_count >= ui->button_capacity)
    return NULL;

  UIButton *button = &ui->buttons[ui->button_count++];
  memset(button, 0, sizeof(UIButton));

  button->id = id;
  button->x = x;
  button->y = y;
  button->width = width;
  button->height = height;
  button->z_order = UI_Z_CONTENT;  // Default to content layer
  button->text = text;
  button->enabled = true;
  button->visible = true;

  // Apply default style
  ui_button_style_default(button);

  return button;
}

void ui_destroy_button(UISystem *ui, UIButton *button)
{
  if (!ui || !button)
    return;

  // Find and remove button from array
  for (int i = 0; i < ui->button_count; i++)
  {
    if (&ui->buttons[i] == button)
    {
      // Shift remaining buttons
      memmove(&ui->buttons[i], &ui->buttons[i + 1],
              (ui->button_count - i - 1) * sizeof(UIButton));
      ui->button_count--;
      break;
    }
  }
}

void ui_button_set_callback(UIButton *button, void (*callback)(int, void *), void *user_data)
{
  if (!button)
    return;
  button->on_click = callback;
  button->user_data = user_data;
}

UIPanel *ui_create_panel(UISystem *ui, int x, int y, int width, int height, const char *title)
{
  if (!ui || ui->panel_count >= ui->panel_capacity)
    return NULL;

  UIPanel *panel = &ui->panels[ui->panel_count++];
  memset(panel, 0, sizeof(UIPanel));

  panel->x = x;
  panel->y = y;
  panel->width = width;
  panel->height = height;
  panel->z_order = UI_Z_PANEL;  // Default to panel layer
  panel->title = title;
  panel->visible = true;
  panel->title_height = 20;
  panel->border_thickness = 2;

  // Apply default style
  ui_panel_style_default(panel);

  return panel;
}

void ui_destroy_panel(UISystem *ui, UIPanel *panel)
{
  if (!ui || !panel)
    return;

  // Find and remove panel from array
  for (int i = 0; i < ui->panel_count; i++)
  {
    if (&ui->panels[i] == panel)
    {
      memmove(&ui->panels[i], &ui->panels[i + 1],
              (ui->panel_count - i - 1) * sizeof(UIPanel));
      ui->panel_count--;
      break;
    }
  }
}

void ui_panel_add_button(UIPanel *panel, UIButton *button)
{
  if (!panel || !button)
    return;

  if (panel->button_count >= panel->button_capacity)
  {
    int new_capacity = panel->button_capacity ? panel->button_capacity * 2 : 8;
    UIButton **new_buttons = realloc(panel->buttons, new_capacity * sizeof(UIButton *));
    if (!new_buttons)
      return;
    panel->buttons = new_buttons;
    panel->button_capacity = new_capacity;
  }

  panel->buttons[panel->button_count++] = button;
}

UIText *ui_create_text(UISystem *ui, int x, int y, const char *text)
{
  if (!ui || ui->text_count >= ui->text_capacity)
    return NULL;

  UIText *text_elem = &ui->texts[ui->text_count++];
  memset(text_elem, 0, sizeof(UIText));

  text_elem->x = x;
  text_elem->y = y;
  text_elem->z_order = UI_Z_CONTENT;  // Default to content layer
  text_elem->text = text;
  text_elem->visible = true;
  text_elem->font = ui->default_font;
  text_elem->color = UI_COLOR_WHITE;

  return text_elem;
}

void ui_destroy_text(UISystem *ui, UIText *text)
{
  if (!ui || !text)
    return;

  for (int i = 0; i < ui->text_count; i++)
  {
    if (&ui->texts[i] == text)
    {
      memmove(&ui->texts[i], &ui->texts[i + 1],
              (ui->text_count - i - 1) * sizeof(UIText));
      ui->text_count--;
      break;
    }
  }
}

UIInputField *ui_create_input_field(UISystem *ui, int x, int y, int width, int height)
{
  if (!ui || ui->input_field_count >= ui->input_field_capacity)
    return NULL;

  UIInputField *field = &ui->input_fields[ui->input_field_count++];
  memset(field, 0, sizeof(UIInputField));

  field->x = x;
  field->y = y;
  field->width = width;
  field->height = height;
  field->z_order = UI_Z_CONTENT;  // Default to content layer
  field->visible = true;
  field->max_length = 256;
  field->text = calloc(field->max_length, sizeof(char));
  field->border_thickness = 1;

  // Apply default styling
  field->background_color = UI_COLOR_WHITE;
  field->border_color = UI_COLOR_GRAY;
  field->text_color = UI_COLOR_BLACK;
  field->cursor_color = UI_COLOR_BLACK;

  // Initialize placeholder
  field->placeholder = NULL;

  return field;
}

void ui_destroy_input_field(UISystem *ui, UIInputField *field)
{
  if (!ui || !field)
    return;

  if (field->text)
    free(field->text);

  if (field->placeholder)
    free(field->placeholder);

  for (int i = 0; i < ui->input_field_count; i++)
  {
    if (&ui->input_fields[i] == field)
    {
      memmove(&ui->input_fields[i], &ui->input_fields[i + 1],
              (ui->input_field_count - i - 1) * sizeof(UIInputField));
      ui->input_field_count--;
      break;
    }
  }
}

void ui_input_field_set_callback(UIInputField *field, void (*on_change)(const char *, void *),
                                 void (*on_enter)(const char *, void *), void *user_data)
{
  if (!field)
    return;
  field->on_text_change = on_change;
  field->on_enter = on_enter;
  field->user_data = user_data;
}

void ui_input_field_set_placeholder(UIInputField *field, const char *placeholder)
{
  if (!field)
    return;

  // Free existing placeholder if any
  if (field->placeholder)
    free(field->placeholder);

  // Set new placeholder
  if (placeholder && *placeholder) {
    field->placeholder = strdup(placeholder);
  } else {
    field->placeholder = NULL;
  }
}

UIDropdown *ui_create_dropdown(UISystem *ui, int x, int y, int width, int height)
{
  if (!ui || ui->dropdown_count >= ui->dropdown_capacity)
    return NULL;

  UIDropdown *dropdown = &ui->dropdowns[ui->dropdown_count++];
  memset(dropdown, 0, sizeof(UIDropdown));

  dropdown->x = x;
  dropdown->y = y;
  dropdown->width = width;
  dropdown->height = height;
  dropdown->z_order = UI_Z_DROPDOWN;  // Default to dropdown layer (high priority)
  dropdown->visible = true;
  dropdown->border_thickness = 1;

  // Apply default styling
  dropdown->background_color = UI_COLOR_WHITE;
  dropdown->border_color = UI_COLOR_GRAY;
  dropdown->text_color = UI_COLOR_BLACK;
  dropdown->selected_color = UI_COLOR_LIGHT_BLUE;

  return dropdown;
}

void ui_destroy_dropdown(UISystem *ui, UIDropdown *dropdown)
{
  if (!ui || !dropdown)
    return;

  // Free options
  if (dropdown->options)
  {
    for (int i = 0; i < dropdown->option_count; i++)
    {
      if (dropdown->options[i])
        free(dropdown->options[i]);
    }
    free(dropdown->options);
  }

  for (int i = 0; i < ui->dropdown_count; i++)
  {
    if (&ui->dropdowns[i] == dropdown)
    {
      memmove(&ui->dropdowns[i], &ui->dropdowns[i + 1],
              (ui->dropdown_count - i - 1) * sizeof(UIDropdown));
      ui->dropdown_count--;
      break;
    }
  }
}

void ui_dropdown_add_option(UIDropdown *dropdown, const char *option)
{
  if (!dropdown || !option)
    return;

  if (dropdown->option_count == 0)
  {
    dropdown->options = malloc(sizeof(char *));
    dropdown->options[0] = strdup(option);
    dropdown->option_count = 1;
  }
  else
  {
    char **new_options = realloc(dropdown->options, (dropdown->option_count + 1) * sizeof(char *));
    if (!new_options)
      return;
    dropdown->options = new_options;
    dropdown->options[dropdown->option_count] = strdup(option);
    dropdown->option_count++;
  }
}

void ui_dropdown_set_callback(UIDropdown *dropdown, void (*callback)(int, const char *, void *), void *user_data)
{
  if (!dropdown)
    return;
  dropdown->on_selection_change = callback;
  dropdown->user_data = user_data;
}

void ui_dropdown_style_default(UIDropdown *dropdown)
{
  if (!dropdown)
    return;

  dropdown->background_color = UI_COLOR_WHITE;
  dropdown->border_color = UI_COLOR_GRAY;
  dropdown->text_color = UI_COLOR_BLACK;
  dropdown->selected_color = UI_COLOR_LIGHT_BLUE;
  dropdown->border_thickness = 1;
}

void ui_dropdown_style_world_editor(UIDropdown *dropdown)
{
  if (!dropdown)
    return;

  // Match the original world editor dropdown styling
  dropdown->background_color.r = 0;
  dropdown->background_color.g = 0;
  dropdown->background_color.b = 0;
  dropdown->background_color.a = 255;
  dropdown->border_color.r = 255;
  dropdown->border_color.g = 255;
  dropdown->border_color.b = 255;
  dropdown->border_color.a = 120;
  dropdown->text_color.r = 230;
  dropdown->text_color.g = 230;
  dropdown->text_color.b = 230;
  dropdown->text_color.a = 255;
  dropdown->selected_color.r = 80;
  dropdown->selected_color.g = 160;
  dropdown->selected_color.b = 220;
  dropdown->selected_color.a = 255;
  dropdown->border_thickness = 1;
}

// ============================================================================
// FILE BROWSER FUNCTIONS
// ============================================================================

UIFileBrowser *ui_create_file_browser(UISystem *ui, int x, int y, int width, int height)
{
  if (!ui || ui->file_browser_count >= ui->file_browser_capacity)
    return NULL;

  UIFileBrowser *browser = &ui->file_browsers[ui->file_browser_count++];
  memset(browser, 0, sizeof(UIFileBrowser));

  browser->x = x;
  browser->y = y;
  browser->width = width;
  browser->height = height;
  browser->z_order = UI_Z_CONTENT;  // Default to content layer
  browser->visible = true;
  browser->show_hidden = false;
  browser->border_thickness = 1;
  browser->item_height = 20;
  browser->scroll_offset = 0;
  browser->selected_index = -1;

  // Apply default styling
  browser->background_color = UI_COLOR_WHITE;
  browser->border_color = UI_COLOR_GRAY;
  browser->text_color = UI_COLOR_BLACK;
  browser->selected_color = UI_COLOR_LIGHT_BLUE;
  browser->directory_color = UI_COLOR_DARK_BLUE;

  // Set default path to current directory
  browser->current_path = strdup(".");
  browser->file_filter = NULL;

  // Initialize file list
  browser->file_list = NULL;
  browser->file_count = 0;

  return browser;
}

void ui_destroy_file_browser(UISystem *ui, UIFileBrowser *browser)
{
  if (!ui || !browser)
    return;

  // Free current path
  if (browser->current_path)
    free(browser->current_path);

  // Free file filter
  if (browser->file_filter)
    free(browser->file_filter);

  // Free file list
  if (browser->file_list)
  {
    for (int i = 0; i < browser->file_count; i++)
    {
      if (browser->file_list[i])
        free(browser->file_list[i]);
    }
    free(browser->file_list);
  }

  // Remove from UI system
  for (int i = 0; i < ui->file_browser_count; i++)
  {
    if (&ui->file_browsers[i] == browser)
    {
      memmove(&ui->file_browsers[i], &ui->file_browsers[i + 1],
              (ui->file_browser_count - i - 1) * sizeof(UIFileBrowser));
      ui->file_browser_count--;
      break;
    }
  }
}

void ui_file_browser_set_path(UIFileBrowser *browser, const char *path)
{
  if (!browser || !path)
    return;

  if (browser->current_path)
    free(browser->current_path);

  browser->current_path = strdup(path);
  browser->selected_index = -1;
  browser->scroll_offset = 0;

  // Refresh the file list
  ui_file_browser_refresh(browser);
}

void ui_file_browser_set_filter(UIFileBrowser *browser, const char *filter)
{
  if (!browser)
    return;

  if (browser->file_filter)
    free(browser->file_filter);

  browser->file_filter = filter ? strdup(filter) : NULL;

  // Refresh the file list
  ui_file_browser_refresh(browser);
}

void ui_file_browser_set_callbacks(UIFileBrowser *browser,
                                   void (*on_file_selected)(const char *, void *),
                                   void (*on_directory_changed)(const char *, void *),
                                   void *user_data)
{
  if (!browser)
    return;

  browser->on_file_selected = on_file_selected;
  browser->on_directory_changed = on_directory_changed;
  browser->user_data = user_data;
}

void ui_file_browser_refresh(UIFileBrowser *browser)
{
  if (!browser || !browser->current_path)
    return;

  // Free existing file list
  if (browser->file_list)
  {
    for (int i = 0; i < browser->file_count; i++)
    {
      if (browser->file_list[i])
        free(browser->file_list[i]);
    }
    free(browser->file_list);
    browser->file_list = NULL;
    browser->file_count = 0;
  }

  // This is just a placeholder - actual file browsing is handled by the world editor
  // The world editor will populate this when the file browser is shown
  browser->file_count = 1;
  browser->file_list = malloc(sizeof(char *));
  browser->file_list[0] = strdup("Loading...");
}

// ============================================================================
// PROGRESS BAR MANAGEMENT
// ============================================================================

UIProgressBar *ui_create_progress_bar(UISystem *ui, int x, int y, int width, int height)
{
  if (!ui || ui->progress_bar_count >= ui->progress_bar_capacity)
    return NULL;

  UIProgressBar *progress_bar = &ui->progress_bars[ui->progress_bar_count];
  ui->progress_bar_count++;

  // Initialize progress bar
  progress_bar->x = x;
  progress_bar->y = y;
  progress_bar->width = width;
  progress_bar->height = height;
  progress_bar->z_order = UI_Z_MODAL;
  progress_bar->visible = true;
  progress_bar->progress = 0.0f;
  progress_bar->message = NULL;
  progress_bar->font = ui->default_font;

  // Default styling
  progress_bar->background_color = UI_COLOR_DARK_GRAY;
  progress_bar->progress_color = UI_COLOR_BLUE;
  progress_bar->border_color = UI_COLOR_GRAY;
  progress_bar->text_color = UI_COLOR_WHITE;
  progress_bar->border_thickness = 2;

  return progress_bar;
}

void ui_destroy_progress_bar(UISystem *ui, UIProgressBar *progress_bar)
{
  if (!ui || !progress_bar)
    return;

  // Free message string
  if (progress_bar->message)
  {
    free(progress_bar->message);
    progress_bar->message = NULL;
  }

  // Find and remove from array
  for (int i = 0; i < ui->progress_bar_count; i++)
  {
    if (&ui->progress_bars[i] == progress_bar)
    {
      // Shift remaining elements
      for (int j = i; j < ui->progress_bar_count - 1; j++)
      {
        ui->progress_bars[j] = ui->progress_bars[j + 1];
      }
      ui->progress_bar_count--;
      break;
    }
  }
}

void ui_progress_bar_set_progress(UIProgressBar *progress_bar, float progress)
{
  if (!progress_bar)
    return;

  // Clamp progress between 0.0 and 1.0
  if (progress < 0.0f)
    progress = 0.0f;
  else if (progress > 1.0f)
    progress = 1.0f;

  progress_bar->progress = progress;
}

void ui_progress_bar_set_message(UIProgressBar *progress_bar, const char *message)
{
  if (!progress_bar)
    return;

  // Free existing message
  if (progress_bar->message)
  {
    free(progress_bar->message);
    progress_bar->message = NULL;
  }

  // Set new message
  if (message)
  {
    progress_bar->message = strdup(message);
  }
}

void ui_progress_bar_set_visible(UIProgressBar *progress_bar, bool visible)
{
  if (!progress_bar)
    return;

  progress_bar->visible = visible;
}

// ============================================================================
// STYLING FUNCTIONS
// ============================================================================

void ui_button_style_default(UIButton *button)
{
  if (!button)
    return;

  button->normal_color = UI_COLOR_LIGHT_GRAY;
  button->hover_color = UI_COLOR_GRAY;
  button->pressed_color = UI_COLOR_DARK_GRAY;
  button->disabled_color = UI_COLOR_DARK_GRAY;
  button->text_color = UI_COLOR_BLACK;
  button->selected_color = UI_COLOR_BLUE;
}

void ui_button_style_primary(UIButton *button)
{
  if (!button)
    return;

  button->normal_color = UI_COLOR_BLUE;
  button->hover_color = UI_COLOR_LIGHT_BLUE;
  button->pressed_color = UI_COLOR_DARK_BLUE;
  button->disabled_color = UI_COLOR_DARK_GRAY;
  button->text_color = UI_COLOR_WHITE;
  button->selected_color = UI_COLOR_WHITE;  // White border by default
}

void ui_button_style_secondary(UIButton *button)
{
  if (!button)
    return;

  button->normal_color = UI_COLOR_GRAY;
  button->hover_color = UI_COLOR_LIGHT_GRAY;
  button->pressed_color = UI_COLOR_DARK_GRAY;
  button->disabled_color = UI_COLOR_DARK_GRAY;
  button->text_color = UI_COLOR_WHITE;
  button->selected_color = UI_COLOR_WHITE;  // White border by default
}

void ui_button_style_danger(UIButton *button)
{
  if (!button)
    return;

  button->normal_color = UI_COLOR_RED;
  button->hover_color.r = 255;
  button->hover_color.g = 100;
  button->hover_color.b = 100;
  button->hover_color.a = 255;
  button->pressed_color.r = 200;
  button->pressed_color.g = 0;
  button->pressed_color.b = 0;
  button->pressed_color.a = 255;
  button->disabled_color = UI_COLOR_DARK_GRAY;
  button->text_color = UI_COLOR_WHITE;
  button->selected_color = UI_COLOR_WHITE;  // White border by default
}

void ui_button_style_world_editor(UIButton *button)
{
  if (!button)
    return;

  // Use visible colors for world editor buttons
  // Background: dark gray (visible on light backgrounds)
  button->normal_color.r = 60;
  button->normal_color.g = 60;
  button->normal_color.b = 60;
  button->normal_color.a = 255;
  button->hover_color.r = 80;
  button->hover_color.g = 80;
  button->hover_color.b = 80;
  button->hover_color.a = 255;
  button->pressed_color.r = 100;
  button->pressed_color.g = 100;
  button->pressed_color.b = 100;
  button->pressed_color.a = 255;
  button->disabled_color.r = 40;
  button->disabled_color.g = 40;
  button->disabled_color.b = 40;
  button->disabled_color.a = 255;

  // Text: solid white (alpha 255)
  button->text_color.r = 255;
  button->text_color.g = 255;
  button->text_color.b = 255;
  button->text_color.a = 255;

  // Border: solid white (alpha 255) - this is the selected_color
  button->selected_color.r = 255;
  button->selected_color.g = 255;
  button->selected_color.b = 255;
  button->selected_color.a = 255;
}

void ui_panel_style_default(UIPanel *panel)
{
  if (!panel)
    return;

  panel->background_color.r = 240;
  panel->background_color.g = 240;
  panel->background_color.b = 240;
  panel->background_color.a = 255;
  panel->border_color = UI_COLOR_GRAY;
  panel->title_color = UI_COLOR_BLACK;
}

void ui_panel_style_modal(UIPanel *panel)
{
  if (!panel)
    return;

  // Use black background with white text for modals
  panel->background_color.r = 0;
  panel->background_color.g = 0;
  panel->background_color.b = 0;
  panel->background_color.a = 255;
  panel->border_color.r = 100;
  panel->border_color.g = 100;
  panel->border_color.b = 100;
  panel->border_color.a = 255;
  panel->title_color.r = 255;
  panel->title_color.g = 255;
  panel->title_color.b = 255;
  panel->title_color.a = 255;
}

void ui_panel_style_toolbar(UIPanel *panel)
{
  if (!panel)
    return;

  panel->background_color.r = 220;
  panel->background_color.g = 220;
  panel->background_color.b = 220;
  panel->background_color.a = 255;
  panel->border_color = UI_COLOR_DARK_GRAY;
  panel->title_color = UI_COLOR_BLACK;
}

void ui_panel_style_world_editor(UIPanel *panel)
{
  if (!panel)
    return;

  // Use visible colors for world editor panels
  panel->background_color.r = 240;
  panel->background_color.g = 240;
  panel->background_color.b = 240;
  panel->background_color.a = 255;
  panel->border_color.r = 100;
  panel->border_color.g = 100;
  panel->border_color.b = 100;
  panel->border_color.a = 255;
  panel->title_color.r = 0;
  panel->title_color.g = 0;
  panel->title_color.b = 0;
  panel->title_color.a = 255;
}

// ============================================================================
// RENDERING
// ============================================================================

void ui_render_button(UISystem *ui, UIButton *button)
{
  if (!ui || !button || !button->enabled || !button->visible)
    return;

  SDL_Rect rect = {button->x, button->y, button->width, button->height};
  SDL_Color fill_color, border_color;

  // Determine colors based on state
  if (button->pressed)
  {
    fill_color = button->pressed_color;
    border_color = button->selected_color; // White border
  }
  else if (button->hovered || button->selected)
  {
    fill_color = button->hover_color;
    border_color = button->selected_color; // White border
  }
  else
  {
    fill_color = button->normal_color;
    border_color = button->selected_color; // Always white border for world editor buttons
  }

  // Draw button
  ui_draw_rect(ui, rect, fill_color, border_color, 1);

  // Draw text
  if (button->text)
  {
    int text_w, text_h;
    ui_get_text_size(ui->default_font, button->text, &text_w, &text_h);

    // Match the original World Editor text positioning: offset by +12 horizontally, +3 vertically
    int text_x = button->x + 12;
    int text_y = button->y + 3;

    ui_draw_text(ui, ui->default_font, button->text, text_x, text_y, button->text_color);
  }
}

void ui_render_panel(UISystem *ui, UIPanel *panel)
{
  if (!ui || !panel || !panel->visible)
    return;

  SDL_Rect panel_rect = {panel->x, panel->y, panel->width, panel->height};

  // Draw panel background
  ui_draw_rect(ui, panel_rect, panel->background_color, panel->border_color, panel->border_thickness);

  // Draw title if present
  if (panel->title)
  {
    ui_draw_text(ui, ui->default_font, panel->title,
                 panel->x + 5, panel->y + 2, panel->title_color);
  }

  // Draw panel buttons
  for (int i = 0; i < panel->button_count; i++)
  {
    if (panel->buttons[i])
    {
      ui_render_button(ui, panel->buttons[i]);
    }
  }
}

void ui_render_text(UISystem *ui, UIText *text)
{
  if (!ui || !text || !text->visible)
    return;

  if (text->centered)
  {
    int text_w, text_h;
    ui_get_text_size(text->font, text->text, &text_w, &text_h);
    int x = text->x - text_w / 2;
    ui_draw_text(ui, text->font, text->text, x, text->y, text->color);
  }
  else
  {
    ui_draw_text(ui, text->font, text->text, text->x, text->y, text->color);
  }
}

void ui_render_input_field(UISystem *ui, UIInputField *field)
{
  if (!ui || !field || !field->visible)
    return;

  SDL_Rect field_rect = {field->x, field->y, field->width, field->height};

  // Draw field background and border
  SDL_Color border_color = field->focused ? UI_COLOR_BLUE : field->border_color;
  ui_draw_rect(ui, field_rect, field->background_color, border_color, field->border_thickness);

  // Draw text or placeholder
  if (field->text && strlen(field->text) > 0)
  {
    ui_draw_text(ui, ui->default_font, field->text,
                 field->x + 2, field->y + 2, field->text_color);
  }
  else if (field->placeholder && !field->focused)
  {
    // Draw placeholder text in a lighter color when not focused
    SDL_Color placeholder_color = {128, 128, 128, 255}; // Light gray
    ui_draw_text(ui, ui->default_font, field->placeholder,
                 field->x + 2, field->y + 2, placeholder_color);
  }

  // Draw cursor if focused
  if (field->focused)
  {
    int cursor_x = field->x + 2;
    if (field->text && field->cursor_pos > 0)
    {
      char temp[256];
      strncpy(temp, field->text, field->cursor_pos);
      temp[field->cursor_pos] = '\0';
      int text_w, text_h;
      ui_get_text_size(ui->default_font, temp, &text_w, &text_h);
      cursor_x += text_w;
    }

    SDL_Rect cursor_rect = {cursor_x, field->y + 2, 2, field->height - 4};
    ui_draw_rect(ui, cursor_rect, field->cursor_color, field->cursor_color, 0);
  }
}

void ui_render_dropdown(UISystem *ui, UIDropdown *dropdown)
{
  if (!ui || !dropdown || !dropdown->visible)
    return;

  SDL_Rect dropdown_rect = {dropdown->x, dropdown->y, dropdown->width, dropdown->height};

  // Draw dropdown background and border
  ui_draw_rect(ui, dropdown_rect, dropdown->background_color, dropdown->border_color, dropdown->border_thickness);

  // Draw selected option
  if (dropdown->selected_index >= 0 && dropdown->selected_index < dropdown->option_count)
  {
    const char *selected_text = dropdown->options[dropdown->selected_index];
    ui_draw_text(ui, ui->default_font, selected_text,
                 dropdown->x + 2, dropdown->y + 2, dropdown->text_color);
  }

  // Draw dropdown arrow
  int arrow_x = dropdown->x + dropdown->width - 20;
  int arrow_y = dropdown->y + dropdown->height / 2 - 4;
  ui_draw_text(ui, ui->default_font, dropdown->expanded ? "^" : "v",
               arrow_x, arrow_y, dropdown->text_color);

  // Draw expanded options
  if (dropdown->expanded && dropdown->options)
  {
    for (int i = 0; i < dropdown->option_count; i++)
    {
      SDL_Rect option_rect = {dropdown->x, dropdown->y + dropdown->height + i * 20,
                              dropdown->width, 20};

      SDL_Color bg_color = (i == dropdown->selected_index) ? dropdown->selected_color : dropdown->background_color;

      ui_draw_rect(ui, option_rect, bg_color, dropdown->border_color, 1);
      ui_draw_text(ui, ui->default_font, dropdown->options[i],
                   option_rect.x + 2, option_rect.y + 2, dropdown->text_color);
    }
  }
}

void ui_render_file_browser(UISystem *ui, UIFileBrowser *browser)
{
  if (!ui || !browser || !browser->visible)
    return;

  SDL_Rect browser_rect = {browser->x, browser->y, browser->width, browser->height};

  // Draw browser background and border
  ui_draw_rect(ui, browser_rect, browser->background_color, browser->border_color, browser->border_thickness);

  // Draw current path at the top
  if (browser->current_path)
  {
    ui_draw_text(ui, ui->default_font, browser->current_path,
                 browser->x + 2, browser->y + 2, browser->text_color);
  }

  // Draw file list
  if (browser->file_list)
  {
    int y_offset = browser->y + 25; // Start below the path display

    for (int i = 0; i < browser->file_count && y_offset < browser->y + browser->height; i++)
    {
      if (i < browser->scroll_offset)
        continue;

      SDL_Rect item_rect = {browser->x + 2, y_offset, browser->width - 4, browser->item_height};

      // Determine item color
      SDL_Color bg_color = browser->background_color;
      SDL_Color text_color = browser->text_color;

      if (i == browser->selected_index)
      {
        bg_color = browser->selected_color;
        text_color = UI_COLOR_WHITE;
      }
      else if (strstr(browser->file_list[i], "/") || strcmp(browser->file_list[i], "..") == 0)
      {
        // Directory
        text_color = browser->directory_color;
      }

      // Draw item background
      ui_draw_rect(ui, item_rect, bg_color, browser->border_color, 0);

      // Draw item text
      ui_draw_text(ui, ui->default_font, browser->file_list[i],
                   item_rect.x + 2, item_rect.y + 2, text_color);

      y_offset += browser->item_height;
    }
  }
}

void ui_render_progress_bar(UISystem *ui, UIProgressBar *progress_bar)
{
  if (!ui || !progress_bar || !progress_bar->visible)
    return;

  SDL_Rect progress_rect = {progress_bar->x, progress_bar->y, progress_bar->width, progress_bar->height};

  // Draw background
  ui_draw_rect(ui, progress_rect, progress_bar->background_color, progress_bar->border_color, progress_bar->border_thickness);

  // Draw progress fill
  if (progress_bar->progress > 0.0f)
  {
    int fill_width = (int)(progress_bar->width * progress_bar->progress);
    if (fill_width > 0)
    {
      SDL_Rect fill_rect = {progress_bar->x, progress_bar->y, fill_width, progress_bar->height};
      ui_draw_rect(ui, fill_rect, progress_bar->progress_color, progress_bar->progress_color, 0);
    }
  }

  // Draw message text
  if (progress_bar->message && progress_bar->font)
  {
    // Center the text vertically and horizontally
    int text_width, text_height;
    ui_get_text_size(progress_bar->font, progress_bar->message, &text_width, &text_height);

    int text_x = progress_bar->x + (progress_bar->width - text_width) / 2;
    int text_y = progress_bar->y + (progress_bar->height - text_height) / 2;

    ui_draw_text(ui, progress_bar->font, progress_bar->message, text_x, text_y, progress_bar->text_color);
  }
}

void ui_system_render(UISystem *ui)
{
  if (!ui)
    return;

  // Render all elements in proper z-order (background to foreground)
  // 1. Panels (background)
  for (int i = 0; i < ui->panel_count; i++)
  {
    if (ui->panels[i].visible)
      ui_render_panel(ui, &ui->panels[i]);
  }

  // 2. File browsers (above panels)
  for (int i = 0; i < ui->file_browser_count; i++)
  {
    if (ui->file_browsers[i].visible)
      ui_render_file_browser(ui, &ui->file_browsers[i]);
  }

  // 2.5. Progress bars (above panels, below modals)
  for (int i = 0; i < ui->progress_bar_count; i++)
  {
    if (ui->progress_bars[i].visible)
      ui_render_progress_bar(ui, &ui->progress_bars[i]);
  }

  // 3. Standalone buttons (above panels, below dropdowns) - sorted by Z-order
  // First pass: collect visible buttons
  UIButton *visible_buttons[1024];  // Reasonable limit
  int visible_button_count = 0;

  for (int i = 0; i < ui->button_count && visible_button_count < 1024; i++)
  {
    if (ui->buttons[i].visible)
    {
      visible_buttons[visible_button_count++] = &ui->buttons[i];
    }
  }

  // Sort visible buttons by Z-order (ascending - background to foreground)
  for (int i = 0; i < visible_button_count - 1; i++)
  {
    for (int j = 0; j < visible_button_count - i - 1; j++)
    {
      if (visible_buttons[j]->z_order > visible_buttons[j + 1]->z_order)
      {
        UIButton *temp = visible_buttons[j];
        visible_buttons[j] = visible_buttons[j + 1];
        visible_buttons[j + 1] = temp;
      }
    }
  }

  // Render buttons in Z-order
  for (int i = 0; i < visible_button_count; i++)
  {
    ui_render_button(ui, visible_buttons[i]);
  }

  // 4. Text elements (above buttons)
  for (int i = 0; i < ui->text_count; i++)
  {
    if (ui->texts[i].visible)
      ui_render_text(ui, &ui->texts[i]);
  }

  // 5. Input fields (above text)
  for (int i = 0; i < ui->input_field_count; i++)
  {
    if (ui->input_fields[i].visible)
      ui_render_input_field(ui, &ui->input_fields[i]);
  }

  // 6. Dropdowns (top layer - always on top)
  for (int i = 0; i < ui->dropdown_count; i++)
  {
    if (ui->dropdowns[i].visible)
      ui_render_dropdown(ui, &ui->dropdowns[i]);
  }
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

void ui_system_handle_mouse_motion(UISystem *ui, int x, int y)
{
  if (!ui)
    return;

  ui->mouse_x = x;
  ui->mouse_y = y;

    // Update button hover states
  for (int i = 0; i < ui->button_count; i++)
  {
    UIButton *button = &ui->buttons[i];
    if (button->enabled && button->visible)
    {
      SDL_Rect rect = {button->x, button->y, button->width, button->height};
      button->hovered = ui_point_in_rect(x, y, rect);
    }
    else
    {
      button->hovered = false;
    }
  }

  // Update panel button hover states
  for (int i = 0; i < ui->panel_count; i++)
  {
    UIPanel *panel = &ui->panels[i];
    for (int j = 0; j < panel->button_count; j++)
    {
      UIButton *button = panel->buttons[j];
      if (button && button->enabled && button->visible)
      {
        SDL_Rect rect = {button->x, button->y, button->width, button->height};
        button->hovered = ui_point_in_rect(x, y, rect);
      }
      else if (button)
      {
        button->hovered = false;
      }
    }
  }
}

void ui_system_handle_mouse_click(UISystem *ui, int x, int y, int button)
{
  if (!ui)
    return;

  ui->mouse_pressed = true;
  ui->mouse_button = button;

  // Handle button clicks
  for (int i = 0; i < ui->button_count; i++)
  {
    UIButton *btn = &ui->buttons[i];
    if (btn->enabled && btn->visible && btn->hovered)
    {
      btn->pressed = true;
      if (btn->on_click)
      {
        btn->on_click(btn->id, btn->user_data);
      }
      if (ui->on_button_click)
      {
        ui->on_button_click(btn->id, ui->global_user_data);
      }
      return;
    }
  }

  // Handle panel button clicks
  for (int i = 0; i < ui->panel_count; i++)
  {
    UIPanel *panel = &ui->panels[i];
    for (int j = 0; j < panel->button_count; j++)
    {
      UIButton *btn = panel->buttons[j];
      if (btn && btn->enabled && btn->visible && btn->hovered)
      {
        btn->pressed = true;
        if (btn->on_click)
        {
          btn->on_click(btn->id, btn->user_data);
        }
        if (ui->on_button_click)
        {
          ui->on_button_click(btn->id, ui->global_user_data);
        }
        return;
      }
    }
  }

  // Handle input field focus
  for (int i = 0; i < ui->input_field_count; i++)
  {
    UIInputField *field = &ui->input_fields[i];
    SDL_Rect rect = {field->x, field->y, field->width, field->height};
    if (ui_point_in_rect(x, y, rect))
    {
      // Unfocus previous field
      if (ui->focused_input && ui->focused_input != field)
      {
        ui->focused_input->focused = false;
      }
      field->focused = true;
      ui->focused_input = field;
      return;
    }
  }

  // Handle dropdown clicks
  for (int i = 0; i < ui->dropdown_count; i++)
  {
    UIDropdown *dropdown = &ui->dropdowns[i];
    SDL_Rect rect = {dropdown->x, dropdown->y, dropdown->width, dropdown->height};
    if (ui_point_in_rect(x, y, rect))
    {
      dropdown->expanded = !dropdown->expanded;

      // Close other dropdowns
      for (int j = 0; j < ui->dropdown_count; j++)
      {
        if (j != i)
        {
          ui->dropdowns[j].expanded = false;
        }
      }
      return;
    }

    // Handle option selection
    if (dropdown->expanded && dropdown->options)
    {
      for (int j = 0; j < dropdown->option_count; j++)
      {
        SDL_Rect option_rect = {dropdown->x, dropdown->y + dropdown->height + j * 20,
                                dropdown->width, 20};
        if (ui_point_in_rect(x, y, option_rect))
        {
          dropdown->selected_index = j;
          dropdown->expanded = false;
          if (dropdown->on_selection_change)
          {
            dropdown->on_selection_change(j, dropdown->options[j], dropdown->user_data);
          }
          return;
        }
      }
    }
  }

  // Handle file browser clicks
  for (int i = 0; i < ui->file_browser_count; i++)
  {
    UIFileBrowser *browser = &ui->file_browsers[i];
    SDL_Rect rect = {browser->x, browser->y, browser->width, browser->height};
    if (ui_point_in_rect(x, y, rect))
    {
      // Calculate which item was clicked
      int item_y = y - browser->y - 25; // Adjust for path display
      if (item_y >= 0 && browser->file_list)
      {
        int item_index = (item_y / browser->item_height) + browser->scroll_offset;
        if (item_index >= 0 && item_index < browser->file_count)
        {
          browser->selected_index = item_index;

          // Handle file/directory selection
          const char *selected_item = browser->file_list[item_index];
          if (strcmp(selected_item, "..") == 0)
          {
            // Go up one directory
            if (browser->on_directory_changed)
            {
              // TODO: Implement proper directory navigation
              browser->on_directory_changed("..", browser->user_data);
            }
          }
          else if (strstr(selected_item, "/") || strstr(selected_item, "\\"))
          {
            // Directory
            if (browser->on_directory_changed)
            {
              browser->on_directory_changed(selected_item, browser->user_data);
            }
          }
          else
          {
            // File
            if (browser->on_file_selected)
            {
              char full_path[512];
              snprintf(full_path, sizeof(full_path), "%s/%s",
                      browser->current_path ? browser->current_path : ".", selected_item);
              browser->on_file_selected(full_path, browser->user_data);
            }
          }
        }
      }
      return;
    }
  }

  // Unfocus input field if clicking elsewhere
  if (ui->focused_input)
  {
    ui->focused_input->focused = false;
    ui->focused_input = NULL;
  }

  // Close dropdowns if clicking elsewhere
  for (int i = 0; i < ui->dropdown_count; i++)
  {
    ui->dropdowns[i].expanded = false;
  }
}

void ui_system_handle_key(UISystem *ui, int key, bool pressed)
{
  if (!ui || !pressed)
    return;

  ui->key_pressed = true;
  ui->last_key = key;

  // Handle input field input
  if (ui->focused_input)
  {
    UIInputField *field = ui->focused_input;

    switch (key)
    {
    case SDLK_BACKSPACE:
      if (field->cursor_pos > 0)
      {
        field->cursor_pos--;
        field->text[field->cursor_pos] = '\0';
        if (field->on_text_change)
        {
          field->on_text_change(field->text, field->user_data);
        }
      }
      break;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      if (field->on_enter)
      {
        field->on_enter(field->text, field->user_data);
      }
      break;

    case SDLK_LEFT:
      if (field->cursor_pos > 0)
        field->cursor_pos--;
      break;

    case SDLK_RIGHT:
      if (field->cursor_pos < field->text_length)
        field->cursor_pos++;
      break;

    case SDLK_HOME:
      field->cursor_pos = 0;
      break;

    case SDLK_END:
      field->cursor_pos = field->text_length;
      break;
    }
  }
}

bool ui_system_handle_event(UISystem *ui, SDL_Event *event)
{
  if (!ui || !event)
    return false;

  switch (event->type)
  {
  case SDL_MOUSEMOTION:
    ui_system_handle_mouse_motion(ui, event->motion.x, event->motion.y);
    return false; // Don't consume mouse motion events - let them pass through for chart hover detection

  case SDL_MOUSEBUTTONDOWN:
    ui_system_handle_mouse_click(ui, event->button.x, event->button.y, event->button.button);
    // Only consume the event if it was actually handled by UI elements
    // This allows clicks outside UI to pass through to the main handler
    return ui_system_has_clickable_element_at(ui, event->button.x, event->button.y);

  case SDL_MOUSEBUTTONUP:
    ui->mouse_pressed = false;
    // Reset button pressed states
    for (int i = 0; i < ui->button_count; i++)
    {
      ui->buttons[i].pressed = false;
    }
    for (int i = 0; i < ui->panel_count; i++)
    {
      for (int j = 0; j < ui->panels[i].button_count; j++)
      {
        if (ui->panels[i].buttons[j])
        {
          ui->panels[i].buttons[j]->pressed = false;
        }
      }
    }
    return true;

  case SDL_KEYDOWN:
    // Only handle UI-specific keys (like Tab for navigation, Enter for input fields)
    // Let game keys pass through to the main input handler
    if (ui->focused_input)
    {
      // If an input field is focused, handle all keyboard input
      ui_system_handle_key(ui, event->key.keysym.sym, true);
      return true;
    }
    // For non-input-field keys, only handle UI navigation keys
    switch (event->key.keysym.sym)
    {
    case SDLK_TAB:
      // Tab navigation between UI elements
      ui_system_handle_key(ui, event->key.keysym.sym, true);
      return true;
    case SDLK_ESCAPE:
      // Escape to close dialogs/panels
      ui_system_handle_key(ui, event->key.keysym.sym, true);
      return true;
    default:
      // Let all other keys pass through to game input handler
      return false;
    }

  case SDL_KEYUP:
    // Only handle key up for UI elements that are currently focused
    if (ui->focused_input)
    {
      ui_system_handle_key(ui, event->key.keysym.sym, false);
      return true;
    }
    return false;

  case SDL_TEXTINPUT:
    if (ui->focused_input)
    {
      UIInputField *field = ui->focused_input;
      if (field->text_length < field->max_length - 1)
      {
        field->text[field->text_length] = event->text.text[0];
        field->text_length++;
        field->cursor_pos = field->text_length;
        field->text[field->text_length] = '\0';
        if (field->on_text_change)
        {
          field->on_text_change(field->text, field->user_data);
        }
      }
      return true;
    }
    return false;
  }

  return false;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Check if there's a clickable UI element at the given coordinates
bool ui_system_has_clickable_element_at(UISystem *ui, int x, int y)
{
  if (!ui)
    return false;

  // Check if mouse is over any UI panels or buttons
  for (int i = 0; i < ui->panel_count; i++)
  {
    UIPanel *panel = &ui->panels[i];
    if (panel->visible && x >= panel->x && x < panel->x + panel->width &&
        y >= panel->y && y < panel->y + panel->height)
    {
      return true;
    }
  }

  // Check standalone buttons
  for (int i = 0; i < ui->button_count; i++)
  {
    UIButton *button = &ui->buttons[i];
    if (button->visible && x >= button->x && x < button->x + button->width &&
        y >= button->y && y < button->y + button->height)
    {
      return true;
    }
  }

  return false;
}

// ============================================================================
// FILE BROWSER EVENT HANDLING
// ============================================================================

bool ui_file_browser_handle_event(UIFileBrowser *browser, SDL_Event *event)
{
  if (!browser || !event)
    return false;

  switch (event->type)
  {
  case SDL_KEYDOWN:
    switch (event->key.keysym.sym)
    {
    case SDLK_UP:
      if (browser->selected_index > 0)
      {
        browser->selected_index--;
        if (browser->selected_index < browser->scroll_offset)
          browser->scroll_offset = browser->selected_index;
      }
      return true;

    case SDLK_DOWN:
      if (browser->selected_index < browser->file_count - 1)
      {
        browser->selected_index++;
        int max_visible = (browser->height - 25) / browser->item_height;
        if (browser->selected_index >= browser->scroll_offset + max_visible)
          browser->scroll_offset = browser->selected_index - max_visible + 1;
      }
      return true;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      if (browser->selected_index >= 0 && browser->selected_index < browser->file_count)
      {
        const char *selected_item = browser->file_list[browser->selected_index];
        if (strcmp(selected_item, "..") == 0)
        {
          if (browser->on_directory_changed)
            browser->on_directory_changed("..", browser->user_data);
        }
        else if (strstr(selected_item, "/") || strstr(selected_item, "\\"))
        {
          if (browser->on_directory_changed)
            browser->on_directory_changed(selected_item, browser->user_data);
        }
        else
        {
          if (browser->on_file_selected)
          {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s",
                    browser->current_path ? browser->current_path : ".", selected_item);
            browser->on_file_selected(full_path, browser->user_data);
          }
        }
      }
      return true;
    }
    break;
  }

  return false;
}

// ============================================================================
// Z-ORDER MANAGEMENT FUNCTIONS
// ============================================================================

void ui_button_set_z_order(UIButton *button, int z_order)
{
  if (button)
    button->z_order = z_order;
}

void ui_panel_set_z_order(UIPanel *panel, int z_order)
{
  if (panel)
    panel->z_order = z_order;
}

void ui_text_set_z_order(UIText *text, int z_order)
{
  if (text)
    text->z_order = z_order;
}

void ui_input_field_set_z_order(UIInputField *input, int z_order)
{
  if (input)
    input->z_order = z_order;
}

void ui_dropdown_set_z_order(UIDropdown *dropdown, int z_order)
{
  if (dropdown)
    dropdown->z_order = z_order;
}

void ui_file_browser_set_z_order(UIFileBrowser *browser, int z_order)
{
  if (browser)
    browser->z_order = z_order;
}

void ui_progress_bar_set_z_order(UIProgressBar *progress_bar, int z_order)
{
  if (progress_bar)
    progress_bar->z_order = z_order;
}

int ui_button_get_z_order(const UIButton *button)
{
  return button ? button->z_order : 0;
}

int ui_panel_get_z_order(const UIPanel *panel)
{
  return panel ? panel->z_order : 0;
}

int ui_text_get_z_order(const UIText *text)
{
  return text ? text->z_order : 0;
}

int ui_input_field_get_z_order(const UIInputField *input)
{
  return input ? input->z_order : 0;
}

int ui_dropdown_get_z_order(const UIDropdown *dropdown)
{
  return dropdown ? dropdown->z_order : 0;
}

int ui_file_browser_get_z_order(const UIFileBrowser *browser)
{
  return browser ? browser->z_order : 0;
}

int ui_progress_bar_get_z_order(const UIProgressBar *progress_bar)
{
  return progress_bar ? progress_bar->z_order : 0;
}
