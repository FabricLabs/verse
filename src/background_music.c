#include "background_music.h"
#include "sequencer/melody_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

static void background_music_init_pad_channels(BackgroundMusicSystem *music) {
    if (!music || !music->synth)
        return;

    for (int i = 0; i < 4; i++) {
        music->pad_channels[i] =
            synthesizer_add_channel(music->synth, WAVE_SINE, 92.50f * (i + 1), 0.0f);
        if (music->pad_channels[i] >= 0) {
            synthesizer_set_channel_adsr(music->synth, music->pad_channels[i], 0.8f, 0.4f, 0.85f,
                                        1.2f);
            synthesizer_set_channel_filter(music->synth, music->pad_channels[i], FILTER_NONE,
                                          20000.0f, 0.0f);
        }
    }
    music->accent_channel = synthesizer_add_channel(music->synth, WAVE_SINE, 185.0f, 0.0f);
    if (music->accent_channel >= 0) {
        synthesizer_set_channel_adsr(music->synth, music->accent_channel, 0.005f, 0.05f, 0.4f,
                                    0.3f);
    }
}

BackgroundMusicSystem* background_music_create(int sample_rate) {
    BackgroundMusicSystem* music = malloc(sizeof(BackgroundMusicSystem));
    if (!music) return NULL;

    music->sample_rate = sample_rate;
    music->melody_library = NULL;
    music->synth = NULL;
    music->current_melody = NULL;
    music->current_note_index = 0;
    music->note_duration = 0.45f; // slower ambient default; combat shortens via tempo
    music->note_timer = 0.0f;
    music->master_volume = 1.0f;
    music->music_volume = 0.55f; // keep ambient subtle; intensity raises effective gain
    music->enabled = true;
    music->tuning_scale = 440.0f; // Default A4 = 440Hz
    music->initialized = false;

    // Advanced features
    music->use_advanced_notes = false;
    music->syncopation_enabled = false;
    music->sustain_enabled = false;
    music->ambient_tempo = 72.0f;
    music->combat_tempo = 138.0f;
    music->tempo = music->ambient_tempo;
    music->time_signature_numerator = 4.0f;
    music->time_signature_denominator = 4.0f;
    music->beat_pattern_type = 0; // Standard 4/4

    // Bar/section loop defaults
    music->bars_in_section = 4;
    music->num_sections = 12;
    music->bar_index = 0;
    music->section_index = 0;
    music->bar_time = 0.0f;
    music->bar_duration = 60.0f / music->tempo * music->time_signature_numerator;
    music->seconds_per_beat = 60.0f / music->tempo;
    music->theme_initialized = false;
    music->accent_active = false;
    music->accent_timer = 0.0f;
    music->pad_enabled = true; // soft pad under ambient; combat dials melody up instead
    for (int i = 0; i < 4; i++)
        music->pad_channels[i] = -1;
    music->accent_channel = -1;

    music->intensity = 0.0f;
    music->target_intensity = 0.0f;
    strncpy(music->active_category, "Ambient", sizeof(music->active_category) - 1);
    music->active_category[sizeof(music->active_category) - 1] = '\0';

    return music;
}

void background_music_destroy(BackgroundMusicSystem* music) {
    if (!music) return;

    if (music->melody_library) {
        melody_library_destroy(music->melody_library);
    }

    if (music->synth) {
        synthesizer_destroy(music->synth);
    }

    free(music);
}

