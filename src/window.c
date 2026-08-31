#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_rwops.h>
#include <math.h>
#include "window.h"
#include "console.h"
#include "ui.h"
#include "renderer.h"
#include "character.h"
#include "isometric_renderer.h"
#include "fp_renderer.h"
#include "game_state.h"
#include "player_controls.h"
#include "fog_of_war.h"
#include "shadow_world.h"
#include "spirit_sprite.h"
#include "item_icon.h"
#include "mob_models.h"
#include "poly_mesh.h"
#include "dialogue.h"
#include "skill.h"
#include "nav_aide.h"
#include "settlement.h"
#include "currency.h"
#include "shop.h"
#include "craft.h"

#include "gamepad.h"

// Window state is now defined in window.h
WindowState window_state = {0};

// External variable for exit prompt state
extern int g_show_exit_prompt;

// External variables for tutorial system
extern World *g_game_world;
extern World *g_main_menu_world;
extern int g_gold;
extern char g_status_message[256];

// Global movement destination (deprecated - movement now handled by game_state)
// Left for compatibility with window_interactive_test.c
MovementDestination g_movement_destination = {0};

// Tutorial system globals
TutorialQuest g_tutorial_quest = {0};

// Main menu save-list cache. -1 means "not yet scanned".
static int g_saves_exist_cached = -1;

void window_invalidate_saves_exist_cache(void)
{
    g_saves_exist_cached = -1;
}
bool g_show_tutorial_modal = false;
bool g_tutorial_completed = false;

// Global isometric renderer
IsometricRenderer *g_isometric_renderer = NULL;

// First-person rendering mode flag
static bool g_fp_mode_enabled = false;

// Mouse look state. While active SDL captures the pointer and reports relative motion only, which
// is what lets the view keep turning past the edge of the screen. Deltas accumulate here between
// frames because several motion events can arrive per frame.
static bool g_mouse_look_active = false;
static int g_mouse_look_dx = 0;
static int g_mouse_look_dy = 0;
// Mute background music during title screen mixing
static bool g_audio_title_mode = true;
// Recenter viewport on next present after window size change
static bool g_needs_recentering = false;

// Voxel interaction state
static bool g_has_hovered_voxel = false;
static int g_hovered_voxel_x = 0;
static int g_hovered_voxel_y = 0;
static int g_hovered_voxel_z = 0;

static bool g_show_voxel_info = false;
static int g_info_voxel_x = 0;
static int g_info_voxel_y = 0;
static int g_info_voxel_z = 0;

// New selection system
static Selection g_current_selection = {.type = SELECTION_NONE, .data = {0}};

// Initialize SDL and create window
int window_init(const char *title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0)
    {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 0;
    }

    if (TTF_Init() < 0)
    {
        printf("TTF initialization failed: %s\n", TTF_GetError());
        return 0;
    }

    // Create window
    window_state.window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width,
        height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!window_state.window)
    {
        printf("Window creation failed: %s\n", SDL_GetError());
        return 0;
    }

    // Create renderer.
    //
    // Vsync is on by default because tearing is worse than a frame of latency, but it also pins
    // the frame rate to the display's refresh rate: on a 60Hz panel the client cannot exceed 60
    // FPS with this set, whatever the target frame rate says. VERSE_VSYNC=0 lifts that, which is
    // how the renderer's actual headroom gets measured.
    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    const char *vsync_env = getenv("VERSE_VSYNC");
    const bool vsync_enabled = !(vsync_env && vsync_env[0] == '0');
    if (vsync_enabled)
        renderer_flags |= SDL_RENDERER_PRESENTVSYNC;

    window_state.renderer = SDL_CreateRenderer(window_state.window, -1, renderer_flags);

    if (!window_state.renderer)
    {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return 0;
    }
    printf("Renderer created (vsync %s)\n", vsync_enabled ? "on" : "off");

    // Create base render texture for 256x240 resolution
    window_state.base_render_texture = SDL_CreateTexture(
        window_state.renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        BASE_RESOLUTION_WIDTH,
        BASE_RESOLUTION_HEIGHT);

    if (!window_state.base_render_texture)
    {
        printf("Base render texture creation failed: %s\n", SDL_GetError());
        return 0;
    }
    // Opaque blit when presenting: geometry that wrote low alpha (foliage gaps, blend) must not
    // let the window clear show through the scaled frame.
    SDL_SetTextureBlendMode(window_state.base_render_texture, SDL_BLENDMODE_NONE);

    // Load font (use SDL RWops so this works with Android APK assets)
    {
        SDL_RWops *rw = SDL_RWFromFile("assets/fonts/visitor-tt2-brk.ttf", "rb");
        if (rw)
        {
            window_state.font = TTF_OpenFontRW(rw, 1, 12);
        }
        else
        {
            window_state.font = NULL;
        }
    }
    if (!window_state.font)
    {
        // Fallback to default font
        window_state.font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 12);
    }

    if (!window_state.font)
    {
        printf("Font loading failed: %s\n", TTF_GetError());
        return 0;
    }

    // Set window properties
    window_state.width = width;
    window_state.height = height;
    window_state.base_width = BASE_RESOLUTION_WIDTH;
    window_state.base_height = BASE_RESOLUTION_HEIGHT;
    window_state.scale_factor = DEFAULT_SCALE_FACTOR;
    window_state.cell_width = 8;   // Adjusted for 12px font
    window_state.cell_height = 12; // Adjusted for 12px font

    // Set colors
    window_state.background_color = (SDL_Color){20, 20, 20, 255}; // Dark gray
    window_state.text_color = (SDL_Color){240, 240, 240, 255};    // Light gray
    window_state.ui_color = (SDL_Color){70, 130, 180, 255};       // Steel blue
    window_state.highlight_color = (SDL_Color){255, 215, 0, 255}; // Gold

    // Initialize fullscreen state
    window_state.fullscreen = 0;

    // Set windowed mode with optimal scale factor
    window_set_windowed_with_optimal_scale();

    // Initialize settings state
    window_state.settings_section = 0;
    window_state.settings_selection = 0;

    // Load settings from file or use defaults
    GameSettings settings;
    if (!settings_load(&settings, "settings.dat"))
    {
        // File doesn't exist or is corrupted, use defaults
        settings = DEFAULT_SETTINGS;
        printf("Using default settings\n");
    }

    // Apply loaded settings to window state
    settings_apply_to_window_state(&settings);

    // Settings loaded successfully
    printf("Settings loaded: music_volume=%d%%, background_music_enabled=%s\n",
           settings.music_volume, settings.background_music_enabled ? "true" : "false");

    // Re-apply window scaling with the loaded scale factor to ensure consistency
    // This ensures the window size matches the loaded scale factor
    printf("Applying loaded scale factor: %d\n", window_state.scale_factor);
    window_set_scale_factor(window_state.scale_factor);

    printf("Settings initialization complete\n");

    // Initialize isometric renderer
    g_isometric_renderer = isometric_renderer_create(
        window_state.base_width,
        window_state.base_height);
    if (!g_isometric_renderer)
    {
        printf("Failed to create isometric renderer\n");
        return 0;
    }

    // FP renderer is stateless - no initialization needed
    printf("FP renderer support initialized\n");

    window_state.cursor_x = window_state.base_width / 2;
    window_state.cursor_y = window_state.base_height / 2;
    window_state.custom_cursor_active = false;

    if (!spirit_sprite_init(window_state.renderer))
    {
        printf("Warning: spirit sprite textures failed to load\n");
    }
    if (!item_icon_init(window_state.renderer))
    {
        printf("Warning: item icon textures failed to load\n");
    }

    return 1;
}

void window_cleanup()
{
    gamepad_shutdown();
    item_icon_shutdown();
    spirit_sprite_shutdown();
    mob_models_shutdown();
    poly_mesh_shutdown();

    if (g_isometric_renderer)
    {
        isometric_renderer_destroy(g_isometric_renderer);
        g_isometric_renderer = NULL;
    }

    if (window_state.font)
    {
        TTF_CloseFont(window_state.font);
    }
    if (window_state.base_render_texture)
    {
        SDL_DestroyTexture(window_state.base_render_texture);
    }
    if (window_state.renderer)
    {
        SDL_DestroyRenderer(window_state.renderer);
    }
    if (window_state.window)
    {
        SDL_DestroyWindow(window_state.window);
    }
    TTF_Quit();
    SDL_Quit();
}

// Toggle between isometric and first-person rendering modes
void window_toggle_render_mode()
{
    g_fp_mode_enabled = !g_fp_mode_enabled;
    printf("Rendering mode switched to: %s\n", g_fp_mode_enabled ? "First-Person" : "Isometric");
}

void window_set_mouse_look_active(bool active)
{
    if (active == g_mouse_look_active)
        return;

    g_mouse_look_active = active;
    g_mouse_look_dx = 0;
    g_mouse_look_dy = 0;
    SDL_SetRelativeMouseMode(active ? SDL_TRUE : SDL_FALSE);

    if (active)
    {
        // Relative mode stops reporting an absolute position, so park the virtual cursor on the
        // crosshair; UI that reads it (aim, picking) then stays consistent with what is drawn.
        window_state.cursor_x = window_state.base_width / 2;
        window_state.cursor_y = window_state.base_height / 2;
    }
}

bool window_mouse_look_active(void)
{
    return g_mouse_look_active;
}

void window_consume_mouse_look_delta(int *dx, int *dy)
{
    if (dx)
        *dx = g_mouse_look_dx;
    if (dy)
        *dy = g_mouse_look_dy;
    g_mouse_look_dx = 0;
    g_mouse_look_dy = 0;
}

// Get current rendering mode
bool window_is_fp_mode()
{
    return g_fp_mode_enabled;
}

void window_clear()
{
    // Set the base render texture as the target
    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);

    SDL_SetRenderDrawColor(window_state.renderer,
                           window_state.background_color.r,
                           window_state.background_color.g,
                           window_state.background_color.b,
                           window_state.background_color.a);
    SDL_RenderClear(window_state.renderer);
}

void window_present()
{
    // Paint ephemeral toasts onto the base texture so they scale with the rest of the UI.
    // Drawing after the upscale left them at the raw 12px font size on the window surface.
    extern GameState *g_game_state;
    if (g_game_state && g_game_state->toast_active && g_game_state->toast_text[0] != '\0') {
        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - g_game_state->toast_start_ms;
        Uint32 hold_ms = g_game_state->toast_duration_ms > 1000
                             ? g_game_state->toast_duration_ms - 1000
                             : 3000;
        const Uint32 anim_ms = 1000;
        if (elapsed <= hold_ms + anim_ms) {
            Uint8 alpha = 255;
            int y_offset = 0;
            if (elapsed > hold_ms) {
                float t = (float)(elapsed - hold_ms) / (float)anim_ms; // 0..1
                alpha = (Uint8)((1.0f - t) * 255.0f);
                y_offset = (int)(t * 12.0f);
            }
            SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);
            int text_w = 0, text_h = 0;
            window_measure_text(g_game_state->toast_text, &text_w, &text_h);
            int centered_x = (window_state.base_width - text_w) / 2;
            if (centered_x < 4)
                centered_x = 4;
            int base_y = 4 - y_offset;
            SDL_Color col = {255, 255, 255, alpha};
            SDL_Color shadow = {0, 0, 0, alpha};
            window_render_text(g_game_state->toast_text, centered_x + 1, base_y + 1, shadow);
            window_render_text(g_game_state->toast_text, centered_x, base_y, col);
        } else {
            g_game_state->toast_active = false;
            g_game_state->toast_text[0] = '\0';
        }
    }

    // Set the main renderer as the target
    SDL_SetRenderTarget(window_state.renderer, NULL);

    // Clear the main renderer
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 255);
    SDL_RenderClear(window_state.renderer);

    // Center the rendering for both fullscreen and windowed modes
    // Re-apply viewport centering after a resize only after content is (re)rendered
    if (g_needs_recentering) {
        window_center_rendering();
        g_needs_recentering = false;
    } else {
        window_center_rendering();
    }

    // Copy the base texture to the main renderer with cubic scaling
    SDL_Rect dest_rect = {0, 0, window_state.width, window_state.height};
    SDL_RenderCopy(window_state.renderer, window_state.base_render_texture, NULL, &dest_rect);

    // Present the final result
    SDL_RenderPresent(window_state.renderer);
}

// Measure text in pixels using current font
void window_measure_text(const char *text, int *out_w, int *out_h)
{
    if (!text || !window_state.font) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    int w = 0, h = 0;
    if (TTF_SizeText(window_state.font, text, &w, &h) != 0) {
        w = 0; h = 0;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

// Render text at position
void window_render_text(const char *text, int x, int y, SDL_Color color)
{
    if (!window_state.font)
        return;

    SDL_Surface *surface = TTF_RenderText_Solid(window_state.font, text, color);
    if (!surface)
        return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(window_state.renderer, surface);
    if (!texture)
    {
        SDL_FreeSurface(surface);
        return;
    }

    // Set alpha blending for the texture
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, color.a);

    // Render to base texture at base coordinates (no scaling needed)
    SDL_Rect dest = {x, y, surface->w, surface->h};
    SDL_RenderCopy(window_state.renderer, texture, NULL, &dest);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

// Render text with background
void window_render_text_box(const char *text, int x, int y, int width, int height, SDL_Color bg_color, SDL_Color text_color)
{
    // Draw background
    SDL_Rect bg_rect = {x, y, width, height};
    SDL_SetRenderDrawColor(window_state.renderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_RenderFillRect(window_state.renderer, &bg_rect);

    // Draw border
    SDL_SetRenderDrawColor(window_state.renderer, text_color.r, text_color.g, text_color.b, text_color.a);
    SDL_RenderDrawRect(window_state.renderer, &bg_rect);

    // Draw wrapped text
    window_render_wrapped_text(text, x + 5, y + 5, width - 10, text_color);
}

void window_render_wrapped_text(const char *text, int x, int y, int max_width, SDL_Color color)
{
    if (!text)
        return;

    char *text_copy = strdup(text);
    char *line = strtok(text_copy, "\n");
    int current_y = y;

    while (line)
    {
        char *word = strtok(line, " ");
        int current_x = x;
        char current_line[256] = "";

        while (word)
        {
            char test_line[256];
            if (strlen(current_line) == 0)
            {
                strcpy(test_line, word);
            }
            else
            {
                snprintf(test_line, sizeof(test_line), "%s %s", current_line, word);
            }

            // Check if this line would exceed max_width (base coordinates)
            int text_width = strlen(test_line) * window_state.cell_width;

            if (text_width > max_width && strlen(current_line) > 0)
            {
                // Render current line and start new line
                window_render_text(current_line, current_x, current_y, color);
                current_y += (window_state.cell_height + 2);
                strcpy(current_line, word);
            }
            else
            {
                strcpy(current_line, test_line);
            }

            word = strtok(NULL, " ");
        }

        // Render the last line
        if (strlen(current_line) > 0)
        {
            window_render_text(current_line, current_x, current_y, color);
            current_y += (window_state.cell_height + 2);
        }

        line = strtok(NULL, "\n");
    }

    free(text_copy);
}

void window_render_help_modal()
{
    // Semi-transparent overlay
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 128);
    SDL_Rect overlay = {0, 0, window_state.width, window_state.height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    // Help panel
    int panel_width = 300;
    int panel_height = 500;
    int panel_x = (window_state.base_width - panel_width) / 2;
    int panel_y = (window_state.base_height - panel_height) / 2;

    // Draw panel background
    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Help title
    window_render_text("Controls", panel_x + panel_width / 2 - 40, panel_y + 20, window_state.highlight_color);

    // Help content
    const char *help_text = "Action RPG Controls:\n\n"
                            "W - Move forward (momentum)\n"
                            "A/D - Strafe left/right\n"
                            "S - Move backward\n"
                            "Space - Up strafe / jump (stamina)\n"
                            "Ctrl - Crouch (ground) / down strafe (air)\n"
                            "Shift - Run (ground) / afterburner (flight)\n"
                            "Mouse - Aim / facing direction\n"
                            "Left click - Select / inspect voxel\n"
                            "Right click - Queue attack-move\n"
                            "1-4 - Use assigned skills\n"
                            "  Scroll - Cycle known skills on hotbar\n"
                            "  1 Fireball  2 Dominate  3 Talk\n"
                            "  4 Fly (while inhabiting a bird)\n"
                            "TAB - Character / skill tree\n"
                            "I - Open inventory\n"
                            "TAB - Character profile\n"
                            "M - World map (fog of war)\n"
                            "T - Hold: Town Portal home\n"
                            "Q/E - Bank (roll) left/right in the air\n"
                            "G - Toggle flying/gravity\n"
                            "`/~ - Developer console\n"
                            "  admin - World editor / gravity\n"
                            "F - Loot nearby corpse\n"
                            "F4 - Toggle isometric / first-person\n"
                            "ESC - Menu/Back\n"
                            "Enter - Confirm / advance dialogue\n\n"
                            "Spirit turn speed limits how fast\n"
                            "you rotate toward the cursor.\n"
                            "Inventory is always on the spirit.\n"
                            "Equipment slots exist only on bodies.";

    window_render_wrapped_text(help_text, panel_x + 20, panel_y + 50, panel_width - 40, window_state.text_color);

    // Add close button
    window_clear_buttons();
    int button_width = 80;
    int button_height = 25;
    int button_x = panel_x + (panel_width - button_width) / 2;
    int button_y = panel_y + panel_height - 40;
    window_add_button(button_x, button_y, button_width, button_height, "Close", BUTTON_CLOSE_HELP);
    window_render_buttons();
}

static SDL_Rect g_world_editor_dec = {0};
static SDL_Rect g_world_editor_inc = {0};
static SDL_Rect g_world_editor_bar = {0};
static SDL_Rect g_world_editor_reset = {0};
static SDL_Rect g_world_editor_close = {0};

static const char *world_editor_type_name(WorldGenerationType type)
{
    switch (type)
    {
    case WORLD_TYPE_HOME:
        return "Home";
    case WORLD_TYPE_FARM:
        return "Farm";
    case WORLD_TYPE_RANDOM:
        return "Random";
    case WORLD_TYPE_WILDERNESS:
        return "Wilderness";
    case WORLD_TYPE_SOLID:
        return "Solid";
    case WORLD_TYPE_UNDERWORLD:
        return "Underworld";
    case WORLD_TYPE_SCOURED:
        return "Scoured";
    case WORLD_TYPE_LABYRINTH_SQUARE:
        return "Labyrinth";
    case WORLD_TYPE_WFC_TOWN:
        return "Town";
    case WORLD_TYPE_CLOUD:
        return "Cloud";
    case WORLD_TYPE_ARENA:
        return "Arena";
    default:
        return "Unknown";
    }
}

static void world_editor_fill_rect(SDL_Rect rect, SDL_Color fill, SDL_Color border)
{
    SDL_SetRenderDrawColor(window_state.renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(window_state.renderer, &rect);
    SDL_SetRenderDrawColor(window_state.renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(window_state.renderer, &rect);
}

static bool world_editor_point_in_rect(int x, int y, SDL_Rect rect)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

void window_render_world_editor_modal(const World *world)
{
    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 160);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    const int panel_width = 236;
    const int panel_height = 154;
    const int panel_x = (window_state.base_width - panel_width) / 2;
    const int panel_y = (window_state.base_height - panel_height) / 2;

    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    world_editor_fill_rect(panel_rect, (SDL_Color){40, 44, 52, 255}, (SDL_Color){140, 150, 170, 255});

    window_render_text("World Editor", panel_x + 8, panel_y + 6, window_state.highlight_color);

    char line[96];
    const char *type_name = world ? world_editor_type_name(world->generation_type) : "None";
    if (world)
        snprintf(line, sizeof(line), "Type: %s  %ux%ux%u", type_name,
                 world->width, world->height, world->depth);
    else
        snprintf(line, sizeof(line), "Type: none");
    window_render_text(line, panel_x + 8, panel_y + 22, window_state.text_color);

    if (world && world->seed_id[0])
        snprintf(line, sizeof(line), "Seed: %.12s", world->seed_id);
    else
        snprintf(line, sizeof(line), "Seed: (none)");
    window_render_text(line, panel_x + 8, panel_y + 36, window_state.text_color);

    float gravity = (world && world->gravity > 0.0f) ? world->gravity : GRAVITY_DEFAULT;
    snprintf(line, sizeof(line), "Gravity  %.0f  voxels/s", gravity);
    window_render_text(line, panel_x + 8, panel_y + 54, window_state.highlight_color);

    g_world_editor_dec = (SDL_Rect){panel_x + 8, panel_y + 70, 22, 16};
    g_world_editor_inc = (SDL_Rect){panel_x + panel_width - 30, panel_y + 70, 22, 16};
    g_world_editor_bar = (SDL_Rect){panel_x + 34, panel_y + 72, panel_width - 68, 12};

    world_editor_fill_rect(g_world_editor_dec, (SDL_Color){80, 80, 80, 255}, (SDL_Color){180, 180, 180, 255});
    world_editor_fill_rect(g_world_editor_inc, (SDL_Color){80, 80, 80, 255}, (SDL_Color){180, 180, 180, 255});
    window_render_text("-", g_world_editor_dec.x + 7, g_world_editor_dec.y + 2, window_state.text_color);
    window_render_text("+", g_world_editor_inc.x + 6, g_world_editor_inc.y + 2, window_state.text_color);

    world_editor_fill_rect(g_world_editor_bar, (SDL_Color){24, 24, 28, 255}, (SDL_Color){120, 120, 130, 255});
    float t = (gravity - GRAVITY_MIN) / (GRAVITY_MAX - GRAVITY_MIN);
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    int fill_w = (int)(t * (float)(g_world_editor_bar.w - 2));
    if (fill_w > 0)
    {
        SDL_Rect fill = {g_world_editor_bar.x + 1, g_world_editor_bar.y + 1, fill_w, g_world_editor_bar.h - 2};
        SDL_SetRenderDrawColor(window_state.renderer, 70, 140, 220, 255);
        SDL_RenderFillRect(window_state.renderer, &fill);
    }

    window_render_text("Floaty              Heavy", panel_x + 34, panel_y + 88, window_state.text_color);

    g_world_editor_reset = (SDL_Rect){panel_x + 8, panel_y + panel_height - 24, 54, 16};
    g_world_editor_close = (SDL_Rect){panel_x + panel_width - 54, panel_y + panel_height - 24, 46, 16};
    world_editor_fill_rect(g_world_editor_reset, (SDL_Color){80, 80, 80, 255}, (SDL_Color){180, 180, 180, 255});
    world_editor_fill_rect(g_world_editor_close, (SDL_Color){80, 80, 80, 255}, (SDL_Color){180, 180, 180, 255});
    window_render_text("Reset", g_world_editor_reset.x + 8, g_world_editor_reset.y + 2, window_state.text_color);
    window_render_text("Close", g_world_editor_close.x + 6, g_world_editor_close.y + 2, window_state.text_color);
}

int window_world_editor_modal_hit(int x, int y, float *bar_t)
{
    if (bar_t)
        *bar_t = 0.0f;
    if (world_editor_point_in_rect(x, y, g_world_editor_dec))
        return WINDOW_WORLD_EDITOR_HIT_GRAVITY_DEC;
    if (world_editor_point_in_rect(x, y, g_world_editor_inc))
        return WINDOW_WORLD_EDITOR_HIT_GRAVITY_INC;
    if (world_editor_point_in_rect(x, y, g_world_editor_bar))
    {
        if (bar_t && g_world_editor_bar.w > 1)
        {
            float t = (float)(x - g_world_editor_bar.x) / (float)g_world_editor_bar.w;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;
            *bar_t = t;
        }
        return WINDOW_WORLD_EDITOR_HIT_GRAVITY_BAR;
    }
    if (world_editor_point_in_rect(x, y, g_world_editor_reset))
        return WINDOW_WORLD_EDITOR_HIT_RESET;
    if (world_editor_point_in_rect(x, y, g_world_editor_close))
        return WINDOW_WORLD_EDITOR_HIT_CLOSE;
    return WINDOW_WORLD_EDITOR_HIT_NONE;
}

void window_render_name_input(const char *prompt, const char *current_name)
{
    // Semi-transparent overlay
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 128);
    SDL_Rect overlay = {0, 0, window_state.width, window_state.height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    // Input panel - match text dialogue modal padding
    int panel_width = window_state.base_width - 50; // Same padding as text dialogue modal
    int panel_height = 130; // Provide sufficient room so buttons don't overlap the input
    int panel_x = 25; // Same left margin as text dialogue modal
    int panel_y = (window_state.base_height - panel_height) / 2;

    // Draw panel background
    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Title
    window_render_text("Enter Character Name", panel_x + panel_width / 2 - 80, panel_y + 20, window_state.highlight_color);

    // Prompt
    window_render_text(prompt, panel_x + 20, panel_y + 45, window_state.text_color);

    // Current name (input field)
    int ih = window_state.cell_height + 2; if (ih < 14) ih = 14; // ensure minimum height
    SDL_Rect input_rect = {panel_x + 20, panel_y + 60, panel_width - 40, ih}; // height matches font + padding
    SDL_SetRenderDrawColor(window_state.renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(window_state.renderer, &input_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 150, 150, 150, 255);
    SDL_RenderDrawRect(window_state.renderer, &input_rect);

    // Display current name
    const char *display_name = current_name ? current_name : "";
    int text_y = input_rect.y + 1; // fixed 1px top padding (matches visual top spacing)
    if (text_y < input_rect.y) text_y = input_rect.y;
    window_render_text(display_name, input_rect.x + 3, text_y, window_state.text_color); // tighter left padding

    // Add buttons
    window_clear_buttons();

    int button_width = 80;
    int button_height = 25;
    int button_y = panel_y + panel_height - 36; // comfortable spacing above buttons

    int cancel_x = panel_x + 30;
    window_add_button(cancel_x, button_y, button_width, button_height, "Cancel", BUTTON_CANCEL_EXIT);

    int confirm_x = panel_x + panel_width - button_width - 30;
    window_add_button(confirm_x, button_y, button_width, button_height, "Confirm", BUTTON_CONFIRM_NAME);

    window_render_buttons();
}

void window_render_chapter(const char *chapter_title, const char *chapter_content)
{
    // For the simple chapter render, show all text immediately
    window_render_chapter_with_fade(chapter_title, chapter_content, 1.0f, strlen(chapter_content), strlen(chapter_content));
}

void window_render_chapter_with_fade(const char *chapter_title, const char *chapter_content, float fade_alpha, int current_char, int total_chars)
{
    // Clamp fade_alpha to valid range
    if (fade_alpha < 0.0f)
        fade_alpha = 0.0f;
    if (fade_alpha > 1.0f)
        fade_alpha = 1.0f;

    // Calculate alpha values for different elements
    int overlay_alpha = (int)(128 * fade_alpha);
    int panel_alpha = (int)(255 * fade_alpha);
    int border_alpha = (int)(255 * fade_alpha);

    // Semi-transparent overlay with fade
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, overlay_alpha);
    SDL_Rect overlay = {0, 0, window_state.width, window_state.height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    // Chapter panel
    int panel_width = 240;  // Reduced to fit within base width
    int panel_height = 200; // Reduced to fit within base height
    int panel_x = (window_state.base_width - panel_width) / 2;
    int panel_y = (window_state.base_height - panel_height) / 2;

    // Draw panel background with fade
    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 60, 60, 60, panel_alpha);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 100, 100, 100, border_alpha);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Create faded colors for text
    SDL_Color faded_highlight = window_state.highlight_color;
    SDL_Color faded_text = window_state.text_color;
    faded_highlight.a = (Uint8)(faded_highlight.a * fade_alpha);
    faded_text.a = (Uint8)(faded_text.a * fade_alpha);

    // Chapter title with fade
    window_render_text(chapter_title, panel_x + panel_width / 2 - (strlen(chapter_title) * window_state.cell_width) / 2,
                       panel_y + 20, faded_highlight);

    // Chapter content with character-by-character streaming
    char filtered_content[1024];
    int filtered_len = 0;

    // Only show characters up to current_char
    for (int i = 0; i < current_char && chapter_content[i] != '\0'; i++)
    {
        filtered_content[filtered_len++] = chapter_content[i];
    }
    filtered_content[filtered_len] = '\0';

    // Render the filtered content with wrapping
    window_render_wrapped_text(filtered_content, panel_x + 20, panel_y + 50, panel_width - 40, faded_text);

    // Add close button (only show when fully faded in and text is complete)
    if (fade_alpha >= 1.0f && current_char >= total_chars)
    {
        window_clear_buttons();
        int button_width = 80;
        int button_height = 25;
        int button_x = panel_x + (panel_width - button_width) / 2;
        int button_y = panel_y + panel_height - 40;
        window_add_button(button_x, button_y, button_width, button_height, "Continue", BUTTON_CLOSE_HELP);
        window_render_buttons();
    }
}

void window_render_scene_text_box(const char *text, int current_char, int total_chars)
{
    window_render_dialogue_box(NULL, text, current_char, -1, total_chars);
}

void window_render_dialogue_box(const char *speaker, const char *text, int current_char,
                                int line_index, int line_count)
{
    if (!text)
        text = "";

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);

    const bool storyline_overlay = (line_index < 0);
    extern GameState *g_game_state;
    const bool show_choices = !storyline_overlay && g_game_state &&
                              dialogue_awaiting_choice(&g_game_state->dialogue);
    const int choice_n = show_choices ? dialogue_choice_count(&g_game_state->dialogue) : 0;

    if (storyline_overlay)
    {
        SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 96);
        SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
        SDL_RenderFillRect(window_state.renderer, &overlay);
    }

    const int panel_width = window_state.base_width - 16;
    const int panel_height = (speaker && speaker[0] ? 88 : 78) + (choice_n > 0 ? choice_n * 12 + 8 : 0);
    const int panel_x = 8;
    const int panel_y = 8;

    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 28, 26, 36, 230);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 180, 200, 170, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    char progress_text[64];
    if (storyline_overlay)
        snprintf(progress_text, sizeof(progress_text), "%d/%d", current_char, line_count);
    else
        snprintf(progress_text, sizeof(progress_text), "%d/%d",
                 line_index + 1, line_count > 0 ? line_count : 1);
    window_render_text(progress_text, panel_x + panel_width - 48, panel_y + 6, window_state.ui_color);

    int text_y = panel_y + 18;
    if (speaker && speaker[0])
    {
        window_render_text(speaker, panel_x + 10, panel_y + 8, window_state.highlight_color);
        text_y = panel_y + 22;
    }

    char filtered_text[1024];
    int filtered_len = 0;
    for (int i = 0; i < current_char && text[i] != '\0' && filtered_len < (int)sizeof(filtered_text) - 1; i++)
        filtered_text[filtered_len++] = text[i];
    filtered_text[filtered_len] = '\0';

    window_render_wrapped_text(filtered_text, panel_x + 10, text_y, panel_width - 20, window_state.text_color);

    if (show_choices)
    {
        int cy = panel_y + panel_height - 14 - choice_n * 12;
        if (cy < text_y + 20)
            cy = text_y + 20;
        for (int i = 0; i < choice_n; i++)
        {
            char row[80];
            const bool sel = (i == dialogue_selected_choice(&g_game_state->dialogue));
            snprintf(row, sizeof(row), "%s%d. %s", sel ? "> " : "  ", i + 1,
                     dialogue_choice_label(&g_game_state->dialogue, i));
            window_render_text(row, panel_x + 10, cy + i * 12,
                               sel ? window_state.highlight_color : window_state.text_color);
        }
    }
    else
    {
        const char *hint = storyline_overlay ? "Press Enter to continue..." : "Enter / Talk to continue";
        if (!storyline_overlay && g_game_state &&
            player_controls_town_portal_charging(&g_game_state->controls))
            hint = "Keep holding T...";
        window_render_text(hint, panel_x + 10, panel_y + panel_height - 14, window_state.ui_color);
    }
}

