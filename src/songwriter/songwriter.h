#ifndef SONGWRITER_H
#define SONGWRITER_H

#include <stdint.h>
#include <stdbool.h>
#include "synthesizer/synthesizer.h"
#include "sequencer/melody_loader.h"

// MIDI-inspired note values (0-127)
#define MIDI_NOTE_C0 12
#define MIDI_NOTE_C4 60
#define MIDI_NOTE_A4 69
#define MIDI_NOTE_C8 108

// Standard MIDI velocities
#define MIDI_VELOCITY_MIN 1
#define MIDI_VELOCITY_MAX 127
#define MIDI_VELOCITY_DEFAULT 100

// Standard MIDI channels
#define MIDI_CHANNEL_MIN 0
#define MIDI_CHANNEL_MAX 15
#define MIDI_CHANNEL_DEFAULT 0

// Enhanced sequencer settings
#define MAX_EVENTS_PER_TRACK 10000
#define SEQUENCER_BUFFER_SIZE 4096
#define MAX_POLYPHONY_PER_TRACK 8

// Time signatures
typedef struct {
    uint8_t numerator;
    uint8_t denominator;
} TimeSignature;

// Enhanced note event structure with improved timing
typedef struct {
    uint8_t channel;      // MIDI channel (0-15)
    uint8_t note;         // MIDI note number (0-127)
    uint8_t velocity;     // MIDI velocity (1-127)
    uint8_t duration;     // Duration in ticks
    uint32_t start_time;  // Start time in ticks
    uint32_t end_time;    // End time in ticks
    bool is_note_on;      // True for note on, false for note off

    // Enhanced features
    float velocity_float;  // High-resolution velocity
    float duration_float;  // High-resolution duration
    uint8_t articulation;  // Articulation type
    float portamento_time; // Portamento time
    float lfo_frequency;   // LFO frequency
    float lfo_depth;       // LFO depth
    uint8_t filter_type;   // Filter type
    float filter_cutoff;   // Filter cutoff
    float filter_resonance; // Filter resonance
} NoteEvent;

// Enhanced track structure with improved audio quality
typedef struct {
    char name[64];
    uint8_t channel;
    uint8_t program;      // MIDI program number (instrument)
    uint8_t volume;       // MIDI volume (0-127)
    uint8_t pan;          // MIDI pan (0-127, 64 = center)
    NoteEvent* events;
    uint32_t event_count;
    uint32_t event_capacity;

    // Enhanced features
    float volume_float;    // High-resolution volume
    float pan_float;       // High-resolution pan
    uint8_t wave_type;     // Wave type for synthesis
    float attack_time;     // ADSR attack
    float decay_time;      // ADSR decay
    float sustain_level;   // ADSR sustain
    float release_time;    // ADSR release
    bool auto_normalize;   // Auto-normalize for this track
    float compressor_threshold;
    float compressor_ratio;
    float limiter_threshold;

    // Polyphony support
    int polyphony_limit;
    int active_voices;
    uint8_t* voice_notes;
    float* voice_velocities;
    uint32_t* voice_start_times;
} Track;

// Enhanced song structure with improved timing
typedef struct {
    char name[128];
    char artist[128];
    char description[256];
    uint32_t tempo;           // BPM
    TimeSignature time_signature;
    uint32_t ticks_per_beat;  // MIDI ticks per quarter note
    uint32_t total_ticks;     // Total song length in ticks
    Track* tracks;
    uint8_t track_count;
    uint8_t track_capacity;

    // Enhanced features
    float tempo_float;        // High-resolution tempo
    bool auto_normalize;      // Global auto-normalize
    float master_volume;      // Master volume
    float master_filter_cutoff;
    float master_filter_resonance;
    float compressor_threshold;
    float compressor_ratio;
    float limiter_threshold;
    float delay_time;
    float delay_feedback;
    float delay_mix;
    float reverb_time;
    float reverb_mix;
} Song;

// Enhanced songwriter system with improved sequencer
typedef struct {
    Song* current_song;
    Synthesizer* synth;
    uint32_t current_tick;
    uint32_t current_beat;
    uint32_t current_measure;
    bool is_playing;
    bool is_recording;
    float playback_speed;
    uint32_t loop_start;
    uint32_t loop_end;
    bool loop_enabled;

    // Enhanced sequencer features
    uint32_t next_event_tick;
    uint32_t* track_event_indices;
    float* track_volumes;
    float* track_pans;
    bool* track_muted;
    bool* track_soloed;

    // Audio quality settings
    int sample_rate;
    float master_volume;
    bool auto_normalize;
    float normalization_factor;
    float peak_detector;
    float rms_detector;

    // Timing and synchronization
    uint64_t sample_count;
    float time_position;
    float beat_position;
    float measure_position;
    bool sync_to_external;
    float external_tempo;
    float external_phase;

    // Event scheduling
    NoteEvent** scheduled_events;
    uint32_t scheduled_event_count;
    uint32_t scheduled_event_capacity;

    // Audio buffer management
    float* audio_buffer;
    int audio_buffer_size;
    int audio_buffer_position;
    bool buffer_underrun;
    bool buffer_overrun;
} Songwriter;

// Songwriter API functions
Songwriter* songwriter_create(int sample_rate);
void songwriter_destroy(Songwriter* writer);

// Song management
Song* songwriter_create_song(const char* name, const char* artist, uint32_t tempo);
void songwriter_destroy_song(Song* song);
bool songwriter_load_song(Songwriter* writer, Song* song);
bool songwriter_save_song(Song* song, const char* filename);
Song* songwriter_load_song_from_file(const char* filename);