bool background_music_initialize(BackgroundMusicSystem* music, const char* melodies_directory) {
    if (!music || !melodies_directory) return false;

    // Create synthesizer with advanced features
    music->synth = synthesizer_create(music->sample_rate);
    if (!music->synth) {
        printf("Failed to create synthesizer for background music\n");
        return false;
    }

    background_music_init_pad_channels(music);

    // Set advanced timing
    synthesizer_set_tempo(music->synth, music->tempo);
    synthesizer_set_time_signature(music->synth, music->time_signature_numerator, music->time_signature_denominator);

    // Create melody library
    music->melody_library = melody_library_create();
    if (!music->melody_library) {
        printf("Failed to create melody library\n");
        return false;
    }

    // Load melodies from directory
    if (!melody_library_load_from_directory(music->melody_library, melodies_directory)) {
        printf("Failed to load melodies from directory: %s\n", melodies_directory);
        return false;
    }

    // Prefer subtle ambient as the default procedural bed
    music->current_melody = melody_library_get_melody_by_name(music->melody_library, "Ambient", "Peaceful");
    if (!music->current_melody) {
        music->current_melody = melody_library_get_random_melody(music->melody_library, "Ambient");
    }
    if (!music->current_melody) {
        printf("Warning: No Ambient melody found, trying Theme\n");
        music->current_melody = melody_library_get_melody_by_name(music->melody_library, "Theme", "Main");
    }
    if (!music->current_melody) {
        printf("Warning: No Theme melody found, trying random melody\n");
        music->current_melody = melody_library_get_random_melody(music->melody_library, NULL);
    }

    if (music->current_melody) {
        printf("Background music: Loaded melody '%s' with %d notes\n", music->current_melody->name, music->current_melody->note_count);

        // Initialize advanced features for the melody
        if (music->current_melody->use_advanced_notes) {
            music->use_advanced_notes = true;
            printf("Using advanced note features\n");
        }

        // Apply syncopation if enabled
        if (music->syncopation_enabled) {
            apply_syncopation_to_melody(music->current_melody);
            printf("Applied syncopation to melody\n");
        }

        // Apply sustain if enabled
        if (music->sustain_enabled) {
            apply_sustain_to_melody(music->current_melody);
            printf("Applied sustain to melody\n");
        }

        // Generate beat pattern
        generate_beat_pattern(music->current_melody, music->beat_pattern_type);
        printf("Generated beat pattern type %d\n", music->beat_pattern_type);
    } else {
        printf("ERROR: No melody loaded at all!\n");
    }

    music->initialized = true;
    printf("Background music system initialized successfully\n");
    return true;
}

void background_music_start(BackgroundMusicSystem* music) {
    if (!music || !music->initialized) return;

    music->current_note_index = 0;
    music->note_timer = 0.0f;

    if (music->current_melody && music->current_melody->note_count > 0) {
        // Start playing the first note with advanced features
        if (music->use_advanced_notes && music->current_melody->advanced_notes[0].note[0] != '\0') {
            // Use advanced note
            AdvancedNote* note = &music->current_melody->advanced_notes[0];
            float frequency = note_to_frequency(note->note, music->tuning_scale);

            if (frequency > 0.0f) {
                synthesizer_set_channel_frequency(music->synth, 0, frequency);
                synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume * note->velocity);
                synthesizer_set_channel_duration(music->synth, 0, note->duration);

                // Apply articulation
                if (note->accent > 0) {
                    synthesizer_set_channel_accent(music->synth, 0, note->accent);
                }

                // Apply syncopation
                if (note->syncopated) {
                    synthesizer_set_channel_syncopation(music->synth, 0, 0.125f); // 1/8 note offset
                }

                // Apply sustain
                if (note->sustained) {
                    synthesizer_set_channel_sustain(music->synth, 0, note->sustain_level, note->sustain_time);
                }

                synthesizer_note_on_advanced(music->synth, 0, note->velocity, note->duration);
            }
        } else {
            // Use simple note
            const char* note = music->current_melody->notes[0];
            float frequency = note_to_frequency(note, music->tuning_scale);

            if (frequency > 0.0f) {
                synthesizer_set_channel_frequency(music->synth, 0, frequency);
                synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume);
                synthesizer_note_on(music->synth, 0);
            }
        }
    }
}

void background_music_stop(BackgroundMusicSystem* music) {
    if (!music || !music->initialized) return;

    synthesizer_note_off(music->synth, 0);
}

void background_music_pause(BackgroundMusicSystem* music) {
    if (!music || !music->initialized) return;

    synthesizer_note_off(music->synth, 0);
}

void background_music_resume(BackgroundMusicSystem* music) {
    if (!music || !music->initialized) return;

    if (music->current_melody && music->current_melody->note_count > 0) {
        const char* note = music->current_melody->notes[music->current_note_index];
        float frequency = note_to_frequency(note, music->tuning_scale);

        if (frequency > 0.0f) {
            synthesizer_set_channel_frequency(music->synth, 0, frequency);
            synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume);
            synthesizer_note_on(music->synth, 0);
        }
    }
}