void window_render_nav_aide(GameState *state)
{
    if (!state || !state->mission_tracked || !nav_aide_active(&state->nav_aide) || !state->current_world)
        return;
    if (!state->game_started || state->current_screen != GAME_SCREEN_WORLD)
        return;

    float bearing = 0.0f;
    if (!nav_aide_world_bearing(&state->nav_aide, state->player_universe_x, state->player_universe_y,
                               state->player_universe_z, state->player_world_x, state->player_world_y,
                               state->current_world->width, state->current_world->height, &bearing))
        return;

    // FP aims with camera yaw; isometric uses the spirit's facing.
    float facing = state->controls.facing_yaw;
    if (window_is_fp_mode())
        facing = state->controls.aim_yaw;

    const float relative = nav_aide_relative_yaw(bearing, facing);
    nav_aide_render(window_state.renderer, window_state.base_width, window_state.base_height, relative,
                    state->nav_aide.label);

    float distance_m = 0.0f;
    if (nav_aide_planar_distance(&state->nav_aide, state->player_universe_x, state->player_universe_y,
                                 state->player_universe_z, state->player_world_x, state->player_world_y,
                                 state->current_world->width, state->current_world->height, &distance_m))
    {
        char dist_text[16];
        if (nav_aide_format_distance(distance_m, dist_text, sizeof(dist_text)))
        {
            // Sit just under the chevron (same center as nav_aide_render).
            const int cx = window_state.base_width / 2;
            const int cy = window_state.base_height / 2 - 30;
            const int cell_w = window_state.cell_width > 0 ? window_state.cell_width : 8;
            const int text_x = cx - ((int)strlen(dist_text) * cell_w) / 2;
            const int text_y = cy + 14;
            SDL_Color shadow = {0, 0, 0, 200};
            SDL_Color gold = {255, 224, 96, 255};
            window_render_text(dist_text, text_x + 1, text_y + 1, shadow);
            window_render_text(dist_text, text_x, text_y, gold);
        }
    }
}

static int journal_estimate_wrap_lines(const char *text, int max_width)
{
    if (!text || !text[0])
        return 0;
    int cell_w = window_state.cell_width > 0 ? window_state.cell_width : 6;
    int cols = max_width / cell_w;
    if (cols < 8)
        cols = 8;
    // Word-aware estimate matching window_render_wrapped_text's wrap behaviour.
    int lines = 1;
    int line_len = 0;
    const char *p = text;
    while (*p)
    {
        if (*p == '\n')
        {
            lines++;
            line_len = 0;
            p++;
            continue;
        }
        const char *start = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        int word_len = (int)(p - start);
        if (line_len > 0 && line_len + 1 + word_len > cols)
        {
            lines++;
            line_len = word_len;
        }
        else if (line_len == 0)
            line_len = word_len;
        else
            line_len += 1 + word_len;
        if (*p == ' ')
            p++;
    }
    return lines > 0 ? lines : 1;
}

static int journal_quest_block_height(const StorylineQuest *quest, int text_w, int line_h)
{
    if (!quest)
        return line_h;
    int h = line_h + 2; // title
    if (quest->description && quest->description[0])
        h += journal_estimate_wrap_lines(quest->description, text_w) * line_h + 4;
    h += line_h; // "Objectives" / status
    int obj_lines = quest->objectives_count;
    if (obj_lines > STORYLINE_MAX_OBJECTIVES)
        obj_lines = STORYLINE_MAX_OBJECTIVES;
    if (obj_lines < 1)
        obj_lines = 1;
    h += obj_lines * line_h;
    h += line_h + 6; // progress + gap
    return h;
}

static int journal_draw_quest_block(const StorylineQuest *quest, int x, int y, int text_w, int line_h,
                                    bool completed_entry)
{
    if (!quest)
        return y;

    const char *name = quest->name && quest->name[0] ? quest->name : "Untitled Mission";
    char title[192];
    if (completed_entry)
        snprintf(title, sizeof(title), "%s  [done]", name);
    else
        snprintf(title, sizeof(title), "%s", name);
    SDL_Color title_col = completed_entry ? (SDL_Color){140, 180, 140, 255} : window_state.highlight_color;
    window_render_text(title, x, y, title_col);
    y += line_h + 2;

    if (quest->description && quest->description[0])
    {
        window_render_wrapped_text(quest->description, x, y, text_w, window_state.text_color);
        y += journal_estimate_wrap_lines(quest->description, text_w) * line_h + 4;
    }

    window_render_text("Objectives", x, y, window_state.ui_color);
    y += line_h;

    if (quest->objectives_count <= 0)
    {
        window_render_text("(no objectives)", x, y, window_state.text_color);
        y += line_h;
    }
    else
    {
        for (int i = 0; i < quest->objectives_count && i < STORYLINE_MAX_OBJECTIVES; i++)
        {
            const StorylineObjective *obj = &quest->objectives[i];
            bool done = obj->progress >= obj->required && obj->required > 0;
            char line[160];
            const char *type = obj->type && obj->type[0] ? obj->type : "do";
            const char *target = obj->target && obj->target[0] ? obj->target : "objective";
            if (obj->required > 1)
                snprintf(line, sizeof(line), "%s %s  %d/%d", type, target, obj->progress, obj->required);
            else
                snprintf(line, sizeof(line), "%s %s%s", type, target, done ? "  [done]" : "");
            SDL_Color col = done ? (SDL_Color){80, 200, 100, 255} : window_state.text_color;
            window_render_text(line, x, y, col);
            y += line_h;
        }
    }

    char progress[64];
    snprintf(progress, sizeof(progress), "Progress: %d/%d", quest->completed_objectives,
             quest->objectives_count > 0 ? quest->objectives_count : 0);
    window_render_text(progress, x, y, window_state.ui_color);
    y += line_h + 6;
    return y;
}

static SDL_Rect g_journal_track_hit = {0, 0, 0, 0};
static bool g_journal_track_hit_valid = false;

bool window_quest_journal_hit_track(int base_x, int base_y)
{
    if (!g_journal_track_hit_valid || g_journal_track_hit.w <= 0)
        return false;
    return base_x >= g_journal_track_hit.x && base_x < g_journal_track_hit.x + g_journal_track_hit.w &&
           base_y >= g_journal_track_hit.y && base_y < g_journal_track_hit.y + g_journal_track_hit.h;
}

static void journal_draw_track_button(int x, int y, int w, int h, bool mission_tracked)
{
    g_journal_track_hit = (SDL_Rect){x, y, w, h};
    g_journal_track_hit_valid = true;

    SDL_SetRenderDrawColor(window_state.renderer, 70, 70, 90, 255);
    SDL_RenderFillRect(window_state.renderer, &g_journal_track_hit);
    SDL_SetRenderDrawColor(window_state.renderer, 180, 180, 200, 255);
    SDL_RenderDrawRect(window_state.renderer, &g_journal_track_hit);

    const char *label = mission_tracked ? "Untrack" : "Track";
    int text_x = x + (w - (int)strlen(label) * window_state.cell_width) / 2;
    int text_y = y + (h - window_state.cell_height) / 2;
    if (text_x < x + 2)
        text_x = x + 2;
    window_render_text(label, text_x, text_y, window_state.text_color);
}

void window_render_quest_journal(const StorylineState *storyline, int *scroll_y, bool mission_tracked)
{
    g_journal_track_hit_valid = false;

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 150);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    const int margin = 8;
    int panel_w = window_state.base_width - margin * 2;
    int panel_h = window_state.base_height - margin * 2;
    int panel_x = margin;
    int panel_y = margin;

    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(window_state.renderer, 42, 42, 52, 255);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 120, 120, 140, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);

    window_render_text("Mission Journal", panel_x + 8, panel_y + 4, window_state.highlight_color);

    const StorylineQuest *active =
        (storyline && storyline_has_active_quest(storyline)) ? storyline_active_quest(storyline) : NULL;

    if (active)
    {
        const int btn_w = 52;
        const int btn_h = 14;
        const int btn_x = panel_x + panel_w - btn_w - 8;
        const int btn_y = panel_y + 3;
        journal_draw_track_button(btn_x, btn_y, btn_w, btn_h, mission_tracked);
        window_render_text("T", btn_x - 12, panel_y + 4, window_state.ui_color);
    }
    else
    {
        window_render_text("J/ESC", panel_x + panel_w - 40, panel_y + 4, window_state.ui_color);
    }

    const int pad = 10;
    const int line_h = window_state.cell_height + 3;
    const int scrollbar_w = 5;
    const int header_h = 20;
    int body_x = panel_x + pad;
    int body_y = panel_y + header_h;
    int body_h = panel_h - header_h - pad;
    int text_w = panel_w - pad * 2 - scrollbar_w - 4;

    int history_count = storyline ? storyline_quest_history_count(storyline) : 0;

    // Measure full content height for scrolling.
    int content_h = 0;
    if (storyline && storyline->chapter_title[0])
        content_h += line_h + 2;
    content_h += line_h; // "Active"
    if (active)
    {
        content_h += line_h; // Nav: tracked/untracked
        content_h += journal_quest_block_height(active, text_w, line_h);
    }
    else
        content_h += line_h * 2;
    content_h += line_h + 2; // "History"
    if (history_count <= 0)
        content_h += line_h;
    else
    {
        for (int i = 0; i < history_count; i++)
        {
            const StorylineQuest *q = storyline_quest_history_entry(storyline, i);
            content_h += journal_quest_block_height(q, text_w, line_h);
        }
    }

    int max_scroll = content_h - body_h;
    if (max_scroll < 0)
        max_scroll = 0;
    int scroll = scroll_y ? *scroll_y : 0;
    if (scroll < 0)
        scroll = 0;
    if (scroll > max_scroll)
        scroll = max_scroll;
    if (scroll_y)
        *scroll_y = scroll;

    SDL_Rect clip = {body_x, body_y, text_w + 2, body_h};
    SDL_RenderSetClipRect(window_state.renderer, &clip);

    int y = body_y - scroll;

    if (storyline && storyline->chapter_title[0])
    {
        char chapter_line[160];
        snprintf(chapter_line, sizeof(chapter_line), "Chapter: %s", storyline->chapter_title);
        window_render_text(chapter_line, body_x, y, window_state.ui_color);
        y += line_h + 2;
    }

    window_render_text("Active", body_x, y, window_state.highlight_color);
    y += line_h;
    if (active)
    {
        if (mission_tracked)
            window_render_text("Nav: tracked", body_x, y, (SDL_Color){255, 224, 96, 255});
        else
            window_render_text("Nav: untracked", body_x, y, window_state.ui_color);
        y += line_h;
        y = journal_draw_quest_block(active, body_x, y, text_w, line_h, false);
    }
    else
    {
        window_render_wrapped_text("No active mission.", body_x, y, text_w, window_state.text_color);
        y += line_h * 2;
    }

    window_render_text("History", body_x, y, window_state.highlight_color);
    y += line_h + 2;
    if (history_count <= 0)
    {
        window_render_text("No completed missions yet.", body_x, y, window_state.text_color);
    }
    else
    {
        // Most recent first (storage is oldest-first).
        for (int i = history_count - 1; i >= 0; i--)
        {
            const StorylineQuest *q = storyline_quest_history_entry(storyline, i);
            y = journal_draw_quest_block(q, body_x, y, text_w, line_h, true);
        }
    }

    SDL_RenderSetClipRect(window_state.renderer, NULL);

    // Scrollbar when content overflows the panel body.
    if (max_scroll > 0)
    {
        int track_x = panel_x + panel_w - pad - scrollbar_w;
        int track_y = body_y;
        int track_h = body_h;
        SDL_Rect track = {track_x, track_y, scrollbar_w, track_h};
        SDL_SetRenderDrawColor(window_state.renderer, 30, 30, 38, 255);
        SDL_RenderFillRect(window_state.renderer, &track);

        float visible = (float)body_h / (float)content_h;
        int thumb_h = (int)(track_h * visible);
        if (thumb_h < 10)
            thumb_h = 10;
        if (thumb_h > track_h)
            thumb_h = track_h;
        int thumb_travel = track_h - thumb_h;
        int thumb_y = track_y;
        if (max_scroll > 0 && thumb_travel > 0)
            thumb_y = track_y + (scroll * thumb_travel) / max_scroll;
        SDL_Rect thumb = {track_x, thumb_y, scrollbar_w, thumb_h};
        SDL_SetRenderDrawColor(window_state.renderer, 160, 160, 180, 255);
        SDL_RenderFillRect(window_state.renderer, &thumb);
    }
}

void window_render_legends(const Chronicle *chronicle, int *scroll_y)
{
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 160);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    const int panel_x = 12;
    const int panel_y = 12;
    const int panel_w = window_state.base_width - 24;
    const int panel_h = window_state.base_height - 24;
    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(window_state.renderer, 24, 22, 32, 240);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 180, 200, 170, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);

    window_render_text("Legends (newest first)", panel_x + 8, panel_y + 6,
                       window_state.highlight_color);
    window_render_text("Up/Down  Esc", panel_x + panel_w - 78, panel_y + 6, window_state.ui_color);

    if (!chronicle || chronicle->event_count == 0)
    {
        window_render_text("No chronicles recorded.", panel_x + 8, panel_y + 28,
                           window_state.text_color);
        return;
    }

    int scroll = scroll_y ? *scroll_y : 0;
    if (scroll < 0)
        scroll = 0;
    const int line_h = 12;
    const int body_top = panel_y + 24;
    const int body_h = panel_h - 36;
    const int visible = body_h / line_h;
    const int total = (int)chronicle->event_count;
    int max_scroll = total - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (scroll > max_scroll)
        scroll = max_scroll;
    if (scroll_y)
        *scroll_y = scroll;

    // Newest first: scroll 0 shows the last event at the top.
    char buf[CHRONICLE_LINE_MAX];
    for (int row = 0; row < visible; row++)
    {
        int newest_index = scroll + row; // 0 = most recent
        int idx = total - 1 - newest_index;
        if (idx < 0)
            break;
        chronicle_format_line(chronicle, &chronicle->events[idx], buf, sizeof(buf));
        if (strlen(buf) > 34)
        {
            buf[31] = '.';
            buf[32] = '.';
            buf[33] = '.';
            buf[34] = '\0';
        }
        window_render_text(buf, panel_x + 8, body_top + row * line_h, window_state.text_color);
    }

    if (max_scroll > 0)
    {
        char hint[48];
        snprintf(hint, sizeof(hint), "%d/%d", scroll + 1, max_scroll + 1);
        window_render_text(hint, panel_x + 8, panel_y + panel_h - 12, window_state.ui_color);
    }
}

void window_render_game_log(const GameLog *log)
{
    if (!log)
        return;

    const int line_h = 10;
    const int max_show = 6;
    const int base_x = 6;
    const int base_y = 6; // top-left
    int n = game_log_line_count(log);
    if (n > max_show)
        n = max_show;

    // Newest at top; older lines stack downward.
    for (int i = 0; i < n; i++)
    {
        const char *text = game_log_line(log, i);
        if (!text || !text[0])
            continue;
        GameLogChannel ch = game_log_line_channel(log, i);
        SDL_Color col = window_state.text_color;
        if (ch == GAME_LOG_CHRONICLE)
            col = window_state.highlight_color;
        else if (ch == GAME_LOG_GLOBAL)
        {
            col.r = 140;
            col.g = 200;
            col.b = 255;
            col.a = 255;
        }
        char clipped[40];
        snprintf(clipped, sizeof(clipped), "%s", text);
        if (strlen(clipped) > 38)
        {
            clipped[35] = '.';
            clipped[36] = '.';
            clipped[37] = '.';
            clipped[38] = '\0';
        }
        window_render_text(clipped, base_x, base_y + i * line_h, col);
    }

    if (log->chat_open)
    {
        const int prompt_y = base_y + n * line_h + (n > 0 ? 2 : 0);
        char prompt[GAME_CHAT_INPUT_MAX + 16];
        snprintf(prompt, sizeof(prompt), "Global> %s_", log->chat_input);
        if (strlen(prompt) > 38)
        {
            // Keep the editable tail visible.
            const char *tail = prompt + (strlen(prompt) - 37);
            char shortp[40];
            snprintf(shortp, sizeof(shortp), "..%s", tail);
            window_render_text(shortp, base_x, prompt_y, window_state.highlight_color);
        }
        else
            window_render_text(prompt, base_x, prompt_y, window_state.highlight_color);
    }
}

void window_render_quest_box(const char *quest_name, const char *quest_description, int completed_objectives, int total_objectives)
{
    // Semi-transparent overlay
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 128);
    SDL_Rect overlay = {0, 0, window_state.width, window_state.height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    // Quest box panel - fit within base resolution
    int panel_width = 220;  // Reduced to fit within 256 base width
    int panel_height = 180; // Reduced to fit within 240 base height
    int panel_x = (window_state.base_width - panel_width) / 2;
    int panel_y = (window_state.base_height - panel_height) / 2;

    // Draw panel background
    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Quest title
    window_render_text("QUEST", panel_x + panel_width / 2 - 25, panel_y + 20, window_state.highlight_color);
    window_render_text(quest_name, panel_x + panel_width / 2 - (strlen(quest_name) * window_state.cell_width) / 2,
                       panel_y + 40, window_state.highlight_color);

    // Quest description
    window_render_wrapped_text(quest_description, panel_x + 20, panel_y + 70, panel_width - 40, window_state.text_color);

    // Progress bar
    int bar_width = panel_width - 40;
    int bar_height = 20;
    int bar_x = panel_x + 20;
    int bar_y = panel_y + 150;

    // Background bar
    SDL_Rect bg_bar = {bar_x, bar_y, bar_width, bar_height};
    SDL_SetRenderDrawColor(window_state.renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(window_state.renderer, &bg_bar);

    // Progress bar
    if (total_objectives > 0)
    {
        int progress_width = (bar_width * completed_objectives) / total_objectives;
        SDL_Rect progress_bar = {bar_x, bar_y, progress_width, bar_height};
        SDL_SetRenderDrawColor(window_state.renderer, 34, 139, 34, 255); // Green
        SDL_RenderFillRect(window_state.renderer, &progress_bar);
    }

    // Progress text
    char progress_text[64];
    snprintf(progress_text, sizeof(progress_text), "Progress: %d/%d", completed_objectives, total_objectives);
    window_render_text(progress_text, bar_x, bar_y + bar_height + 5, window_state.ui_color);

    // Instructions
    window_render_text("Press any key to continue...", panel_x + 20, panel_y + panel_height - 30, window_state.ui_color);
}

// Render world grid (X-Z plane for overhead view)
// Alpha blending function
SDL_Color blend_colors(SDL_Color background, SDL_Color foreground)
{
    float alpha = foreground.a / 255.0f;
    float inv_alpha = 1.0f - alpha;

    SDL_Color result;
    result.r = (uint8_t)(background.r * inv_alpha + foreground.r * alpha);
    result.g = (uint8_t)(background.g * inv_alpha + foreground.g * alpha);
    result.b = (uint8_t)(background.b * inv_alpha + foreground.b * alpha);
    result.a = 255; // Final result is always opaque

    return result;
}

// Triangle fill helper used for voxel face rendering
static void fill_triangle_impl(SDL_Renderer *rr, SDL_Point p1, SDL_Point p2, SDL_Point p3, SDL_Color color)
{
    if (p1.y > p2.y) { SDL_Point t = p1; p1 = p2; p2 = t; }
    if (p2.y > p3.y) { SDL_Point t = p2; p2 = p3; p3 = t; }
    if (p1.y > p2.y) { SDL_Point t = p1; p1 = p2; p2 = t; }
    if (p1.y == p3.y) return;
    SDL_SetRenderDrawColor(rr, color.r, color.g, color.b, color.a);
    for (int y = p1.y; y <= p3.y; y++)
    {
        float x_start, x_end;
        if (y <= p2.y)
        {
            float t1 = (p2.y == p1.y) ? 0.0f : (float)(y - p1.y) / (float)(p2.y - p1.y);
            float t2 = (p3.y == p1.y) ? 0.0f : (float)(y - p1.y) / (float)(p3.y - p1.y);
            x_start = p1.x + t1 * (p2.x - p1.x);
            x_end   = p1.x + t2 * (p3.x - p1.x);
        }
        else
        {
            float t1 = (p3.y == p2.y) ? 0.0f : (float)(y - p2.y) / (float)(p3.y - p2.y);
            float t2 = (p3.y == p1.y) ? 0.0f : (float)(y - p1.y) / (float)(p3.y - p1.y);
            x_start = p2.x + t1 * (p3.x - p2.x);
            x_end   = p1.x + t2 * (p3.x - p1.x);
        }
        if (x_start > x_end) { float tmp = x_start; x_start = x_end; x_end = tmp; }
        int xs = (int)(x_start + 0.5f);
        int xe = (int)(x_end + 0.5f);
        SDL_RenderDrawLine(rr, xs, y, xe, y);
    }
}

void window_render_world_grid(World *world, int player_x, int player_y, int player_z)
{
    int grid_size = 14; // visible cells along each axis
    int start_x = player_x - grid_size / 2;
    int start_z = player_z - grid_size / 2;
    int cell_size = 8;
    // Center the grid within the base logical resolution
    int grid_pixel_w = grid_size * cell_size;
    int grid_pixel_h = grid_size * cell_size;
    int offset_x = (window_state.base_width - grid_pixel_w) / 2;
    int offset_y = (window_state.base_height - grid_pixel_h) / 2;

    // Draw grid background
    SDL_Rect grid_rect = {offset_x - 5, offset_y - 5, grid_size * cell_size + 10, grid_size * cell_size + 10};
    SDL_SetRenderDrawColor(window_state.renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(window_state.renderer, &grid_rect);

    // Opacity profile for 5 blended layers around the chosen center height
    int layer_offsets[5] = {-2, -1, 0, 1, 2};
    int opacity_levels[5] = {64, 128, 255, 128, 64};

    // Render each grid cell with proper alpha blending
    for (int z = 0; z < grid_size; z++)
    {
        for (int x = 0; x < grid_size; x++)
        {
            int world_x = start_x + x;
            int world_z = start_z + z;

            SDL_Rect cell_rect = {
                offset_x + x * cell_size,
                offset_y + z * cell_size,
                cell_size - 1,
                cell_size - 1};

            // Start with transparent background (we'll blend layers into this)
            SDL_Color final_color = {0, 0, 0, 0};
            bool has_player = false;
            SDL_Color player_color = {255, 255, 0, 255}; // Yellow for player

            // Determine a sensible center Y for this (x,z): use the highest non-air voxel (ground top)
            int center_y = player_y;
            {
                int top_y = (int)world->height - 1;
                for (int yy = top_y; yy >= 0; yy--) {
                    Voxel *tv = world_get_voxel(world, world_x, (uint32_t)yy, world_z);
                    if (tv && tv->type != VOXEL_AIR) { center_y = yy; break; }
                }
            }

            // Blend all levels around the per-cell center height
            for (int level_idx = 0; level_idx < 5; level_idx++)
            {
                int current_y = center_y + layer_offsets[level_idx];
                int opacity = opacity_levels[level_idx];

                // Skip rendering if level is outside world bounds
                if (current_y < 0 || current_y >= (int)world->height)
                    continue;

                // Check if this is the player position
                if (world_x == player_x && world_z == player_z && current_y == player_y)
                {
                    has_player = true;
                    player_color.a = opacity; // Apply level opacity to player
                }
                else
                {
                    // Get voxel at position for this level
                    Voxel *voxel = world_get_voxel(world, world_x, current_y, world_z);
                    if (voxel && voxel->type != VOXEL_AIR)
                    {
                        uint8_t r=64,g=64,b=64; // fallback
                        world_voxel_type_color(voxel->type, &r, &g, &b);
                        SDL_Color level_color = (SDL_Color){r, g, b, (Uint8)opacity};

                        // Blend this level's color with the accumulated color
                        final_color = blend_colors(final_color, level_color);
                    }
                }
            }

            // If player is present, blend them on top
            if (has_player)
            {
                final_color = blend_colors(final_color, player_color);
            }

            // Render the final blended color
            if (final_color.a > 0)
            {
                SDL_SetRenderDrawColor(window_state.renderer, final_color.r, final_color.g, final_color.b, final_color.a);
                SDL_RenderFillRect(window_state.renderer, &cell_rect);
            }
        }
    }
}

// --- Map framebuffer utilities ---
// Draw the 64x64 overview map in the upper-right corner.
//
// The old version of this allocated a texture, redrew all 16k cells of the world slice as separate
// filled rects, blitted it and destroyed the texture — once per frame, in both render modes. It is
// an overview of a world that is mostly not changing, so the texture now lives across frames and
// its contents are only redrawn when the world, the slice, or the world's voxels differ from what
// is already in it.
static void window_render_cached_minimap(World *world, int player_x, int player_y, int player_z)
{
    if (!world || !window_state.renderer)
        return;

    const MapRenderParams mini_params = {
        .camera_type = MAP_CAMERA_OVERHEAD,
        .width = 64,
        .height = 64,
        .fov_degrees = 0.0f,
        .y_min = 0,
        .y_max = (int)world->height
    };

    static MapFramebuffer mini_fb = {0};
    static World *cached_world = NULL;
    static uint64_t cached_revision = 0;
    static uint64_t cached_fog_revision = 0;
    static int cached_slice_z = -1;
    static int cached_width = 0, cached_height = 0;

    if (mini_fb.texture &&
        (cached_width != mini_params.width || cached_height != mini_params.height))
        window_mapfb_destroy(&mini_fb);

    if (!mini_fb.texture)
    {
        if (!window_mapfb_init(&mini_fb, mini_params.width, mini_params.height))
            return;
        cached_width = mini_params.width;
        cached_height = mini_params.height;
        cached_world = NULL; // force a redraw into the new texture
    }

    extern GameState *g_game_state;
    const FogAtlas *fog = NULL; // Fog of war is map-only; minimap shows full terrain.

    // +1 when fog is active so an empty atlas (revision 0) still differs from fog-off (key 0).
    const uint64_t fog_revision = fog ? fog_atlas_revision(fog) + 1 : 0;

    if (cached_world != world || cached_revision != world->voxel_revision ||
        cached_slice_z != player_z || cached_fog_revision != fog_revision)
    {
        window_render_world_view(world, player_x, player_y, player_z, &mini_params, &mini_fb, fog);
        cached_world = world;
        cached_revision = world->voxel_revision;
        cached_fog_revision = fog_revision;
        cached_slice_z = player_z;
    }

    // Ensure we are drawing to the base texture
    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);
    SDL_SetTextureBlendMode(mini_fb.texture, SDL_BLENDMODE_BLEND);
    SDL_Rect dst = { window_state.base_width - mini_params.width - 6, 6,
                     mini_params.width, mini_params.height };
    SDL_RenderCopy(window_state.renderer, mini_fb.texture, NULL, &dst);
}

bool window_mapfb_init(MapFramebuffer* fb, int width, int height)
{
    if (!fb || !window_state.renderer) return false;
    if (width <= 0 || height <= 0) return false;

    fb->width = width;
    fb->height = height;
    fb->texture = SDL_CreateTexture(window_state.renderer,
                                    SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_TARGET,
                                    width, height);
    if (!fb->texture) return false;
    return true;
}

void window_mapfb_destroy(MapFramebuffer* fb)
{
    if (!fb) return;
    if (fb->texture) {
        SDL_DestroyTexture(fb->texture);
        fb->texture = NULL;
    }
}

// Render a simple overhead map into the framebuffer
static void render_overhead_map(World* world, int player_x, int player_y, int player_z,
                                const MapRenderParams* params, const FogAtlas *fog)
{
    const int w = params->width;
    const int h = params->height;
    if (!world || w <= 0 || h <= 0) return;

    // Clear to dark background
    SDL_SetRenderDrawColor(window_state.renderer, 20, 22, 35, 255);
    SDL_RenderClear(window_state.renderer);

    // Fit entire world X,Y into framebuffer
    float scale_x = (float)w / (float)world->width;
    float scale_y = (float)h / (float)world->height;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale < 1.0f) scale = 1.0f;

    // Render a single Z-slice (match layer_viewer behavior) using player_z as the slice index
    int slice_z = player_z;
    if (slice_z < 0) slice_z = 0;
    if (slice_z >= (int)world->depth) slice_z = (int)world->depth - 1;

    for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
            SDL_Color c = {0, 0, 0, 255};
            if (!fog || fog_is_explored(fog, world, x, y, (uint32_t)slice_z)) {
                Voxel* v = world_get_voxel(world, x, y, (uint32_t)slice_z);
                c = (SDL_Color){25, 25, 50, 255};
                if (v) {
                    switch (v->type) {
                        case VOXEL_GRASS: c = (SDL_Color){90,170,50,255}; break;
                        case VOXEL_SOIL:  c = (SDL_Color){120,80,40,255}; break;
                        case VOXEL_STONE: c = (SDL_Color){110,110,110,255}; break;
                        case VOXEL_SAND:  c = (SDL_Color){194,178,128,255}; break;
                        case VOXEL_WATER: c = (SDL_Color){50,100,200,255}; break;
                        case VOXEL_WOOD:  c = (SDL_Color){130,80,40,255}; break;
                        case VOXEL_LEAVES:c = (SDL_Color){60,140,60,255}; break;
                        default: break;
                    }
                }
            }
            SDL_SetRenderDrawColor(window_state.renderer, c.r, c.g, c.b, 255);
            int sx = (int)(x * scale);
            int sy = (int)(y * scale);
            SDL_Rect r = { sx, sy, (int)ceilf(scale), (int)ceilf(scale) };
            SDL_RenderFillRect(window_state.renderer, &r);
        }
    }

    // Draw grid lines like layer_viewer for readability
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 40);
    int cell_w = (int)ceilf(scale);
    int cell_h = (int)ceilf(scale);
    for (uint32_t gx = 0; gx <= world->width; gx++) {
        int sx = (int)(gx * cell_w);
        SDL_RenderDrawLine(window_state.renderer, sx, 0, sx, h);
    }
    for (uint32_t gy = 0; gy <= world->height; gy++) {
        int sy = (int)(gy * cell_h);
        SDL_RenderDrawLine(window_state.renderer, 0, sy, w, sy);
    }

    // Draw player marker
    SDL_SetRenderDrawColor(window_state.renderer, 255, 0, 0, 255);
    int px = (int)(player_x * scale);
    int py = (int)(player_y * scale);
    SDL_RenderDrawLine(window_state.renderer, px-2, py, px+2, py);
    SDL_RenderDrawLine(window_state.renderer, px, py-2, px, py+2);
}

