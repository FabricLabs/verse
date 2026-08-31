/*
 * sequencer_ui_core.h - Core sequencer UI system
 *
 * Provides the main interface for the sequencer UI built on top of the songwriter system.
 * This creates a professional sequencer interface with piano roll, mixer, and transport controls.
 */

#ifndef SEQUENCER_UI_CORE_H
#define SEQUENCER_UI_CORE_H

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>
#include <stdint.h>
#include "songwriter/songwriter.h"

// UI Configuration
#define SEQUENCER_UI_DEFAULT_WIDTH 1200
#define SEQUENCER_UI_DEFAULT_HEIGHT 800
#define SEQUENCER_UI_MIN_WIDTH 800
#define SEQUENCER_UI_MIN_HEIGHT 600

// UI Layout Constants
#define SEQUENCER_TRANSPORT_HEIGHT 60
#define SEQUENCER_TRACK_LIST_WIDTH 200
#define SEQUENCER_MIXER_HEIGHT 120
#define SEQUENCER_PIANO_ROLL_KEYS_WIDTH 80

// Colors (RGBA)
#define SEQUENCER_COLOR_BACKGROUND 0x2D2D2DFF
#define SEQUENCER_COLOR_TRANSPORT 0x3D3D3DFF
#define SEQUENCER_COLOR_TRACK_LIST 0x353535FF
#define SEQUENCER_COLOR_PIANO_ROLL 0x404040FF
#define SEQUENCER_COLOR_MIXER 0x3A3A3AFF
#define SEQUENCER_COLOR_GRID 0x555555FF
#define SEQUENCER_COLOR_NOTE 0x4A9EFFFF
#define SEQUENCER_COLOR_NOTE_SELECTED 0xFFFF4AFF
#define SEQUENCER_COLOR_PLAYHEAD 0xFF4A4AFF
#define SEQUENCER_COLOR_TEXT 0xFFFFFFFF
#define SEQUENCER_COLOR_TEXT_DIM 0xCCCCCCFF

// UI State
typedef enum {
    SEQUENCER_UI_STATE_IDLE,
    SEQUENCER_UI_STATE_PLAYING,
    SEQUENCER_UI_STATE_RECORDING,
    SEQUENCER_UI_STATE_PAUSED
} SequencerUIState;

// Mouse interaction modes
typedef enum {
    SEQUENCER_MOUSE_MODE_SELECT,
    SEQUENCER_MOUSE_MODE_DRAW,
    SEQUENCER_MOUSE_MODE_ERASE,
    SEQUENCER_MOUSE_MODE_ZOOM
} SequencerMouseMode;

// Note selection info
typedef struct {
    uint32_t track_index;
    uint32_t note_index;
    uint8_t note_number;
    uint32_t start_tick;
    uint32_t end_tick;
    uint8_t velocity;
} SelectedNote;

// Main sequencer UI structure
typedef struct {
    // Core systems
    Songwriter* songwriter;
    SDL_Window* window;
    SDL_Renderer* renderer;

    // UI State
    bool is_open;
    bool is_initialized;
    SequencerUIState state;
    SequencerMouseMode mouse_mode;

    // Window properties
    int window_width;
    int window_height;
    bool is_fullscreen;

    // UI Layout
    SDL_Rect transport_rect;
    SDL_Rect track_list_rect;
    SDL_Rect piano_roll_rect;
    SDL_Rect mixer_rect;

    // Timeline state
    uint32_t current_tick;
    uint32_t visible_start_tick;
    uint32_t visible_end_tick;
    float pixels_per_tick;
    float pixels_per_note;
    int visible_notes_start;  // MIDI note number
    int visible_notes_count;

    // Interaction state
    int selected_track;
    SelectedNote* selected_notes;
    int selected_note_count;
    bool is_dragging;
    int drag_start_x, drag_start_y;
    int last_mouse_x, last_mouse_y;

    // Playback state
    bool is_playing;
    bool is_recording;
    uint32_t loop_start_tick;
    uint32_t loop_end_tick;
    bool loop_enabled;
    uint8_t current_pattern;
    // Stepper state: 16-step bitmask per pattern (A-H)
    uint16_t pattern_steps[8];

    // Zoom and scroll
    float horizontal_zoom;
    float vertical_zoom;
    int scroll_x, scroll_y;

    // UI Components (forward declarations)
    void* timeline_component;
    void* track_list_component;
    void* mixer_component;
    void* transport_component;

    // Fonts and resources
    TTF_Font* font_small;
    TTF_Font* font_medium;
    TTF_Font* font_large;

    // Performance tracking
    uint32_t frame_count;
    uint32_t last_fps_time;
    float current_fps;
} SequencerUI;

// Core API Functions

// Initialization and cleanup
SequencerUI* sequencer_ui_create(Songwriter* songwriter);
void sequencer_ui_destroy(SequencerUI* ui);
bool sequencer_ui_initialize(SequencerUI* ui);
void sequencer_ui_shutdown(SequencerUI* ui);

// Window management
bool sequencer_ui_open(SequencerUI* ui);
void sequencer_ui_close(SequencerUI* ui);
bool sequencer_ui_is_open(SequencerUI* ui);
void sequencer_ui_set_fullscreen(SequencerUI* ui, bool fullscreen);
void sequencer_ui_resize(SequencerUI* ui, int width, int height);

// Main update and render loop
void sequencer_ui_update(SequencerUI* ui, double delta_time);
void sequencer_ui_render(SequencerUI* ui);
void sequencer_ui_handle_event(SequencerUI* ui, SDL_Event* event);