void background_music_set_master_volume(BackgroundMusicSystem* music, float volume) {
    if (!music) return;

    music->master_volume = fmaxf(0.0f, fminf(1.0f, volume));
}

void background_music_set_music_volume(BackgroundMusicSystem* music, float volume) {
    if (!music) return;

    music->music_volume = fmaxf(0.0f, fminf(1.0f, volume));

    // Update current note volume if playing
    if (music->current_melody && music->current_melody->note_count > 0) {
        if (music->use_advanced_notes && music->current_melody->advanced_notes[music->current_note_index].note[0] != '\0') {
            // Update advanced note
            AdvancedNote* note = &music->current_melody->advanced_notes[music->current_note_index];
            float frequency = note_to_frequency(note->note, music->tuning_scale);

            if (frequency > 0.0f) {
                synthesizer_set_channel_frequency(music->synth, 0, frequency);
                synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume * note->velocity);
                synthesizer_note_on_advanced(music->synth, 0, note->velocity, note->duration);
            }
        } else {
            // Update simple note
            const char* note = music->current_melody->notes[music->current_note_index];
            float frequency = note_to_frequency(note, music->tuning_scale);

            if (frequency > 0.0f) {
                synthesizer_set_channel_frequency(music->synth, 0, frequency);
                synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume);
                synthesizer_note_on(music->synth, 0);
            }
        }
    }
}

float background_music_get_master_volume(BackgroundMusicSystem* music) {
    return music ? music->master_volume : 0.0f;
}

float background_music_get_music_volume(BackgroundMusicSystem* music) {
    return music ? music->music_volume : 0.0f;
}

void background_music_set_category(BackgroundMusicSystem* music, const char* category) {
    if (!music || !music->initialized || !category) return;

    // Avoid restarting the same bed mid-phrase
    if (strcasecmp(music->active_category, category) == 0 && music->current_melody)
        return;

    Melody* new_melody = melody_library_get_random_melody(music->melody_library, category);
    if (new_melody) {
        music->current_melody = new_melody;
        music->current_note_index = 0;
        music->note_timer = 0.0f;
        strncpy(music->active_category, category, sizeof(music->active_category) - 1);
        music->active_category[sizeof(music->active_category) - 1] = '\0';

        // Apply advanced features to new melody
        if (music->syncopation_enabled) {
            apply_syncopation_to_melody(music->current_melody);
        }
        if (music->sustain_enabled) {
            apply_sustain_to_melody(music->current_melody);
        }
        generate_beat_pattern(music->current_melody, music->beat_pattern_type);

        // Start playing the new melody
        if (music->enabled && new_melody->note_count > 0) {
            if (music->use_advanced_notes && new_melody->advanced_notes[0].note[0] != '\0') {
                AdvancedNote* note = &new_melody->advanced_notes[0];
                float frequency = note_to_frequency(note->note, music->tuning_scale);

                if (frequency > 0.0f) {
                    synthesizer_set_channel_frequency(music->synth, 0, frequency);
                    synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume * note->velocity);
                    synthesizer_note_on_advanced(music->synth, 0, note->velocity, note->duration);
                }
            } else {
                const char* note = new_melody->notes[0];
                float frequency = note_to_frequency(note, music->tuning_scale);

                if (frequency > 0.0f) {
                    synthesizer_set_channel_frequency(music->synth, 0, frequency);
                    synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume);
                    synthesizer_note_on(music->synth, 0);
                }
            }
        }
    }
}