void window_render_map(World* world,
                       int player_x, int player_y, int player_z,
                       const MapRenderParams* params,
                       MapFramebuffer* fb,
                       const FogAtlas *fog)
{
    if (!world || !params || !fb || !fb->texture) return;
    // Bind framebuffer as render target
    SDL_SetRenderTarget(window_state.renderer, fb->texture);

    switch (params->camera_type) {
        case MAP_CAMERA_OVERHEAD:
            render_overhead_map(world, player_x, player_y, player_z, params, fog);
            break;
        case MAP_CAMERA_ISOMETRIC:
            // TODO: could render isometric miniature using isometric_renderer
            render_overhead_map(world, player_x, player_y, player_z, params, fog);
            break;
        case MAP_CAMERA_ACTOR:
            // TODO: actor-centric top-down around player
            render_overhead_map(world, player_x, player_y, player_z, params, fog);
            break;
    }

    // Restore default render target
    SDL_SetRenderTarget(window_state.renderer, NULL);
}

bool window_mapfb_read_pixels(MapFramebuffer* fb, void* dst_pixels, int dst_pitch_bytes)
{
    if (!fb || !fb->texture || !dst_pixels) return false;
    // Read pixels in ARGB8888
    if (SDL_SetRenderTarget(window_state.renderer, fb->texture) != 0) return false;
    bool ok = (SDL_RenderReadPixels(window_state.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                    dst_pixels, dst_pitch_bytes) == 0);
    SDL_SetRenderTarget(window_state.renderer, NULL);
    return ok;
}

// Unified world renderer (framebuffer-based). Supports overhead now; isometric path draws via iso renderer.
void window_render_world_view(World* world,
                              int player_x, int player_y, int player_z,
                              const MapRenderParams* params,
                              MapFramebuffer* fb,
                              const FogAtlas *fog)
{
    if (!world || !params || !fb || !fb->texture) return;

    SDL_SetRenderTarget(window_state.renderer, fb->texture);
    SDL_SetRenderDrawColor(window_state.renderer, 25, 25, 50, 255);
    SDL_RenderClear(window_state.renderer);

    if (params->camera_type == MAP_CAMERA_OVERHEAD) {
        render_overhead_map(world, player_x, player_y, player_z, params, fog);
    } else if (params->camera_type == MAP_CAMERA_ISOMETRIC) {
        // Use isometric renderer to draw the full 3D scene
        if (g_isometric_renderer) {
            float ax=player_x, ay=player_y, az=player_z;
            extern GameState *g_game_state;
            if (g_game_state) {
                game_state_sync_isometric_renderer(g_game_state, g_isometric_renderer);
                isometric_renderer_set_game_worlds(g_isometric_renderer, g_game_state->game_worlds);
                isometric_renderer_set_auto_center(g_isometric_renderer, false);
                game_state_get_animated_player_position(g_game_state, &ax, &ay, &az);
            }
            (void)ax;
            (void)ay;
            (void)az;
            // Temporarily point renderer at the provided world
            GameWorlds *gw = g_isometric_renderer->game_worlds;
            World *saved_home = NULL;
            GameWorlds tmp = {0};
            bool used_tmp = false;
            if (gw) {
                saved_home = gw->home_world;
                gw->home_world = world;
            } else {
                tmp.home_world = world;
                isometric_renderer_set_game_worlds(g_isometric_renderer, &tmp);
                used_tmp = true;
            }
            isometric_renderer_render_gpu(g_isometric_renderer, window_state.renderer);
            if (gw) {
                gw->home_world = saved_home;
            }
            if (used_tmp) {
                // Restore to NULL to avoid dangling pointer
                isometric_renderer_set_game_worlds(g_isometric_renderer, NULL);
            }
        }
    } else if (params->camera_type == MAP_CAMERA_ACTOR) {
        // For now actor == overhead centered around player; can be extended later
        MapRenderParams local = *params;
        render_overhead_map(world, player_x, player_y, player_z, &local, fog);
    }

    SDL_SetRenderTarget(window_state.renderer, NULL);
}

// Render UI elements
void window_render_ui_panel(const char *title, const char *content, int x, int y, int width, int height)
{
    SDL_Color panel_bg = {60, 60, 60, 255};
    SDL_Color panel_border = {100, 100, 100, 255};

    // Draw panel background
    SDL_Rect panel_rect = {x, y, width, height};
    SDL_SetRenderDrawColor(window_state.renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);

    // Draw panel border
    SDL_SetRenderDrawColor(window_state.renderer, panel_border.r, panel_border.g, panel_border.b, panel_border.a);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Draw title
    window_render_text(title, x + 10, y + 10, window_state.ui_color);

    // Draw content
    window_render_text(content, x + 10, y + 40, window_state.text_color);
}

// Render main menu
void window_render_main_menu()
{
    window_clear();

    // Background: render the universe's origin world (0,0,0) in isometric view behind the menu
    extern GameState *g_game_state;
    if (g_game_state) {
        World* origin = universe_get(&g_game_state->universe, 0, 0, 0);
        if (!origin) origin = g_game_state->main_menu_world;
        if (origin) {
            // Prefer the surface spawn the game state already resolved — z=0 on a deep world
            // forces a full-height column scan for no visual gain on the menu.
            int px = g_game_state->player_x;
            int py = g_game_state->player_y;
            int pz = g_game_state->player_z;
            if (px < 0 || py < 0 || pz < 0 ||
                px >= (int)origin->width || py >= (int)origin->height || pz >= (int)origin->depth)
            {
                px = (int)(origin->width / 2);
                py = (int)(origin->height / 2);
                pz = world_height_at_cached(origin, px, py);
                if (pz < 0)
                    pz = 0;
            }
            window_render_isomorphic_world(origin, px, py, pz, "Origin");
        }
    }

    // Title - use base coordinates since text rendering handles scaling
    window_render_text("VERSE", window_state.base_width / 2 - 25, 20, window_state.highlight_color);
    window_render_text("A robust rogue-like RPG", window_state.base_width / 2 - 50, 35, window_state.text_color);

    // Remember currently selected button ID before clearing
    int previously_selected_button_id = 0;
    for (int i = 0; i < window_state.button_count; i++)
    {
        if (window_state.buttons[i].selected)
        {
            previously_selected_button_id = window_state.buttons[i].id;
            break;
        }
    }

    // Clear existing buttons and add menu buttons
    window_clear_buttons();

    int menu_y = 80;
    int button_width = 100;
    int button_height = 20;
    int button_x = window_state.base_width / 2 - button_width / 2;

    // Directory scan once until a save is written or deleted, not every paint.
    if (g_saves_exist_cached < 0)
        g_saves_exist_cached = character_any_saves_exist() ? 1 : 0;
    bool saves_exist = g_saves_exist_cached != 0;

    int current_y = menu_y;

    if (saves_exist)
    {
        window_add_button(button_x, current_y, button_width, button_height, "Continue", BUTTON_CONTINUE);
        current_y += 25;
    }

    window_add_button(button_x, current_y, button_width, button_height, "New Game", BUTTON_NEW_GAME);
    current_y += 25;

    if (saves_exist)
    {
        window_add_button(button_x, current_y, button_width, button_height, "Load Game", BUTTON_LOAD_GAME);
        current_y += 25;
    }
    window_add_button(button_x, current_y, button_width, button_height, "Settings", BUTTON_SETTINGS);
    current_y += 25;
    // Song Editor button removed - now standalone program
    current_y += 25;
    window_add_button(button_x, current_y, button_width, button_height, "Exit", BUTTON_EXIT);

    // Restore previous selection if it still exists, otherwise select first button
    bool selection_restored = false;
    if (previously_selected_button_id != 0)
    {
        for (int i = 0; i < window_state.button_count; i++)
        {
            if (window_state.buttons[i].id == previously_selected_button_id)
            {
                window_state.buttons[i].selected = true;
                selection_restored = true;
                break;
            }
        }
    }

    // If no previous selection or button no longer exists, select first button
    if (!selection_restored && window_state.button_count > 0)
        {
            window_state.buttons[0].selected = true;
    }

    // Render buttons
    window_render_buttons();
}

// Render game world
void window_render_game_world(World *world, int player_x, int player_y, int player_z, const char *player_name)
{
    window_clear();

    // Render world grid
    window_render_world_grid(world, player_x, player_y, player_z);

    // Top-center HH:SS clock based on runtime clock in game_state
    extern GameState* g_game_state;
    if (g_game_state)
    {
        Uint32 ms = (Uint32)(g_game_state->runtime_clock_ms % (60 * 60 * 1000)); // within an hour window
        Uint32 secs_total = ms / 1000U;
        Uint32 minutes = (secs_total / 60U) % 60U;
        Uint32 seconds = secs_total % 60U;
        char buf[16]; snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)minutes, (unsigned)seconds);
        int tw=0, th=0; window_measure_text(buf, &tw, &th);
        int x = window_state.base_width / 2 - tw / 2;
        int y = 6; // small top margin
        window_render_text(buf, x, y, window_state.ui_color);
    }

    // Player info panel
    char player_info[256];
    snprintf(player_info, sizeof(player_info), "Player: %s\nPos: (%d,%d,%d)\nX:LR Y:H Z:FB",
             player_name, player_x, player_y, player_z);
    window_render_ui_panel("Player Info", player_info, 5, 5, 120, 60);

    // Controls panel removed for simplicity
}

// Render battle interface
void window_render_battle_interface(int time_remaining, const char *enemy_info)
{
    window_clear();

    // Battle timer
    char timer_text[64];
    snprintf(timer_text, sizeof(timer_text), "Time: %d:%02d", time_remaining / 60, time_remaining % 60);
    window_render_text(timer_text, window_state.base_width / 2 - 40, 20, window_state.highlight_color);

    // Enemy info
    window_render_ui_panel("Enemy", enemy_info, 5, 50, 120, 80);

    // Battle options
    const char *battle_options = "1.Attack\n2.Special\n3.Item\n4.Flee";
    window_render_ui_panel("Battle", battle_options, window_state.base_width - 125, 50, 120, 80);
}

// Render character sheet
void window_render_character_sheet(const char *player_name, int strength, int dexterity, int intelligence,
                                   int wisdom, int constitution, int luck, int experience_points)
{
    window_clear();

    // Title
    window_render_text("Character Sheet", window_state.base_width / 2 - 50, 15, window_state.highlight_color);

    // Character name
    char name_text[128];
    snprintf(name_text, sizeof(name_text), "Name: %s", player_name);
    window_render_text(name_text, 25, 40, window_state.text_color);

    // Experience points
    char exp_text[128];
    snprintf(exp_text, sizeof(exp_text), "EXP: %d", experience_points);
    window_render_text(exp_text, 25, 55, window_state.text_color);

    // Attributes panel
    int panel_x = 25;
    int panel_y = 80;
    int panel_width = 200;
    int panel_height = 150;

    // Draw panel background
    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Panel title
    window_render_text("Attributes", panel_x + 10, panel_y + 10, window_state.ui_color);

    // Attribute names and values
    const char *attr_names[] = {"Strength", "Dexterity", "Intelligence", "Wisdom", "Constitution", "Luck"};
    int attr_values[] = {strength, dexterity, intelligence, wisdom, constitution, luck};

    int attr_y = panel_y + 25;
    int attr_spacing = 18;

    for (int i = 0; i < 6; i++)
    {
        int y_pos = attr_y + (i * attr_spacing);

        // Attribute name
        window_render_text(attr_names[i], panel_x + 10, y_pos, window_state.text_color);

        // Attribute value
        char value_text[32];
        snprintf(value_text, sizeof(value_text), "%d", attr_values[i]);
        window_render_text(value_text, panel_x + 100, y_pos, window_state.text_color);

        // Greyed out plus button
        SDL_Rect plus_rect = {panel_x + 125, y_pos - 2, 12, 12};
        SDL_SetRenderDrawColor(window_state.renderer, 80, 80, 80, 255); // Grey background
        SDL_RenderFillRect(window_state.renderer, &plus_rect);
        SDL_SetRenderDrawColor(window_state.renderer, 120, 120, 120, 255); // Grey border
        SDL_RenderDrawRect(window_state.renderer, &plus_rect);

        // Plus symbol in grey
        SDL_SetRenderDrawColor(window_state.renderer, 100, 100, 100, 255);
        // Horizontal line
        SDL_RenderDrawLine(window_state.renderer,
                           plus_rect.x + 3, plus_rect.y + 6,
                           plus_rect.x + 9, plus_rect.y + 6);
        // Vertical line
        SDL_RenderDrawLine(window_state.renderer,
                           plus_rect.x + 6, plus_rect.y + 3,
                           plus_rect.x + 6, plus_rect.y + 9);
    }

    // Instructions panel
    const char *instructions = "ESC-Return\nI-Inventory\nN-Navigation\nB-Building";
    window_render_ui_panel("Instructions", instructions, window_state.base_width - 125, 80, 120, 80);
}

// Draw one inventory/equipment cell (Minecraft-style dark slot).
static void window_render_inv_slot(int x, int y, int size, ItemId id, uint32_t pieces)
{
    SDL_Rect slot_rect = {x, y, size, size};
    SDL_SetRenderDrawColor(window_state.renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(window_state.renderer, &slot_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 90, 90, 90, 255);
    SDL_RenderDrawRect(window_state.renderer, &slot_rect);

    if (id == ITEM_NONE || pieces == 0)
        return;

    const int icon = size >= 18 ? size - 4 : size - 2;
    const int ox = x + (size - icon) / 2;
    const int oy = y + (size - icon) / 2;
    if (item_icon_texture(id))
        item_icon_draw(window_state.renderer, id, ox, oy, icon);
    else
        window_render_text(item_abbrev(id), x + 1, y + 2, window_state.highlight_color);

    if (pieces > 1 || item_is_fractional(id))
    {
        char count_buf[16];
        item_format_quantity(id, pieces, count_buf, sizeof(count_buf));
        window_render_text(count_buf, x + 1, y + size - 10, window_state.text_color);
    }
}

static bool window_point_in_rect(int x, int y, SDL_Rect r)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void window_skill_hotbar_layout(int *out_x0, int *out_y0, int *out_slot_w, int *out_slot_h,
                                       int *out_gap);
static int window_skill_hotbar_hit(int cx, int cy);
static void loot_draw_button(SDL_Rect r, const char *label, bool hover);

// Floating label for abbreviated HUD cells. Anchored near the cursor, clamped on-screen.
// Supports multiple lines separated by '\n' (title + body for skill tips).
static void window_render_tooltip(const char *text)
{
    if (!text || !text[0])
        return;

    enum { TIP_MAX_LINES = 4 };
    char lines[TIP_MAX_LINES][128];
    int line_count = 0;
    {
        const char *p = text;
        while (*p && line_count < TIP_MAX_LINES)
        {
            size_t n = 0;
            while (p[n] && p[n] != '\n' && n + 1 < sizeof(lines[0]))
                n++;
            memcpy(lines[line_count], p, n);
            lines[line_count][n] = '\0';
            if (n > 0 || line_count == 0)
                line_count++;
            p += n;
            if (*p == '\n')
                p++;
        }
    }
    if (line_count <= 0)
        return;

    int tw = 0, th = 0, line_h = 0;
    for (int i = 0; i < line_count; i++)
    {
        int w = 0, h = 0;
        window_measure_text(lines[i], &w, &h);
        if (w > tw)
            tw = w;
        if (h > line_h)
            line_h = h;
    }
    if (line_h <= 0)
        line_h = 8;
    th = line_h * line_count + (line_count > 1 ? (line_count - 1) : 0);

    const int pad = 3;
    int box_w = tw + pad * 2;
    int box_h = th + pad * 2;
    if (box_w < 20)
        box_w = 20;

    int x = window_state.cursor_x + 10;
    int y = window_state.cursor_y - box_h - 6;
    if (x + box_w > window_state.base_width - 2)
        x = window_state.base_width - box_w - 2;
    if (x < 2)
        x = 2;
    if (y < 2)
        y = window_state.cursor_y + 12;
    if (y + box_h > window_state.base_height - 2)
        y = window_state.base_height - box_h - 2;

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect box = {x, y, box_w, box_h};
    SDL_SetRenderDrawColor(window_state.renderer, 20, 20, 28, 230);
    SDL_RenderFillRect(window_state.renderer, &box);
    SDL_SetRenderDrawColor(window_state.renderer, 180, 180, 200, 255);
    SDL_RenderDrawRect(window_state.renderer, &box);

    for (int i = 0; i < line_count; i++)
    {
        SDL_Color color = (i == 0) ? (SDL_Color){255, 245, 220, 255}
                                   : (SDL_Color){200, 200, 210, 255};
        window_render_text(lines[i], x + pad, y + pad + i * (line_h + 1), color);
    }
}

// Build "Name [rank]\nblurb" (and optional gate line) for skill hover tips.
static void window_format_skill_tooltip(char *buf, size_t buf_sz, SkillId id,
                                        const Actor *spirit)
{
    if (!buf || buf_sz == 0)
        return;
    buf[0] = '\0';
    if (id == SKILL_NONE)
    {
        snprintf(buf, buf_sz, "Empty\nNo skill equipped");
        return;
    }
    const SkillDef *def = skill_def(id);
    if (!def)
    {
        snprintf(buf, buf_sz, "Unknown");
        return;
    }

    const char *blurb = (def->blurb && def->blurb[0]) ? def->blurb : "";
    const uint8_t cur = spirit ? skill_rank(spirit, id) : 0;
    const uint8_t next = spirit ? skill_next_rank(spirit, id) : 0;

    char title[64];
    if (cur > 0)
        snprintf(title, sizeof(title), "%s %s", def->name, skill_rank_roman(cur));
    else
        snprintf(title, sizeof(title), "%s", def->name);

    if (next > 0 && spirit)
    {
        snprintf(buf, buf_sz, "%s\n%s\nNext %s: spirit lv %u", title, blurb,
                 skill_rank_roman(next), (unsigned)skill_min_level_for_rank(id, next));
    }
    else if (cur >= SKILL_RANK_POWERFUL)
    {
        snprintf(buf, buf_sz, "%s\n%s\nMax rank", title, blurb);
    }
    else
    {
        snprintf(buf, buf_sz, "%s\n%s", title, blurb);
    }
}

// Hit targets for character-profile attribute + buttons (spirit = 0, body = 1).
static SDL_Rect s_char_attr_rects[2][ACTOR_ATTR_COUNT];
static bool s_char_attr_enabled[2][ACTOR_ATTR_COUNT];
static bool s_char_attr_has_body;

// Skill-list unlock / drag rows (popout).
#define CHAR_SKILL_HIT_MAX 32
#define CHAR_SKILL_LIST_VISIBLE 7
#define CHAR_SKILL_ROW_H 28
static SDL_Rect s_char_skill_rects[CHAR_SKILL_HIT_MAX];
static SDL_Rect s_char_skill_buy_rects[CHAR_SKILL_HIT_MAX];
static SkillId s_char_skill_ids[CHAR_SKILL_HIT_MAX];
static bool s_char_skill_enabled[CHAR_SKILL_HIT_MAX]; // can buy / unlock
static bool s_char_skill_known[CHAR_SKILL_HIT_MAX];   // known → draggable
static int s_char_skill_hit_count;

// Compact hotbar strip on the character screen.
static SDL_Rect s_char_hotbar_rects[PLAYER_HOTBAR_SLOTS];
static SDL_Rect s_char_skills_btn_rect;
static SDL_Rect s_char_skills_close_rect;
static SDL_Rect s_char_skills_list_rect; // scrollable body (for wheel hit)
static bool s_char_skills_open;
static int s_char_skills_scroll;
static int s_char_skills_total; // tree skill count (for scroll clamp)

// Drag-and-drop onto hotbar slots.
static SkillId s_char_drag_skill = SKILL_NONE;
static int s_char_drag_from_slot = -1; // -1 = from skill list

static void window_draw_attr_plus(int x, int y, bool enabled)
{
    SDL_Rect plus_rect = {x, y, 12, 12};
    if (enabled)
    {
        SDL_SetRenderDrawColor(window_state.renderer, 40, 100, 50, 255);
        SDL_RenderFillRect(window_state.renderer, &plus_rect);
        SDL_SetRenderDrawColor(window_state.renderer, 120, 220, 140, 255);
        SDL_RenderDrawRect(window_state.renderer, &plus_rect);
        SDL_SetRenderDrawColor(window_state.renderer, 220, 255, 220, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(window_state.renderer, 80, 80, 80, 255);
        SDL_RenderFillRect(window_state.renderer, &plus_rect);
        SDL_SetRenderDrawColor(window_state.renderer, 120, 120, 120, 255);
        SDL_RenderDrawRect(window_state.renderer, &plus_rect);
        SDL_SetRenderDrawColor(window_state.renderer, 100, 100, 100, 255);
    }
    SDL_RenderDrawLine(window_state.renderer,
                       plus_rect.x + 3, plus_rect.y + 6,
                       plus_rect.x + 9, plus_rect.y + 6);
    SDL_RenderDrawLine(window_state.renderer,
                       plus_rect.x + 6, plus_rect.y + 3,
                       plus_rect.x + 6, plus_rect.y + 9);
}

static void window_render_stat_column(const char *title, const Actor *actor, int x, int y,
                                     int width, int height, int column_index)
{
    SDL_Rect panel = {x, y, width, height};
    SDL_SetRenderDrawColor(window_state.renderer, 50, 50, 58, 255);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 110, 110, 130, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);

    window_render_text(title, x + 6, y + 4, window_state.highlight_color);
    if (!actor)
    {
        window_render_text("None", x + 6, y + 20, window_state.text_color);
        return;
    }

    char line[96];
    int ly = y + 18;
    const int step = 12;

    snprintf(line, sizeof(line), "%s", actor->name[0] ? actor->name : "Unknown");
    window_render_text(line, x + 6, ly, window_state.ui_color);
    ly += step;

    uint32_t xp_into = actor->experience % 100u;
    snprintf(line, sizeof(line), "Lv %u  XP %u (%u/100)", actor->level, actor->experience, xp_into);
    window_render_text(line, x + 6, ly, window_state.text_color);
    ly += step;

    snprintf(line, sizeof(line), "HP %u/%u", actor->health, actor_max_health(actor));
    window_render_text(line, x + 6, ly, window_state.text_color);
    ly += step;

    snprintf(line, sizeof(line), "STA %.0f/%.0f  MP %.0f/%.0f",
             (double)actor->stamina, (double)actor_max_stamina(actor),
             (double)actor->mana, (double)actor_max_mana(actor));
    window_render_text(line, x + 6, ly, window_state.text_color);
    ly += step;

    if (actor->attribute_points > 0)
        snprintf(line, sizeof(line), "Points %u", actor->attribute_points);
    else
        snprintf(line, sizeof(line), "Points 0");
    window_render_text(line, x + 6, ly,
                       actor->attribute_points > 0 ? window_state.highlight_color
                                                   : window_state.text_color);
    ly += step + 2;

    const char *labels[] = {"STR", "DEX", "INT", "WIS", "CON", "CHA", "LUK"};
    const uint32_t values[] = {
        actor->strength, actor->dexterity, actor->intelligence, actor->wisdom,
        actor->constitution, actor->charisma, actor->luck};
    const bool can_spend = actor->attribute_points > 0;
    for (int i = 0; i < ACTOR_ATTR_COUNT; i++)
    {
        snprintf(line, sizeof(line), "%s %u", labels[i], values[i]);
        window_render_text(line, x + 6, ly, window_state.text_color);

        const bool enabled = can_spend && values[i] < ACTOR_ATTR_MAX;
        const int plus_x = x + width - 18;
        const int plus_y = ly - 1;
        window_draw_attr_plus(plus_x, plus_y, enabled);
        if (column_index >= 0 && column_index < 2)
        {
            s_char_attr_rects[column_index][i] = (SDL_Rect){plus_x, plus_y, 12, 12};
            s_char_attr_enabled[column_index][i] = enabled;
        }

        ly += step;
        if (ly + step > y + height - 4)
            break;
    }

    // Derived combat stats (armor from DEX, turn from DEX, luck boosted by WIS).
    if (ly + step <= y + height - 4)
    {
        snprintf(line, sizeof(line), "AC %u  TRN %u  LUK~%u",
                 actor_armor(actor), actor->turn_speed, actor_effective_luck(actor));
        window_render_text(line, x + 6, ly, window_state.text_color);
    }
}

static void window_draw_char_hotbar_slot(int x, int y, int w, int h, int slot_index,
                                         SkillId skill, bool highlight_drop)
{
    SDL_Rect slot = {x, y, w, h};
    SDL_Color fill = {30, 30, 40, 220};
    SDL_Color border = {90, 90, 110, 255};
    const SkillDef *def = skill_def(skill);
    if (def)
    {
        fill = (SDL_Color){def->fill_r, def->fill_g, def->fill_b, 220};
        border = (SDL_Color){def->border_r, def->border_g, def->border_b, 255};
    }
    if (highlight_drop)
        border = (SDL_Color){220, 200, 120, 255};

    SDL_SetRenderDrawColor(window_state.renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(window_state.renderer, &slot);
    SDL_SetRenderDrawColor(window_state.renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(window_state.renderer, &slot);

    char label[8];
    snprintf(label, sizeof(label), "%d", slot_index + 1);
    window_render_text(label, slot.x + 3, slot.y + 2, (SDL_Color){230, 230, 240, 255});
    const char *abbrev = skill_abbrev(skill);
    if (abbrev && abbrev[0])
        window_render_text(abbrev, slot.x + 14, slot.y + 6, (SDL_Color){255, 220, 160, 255});
}

static void window_render_char_skills_popout(const Actor *spirit)
{
    s_char_skill_hit_count = 0;
    memset(s_char_skill_enabled, 0, sizeof(s_char_skill_enabled));
    memset(s_char_skill_known, 0, sizeof(s_char_skill_known));
    memset(&s_char_skills_close_rect, 0, sizeof(s_char_skills_close_rect));
    memset(&s_char_skills_list_rect, 0, sizeof(s_char_skills_list_rect));

    if (!s_char_skills_open)
        return;

    const int pad = 6;
    const int header_h = 16;
    const int row_h = CHAR_SKILL_ROW_H;
    const int visible = CHAR_SKILL_LIST_VISIBLE;
    const int panel_w = 220;
    const int panel_h = pad + header_h + pad + visible * row_h + pad + 12;
    int panel_x = (window_state.base_width - panel_w) / 2;
    int panel_y = (window_state.base_height - panel_h) / 2 - 8;
    if (panel_x < 4)
        panel_x = 4;
    if (panel_y < 4)
        panel_y = 4;

    // Dim character panel slightly behind the popout.
    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 90);
    SDL_Rect dim = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &dim);

    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(window_state.renderer, 42, 46, 60, 255);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 130, 150, 190, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);

    const uint32_t pts = spirit ? spirit->skill_points : 0u;
    char title[64];
    snprintf(title, sizeof(title), "Skills  pts %u", pts);
    window_render_text(title, panel_x + pad, panel_y + 3,
                       pts > 0 ? window_state.highlight_color : window_state.text_color);
    window_render_text("drag to 1-4", panel_x + pad, panel_y + 12, window_state.ui_color);

    s_char_skills_close_rect = (SDL_Rect){panel_x + panel_w - pad - 14, panel_y + 2, 12, 12};
    bool close_hover =
        window_point_in_rect(window_state.cursor_x, window_state.cursor_y, s_char_skills_close_rect);
    loot_draw_button(s_char_skills_close_rect, "X", close_hover);

    // Collect tree skills.
    SkillId list[CHAR_SKILL_HIT_MAX];
    int total = 0;
    for (SkillId id = (SkillId)1; id < SKILL_COUNT && total < CHAR_SKILL_HIT_MAX; id++)
    {
        if (!skill_appears_in_tree(id))
            continue;
        list[total++] = id;
    }
    s_char_skills_total = total;

    int max_scroll = total - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (s_char_skills_scroll < 0)
        s_char_skills_scroll = 0;
    if (s_char_skills_scroll > max_scroll)
        s_char_skills_scroll = max_scroll;

    const int list_x = panel_x + pad;
    const int list_y = panel_y + header_h + pad + 4;
    const int list_w = panel_w - pad * 2 - 8;
    s_char_skills_list_rect = (SDL_Rect){list_x, list_y, list_w + 8, visible * row_h};

    static char tip_buf[192];
    SkillId tip_id = SKILL_NONE;
    const int cx = window_state.cursor_x;
    const int cy = window_state.cursor_y;

    for (int row = 0; row < visible; row++)
    {
        const int idx = s_char_skills_scroll + row;
        if (idx >= total)
            break;
        const SkillId id = list[idx];
        const SkillDef *def = skill_def(id);
        if (!def)
            continue;

        const uint8_t cur = skill_rank(spirit, id);
        const uint8_t next = skill_next_rank(spirit, id);
        const bool maxed = (next == 0 && cur >= SKILL_RANK_POWERFUL);
        const bool known = skill_is_known(spirit, id);
        const bool level_ok = next > 0 && skill_meets_level_for_rank(spirit, id, next);
        const bool can_buy = next > 0 && spirit && spirit->skill_points > 0 && level_ok;

        SDL_Rect row_r = {list_x, list_y + row * row_h, list_w, row_h - 2};
        if (maxed)
            SDL_SetRenderDrawColor(window_state.renderer, 30, 55, 40, 255);
        else if (can_buy)
            SDL_SetRenderDrawColor(window_state.renderer, def->fill_r, def->fill_g, def->fill_b, 255);
        else if (known)
            SDL_SetRenderDrawColor(window_state.renderer, 48, 52, 68, 255);
        else
            SDL_SetRenderDrawColor(window_state.renderer, 55, 55, 65, 255);
        SDL_RenderFillRect(window_state.renderer, &row_r);
        SDL_SetRenderDrawColor(window_state.renderer,
                               maxed ? 100 : (can_buy ? def->border_r : (known ? 120 : 90)),
                               maxed ? 180 : (can_buy ? def->border_g : (known ? 130 : 90)),
                               maxed ? 120 : (can_buy ? def->border_b : (known ? 160 : 110)), 255);
        SDL_RenderDrawRect(window_state.renderer, &row_r);

        // Color chip
        SDL_Rect chip = {row_r.x + 2, row_r.y + 4, 16, 16};
        SDL_SetRenderDrawColor(window_state.renderer, def->fill_r, def->fill_g, def->fill_b, 255);
        SDL_RenderFillRect(window_state.renderer, &chip);
        SDL_SetRenderDrawColor(window_state.renderer, def->border_r, def->border_g, def->border_b, 255);
        SDL_RenderDrawRect(window_state.renderer, &chip);
        window_render_text(def->abbrev, chip.x + 2, chip.y + 3, window_state.text_color);

        char title_line[40];
        if (cur > 0)
            snprintf(title_line, sizeof(title_line), "%s %s", def->name, skill_rank_roman(cur));
        else
            snprintf(title_line, sizeof(title_line), "%s", def->name);
        window_render_text(title_line, row_r.x + 22, row_r.y + 2,
                           can_buy || maxed || known ? window_state.highlight_color
                                                    : window_state.text_color);

        char sub[32];
        if (maxed)
            snprintf(sub, sizeof(sub), "III max");
        else if (can_buy)
            snprintf(sub, sizeof(sub), "%s ready", skill_rank_roman(next));
        else if (next > 0 && !level_ok)
            snprintf(sub, sizeof(sub), "%s needs lv%u", skill_rank_roman(next),
                     (unsigned)skill_min_level_for_rank(id, next));
        else if (known)
            snprintf(sub, sizeof(sub), "drag to hotbar");
        else if (next > 0)
            snprintf(sub, sizeof(sub), "%s lv%u", skill_rank_roman(next),
                     (unsigned)skill_min_level_for_rank(id, next));
        else
            snprintf(sub, sizeof(sub), "locked");
        window_render_text(sub, row_r.x + 22, row_r.y + 14, window_state.text_color);

        SDL_Rect buy = {0, 0, 0, 0};
        if (can_buy)
        {
            buy = (SDL_Rect){row_r.x + row_r.w - 22, row_r.y + 4, 18, 18};
            bool buy_hover = window_point_in_rect(cx, cy, buy);
            loot_draw_button(buy, "+", buy_hover);
        }

        if (s_char_skill_hit_count < CHAR_SKILL_HIT_MAX)
        {
            s_char_skill_rects[s_char_skill_hit_count] = row_r;
            s_char_skill_buy_rects[s_char_skill_hit_count] = buy;
            s_char_skill_ids[s_char_skill_hit_count] = id;
            s_char_skill_enabled[s_char_skill_hit_count] = can_buy;
            s_char_skill_known[s_char_skill_hit_count] = known;
            s_char_skill_hit_count++;
        }

        if (window_point_in_rect(cx, cy, row_r))
            tip_id = id;
    }

    // Scrollbar
    if (max_scroll > 0)
    {
        const int sb_w = 5;
        const int track_x = panel_x + panel_w - pad - sb_w;
        const int track_y = list_y;
        const int track_h = visible * row_h;
        SDL_Rect track = {track_x, track_y, sb_w, track_h};
        SDL_SetRenderDrawColor(window_state.renderer, 30, 32, 40, 255);
        SDL_RenderFillRect(window_state.renderer, &track);
        const int thumb_h = track_h * visible / total;
        int thumb_travel = track_h - thumb_h;
        if (thumb_travel < 0)
            thumb_travel = 0;
        int thumb_y = track_y;
        if (max_scroll > 0 && thumb_travel > 0)
            thumb_y = track_y + (s_char_skills_scroll * thumb_travel) / max_scroll;
        SDL_Rect thumb = {track_x, thumb_y, sb_w, thumb_h > 4 ? thumb_h : 4};
        SDL_SetRenderDrawColor(window_state.renderer, 140, 160, 200, 255);
        SDL_RenderFillRect(window_state.renderer, &thumb);
    }

    if (tip_id != SKILL_NONE && s_char_drag_skill == SKILL_NONE)
    {
        window_format_skill_tooltip(tip_buf, sizeof(tip_buf), tip_id, spirit);
        window_render_tooltip(tip_buf);
    }
}

