#include "dynamic_music.h"
#include "synthesizer/synthesizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Default instrument configurations based on the JSON
static const MusicInstrumentConfig default_instruments[5] = {
    {
        .name = "Ambient Pad",
        .wave_type = WAVE_SINE,
        .base_frequency = 220.0f,
        .base_amplitude = 0.3f,
        .attack_time = 0.5f,
        .decay_time = 0.3f,
        .sustain_level = 0.6f,
        .release_time = 1.0f,
        .active = true,
        .amplitude_multiplier = 1.0f,
        .frequency_multiplier = 1.0f,
        .pattern = PATTERN_SUSTAINED,
        .channel_id = -1
    },
    {
        .name = "Rhythm Bass",
        .wave_type = WAVE_TRIANGLE,
        .base_frequency = 110.0f,
        .base_amplitude = 0.4f,
        .attack_time = 0.1f,
        .decay_time = 0.2f,
        .sustain_level = 0.7f,
        .release_time = 0.3f,
        .active = true,
        .amplitude_multiplier = 1.0f,
        .frequency_multiplier = 1.0f,
        .pattern = PATTERN_SIMPLE_RHYTHM,
        .channel_id = -1
    },
    {
        .name = "Melody Lead",
        .wave_type = WAVE_SAW,
        .base_frequency = 440.0f,
        .base_amplitude = 0.25f,
        .attack_time = 0.05f,
        .decay_time = 0.1f,
        .sustain_level = 0.8f,
        .release_time = 0.2f,
        .active = true,
        .amplitude_multiplier = 1.0f,
        .frequency_multiplier = 1.0f,
        .pattern = PATTERN_CLEAR_MELODY,
        .channel_id = -1
    },
    {
        .name = "Harmony Voice",
        .wave_type = WAVE_SQUARE,
        .base_frequency = 330.0f,
        .base_amplitude = 0.2f,
        .attack_time = 0.2f,
        .decay_time = 0.4f,
        .sustain_level = 0.5f,
        .release_time = 0.6f,
        .active = true,
        .amplitude_multiplier = 1.0f,
        .frequency_multiplier = 1.0f,
        .pattern = PATTERN_SUPPORTIVE_CHORDS,
        .channel_id = -1
    },
    {
        .name = "Percussion",
        .wave_type = WAVE_SINE,
        .base_frequency = 880.0f,
        .base_amplitude = 0.15f,
        .attack_time = 0.01f,
        .decay_time = 0.05f,
        .sustain_level = 0.3f,
        .release_time = 0.1f,
        .active = true,
        .amplitude_multiplier = 1.0f,
        .frequency_multiplier = 1.0f,
        .pattern = PATTERN_GENTLE_BEAT,
        .channel_id = -1
    }
};