void background_music_set_melody(BackgroundMusicSystem* music, const char* file_name, const char* melody_name) {
    if (!music || !music->initialized) return;

    Melody* new_melody = melody_library_get_melody_by_name(music->melody_library, file_name, melody_name);
    if (new_melody) {
        music->current_melody = new_melody;
        music->current_note_index = 0;
        music->note_timer = 0.0f;

        // Apply advanced features to new melody
        if (music->syncopation_enabled) {
            apply_syncopation_to_melody(music->current_melody);
        }
        if (music->sustain_enabled) {
            apply_sustain_to_melody(music->current_melody);
        }
        generate_beat_pattern(music->current_melody, music->beat_pattern_type);

        // Start playing the new melody
        if (music->enabled && new_melody->note_count > 0) {
            if (music->use_advanced_notes && new_melody->advanced_notes[0].note[0] != '\0') {
                AdvancedNote* note = &new_melody->advanced_notes[0];
                float frequency = note_to_frequency(note->note, music->tuning_scale);

                if (frequency > 0.0f) {
                    synthesizer_set_channel_frequency(music->synth, 0, frequency);
                    synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume * note->velocity);
                    synthesizer_note_on_advanced(music->synth, 0, note->velocity, note->duration);
                }
            } else {
                const char* note = new_melody->notes[0];
                float frequency = note_to_frequency(note, music->tuning_scale);

                if (frequency > 0.0f) {
                    synthesizer_set_channel_frequency(music->synth, 0, frequency);
                    synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume);
                    synthesizer_note_on(music->synth, 0);
                }
            }
        }
    }
}