// Character profile modal: spirit always, inhabited body when dominating, hotbar + skills popout.
void window_render_character_profile(const Actor *spirit, const Actor *body)
{
    memset(s_char_attr_enabled, 0, sizeof(s_char_attr_enabled));
    s_char_attr_has_body = body != NULL;
    s_char_skill_hit_count = 0;
    memset(s_char_skill_enabled, 0, sizeof(s_char_skill_enabled));
    memset(s_char_skill_known, 0, sizeof(s_char_skill_known));
    memset(s_char_hotbar_rects, 0, sizeof(s_char_hotbar_rects));
    memset(&s_char_skills_btn_rect, 0, sizeof(s_char_skills_btn_rect));

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 150);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    const int margin = 6;
    int panel_w = window_state.base_width - margin * 2;
    int panel_h = window_state.base_height - margin * 2;
    int panel_x = margin;
    int panel_y = margin;

    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(window_state.renderer, 45, 45, 52, 255);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 130, 130, 150, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);

    window_render_text("Character", panel_x + 8, panel_y + 4, window_state.highlight_color);
    window_render_text("TAB/ESC close", panel_x + panel_w - 90, panel_y + 4, window_state.text_color);

    const int header = 18;
    const int gap = 6;
    const int hotbar_h = 44;
    const int inner_y = panel_y + header;
    const int stats_h = panel_h - header - 6 - hotbar_h - gap;
    const int col_w = body ? (panel_w - 18 - gap) / 2 : (panel_w - 16);

    window_render_stat_column("Spirit", spirit, panel_x + 8, inner_y, col_w, stats_h, 0);
    if (body)
        window_render_stat_column("Body", body, panel_x + 8 + col_w + gap, inner_y, col_w, stats_h, 1);

    // --- Hotbar strip (skills list lives in a popout) ------------------------
    const int bar_y = inner_y + stats_h + gap;
    SDL_Rect bar = {panel_x + 8, bar_y, panel_w - 16, hotbar_h};
    SDL_SetRenderDrawColor(window_state.renderer, 40, 44, 58, 255);
    SDL_RenderFillRect(window_state.renderer, &bar);
    SDL_SetRenderDrawColor(window_state.renderer, 100, 120, 160, 255);
    SDL_RenderDrawRect(window_state.renderer, &bar);

    extern GameState *g_game_state;
    const uint32_t pts = spirit ? spirit->skill_points : 0u;
    char line[96];
    snprintf(line, sizeof(line), "Hotbar  pts %u", pts);
    window_render_text(line, bar.x + 4, bar.y + 3,
                       pts > 0 ? window_state.highlight_color : window_state.text_color);

    s_char_skills_btn_rect = (SDL_Rect){bar.x + 4, bar.y + 16, 52, 22};
    bool skills_hover =
        window_point_in_rect(window_state.cursor_x, window_state.cursor_y, s_char_skills_btn_rect);
    loot_draw_button(s_char_skills_btn_rect, s_char_skills_open ? "Hide" : "Skills", skills_hover);

    const int slot_w = 48;
    const int slot_h = 22;
    const int slot_gap = 4;
    const int slots_w = PLAYER_HOTBAR_SLOTS * slot_w + (PLAYER_HOTBAR_SLOTS - 1) * slot_gap;
    int slot_x0 = bar.x + bar.w - slots_w - 6;
    if (slot_x0 < s_char_skills_btn_rect.x + s_char_skills_btn_rect.w + 8)
        slot_x0 = s_char_skills_btn_rect.x + s_char_skills_btn_rect.w + 8;
    const int slot_y = bar.y + (hotbar_h - slot_h) / 2;

    const int cx = window_state.cursor_x;
    const int cy = window_state.cursor_y;
    static char tip_buf[192];
    SkillId tip_id = SKILL_NONE;

    for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
    {
        SkillId skill = SKILL_NONE;
        if (g_game_state)
            skill = player_controls_hotbar_skill(&g_game_state->controls, i);
        // While dragging this slot's skill, show empty source.
        if (s_char_drag_skill != SKILL_NONE && s_char_drag_from_slot == i)
            skill = SKILL_NONE;

        SDL_Rect slot = {slot_x0 + i * (slot_w + slot_gap), slot_y, slot_w, slot_h};
        s_char_hotbar_rects[i] = slot;
        const bool drop_hi =
            s_char_drag_skill != SKILL_NONE && window_point_in_rect(cx, cy, slot);
        window_draw_char_hotbar_slot(slot.x, slot.y, slot.w, slot.h, i, skill, drop_hi);

        if (s_char_drag_skill == SKILL_NONE && window_point_in_rect(cx, cy, slot))
            tip_id = skill;
    }

    // Skill-list popout (drawn above the strip; can cover stats).
    window_render_char_skills_popout(spirit);

    // Drag ghost follows the cursor.
    if (s_char_drag_skill != SKILL_NONE)
    {
        const SkillDef *def = skill_def(s_char_drag_skill);
        SDL_Rect ghost = {cx - 12, cy - 10, 36, 20};
        if (def)
            SDL_SetRenderDrawColor(window_state.renderer, def->fill_r, def->fill_g, def->fill_b, 220);
        else
            SDL_SetRenderDrawColor(window_state.renderer, 60, 60, 80, 220);
        SDL_RenderFillRect(window_state.renderer, &ghost);
        if (def)
            SDL_SetRenderDrawColor(window_state.renderer, def->border_r, def->border_g, def->border_b,
                                   255);
        else
            SDL_SetRenderDrawColor(window_state.renderer, 140, 140, 160, 255);
        SDL_RenderDrawRect(window_state.renderer, &ghost);
        const char *abbrev = skill_abbrev(s_char_drag_skill);
        if (abbrev)
            window_render_text(abbrev, ghost.x + 4, ghost.y + 5, (SDL_Color){255, 240, 200, 255});
    }
    else if (tip_id != SKILL_NONE && !s_char_skills_open)
    {
        window_format_skill_tooltip(tip_buf, sizeof(tip_buf), tip_id, spirit);
        window_render_tooltip(tip_buf);
    }
}

bool window_character_profile_hit(int mouse_x, int mouse_y, bool *out_is_body,
                                  ActorAttribute *out_attr)
{
    // Ignore attribute clicks while the skill list covers the sheet.
    if (s_char_skills_open)
        return false;

    const int cols = s_char_attr_has_body ? 2 : 1;
    for (int col = 0; col < cols; col++)
    {
        for (int i = 0; i < ACTOR_ATTR_COUNT; i++)
        {
            if (!s_char_attr_enabled[col][i])
                continue;
            if (!window_point_in_rect(mouse_x, mouse_y, s_char_attr_rects[col][i]))
                continue;
            if (out_is_body)
                *out_is_body = (col == 1);
            if (out_attr)
                *out_attr = (ActorAttribute)i;
            return true;
        }
    }
    return false;
}

bool window_character_profile_skill_hit(int mouse_x, int mouse_y, SkillId *out_skill)
{
    for (int i = 0; i < s_char_skill_hit_count; i++)
    {
        if (!s_char_skill_enabled[i])
            continue;
        if (!window_point_in_rect(mouse_x, mouse_y, s_char_skill_buy_rects[i]) &&
            !(s_char_skill_buy_rects[i].w == 0 &&
              window_point_in_rect(mouse_x, mouse_y, s_char_skill_rects[i])))
            continue;
        if (out_skill)
            *out_skill = s_char_skill_ids[i];
        return true;
    }
    return false;
}

WindowCharSkillHit window_character_skill_ui_hit(int mouse_x, int mouse_y,
                                                 SkillId *out_skill, int *out_slot)
{
    if (out_skill)
        *out_skill = SKILL_NONE;
    if (out_slot)
        *out_slot = -1;

    if (s_char_skills_open)
    {
        if (window_point_in_rect(mouse_x, mouse_y, s_char_skills_close_rect))
            return WINDOW_CHAR_SKILL_HIT_CLOSE;

        // Prefer explicit buy buttons so known skills remain draggable.
        for (int i = 0; i < s_char_skill_hit_count; i++)
        {
            if (!s_char_skill_enabled[i])
                continue;
            if (!window_point_in_rect(mouse_x, mouse_y, s_char_skill_buy_rects[i]))
                continue;
            if (out_skill)
                *out_skill = s_char_skill_ids[i];
            return WINDOW_CHAR_SKILL_HIT_UNLOCK;
        }

        for (int i = 0; i < s_char_skill_hit_count; i++)
        {
            if (!window_point_in_rect(mouse_x, mouse_y, s_char_skill_rects[i]))
                continue;
            if (out_skill)
                *out_skill = s_char_skill_ids[i];
            if (s_char_skill_known[i])
                return WINDOW_CHAR_SKILL_HIT_DRAG;
            // Unknown but purchasable: whole row buys when there is no separate button miss.
            if (s_char_skill_enabled[i])
                return WINDOW_CHAR_SKILL_HIT_UNLOCK;
            return WINDOW_CHAR_SKILL_HIT_NONE;
        }
    }

    if (window_point_in_rect(mouse_x, mouse_y, s_char_skills_btn_rect))
        return WINDOW_CHAR_SKILL_HIT_OPEN;

    for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
    {
        if (!window_point_in_rect(mouse_x, mouse_y, s_char_hotbar_rects[i]))
            continue;
        if (out_slot)
            *out_slot = i;
        return WINDOW_CHAR_SKILL_HIT_HOTBAR;
    }
    return WINDOW_CHAR_SKILL_HIT_NONE;
}

void window_character_skills_toggle(void)
{
    s_char_skills_open = !s_char_skills_open;
    if (!s_char_skills_open)
        window_character_skill_drag_clear();
}

void window_character_skills_close(void)
{
    s_char_skills_open = false;
    window_character_skill_drag_clear();
}

bool window_character_skills_is_open(void)
{
    return s_char_skills_open;
}

void window_character_skills_on_wheel(int wheel_y)
{
    if (!s_char_skills_open || wheel_y == 0)
        return;
    // Positive y = scroll up → earlier rows.
    s_char_skills_scroll -= wheel_y;
    int max_scroll = s_char_skills_total - CHAR_SKILL_LIST_VISIBLE;
    if (max_scroll < 0)
        max_scroll = 0;
    if (s_char_skills_scroll < 0)
        s_char_skills_scroll = 0;
    if (s_char_skills_scroll > max_scroll)
        s_char_skills_scroll = max_scroll;
}

void window_character_skill_drag_begin(SkillId skill, int from_hotbar_slot)
{
    if (skill == SKILL_NONE)
    {
        window_character_skill_drag_clear();
        return;
    }
    s_char_drag_skill = skill;
    s_char_drag_from_slot = from_hotbar_slot;
}

void window_character_skill_drag_clear(void)
{
    s_char_drag_skill = SKILL_NONE;
    s_char_drag_from_slot = -1;
}

SkillId window_character_skill_drag_skill(void)
{
    return s_char_drag_skill;
}

int window_character_skill_drag_from_slot(void)
{
    return s_char_drag_skill == SKILL_NONE ? -1 : s_char_drag_from_slot;
}

void window_character_skills_reset_ui(void)
{
    s_char_skills_open = false;
    s_char_skills_scroll = 0;
    window_character_skill_drag_clear();
}

// Minecraft-like inventory modal: dimmed world behind a large centered bag panel.
// Equipment is body-only; pass NULL for free spirit.

#define INV_HIT_MAX 64
typedef struct {
    WindowInvHit kind;
    int index;
    SDL_Rect rect;
} InvHitRect;
static InvHitRect s_inv_hits[INV_HIT_MAX];
static int s_inv_hit_count;

void window_render_inventory(const char *owner_name, const Wallet *purse,
                             const Inventory *inventory,
                             const Equipment *equipment,
                             const char *body_name)
{
    (void)owner_name;
    (void)body_name;
    s_inv_hit_count = 0;

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 140);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    const int slot = 20;
    const int gap = 2;
    const int cols = 8;
    const int pad = 8;
    const int header_h = 14;

    int capacity = inventory ? inventory->capacity : 0;
    if (capacity > INVENTORY_MAX_SLOTS)
        capacity = INVENTORY_MAX_SLOTS;
    if (capacity < 1)
        capacity = 1;

    int rows = (capacity + cols - 1) / cols;
    int bag_w = cols * (slot + gap) - gap;
    int bag_h = rows * (slot + gap) - gap;

    static const EquipmentSlot equip_order[EQUIP_SLOT_COUNT] = {
        EQUIP_SLOT_HELMET,
        EQUIP_SLOT_CORE_ARMOR,
        EQUIP_SLOT_LEGGINGS,
        EQUIP_SLOT_GAUNTLETS,
        EQUIP_SLOT_RING,
        EQUIP_SLOT_NECKLACE,
        EQUIP_SLOT_MAIN_HAND,
    };
    const int show_equip = equipment != NULL;
    int equip_h = show_equip ? (EQUIP_SLOT_COUNT * (slot + gap) - gap) : 0;
    int equip_w = show_equip ? slot : 0;
    int content_h = bag_h > equip_h ? bag_h : equip_h;
    int content_w = bag_w + (show_equip ? equip_w + pad : 0);

    int panel_w = pad * 2 + content_w;
    int panel_h = pad + header_h + pad + content_h + pad;
    if (panel_w < window_state.base_width - 24)
        panel_w = window_state.base_width - 24;
    if (panel_h < window_state.base_height - 24)
        panel_h = window_state.base_height - 24;
    if (panel_w > window_state.base_width - 8)
        panel_w = window_state.base_width - 8;
    if (panel_h > window_state.base_height - 8)
        panel_h = window_state.base_height - 8;

    int panel_x = (window_state.base_width - panel_w) / 2;
    int panel_y = (window_state.base_height - panel_h) / 2;

    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(window_state.renderer, 55, 55, 55, 255);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 120, 120, 120, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);
    SDL_Rect inset = {panel_x + 1, panel_y + 1, panel_w - 2, panel_h - 2};
    SDL_SetRenderDrawColor(window_state.renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(window_state.renderer, &inset);

    window_render_text("Inventory", panel_x + pad, panel_y + 4, window_state.highlight_color);
    window_render_text("RMB drop", panel_x + pad + 70, panel_y + 4, window_state.text_color);
    if (purse && wallet_total_copper(purse) > 0)
    {
        char purse_text[48];
        wallet_format(purse, purse_text, sizeof(purse_text));
        int tw = (int)strlen(purse_text) * 6;
        window_render_text(purse_text, panel_x + panel_w - pad - tw, panel_y + 4,
                           window_state.text_color);
    }

    int content_y = panel_y + header_h + pad;
    int cluster_w = content_w;
    int cluster_x = panel_x + (panel_w - cluster_w) / 2;
    int bag_x = cluster_x + (show_equip ? equip_w + pad : 0);
    int bag_y = content_y;

    const char *tooltip = NULL;
    static char tip_buf[96];
    int cx = window_state.cursor_x;
    int cy = window_state.cursor_y;

    if (show_equip)
    {
        int ex = cluster_x;
        for (int i = 0; i < EQUIP_SLOT_COUNT; i++)
        {
            EquipmentSlot es = equip_order[i];
            int ey = content_y + i * (slot + gap);
            ItemId eid = equipment->slots[es];
            window_render_inv_slot(ex, ey, slot, eid, eid != ITEM_NONE ? 1u : 0u);
            SDL_Rect hit = {ex, ey, slot, slot};
            if (s_inv_hit_count < INV_HIT_MAX)
            {
                s_inv_hits[s_inv_hit_count].kind = WINDOW_INV_HIT_EQUIP;
                s_inv_hits[s_inv_hit_count].index = (int)es;
                s_inv_hits[s_inv_hit_count].rect = hit;
                s_inv_hit_count++;
            }
            if (window_point_in_rect(cx, cy, hit))
            {
                if (eid != ITEM_NONE)
                {
                    snprintf(tip_buf, sizeof(tip_buf), "%s: %s",
                             equipment_slot_name(es), item_name(eid));
                    tooltip = tip_buf;
                }
                else
                {
                    tooltip = equipment_slot_name(es);
                }
            }
        }
    }

    for (int i = 0; i < capacity; i++)
    {
        int row = i / cols;
        int col = i % cols;
        int x = bag_x + col * (slot + gap);
        int y = bag_y + row * (slot + gap);
        ItemId id = ITEM_NONE;
        uint32_t pieces = 0;
        if (inventory)
        {
            id = inventory->slots[i].id;
            pieces = inventory->slots[i].pieces;
        }
        window_render_inv_slot(x, y, slot, id, pieces);
        SDL_Rect hit = {x, y, slot, slot};
        if (s_inv_hit_count < INV_HIT_MAX)
        {
            s_inv_hits[s_inv_hit_count].kind = WINDOW_INV_HIT_BAG;
            s_inv_hits[s_inv_hit_count].index = i;
            s_inv_hits[s_inv_hit_count].rect = hit;
            s_inv_hit_count++;
        }
        if (window_point_in_rect(cx, cy, hit))
        {
            if (id != ITEM_NONE && pieces > 0)
            {
                char qty[16];
                item_format_quantity(id, pieces, qty, sizeof(qty));
                snprintf(tip_buf, sizeof(tip_buf), "%s x%s (RMB drop)", item_name(id), qty);
                tooltip = tip_buf;
            }
            else
            {
                tooltip = "Empty";
            }
        }
    }

    if (tooltip)
        window_render_tooltip(tooltip);
}

WindowInvHit window_inventory_hit(int mouse_x, int mouse_y, int *out_index)
{
    for (int i = s_inv_hit_count - 1; i >= 0; i--)
    {
        if (!window_point_in_rect(mouse_x, mouse_y, s_inv_hits[i].rect))
            continue;
        if (out_index)
            *out_index = s_inv_hits[i].index;
        return s_inv_hits[i].kind;
    }
    return WINDOW_INV_HIT_NONE;
}


// --- Corpse loot modal -------------------------------------------------------

#define LOOT_SLOT 16
#define LOOT_GAP 2
#define LOOT_CORPSE_COLS 4
#define LOOT_PLAYER_COLS 8
#define LOOT_HIT_MAX 96

typedef struct {
    WindowLootHit kind;
    int index;
    SDL_Rect rect;
} LootHitRect;

static LootHitRect s_loot_hits[LOOT_HIT_MAX];
static int s_loot_hit_count;
static SDL_Rect s_loot_all_rect;
static SDL_Rect s_loot_close_rect;

static void loot_hit_reset(void)
{
    s_loot_hit_count = 0;
    memset(&s_loot_all_rect, 0, sizeof(s_loot_all_rect));
    memset(&s_loot_close_rect, 0, sizeof(s_loot_close_rect));
}

static void loot_hit_add(WindowLootHit kind, int index, SDL_Rect rect)
{
    if (s_loot_hit_count >= LOOT_HIT_MAX)
        return;
    s_loot_hits[s_loot_hit_count].kind = kind;
    s_loot_hits[s_loot_hit_count].index = index;
    s_loot_hits[s_loot_hit_count].rect = rect;
    s_loot_hit_count++;
}

static void loot_draw_button(SDL_Rect r, const char *label, bool hover)
{
    if (hover)
        SDL_SetRenderDrawColor(window_state.renderer, 70, 90, 70, 255);
    else
        SDL_SetRenderDrawColor(window_state.renderer, 50, 60, 50, 255);
    SDL_RenderFillRect(window_state.renderer, &r);
    SDL_SetRenderDrawColor(window_state.renderer, 140, 180, 140, 255);
    SDL_RenderDrawRect(window_state.renderer, &r);
    window_render_text(label, r.x + 6, r.y + 4, window_state.highlight_color);
}