// Default game state configurations
static const MusicGameStateConfig default_state_configs[5] = {
    {   // PREGAME
        .name = "Main Menu",
        .description = "Calm, welcoming music for the main menu",
        .tempo = 80,
        .key = "C",
        .scale = "major",
        .instruments = {
            {.active = true, .amplitude_multiplier = 0.8f, .pattern = PATTERN_SUSTAINED},
            {.active = false, .amplitude_multiplier = 0.0f, .pattern = PATTERN_NONE},
            {.active = true, .amplitude_multiplier = 0.6f, .pattern = PATTERN_GENTLE_ARPEGGIO},
            {.active = true, .amplitude_multiplier = 0.4f, .pattern = PATTERN_CHORD_PROGRESSION},
            {.active = false, .amplitude_multiplier = 0.0f, .pattern = PATTERN_NONE}
        }
    },
    {   // TUTORIAL
        .name = "Tutorial Mode",
        .description = "Guiding, educational music with clear structure",
        .tempo = 90,
        .key = "G",
        .scale = "major",
        .instruments = {
            {.active = true, .amplitude_multiplier = 0.7f, .pattern = PATTERN_SUSTAINED},
            {.active = true, .amplitude_multiplier = 0.5f, .pattern = PATTERN_SIMPLE_RHYTHM},
            {.active = true, .amplitude_multiplier = 0.8f, .pattern = PATTERN_CLEAR_MELODY},
            {.active = true, .amplitude_multiplier = 0.6f, .pattern = PATTERN_SUPPORTIVE_CHORDS},
            {.active = true, .amplitude_multiplier = 0.3f, .pattern = PATTERN_GENTLE_BEAT}
        }
    },
    {   // HOME
        .name = "Home Base",
        .description = "Comfortable, safe haven music",
        .tempo = 75,
        .key = "F",
        .scale = "major",
        .instruments = {
            {.active = true, .amplitude_multiplier = 1.0f, .pattern = PATTERN_WARM_SUSTAINED},
            {.active = true, .amplitude_multiplier = 0.6f, .pattern = PATTERN_GENTLE_PULSE},
            {.active = true, .amplitude_multiplier = 0.7f, .pattern = PATTERN_COMFORTING_MELODY},
            {.active = true, .amplitude_multiplier = 0.8f, .pattern = PATTERN_RICH_HARMONY},
            {.active = false, .amplitude_multiplier = 0.0f, .pattern = PATTERN_NONE}
        }
    },
    {   // EXPLORE
        .name = "Exploration",
        .description = "Adventurous, mysterious exploration music",
        .tempo = 100,
        .key = "D",
        .scale = "minor",
        .instruments = {
            {.active = true, .amplitude_multiplier = 0.9f, .pattern = PATTERN_MYSTERIOUS_PAD},
            {.active = true, .amplitude_multiplier = 0.8f, .pattern = PATTERN_EXPLORATION_RHYTHM},
            {.active = true, .amplitude_multiplier = 0.9f, .pattern = PATTERN_ADVENTURE_MELODY},
            {.active = true, .amplitude_multiplier = 0.7f, .pattern = PATTERN_TENSION_CHORDS},
            {.active = true, .amplitude_multiplier = 0.6f, .pattern = PATTERN_EXPLORATION_BEAT}
        }
    },
    {   // COMBAT
        .name = "Combat",
        .description = "Intense, fast-paced battle music",
        .tempo = 140,
        .key = "A",
        .scale = "minor",
        .instruments = {
            {.active = false, .amplitude_multiplier = 0.0f, .pattern = PATTERN_NONE},
            {.active = true, .amplitude_multiplier = 1.0f, .pattern = PATTERN_AGGRESSIVE_BASS},
            {.active = true, .amplitude_multiplier = 1.0f, .pattern = PATTERN_INTENSE_MELODY},
            {.active = true, .amplitude_multiplier = 0.9f, .pattern = PATTERN_DRAMATIC_CHORDS},
            {.active = true, .amplitude_multiplier = 1.0f, .pattern = PATTERN_INTENSE_BEAT}
        }
    }
};