float background_music_generate_sample(BackgroundMusicSystem* music) {
    if (!music || !music->initialized || !music->enabled) return 0.0f;

    // Update timing
    float delta_time = 1.0f / music->sample_rate;
    synthesizer_update_timing(music->synth, delta_time);

    // Intensity shapes tempo and melody presence (subtle ambient ↔ intense combat)
    float intensity = music->intensity;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    music->tempo = music->ambient_tempo + (music->combat_tempo - music->ambient_tempo) * intensity;
    // Ambient: quiet melody under a soft pad. Combat: melody forward, pad recessed.
    float melody_gain = 0.28f + 0.72f * intensity;
    float pad_gain = 0.55f * (1.0f - 0.75f * intensity);

    // Update note timer
    music->note_timer += delta_time;

    // Calculate musical timeline
    music->seconds_per_beat = 60.0f / music->tempo;
    float seconds_per_bar = music->seconds_per_beat * music->time_signature_numerator;
    music->bar_duration = seconds_per_bar;
    music->bar_time += delta_time;

    if (music->bar_time >= music->bar_duration) {
        music->bar_time -= music->bar_duration;
        music->bar_index = (music->bar_index + 1) % music->bars_in_section;
        if (music->bar_index == 0) {
            music->section_index = (music->section_index + 1) % music->num_sections;
        }
        // Thematic progression per bar (introductory style aligned with title tone)
        // Key center transposes each section (12-semitone cycle)
        float section_shift_semitones = (float)(music->section_index % 12);
        float key_ratio = powf(2.0f, section_shift_semitones / 12.0f);
        float base_f = 92.50f * key_ratio; // F#2 transposed

        // Initialize sustained pad (disabled unless enabled explicitly)
        if (!music->theme_initialized && music->pad_enabled) {
            for (int i = 0; i < 4; i++) {
                int ch = music->pad_channels[i];
                if (ch >= 0) {
                    synthesizer_set_channel_frequency(music->synth, ch, base_f * (i + 1));
                    synthesizer_set_channel_amplitude(music->synth, ch,
                                                     music->music_volume * pad_gain *
                                                         (0.20f - 0.04f * i));
                    synthesizer_note_on(music->synth, ch);
                }
            }
            music->theme_initialized = true;
        }
    }

    // Within-bar shaping for pad (only if enabled)
    float bar_pos = music->bar_time / fmaxf(music->bar_duration, 0.001f); // 0..1
    int bar = music->bar_index;
    // Measure plan: bar 0 hold (true hold ~2 seconds), bar 1 build, bar 2 crescendo, bar 3 resolve + accent
    float base_amp[4] = {0.22f, 0.18f, 0.14f, 0.10f};
    if (music->pad_enabled) for (int i = 0; i < 4; i++) {
        int ch = music->pad_channels[i];
        if (ch < 0) continue;
        float a = base_amp[i] * pad_gain;
        switch (bar) {
            case 0: // Hold: keep steady amplitude throughout bar
                // slight onset swell only during first 10% to avoid clicks, then hold
                if (bar_pos < 0.1f) a *= (0.5f + 0.5f * (bar_pos / 0.1f));
                break;
            case 1: // Build: bring in upper partials more
                if (i == 0) a *= 0.9f; else a *= (0.6f + 0.4f * bar_pos);
                break;
            case 2: // Crescendo: all increase, but avoid clipping
                a *= (0.8f + 0.4f * bar_pos);
                break;
            case 3: // Resolve: keep steady pad to support accent
                a *= 0.9f;
                break;
        }
        synthesizer_set_channel_amplitude(music->synth, ch, music->music_volume * a);
    }

    // Final bang at the end of bar 3, briefly — only when combat is heating up
    if (music->pad_enabled && intensity > 0.45f && music->bar_index == 3 && bar_pos > 0.95f &&
        !music->accent_active) {
        if (music->accent_channel >= 0) {
            synthesizer_set_channel_frequency(music->synth, music->accent_channel, 185.0f * powf(2.0f, (music->section_index % 12) / 12.0f));
            synthesizer_set_channel_amplitude(music->synth, music->accent_channel,
                                             music->music_volume * 0.55f * intensity);
            synthesizer_note_on(music->synth, music->accent_channel);
            music->accent_active = true;
            music->accent_timer = 0.0f;
        }
    }
    if (music->accent_active) {
        music->accent_timer += delta_time;
        if (music->accent_timer >= 0.15f) { // short
            synthesizer_note_off(music->synth, music->accent_channel);
            music->accent_active = false;
        }
    }
    // Advance melody based on source timing (advanced beats -> seconds); otherwise fixed seconds
    if (music->current_melody) {
        float target_note_seconds = music->note_duration * (music->ambient_tempo / music->tempo);
        if (music->use_advanced_notes && music->current_melody->advanced_notes[music->current_note_index].note[0] != '\0') {
            AdvancedNote* cn = &music->current_melody->advanced_notes[music->current_note_index];
            target_note_seconds = cn->duration * music->seconds_per_beat;
        }
        if (music->note_timer >= target_note_seconds) {
            music->note_timer = 0.0f;
            if (music->use_advanced_notes) {
                // Walk to next populated advanced note; stop at end then wrap to start
                int next = music->current_note_index + 1;
                while (next < MAX_NOTES_PER_MELODY && music->current_melody->advanced_notes[next].note[0] == '\0') next++;
                if (next >= MAX_NOTES_PER_MELODY) next = 0;
                music->current_note_index = next;
            } else {
                music->current_note_index = (music->current_note_index + 1) % music->current_melody->note_count;
            }
            // Stop current note and start next
            synthesizer_note_off(music->synth, 0);

            if (music->use_advanced_notes && music->current_melody->advanced_notes[music->current_note_index].note[0] != '\0') {
                // Use advanced note
                AdvancedNote* note = &music->current_melody->advanced_notes[music->current_note_index];
                float frequency = note_to_frequency(note->note, music->tuning_scale);

                if (frequency > 0.0f) {
                    synthesizer_set_channel_frequency(music->synth, 0, frequency);
                    synthesizer_set_channel_amplitude(music->synth, 0,
                                                     music->music_volume * melody_gain *
                                                         note->velocity);
                    synthesizer_set_channel_duration(music->synth, 0, note->duration * music->seconds_per_beat);

                    // Apply articulation
                    if (note->accent > 0) {
                        synthesizer_set_channel_accent(music->synth, 0, note->accent);
                    }

                    // Apply syncopation
                    if (note->syncopated) {
                        synthesizer_set_channel_syncopation(music->synth, 0, 0.125f);
                    }

                    // Apply sustain
                    if (note->sustained) {
                        synthesizer_set_channel_sustain(music->synth, 0, note->sustain_level, note->sustain_time);
                    }

                    synthesizer_note_on_advanced(music->synth, 0, note->velocity, note->duration);
                }
            } else if (music->current_melody->note_count > 0) {
                // Use simple note
                const char* note = music->current_melody->notes[music->current_note_index];
                float frequency = note_to_frequency(note, music->tuning_scale);

                if (frequency > 0.0f) {
                    synthesizer_set_channel_frequency(music->synth, 0, frequency);
                    synthesizer_set_channel_amplitude(music->synth, 0,
                                                     music->music_volume * melody_gain);
                    synthesizer_note_on(music->synth, 0);
                }
            }
        }
    }

    // Generate audio sample
    float sample = synthesizer_generate_sample(music->synth);
    return sample * music->master_volume;
}