void window_render_loot(const char *corpse_name,
                        const Inventory *corpse_inv,
                        const Equipment *corpse_eq,
                        const Inventory *player_inv,
                        const ItemStack *held)
{
    loot_hit_reset();

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 150);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    const int slot = LOOT_SLOT;
    const int gap = LOOT_GAP;
    const int pad = 6;
    const int header_h = 14;
    const int footer_h = 22;

    int corpse_cap = corpse_inv ? corpse_inv->capacity : 0;
    if (corpse_cap > INVENTORY_MAX_SLOTS)
        corpse_cap = INVENTORY_MAX_SLOTS;
    int player_cap = player_inv ? player_inv->capacity : 0;
    if (player_cap > INVENTORY_MAX_SLOTS)
        player_cap = INVENTORY_MAX_SLOTS;

    int corpse_rows = corpse_cap > 0 ? (corpse_cap + LOOT_CORPSE_COLS - 1) / LOOT_CORPSE_COLS : 1;
    int player_rows = player_cap > 0 ? (player_cap + LOOT_PLAYER_COLS - 1) / LOOT_PLAYER_COLS : 1;

    static const EquipmentSlot equip_order[EQUIP_SLOT_COUNT] = {
        EQUIP_SLOT_HELMET,
        EQUIP_SLOT_CORE_ARMOR,
        EQUIP_SLOT_LEGGINGS,
        EQUIP_SLOT_GAUNTLETS,
        EQUIP_SLOT_RING,
        EQUIP_SLOT_NECKLACE,
        EQUIP_SLOT_MAIN_HAND,
    };

    int equip_w = corpse_eq ? slot : 0;
    int corpse_bag_w = LOOT_CORPSE_COLS * (slot + gap) - gap;
    int player_bag_w = LOOT_PLAYER_COLS * (slot + gap) - gap;
    int left_w = (corpse_eq ? equip_w + pad : 0) + corpse_bag_w;
    int bags_h = (corpse_rows > player_rows ? corpse_rows : player_rows) * (slot + gap) - gap;
    if (corpse_eq)
    {
        int eh = EQUIP_SLOT_COUNT * (slot + gap) - gap;
        if (eh > bags_h)
            bags_h = eh;
    }

    int panel_w = pad * 2 + left_w + pad * 2 + player_bag_w;
    int panel_h = pad + header_h + pad + bags_h + pad + footer_h + pad;
    if (panel_w > window_state.base_width - 8)
        panel_w = window_state.base_width - 8;
    if (panel_h > window_state.base_height - 8)
        panel_h = window_state.base_height - 8;
    int panel_x = (window_state.base_width - panel_w) / 2;
    int panel_y = (window_state.base_height - panel_h) / 2;

    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(window_state.renderer, 48, 48, 56, 255);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 130, 130, 150, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);

    char title[96];
    snprintf(title, sizeof(title), "Loot: %s",
             (corpse_name && corpse_name[0]) ? corpse_name : "Corpse");
    window_render_text(title, panel_x + pad, panel_y + 3, window_state.highlight_color);
    window_render_text("ESC close", panel_x + panel_w - pad - 56, panel_y + 3, window_state.text_color);

    s_loot_close_rect = (SDL_Rect){panel_x + panel_w - pad - 14, panel_y + 2, 12, 12};
    loot_hit_add(WINDOW_LOOT_HIT_CLOSE, 0, s_loot_close_rect);
    {
        bool hover = window_point_in_rect(window_state.cursor_x, window_state.cursor_y, s_loot_close_rect);
        SDL_SetRenderDrawColor(window_state.renderer, hover ? 120 : 80, 40, 40, 255);
        SDL_RenderFillRect(window_state.renderer, &s_loot_close_rect);
        window_render_text("X", s_loot_close_rect.x + 3, s_loot_close_rect.y + 1, window_state.text_color);
    }

    int content_y = panel_y + header_h + pad;
    int left_x = panel_x + pad;
    int right_x = panel_x + panel_w - pad - player_bag_w;

    window_render_text("Corpse", left_x, content_y - 12, window_state.text_color);
    window_render_text("You", right_x, content_y - 12, window_state.text_color);

    const char *tooltip = NULL;
    static char tip_buf[96];
    int cx = window_state.cursor_x;
    int cy = window_state.cursor_y;

    int bag_x = left_x + (corpse_eq ? equip_w + pad : 0);
    if (corpse_eq)
    {
        for (int i = 0; i < EQUIP_SLOT_COUNT; i++)
        {
            EquipmentSlot es = equip_order[i];
            int ey = content_y + i * (slot + gap);
            ItemId eid = corpse_eq->slots[es];
            // Hide the slot contents while that piece is on the cursor.
            if (held && held->id != ITEM_NONE && held->pieces > 0)
            {
                /* kept visible empty if currently dragged from here; bag already cleared */
            }
            window_render_inv_slot(left_x, ey, slot, eid, eid != ITEM_NONE ? 1u : 0u);
            SDL_Rect hit = {left_x, ey, slot, slot};
            loot_hit_add(WINDOW_LOOT_HIT_CORPSE_EQUIP, (int)es, hit);
            if (window_point_in_rect(cx, cy, hit))
            {
                if (eid != ITEM_NONE)
                {
                    snprintf(tip_buf, sizeof(tip_buf), "%s: %s",
                             equipment_slot_name(es), item_name(eid));
                    tooltip = tip_buf;
                }
                else
                    tooltip = equipment_slot_name(es);
            }
        }
    }

    for (int i = 0; i < corpse_cap; i++)
    {
        int row = i / LOOT_CORPSE_COLS;
        int col = i % LOOT_CORPSE_COLS;
        int x = bag_x + col * (slot + gap);
        int y = content_y + row * (slot + gap);
        ItemId id = ITEM_NONE;
        uint32_t pieces = 0;
        if (corpse_inv)
        {
            id = corpse_inv->slots[i].id;
            pieces = corpse_inv->slots[i].pieces;
        }
        window_render_inv_slot(x, y, slot, id, pieces);
        SDL_Rect hit = {x, y, slot, slot};
        loot_hit_add(WINDOW_LOOT_HIT_CORPSE_BAG, i, hit);
        if (window_point_in_rect(cx, cy, hit))
        {
            if (id != ITEM_NONE && pieces > 0)
            {
                char qty[16];
                item_format_quantity(id, pieces, qty, sizeof(qty));
                snprintf(tip_buf, sizeof(tip_buf), "%s x%s", item_name(id), qty);
                tooltip = tip_buf;
            }
            else
                tooltip = "Empty";
        }
    }

    for (int i = 0; i < player_cap; i++)
    {
        int row = i / LOOT_PLAYER_COLS;
        int col = i % LOOT_PLAYER_COLS;
        int x = right_x + col * (slot + gap);
        int y = content_y + row * (slot + gap);
        ItemId id = ITEM_NONE;
        uint32_t pieces = 0;
        if (player_inv)
        {
            id = player_inv->slots[i].id;
            pieces = player_inv->slots[i].pieces;
        }
        window_render_inv_slot(x, y, slot, id, pieces);
        SDL_Rect hit = {x, y, slot, slot};
        loot_hit_add(WINDOW_LOOT_HIT_PLAYER_BAG, i, hit);
        if (window_point_in_rect(cx, cy, hit))
        {
            if (id != ITEM_NONE && pieces > 0)
            {
                char qty[16];
                item_format_quantity(id, pieces, qty, sizeof(qty));
                snprintf(tip_buf, sizeof(tip_buf), "%s x%s", item_name(id), qty);
                tooltip = tip_buf;
            }
            else
                tooltip = "Empty";
        }
    }

    s_loot_all_rect = (SDL_Rect){panel_x + pad, panel_y + panel_h - pad - footer_h + 2, 72, 16};
    bool all_hover = window_point_in_rect(cx, cy, s_loot_all_rect);
    loot_draw_button(s_loot_all_rect, "Loot All", all_hover);
    loot_hit_add(WINDOW_LOOT_HIT_LOOT_ALL, 0, s_loot_all_rect);

    // Dragged stack follows the cursor.
    if (held && held->id != ITEM_NONE && held->pieces > 0)
    {
        window_render_inv_slot(cx + 4, cy + 4, slot, held->id, held->pieces);
        if (!tooltip)
        {
            char qty[16];
            item_format_quantity(held->id, held->pieces, qty, sizeof(qty));
            snprintf(tip_buf, sizeof(tip_buf), "%s x%s", item_name(held->id), qty);
            tooltip = tip_buf;
        }
    }

    if (tooltip)
        window_render_tooltip(tooltip);
}

WindowLootHit window_loot_hit(int mouse_x, int mouse_y, int *out_index)
{
    for (int i = s_loot_hit_count - 1; i >= 0; i--)
    {
        if (!window_point_in_rect(mouse_x, mouse_y, s_loot_hits[i].rect))
            continue;
        if (out_index)
            *out_index = s_loot_hits[i].index;
        return s_loot_hits[i].kind;
    }
    if (out_index)
        *out_index = -1;
    return WINDOW_LOOT_HIT_NONE;
}

// --- Settlement shop modal ---------------------------------------------------

#define SHOP_UI_SLOT 14
#define SHOP_UI_GAP 2
#define SHOP_UI_STOCK_COLS 4
#define SHOP_UI_PLAYER_COLS 8
#define SHOP_HIT_MAX 96

typedef struct {
    WindowShopHit kind;
    int index;
    SDL_Rect rect;
} ShopHitRect;

static ShopHitRect s_shop_hits[SHOP_HIT_MAX];
static int s_shop_hit_count;
static SDL_Rect s_shop_close_rect;

static void shop_hit_reset(void)
{
    s_shop_hit_count = 0;
    memset(&s_shop_close_rect, 0, sizeof(s_shop_close_rect));
}

static void shop_hit_add(WindowShopHit kind, int index, SDL_Rect rect)
{
    if (s_shop_hit_count >= SHOP_HIT_MAX)
        return;
    s_shop_hits[s_shop_hit_count].kind = kind;
    s_shop_hits[s_shop_hit_count].index = index;
    s_shop_hits[s_shop_hit_count].rect = rect;
    s_shop_hit_count++;
}

void window_render_shop(const char *merchant_name,
                        const Wallet *player_purse,
                        const Wallet *merchant_purse,
                        const ShopSession *shop,
                        const Inventory *player_inv)
{
    shop_hit_reset();

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 150);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    const int slot = SHOP_UI_SLOT;
    const int gap = SHOP_UI_GAP;
    const int pad = 6;
    const int header_h = 24;
    const int footer_h = 18;

    int listing_count = shop ? shop->listing_count : 0;
    if (listing_count > SHOP_LISTING_MAX)
        listing_count = SHOP_LISTING_MAX;
    int player_cap = player_inv ? player_inv->capacity : 0;
    if (player_cap > INVENTORY_MAX_SLOTS)
        player_cap = INVENTORY_MAX_SLOTS;

    int stock_rows = listing_count > 0
                         ? (listing_count + SHOP_UI_STOCK_COLS - 1) / SHOP_UI_STOCK_COLS
                         : 1;
    int player_rows = player_cap > 0
                          ? (player_cap + SHOP_UI_PLAYER_COLS - 1) / SHOP_UI_PLAYER_COLS
                          : 1;

    int stock_w = SHOP_UI_STOCK_COLS * (slot + gap) - gap;
    int player_w = SHOP_UI_PLAYER_COLS * (slot + gap) - gap;
    int stock_h = stock_rows * (slot + gap) - gap;
    int player_h = player_rows * (slot + gap) - gap;

    int content_w = stock_w + pad + player_w;
    int content_h = stock_h > player_h ? stock_h : player_h;
    int panel_w = pad * 2 + content_w;
    int panel_h = pad + header_h + pad + content_h + pad + footer_h;
    if (panel_w > window_state.base_width - 8)
        panel_w = window_state.base_width - 8;
    if (panel_h > window_state.base_height - 8)
        panel_h = window_state.base_height - 8;

    int panel_x = (window_state.base_width - panel_w) / 2;
    int panel_y = (window_state.base_height - panel_h) / 2;

    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(window_state.renderer, 45, 50, 45, 255);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 140, 160, 120, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);

    const char *title = merchant_name && merchant_name[0] ? merchant_name : "Shop";
    window_render_text(title, panel_x + pad, panel_y + 3, window_state.highlight_color);

    char purse_line[64];
    char pbuf[32], mbuf[32];
    static const Wallet empty_purse = {0, 0, 0};
    wallet_format(player_purse ? player_purse : &empty_purse, pbuf, sizeof(pbuf));
    wallet_format(merchant_purse ? merchant_purse : &empty_purse, mbuf, sizeof(mbuf));
    snprintf(purse_line, sizeof(purse_line), "You %s  Shop %s", pbuf, mbuf);
    window_render_text(purse_line, panel_x + pad, panel_y + 13, window_state.text_color);

    s_shop_close_rect = (SDL_Rect){panel_x + panel_w - pad - 40, panel_y + 3, 36, 12};
    bool close_hover = window_point_in_rect(window_state.cursor_x, window_state.cursor_y,
                                            s_shop_close_rect);
    loot_draw_button(s_shop_close_rect, "X", close_hover);
    shop_hit_add(WINDOW_SHOP_HIT_CLOSE, 0, s_shop_close_rect);

    int content_y = panel_y + header_h + pad;
    int stock_x = panel_x + pad;
    int player_x = stock_x + stock_w + pad;

    window_render_text("Buy", stock_x, content_y - 10, window_state.text_color);
    window_render_text("Sell", player_x, content_y - 10, window_state.text_color);

    const char *tooltip = NULL;
    static char tip_buf[96];
    int cx = window_state.cursor_x;
    int cy = window_state.cursor_y;

    for (int i = 0; i < listing_count; i++)
    {
        int row = i / SHOP_UI_STOCK_COLS;
        int col = i % SHOP_UI_STOCK_COLS;
        int x = stock_x + col * (slot + gap);
        int y = content_y + row * (slot + gap);
        const ItemStack *st = &shop->listings[i].stack;
        window_render_inv_slot(x, y, slot, st->id, st->pieces);
        SDL_Rect hit = {x, y, slot, slot};
        shop_hit_add(WINDOW_SHOP_HIT_LISTING, i, hit);
        if (window_point_in_rect(cx, cy, hit))
        {
            char qty[16], price_buf[32];
            item_format_quantity(st->id, st->pieces, qty, sizeof(qty));
            Wallet tmp;
            wallet_from_copper(&tmp, item_buy_price_copper(st));
            wallet_format(&tmp, price_buf, sizeof(price_buf));
            snprintf(tip_buf, sizeof(tip_buf), "Buy %s x%s (%s)", item_name(st->id), qty,
                     price_buf);
            tooltip = tip_buf;
        }
    }

    for (int i = 0; i < player_cap; i++)
    {
        int row = i / SHOP_UI_PLAYER_COLS;
        int col = i % SHOP_UI_PLAYER_COLS;
        int x = player_x + col * (slot + gap);
        int y = content_y + row * (slot + gap);
        ItemId id = ITEM_NONE;
        uint32_t pieces = 0;
        if (player_inv)
        {
            id = player_inv->slots[i].id;
            pieces = player_inv->slots[i].pieces;
        }
        window_render_inv_slot(x, y, slot, id, pieces);
        SDL_Rect hit = {x, y, slot, slot};
        shop_hit_add(WINDOW_SHOP_HIT_PLAYER_BAG, i, hit);
        if (window_point_in_rect(cx, cy, hit) && id != ITEM_NONE && pieces > 0)
        {
            char qty[16], price_buf[32];
            item_format_quantity(id, pieces, qty, sizeof(qty));
            Wallet tmp;
            wallet_from_copper(&tmp, item_sell_price_copper(&player_inv->slots[i]));
            wallet_format(&tmp, price_buf, sizeof(price_buf));
            snprintf(tip_buf, sizeof(tip_buf), "Sell %s x%s (%s)", item_name(id), qty, price_buf);
            tooltip = tip_buf;
        }
    }

    window_render_text("Click stock to buy, bag to sell", panel_x + pad,
                       panel_y + panel_h - footer_h + 2, window_state.text_color);
    if (tooltip)
        window_render_tooltip(tooltip);
}

WindowShopHit window_shop_hit(int mouse_x, int mouse_y, int *out_index)
{
    for (int i = s_shop_hit_count - 1; i >= 0; i--)
    {
        if (!window_point_in_rect(mouse_x, mouse_y, s_shop_hits[i].rect))
            continue;
        if (out_index)
            *out_index = s_shop_hits[i].index;
        return s_shop_hits[i].kind;
    }
    if (out_index)
        *out_index = -1;
    return WINDOW_SHOP_HIT_NONE;
}


// --- Crafting / recipe book modal -------------------------------------------

#define CRAFT_UI_VISIBLE 8
#define CRAFT_HIT_MAX 64

typedef struct {
    WindowCraftHit kind;
    int index;
    SDL_Rect rect;
} CraftHitRect;

static CraftHitRect s_craft_hits[CRAFT_HIT_MAX];
static int s_craft_hit_count;

static void craft_hit_reset(void)
{
    s_craft_hit_count = 0;
}

static void craft_hit_add(WindowCraftHit kind, int index, SDL_Rect rect)
{
    if (s_craft_hit_count >= CRAFT_HIT_MAX)
        return;
    s_craft_hits[s_craft_hit_count].kind = kind;
    s_craft_hits[s_craft_hit_count].index = index;
    s_craft_hits[s_craft_hit_count].rect = rect;
    s_craft_hit_count++;
}

void window_render_craft(const CraftSession *craft, const Inventory *player_inv)
{
    craft_hit_reset();

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 150);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    const int pad = 6;
    const int header_h = 24;
    const int footer_h = 18;
    const int row_h = 16;
    const int list_w = 150;
    const int detail_w = 90;

    int panel_w = pad * 2 + list_w + pad + detail_w;
    int panel_h = pad + header_h + pad + CRAFT_UI_VISIBLE * row_h + pad + 28 + pad + footer_h;
    if (panel_w > window_state.base_width - 8)
        panel_w = window_state.base_width - 8;
    if (panel_h > window_state.base_height - 8)
        panel_h = window_state.base_height - 8;
    int panel_x = (window_state.base_width - panel_w) / 2;
    int panel_y = (window_state.base_height - panel_h) / 2;

    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_SetRenderDrawColor(window_state.renderer, 50, 45, 40, 255);
    SDL_RenderFillRect(window_state.renderer, &panel);
    SDL_SetRenderDrawColor(window_state.renderer, 180, 150, 100, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel);

    const char *title = "Recipe Book";
    if (craft && !craft->book_mode)
        title = craft_station_name(craft->station);
    window_render_text(title, panel_x + pad, panel_y + 3, window_state.highlight_color);

    SDL_Rect close_rect = {panel_x + panel_w - pad - 40, panel_y + 3, 36, 12};
    bool close_hover = window_point_in_rect(window_state.cursor_x, window_state.cursor_y, close_rect);
    loot_draw_button(close_rect, "X", close_hover);
    craft_hit_add(WINDOW_CRAFT_HIT_CLOSE, 0, close_rect);

    int indices[64];
    int count = craft ? craft_session_list_recipes(craft, indices, 64) : 0;
    int selected = craft ? craft->selected : 0;
    if (selected < 0)
        selected = 0;
    if (count > 0 && selected >= count)
        selected = count - 1;
    int scroll = craft ? craft->scroll : 0;
    if (scroll < 0)
        scroll = 0;
    if (count > CRAFT_UI_VISIBLE && scroll > count - CRAFT_UI_VISIBLE)
        scroll = count - CRAFT_UI_VISIBLE;
    if (count <= CRAFT_UI_VISIBLE)
        scroll = 0;

    int list_x = panel_x + pad;
    int list_y = panel_y + header_h + pad;
    window_render_text("Recipes", list_x, list_y - 10, window_state.text_color);

    const CraftRecipe *sel_recipe = NULL;
    static char tip_buf[128];
    const char *tooltip = NULL;

    for (int row = 0; row < CRAFT_UI_VISIBLE; row++)
    {
        int view_i = scroll + row;
        if (view_i >= count)
            break;
        const CraftRecipe *r = craft_recipe_at(indices[view_i]);
        if (!r)
            continue;
        int y = list_y + row * row_h;
        SDL_Rect hit = {list_x, y, list_w - 4, row_h - 1};
        if (view_i == selected)
        {
            SDL_SetRenderDrawColor(window_state.renderer, 90, 70, 40, 255);
            SDL_RenderFillRect(window_state.renderer, &hit);
            sel_recipe = r;
        }
        craft_hit_add(WINDOW_CRAFT_HIT_RECIPE, view_i, hit);
        window_render_text(r->name, list_x + 2, y + 2,
                           view_i == selected ? window_state.highlight_color
                                              : window_state.text_color);
        if (window_point_in_rect(window_state.cursor_x, window_state.cursor_y, hit))
        {
            snprintf(tip_buf, sizeof(tip_buf), "%s (%s)", r->blurb,
                     craft_station_name(r->station));
            tooltip = tip_buf;
        }
    }

    int detail_x = list_x + list_w + pad;
    window_render_text("Needs", detail_x, list_y - 10, window_state.text_color);
    if (sel_recipe)
    {
        for (uint8_t i = 0; i < sel_recipe->input_count; i++)
        {
            const CraftIngredient *in = &sel_recipe->inputs[i];
            char qty[16], line[48];
            item_format_quantity(in->id, in->pieces, qty, sizeof(qty));
            uint32_t have = player_inv ? inventory_count_pieces(player_inv, in->id) : 0;
            snprintf(line, sizeof(line), "%s %s", item_abbrev(in->id), qty);
            SDL_Color col = window_state.text_color;
            if (have < in->pieces)
            {
                col.r = 200;
                col.g = 80;
                col.b = 80;
            }
            window_render_text(line, detail_x, list_y + (int)i * 12, col);
        }
        char out_line[48], oq[16];
        item_format_quantity(sel_recipe->output_id, sel_recipe->output_pieces, oq, sizeof(oq));
        snprintf(out_line, sizeof(out_line), "=> %s %s", item_abbrev(sel_recipe->output_id), oq);
        window_render_text(out_line, detail_x, list_y + 60, window_state.highlight_color);

        bool can = player_inv && craft_can_craft(player_inv, sel_recipe);
        if (craft && craft->book_mode && sel_recipe->station != CRAFT_STATION_HAND)
            can = false;

        SDL_Rect craft_btn = {detail_x, panel_y + panel_h - footer_h - 22, 70, 14};
        bool hover = window_point_in_rect(window_state.cursor_x, window_state.cursor_y, craft_btn);
        loot_draw_button(craft_btn, can ? "Craft" : "Need", hover && can);
        if (can)
            craft_hit_add(WINDOW_CRAFT_HIT_CRAFT, selected, craft_btn);
    }

    window_render_text(craft && craft->book_mode ? "B closes" : "Select + Craft",
                       panel_x + pad, panel_y + panel_h - footer_h + 2, window_state.text_color);
    if (tooltip)
        window_render_tooltip(tooltip);
}

WindowCraftHit window_craft_hit(int mouse_x, int mouse_y, int *out_index)
{
    for (int i = s_craft_hit_count - 1; i >= 0; i--)
    {
        if (!window_point_in_rect(mouse_x, mouse_y, s_craft_hits[i].rect))
            continue;
        if (out_index)
            *out_index = s_craft_hits[i].index;
        return s_craft_hits[i].kind;
    }
    if (out_index)
        *out_index = -1;
    return WINDOW_CRAFT_HIT_NONE;
}




// Render exit confirmation prompt
void window_render_exit_prompt()
{
    // Semi-transparent overlay (base resolution coordinates)
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 128);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    // Prompt panel (base resolution coordinates)
    int panel_width = 250;
    int panel_height = 120;
    int panel_x = (window_state.base_width - panel_width) / 2;
    int panel_y = (window_state.base_height - panel_height) / 2;

    // Draw panel background
    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Prompt title (base resolution coordinates)
    window_render_text("Exit Game", panel_x + panel_width / 2 - 50, panel_y + 20, window_state.highlight_color);

    // Prompt message (base resolution coordinates)
    window_render_text("Are you sure you want to exit?", panel_x + 25, panel_y + 45, window_state.text_color);

    // Add buttons (base resolution coordinates)
    window_clear_buttons();

    // Cancel button (left)
    int button_width = 80;
    int button_height = 25;
    int button_y = panel_y + panel_height - 35;

    int cancel_x = panel_x + 30;
    window_add_button(cancel_x, button_y, button_width, button_height, "Cancel", BUTTON_CANCEL_EXIT);

    // Confirm button (right)
    int confirm_x = panel_x + panel_width - button_width - 30;
    window_add_button(confirm_x, button_y, button_width, button_height, "Exit", BUTTON_CONFIRM_EXIT);

    // Render the buttons
    window_render_buttons();
}

// Render new game warning prompt
void window_render_new_game_warning()
{
    // Semi-transparent overlay (base resolution coordinates)
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 128);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    // Warning panel (base resolution coordinates)
    int panel_width = 300;
    int panel_height = 140;
    int panel_x = (window_state.base_width - panel_width) / 2;
    int panel_y = (window_state.base_height - panel_height) / 2;

    // Draw panel background
    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 80, 40, 40, 255); // Reddish background for warning
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 120, 60, 60, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Warning title (base resolution coordinates)
    window_render_text("Warning!", panel_x + panel_width / 2 - 40, panel_y + 20, window_state.highlight_color);

    // Warning message (base resolution coordinates)
    window_render_text("Starting a new game will overwrite", panel_x + 25, panel_y + 45, window_state.text_color);
    window_render_text("your existing save file.", panel_x + 25, panel_y + 60, window_state.text_color);
    window_render_text("Are you sure you want to continue?", panel_x + 25, panel_y + 80, window_state.text_color);

    // Add buttons (base resolution coordinates)
    window_clear_buttons();

    // Cancel button (left)
    int button_width = 80;
    int button_height = 25;
    int button_y = panel_y + panel_height - 35;

    int cancel_x = panel_x + 30;
    window_add_button(cancel_x, button_y, button_width, button_height, "Cancel", BUTTON_CANCEL_NEW_GAME);

    // Confirm button (right)
    int confirm_x = panel_x + panel_width - button_width - 30;
    window_add_button(confirm_x, button_y, button_width, button_height, "Continue", BUTTON_CONFIRM_NEW_GAME);

    // Render the buttons
    window_render_buttons();
}