// Song management
bool sequencer_ui_load_song(SequencerUI* ui, Song* song);
bool sequencer_ui_new_song(SequencerUI* ui);
bool sequencer_ui_save_song(SequencerUI* ui, const char* filename);

// Track management
bool sequencer_ui_add_track(SequencerUI* ui, const char* name, uint8_t channel);
bool sequencer_ui_remove_track(SequencerUI* ui, int track_index);
bool sequencer_ui_rename_track(SequencerUI* ui, int track_index, const char* name);
void sequencer_ui_select_track(SequencerUI* ui, int track_index);

// Note editing
bool sequencer_ui_add_note(SequencerUI* ui, int track_index, uint8_t note,
                          uint32_t start_tick, uint32_t duration, uint8_t velocity);
bool sequencer_ui_remove_note(SequencerUI* ui, int track_index, uint32_t note_index);
bool sequencer_ui_modify_note(SequencerUI* ui, int track_index, uint32_t note_index,
                             uint8_t note, uint32_t start_tick, uint32_t duration, uint8_t velocity);
void sequencer_ui_select_note(SequencerUI* ui, int track_index, uint32_t note_index);
void sequencer_ui_clear_selection(SequencerUI* ui);

// Playback control
void sequencer_ui_play(SequencerUI* ui);
void sequencer_ui_pause(SequencerUI* ui);
void sequencer_ui_stop(SequencerUI* ui);
void sequencer_ui_record(SequencerUI* ui);
void sequencer_ui_seek(SequencerUI* ui, uint32_t tick);
void sequencer_ui_set_loop(SequencerUI* ui, uint32_t start_tick, uint32_t end_tick);
void sequencer_ui_toggle_loop(SequencerUI* ui);

// Timeline navigation
void sequencer_ui_zoom_horizontal(SequencerUI* ui, float factor);
void sequencer_ui_zoom_vertical(SequencerUI* ui, float factor);
void sequencer_ui_scroll_to_tick(SequencerUI* ui, uint32_t tick);
void sequencer_ui_scroll_to_note(SequencerUI* ui, uint8_t note);
void sequencer_ui_fit_to_content(SequencerUI* ui);

// UI state queries
SequencerUIState sequencer_ui_get_state(SequencerUI* ui);
bool sequencer_ui_is_playing(SequencerUI* ui);
bool sequencer_ui_is_recording(SequencerUI* ui);
uint32_t sequencer_ui_get_current_tick(SequencerUI* ui);
int sequencer_ui_get_selected_track(SequencerUI* ui);
int sequencer_ui_get_selected_note_count(SequencerUI* ui);

// Mouse interaction
void sequencer_ui_set_mouse_mode(SequencerUI* ui, SequencerMouseMode mode);
SequencerMouseMode sequencer_ui_get_mouse_mode(SequencerUI* ui);
bool sequencer_ui_handle_mouse_click(SequencerUI* ui, int x, int y, int button);
bool sequencer_ui_handle_mouse_drag(SequencerUI* ui, int x, int y);
bool sequencer_ui_handle_mouse_wheel(SequencerUI* ui, int x, int y, int delta);

// Keyboard shortcuts
void sequencer_ui_handle_keyboard(SequencerUI* ui, SDL_Keycode key, bool ctrl, bool shift, bool alt);

// Utility functions
void sequencer_ui_show_message(SequencerUI* ui, const char* message, int duration_ms);
void sequencer_ui_show_error(SequencerUI* ui, const char* error);
void sequencer_ui_show_confirm(SequencerUI* ui, const char* message,
                              void (*callback)(bool confirmed, void* user_data), void* user_data);

// Theme and styling
void sequencer_ui_set_theme(SequencerUI* ui, const char* theme_name);
void sequencer_ui_set_color_scheme(SequencerUI* ui, uint32_t background, uint32_t foreground, uint32_t accent);

// Performance and debugging
void sequencer_ui_show_fps(SequencerUI* ui, bool show);
void sequencer_ui_show_debug_info(SequencerUI* ui, bool show);
void sequencer_ui_print_stats(SequencerUI* ui);

// Internal rendering functions
void render_stepper_header(SequencerUI* ui);
void render_stepper_grid(SequencerUI* ui);
void render_stepper_status(SequencerUI* ui);
void render_pattern_selector(SequencerUI* ui, int start_y, int width, int height);
void render_param_panels(SequencerUI* ui, int start_x, int start_y, int width, int height);
void render_waveform_icon(SequencerUI* ui, int pattern, SDL_Rect rect);
void render_piano_and_steps(SequencerUI* ui, int start_x, int start_y, int width, int height);
void render_piano_roll(SequencerUI* ui, int start_x, int start_y, int width, int height);
void render_step_sequencer(SequencerUI* ui, int start_x, int start_y, int width, int height);
void render_control_panel(SequencerUI* ui, int start_x, int width, int height);
void render_bank_section(SequencerUI* ui, int start_x, int start_y, int width, int height);
void render_scale_section(SequencerUI* ui, int start_x, int start_y, int width, int height);
void render_bpm_section(SequencerUI* ui, int start_x, int start_y, int width, int height);
void render_transport_controls(SequencerUI* ui, int start_x, int start_y, int width, int height);

// Internal event handling functions
void handle_keyboard_event(SequencerUI* ui, SDL_KeyboardEvent* key_event);
void handle_mouse_click(SequencerUI* ui, int x, int y, int button);
void handle_stepper_header_click(SequencerUI* ui, int x, int y);
void handle_stepper_grid_click(SequencerUI* ui, int x, int y);
void handle_stepper_status_click(SequencerUI* ui, int x, int y);

#endif // SEQUENCER_UI_CORE_H