void background_music_generate_buffer(BackgroundMusicSystem* music, float* buffer, int num_samples) {
    if (!music || !music->initialized || !music->enabled) {
        // If disabled or uninitialized, still generate raw synth so UI sounds can pass through separately
        for (int i = 0; i < num_samples; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }

    for (int i = 0; i < num_samples; i++) {
        buffer[i] = background_music_generate_sample(music);
    }
}

// Generate raw synthesizer output without advancing/playing melody.
// Used to ensure UI/menu sounds are audible even when background music is disabled.
void background_music_generate_synth_buffer(BackgroundMusicSystem* music, float* buffer, int num_samples) {
    if (!music || !music->initialized || !music->synth) {
        for (int i = 0; i < num_samples; i++) buffer[i] = 0.0f;
        return;
    }
    for (int i = 0; i < num_samples; i++) {
        buffer[i] = synthesizer_generate_sample(music->synth) * music->master_volume;
    }
}

void background_music_set_enabled(BackgroundMusicSystem* music, bool enabled) {
    if (!music) return;

    music->enabled = enabled;

    if (!enabled) {
        synthesizer_note_off(music->synth, 0);
    } else if (music->current_melody && music->current_melody->note_count > 0) {
        // Resume playing
        if (music->use_advanced_notes && music->current_melody->advanced_notes[music->current_note_index].note[0] != '\0') {
            AdvancedNote* note = &music->current_melody->advanced_notes[music->current_note_index];
            float frequency = note_to_frequency(note->note, music->tuning_scale);

            if (frequency > 0.0f) {
                synthesizer_set_channel_frequency(music->synth, 0, frequency);
                synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume * note->velocity);
                synthesizer_note_on_advanced(music->synth, 0, note->velocity, note->duration);
            }
        } else {
            const char* note = music->current_melody->notes[music->current_note_index];
            float frequency = note_to_frequency(note, music->tuning_scale);

            if (frequency > 0.0f) {
                synthesizer_set_channel_frequency(music->synth, 0, frequency);
                synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume);
                synthesizer_note_on(music->synth, 0);
            }
        }
    }
}

bool background_music_is_enabled(BackgroundMusicSystem* music) {
    return music ? music->enabled : false;
}

void background_music_set_tuning_scale(BackgroundMusicSystem* music, float tuning_scale) {
    if (!music) return;

    music->tuning_scale = tuning_scale;

    // Update current note if playing
    if (music->enabled && music->current_melody && music->current_melody->note_count > 0) {
        if (music->use_advanced_notes && music->current_melody->advanced_notes[music->current_note_index].note[0] != '\0') {
            AdvancedNote* note = &music->current_melody->advanced_notes[music->current_note_index];
            float frequency = note_to_frequency(note->note, music->tuning_scale);

            if (frequency > 0.0f) {
                synthesizer_set_channel_frequency(music->synth, 0, frequency);
                synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume * note->velocity);
                synthesizer_note_on_advanced(music->synth, 0, note->velocity, note->duration);
            }
        } else {
            const char* note = music->current_melody->notes[music->current_note_index];
            float frequency = note_to_frequency(note, music->tuning_scale);

            if (frequency > 0.0f) {
                synthesizer_set_channel_frequency(music->synth, 0, frequency);
                synthesizer_set_channel_amplitude(music->synth, 0, music->music_volume);
                synthesizer_note_on(music->synth, 0);
            }
        }
    }
}

float background_music_get_tuning_scale(BackgroundMusicSystem* music) {
    return music ? music->tuning_scale : 440.0f;
}

bool background_music_is_initialized(BackgroundMusicSystem* music) {
    return music ? music->initialized : false;
}

// Advanced feature setters
void background_music_set_syncopation(BackgroundMusicSystem* music, bool enabled) {
    if (!music) return;
    music->syncopation_enabled = enabled;
    if (music->current_melody && enabled) {
        apply_syncopation_to_melody(music->current_melody);
    }
}

void background_music_set_sustain(BackgroundMusicSystem* music, bool enabled) {
    if (!music) return;
    music->sustain_enabled = enabled;
    if (music->current_melody && enabled) {
        apply_sustain_to_melody(music->current_melody);
    }
}