// Create dynamic music system
DynamicMusicSystem* dynamic_music_create(int sample_rate) {
    DynamicMusicSystem* music = malloc(sizeof(DynamicMusicSystem));
    if (!music) {
        printf("Failed to allocate dynamic music system\n");
        return NULL;
    }

    // Initialize synthesizer
    music->synth = synthesizer_create(sample_rate);
    if (!music->synth) {
        printf("Failed to create synthesizer for dynamic music\n");
        free(music);
        return NULL;
    }

    // Initialize default values
    music->current_state = MUSIC_STATE_PREGAME;
    music->master_volume = 0.7f;
    music->sample_rate = sample_rate;
    music->initialized = false;
    music->transition_progress = 0.0f;
    music->crossfade_duration = 2.0f;
    music->smooth_transitions = true;

    // Initialize pattern timing
    music->pattern_timer = 0.0f;
    music->pattern_update_interval = 0.5f; // Update patterns every 0.5 seconds
    music->current_pattern_step = 0;

    // Initialize adaptive parameters
    memset(&music->adaptive_params, 0, sizeof(AdaptiveParameters));
    music->adaptive_params.health = 1.0f;
    music->adaptive_params.battle_time = 0.0f;
    music->adaptive_params.player_level = 1;

    // Copy default configurations
    memcpy(music->state_configs, default_state_configs, sizeof(default_state_configs));

    // Initialize instruments with synthesizer channels
    for (int i = 0; i < 5; i++) {
        MusicInstrumentConfig* config = &music->state_configs[0].instruments[i];
        memcpy(config, &default_instruments[i], sizeof(MusicInstrumentConfig));

        // Add channel to synthesizer and store the channel ID
        int channel_id = synthesizer_add_channel(
            music->synth,
            config->wave_type,
            config->base_frequency,
            config->base_amplitude
        );

        // Update the channel ID in the config
        config->channel_id = channel_id;

        // Set ADSR envelope
        synthesizer_set_channel_adsr(
            music->synth,
            channel_id,
            config->attack_time,
            config->decay_time,
            config->sustain_level,
            config->release_time
        );

        // Also update the default instruments array for consistency
        music->default_instruments[i] = *config;
    }

    music->initialized = true;
    printf("Dynamic music system created successfully\n");
    return music;
}

// Destroy dynamic music system
void dynamic_music_destroy(DynamicMusicSystem* music) {
    if (!music) return;

    if (music->synth) {
        synthesizer_destroy(music->synth);
    }

    free(music);
    printf("Dynamic music system destroyed\n");
}

// Set game state
void dynamic_music_set_game_state(DynamicMusicSystem* music, MusicGameState state) {
    if (!music || !music->initialized) return;

    if (state == music->current_state) return;

    printf("Transitioning music from state %d to %d\n", music->current_state, state);

    // Start transition
    dynamic_music_start_transition(music, state);

    // Update instrument configurations for new state
    MusicGameStateConfig* new_config = &music->state_configs[state];

    for (int i = 0; i < 5; i++) {
        MusicInstrumentConfig* instrument = &new_config->instruments[i];
        const MusicInstrumentConfig* default_instrument = &music->default_instruments[i];

        // Update instrument settings
        if (instrument->active) {
            synthesizer_set_channel_amplitude(
                music->synth,
                default_instrument->channel_id,
                default_instrument->base_amplitude * instrument->amplitude_multiplier
            );

            // Trigger the instrument based on its pattern
            dynamic_music_generate_pattern(music, i, instrument->pattern);
        } else {
            // Turn off inactive instruments
            synthesizer_note_off(music->synth, default_instrument->channel_id);
            synthesizer_set_channel_amplitude(music->synth, default_instrument->channel_id, 0.0f);
        }
    }

    music->current_state = state;
    printf("Music state transitioned to %d\n", state);
}

// Get current game state
MusicGameState dynamic_music_get_current_state(DynamicMusicSystem* music) {
    return music ? music->current_state : MUSIC_STATE_PREGAME;
}

// Update health-based adaptive parameters
void dynamic_music_update_health(DynamicMusicSystem* music, float health_percentage) {
    if (!music) return;

    music->adaptive_params.health = health_percentage;
    music->adaptive_params.low_health = (health_percentage < 0.3f);
    music->adaptive_params.high_health = (health_percentage > 0.7f);

    // Apply health-based effects
    if (music->adaptive_params.low_health) {
        // Slow down tempo and reduce volume for low health
        synthesizer_set_master_volume(music->synth, music->master_volume * 0.7f);
    } else if (music->adaptive_params.high_health) {
        // Increase tempo and volume for high health
        synthesizer_set_master_volume(music->synth, music->master_volume * 1.1f);
    } else {
        // Normal volume
        synthesizer_set_master_volume(music->synth, music->master_volume);
    }
}

