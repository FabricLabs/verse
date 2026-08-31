#ifndef SONG_EDITOR_H
#define SONG_EDITOR_H

#include "songwriter/songwriter.h"
#ifdef SONG_EDITOR_STANDALONE
#include "song_editor_standalone.h"
#else
#include "window.h"
#endif

// Enhanced piano roll dimensions
#define PIANO_ROLL_WIDTH 1000
#define PIANO_ROLL_HEIGHT 600
#define NOTE_HEIGHT 12
#define NOTE_WIDTH_MIN 20
#define GRID_SIZE 20

// Editor states with enhanced features
typedef enum {
    EDITOR_STATE_SELECT,
    EDITOR_STATE_DRAW,
    EDITOR_STATE_ERASE,
    EDITOR_STATE_PLAY,
    EDITOR_STATE_RECORD,
    EDITOR_STATE_QUANTIZE,
    EDITOR_STATE_EDIT_VELOCITY,
    EDITOR_STATE_EDIT_DURATION
} EditorState;

// Enhanced note selection with audio quality features
typedef struct {
    uint8_t track_index;
    uint32_t event_index;
    bool selected;
    float velocity;
    float duration;
    uint8_t articulation;
    float portamento_time;
    float lfo_frequency;
    float lfo_depth;
    uint8_t filter_type;
    float filter_cutoff;
    float filter_resonance;
} NoteSelection;

// Audio quality visualization
typedef struct {
    float peak_level;
    float rms_level;
    float normalization_factor;
    bool clipping;
    float* waveform_buffer;
    int waveform_length;
    int waveform_position;
} AudioVisualizer;

// Enhanced song editor structure
typedef struct {
    Songwriter* writer;
    Song* current_song;
    Track* current_track;

    // UI state
    EditorState state;
    int scroll_x;
    int scroll_y;
    float zoom_level;
    bool show_grid;
    bool snap_to_grid;
    bool show_velocity;
    bool show_duration;
    bool show_articulation;

    // Selection
    NoteSelection* selections;
    int selection_count;
    int selection_capacity;

    // Mouse state
    int mouse_x;
    int mouse_y;
    bool mouse_down;
    int drag_start_x;
    int drag_start_y;
    bool right_click;

    // Playback
    bool is_playing;
    uint32_t playhead_position;
    float playhead_x;
    bool loop_enabled;
    float playback_speed;

    // Recording
    bool is_recording;
    uint32_t record_start_tick;
    uint8_t recording_track;
    float recording_velocity;
    float recording_duration;

    // Audio quality controls
    bool auto_normalize;
    float master_volume;
    float compressor_threshold;
    float compressor_ratio;
    float limiter_threshold;
    float delay_time;
    float delay_feedback;
    float delay_mix;
    float reverb_time;
    float reverb_mix;

    // Track controls
    bool* track_muted;
    bool* track_soloed;
    float* track_volumes;
    float* track_pans;

    // Audio visualization
    AudioVisualizer visualizer;

    // UI elements
    SDL_Rect piano_roll_rect;
    SDL_Rect toolbar_rect;
    SDL_Rect timeline_rect;
    SDL_Rect track_list_rect;
    SDL_Rect audio_controls_rect;
    SDL_Rect visualizer_rect;
    SDL_Rect properties_rect;

    // Instrument editor
    bool show_instrument_editor;
    SDL_Rect instrument_editor_rect;
    int selected_instrument_track;
} SongEditor;

// Song editor functions
SongEditor* song_editor_create(Songwriter* writer);
void song_editor_destroy(SongEditor* editor);

// Song management
bool song_editor_new_song(SongEditor* editor, const char* name, const char* artist, uint32_t tempo);
bool song_editor_load_song(SongEditor* editor, Song* song);
bool song_editor_save_song(SongEditor* editor, const char* filename);

// Track management
bool song_editor_add_track(SongEditor* editor, const char* name, uint8_t channel);
bool song_editor_remove_track(SongEditor* editor, uint8_t track_index);
bool song_editor_select_track(SongEditor* editor, uint8_t track_index);

// Note editing with enhanced features
bool song_editor_add_note(SongEditor* editor, uint8_t note, uint32_t start_time, uint32_t duration, uint8_t velocity);
bool song_editor_remove_note(SongEditor* editor, uint8_t track_index, uint32_t event_index);
bool song_editor_modify_note(SongEditor* editor, uint8_t track_index, uint32_t event_index, uint8_t note, uint32_t start_time, uint32_t duration, uint8_t velocity);

// Enhanced note editing
bool song_editor_add_note_advanced(SongEditor* editor, uint8_t note, float velocity, float start_time, float duration,
                                 uint8_t articulation, float portamento_time, float lfo_freq, float lfo_depth,
                                 uint8_t filter_type, float filter_cutoff, float filter_resonance);
bool song_editor_modify_note_advanced(SongEditor* editor, uint8_t track_index, uint32_t event_index, uint8_t note,
                                    float velocity, float start_time, float duration, uint8_t articulation,
                                    float portamento_time, float lfo_freq, float lfo_depth, uint8_t filter_type,
                                    float filter_cutoff, float filter_resonance);