void background_music_set_tempo(BackgroundMusicSystem* music, float bpm) {
    if (!music) return;
    music->tempo = bpm;
    if (music->synth) {
        synthesizer_set_tempo(music->synth, bpm);
    }
}

void background_music_set_beat_pattern(BackgroundMusicSystem* music, int pattern_type) {
    if (!music) return;
    music->beat_pattern_type = pattern_type;
    if (music->current_melody) {
        generate_beat_pattern(music->current_melody, pattern_type);
    }
}

void background_music_print_status(BackgroundMusicSystem* music) {
    if (!music) {
        printf("Background music: Not initialized\n");
        return;
    }

    printf("Background Music Status:\n");
    printf("  Initialized: %s\n", music->initialized ? "Yes" : "No");
    printf("  Enabled: %s\n", music->enabled ? "Yes" : "No");
    printf("  Master Volume: %.1f%%\n", music->master_volume * 100.0f);
    printf("  Music Volume: %.1f%%\n", music->music_volume * 100.0f);
    printf("  Tuning Scale: %.0f Hz\n", music->tuning_scale);
    printf("  Tempo: %.0f BPM\n", music->tempo);
    printf("  Intensity: %.0f%% (target %.0f%%)\n", music->intensity * 100.0f,
           music->target_intensity * 100.0f);
    printf("  Category: %s\n", music->active_category);
    printf("  Syncopation: %s\n", music->syncopation_enabled ? "Enabled" : "Disabled");
    printf("  Sustain: %s\n", music->sustain_enabled ? "Enabled" : "Disabled");
    printf("  Beat Pattern: %d\n", music->beat_pattern_type);

    if (music->current_melody) {
        printf("  Current Melody: %s (%s)\n", music->current_melody->name, music->current_melody->description);
        printf("  Note Index: %d/%d\n", music->current_note_index + 1, music->current_melody->note_count);
        if (music->current_melody->note_count > 0) {
            if (music->use_advanced_notes && music->current_melody->advanced_notes[music->current_note_index].note[0] != '\0') {
                printf("  Current Note: %s (Advanced)\n", music->current_melody->advanced_notes[music->current_note_index].note);
            } else {
                printf("  Current Note: %s\n", music->current_melody->notes[music->current_note_index]);
            }
        }
    } else {
        printf("  Current Melody: None\n");
    }
}

void background_music_set_intensity(BackgroundMusicSystem *music, float intensity)
{
    if (!music)
        return;
    if (intensity < 0.0f)
        intensity = 0.0f;
    if (intensity > 1.0f)
        intensity = 1.0f;
    music->target_intensity = intensity;
}

float background_music_get_intensity(const BackgroundMusicSystem *music)
{
    return music ? music->intensity : 0.0f;
}

void background_music_update(BackgroundMusicSystem *music, float delta_time)
{
    if (!music || !music->initialized || delta_time <= 0.0f)
        return;

    // Fast attack into combat, slow release back to ambient so the bed doesn't flicker.
    const float attack = 2.8f;
    const float release = 0.22f;
    float rate = (music->target_intensity > music->intensity) ? attack : release;
    float t = 1.0f - expf(-rate * delta_time);
    music->intensity += (music->target_intensity - music->intensity) * t;

    // Hysteresis around the ambient ↔ combat category switch.
    const char *desired = "Ambient";
    if (music->intensity >= 0.55f)
        desired = "Combat";
    else if (music->intensity >= 0.28f && strcasecmp(music->active_category, "Combat") == 0)
        desired = "Combat";

    if (strcasecmp(music->active_category, desired) != 0)
        background_music_set_category(music, desired);

    // Keep synthesizer tempo in sync for any consumers that read it.
    if (music->synth)
        synthesizer_set_tempo(music->synth, music->tempo);
}

void background_music_play_ambient(BackgroundMusicSystem *music)
{
    if (!music || !music->initialized)
        return;

    music->target_intensity = 0.0f;
    music->intensity = 0.0f;
    music->tempo = music->ambient_tempo;
    music->theme_initialized = false;
    background_music_set_category(music, "Ambient");
    background_music_start(music);
}