// Track management
Track* songwriter_create_track(Song* song, const char* name, uint8_t channel);
void songwriter_destroy_track(Track* track);
bool songwriter_add_track(Song* song, Track* track);
Track* songwriter_get_track(Song* song, uint8_t track_index);

// Note management
bool songwriter_add_note(Track* track, uint8_t note, uint8_t velocity, uint32_t start_time, uint32_t duration);
bool songwriter_remove_note(Track* track, uint32_t event_index);
bool songwriter_modify_note(Track* track, uint32_t event_index, uint8_t note, uint8_t velocity, uint32_t start_time, uint32_t duration);
void songwriter_update_song_duration(Song* song);

// Enhanced note management with audio quality features
bool songwriter_add_note_advanced(Track* track, uint8_t note, float velocity, float start_time, float duration,
                                uint8_t articulation, float portamento_time, float lfo_freq, float lfo_depth,
                                uint8_t filter_type, float filter_cutoff, float filter_resonance);
bool songwriter_modify_note_advanced(Track* track, uint32_t event_index, uint8_t note, float velocity,
                                   float start_time, float duration, uint8_t articulation, float portamento_time,
                                   float lfo_freq, float lfo_depth, uint8_t filter_type, float filter_cutoff,
                                   float filter_resonance);

// Playback control
void songwriter_play(Songwriter* writer);
void songwriter_stop(Songwriter* writer);
void songwriter_pause(Songwriter* writer);
void songwriter_seek(Songwriter* writer, uint32_t tick);
void songwriter_set_loop(Songwriter* writer, uint32_t start_tick, uint32_t end_tick);
void songwriter_set_playback_speed(Songwriter* writer, float speed);

// Enhanced playback control
void songwriter_seek_to_time(Songwriter* writer, float time_seconds);
void songwriter_set_tempo(Songwriter* writer, float bpm);
void songwriter_sync_to_external(Songwriter* writer, float tempo, float phase);
void songwriter_set_master_volume(Songwriter* writer, float volume);
void songwriter_set_auto_normalize(Songwriter* writer, bool enabled);

// Recording
void songwriter_start_recording(Songwriter* writer, Track* track);
void songwriter_stop_recording(Songwriter* writer);
void songwriter_record_note_on(Songwriter* writer, uint8_t note, uint8_t velocity);
void songwriter_record_note_off(Songwriter* writer, uint8_t note);

// Enhanced recording
void songwriter_record_note_on_advanced(Songwriter* writer, uint8_t note, float velocity, float duration);
void songwriter_record_note_off_advanced(Songwriter* writer, uint8_t note);

// Audio generation
float songwriter_generate_sample(Songwriter* writer);
void songwriter_generate_buffer(Songwriter* writer, float* buffer, int num_samples);

// Enhanced audio generation
void songwriter_generate_buffer_high_quality(Songwriter* writer, float* buffer, int num_samples);
float songwriter_get_peak_level(Songwriter* writer);
float songwriter_get_rms_level(Songwriter* writer);

// Track control
void songwriter_set_track_volume(Songwriter* writer, uint8_t track_index, float volume);
void songwriter_set_track_pan(Songwriter* writer, uint8_t track_index, float pan);
void songwriter_mute_track(Songwriter* writer, uint8_t track_index, bool muted);
void songwriter_solo_track(Songwriter* writer, uint8_t track_index, bool soloed);

// Audio quality control
void songwriter_set_track_wave_type(Songwriter* writer, uint8_t track_index, uint8_t wave_type);
void songwriter_set_track_adsr(Songwriter* writer, uint8_t track_index, float attack, float decay, float sustain, float release);
void songwriter_set_track_filter(Songwriter* writer, uint8_t track_index, uint8_t filter_type, float cutoff, float resonance);
void songwriter_set_track_compressor(Songwriter* writer, uint8_t track_index, float threshold, float ratio);
void songwriter_set_track_limiter(Songwriter* writer, uint8_t track_index, float threshold);

// Utility functions
float midi_note_to_frequency(uint8_t midi_note, float tuning_scale);
uint8_t frequency_to_midi_note(float frequency, float tuning_scale);
uint32_t beats_to_ticks(uint32_t beats, uint32_t ticks_per_beat);
uint32_t ticks_to_beats(uint32_t ticks, uint32_t ticks_per_beat);
uint32_t time_to_ticks(float time_seconds, uint32_t tempo, uint32_t ticks_per_beat);
float ticks_to_time(uint32_t ticks, uint32_t tempo, uint32_t ticks_per_beat);

// Enhanced utility functions
float time_to_ticks_float(float time_seconds, float tempo, uint32_t ticks_per_beat);
float ticks_to_time_float(uint32_t ticks, float tempo, uint32_t ticks_per_beat);
float velocity_to_float(uint8_t velocity);
uint8_t velocity_to_midi(float velocity_float);
float normalize_audio(float sample, float target_peak);

// Status and debugging
bool songwriter_is_playing(Songwriter* writer);
bool songwriter_is_recording(Songwriter* writer);
uint32_t songwriter_get_current_tick(Songwriter* writer);
uint32_t songwriter_get_current_beat(Songwriter* writer);
uint32_t songwriter_get_current_measure(Songwriter* writer);
float songwriter_get_current_time(Songwriter* writer);
float songwriter_get_current_beat_float(Songwriter* writer);
float songwriter_get_current_measure_float(Songwriter* writer);
void songwriter_print_song_info(Song* song);

// Enhanced status functions
void songwriter_get_audio_stats(Songwriter* writer, float* peak, float* rms, float* normalization);
void songwriter_get_timing_info(Songwriter* writer, float* time_pos, float* beat_pos, float* measure_pos);
void songwriter_get_buffer_status(Songwriter* writer, bool* underrun, bool* overrun, int* buffer_position);

#endif // SONGWRITER_H