// Render loading bar
void window_render_loading_bar(int current, int total, const char *message)
{
    // Semi-transparent overlay (base resolution coordinates)
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_RenderFillRect(window_state.renderer, &overlay);

    // Loading panel (base resolution coordinates)
    int panel_width = 400;
    int panel_height = 200;
    int panel_x = (window_state.base_width - panel_width) / 2;
    int panel_y = (window_state.base_height - panel_height) / 2;

    // Draw panel background
    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 80, 80, 80, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Title (base resolution coordinates)
    window_render_text("Generating Worlds", panel_x + panel_width / 2 - 80, panel_y + 20, window_state.highlight_color);

    // Progress text (base resolution coordinates)
    char progress_text[128];
    snprintf(progress_text, sizeof(progress_text), "Progress: %d / %d", current, total);
    window_render_text(progress_text, panel_x + 25, panel_y + 50, window_state.text_color);

    // Message (base resolution coordinates)
    if (message)
    {
        window_render_text(message, panel_x + 25, panel_y + 80, window_state.text_color);
    }

    // Progress bar background (base resolution coordinates)
    int bar_width = 350;
    int bar_height = 20;
    int bar_x = panel_x + (panel_width - bar_width) / 2;
    int bar_y = panel_y + 120;

    SDL_Rect bar_bg = {bar_x, bar_y, bar_width, bar_height};
    SDL_SetRenderDrawColor(window_state.renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(window_state.renderer, &bar_bg);

    // Progress bar fill
    if (total > 0)
    {
        int fill_width = (int)((float)current / (float)total * bar_width);
        if (fill_width > 0)
        {
            SDL_Rect bar_fill = {bar_x, bar_y, fill_width, bar_height};
            SDL_SetRenderDrawColor(window_state.renderer, 0, 150, 0, 255); // Green
            SDL_RenderFillRect(window_state.renderer, &bar_fill);
        }
    }

    // Percentage text (base resolution coordinates)
    char percent_text[32];
    if (total > 0)
    {
        int percent = (int)((float)current / (float)total * 100);
        snprintf(percent_text, sizeof(percent_text), "%d%%", percent);
    }
    else
    {
        snprintf(percent_text, sizeof(percent_text), "0%%");
    }
    window_render_text(percent_text, bar_x + bar_width / 2 - 15, bar_y + 25, window_state.text_color);
}

// Render in-game menu
void window_render_in_game_menu()
{
    window_clear();

    // Title
    window_render_text("Game Menu", window_state.base_width / 2 - 35, 15, window_state.highlight_color);

    // Remember previously selected button ID before clearing
    int previously_selected_button_id = 0;
    for (int i = 0; i < window_state.button_count; i++)
    {
        if (window_state.buttons[i].selected)
        {
            previously_selected_button_id = window_state.buttons[i].id;
            break;
        }
    }

    // Clear existing buttons and add menu buttons
    window_clear_buttons();

    // Menu buttons
    int button_width = 120;
    int button_height = 25;
    int button_x = window_state.base_width / 2 - button_width / 2;
    int current_y = 50; // Reduced from 80 to eliminate extra space after title

    window_add_button(button_x, current_y, button_width, button_height, "Resume Game", BUTTON_RESUME);
    current_y += 35;
    window_add_button(button_x, current_y, button_width, button_height, "Save Game", BUTTON_SAVE_GAME);
    current_y += 35;
    window_add_button(button_x, current_y, button_width, button_height, "Load Game", BUTTON_LOAD_GAME);
    current_y += 35;
    window_add_button(button_x, current_y, button_width, button_height, "Settings", BUTTON_SETTINGS);
    current_y += 35;
    window_add_button(button_x, current_y, button_width, button_height, "Main Menu", BUTTON_MAIN_MENU);
    current_y += 35;
    window_add_button(button_x, current_y, button_width, button_height, "Exit Game", BUTTON_EXIT);

    // Restore previous selection if it still exists, otherwise select first button
    bool selection_restored = false;
    if (previously_selected_button_id != 0)
    {
        for (int i = 0; i < window_state.button_count; i++)
        {
            if (window_state.buttons[i].id == previously_selected_button_id)
            {
                window_state.buttons[i].selected = true;
                selection_restored = true;
                break;
            }
        }
    }

    // If no previous selection or button no longer exists, select first button
    if (!selection_restored && window_state.button_count > 0)
    {
        window_state.buttons[0].selected = true;
    }

    // Render the buttons
    window_render_buttons();
}

void window_render_save_browser(void)
{
    window_clear();
    window_render_text("Saved Games", window_state.base_width / 2 - 40, 8, window_state.highlight_color);

    extern GameState *g_game_state;
    GameState *state = g_game_state;
    if (!state)
        return;

    window_clear_buttons();

    if (state->save_browser_count <= 0)
    {
        window_render_text("No saved games", 40, 80, window_state.text_color);
        int back_w = 80;
        window_add_button(window_state.base_width / 2 - back_w / 2, 200, back_w, 18, "Back",
                          BUTTON_SAVE_BROWSER_BACK);
        if (window_state.button_count > 0)
            window_state.buttons[0].selected = true;
        window_render_buttons();
        return;
    }

    const int list_x = 8;
    const int list_w = window_state.base_width - 16;
    const int row_h = 24;
    const int list_y = 24;
    const int visible = SAVE_BROWSER_VISIBLE_ROWS;
    static char row_labels[SAVE_BROWSER_VISIBLE_ROWS][40];

    for (int i = 0; i < visible; i++)
    {
        int idx = state->save_browser_scroll + i;
        if (idx >= state->save_browser_count)
            break;

        const CharacterSaveEntry *e = &state->save_browser_entries[idx];
        int y = list_y + i * row_h;
        bool selected = (idx == state->save_browser_selected);

        SDL_Rect row = {list_x, y, list_w, row_h - 1};
        if (selected)
        {
            SDL_SetRenderDrawColor(window_state.renderer, 0, 90, 180, 255);
            SDL_RenderFillRect(window_state.renderer, &row);
        }

        const char *shown_name = e->data.name[0] ? e->data.name : e->filename;
        if (strcmp(e->filename, "latest.save") == 0)
            snprintf(row_labels[i], sizeof(row_labels[i]), "Latest  %s", shown_name);
        else
            snprintf(row_labels[i], sizeof(row_labels[i]), "%s", shown_name);

        SDL_Color name_color = selected ? (SDL_Color){255, 255, 255, 255} : window_state.text_color;
        window_render_text(row_labels[i], list_x + 4, y + 1, name_color);

        char detail[80];
        snprintf(detail, sizeof(detail), "%s  (%d,%d,%d)",
                 e->data.save_timestamp[0] ? e->data.save_timestamp : e->filename,
                 e->data.x, e->data.y, e->data.z);
        window_render_text(detail, list_x + 4, y + 11,
                           selected ? (SDL_Color){220, 220, 220, 255} : window_state.ui_color);

        window_add_button(list_x, y, list_w, row_h - 1, "", BUTTON_SAVE_BROWSER_SLOT0 + i);
    }

    if (state->save_browser_count > visible)
    {
        char more[32];
        snprintf(more, sizeof(more), "%d/%d", state->save_browser_selected + 1,
                 state->save_browser_count);
        window_render_text(more, window_state.base_width - 40, 8, window_state.ui_color);
    }

    int by = 200;
    int bw = 56;
    int gap = 6;
    int total = bw * 3 + gap * 2;
    int bx = window_state.base_width / 2 - total / 2;

    if (state->save_browser_confirm_delete)
    {
        window_render_text("Delete this save?", 50, 186, window_state.highlight_color);
        window_add_button(bx, by, bw, 18, "Yes", BUTTON_SAVE_BROWSER_CONFIRM_DELETE);
        window_add_button(bx + bw + gap, by, bw, 18, "No", BUTTON_SAVE_BROWSER_CANCEL_DELETE);
        window_add_button(bx + (bw + gap) * 2, by, bw, 18, "Back", BUTTON_SAVE_BROWSER_BACK);
    }
    else
    {
        window_add_button(bx, by, bw, 18, "Load", BUTTON_SAVE_BROWSER_LOAD);
        window_add_button(bx + bw + gap, by, bw, 18, "Delete", BUTTON_SAVE_BROWSER_DELETE);
        window_add_button(bx + (bw + gap) * 2, by, bw, 18, "Back", BUTTON_SAVE_BROWSER_BACK);
    }

    window_render_buttons();
}

// Render settings screen
void window_render_settings()
{
    window_clear();

    // Title
    window_render_text("Settings", window_state.base_width / 2 - 25, 15, window_state.highlight_color);

    // Section headers
    const char *sections[] = {"Video", "Sound", "Preferences"};
    for (int i = 0; i < 3; i++)
    {
        SDL_Color text_color = (i == window_state.settings_section) ? window_state.highlight_color : window_state.text_color;
        window_render_text(sections[i], 25 + i * 60, 40, text_color);
    }

    // Render current section content
    switch (window_state.settings_section)
    {
    case 0: // Video
        render_video_settings();
        break;
    case 1: // Sound
        render_sound_settings();
        break;
    case 2: // Preferences
        render_preferences_settings();
        break;
    }

    // Instructions removed for cleaner UI
}

void render_video_settings()
{
    int y_start = 70;

    // Scale factor
    char scale_text[128];
    snprintf(scale_text, sizeof(scale_text), "Scale: %dx", window_state.scale_factor);
    SDL_Color text_color = (window_state.settings_selection == 0) ? window_state.highlight_color : window_state.text_color;
    window_render_text(scale_text, 35, y_start, text_color);

    // Resolution info
    char res_text[128];
    snprintf(res_text, sizeof(res_text), "Resolution: %dx%d",
             BASE_RESOLUTION_WIDTH * window_state.scale_factor,
             BASE_RESOLUTION_HEIGHT * window_state.scale_factor);
    text_color = (window_state.settings_selection == 1) ? window_state.highlight_color : window_state.text_color;
    window_render_text(res_text, 35, y_start + 20, text_color);

    // Fullscreen status
    char fullscreen_text[128];
    snprintf(fullscreen_text, sizeof(fullscreen_text), "Fullscreen: %s",
             window_state.fullscreen ? "ON" : "OFF");
    text_color = (window_state.settings_selection == 2) ? window_state.highlight_color : window_state.text_color;
    window_render_text(fullscreen_text, 35, y_start + 40, text_color);
}

void render_sound_settings()
{
    int y_start = 70;

    // Text speed
    const char *speed_options[] = {"Slow", "Medium", "Fast"};
    char speed_text[128];
    snprintf(speed_text, sizeof(speed_text), "Text Speed: %s", speed_options[window_state.text_speed]);
    SDL_Color text_color = (window_state.settings_selection == 0) ? window_state.highlight_color : window_state.text_color;
    window_render_text(speed_text, 35, y_start, text_color);

    // Background music enabled/disabled
    char music_enabled_text[128];
    snprintf(music_enabled_text, sizeof(music_enabled_text), "Background Music: %s",
             window_state.background_music_enabled ? "ON" : "OFF");
    text_color = (window_state.settings_selection == 1) ? window_state.highlight_color : window_state.text_color;
    window_render_text(music_enabled_text, 35, y_start + 20, text_color);

    // Menu/UI sounds toggle (separate from background music)
    char menu_sfx_text[128];
    snprintf(menu_sfx_text, sizeof(menu_sfx_text), "Menu Sounds: %s",
             window_state.menu_sounds_enabled ? "ON" : "OFF");
    text_color = (window_state.settings_selection == 2) ? window_state.highlight_color : window_state.text_color;
    window_render_text(menu_sfx_text, 35, y_start + 40, text_color);

    // Master volume
    char master_volume_text[128];
    snprintf(master_volume_text, sizeof(master_volume_text), "Master Volume: %d%%", window_state.master_volume);
    text_color = (window_state.settings_selection == 3) ? window_state.highlight_color : window_state.text_color;
    window_render_text(master_volume_text, 35, y_start + 60, text_color);

    // Music volume
    char music_volume_text[128];
    snprintf(music_volume_text, sizeof(music_volume_text), "Music Volume: %d%%", window_state.music_volume);
    text_color = (window_state.settings_selection == 4) ? window_state.highlight_color : window_state.text_color;
    window_render_text(music_volume_text, 35, y_start + 80, text_color);
}

void render_preferences_settings()
{
    int y_start = 70;

    // Tuning scale
    const char *tuning_options[] = {"432 Hz", "440 Hz"};
    char tuning_text[128];
    snprintf(tuning_text, sizeof(tuning_text), "Tuning Scale: %s", tuning_options[window_state.tuning_scale]);
    SDL_Color text_color = (window_state.settings_selection == 0) ? window_state.highlight_color : window_state.text_color;
    window_render_text(tuning_text, 35, y_start, text_color);

    // Language (placeholder for future internationalization)
    char language_text[128];
    snprintf(language_text, sizeof(language_text), "Language: English");
    text_color = (window_state.settings_selection == 1) ? window_state.highlight_color : window_state.text_color;
    window_render_text(language_text, 35, y_start + 20, text_color);
}

// Handle SDL events
int window_handle_events()
{
    extern GameState *g_game_state; // Declare at function level
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            return -1; // Signal exit
        case SDL_KEYDOWN:
            // Call key callback if set
            if (window_state.key_callback)
            {
                window_state.key_callback(event.key.keysym.sym);
            }

            // Handle special keys
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                // Let the application handle ESC key
                printf("ESC pressed - handled by application\n");
                break;
            }
            break;
        case SDL_TEXTINPUT:
            // Name entry, Global chat, and developer console consume text input.
            {
                extern Console *g_console;
                if (g_console && console_is_open(g_console))
                {
                    console_handle_text(g_console, event.text.text);
                }
                else if (g_game_state && game_log_chat_is_open(&g_game_state->game_log))
                {
                    game_log_chat_text(&g_game_state->game_log, event.text.text);
                }
                else if (g_game_state && g_game_state->show_name_input)
                {
                    window_handle_text_input(event.text.text);
                }
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            window_state.cursor_x = event.button.x / window_state.scale_factor;
            window_state.cursor_y = event.button.y / window_state.scale_factor;
            if (window_state.mouse_callback)
            {
                window_state.mouse_callback(event.button.x, event.button.y, event.button.button);
            }

            // Handle button clicks - use raw mouse coordinates for exit prompt, scaled for others
            int button_id = 0;
            if (g_show_exit_prompt)
            {
                // For exit prompt, use raw coordinates since buttons are scaled
                button_id = window_handle_button_click(event.button.x, event.button.y);
            }
            else
            {
                // For other screens, scale mouse coordinates to base resolution
                int scaled_x = event.button.x / window_state.scale_factor;
                int scaled_y = event.button.y / window_state.scale_factor;
                button_id = window_handle_button_click(scaled_x, scaled_y);
            }

            if (button_id > 0)
            {
                printf("Button clicked: %d\n", button_id);
                // Call button callback if set
                if (window_state.button_callback)
                {
                    window_state.button_callback(button_id);
                }
                // Add a small delay to prevent rapid-fire clicks
                SDL_Delay(50);
            }
            else
            {
                printf("Mouse click at (%d, %d)\n", event.button.x, event.button.y);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            window_state.cursor_x = event.button.x / window_state.scale_factor;
            window_state.cursor_y = event.button.y / window_state.scale_factor;
            if (window_state.mouse_up_callback)
            {
                window_state.mouse_up_callback(event.button.x, event.button.y, event.button.button);
            }
            // Also handle mouse button up for more reliable click detection
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                int button_id = 0;
                if (g_show_exit_prompt)
                {
                    // For exit prompt, use raw coordinates since buttons are scaled
                    button_id = window_handle_button_click(event.button.x, event.button.y);
                }
                else
                {
                    // For other screens, scale mouse coordinates to base resolution
                    int scaled_x = event.button.x / window_state.scale_factor;
                    int scaled_y = event.button.y / window_state.scale_factor;
                    button_id = window_handle_button_click(scaled_x, scaled_y);
                }

                if (button_id > 0)
                {
                    printf("Button clicked (up): %d\n", button_id);
                    // Call button callback if set
                    if (window_state.button_callback)
                    {
                        window_state.button_callback(button_id);
                    }
                }
            }
            break;
        case SDL_MOUSEMOTION:
            if (g_mouse_look_active)
            {
                // Relative mode reports deltas; absolute x/y are meaningless while captured.
                g_mouse_look_dx += event.motion.xrel;
                g_mouse_look_dy += event.motion.yrel;
                break;
            }
            window_state.cursor_x = event.motion.x / window_state.scale_factor;
            window_state.cursor_y = event.motion.y / window_state.scale_factor;
            if (window_state.mouse_motion_callback)
            {
                // Scale mouse coordinates to base resolution
                int scaled_x = event.motion.x / window_state.scale_factor;
                int scaled_y = event.motion.y / window_state.scale_factor;
                window_state.mouse_motion_callback(scaled_x, scaled_y);
            }
            break;
        case SDL_MOUSEWHEEL:
            if (g_game_state && g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL)
            {
                // Positive y = scroll up (content moves down / scroll offset decreases).
                g_game_state->quest_journal_scroll -= event.wheel.y * 16;
            }
            else if (g_game_state && g_game_state->current_screen == GAME_SCREEN_CHARACTER &&
                     window_character_skills_is_open())
            {
                window_character_skills_on_wheel(event.wheel.y);
            }
            else if (g_game_state && g_game_state->game_started &&
                     g_game_state->current_screen == GAME_SCREEN_WORLD &&
                     event.wheel.y != 0)
            {
                // Cycle known skills on the hovered hotbar slot, or slot 1 when none hovered.
                int slot = window_skill_hotbar_hit(window_state.cursor_x, window_state.cursor_y);
                if (slot < 0)
                    slot = 0;
                // SDL: positive y = scroll away from user (typically "up") → previous skill.
                const int dir = event.wheel.y > 0 ? -1 : 1;
                skill_cycle_hotbar(g_game_state, &g_game_state->controls, slot, dir);
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
            gamepad_handle_event(&event);
            break;
        case SDL_WINDOWEVENT:
            if (window_state.window_callback)
            {
                window_state.window_callback(event.window.event);
            }
            switch (event.window.event)
            {
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            {
                // Prevent manual resizing - snap to allowed scale multiples
                int new_width = event.window.data1;
                int new_height = event.window.data2;

                // Calculate the closest allowed scale factor
                int target_scale = new_width / BASE_RESOLUTION_WIDTH;
                if (target_scale < MIN_SCALE_FACTOR)
                    target_scale = MIN_SCALE_FACTOR;
                if (target_scale > MAX_SCALE_FACTOR)
                    target_scale = MAX_SCALE_FACTOR;

                // Only change if the scale factor actually changed
                if (target_scale != window_state.scale_factor)
                {
                    window_set_scale_factor(target_scale);
                    printf("Window snapped to scale %dx (%dx%d)\n",
                           target_scale,
                           BASE_RESOLUTION_WIDTH * target_scale,
                           BASE_RESOLUTION_HEIGHT * target_scale);
                }
                else
                {
                    // Restore the correct size if user tried to resize to invalid dimensions
                    SDL_SetWindowSize(window_state.window,
                                      BASE_RESOLUTION_WIDTH * window_state.scale_factor,
                                      BASE_RESOLUTION_HEIGHT * window_state.scale_factor);
                }
                // Defer viewport recentering until after the next frame render
                g_needs_recentering = true;
                break;
            }
            }
            break;
        }
    }
    return 1;
}

// Set input callbacks
void window_set_key_callback(KeyCallback callback)
{
    window_state.key_callback = callback;
}

void window_set_mouse_callback(MouseCallback callback)
{
    window_state.mouse_callback = callback;
}

void window_set_mouse_up_callback(MouseUpCallback callback)
{
    window_state.mouse_up_callback = callback;
}

void window_set_mouse_motion_callback(MouseMotionCallback callback)
{
    window_state.mouse_motion_callback = callback;
}

void window_set_custom_cursor_active(bool active)
{
    window_state.custom_cursor_active = active;
    SDL_ShowCursor(active ? SDL_DISABLE : SDL_ENABLE);
}

void window_render_custom_cursor(void)
{
    if (!window_state.custom_cursor_active)
        return;

    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);
    spirit_sprite_draw_crosshair(window_state.renderer,
                                 window_state.cursor_x,
                                 window_state.cursor_y);
}

void window_get_cursor_pos(int *x, int *y)
{
    if (x)
        *x = window_state.cursor_x;
    if (y)
        *y = window_state.cursor_y;
}

void window_render_player_spirit_overlay(float facing_yaw, uint32_t stamina, uint32_t stamina_max,
                                         float stamina_meter_alpha)
{
    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);
    int cx = window_state.base_width / 2;
    int cy = window_state.base_height / 2 - spirit_sprite_hover_bob_px();
    spirit_sprite_draw_avatar(window_state.renderer, cx, cy, facing_yaw);
    if (stamina_max > 0 && stamina_meter_alpha > 0.01f)
        spirit_sprite_draw_stamina_meter(window_state.renderer, cx, cy,
                                         (float)stamina, (float)stamina_max,
                                         stamina_meter_alpha);
}

static void window_skill_hotbar_layout(int *out_x0, int *out_y0, int *out_slot_w, int *out_slot_h,
                                       int *out_gap)
{
    const int slot_w = 48;
    const int slot_h = 22;
    const int gap = 4;
    const int total_w = PLAYER_HOTBAR_SLOTS * slot_w + (PLAYER_HOTBAR_SLOTS - 1) * gap;
    if (out_x0)
        *out_x0 = (window_state.base_width - total_w) / 2;
    if (out_y0)
        *out_y0 = window_state.base_height - slot_h - 4;
    if (out_slot_w)
        *out_slot_w = slot_w;
    if (out_slot_h)
        *out_slot_h = slot_h;
    if (out_gap)
        *out_gap = gap;
}

// Returns 0..PLAYER_HOTBAR_SLOTS-1 when the cursor is over a slot, else -1.
static int window_skill_hotbar_hit(int cx, int cy)
{
    int x0, y0, slot_w, slot_h, gap;
    window_skill_hotbar_layout(&x0, &y0, &slot_w, &slot_h, &gap);
    for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
    {
        SDL_Rect slot = {x0 + i * (slot_w + gap), y0, slot_w, slot_h};
        if (window_point_in_rect(cx, cy, slot))
            return i;
    }
    return -1;
}

