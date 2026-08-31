#ifndef DYNAMIC_MUSIC_H
#define DYNAMIC_MUSIC_H

#include "synthesizer/synthesizer.h"
#include <stdbool.h>
#include <stdint.h>

// Game states for music adaptation
typedef enum {
    MUSIC_STATE_PREGAME,    // Main menu
    MUSIC_STATE_TUTORIAL,   // Tutorial mode
    MUSIC_STATE_HOME,       // Home base/safe area
    MUSIC_STATE_EXPLORE,    // Exploration
    MUSIC_STATE_COMBAT      // Combat/battle
} MusicGameState;

// Instrument types for the 5-instrument system
typedef enum {
    INSTRUMENT_AMBIENT_PAD,    // Atmospheric background
    INSTRUMENT_RHYTHM_BASS,    // Rhythmic foundation
    INSTRUMENT_MELODY_LEAD,    // Main melodic voice
    INSTRUMENT_HARMONY_VOICE,  // Harmonic accompaniment
    INSTRUMENT_PERCUSSION      // Rhythmic elements
} MusicInstrument;

// Music pattern types
typedef enum {
    PATTERN_NONE,
    PATTERN_SUSTAINED,
    PATTERN_GENTLE_ARPEGGIO,
    PATTERN_CHORD_PROGRESSION,
    PATTERN_SIMPLE_RHYTHM,
    PATTERN_CLEAR_MELODY,
    PATTERN_SUPPORTIVE_CHORDS,
    PATTERN_GENTLE_BEAT,
    PATTERN_WARM_SUSTAINED,
    PATTERN_GENTLE_PULSE,
    PATTERN_COMFORTING_MELODY,
    PATTERN_RICH_HARMONY,
    PATTERN_MYSTERIOUS_PAD,
    PATTERN_EXPLORATION_RHYTHM,
    PATTERN_ADVENTURE_MELODY,
    PATTERN_TENSION_CHORDS,
    PATTERN_EXPLORATION_BEAT,
    PATTERN_AGGRESSIVE_BASS,
    PATTERN_INTENSE_MELODY,
    PATTERN_DRAMATIC_CHORDS,
    PATTERN_INTENSE_BEAT
} MusicPattern;

// Instrument configuration
typedef struct {
    char name[32];
    WaveType wave_type;
    float base_frequency;
    float base_amplitude;
    float attack_time;
    float decay_time;
    float sustain_level;
    float release_time;
    bool active;
    float amplitude_multiplier;
    float frequency_multiplier;
    MusicPattern pattern;
    int channel_id;
} MusicInstrumentConfig;

// Game state music configuration
typedef struct {
    char name[32];
    char description[128];
    int tempo;
    char key[8];
    char scale[16];
    MusicInstrumentConfig instruments[5];
} MusicGameStateConfig;

// Adaptive parameters
typedef struct {
    float health;
    float battle_time;
    int player_level;
    bool low_health;
    bool high_health;
    bool short_battle;
    bool long_battle;
    bool low_level;
    bool high_level;
} AdaptiveParameters;

// Dynamic music system
typedef struct {
    Synthesizer* synth;
    MusicGameState current_state;
    MusicGameStateConfig state_configs[5];
    MusicInstrumentConfig default_instruments[5];  // Store default instrument configs with channel IDs
    AdaptiveParameters adaptive_params;
    float master_volume;
    int sample_rate;
    bool initialized;
    float transition_progress;
    float crossfade_duration;
    bool smooth_transitions;

    // Pattern timing
    float pattern_timer;
    float pattern_update_interval;
    int current_pattern_step;
} DynamicMusicSystem;

// Function declarations

// Core system functions
DynamicMusicSystem* dynamic_music_create(int sample_rate);
void dynamic_music_destroy(DynamicMusicSystem* music);
bool dynamic_music_initialize(DynamicMusicSystem* music, const char* config_file);

// State management
void dynamic_music_set_game_state(DynamicMusicSystem* music, MusicGameState state);
MusicGameState dynamic_music_get_current_state(DynamicMusicSystem* music);

// Adaptive parameter updates
void dynamic_music_update_health(DynamicMusicSystem* music, float health_percentage);
void dynamic_music_update_battle_time(DynamicMusicSystem* music, float battle_time_seconds);
void dynamic_music_update_player_level(DynamicMusicSystem* music, int player_level);

// Audio generation
float dynamic_music_generate_sample(DynamicMusicSystem* music);
void dynamic_music_generate_buffer(DynamicMusicSystem* music, float* buffer, int num_samples);

// Instrument control
void dynamic_music_set_instrument_active(DynamicMusicSystem* music, MusicInstrument instrument, bool active);
void dynamic_music_set_instrument_amplitude(DynamicMusicSystem* music, MusicInstrument instrument, float amplitude);
void dynamic_music_set_instrument_frequency(DynamicMusicSystem* music, MusicInstrument instrument, float frequency);

// Pattern generation
void dynamic_music_generate_pattern(DynamicMusicSystem* music, MusicInstrument instrument, MusicPattern pattern);
void dynamic_music_update_patterns(DynamicMusicSystem* music, float delta_time);

// Utility functions
void dynamic_music_set_master_volume(DynamicMusicSystem* music, float volume);
float dynamic_music_get_master_volume(DynamicMusicSystem* music);
bool dynamic_music_is_initialized(DynamicMusicSystem* music);

// Configuration loading
bool dynamic_music_load_config(DynamicMusicSystem* music, const char* config_file);
bool dynamic_music_save_config(DynamicMusicSystem* music, const char* config_file);

// Transition management
void dynamic_music_start_transition(DynamicMusicSystem* music, MusicGameState new_state);
bool dynamic_music_is_transitioning(DynamicMusicSystem* music);
float dynamic_music_get_transition_progress(DynamicMusicSystem* music);

#endif // DYNAMIC_MUSIC_H