// Update battle time adaptive parameters
void dynamic_music_update_battle_time(DynamicMusicSystem* music, float battle_time_seconds) {
    if (!music) return;

    music->adaptive_params.battle_time = battle_time_seconds;
    music->adaptive_params.short_battle = (battle_time_seconds < 30.0f);
    music->adaptive_params.long_battle = (battle_time_seconds > 120.0f);

    // Apply battle time effects
    if (music->adaptive_params.short_battle && music->current_state == MUSIC_STATE_COMBAT) {
        // Increase intensity for short battles
        for (int i = 0; i < 5; i++) {
            const MusicInstrumentConfig* instrument = &default_instruments[i];
            if (instrument->channel_id >= 0) {
                float current_amp = synthesizer_get_channel_amplitude(music->synth, instrument->channel_id);
                synthesizer_set_channel_amplitude(music->synth, instrument->channel_id, current_amp * 1.2f);
            }
        }
    } else if (music->adaptive_params.long_battle && music->current_state == MUSIC_STATE_COMBAT) {
        // Reduce intensity for long battles
        for (int i = 0; i < 5; i++) {
            const MusicInstrumentConfig* instrument = &default_instruments[i];
            if (instrument->channel_id >= 0) {
                float current_amp = synthesizer_get_channel_amplitude(music->synth, instrument->channel_id);
                synthesizer_set_channel_amplitude(music->synth, instrument->channel_id, current_amp * 0.9f);
            }
        }
    }
}

// Update player level adaptive parameters
void dynamic_music_update_player_level(DynamicMusicSystem* music, int player_level) {
    if (!music) return;

    music->adaptive_params.player_level = player_level;
    music->adaptive_params.low_level = (player_level < 5);
    music->adaptive_params.high_level = (player_level > 15);

    // Apply level-based effects
    if (music->adaptive_params.low_level) {
        // Simplify music for low-level players
        for (int i = 0; i < 5; i++) {
            const MusicInstrumentConfig* instrument = &default_instruments[i];
            if (instrument->channel_id >= 0 && i != INSTRUMENT_MELODY_LEAD) {
                synthesizer_set_channel_amplitude(music->synth, instrument->channel_id, 0.0f);
            }
        }
    } else if (music->adaptive_params.high_level) {
        // Add complexity for high-level players
        for (int i = 0; i < 5; i++) {
            const MusicInstrumentConfig* instrument = &default_instruments[i];
            if (instrument->channel_id >= 0) {
                float current_amp = synthesizer_get_channel_amplitude(music->synth, instrument->channel_id);
                synthesizer_set_channel_amplitude(music->synth, instrument->channel_id, current_amp * 1.1f);
            }
        }
    }
}

// Generate audio sample
float dynamic_music_generate_sample(DynamicMusicSystem* music) {
    if (!music || !music->initialized) return 0.0f;

    float sample = synthesizer_generate_sample(music->synth);
    return sample * music->master_volume;
}

// Generate audio buffer
void dynamic_music_generate_buffer(DynamicMusicSystem* music, float* buffer, int num_samples) {
    if (!music || !music->initialized || !buffer) return;

    // Update patterns based on time
    float delta_time = (float)num_samples / (float)music->sample_rate;
    dynamic_music_update_patterns(music, delta_time);

    synthesizer_generate_buffer(music->synth, buffer, num_samples);

    // Apply master volume
    for (int i = 0; i < num_samples; i++) {
        buffer[i] *= music->master_volume;
    }
}

// Set instrument active state
void dynamic_music_set_instrument_active(DynamicMusicSystem* music, MusicInstrument instrument, bool active) {
    if (!music || !music->initialized || instrument < 0 || instrument >= 5) return;

    const MusicInstrumentConfig* config = &music->default_instruments[instrument];
    if (config->channel_id >= 0) {
        if (active) {
            synthesizer_note_on(music->synth, config->channel_id);
        } else {
            synthesizer_note_off(music->synth, config->channel_id);
        }
    }
}