static void window_render_skill_hotbar(GameState *state)
{
    if (!state || !state->game_started || state->current_screen != GAME_SCREEN_WORLD)
        return;

    int x0, y0, slot_w, slot_h, gap;
    window_skill_hotbar_layout(&x0, &y0, &slot_w, &slot_h, &gap);
    const int total_w = PLAYER_HOTBAR_SLOTS * slot_w + (PLAYER_HOTBAR_SLOTS - 1) * gap;

    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);

    if (player_controls_town_portal_charging(&state->controls))
    {
        float progress = player_controls_town_portal_charge_progress(&state->controls);
        const int bar_w = total_w;
        const int bar_h = 6;
        const int bar_x = x0;
        const int bar_y = y0 - bar_h - 4;

        SDL_Rect bg = {bar_x, bar_y, bar_w, bar_h};
        SDL_SetRenderDrawColor(window_state.renderer, 24, 28, 40, 200);
        SDL_RenderFillRect(window_state.renderer, &bg);
        SDL_SetRenderDrawColor(window_state.renderer, 90, 120, 160, 255);
        SDL_RenderDrawRect(window_state.renderer, &bg);

        int fill_w = (int)((float)bar_w * progress + 0.5f);
        if (fill_w > 0)
        {
            if (fill_w > bar_w)
                fill_w = bar_w;
            SDL_Rect fill = {bar_x, bar_y, fill_w, bar_h};
            SDL_SetRenderDrawColor(window_state.renderer, 120, 190, 230, 230);
            SDL_RenderFillRect(window_state.renderer, &fill);
        }
    }

    const char *tooltip = NULL;
    static char tip_buf[192];
    int cx = window_state.cursor_x;
    int cy = window_state.cursor_y;

    for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
    {
        SkillId skill = player_controls_hotbar_skill(&state->controls, i);
        SDL_Rect slot = {x0 + i * (slot_w + gap), y0, slot_w, slot_h};

        SDL_Color fill = {30, 30, 40, 180};
        SDL_Color border = {90, 90, 110, 255};
        const SkillDef *def = skill_def(skill);
        if (def)
        {
            fill = (SDL_Color){def->fill_r, def->fill_g, def->fill_b, 200};
            border = (SDL_Color){def->border_r, def->border_g, def->border_b, 255};
        }
        if (skill != SKILL_NONE && skill_highlighted(state, skill))
            border = (SDL_Color){200, 235, 255, 255};

        bool ready = player_controls_skill_ready(state, &state->controls, skill);
        if (skill != SKILL_NONE && !ready)
        {
            fill.r = (Uint8)(fill.r * 0.45f);
            fill.g = (Uint8)(fill.g * 0.45f);
            fill.b = (Uint8)(fill.b * 0.45f);
        }

        SDL_SetRenderDrawColor(window_state.renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(window_state.renderer, &slot);
        SDL_SetRenderDrawColor(window_state.renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(window_state.renderer, &slot);

        char label[8];
        snprintf(label, sizeof(label), "%d", i + 1);
        window_render_text(label, slot.x + 3, slot.y + 2, (SDL_Color){230, 230, 240, 255});

        const char *abbrev = skill_abbrev(skill);
        if (abbrev && abbrev[0])
            window_render_text(abbrev, slot.x + 14, slot.y + 6,
                               (SDL_Color){255, 220, 160, 255});

        if (window_point_in_rect(cx, cy, slot))
        {
            window_format_skill_tooltip(tip_buf, sizeof(tip_buf), skill, state->player);
            tooltip = tip_buf;
        }
    }

    if (tooltip)
        window_render_tooltip(tooltip);
}

void window_set_window_callback(WindowCallback callback)
{
    window_state.window_callback = callback;
}

// Button management functions
void window_add_button(int x, int y, int width, int height, const char *text, int id)
{
    if (window_state.button_count >= 16)
        return;

    UIButton *button = &window_state.buttons[window_state.button_count];
    button->x = x;
    button->y = y;
    button->width = width;
    button->height = height;
    button->text = text;
    button->id = id;
    button->normal_color = (SDL_Color){80, 80, 80, 255};
    button->hover_color = (SDL_Color){120, 120, 120, 255};
    button->selected_color = (SDL_Color){0, 120, 255, 255}; // Blue for selected
    button->text_color = (SDL_Color){255, 255, 255, 255};
    button->selected = false;

    window_state.button_count++;
}

void window_clear_buttons()
{
    window_state.button_count = 0;
}

int window_handle_button_click(int mouse_x, int mouse_y)
{
    for (int i = 0; i < window_state.button_count; i++)
    {
        UIButton *button = &window_state.buttons[i];
        if (mouse_x >= button->x && mouse_x <= button->x + button->width &&
            mouse_y >= button->y && mouse_y <= button->y + button->height)
        {
            if (window_state.button_callback)
            {
                window_state.button_callback(button->id);
            }
            return button->id;
        }
    }
    return 0;
}

void window_render_buttons()
{
    for (int i = 0; i < window_state.button_count; i++)
    {
        UIButton *button = &window_state.buttons[i];

        // Empty labels are hit targets only (the save browser paints its own rows).
        if (!button->text || !button->text[0])
            continue;

        // Choose background color based on selection state
        SDL_Color bg_color = button->selected ? button->selected_color : button->normal_color;

        // Use base coordinates for button rendering
        SDL_Rect button_rect = {
            button->x,
            button->y,
            button->width,
            button->height};
        SDL_SetRenderDrawColor(window_state.renderer,
                               bg_color.r, bg_color.g,
                               bg_color.b, bg_color.a);
        SDL_RenderFillRect(window_state.renderer, &button_rect);

        // Draw button border - thicker for selected buttons
        SDL_Color border_color = button->selected ? (SDL_Color){255, 255, 255, 255} : button->text_color;
        SDL_SetRenderDrawColor(window_state.renderer,
                               border_color.r, border_color.g,
                               border_color.b, border_color.a);

        // Draw thicker border for selected buttons
        if (button->selected)
        {
            SDL_Rect outer_rect = {
                button->x - 2,
                button->y - 2,
                button->width + 4,
                button->height + 4};
            SDL_RenderDrawRect(window_state.renderer, &outer_rect);
        }
        SDL_RenderDrawRect(window_state.renderer, &button_rect);

        // Draw button text (scaled coordinates)
        int text_x = button->x + (button->width - strlen(button->text) * window_state.cell_width) / 2;
        int text_y = button->y + (button->height - window_state.cell_height) / 2;
        window_render_text(button->text, text_x, text_y, button->text_color);
    }
}

void window_set_button_callback(ButtonCallback callback)
{
    window_state.button_callback = callback;
}

// Get window dimensions
void window_get_size(int *width, int *height)
{
    *width = window_state.width;
    *height = window_state.height;
}

// Resolution and scaling functions
void window_set_scale_factor(int scale_factor)
{
    if (scale_factor >= MIN_SCALE_FACTOR && scale_factor <= MAX_SCALE_FACTOR)
    {
        window_state.scale_factor = scale_factor;
        int new_width = BASE_RESOLUTION_WIDTH * scale_factor;
        int new_height = BASE_RESOLUTION_HEIGHT * scale_factor;
        SDL_SetWindowSize(window_state.window, new_width, new_height);
        window_state.width = new_width;
        window_state.height = new_height;
    }
}

int window_get_scale_factor()
{
    return window_state.scale_factor;
}

void window_get_base_size(int *width, int *height)
{
    *width = window_state.base_width;
    *height = window_state.base_height;
}

void window_get_scaled_size(int *width, int *height)
{
    *width = window_state.width;
    *height = window_state.height;
}

// Fullscreen functions
void window_toggle_fullscreen()
{
    if (window_state.fullscreen)
    {
        SDL_SetWindowFullscreen(window_state.window, 0);
        window_state.fullscreen = 0;
        // Restore the last scale factor
        window_set_scale_factor(window_state.scale_factor);
    }
    else
    {
        SDL_SetWindowFullscreen(window_state.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        window_state.fullscreen = 1;
    }
}

void window_set_fullscreen(int fullscreen)
{
    if (fullscreen != window_state.fullscreen)
    {
        window_toggle_fullscreen();
    }
}

int window_is_fullscreen()
{
    return window_state.fullscreen;
}

// Text speed functions
void window_set_text_speed(int speed)
{
    // This function will be called from the test program
    // The actual text speed is managed in the test program
}

int window_get_text_speed()
{
    return window_state.text_speed;
}

// Settings navigation functions
void window_settings_next_section()
{
    window_state.settings_section = (window_state.settings_section + 1) % 3;
    window_state.settings_selection = 0; // Reset selection when changing sections
}

void window_settings_prev_section()
{
    window_state.settings_section = (window_state.settings_section - 1 + 3) % 3;
    window_state.settings_selection = 0; // Reset selection when changing sections
}

void window_settings_next_selection()
{
    // Max selections per section
    int max_selections[] = {3, 5, 2}; // Video: 3 options, Sound: 5 options, Preferences: 2 options
    window_state.settings_selection = (window_state.settings_selection + 1) % max_selections[window_state.settings_section];
}

void window_settings_prev_selection()
{
    // Max selections per section
    int max_selections[] = {3, 5, 2}; // Video: 3 options, Sound: 5 options, Preferences: 2 options
    window_state.settings_selection = (window_state.settings_selection - 1 + max_selections[window_state.settings_section]) % max_selections[window_state.settings_section];
}

int window_get_settings_section()
{
    return window_state.settings_section;
}

int window_get_settings_selection()
{
    return window_state.settings_selection;
}

// Tuning scale functions
void window_set_tuning_scale(int scale)
{
    if (scale >= 0 && scale <= 1)
    {
        window_state.tuning_scale = scale;
    }
}

int window_get_tuning_scale()
{
    return window_state.tuning_scale;
}

// Settings change functions
void window_change_text_speed()
{
    window_state.text_speed = (window_state.text_speed + 1) % 3; // Cycle through 0, 1, 2
    // Save settings after change
    window_save_settings();
}

void window_change_background_music(BackgroundMusicSystem *music)
{
    window_state.background_music_enabled = !window_state.background_music_enabled;
    // Apply to background music system
    if (music)
    {
        background_music_set_enabled(music, window_state.background_music_enabled);
    }
    // Save settings after change
    window_save_settings();
}

void window_change_menu_sounds(UISoundSystem *ui_sounds)
{
    window_state.menu_sounds_enabled = !window_state.menu_sounds_enabled;
    if (ui_sounds)
    {
        ui_sounds_set_enabled(ui_sounds, window_state.menu_sounds_enabled);
    }
    window_save_settings();
}

void window_change_master_volume(BackgroundMusicSystem *music)
{
    window_state.master_volume = (window_state.master_volume + 10) % 110; // 0-100, step by 10
    if (window_state.master_volume > 100)
        window_state.master_volume = 0;
    // Apply to background music system
    if (music)
    {
        float volume = window_state.master_volume / 100.0f; // Convert 0-100 to 0.0-1.0
        background_music_set_master_volume(music, volume);
    }
    // Save settings after change
    window_save_settings();
}

void window_change_music_volume(BackgroundMusicSystem *music)
{
    window_state.music_volume = (window_state.music_volume + 10) % 110; // 0-100, step by 10
    if (window_state.music_volume > 100)
        window_state.music_volume = 0;
    // Apply to background music system
    if (music)
    {
        float volume = window_state.music_volume / 100.0f; // Convert 0-100 to 0.0-1.0
        background_music_set_music_volume(music, volume);
    }
    // Save settings after change
    window_save_settings();
}

void window_change_tuning_scale(BackgroundMusicSystem *music)
{
    window_state.tuning_scale = (window_state.tuning_scale + 1) % 2; // Toggle between 0 and 1
    // Apply to background music system
    if (music)
    {
        float tuning_scale = (window_state.tuning_scale == 0) ? 432.0f : 440.0f;
        background_music_set_tuning_scale(music, tuning_scale);
    }
    // Save settings after change
    window_save_settings();
}

// Sync window settings with background music system
void window_sync_settings_with_audio(BackgroundMusicSystem *music)
{
    if (!music)
        return;

    // Sync master volume
    float master_vol = background_music_get_master_volume(music);
    window_state.master_volume = (int)(master_vol * 100.0f);

    // Sync music volume
    float music_vol = background_music_get_music_volume(music);
    window_state.music_volume = (int)(music_vol * 100.0f);

    // Sync background music enabled
    window_state.background_music_enabled = background_music_is_enabled(music);

    // Sync tuning scale
    float tuning = background_music_get_tuning_scale(music);
    window_state.tuning_scale = (tuning == 432.0f) ? 0 : 1;
}

void window_change_scale_factor()
{
    // Cycle through scale factors: 2, 3, 4, 5, 6
    int current_scale = window_state.scale_factor;
    int new_scale;
    if (current_scale < 6)
    {
        new_scale = current_scale + 1;
    }
    else
    {
        new_scale = 2;
    }

    // Use window_set_scale_factor to properly resize the window
    window_set_scale_factor(new_scale);

    // Save settings after change
    window_save_settings();
}

void window_change_fullscreen()
{
    window_state.fullscreen = !window_state.fullscreen;
    if (window_state.fullscreen)
    {
        window_set_fullscreen_with_optimal_scale();
    }
    else
    {
        window_set_windowed_with_optimal_scale();
    }
}

// Activate the currently selected setting
void window_activate_setting(BackgroundMusicSystem *music, UISoundSystem *ui_sounds)
{
    switch (window_state.settings_section)
    {
    case 0: // Video
        switch (window_state.settings_selection)
        {
        case 0: // Scale factor
            window_change_scale_factor();
            break;
        case 1: // Resolution (read-only)
            break;
        case 2: // Fullscreen
            window_change_fullscreen();
            break;
        }
        break;
    case 1: // Sound
        switch (window_state.settings_selection)
        {
        case 0: // Text speed
            window_change_text_speed();
            break;
        case 1: // Background music
            window_change_background_music(music);
            break;
        case 2: // Menu sounds
            window_change_menu_sounds(ui_sounds);
            break;
        case 3: // Master volume
            window_change_master_volume(music);
            break;
        case 4: // Music volume
            window_change_music_volume(music);
            break;
        }
        break;
    case 2: // Preferences
        switch (window_state.settings_selection)
        {
        case 0: // Tuning scale
            window_change_tuning_scale(music);
            break;
        case 1: // Other preferences
            break;
        }
        break;
    }
}

// Calculate the optimal scale factor for the current screen
int window_calculate_optimal_scale_factor()
{
    SDL_DisplayMode display_mode;
    if (SDL_GetCurrentDisplayMode(0, &display_mode) != 0)
    {
        printf("Failed to get display mode, using default scale factor\n");
        return DEFAULT_SCALE_FACTOR;
    }

    // Calculate how many times the base resolution fits in the screen
    int scale_x = display_mode.w / BASE_RESOLUTION_WIDTH;
    int scale_y = display_mode.h / BASE_RESOLUTION_HEIGHT;

    // Use the smaller scale to ensure it fits completely
    int optimal_scale = (scale_x < scale_y) ? scale_x : scale_y;

    // Clamp to valid range
    if (optimal_scale < MIN_SCALE_FACTOR)
    {
        optimal_scale = MIN_SCALE_FACTOR;
    }
    else if (optimal_scale > MAX_SCALE_FACTOR)
    {
        optimal_scale = MAX_SCALE_FACTOR;
    }

    printf("Screen resolution: %dx%d\n", display_mode.w, display_mode.h);
    printf("Base resolution: %dx%d\n", BASE_RESOLUTION_WIDTH, BASE_RESOLUTION_HEIGHT);
    printf("Calculated optimal scale factor: %d\n", optimal_scale);

    return optimal_scale;
}

// Set fullscreen mode with optimal scale factor
void window_set_fullscreen_with_optimal_scale()
{
    // Calculate optimal scale factor
    int optimal_scale = window_calculate_optimal_scale_factor();

    // Set the scale factor
    window_state.scale_factor = optimal_scale;

    // Set fullscreen mode
    SDL_SetWindowFullscreen(window_state.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    window_state.fullscreen = 1;

    // Update window dimensions
    window_state.width = BASE_RESOLUTION_WIDTH * optimal_scale;
    window_state.height = BASE_RESOLUTION_HEIGHT * optimal_scale;

    printf("Set fullscreen mode with scale factor %d\n", optimal_scale);
}

void window_set_windowed_with_optimal_scale()
{
    // Calculate optimal scale factor
    int optimal_scale = window_calculate_optimal_scale_factor();

    // Set the scale factor
    window_state.scale_factor = optimal_scale;

    // Ensure windowed mode
    SDL_SetWindowFullscreen(window_state.window, 0);
    window_state.fullscreen = 0;

    // Update window dimensions for windowed mode
    window_state.width = BASE_RESOLUTION_WIDTH * optimal_scale;
    window_state.height = BASE_RESOLUTION_HEIGHT * optimal_scale;

    // Resize window to match the scaled dimensions
    SDL_SetWindowSize(window_state.window, window_state.width, window_state.height);

    // Center the window on screen
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0)
    {
        int window_x = (dm.w - window_state.width) / 2;
        int window_y = (dm.h - window_state.height) / 2;
        SDL_SetWindowPosition(window_state.window, window_x, window_y);
    }

    printf("Set windowed mode with scale factor %d (window size: %dx%d)\n",
           optimal_scale, window_state.width, window_state.height);
}

// Center the rendering on the screen
void window_center_rendering()
{
    // Get current window size
    int window_width, window_height;
    SDL_GetWindowSize(window_state.window, &window_width, &window_height);

    // Calculate centered position
    int render_width = BASE_RESOLUTION_WIDTH * window_state.scale_factor;
    int render_height = BASE_RESOLUTION_HEIGHT * window_state.scale_factor;

    int offset_x = (window_width - render_width) / 2;
    int offset_y = (window_height - render_height) / 2;

    // Set the viewport to center the rendering
    SDL_Rect viewport = {offset_x, offset_y, render_width, render_height};
    SDL_RenderSetViewport(window_state.renderer, &viewport);
}

// Selection functions
void window_select_button(int button_id)
{
    // Clear all selections first
    for (int i = 0; i < window_state.button_count; i++)
    {
        window_state.buttons[i].selected = false;
    }

    // Select the specified button
    for (int i = 0; i < window_state.button_count; i++)
    {
        if (window_state.buttons[i].id == button_id)
        {
            window_state.buttons[i].selected = true;
            break;
        }
    }
}

void window_clear_selection()
{
    for (int i = 0; i < window_state.button_count; i++)
    {
        window_state.buttons[i].selected = false;
    }
}

int window_get_selected_button()
{
    for (int i = 0; i < window_state.button_count; i++)
    {
        if (window_state.buttons[i].selected)
        {
            return window_state.buttons[i].id;
        }
    }
    return 0; // No selection
}

void window_next_selection()
{
    printf("DEBUG: window_next_selection() called, button_count=%d\n", window_state.button_count);

    if (window_state.button_count == 0)
    {
        printf("DEBUG: No buttons to select\n");
        return;
    }

    int current_selected = -1;
    for (int i = 0; i < window_state.button_count; i++)
    {
        if (window_state.buttons[i].selected)
        {
            current_selected = i;
            printf("DEBUG: Found current selection at index %d (button ID %d)\n", i, window_state.buttons[i].id);
            break;
        }
    }

    // If no button is selected, select the first one
    if (current_selected == -1)
    {
        window_state.buttons[0].selected = true;
        printf("DEBUG: No button selected, selecting first button (ID %d)\n", window_state.buttons[0].id);
        // Play highlight sound for navigation
        extern void play_highlight_sound();
        play_highlight_sound();
        return;
    }

    // Move to next button
    window_state.buttons[current_selected].selected = false;
    int next_index = (current_selected + 1) % window_state.button_count;
    window_state.buttons[next_index].selected = true;
    printf("DEBUG: Moved selection from index %d to %d (button ID %d)\n",
           current_selected, next_index, window_state.buttons[next_index].id);

    // Play highlight sound for navigation
    extern void play_highlight_sound();
    play_highlight_sound();
}

void window_prev_selection()
{
    if (window_state.button_count == 0)
        return;

    int current_selected = -1;
    for (int i = 0; i < window_state.button_count; i++)
    {
        if (window_state.buttons[i].selected)
        {
            current_selected = i;
            break;
        }
    }

    // If no button is selected, select the first one
    if (current_selected == -1)
    {
        window_state.buttons[0].selected = true;
        // Play highlight sound for navigation
        extern void play_highlight_sound();
        play_highlight_sound();
        return;
    }

    // Move to previous button
    window_state.buttons[current_selected].selected = false;
    int prev_index = (current_selected - 1 + window_state.button_count) % window_state.button_count;
    window_state.buttons[prev_index].selected = true;

    // Play highlight sound for navigation
    extern void play_highlight_sound();
    play_highlight_sound();
}

void window_activate_selection()
{
    int selected_id = window_get_selected_button();
    if (selected_id != 0 && window_state.button_callback)
    {
        window_state.button_callback(selected_id);
    }
}

// Convert screen coordinates to world coordinates for isomorphic view (legacy integer version)
int window_screen_to_world_coords(int screen_x, int screen_y, int *world_x, int *world_y, int *world_z)
{
    if (!g_isometric_renderer)
        return 0;

    int world_index;
    isometric_screen_to_world(g_isometric_renderer, screen_x, screen_y,
                              world_x, world_y, world_z, &world_index);

    // The isometric conversion gives us a position relative to camera
    // We need to find the appropriate walkable position at this screen location

    // Get the world to check for voxels
    extern GameState *g_game_state;
    if (!g_game_state || !g_game_state->current_world)
        return 0;

    World *world = g_game_state->current_world;

    // Cast a ray from the top down to find the first solid voxel, then return the air space above it
    // isometric_screen_to_world returns absolute world coordinates on the camera plane
    int base_x = *world_x;
    int base_y = *world_y;

    // Check bounds first
    if (base_x < 0 || base_x >= (int)world->width ||
        base_y < 0 || base_y >= (int)world->height)
    {
        return 0; // Out of bounds
    }

    // Start from the top of the world and work down to find solid ground
    for (int test_z = world->depth - 1; test_z >= 0; test_z--)
    {
        Voxel *voxel = world_get_voxel(world, base_x, base_y, test_z);
        if (voxel && voxel->type != VOXEL_AIR)
        {
            // Found a solid voxel - the walkable position is the air space above it
            int walkable_z = test_z + 1;

            // Make sure the walkable position is within bounds and has headroom
            if (walkable_z < (int)world->depth - 1) // Need headroom (Z+1 should also be air)
            {
                Voxel *walkable_voxel = world_get_voxel(world, base_x, base_y, walkable_z);
                Voxel *headroom_voxel = world_get_voxel(world, base_x, base_y, walkable_z + 1);

                if (walkable_voxel && walkable_voxel->type == VOXEL_AIR &&
                    headroom_voxel && headroom_voxel->type == VOXEL_AIR)
                {
                    // This is a valid walkable position
                    *world_x = base_x;
                    *world_y = base_y;
                    *world_z = walkable_z;
                    return 1; // Success
                }
            }
        }
    }

    // If no solid voxel found, return ground level + 1 (air space above ground)
    *world_x = base_x;
    *world_y = base_y;
    *world_z = 1;
    return 1; // Success (fallback position)
}

// Convert screen coordinates to floating-point world coordinates for smooth movement
int window_screen_to_world_coords_float(int screen_x, int screen_y, float *world_x, float *world_y, float *world_z)
{
    if (!g_isometric_renderer || !world_x || !world_y || !world_z)
        return 0;

    int world_index;
    int temp_x, temp_y, temp_z;
    isometric_screen_to_world(g_isometric_renderer, screen_x, screen_y,
                              &temp_x, &temp_y, &temp_z, &world_index);

    // Convert to floating-point and add offset to center of "voxel space"
    *world_x = (float)temp_x + 0.5f;
    *world_y = (float)temp_y + 0.5f;

    // Get the world to check for ground level
    extern GameState *g_game_state;
    if (!g_game_state || !g_game_state->current_world)
    {
        *world_z = (float)temp_z + 0.5f;
        return 1;
    }

    World *world = g_game_state->current_world;

    // Check bounds
    if (temp_x < 0 || temp_x >= (int)world->width ||
        temp_y < 0 || temp_y >= (int)world->height)
    {
        return 0; // Out of bounds
    }

    // Cast a ray from the top down to find the ground level, then position above it
    for (int test_z = world->depth - 1; test_z >= 0; test_z--)
    {
        Voxel *voxel = world_get_voxel(world, temp_x, temp_y, test_z);
        if (voxel && voxel->type != VOXEL_AIR)
        {
            // Found a solid voxel - place the player in the air space above it
            *world_z = (float)(test_z + 1) + 0.5f; // Center of the air voxel above solid ground
            return 1; // Success
        }
    }

    // If no solid voxel found, use ground level + 1
    *world_z = 1.5f; // Center of voxel at Z=1
    return 1; // Success (fallback position)
}

// Convert world coordinates to screen coordinates for isomorphic view
void window_world_to_screen_coords(int world_x, int world_y, int world_z, int player_x, int player_y, int player_z, int *screen_x, int *screen_y)
{
    if (!g_isometric_renderer)
        return;

    extern GameState *g_game_state;
    if (g_game_state)
        game_state_sync_isometric_renderer(g_game_state, g_isometric_renderer);
    else
        isometric_renderer_set_camera(g_isometric_renderer, player_x, player_y, player_z);

    isometric_world_to_screen(g_isometric_renderer, world_x, world_y, world_z, 0,
                              screen_x, screen_y);
}

// Handle mouse input for isomorphic view
void window_handle_isomorphic_mouse(int mouse_x, int mouse_y, int button)
{
    // This function is deprecated - mouse handling is now done through verse_client.c
    // Left here for compatibility with window_interactive_test.c
    // The actual movement system is handled by handle_mouse_click() in verse_client.c
    (void)mouse_x; // Unused parameter
    (void)mouse_y; // Unused parameter
    (void)button;  // Unused parameter
}

// Render isomorphic world view centered on character's spirit
void window_render_isomorphic_world(World *world, int player_x, int player_y, int player_z, const char *player_name)
{
    // Set the base render texture as the target for proper scaling
    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);

    // Clear the base texture
    SDL_SetRenderDrawColor(window_state.renderer, 25, 25, 50, 255); // Dark blue background
    SDL_RenderClear(window_state.renderer);

    // Check if world and renderer are valid
    if (!world)
    {
        return;
    }

    // Draw background via the new map framebuffer (overhead projection filling entire base)
    // 1) Render the full-scene world view via isometric renderer (GPU with CPU fallback)
    {
        if (g_isometric_renderer) {
            extern GameState *g_game_state;
            if (g_game_state) {
                game_state_sync_isometric_renderer(g_game_state, g_isometric_renderer);
                isometric_renderer_set_game_worlds(g_isometric_renderer, g_game_state->game_worlds);
                isometric_renderer_set_auto_center(g_isometric_renderer, false);
            }

            // Temporarily point renderer at the provided world
            GameWorlds *gw = g_isometric_renderer->game_worlds;
            World *saved_home = NULL;
            GameWorlds tmp = (GameWorlds){0};
            bool used_tmp = false;
            if (gw) {
                saved_home = gw->home_world;
                gw->home_world = world;
            } else {
                tmp.home_world = world;
                isometric_renderer_set_game_worlds(g_isometric_renderer, &tmp);
                used_tmp = true;
            }
            isometric_renderer_render_gpu(g_isometric_renderer, window_state.renderer);
            if (gw) {
                gw->home_world = saved_home;
            }
            if (used_tmp) {
                // Restore to NULL to avoid dangling pointer
                isometric_renderer_set_game_worlds(g_isometric_renderer, NULL);
            }
        }
    }

    // 2) Render a mini top-down map in the upper-right corner over the isometric background
    //    Skip this overlay when on the main menu. We gate via a parameter instead of global to
    //    avoid referencing globals in this utility.
    bool should_show_minimap = true;
    extern GameState *g_game_state;
    if (g_game_state && (g_game_state->current_screen == GAME_SCREEN_MAIN_MENU ||
                         g_game_state->current_screen == GAME_SCREEN_MAP)) {
        should_show_minimap = false;
    }
    if (should_show_minimap)
    {
        window_render_cached_minimap(world, player_x, player_y, player_z);
    }

    // Player info panel removed for performance

    // Movement destination panel removed for performance

    // Render selection information (replaces old voxel info box)
    window_render_selection_info();

    // Spirit avatar HUD: always drawn at screen center so facing/stamina stay visible
    // even if the isometric GPU pass skipped its overlay.
    {
        extern GameState *g_game_state;
        if (g_game_state && g_game_state->game_started &&
            g_game_state->current_screen == GAME_SCREEN_WORLD)
        {
            if (!player_controls_is_dominating(g_game_state))
            {
                uint32_t stam = g_game_state->player ? (uint32_t)(g_game_state->player->stamina + 0.5f) : 0;
                uint32_t stam_max = g_game_state->player
                    ? (uint32_t)(actor_max_stamina(g_game_state->player) + 0.5f)
                    : (uint32_t)PLAYER_DEFAULT_STAMINA_MAX;
                window_render_player_spirit_overlay(g_game_state->controls.facing_yaw, stam, stam_max,
                                                   g_game_state->stamina_meter_alpha);
            }
            window_render_skill_hotbar(g_game_state);
            window_render_nav_aide(g_game_state);
            if (dialogue_active(&g_game_state->dialogue))
                window_render_dialogue_box(dialogue_speaker(&g_game_state->dialogue),
                                           dialogue_current_text(&g_game_state->dialogue),
                                           dialogue_current_char(&g_game_state->dialogue),
                                           dialogue_line_index(&g_game_state->dialogue),
                                           dialogue_line_count(&g_game_state->dialogue));
            else
                window_render_game_log(&g_game_state->game_log);
        }
    }

    // Legacy voxel information box (keep for now for compatibility)
    if (g_show_voxel_info)
    {
        // Get voxel type from the world
        VoxelType voxel_type = VOXEL_AIR;
        const char *type_name = "Air";

        // Try to get the voxel from the home world first
        extern GameState *g_game_state;
        if (g_game_state && g_game_state->game_worlds && g_game_state->game_worlds->home_world)
        {
            World *world = g_game_state->game_worlds->home_world;
            if (g_info_voxel_x >= 0 && g_info_voxel_x < (int)world->width &&
                g_info_voxel_y >= 0 && g_info_voxel_y < (int)world->height &&
                g_info_voxel_z >= 0 && g_info_voxel_z < (int)world->depth)
            {
                Voxel *voxel = world_get_voxel(world, g_info_voxel_x, g_info_voxel_y, g_info_voxel_z);
                if (voxel)
                {
                    voxel_type = voxel->type;
                }

                // Convert voxel type to readable name
                switch (voxel_type)
                {
                case VOXEL_AIR:
                    type_name = "Air";
                    break;
                case VOXEL_GRASS:
                    type_name = "Grass";
                    break;
                case VOXEL_SOIL:
                    type_name = "Dirt";
                    break;
                case VOXEL_STONE:
                    type_name = "Stone";
                    break;
                case VOXEL_WOOD:
                    type_name = "Wood";
                    break;
                case VOXEL_LEAVES:
                    type_name = "Leaves";
                    break;
                case VOXEL_WATER:
                    type_name = "Water";
                    break;
                case VOXEL_SAND:
                    type_name = "Sand";
                    break;
                case VOXEL_BEDROCK:
                    type_name = "Bedrock";
                    break;

                case VOXEL_SPRING:
                    type_name = "Spring";
                    break;
                default:
                    type_name = "Unknown";
                    break;
                }
            }
        }

        char voxel_info[256];
        snprintf(voxel_info, sizeof(voxel_info), "Position: (%d,%d,%d)\nType: %s (%d)\n\nRight-click to move\nClick elsewhere to close",
                 g_info_voxel_x, g_info_voxel_y, g_info_voxel_z, type_name, voxel_type);

        // Position the info box in the center-left area
        window_render_ui_panel("Voxel Info", voxel_info, 20, window_state.base_height / 2 - 50, 180, 100);
    }

    // Ensure subsequent UI draws (e.g., main menu) continue on the base texture
    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);
}

// Full-screen isometric map: current world, loaded horizontal neighbours, fog of war.
// Zooms out so a 3×3 neighbourhood fits, keeps the camera on the player, and restores renderer
// settings afterwards so the play view is unchanged when the map closes.
void window_render_world_map(World *world, int player_x, int player_y, int player_z)
{
    (void)player_x;
    (void)player_y;
    (void)player_z;

    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);
    SDL_SetRenderDrawColor(window_state.renderer, 20, 22, 35, 255);
    SDL_RenderClear(window_state.renderer);

    if (!world || !g_isometric_renderer)
        return;

    extern GameState *g_game_state;
    IsometricRenderer *ir = g_isometric_renderer;

    const float saved_zoom = ir->zoom_scale;
    const bool saved_culling = ir->disable_culling;
    const int saved_radius = ir->neighbor_inclusion_radius;
    FogAtlas *saved_fog = ir->fog;
    World *saved_edges[125];
    for (int i = 0; i < 125; i++)
        saved_edges[i] = ir->edge_worlds[i];

    if (g_game_state)
    {
        game_state_sync_isometric_renderer(g_game_state, ir);
        isometric_renderer_set_game_worlds(ir, g_game_state->game_worlds);
    }
    isometric_renderer_set_auto_center(ir, false);

    // Drop vertical neighbours — the map is a horizontal overview only.
    for (int i = 1; i < 125; i++)
    {
        if (ir->world_offsets[i].dz != 0)
            ir->edge_worlds[i] = NULL;
    }

    // Fit centre + one ring of horizontal neighbours (3 worlds across).
    ir->neighbor_inclusion_radius = 1;
    ir->zoom_scale = 1.0f / 3.0f;
    isometric_renderer_set_disable_culling(ir, true);

    // Fog of war is map-only (toggle with console `fog`). Play views never mask terrain.
    if (g_game_state && g_game_state->fog && console_fog_enabled())
        isometric_renderer_set_fog(ir, g_game_state->fog);
    else
        isometric_renderer_set_fog(ir, NULL);

    GameWorlds *gw = ir->game_worlds;
    World *saved_home = NULL;
    GameWorlds tmp = {0};
    bool used_tmp = false;
    if (gw)
    {
        saved_home = gw->home_world;
        gw->home_world = world;
    }
    else
    {
        tmp.home_world = world;
        isometric_renderer_set_game_worlds(ir, &tmp);
        used_tmp = true;
    }

    isometric_renderer_render_gpu(ir, window_state.renderer);

    // Settlement waypoints: markers in the current cell and its loaded neighbours.
    if (g_game_state && g_game_state->waypoint_count > 0)
    {
        const int player_gx = (int)(int64_t)g_game_state->player_universe_x;
        const int player_gy = (int)(int64_t)g_game_state->player_universe_y;
        const int mid_x = world->width / 2;
        const int mid_y = world->height / 2;
        const int mid_z = 4;
        const int selected = g_game_state->selected_waypoint;

        for (int i = 0; i < g_game_state->waypoint_count; i++)
        {
            const int dx = g_game_state->waypoints[i].gx - player_gx;
            const int dy = g_game_state->waypoints[i].gy - player_gy;
            if (dx < -1 || dx > 1 || dy < -1 || dy > 1)
                continue;
            const int widx = isometric_renderer_offset_index(ir, dx, dy, 0);
            if (widx < 0)
                continue;

            int sx = 0, sy = 0;
            // Prefer stamped plaza/anchor when that cell is loaded.
            World *cell = (widx == 0) ? world : ir->edge_worlds[widx];
            int ax = mid_x, ay = mid_y, az = mid_z;
            if (cell && settlement_anchor(cell, &ax, &ay, &az))
            {
                /* plaza / building anchor */
            }
            else if (cell)
            {
                ax = (int)cell->width / 2;
                ay = (int)cell->height / 2;
            }
            isometric_world_to_screen(ir, ax, ay, az, widx, &sx, &sy);

            const bool is_sel = (i == selected);
            // Pin: filled diamond with a stem.
            SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 220);
            const int r = is_sel ? 7 : 5;
            for (int o = -1; o <= 1; o++)
            {
                SDL_RenderDrawLine(window_state.renderer, sx + o, sy - r, sx + r + o, sy);
                SDL_RenderDrawLine(window_state.renderer, sx + r + o, sy, sx + o, sy + r);
                SDL_RenderDrawLine(window_state.renderer, sx + o, sy + r, sx - r + o, sy);
                SDL_RenderDrawLine(window_state.renderer, sx - r + o, sy, sx + o, sy - r);
            }
            if (is_sel)
                SDL_SetRenderDrawColor(window_state.renderer, 255, 220, 64, 255);
            else
                SDL_SetRenderDrawColor(window_state.renderer, 80, 200, 255, 255);
            SDL_RenderDrawLine(window_state.renderer, sx, sy - r + 1, sx + r - 1, sy);
            SDL_RenderDrawLine(window_state.renderer, sx + r - 1, sy, sx, sy + r - 1);
            SDL_RenderDrawLine(window_state.renderer, sx, sy + r - 1, sx - r + 1, sy);
            SDL_RenderDrawLine(window_state.renderer, sx - r + 1, sy, sx, sy - r + 1);
            SDL_RenderDrawLine(window_state.renderer, sx, sy + r, sx, sy + r + 6);

            if (is_sel && g_game_state->waypoints[i].label[0])
            {
                SDL_Color gold = {255, 220, 64, 255};
                SDL_Color shadow = {0, 0, 0, 200};
                const int cell_w = window_state.cell_width > 0 ? window_state.cell_width : 8;
                const int lx =
                    sx - ((int)strlen(g_game_state->waypoints[i].label) * cell_w) / 2;
                window_render_text(g_game_state->waypoints[i].label, lx + 1, sy + r + 8 + 1, shadow);
                window_render_text(g_game_state->waypoints[i].label, lx, sy + r + 8, gold);
            }
        }
    }

    if (gw)
        gw->home_world = saved_home;
    if (used_tmp)
        isometric_renderer_set_game_worlds(ir, NULL);

    ir->zoom_scale = saved_zoom;
    ir->disable_culling = saved_culling;
    ir->neighbor_inclusion_radius = saved_radius;
    ir->fog = saved_fog;
    for (int i = 0; i < 125; i++)
        ir->edge_worlds[i] = saved_edges[i];

    window_render_text("Map", 8, 8, window_state.highlight_color);
    window_render_text("M / Esc close · click waypoint to navigate", 8, 20, window_state.text_color);
    if (g_game_state && g_game_state->waypoint_count > 0)
    {
        char line[64];
        snprintf(line, sizeof(line), "Waypoints: %d", g_game_state->waypoint_count);
        window_render_text(line, 8, 32, window_state.ui_color);
    }

    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);
}

// Hit-test settlement waypoints on the map screen. Uses the same zoomed projection as the map.
// Returns waypoint index or -1.
int window_map_hit_waypoint(GameState *state, int screen_x, int screen_y)
{
    if (!state || !state->current_world || !g_isometric_renderer || state->waypoint_count <= 0)
        return -1;

    IsometricRenderer *ir = g_isometric_renderer;
    const float saved_zoom = ir->zoom_scale;
    const int saved_radius = ir->neighbor_inclusion_radius;
    ir->neighbor_inclusion_radius = 1;
    ir->zoom_scale = 1.0f / 3.0f;
    if (state->game_worlds)
        isometric_renderer_set_game_worlds(ir, state->game_worlds);

    const int player_gx = (int)(int64_t)state->player_universe_x;
    const int player_gy = (int)(int64_t)state->player_universe_y;
    World *world = state->current_world;
    int best = -1;
    int best_d2 = 14 * 14; // click radius in base pixels

    for (int i = 0; i < state->waypoint_count; i++)
    {
        const int dx = state->waypoints[i].gx - player_gx;
        const int dy = state->waypoints[i].gy - player_gy;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1)
            continue;
        const int widx = isometric_renderer_offset_index(ir, dx, dy, 0);
        if (widx < 0)
            continue;
        World *cell = (widx == 0) ? world : ir->edge_worlds[widx];
        int ax = world->width / 2, ay = world->height / 2, az = 4;
        if (cell && settlement_anchor(cell, &ax, &ay, &az))
        {
            /* plaza / building anchor */
        }
        else if (cell)
        {
            ax = (int)cell->width / 2;
            ay = (int)cell->height / 2;
        }
        int sx = 0, sy = 0;
        isometric_world_to_screen(ir, ax, ay, az, widx, &sx, &sy);
        const int ddx = sx - screen_x;
        const int ddy = sy - screen_y;
        const int d2 = ddx * ddx + ddy * ddy;
        if (d2 < best_d2)
        {
            best_d2 = d2;
            best = i;
        }
    }

    ir->zoom_scale = saved_zoom;
    ir->neighbor_inclusion_radius = saved_radius;
    return best;
}

