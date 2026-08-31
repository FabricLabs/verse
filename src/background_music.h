#ifndef BACKGROUND_MUSIC_H
#define BACKGROUND_MUSIC_H

#include "synthesizer/synthesizer.h"
#include "sequencer/melody_loader.h"

typedef struct
{
  int sample_rate;
  MelodyLibrary *melody_library;
  Synthesizer *synth;
  Melody *current_melody;
  int current_note_index;
  float note_duration;
  float note_timer;
  float master_volume;
  float music_volume;
  bool enabled;
  float tuning_scale;
  bool initialized;

  // Advanced features
  bool use_advanced_notes;
  bool syncopation_enabled;
  bool sustain_enabled;
  float tempo;
  float time_signature_numerator;
  float time_signature_denominator;
  int beat_pattern_type;

  // New: synthesized ambient pad styled like title hum
  // Bar/section tracking for 4-bar loops across 12 total
  int bar_index;          // 0..(bars_in_section-1)
  int section_index;      // 0..(num_sections-1)
  int bars_in_section;    // default 4
  int num_sections;       // default 12
  float bar_time;         // seconds elapsed in current bar
  float bar_duration;     // seconds per bar (derived from tempo)
  float seconds_per_beat; // derived from tempo

  // Thematic pad channels and accent
  int pad_channels[4]; // harmonic partials 1..4 (keep channel 0 free for melody)
  int accent_channel;  // short "bang" at bar 4 ending
  bool theme_initialized;
  bool accent_active;
  float accent_timer;

  // Advanced timing for melody
  float current_note_seconds; // duration in seconds for current advanced note

  // Ambient pad layer toggle (disable to ensure core melody clarity)
  bool pad_enabled;

  // Dynamic intensity: 0 = subtle ambient, 1 = full combat.
  // target_intensity is set by the game; intensity eases toward it (fast attack, slow release).
  float intensity;
  float target_intensity;
  char active_category[32];
  float ambient_tempo;
  float combat_tempo;
} BackgroundMusicSystem;

// Core functions
BackgroundMusicSystem *background_music_create(int sample_rate);
void background_music_destroy(BackgroundMusicSystem *music);
bool background_music_initialize(BackgroundMusicSystem *music, const char *melodies_directory);

// Playback control
void background_music_start(BackgroundMusicSystem *music);
void background_music_stop(BackgroundMusicSystem *music);
void background_music_pause(BackgroundMusicSystem *music);
void background_music_resume(BackgroundMusicSystem *music);

// Volume control
void background_music_set_master_volume(BackgroundMusicSystem *music, float volume);
void background_music_set_music_volume(BackgroundMusicSystem *music, float volume);
float background_music_get_master_volume(BackgroundMusicSystem *music);
float background_music_get_music_volume(BackgroundMusicSystem *music);

// Melody control
void background_music_set_category(BackgroundMusicSystem *music, const char *category);
void background_music_set_melody(BackgroundMusicSystem *music, const char *file_name, const char *melody_name);

// Dynamic / procedural intensity (0 = ambient, 1 = combat). Call set_intensity from the game
// loop; call update each frame so tempo/category ease toward the target.
void background_music_set_intensity(BackgroundMusicSystem *music, float intensity);
float background_music_get_intensity(const BackgroundMusicSystem *music);
void background_music_update(BackgroundMusicSystem *music, float delta_time);
// Begin subtle ambient playback (used when leaving the title screen).
void background_music_play_ambient(BackgroundMusicSystem *music);

// Audio generation
float background_music_generate_sample(BackgroundMusicSystem *music);
void background_music_generate_buffer(BackgroundMusicSystem *music, float *buffer, int num_samples);
// Generate raw synthesizer output without advancing/playing melody even if music is disabled
// This allows UI/menu sounds (which share the synthesizer) to be heard when background music is off
void background_music_generate_synth_buffer(BackgroundMusicSystem *music, float *buffer, int num_samples);

// Enable/disable
void background_music_set_enabled(BackgroundMusicSystem *music, bool enabled);
bool background_music_is_enabled(BackgroundMusicSystem *music);

// Tuning
void background_music_set_tuning_scale(BackgroundMusicSystem *music, float tuning_scale);
float background_music_get_tuning_scale(BackgroundMusicSystem *music);

// Advanced features
void background_music_set_syncopation(BackgroundMusicSystem *music, bool enabled);
void background_music_set_sustain(BackgroundMusicSystem *music, bool enabled);
void background_music_set_tempo(BackgroundMusicSystem *music, float bpm);
void background_music_set_beat_pattern(BackgroundMusicSystem *music, int pattern_type);

// Status and debugging
bool background_music_is_initialized(BackgroundMusicSystem *music);
void background_music_print_status(BackgroundMusicSystem *music);

#endif // BACKGROUND_MUSIC_H