// Set instrument amplitude
void dynamic_music_set_instrument_amplitude(DynamicMusicSystem* music, MusicInstrument instrument, float amplitude) {
    if (!music || !music->initialized || instrument < 0 || instrument >= 5) return;

    const MusicInstrumentConfig* config = &music->default_instruments[instrument];
    if (config->channel_id >= 0) {
        synthesizer_set_channel_amplitude(music->synth, config->channel_id, amplitude);
    }
}

// Set instrument frequency
void dynamic_music_set_instrument_frequency(DynamicMusicSystem* music, MusicInstrument instrument, float frequency) {
    if (!music || !music->initialized || instrument < 0 || instrument >= 5) return;

    const MusicInstrumentConfig* config = &music->default_instruments[instrument];
    if (config->channel_id >= 0) {
        synthesizer_set_channel_frequency(music->synth, config->channel_id, frequency);
    }
}

// Generate music pattern
void dynamic_music_generate_pattern(DynamicMusicSystem* music, MusicInstrument instrument, MusicPattern pattern) {
    if (!music || !music->initialized) return;

    const MusicInstrumentConfig* config = &music->default_instruments[instrument];
    if (config->channel_id < 0) return;

    switch (pattern) {
        case PATTERN_SUSTAINED:
        case PATTERN_WARM_SUSTAINED:
        case PATTERN_MYSTERIOUS_PAD:
            // Simple sustained note
            synthesizer_note_on(music->synth, config->channel_id);
            break;

        case PATTERN_GENTLE_ARPEGGIO:
        case PATTERN_CLEAR_MELODY:
        case PATTERN_COMFORTING_MELODY:
        case PATTERN_ADVENTURE_MELODY:
        case PATTERN_INTENSE_MELODY:
            // Melodic patterns - trigger note
            synthesizer_note_on(music->synth, config->channel_id);
            break;

        case PATTERN_CHORD_PROGRESSION:
        case PATTERN_SUPPORTIVE_CHORDS:
        case PATTERN_RICH_HARMONY:
        case PATTERN_TENSION_CHORDS:
        case PATTERN_DRAMATIC_CHORDS:
            // Harmonic patterns
            synthesizer_note_on(music->synth, config->channel_id);
            break;

        case PATTERN_SIMPLE_RHYTHM:
        case PATTERN_GENTLE_BEAT:
        case PATTERN_GENTLE_PULSE:
        case PATTERN_EXPLORATION_RHYTHM:
        case PATTERN_EXPLORATION_BEAT:
        case PATTERN_AGGRESSIVE_BASS:
        case PATTERN_INTENSE_BEAT:
            // Rhythmic patterns
            synthesizer_note_on(music->synth, config->channel_id);
            break;

        case PATTERN_NONE:
        default:
            synthesizer_note_off(music->synth, config->channel_id);
            break;
    }
}