// Initialize tutorial quest
void window_init_tutorial_quest()
{
    g_tutorial_quest.title = "Welcome to VERSE!";
    g_tutorial_quest.description = "Learn the basics of controlling your spirit in this mystical realm.";
    g_tutorial_quest.objectives_count = 2;
    g_tutorial_quest.completed_objectives = 0;
    g_tutorial_quest.is_active = true;
    g_tutorial_quest.is_completed = false;
    g_show_tutorial_modal = true;
    printf("Tutorial quest initialized\n");
}

// Update tutorial progress based on player actions
void window_update_tutorial_progress(const char *action)
{
    if (!g_tutorial_quest.is_active || g_tutorial_quest.is_completed)
    {
        return;
    }

    if (strcmp(action, "move") == 0 && g_tutorial_quest.completed_objectives == 0)
    {
        g_tutorial_quest.completed_objectives = 1;
        printf("Tutorial: Movement objective completed!\n");
    }
    else if (strcmp(action, "attack") == 0 && g_tutorial_quest.completed_objectives == 1)
    {
        g_tutorial_quest.completed_objectives = 2;
        g_tutorial_quest.is_completed = true;
        g_tutorial_quest.is_active = false;
        g_show_tutorial_modal = false;
        printf("Tutorial: All objectives completed! Quest finished!\n");
    }
}

// Render first-person world view using optimized FP renderer
void window_render_fp_world(World *world, int player_x, int player_y, int player_z, const char *player_name)
{
    // Set the base render texture as the target for proper scaling
    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);

    // Clear the base texture with sky color
    SDL_SetRenderDrawColor(window_state.renderer, 20, 22, 35, 255); // Sky blue background
    SDL_RenderClear(window_state.renderer);

    // Check if world is valid
    if (!world)
    {
        printf("FP render: Invalid world\n");
        return;
    }

    // Set up camera for first-person view using smooth world position and facing
    FPCamera cam;
    float px = (float)player_x;
    float py = (float)player_y;
    float pz = (float)player_z;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    extern GameState *g_game_state;
    if (g_game_state)
    {
        game_state_get_animated_player_position(g_game_state, &px, &py, &pz);
        yaw = g_game_state->controls.facing_yaw;
        pitch = g_game_state->controls.pitch;
        roll = g_game_state->controls.roll;
    }
    cam.x = px;
    cam.y = py;
    // Eye height above the player's own position. The renderer no longer adds a hidden offset of
    // its own, so the mesh and raycast paths look out from the same point. Ctrl crouch lowers it.
    cam.z = pz + (g_game_state ? player_controls_eye_height(g_game_state, &g_game_state->controls)
                               : PLAYER_EYE_HEIGHT);
    cam.yaw = yaw;
    cam.pitch = pitch;
    cam.roll = roll;
    cam.fov_deg = FP_CAMERA_FOV_DEG;

    fp_renderer_set_mode(FP_MODE_MESH);
    // CPU mesh raster is faster than GL readback on most platforms; set GPU via env if needed.
    fp_renderer_enable_gpu_mesh(getenv("FP_RENDERER_GPU") != NULL);
    {
        const char *lit = getenv("FP_LIGHTING");
        fp_renderer_set_lighting(!(lit && lit[0] == '0' && lit[1] == '\0'));
        // Opt-in: GL occupancy upload has no live consumer yet (CPU lighting uses ShadowWorld).
        fp_renderer_set_gpu_occupancy(getenv("FP_GPU_OCCUPANCY") != NULL);
    }

    // Prefer the cluster path, which draws the surrounding worlds as well, but only when its centre
    // really is the world being asked for. During a transition the two can disagree for a frame,
    // and drawing the player's surroundings from the world they just left would be worse than
    // drawing no surroundings at all.
    const ShadowWorld *cluster =
        (g_game_state && g_game_state->shadow_world &&
         shadow_world_slot_world(g_game_state->shadow_world, SHADOW_CENTRE_SLOT) == world)
            ? g_game_state->shadow_world
            : NULL;

    const FogAtlas *fog = NULL; // Fog of war is map-only; first-person shows full terrain.

    if (cluster)
    {
        fp_renderer_render_cluster(window_state.renderer, cluster, &cam, 0, 0,
                                   window_state.base_width, window_state.base_height, fog);
    }
    else
    {
        GameWorlds temp_gw = {0};
        temp_gw.home_world = world;
        fp_renderer_render_neighbors(window_state.renderer, &temp_gw, &cam, 0, 0,
                                     window_state.base_width, window_state.base_height, 0, fog);
    }

    // Render a mini top-down map in the upper-right corner (same as isometric mode)
    bool should_show_minimap = true;
    extern GameState *g_game_state;
    if (g_game_state && g_game_state->current_screen == GAME_SCREEN_MAIN_MENU) {
        should_show_minimap = false;
    }
    if (should_show_minimap)
    {
        window_render_cached_minimap(world, player_x, player_y, player_z);
    }

    // Render selection information (same as isometric mode)
    window_render_selection_info();

    if (g_game_state && g_game_state->game_started &&
        g_game_state->current_screen == GAME_SCREEN_WORLD)
    {
        window_render_skill_hotbar(g_game_state);
        window_render_nav_aide(g_game_state);
        {
            Actor *corpse = player_controls_nearest_corpse(g_game_state, NULL);
            if (corpse)
            {
                float sx = 0.0f, sy = 0.0f, depth = 0.0f;
                const float head_z = (float)corpse->z + 1.2f;
                if (fp_renderer_debug_project(&cam, window_state.base_width, window_state.base_height,
                                              (float)corpse->x, (float)corpse->y, head_z,
                                              &sx, &sy, &depth) &&
                    depth > 0.2f &&
                    sx >= 0.0f && sx < (float)window_state.base_width &&
                    sy >= 0.0f && sy < (float)window_state.base_height)
                {
                    const char *tip = "F to loot";
                    int tw = 0, th = 0;
                    window_measure_text(tip, &tw, &th);
                    int tx = (int)(sx + 0.5f) - tw / 2;
                    int ty = (int)(sy + 0.5f) - th - 6;
                    SDL_Color shadow = {0, 0, 0, 220};
                    SDL_Color gold = {255, 230, 140, 255};
                    window_render_text(tip, tx + 1, ty + 1, shadow);
                    window_render_text(tip, tx, ty, gold);
                }
            }
        }
        if (g_game_state->controls.is_attacking)
        {
            int cx = window_state.base_width / 2;
            int cy = window_state.base_height / 2;
            spirit_sprite_draw_swing(window_state.renderer, cx, cy,
                                     g_game_state->controls.facing_yaw,
                                     player_controls_swing_progress(&g_game_state->controls),
                                     player_controls_swing_arc(&g_game_state->controls),
                                     g_game_state->controls.attack_armed, false);
        }
        if (dialogue_active(&g_game_state->dialogue))
            window_render_dialogue_box(dialogue_speaker(&g_game_state->dialogue),
                                       dialogue_current_text(&g_game_state->dialogue),
                                       dialogue_current_char(&g_game_state->dialogue),
                                       dialogue_line_index(&g_game_state->dialogue),
                                       dialogue_line_count(&g_game_state->dialogue));
        else
            window_render_game_log(&g_game_state->game_log);
    }

    // Legacy voxel information box (keep for now for compatibility)
    if (g_show_voxel_info)
    {
        // Get voxel type from the world
        VoxelType voxel_type = VOXEL_AIR;
        const char *type_name = "Air";

        // Try to get the voxel from the home world first
        extern GameState *g_game_state;
        if (g_game_state && g_game_state->game_worlds && g_game_state->game_worlds->home_world)
        {
            World *world = g_game_state->game_worlds->home_world;
            if (g_info_voxel_x >= 0 && g_info_voxel_x < (int)world->width &&
                g_info_voxel_y >= 0 && g_info_voxel_y < (int)world->height &&
                g_info_voxel_z >= 0 && g_info_voxel_z < (int)world->depth)
            {
                Voxel *voxel = world_get_voxel(world, g_info_voxel_x, g_info_voxel_y, g_info_voxel_z);
                if (voxel)
                {
                    voxel_type = voxel->type;
                }

                // Convert voxel type to readable name
                switch (voxel_type)
                {
                case VOXEL_AIR:
                    type_name = "Air";
                    break;
                case VOXEL_GRASS:
                    type_name = "Grass";
                    break;
                case VOXEL_SOIL:
                    type_name = "Dirt";
                    break;
                case VOXEL_STONE:
                    type_name = "Stone";
                    break;
                case VOXEL_WOOD:
                    type_name = "Wood";
                    break;
                case VOXEL_LEAVES:
                    type_name = "Leaves";
                    break;
                case VOXEL_WATER:
                    type_name = "Water";
                    break;
                case VOXEL_SAND:
                    type_name = "Sand";
                    break;
                case VOXEL_BEDROCK:
                    type_name = "Bedrock";
                    break;

                case VOXEL_SPRING:
                    type_name = "Spring";
                    break;
                default:
                    type_name = "Unknown";
                    break;
                }
            }
        }

        char voxel_info[256];
        snprintf(voxel_info, sizeof(voxel_info), "Position: (%d,%d,%d)\nType: %s (%d)\n\nRight-click to move\nClick elsewhere to close",
                 g_info_voxel_x, g_info_voxel_y, g_info_voxel_z, type_name, voxel_type);

        // Render voxel info box
        int info_width = 200;
        int info_height = 100;
        int info_x = 10;
        int info_y = 10;

        SDL_Rect info_rect = {info_x, info_y, info_width, info_height};
        SDL_SetRenderDrawColor(window_state.renderer, 40, 40, 60, 200);
        SDL_RenderFillRect(window_state.renderer, &info_rect);
        SDL_SetRenderDrawColor(window_state.renderer, 80, 80, 100, 255);
        SDL_RenderDrawRect(window_state.renderer, &info_rect);

        // Render voxel info text
        window_render_wrapped_text(voxel_info, info_x + 10, info_y + 10, info_width - 20, (SDL_Color){255, 255, 255, 255});
    }
}

// Render tutorial modal
void window_render_tutorial_modal()
{
    // Semi-transparent overlay
    SDL_Rect overlay = {0, 0, window_state.base_width, window_state.base_height};
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(window_state.renderer, &overlay);

    // Tutorial panel
    int panel_width = 400;
    int panel_height = 300;
    int panel_x = (window_state.base_width - panel_width) / 2;
    int panel_y = (window_state.base_height - panel_height) / 2;

    SDL_Rect panel_rect = {panel_x, panel_y, panel_width, panel_height};
    SDL_SetRenderDrawColor(window_state.renderer, 40, 40, 60, 255);
    SDL_RenderFillRect(window_state.renderer, &panel_rect);
    SDL_SetRenderDrawColor(window_state.renderer, 80, 80, 100, 255);
    SDL_RenderDrawRect(window_state.renderer, &panel_rect);

    // Title
    window_render_text(g_tutorial_quest.title, panel_x + panel_width / 2 - 80, panel_y + 20, window_state.highlight_color);

    // Description
    window_render_wrapped_text(g_tutorial_quest.description, panel_x + 20, panel_y + 50, panel_width - 40, window_state.text_color);

    // Objectives
    int obj_y = panel_y + 120;
    window_render_text("Objectives:", panel_x + 20, obj_y, window_state.ui_color);

    // Objective 1: Move your hero
    obj_y += 25;
    const char *obj1_text = "1. Move your hero (Right-click on the map)";
    SDL_Color obj1_color = (g_tutorial_quest.completed_objectives >= 1) ? (SDL_Color){0, 255, 0, 255} : window_state.text_color; // Green if completed
    window_render_text(obj1_text, panel_x + 20, obj_y, obj1_color);

    // Objective 2: Attack a target
    obj_y += 25;
    const char *obj2_text = "2. Attack a target (Right-click a weed)";
    SDL_Color obj2_color = (g_tutorial_quest.completed_objectives >= 2) ? (SDL_Color){0, 255, 0, 255} : window_state.text_color; // Green if completed
    window_render_text(obj2_text, panel_x + 20, obj_y, obj2_color);

    // Progress
    obj_y += 40;
    char progress_text[64];
    snprintf(progress_text, sizeof(progress_text), "Progress: %d/%d objectives completed",
             g_tutorial_quest.completed_objectives, g_tutorial_quest.objectives_count);
    window_render_text(progress_text, panel_x + 20, obj_y, window_state.text_color);

    // Reward message when completed
    if (g_tutorial_quest.is_completed)
    {
        obj_y += 30;
        window_render_text("🎉 Tutorial completed! You've earned 50 gold!",
                           panel_x + 20, obj_y, (SDL_Color){255, 215, 0, 255}); // Gold color
    }

    // Continue button (only show if tutorial is completed)
    if (g_tutorial_quest.is_completed)
    {
        int button_width = 120;
        int button_height = 30;
        int button_x = panel_x + (panel_width - button_width) / 2;
        int button_y = panel_y + panel_height - 50;

        SDL_Rect button_rect = {button_x, button_y, button_width, button_height};
        SDL_SetRenderDrawColor(window_state.renderer, 60, 120, 60, 255);
        SDL_RenderFillRect(window_state.renderer, &button_rect);
        SDL_SetRenderDrawColor(window_state.renderer, 100, 200, 100, 255);
        SDL_RenderDrawRect(window_state.renderer, &button_rect);

        window_render_text("Continue", button_x + 35, button_y + 8, (SDL_Color){255, 255, 255, 255});
    }
}

// Render loading screen with progress bar
void window_render_loading_screen(GameState *game_state)
{
    window_clear();

    // Title
    window_render_text("VERSE", window_state.base_width / 2 - 25, 40, window_state.highlight_color);

    // Loading message
    window_render_text("Generating your world...", window_state.base_width / 2 - 80, 80, window_state.text_color);

    // Progress bar background
    int bar_width = 200;
    int bar_height = 20;
    int bar_x = window_state.base_width / 2 - bar_width / 2;
    int bar_y = 120;

    SDL_Rect bar_bg = {bar_x, bar_y, bar_width, bar_height};
    SDL_SetRenderDrawColor(window_state.renderer, 64, 64, 64, 255); // Dark gray
    SDL_RenderFillRect(window_state.renderer, &bar_bg);

    // Progress bar fill
    if (game_state->loading_total > 0)
    {
        int progress_width = (bar_width * game_state->loading_progress) / game_state->loading_total;
        SDL_Rect progress_bar = {bar_x, bar_y, progress_width, bar_height};
        SDL_SetRenderDrawColor(window_state.renderer, 0, 120, 255, 255); // Blue
        SDL_RenderFillRect(window_state.renderer, &progress_bar);
    }

    // Progress percentage
    char progress_text[64];
    if (game_state->loading_total > 0)
    {
        int percentage = (game_state->loading_progress * 100) / game_state->loading_total;
        snprintf(progress_text, sizeof(progress_text), "%d%%", percentage);
    }
    else
    {
        strcpy(progress_text, "0%");
    }
    window_render_text(progress_text, bar_x + bar_width / 2 - 10, bar_y + bar_height + 10, window_state.text_color);
}

// Render title screen with fade effect
void window_render_title_screen(float fade_alpha, Uint32 title_elapsed_ms)
{
    // Clear to black background
    window_clear();

    // Set the base render texture as the target for proper scaling
    SDL_SetRenderTarget(window_state.renderer, window_state.base_render_texture);

    // Fill with black background
    SDL_SetRenderDrawColor(window_state.renderer, 0, 0, 0, 255);
    SDL_RenderClear(window_state.renderer);

    // Render "VERSE" title with fade effect
    SDL_Color title_color = {
        255, 255, 255, (Uint8)(255 * fade_alpha) // White with fade alpha
    };

    // Center using true measured size
    int title_px_w = 0, title_px_h = 0;
    window_measure_text("VERSE", &title_px_w, &title_px_h);
    int title_x = (window_state.base_width - title_px_w) / 2;
    int title_y = (window_state.base_height - title_px_h) / 2 - window_state.cell_height;
    window_render_text("VERSE", title_x, title_y, title_color);

    // Add subtitle, but only begin to fade in at golden ratio point of 5s
    const float duration_sec = 5.0f;
    const float golden_ratio = 1.6180339887f;
    const float subtitle_onset_sec = duration_sec / golden_ratio; // ~3.09s
    float t_sec = title_elapsed_ms / 1000.0f;
    float subtitle_alpha = 0.0f;
    float u = 0.0f; // fade progress 0..1 from onset->duration
    if (t_sec > subtitle_onset_sec) {
        u = fminf((t_sec - subtitle_onset_sec) / (duration_sec - subtitle_onset_sec), 1.0f);
        // smooth fade-in to alpha 200
        float s = (u * u * (3.0f - 2.0f * u));
        subtitle_alpha = 200.0f * s;
    }
    // After fade-in completes, apply a gentle pulse between the dark gray and white
    Uint8 base_gray = 200;
    Uint8 max_white = 255;
    Uint8 sub_alpha_u8 = (Uint8)subtitle_alpha;
    Uint8 sub_r = base_gray;
    Uint8 sub_g = base_gray;
    Uint8 sub_b = base_gray;
    // Enable pulse when mostly faded in (u >= ~0.9). Use a spiky curve so it briefly hits white.
    if (u >= 0.9f) {
        // ~1.5 Hz pulse
        float p = 0.5f * (sinf((float)title_elapsed_ms * 0.00942f) + 1.0f);
        // Spike shape to prefer dark most of the time, hit white briefly
        float spike = powf(p, 6.0f);
        float mix = spike; // 0..1, allows full white at peak
        sub_r = (Uint8)((1.0f - mix) * base_gray + mix * max_white);
        sub_g = (Uint8)((1.0f - mix) * base_gray + mix * max_white);
        sub_b = (Uint8)((1.0f - mix) * base_gray + mix * max_white);
    }
    SDL_Color subtitle_color = {sub_r, sub_g, sub_b, sub_alpha_u8};

    const char *subtitle = "Press ENTER to begin";
    int sub_px_w = 0, sub_px_h = 0;
    window_measure_text(subtitle, &sub_px_w, &sub_px_h);
    int subtitle_x = (window_state.base_width - sub_px_w) / 2;
    int subtitle_y = title_y + window_state.cell_height * 3;
    if (subtitle_color.a > 0) {
        window_render_text(subtitle, subtitle_x, subtitle_y, subtitle_color);
    }

    // Reset render target to default
    SDL_SetRenderTarget(window_state.renderer, NULL);
}

// Render arrow (simple triangle)
void window_render_arrow(int x, int y, SDL_Color color)
{
    // Draw a simple arrow using lines
    SDL_SetRenderDrawColor(window_state.renderer, color.r, color.g, color.b, color.a);

    // Arrow points right (→) - draw as lines
    SDL_RenderDrawLine(window_state.renderer, x, y - 4, x + 8, y); // Top to tip
    SDL_RenderDrawLine(window_state.renderer, x + 8, y, x, y + 4); // Tip to bottom
    SDL_RenderDrawLine(window_state.renderer, x, y + 4, x, y - 4); // Bottom to top
}

// Audio callback for SDL audio system
void audio_callback(void *userdata, Uint8 *stream, int len)
{
    // Clear the audio buffer
    memset(stream, 0, len);

    // Unpack audio mix context
    AudioMixContext *ctx = (AudioMixContext *)userdata;
    float *float_stream = (float *)stream;
    int num_samples = len / (int)sizeof(float);

    // Temporary buffers
    static float temp_title[8192];
    static float temp_music[8192];
    if (num_samples > 8192) num_samples = 8192; // safety cap

    // Zero temps
    for (int i = 0; i < num_samples; i++) {
        temp_title[i] = 0.0f;
        temp_music[i] = 0.0f;
    }

    // Generate title hum ALWAYS, regardless of settings
    if (ctx && ctx->title_hum) {
        title_hum_generate_buffer(ctx->title_hum, temp_title, num_samples);
    }

    // Generate audio from synthesizer:
    // - If background music is enabled and not in title mode, advance/play melody
    // - Otherwise still pull raw synth so UI/menu sounds are audible
    if (ctx && ctx->music) {
        if (background_music_is_enabled(ctx->music) && !g_audio_title_mode) {
            background_music_generate_buffer(ctx->music, temp_music, num_samples);
        } else {
            background_music_generate_synth_buffer(ctx->music, temp_music, num_samples);
        }
    }

    // Mix and write
    for (int i = 0; i < num_samples; i++) {
        float s = temp_title[i] + temp_music[i];
        // soft clip
        if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
        float_stream[i] = s;
    }
}

// Set up SDL audio system
static AudioMixContext g_audio_ctx; // keep live for SDL

bool window_setup_audio_with_title(BackgroundMusicSystem *music, TitleHumSystem *title_hum)
{
    SDL_AudioSpec desired, obtained;

    // Set up the audio specification
    SDL_zero(desired);
    desired.freq = 44100;
    desired.format = AUDIO_F32;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = audio_callback;
    g_audio_ctx.music = music;
    g_audio_ctx.title_hum = title_hum;
    desired.userdata = &g_audio_ctx;

    // Open the audio device
    if (SDL_OpenAudio(&desired, &obtained) < 0)
    {
        printf("Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }

    // Start audio playback
    SDL_PauseAudio(0);

    printf("Audio system initialized successfully\n");
    printf("Audio format: %d Hz, %d channels, %d samples\n", obtained.freq, obtained.channels, obtained.samples);
    printf("Audio format type: %d (F32=%d)\n", obtained.format, AUDIO_F32);
    return true;
}

// Backward-compatible setup for callers that don't need title hum
bool window_setup_audio(BackgroundMusicSystem *music)
{
    return window_setup_audio_with_title(music, NULL);
}

void window_set_title_mode(bool active)
{
    g_audio_title_mode = active;
}

// Settings persistence functions
void window_save_settings()
{
    GameSettings settings;
    settings_extract_from_window_state(&settings);
    settings_save(&settings, "settings.dat");
}

void window_load_settings()
{
    GameSettings settings;
    if (settings_load(&settings, "settings.dat"))
    {
        settings_apply_to_window_state(&settings);
        printf("Settings loaded and applied to window\n");
    }
    else
    {
        printf("Failed to load settings, using current window state\n");
    }
}

// Text input functions
void window_start_text_input()
{
    SDL_StartTextInput();
    printf("Text input started\n");
}

void window_stop_text_input()
{
    SDL_StopTextInput();
    printf("Text input stopped\n");
}

void window_handle_text_input(const char *text)
{
    extern GameState *g_game_state;
    if (!g_game_state || !text)
        return;

    // Add the input text to the player name
    int current_len = strlen(g_game_state->player_name);
    int text_len = strlen(text);

    // Make sure we don't exceed the buffer size (leaving room for null terminator)
    if (current_len + text_len < sizeof(g_game_state->player_name) - 1)
    {
        strcat(g_game_state->player_name, text);
        printf("Player name updated: '%s'\n", g_game_state->player_name);
    }
}

// Clean up audio system
void window_cleanup_audio()
{
    SDL_CloseAudio();
    printf("Audio system cleaned up\n");
}

// Voxel interaction functions
void window_set_hovered_voxel(int x, int y, int z)
{
    g_has_hovered_voxel = true;
    g_hovered_voxel_x = x;
    g_hovered_voxel_y = y;
    g_hovered_voxel_z = z;

    // Update the isometric renderer
    if (g_isometric_renderer)
    {
        isometric_renderer_set_highlighted_voxel(g_isometric_renderer, x, y, z);
    }
}

void window_clear_hovered_voxel(void)
{
    g_has_hovered_voxel = false;

    // Clear highlighting in the renderer
    if (g_isometric_renderer)
    {
        isometric_renderer_clear_highlighted_voxel(g_isometric_renderer);
    }
}

// New selection system implementation
void window_set_selection(SelectionType type, void* data)
{
    // Clear previous selections in the renderer
    if (g_isometric_renderer && g_current_selection.type == SELECTION_HERO && type != SELECTION_HERO) {
        isometric_renderer_set_hero_selected(g_isometric_renderer, false);
    }

    g_current_selection.type = type;

    switch (type) {
        case SELECTION_VOXEL:
            if (data) {
                int* coords = (int*)data;
                g_current_selection.data.voxel.x = coords[0];
                g_current_selection.data.voxel.y = coords[1];
                g_current_selection.data.voxel.z = coords[2];
            }
            break;
        case SELECTION_HERO:
            if (data) {
                // Named so the declaration and the cast refer to one type; two
                // identically-written anonymous structs are distinct types in C.
                typedef struct {
                    int coords[3];
                    char name[64];
                } HeroSelectionData;
                HeroSelectionData *hero_data = (HeroSelectionData *)data;

                g_current_selection.data.hero.player_x = hero_data->coords[0];
                g_current_selection.data.hero.player_y = hero_data->coords[1];
                g_current_selection.data.hero.player_z = hero_data->coords[2];
                strncpy(g_current_selection.data.hero.name, hero_data->name, sizeof(g_current_selection.data.hero.name) - 1);
                g_current_selection.data.hero.name[sizeof(g_current_selection.data.hero.name) - 1] = '\0';

                // Notify the isometric renderer about hero selection
                if (g_isometric_renderer) {
                    isometric_renderer_set_hero_selected(g_isometric_renderer, true);
                }
            }
            break;
        case SELECTION_PET:
            if (data) {
                const char* name = (const char*)data;
                strncpy(g_current_selection.data.pet.name, name, sizeof(g_current_selection.data.pet.name) - 1);
                g_current_selection.data.pet.name[sizeof(g_current_selection.data.pet.name) - 1] = '\0';
            }
            break;
        case SELECTION_TELEPORT_HOME:
            if (data) {
                const char* home_name = (const char*)data;
                strncpy(g_current_selection.data.teleport_home.home_name, home_name, sizeof(g_current_selection.data.teleport_home.home_name) - 1);
                g_current_selection.data.teleport_home.home_name[sizeof(g_current_selection.data.teleport_home.home_name) - 1] = '\0';
            }
            break;
        case SELECTION_STORY_JOURNAL:
            if (data) {
                const char* journal_title = (const char*)data;
                strncpy(g_current_selection.data.story_journal.journal_title, journal_title, sizeof(g_current_selection.data.story_journal.journal_title) - 1);
                g_current_selection.data.story_journal.journal_title[sizeof(g_current_selection.data.story_journal.journal_title) - 1] = '\0';
            }
            break;
        case SELECTION_NONE:
        default:
            break;
    }
}

void window_clear_entity_selection(void)
{
    // Clear hero selection in renderer if needed
    if (g_isometric_renderer && g_current_selection.type == SELECTION_HERO) {
        isometric_renderer_set_hero_selected(g_isometric_renderer, false);
    }

    g_current_selection.type = SELECTION_NONE;
}

Selection* window_get_current_selection(void)
{
    return &g_current_selection;
}

void window_render_selection_info(void)
{
    if (g_current_selection.type == SELECTION_NONE) {
        return;
    }

    char selection_text[256];
    SDL_Color highlight_color = window_state.highlight_color;

    switch (g_current_selection.type) {
        case SELECTION_VOXEL:
            snprintf(selection_text, sizeof(selection_text), "Voxel (%d,%d,%d)",
                    g_current_selection.data.voxel.x,
                    g_current_selection.data.voxel.y,
                    g_current_selection.data.voxel.z);
            break;
        case SELECTION_HERO:
            snprintf(selection_text, sizeof(selection_text), "%s (you)",
                     g_current_selection.data.hero.name);
            break;
        case SELECTION_PET:
            snprintf(selection_text, sizeof(selection_text), "Pet: %s", g_current_selection.data.pet.name);
            break;
        case SELECTION_TELEPORT_HOME:
            snprintf(selection_text, sizeof(selection_text), "Teleport to %s", g_current_selection.data.teleport_home.home_name);
            break;
        case SELECTION_STORY_JOURNAL:
            snprintf(selection_text, sizeof(selection_text), "Story Journal: %s", g_current_selection.data.story_journal.journal_title);
            break;
        default:
            return;
    }

    // Render text in bottom-left corner
    window_render_text(selection_text, 10, window_state.base_height - 25, highlight_color);
}

void window_set_voxel_info(int x, int y, int z)
{
    g_show_voxel_info = true;
    g_info_voxel_x = x;
    g_info_voxel_y = y;
    g_info_voxel_z = z;

    // Also update the new selection system
    int coords[3] = {x, y, z};
    window_set_selection(SELECTION_VOXEL, coords);
}

void window_clear_voxel_info(void)
{
    g_show_voxel_info = false;

    // Also clear the new selection system
    window_clear_entity_selection();
}

bool window_is_voxel_info_open(void)
{
    return g_show_voxel_info;
}