// UI interaction
void song_editor_handle_mouse(SongEditor* editor, int x, int y, bool down, bool up, bool right_click);
void song_editor_handle_keyboard(SongEditor* editor, SDL_Keycode key, bool pressed);
void song_editor_handle_mouse_wheel(SongEditor* editor, int x, int y, int delta);

// Rendering
void song_editor_render(SongEditor* editor);
void song_editor_render_piano_roll(SongEditor* editor);
void song_editor_render_timeline(SongEditor* editor);
void song_editor_render_toolbar(SongEditor* editor);
void song_editor_render_track_list(SongEditor* editor);
void song_editor_render_notes(SongEditor* editor);
void song_editor_render_playhead(SongEditor* editor);
void song_editor_render_audio_controls(SongEditor* editor);
void song_editor_render_visualizer(SongEditor* editor);
void song_editor_render_properties(SongEditor* editor);

// Utility functions
void song_editor_screen_to_grid(SongEditor* editor, int screen_x, int screen_y, uint8_t* note, uint32_t* time);
void song_editor_grid_to_screen(SongEditor* editor, uint8_t note, uint32_t time, int* screen_x, int* screen_y);
uint8_t song_editor_screen_to_note(SongEditor* editor, int screen_y);
uint32_t song_editor_screen_to_time(SongEditor* editor, int screen_x);
int song_editor_note_to_screen(SongEditor* editor, uint8_t note);
int song_editor_time_to_screen(SongEditor* editor, uint32_t time);

// Playback control
void song_editor_play(SongEditor* editor);
void song_editor_stop(SongEditor* editor);
void song_editor_pause(SongEditor* editor);
void song_editor_seek(SongEditor* editor, uint32_t time);
void song_editor_seek_to_time(SongEditor* editor, float time_seconds);
void song_editor_set_playback_speed(SongEditor* editor, float speed);

// Recording
void song_editor_start_recording(SongEditor* editor, uint8_t track_index);
void song_editor_stop_recording(SongEditor* editor);
void song_editor_record_note(SongEditor* editor, uint8_t note, float velocity);

// Selection
void song_editor_clear_selection(SongEditor* editor);
void song_editor_select_note(SongEditor* editor, uint8_t track_index, uint32_t event_index);
void song_editor_deselect_note(SongEditor* editor, uint8_t track_index, uint32_t event_index);
bool song_editor_is_note_selected(SongEditor* editor, uint8_t track_index, uint32_t event_index);

// Audio quality control
void song_editor_set_master_volume(SongEditor* editor, float volume);
void song_editor_set_auto_normalize(SongEditor* editor, bool enabled);
void song_editor_set_compressor(SongEditor* editor, float threshold, float ratio);
void song_editor_set_limiter(SongEditor* editor, float threshold);
void song_editor_set_delay(SongEditor* editor, float time, float feedback, float mix);
void song_editor_set_reverb(SongEditor* editor, float time, float mix);

// Track control
void song_editor_set_track_volume(SongEditor* editor, uint8_t track_index, float volume);
void song_editor_set_track_pan(SongEditor* editor, uint8_t track_index, float pan);
void song_editor_mute_track(SongEditor* editor, uint8_t track_index, bool muted);
void song_editor_solo_track(SongEditor* editor, uint8_t track_index, bool soloed);

// Audio quality control per track

void song_editor_set_track_adsr(SongEditor* editor, uint8_t track_index, float attack, float decay, float sustain, float release);
void song_editor_set_track_filter(SongEditor* editor, uint8_t track_index, uint8_t filter_type, float cutoff, float resonance);
void song_editor_set_track_compressor(SongEditor* editor, uint8_t track_index, float threshold, float ratio);
void song_editor_set_track_limiter(SongEditor* editor, uint8_t track_index, float threshold);

// Quantization
void song_editor_quantize_selection(SongEditor* editor, uint32_t grid_size);
void song_editor_quantize_track(SongEditor* editor, uint8_t track_index, uint32_t grid_size);

// Status
bool song_editor_is_playing(SongEditor* editor);
bool song_editor_is_recording(SongEditor* editor);
uint32_t song_editor_get_playhead_position(SongEditor* editor);
float song_editor_get_playhead_time(SongEditor* editor);
void song_editor_print_status(SongEditor* editor);

// Audio visualization
void song_editor_update_visualizer(SongEditor* editor);
void song_editor_render_waveform(SongEditor* editor);

// Instrument editor functions
void song_editor_toggle_instrument_editor(SongEditor* editor);
void song_editor_render_instrument_editor(SongEditor* editor);
void song_editor_handle_instrument_editor_input(SongEditor* editor, int x, int y, bool mouse_down);
void song_editor_set_track_wave_type(SongEditor* editor, int track_idx, int wave_type);
void song_editor_set_track_envelope(SongEditor* editor, int track_idx, float attack, float decay, float sustain, float release);

// JSON Save/Load functions
bool song_editor_save_json(SongEditor* editor, const char* filename);
bool song_editor_load_json(SongEditor* editor, const char* filename);

#ifdef SONG_EDITOR_STANDALONE
void song_editor_set_renderer(SDL_Renderer* renderer);
#endif

#endif // SONG_EDITOR_H