// Update patterns over time
void dynamic_music_update_patterns(DynamicMusicSystem* music, float delta_time) {
    if (!music || !music->initialized) return;

    // Update pattern timer
    music->pattern_timer += delta_time;

    // Check if it's time to update patterns
    if (music->pattern_timer >= music->pattern_update_interval) {
        music->pattern_timer = 0.0f;
        music->current_pattern_step++;

        // Get current state configuration
        MusicGameStateConfig* current_config = &music->state_configs[music->current_state];

        // Update each active instrument's pattern
        for (int i = 0; i < 5; i++) {
            MusicInstrumentConfig* instrument = &current_config->instruments[i];
            const MusicInstrumentConfig* default_instrument = &music->default_instruments[i];

            if (instrument->active && default_instrument->channel_id >= 0) {
                // Generate new pattern step based on the pattern type
                switch (instrument->pattern) {
                    case PATTERN_GENTLE_ARPEGGIO:
                    case PATTERN_CLEAR_MELODY:
                    case PATTERN_COMFORTING_MELODY:
                    case PATTERN_ADVENTURE_MELODY:
                    case PATTERN_INTENSE_MELODY:
                        // Melodic patterns - change notes
                        {
                            float base_freq = default_instrument->base_frequency;
                            float note_multiplier = 1.0f + (music->current_pattern_step % 8) * 0.25f;
                            float new_freq = base_freq * note_multiplier;
                            synthesizer_set_channel_frequency(music->synth, default_instrument->channel_id, new_freq);
                        }
                        break;

                    case PATTERN_CHORD_PROGRESSION:
                    case PATTERN_SUPPORTIVE_CHORDS:
                    case PATTERN_RICH_HARMONY:
                    case PATTERN_TENSION_CHORDS:
                    case PATTERN_DRAMATIC_CHORDS:
                        // Harmonic patterns - change chord progressions
                        {
                            float base_freq = default_instrument->base_frequency;
                            float chord_multiplier = 1.0f + (music->current_pattern_step % 4) * 0.5f;
                            float new_freq = base_freq * chord_multiplier;
                            synthesizer_set_channel_frequency(music->synth, default_instrument->channel_id, new_freq);
                        }
                        break;

                    case PATTERN_SIMPLE_RHYTHM:
                    case PATTERN_GENTLE_BEAT:
                    case PATTERN_GENTLE_PULSE:
                    case PATTERN_EXPLORATION_RHYTHM:
                    case PATTERN_EXPLORATION_BEAT:
                    case PATTERN_AGGRESSIVE_BASS:
                    case PATTERN_INTENSE_BEAT:
                        // Rhythmic patterns - change rhythm
                        {
                            float base_amp = default_instrument->base_amplitude;
                            float rhythm_multiplier = (music->current_pattern_step % 4 == 0) ? 1.0f : 0.3f;
                            float new_amp = base_amp * rhythm_multiplier * instrument->amplitude_multiplier;
                            synthesizer_set_channel_amplitude(music->synth, default_instrument->channel_id, new_amp);
                        }
                        break;

                    case PATTERN_SUSTAINED:
                    case PATTERN_WARM_SUSTAINED:
                    case PATTERN_MYSTERIOUS_PAD:
                    default:
                        // Sustained patterns - keep playing
                        break;
                }
            }
        }
    }
}

// Set master volume
void dynamic_music_set_master_volume(DynamicMusicSystem* music, float volume) {
    if (!music) return;
    music->master_volume = volume;
    synthesizer_set_master_volume(music->synth, volume);
}

// Get master volume
float dynamic_music_get_master_volume(DynamicMusicSystem* music) {
    return music ? music->master_volume : 0.0f;
}

// Check if system is initialized
bool dynamic_music_is_initialized(DynamicMusicSystem* music) {
    return music ? music->initialized : false;
}

// Start transition to new state
void dynamic_music_start_transition(DynamicMusicSystem* music, MusicGameState new_state) {
    if (!music) return;
    music->transition_progress = 0.0f;
    printf("Starting transition to state %d\n", new_state);
}

// Check if system is transitioning
bool dynamic_music_is_transitioning(DynamicMusicSystem* music) {
    return music ? (music->transition_progress < 1.0f) : false;
}

// Get transition progress
float dynamic_music_get_transition_progress(DynamicMusicSystem* music) {
    return music ? music->transition_progress : 0.0f;
}

// Load configuration from file (placeholder)
bool dynamic_music_load_config(DynamicMusicSystem* music, const char* config_file) {
    if (!music || !config_file) return false;
    printf("Loading music config from: %s\n", config_file);
    // TODO: Implement JSON config loading
    return true;
}

// Save configuration to file (placeholder)
bool dynamic_music_save_config(DynamicMusicSystem* music, const char* config_file) {
    if (!music || !config_file) return false;
    printf("Saving music config to: %s\n", config_file);
    // TODO: Implement JSON config saving
    return true;
}
