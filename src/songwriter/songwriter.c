#include "songwriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <jansson.h>

// Note names for display
static const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

Songwriter* songwriter_create(int sample_rate) {
    Songwriter* writer = malloc(sizeof(Songwriter));
    if (!writer) return NULL;

    writer->synth = synthesizer_create(sample_rate);
    if (!writer->synth) {
        free(writer);
        return NULL;
    }

    writer->current_song = NULL;
    writer->current_tick = 0;
    writer->current_beat = 0;
    writer->current_measure = 0;
    writer->is_playing = false;
    writer->is_recording = false;
    writer->playback_speed = 1.0f;
    writer->loop_start = 0;
    writer->loop_end = 0;
    writer->loop_enabled = false;

    // Initialize enhanced sequencer features
    writer->next_event_tick = 0;
    writer->track_event_indices = malloc(sizeof(uint32_t) * 16);
    writer->track_volumes = malloc(sizeof(float) * 16);
    writer->track_pans = malloc(sizeof(float) * 16);
    writer->track_muted = malloc(sizeof(bool) * 16);
    writer->track_soloed = malloc(sizeof(bool) * 16);

    if (!writer->track_event_indices || !writer->track_volumes ||
        !writer->track_pans || !writer->track_muted || !writer->track_soloed) {
        synthesizer_destroy(writer->synth);
        free(writer->track_event_indices);
        free(writer->track_volumes);
        free(writer->track_pans);
        free(writer->track_muted);
        free(writer->track_soloed);
        free(writer);
        return NULL;
    }

    // Initialize track arrays
    for (int i = 0; i < 16; i++) {
        writer->track_event_indices[i] = 0;
        writer->track_volumes[i] = 1.0f;
        writer->track_pans[i] = 0.5f; // Center
        writer->track_muted[i] = false;
        writer->track_soloed[i] = false;
    }

    // Initialize audio quality settings
    writer->sample_rate = sample_rate;
    writer->master_volume = 1.0f;
    writer->auto_normalize = true;
    writer->normalization_factor = 1.0f;
    writer->peak_detector = 0.0f;
    writer->rms_detector = 0.0f;

    // Initialize timing and synchronization
    writer->sample_count = 0;
    writer->time_position = 0.0f;
    writer->beat_position = 0.0f;
    writer->measure_position = 0.0f;
    writer->sync_to_external = false;
    writer->external_tempo = 120.0f;
    writer->external_phase = 0.0f;

    // Initialize event scheduling
    writer->scheduled_event_capacity = 1000;
    writer->scheduled_event_count = 0;
    writer->scheduled_events = malloc(sizeof(NoteEvent*) * writer->scheduled_event_capacity);
    if (!writer->scheduled_events) {
        synthesizer_destroy(writer->synth);
        free(writer->track_event_indices);
        free(writer->track_volumes);
        free(writer->track_pans);
        free(writer->track_muted);
        free(writer->track_soloed);
        free(writer);
        return NULL;
    }

    // Initialize audio buffer management
    writer->audio_buffer_size = SEQUENCER_BUFFER_SIZE;
    writer->audio_buffer_position = 0;
    writer->audio_buffer = malloc(sizeof(float) * writer->audio_buffer_size);
    writer->buffer_underrun = false;
    writer->buffer_overrun = false;

    if (!writer->audio_buffer) {
        synthesizer_destroy(writer->synth);
        free(writer->track_event_indices);
        free(writer->track_volumes);
        free(writer->track_pans);
        free(writer->track_muted);
        free(writer->track_soloed);
        free(writer->scheduled_events);
        free(writer);
        return NULL;
    }

    return writer;
}

void songwriter_destroy(Songwriter* writer) {
    if (!writer) return;

    if (writer->current_song) {
        songwriter_destroy_song(writer->current_song);
    }

    if (writer->synth) {
        synthesizer_destroy(writer->synth);
    }

    // Free enhanced sequencer arrays
    if (writer->track_event_indices) free(writer->track_event_indices);
    if (writer->track_volumes) free(writer->track_volumes);
    if (writer->track_pans) free(writer->track_pans);
    if (writer->track_muted) free(writer->track_muted);
    if (writer->track_soloed) free(writer->track_soloed);
    if (writer->scheduled_events) free(writer->scheduled_events);
    if (writer->audio_buffer) free(writer->audio_buffer);

    free(writer);
}

Song* songwriter_create_song(const char* name, const char* artist, uint32_t tempo) {
    Song* song = malloc(sizeof(Song));
    if (!song) return NULL;

    strncpy(song->name, name ? name : "Untitled", sizeof(song->name) - 1);
    song->name[sizeof(song->name) - 1] = '\0';

    strncpy(song->artist, artist ? artist : "Unknown Artist", sizeof(song->artist) - 1);
    song->artist[sizeof(song->artist) - 1] = '\0';

    strcpy(song->description, "A new song");

    song->tempo = tempo;
    song->tempo_float = (float)tempo;
    song->time_signature.numerator = 4;
    song->time_signature.denominator = 4;
    song->ticks_per_beat = 480; // Standard MIDI resolution
    song->total_ticks = 0;

    // Initialize enhanced features
    song->auto_normalize = true;
    song->master_volume = 1.0f;
    song->master_filter_cutoff = 20000.0f;
    song->master_filter_resonance = 0.0f;
    song->compressor_threshold = 0.8f;
    song->compressor_ratio = 4.0f;
    song->limiter_threshold = 0.95f;
    song->delay_time = 0.0f;
    song->delay_feedback = 0.0f;
    song->delay_mix = 0.0f;
    song->reverb_time = 0.0f;
    song->reverb_mix = 0.0f;

    song->track_capacity = 16;
    song->track_count = 0;
    song->tracks = malloc(sizeof(Track) * song->track_capacity);

    if (!song->tracks) {
        free(song);
        return NULL;
    }

    return song;
}

void songwriter_destroy_song(Song* song) {
    if (!song) return;

    for (uint8_t i = 0; i < song->track_count; i++) {
        songwriter_destroy_track(&song->tracks[i]);
    }

    if (song->tracks) {
        free(song->tracks);
    }

    free(song);
}

bool songwriter_load_song(Songwriter* writer, Song* song) {
    if (!writer || !song) return false;

    if (writer->current_song) {
        songwriter_destroy_song(writer->current_song);
    }

    writer->current_song = song;
    writer->current_tick = 0;
    writer->current_beat = 0;
    writer->current_measure = 0;

    // Reset synthesizer state
    if (writer->synth) {
        for (int i = 0; i < 16; i++) {
            writer->synth->channels[i].active = false;
            writer->synth->channels[i].note_on = false;
            writer->synth->channels[i].envelope_level = 0.0f;
            writer->synth->channels[i].envelope_time = 0.0f;
        }
    }

    return true;
}

Track* songwriter_create_track(Song* song, const char* name, uint8_t channel) {
    Track* track = malloc(sizeof(Track));
    if (!track) return NULL;

    strncpy(track->name, name ? name : "Track", sizeof(track->name) - 1);
    track->name[sizeof(track->name) - 1] = '\0';

    track->channel = channel;
    track->program = 0; // Default to Acoustic Grand Piano
    track->volume = 100;
    track->pan = 64; // Center

    // Initialize enhanced features with better defaults for smoother sound
    track->volume_float = 0.3f; // Lower default volume to prevent harshness
    track->pan_float = 0.5f;
    track->wave_type = 0; // WAVE_SINE
    track->attack_time = 0.1f;  // Smoother attack
    track->decay_time = 0.2f;   // Longer decay
    track->sustain_level = 0.6f; // Lower sustain
    track->release_time = 0.5f;  // Longer release
    track->auto_normalize = true;
    track->compressor_threshold = 0.8f;
    track->compressor_ratio = 4.0f;
    track->limiter_threshold = 0.95f;

    // Initialize polyphony support
    track->polyphony_limit = MAX_POLYPHONY_PER_TRACK;
    track->active_voices = 0;
    track->voice_notes = malloc(sizeof(uint8_t) * MAX_POLYPHONY_PER_TRACK);
    track->voice_velocities = malloc(sizeof(float) * MAX_POLYPHONY_PER_TRACK);
    track->voice_start_times = malloc(sizeof(uint32_t) * MAX_POLYPHONY_PER_TRACK);

    if (!track->voice_notes || !track->voice_velocities || !track->voice_start_times) {
        free(track->voice_notes);
        free(track->voice_velocities);
        free(track->voice_start_times);
        free(track);
        return NULL;
    }

    // Initialize polyphony arrays
    for (int i = 0; i < MAX_POLYPHONY_PER_TRACK; i++) {
        track->voice_notes[i] = 0;
        track->voice_velocities[i] = 0.0f;
        track->voice_start_times[i] = 0;
    }

    track->event_capacity = MAX_EVENTS_PER_TRACK;
    track->event_count = 0;
    track->events = malloc(sizeof(NoteEvent) * track->event_capacity);

    if (!track->events) {
        free(track->voice_notes);
        free(track->voice_velocities);
        free(track->voice_start_times);
        free(track);
        return NULL;
    }

    return track;
}

void songwriter_destroy_track(Track* track) {
    if (!track) return;

    if (track->events) {
        free(track->events);
    }

    if (track->voice_notes) {
        free(track->voice_notes);
    }
    if (track->voice_velocities) {
        free(track->voice_velocities);
    }
    if (track->voice_start_times) {
        free(track->voice_start_times);
    }
}

bool songwriter_add_track(Song* song, Track* track) {
    if (!song || !track || song->track_count >= song->track_capacity) return false;

    // Copy track data properly
    Track* song_track = &song->tracks[song->track_count];
    strncpy(song_track->name, track->name, sizeof(song_track->name) - 1);
    song_track->name[sizeof(song_track->name) - 1] = '\0';

    song_track->channel = track->channel;
    song_track->program = track->program;
    song_track->volume = track->volume;
    song_track->pan = track->pan;

    // Allocate new events array for the song track
    song_track->event_capacity = track->event_capacity;
    song_track->event_count = track->event_count;
    song_track->events = malloc(sizeof(NoteEvent) * song_track->event_capacity);

    if (!song_track->events) {
        return false;
    }

    // Copy events
    for (uint32_t i = 0; i < track->event_count; i++) {
        song_track->events[i] = track->events[i];
    }

    song->track_count++;

    return true;
}

Track* songwriter_get_track(Song* song, uint8_t track_index) {
    if (!song || track_index >= song->track_count) return NULL;
    return &song->tracks[track_index];
}

bool songwriter_add_note(Track* track, uint8_t note, uint8_t velocity, uint32_t start_time, uint32_t duration) {
    if (!track || note > 127 || velocity > MIDI_VELOCITY_MAX ||
        track->event_count >= track->event_capacity) return false;

    NoteEvent* event = &track->events[track->event_count];
    event->channel = track->channel;
    event->note = note;
    event->velocity = velocity;
    event->duration = duration;
    event->start_time = start_time;
    event->end_time = start_time + duration;
    event->is_note_on = true;

    // Initialize enhanced fields
    event->velocity_float = velocity / 127.0f;
    event->duration_float = (float)duration;
    event->articulation = 0;
    event->portamento_time = 0.0f;
    event->lfo_frequency = 0.0f;
    event->lfo_depth = 0.0f;
    event->filter_type = 0;
    event->filter_cutoff = 20000.0f;
    event->filter_resonance = 0.0f;

    track->event_count++;

    // Note: Song's total_ticks will be updated by the song editor when needed

    return true;
}

bool songwriter_remove_note(Track* track, uint32_t event_index) {
    if (!track || event_index >= track->event_count) return false;

    // Shift remaining events
    for (uint32_t i = event_index; i < track->event_count - 1; i++) {
        track->events[i] = track->events[i + 1];
    }

    track->event_count--;
    return true;
}

bool songwriter_modify_note(Track* track, uint32_t event_index, uint8_t note, uint8_t velocity, uint32_t start_time, uint32_t duration) {
    if (!track || event_index >= track->event_count || note > 127 || velocity > MIDI_VELOCITY_MAX) return false;

    NoteEvent* event = &track->events[event_index];
    event->note = note;
    event->velocity = velocity;
    event->duration = duration;
    event->start_time = start_time;
    event->end_time = start_time + duration;

    return true;
}

void songwriter_update_song_duration(Song* song) {
    if (!song) return;

    uint32_t max_end_time = 0;

    for (uint8_t i = 0; i < song->track_count; i++) {
        Track* track = &song->tracks[i];
        for (uint32_t j = 0; j < track->event_count; j++) {
            NoteEvent* event = &track->events[j];
            uint32_t end_time = event->start_time + event->duration;
            if (end_time > max_end_time) {
                max_end_time = end_time;
            }
        }
    }

    song->total_ticks = max_end_time;
}

void songwriter_play(Songwriter* writer) {
    if (!writer || !writer->current_song) return;
    writer->is_playing = true;
}

void songwriter_stop(Songwriter* writer) {
    if (!writer) return;
    writer->is_playing = false;
    writer->current_tick = 0;
    writer->current_beat = 0;
    writer->current_measure = 0;

    // Stop all notes and deactivate all channels
    for (int i = 0; i < 16; i++) {
        synthesizer_note_off(writer->synth, i);
        // Force deactivate the channel
        if (writer->synth && i < 16) {
            writer->synth->channels[i].active = false;
            writer->synth->channels[i].note_on = false;
            writer->synth->channels[i].envelope_level = 0.0f;
        }
    }
}

void songwriter_pause(Songwriter* writer) {
    if (!writer) return;
    writer->is_playing = false;
}

void songwriter_seek(Songwriter* writer, uint32_t tick) {
    if (!writer || !writer->current_song) return;
    writer->current_tick = tick;
    writer->current_beat = ticks_to_beats(tick, writer->current_song->ticks_per_beat);
    writer->current_measure = writer->current_beat / writer->current_song->time_signature.numerator;
}

void songwriter_set_loop(Songwriter* writer, uint32_t start_tick, uint32_t end_tick) {
    if (!writer) return;
    writer->loop_start = start_tick;
    writer->loop_end = end_tick;
    writer->loop_enabled = true;
}

void songwriter_set_playback_speed(Songwriter* writer, float speed) {
    if (!writer) return;
    writer->playback_speed = fmaxf(0.1f, fminf(4.0f, speed));
}

void songwriter_start_recording(Songwriter* writer, Track* track) {
    if (!writer || !track) return;
    writer->is_recording = true;
    writer->current_tick = 0;
}

void songwriter_stop_recording(Songwriter* writer) {
    if (!writer) return;
    writer->is_recording = false;
}

void songwriter_record_note_on(Songwriter* writer, uint8_t note, uint8_t velocity) {
    if (!writer || !writer->is_recording || !writer->current_song) return;

    // Find the first track for recording
    if (writer->current_song->track_count > 0) {
        Track* track = &writer->current_song->tracks[0];
        songwriter_add_note(track, note, velocity, writer->current_tick, 480); // Quarter note duration
    }
}

void songwriter_record_note_off(Songwriter* writer, uint8_t note) {
    // Note off events are handled automatically by duration
    if (!writer) return;
}

// Enhanced audio generation with improved sequencer
float songwriter_generate_sample(Songwriter* writer) {
    if (!writer || !writer->current_song || !writer->is_playing) return 0.0f;

    // Update timing
    float delta_time = 1.0f / writer->sample_rate;
    writer->sample_count++;
    writer->time_position += delta_time;

    // Check if there are any notes in the song
    bool has_notes = false;
    for (uint8_t i = 0; i < writer->current_song->track_count; i++) {
        Track* track = &writer->current_song->tracks[i];
        if (track->event_count > 0) {
            has_notes = true;
            break;
        }
    }

    // If no notes, return silence
    if (!has_notes) return 0.0f;

    // Process scheduled events efficiently
    while (writer->scheduled_event_count > 0 &&
           writer->scheduled_events[0]->start_time <= writer->current_tick) {

        NoteEvent* event = writer->scheduled_events[0];
        Track* track = &writer->current_song->tracks[event->channel];

        if (event->is_note_on) {
            float frequency = midi_note_to_frequency(event->note, 440.0f);
            float velocity = event->velocity_float > 0.0f ? event->velocity_float : (event->velocity / 127.0f);

            // Set synthesizer parameters
            synthesizer_set_channel_frequency(writer->synth, track->channel, frequency);
            synthesizer_set_channel_amplitude(writer->synth, track->channel, velocity);
            synthesizer_set_channel_program(writer->synth, track->channel, track->program);

            // Apply enhanced features
            if (event->articulation > 0) {
                synthesizer_set_channel_articulation(writer->synth, track->channel, event->articulation);
            }
            if (event->portamento_time > 0.0f) {
                synthesizer_set_channel_portamento(writer->synth, track->channel, event->portamento_time);
            }
            if (event->lfo_frequency > 0.0f) {
                synthesizer_set_channel_lfo(writer->synth, track->channel, event->lfo_frequency, event->lfo_depth);
            }
            if (event->filter_type > 0) {
                synthesizer_set_channel_filter(writer->synth, track->channel, event->filter_type,
                                            event->filter_cutoff, event->filter_resonance);
            }

            synthesizer_note_on_advanced(writer->synth, track->channel, velocity, event->duration_float);
        } else {
            synthesizer_note_off_advanced(writer->synth, track->channel);
        }

        // Remove processed event
        writer->scheduled_events[0] = writer->scheduled_events[--writer->scheduled_event_count];
        if (writer->scheduled_event_count > 0) {
            // Bubble down the last event
            NoteEvent* last_event = writer->scheduled_events[writer->scheduled_event_count];
            int j = 0;
            while (j < writer->scheduled_event_count &&
                   writer->scheduled_events[j]->start_time <= last_event->start_time) {
                j++;
            }
            if (j < writer->scheduled_event_count) {
                memmove(&writer->scheduled_events[j+1], &writer->scheduled_events[j],
                       (writer->scheduled_event_count - j) * sizeof(NoteEvent*));
                writer->scheduled_events[j] = last_event;
            }
        }
    }

    // Schedule new events if needed
    if (writer->current_tick >= writer->next_event_tick) {
        for (uint8_t i = 0; i < writer->current_song->track_count; i++) {
            Track* track = &writer->current_song->tracks[i];

            while (writer->track_event_indices[i] < track->event_count) {
                NoteEvent* event = &track->events[writer->track_event_indices[i]];

                if (event->start_time > writer->current_tick) {
                    break;
                }

                // Add to scheduled events
                if (writer->scheduled_event_count < writer->scheduled_event_capacity) {
                    writer->scheduled_events[writer->scheduled_event_count++] = event;

                    // Sort by start time (simple insertion sort for small arrays)
                    for (int j = writer->scheduled_event_count - 1; j > 0; j--) {
                        if (writer->scheduled_events[j]->start_time < writer->scheduled_events[j-1]->start_time) {
                            NoteEvent* temp = writer->scheduled_events[j];
                            writer->scheduled_events[j] = writer->scheduled_events[j-1];
                            writer->scheduled_events[j-1] = temp;
                        } else {
                            break;
                        }
                    }
                }

                writer->track_event_indices[i]++;
            }
        }

        writer->next_event_tick = writer->current_tick + 1;
    }

    // Generate audio from synthesizer
    float sample = synthesizer_generate_sample(writer->synth);

    // Check if any tracks are soloed
    bool any_soloed = false;
    for (uint8_t i = 0; i < writer->current_song->track_count; i++) {
        if (writer->track_soloed[i]) {
            any_soloed = true;
            break;
        }
    }

    // Apply track-specific processing
    float track_mix = 0.0f;
    int active_tracks = 0;
    for (uint8_t i = 0; i < writer->current_song->track_count; i++) {
        Track* track = &writer->current_song->tracks[i];

        // Skip muted tracks
        if (writer->track_muted[i]) continue;

        // If any track is soloed, only play soloed tracks
        if (any_soloed && !writer->track_soloed[i]) continue;

        // Apply track volume and pan (for now just volume)
        float track_gain = writer->track_volumes[i] * track->volume_float;
        track_mix += sample * track_gain;
        active_tracks++;
    }

    // Average the track mix if multiple tracks
    if (active_tracks > 0) {
        sample = track_mix / active_tracks;
    }

    // Apply master effects
    if (writer->current_song) {
        // Apply master filter
        if (writer->current_song->master_filter_cutoff < 20000.0f) {
            sample = apply_filter(sample, writer->current_song->master_filter_cutoff,
                                writer->current_song->master_filter_resonance, FILTER_LOWPASS);
        }

        // Apply master compressor
        sample = apply_compressor(sample, writer->current_song->compressor_threshold,
                                writer->current_song->compressor_ratio, 0.01f, 0.1f);

        // Apply master limiter
        sample = apply_limiter(sample, writer->current_song->limiter_threshold);

        // Apply delay
        if (writer->current_song->delay_time > 0.0f) {
            sample = apply_delay(sample, writer->synth->delay_buffer, writer->synth->delay_buffer_size,
                               &writer->synth->delay_position, writer->current_song->delay_feedback,
                               writer->current_song->delay_mix);
        }

        // Apply reverb
        if (writer->current_song->reverb_time > 0.0f) {
            sample = apply_reverb(sample, writer->synth->reverb_buffer, writer->synth->reverb_buffer_size,
                                &writer->synth->reverb_position, writer->current_song->reverb_time,
                                writer->current_song->reverb_mix);
        }

        // Apply master volume
        sample *= writer->current_song->master_volume;
    }

    // Update audio statistics
    float sample_abs = fabsf(sample);
    if (sample_abs > writer->peak_detector) {
        writer->peak_detector = sample_abs;
    } else {
        writer->peak_detector *= 0.999f;
    }

    writer->rms_detector = writer->rms_detector * 0.999f + sample * sample * 0.001f;

    // Auto-normalize
    if (writer->auto_normalize && writer->peak_detector > 0.0f) {
        float target_peak = 0.8f;
        if (writer->peak_detector > target_peak) {
            writer->normalization_factor = target_peak / writer->peak_detector;
        } else {
            writer->normalization_factor = 1.0f;
        }
        sample *= writer->normalization_factor;
    }

    // Apply master volume
    sample *= writer->master_volume;

    // Advance time
    writer->current_tick++;

    // Handle looping
    if (writer->loop_enabled && writer->current_tick >= writer->loop_end) {
        writer->current_tick = writer->loop_start;
        // Reset event indices for looping
        for (uint8_t i = 0; i < writer->current_song->track_count; i++) {
            writer->track_event_indices[i] = 0;
        }
        writer->scheduled_event_count = 0;
        writer->next_event_tick = writer->current_tick;
    }

    // Update beat and measure
    if (writer->current_song) {
        writer->current_beat = ticks_to_beats(writer->current_tick, writer->current_song->ticks_per_beat);
        writer->current_measure = writer->current_beat / writer->current_song->time_signature.numerator;
        writer->beat_position = (float)writer->current_beat +
                              (float)(writer->current_tick % writer->current_song->ticks_per_beat) /
                              (float)writer->current_song->ticks_per_beat;
        writer->measure_position = (float)writer->current_measure +
                                 (float)(writer->current_beat % writer->current_song->time_signature.numerator) /
                                 (float)writer->current_song->time_signature.numerator;
    }

    return sample;
}

void songwriter_generate_buffer(Songwriter* writer, float* buffer, int num_samples) {
    if (!writer || !buffer) return;

    for (int i = 0; i < num_samples; i++) {
        buffer[i] = songwriter_generate_sample(writer);
    }
}

// Utility functions
float midi_note_to_frequency(uint8_t midi_note, float tuning_scale) {
    if (midi_note > 127) return 0.0f;

    // Convert MIDI note to frequency
    float frequency = 440.0f * powf(2.0f, (midi_note - 69) / 12.0f);

    // Apply tuning scale
    if (tuning_scale != 440.0f) {
        frequency *= tuning_scale / 440.0f;
    }

    return frequency;
}

uint8_t frequency_to_midi_note(float frequency, float tuning_scale) {
    if (frequency <= 0.0f) return 0;

    // Normalize to A4 = 440Hz
    if (tuning_scale != 440.0f) {
        frequency *= 440.0f / tuning_scale;
    }

    // Convert frequency to MIDI note
    float midi_note = 69.0f + 12.0f * log2f(frequency / 440.0f);

    return (uint8_t)roundf(midi_note);
}

uint32_t beats_to_ticks(uint32_t beats, uint32_t ticks_per_beat) {
    return beats * ticks_per_beat;
}

uint32_t ticks_to_beats(uint32_t ticks, uint32_t ticks_per_beat) {
    return ticks / ticks_per_beat;
}

uint32_t time_to_ticks(float time_seconds, uint32_t tempo, uint32_t ticks_per_beat) {
    float beats_per_second = tempo / 60.0f;
    float beats = time_seconds * beats_per_second;
    return (uint32_t)(beats * ticks_per_beat);
}

float ticks_to_time(uint32_t ticks, uint32_t tempo, uint32_t ticks_per_beat) {
    float beats = (float)ticks / ticks_per_beat;
    float beats_per_second = tempo / 60.0f;
    return beats / beats_per_second;
}

// Enhanced utility functions
float time_to_ticks_float(float time_seconds, float tempo, uint32_t ticks_per_beat) {
    float beats_per_second = tempo / 60.0f;
    float beats = time_seconds * beats_per_second;
    return beats * ticks_per_beat;
}

float ticks_to_time_float(uint32_t ticks, float tempo, uint32_t ticks_per_beat) {
    float beats = (float)ticks / (float)ticks_per_beat;
    float beats_per_second = tempo / 60.0f;
    return beats / beats_per_second;
}

float velocity_to_float(uint8_t velocity) {
    return (float)velocity / 127.0f;
}

uint8_t velocity_to_midi(float velocity_float) {
    return (uint8_t)(velocity_float * 127.0f + 0.5f);
}

float normalize_audio(float sample, float target_peak) {
    static float peak_detector = 0.0f;
    static float normalization_factor = 1.0f;

    float sample_abs = fabsf(sample);
    if (sample_abs > peak_detector) {
        peak_detector = sample_abs;
    } else {
        peak_detector *= 0.999f;
    }

    if (peak_detector > target_peak) {
        normalization_factor = target_peak / peak_detector;
    } else {
        normalization_factor = 1.0f;
    }

    return sample * normalization_factor;
}

// Enhanced status functions
float songwriter_get_current_time(Songwriter* writer) {
    if (!writer || !writer->current_song) return 0.0f;
    return ticks_to_time_float(writer->current_tick, writer->current_song->tempo_float,
                              writer->current_song->ticks_per_beat);
}

float songwriter_get_current_beat_float(Songwriter* writer) {
    if (!writer) return 0.0f;
    return writer->beat_position;
}

float songwriter_get_current_measure_float(Songwriter* writer) {
    if (!writer) return 0.0f;
    return writer->measure_position;
}

void songwriter_get_audio_stats(Songwriter* writer, float* peak, float* rms, float* normalization) {
    if (!writer) return;
    if (peak) *peak = writer->peak_detector;
    if (rms) *rms = sqrtf(writer->rms_detector);
    if (normalization) *normalization = writer->normalization_factor;
}

void songwriter_get_timing_info(Songwriter* writer, float* time_pos, float* beat_pos, float* measure_pos) {
    if (!writer) return;
    if (time_pos) *time_pos = writer->time_position;
    if (beat_pos) *beat_pos = writer->beat_position;
    if (measure_pos) *measure_pos = writer->measure_position;
}

void songwriter_get_buffer_status(Songwriter* writer, bool* underrun, bool* overrun, int* buffer_position) {
    if (!writer) return;
    if (underrun) *underrun = writer->buffer_underrun;
    if (overrun) *overrun = writer->buffer_overrun;
    if (buffer_position) *buffer_position = writer->audio_buffer_position;
}

// Enhanced playback control functions
void songwriter_seek_to_time(Songwriter* writer, float time_seconds) {
    if (!writer || !writer->current_song) return;

    uint32_t target_tick = (uint32_t)time_to_ticks_float(time_seconds, writer->current_song->tempo_float,
                                                        writer->current_song->ticks_per_beat);
    songwriter_seek(writer, target_tick);
}

void songwriter_set_tempo(Songwriter* writer, float bpm) {
    if (!writer || !writer->current_song) return;

    writer->current_song->tempo = (uint32_t)bpm;
    writer->current_song->tempo_float = bpm;
    synthesizer_set_tempo(writer->synth, bpm);
}

void songwriter_sync_to_external(Songwriter* writer, float tempo, float phase) {
    if (!writer) return;

    writer->sync_to_external = true;
    writer->external_tempo = tempo;
    writer->external_phase = phase;
}

void songwriter_set_master_volume(Songwriter* writer, float volume) {
    if (!writer) return;
    writer->master_volume = fmaxf(0.0f, fminf(2.0f, volume));
}

void songwriter_set_auto_normalize(Songwriter* writer, bool enabled) {
    if (!writer) return;
    writer->auto_normalize = enabled;
}

// Track control functions
void songwriter_set_track_volume(Songwriter* writer, uint8_t track_index, float volume) {
    if (!writer || track_index >= 16) return;
    writer->track_volumes[track_index] = fmaxf(0.0f, fminf(2.0f, volume));
}

void songwriter_set_track_pan(Songwriter* writer, uint8_t track_index, float pan) {
    if (!writer || track_index >= 16) return;
    writer->track_pans[track_index] = fmaxf(0.0f, fminf(1.0f, pan));
}

void songwriter_mute_track(Songwriter* writer, uint8_t track_index, bool muted) {
    if (!writer || track_index >= 16) return;
    writer->track_muted[track_index] = muted;
}

void songwriter_solo_track(Songwriter* writer, uint8_t track_index, bool soloed) {
    if (!writer || track_index >= 16) return;
    writer->track_soloed[track_index] = soloed;
}

// Audio quality control functions
void songwriter_set_track_wave_type(Songwriter* writer, uint8_t track_index, uint8_t wave_type) {
    if (!writer || !writer->current_song || track_index >= writer->current_song->track_count) return;

    Track* track = &writer->current_song->tracks[track_index];
    track->wave_type = wave_type;
    synthesizer_set_channel_wave_type(writer->synth, track->channel, wave_type);
}

void songwriter_set_track_adsr(Songwriter* writer, uint8_t track_index, float attack, float decay, float sustain, float release) {
    if (!writer || !writer->current_song || track_index >= writer->current_song->track_count) return;

    Track* track = &writer->current_song->tracks[track_index];
    track->attack_time = attack;
    track->decay_time = decay;
    track->sustain_level = sustain;
    track->release_time = release;

    synthesizer_set_channel_adsr(writer->synth, track->channel, attack, decay, sustain, release);
}

void songwriter_set_track_filter(Songwriter* writer, uint8_t track_index, uint8_t filter_type, float cutoff, float resonance) {
    if (!writer || !writer->current_song || track_index >= writer->current_song->track_count) return;

    Track* track = &writer->current_song->tracks[track_index];
    synthesizer_set_channel_filter(writer->synth, track->channel, filter_type, cutoff, resonance);
}

void songwriter_set_track_compressor(Songwriter* writer, uint8_t track_index, float threshold, float ratio) {
    if (!writer || !writer->current_song || track_index >= writer->current_song->track_count) return;

    Track* track = &writer->current_song->tracks[track_index];
    track->compressor_threshold = threshold;
    track->compressor_ratio = ratio;
}

void songwriter_set_track_limiter(Songwriter* writer, uint8_t track_index, float threshold) {
    if (!writer || !writer->current_song || track_index >= writer->current_song->track_count) return;

    Track* track = &writer->current_song->tracks[track_index];
    track->limiter_threshold = threshold;
}

// Enhanced audio generation
void songwriter_generate_buffer_high_quality(Songwriter* writer, float* buffer, int num_samples) {
    if (!writer || !buffer) return;

    for (int i = 0; i < num_samples; i++) {
        buffer[i] = songwriter_generate_sample(writer);

        // Apply high-quality anti-aliasing
        if (i > 0) {
            float prev_sample = buffer[i-1];
            float current_sample = buffer[i];
            float diff = current_sample - prev_sample;

            // Simple anti-aliasing filter
            if (fabsf(diff) > 0.1f) {
                buffer[i] = prev_sample + diff * 0.8f;
            }
        }
    }
}

float songwriter_get_peak_level(Songwriter* writer) {
    if (!writer) return 0.0f;
    return writer->peak_detector;
}

float songwriter_get_rms_level(Songwriter* writer) {
    if (!writer) return 0.0f;
    return sqrtf(writer->rms_detector);
}

// Enhanced note management functions
bool songwriter_add_note_advanced(Track* track, uint8_t note, float velocity, float start_time, float duration,
                                uint8_t articulation, float portamento_time, float lfo_freq, float lfo_depth,
                                uint8_t filter_type, float filter_cutoff, float filter_resonance) {
    if (!track) return false;

    if (track->event_count >= track->event_capacity) {
        // Expand capacity
        uint32_t new_capacity = track->event_capacity * 2;
        NoteEvent* new_events = realloc(track->events, sizeof(NoteEvent) * new_capacity);
        if (!new_events) return false;

        track->events = new_events;
        track->event_capacity = new_capacity;
    }

    NoteEvent* event = &track->events[track->event_count];

    event->channel = track->channel;
    event->note = note;
    event->velocity = velocity_to_midi(velocity);
    event->velocity_float = velocity;
    event->start_time = (uint32_t)start_time;
    event->duration = (uint8_t)duration;
    event->duration_float = duration;
    event->end_time = event->start_time + (uint32_t)duration;
    event->is_note_on = true;

    // Enhanced features
    event->articulation = articulation;
    event->portamento_time = portamento_time;
    event->lfo_frequency = lfo_freq;
    event->lfo_depth = lfo_depth;
    event->filter_type = filter_type;
    event->filter_cutoff = filter_cutoff;
    event->filter_resonance = filter_resonance;

    track->event_count++;
    return true;
}

bool songwriter_modify_note_advanced(Track* track, uint32_t event_index, uint8_t note, float velocity,
                                   float start_time, float duration, uint8_t articulation, float portamento_time,
                                   float lfo_freq, float lfo_depth, uint8_t filter_type, float filter_cutoff,
                                   float filter_resonance) {
    if (!track || event_index >= track->event_count) return false;

    NoteEvent* event = &track->events[event_index];

    event->note = note;
    event->velocity = velocity_to_midi(velocity);
    event->velocity_float = velocity;
    event->start_time = (uint32_t)start_time;
    event->duration = (uint8_t)duration;
    event->duration_float = duration;
    event->end_time = event->start_time + (uint32_t)duration;

    // Enhanced features
    event->articulation = articulation;
    event->portamento_time = portamento_time;
    event->lfo_frequency = lfo_freq;
    event->lfo_depth = lfo_depth;
    event->filter_type = filter_type;
    event->filter_cutoff = filter_cutoff;
    event->filter_resonance = filter_resonance;

    return true;
}

// Status functions
bool songwriter_is_playing(Songwriter* writer) {
    return writer ? writer->is_playing : false;
}

bool songwriter_is_recording(Songwriter* writer) {
    return writer ? writer->is_recording : false;
}

uint32_t songwriter_get_current_tick(Songwriter* writer) {
    return writer ? writer->current_tick : 0;
}

uint32_t songwriter_get_current_beat(Songwriter* writer) {
    return writer ? writer->current_beat : 0;
}

uint32_t songwriter_get_current_measure(Songwriter* writer) {
    return writer ? writer->current_measure : 0;
}

void songwriter_print_song_info(Song* song) {
    if (!song) return;

    printf("Song: %s\n", song->name);
    printf("Artist: %s\n", song->artist);
    printf("Description: %s\n", song->description);
    printf("Tempo: %u BPM\n", song->tempo);
    printf("Time Signature: %u/%u\n", song->time_signature.numerator, song->time_signature.denominator);
    printf("Ticks per beat: %u\n", song->ticks_per_beat);
    printf("Total ticks: %u\n", song->total_ticks);
    printf("Track count: %u\n", song->track_count);
    printf("Master volume: %.2f\n", song->master_volume);
    printf("Auto normalize: %s\n", song->auto_normalize ? "enabled" : "disabled");
    printf("Master filter cutoff: %.1f Hz\n", song->master_filter_cutoff);
    printf("Master filter resonance: %.2f\n", song->master_filter_resonance);
    printf("Compressor threshold: %.2f\n", song->compressor_threshold);
    printf("Compressor ratio: %.1f\n", song->compressor_ratio);
    printf("Limiter threshold: %.2f\n", song->limiter_threshold);
    printf("Delay time: %.2f s\n", song->delay_time);
    printf("Delay feedback: %.2f\n", song->delay_feedback);
    printf("Delay mix: %.2f\n", song->delay_mix);
    printf("Reverb time: %.2f s\n", song->reverb_time);
    printf("Reverb mix: %.2f\n", song->reverb_mix);
    printf("\n");

    for (uint8_t i = 0; i < song->track_count; i++) {
        Track* track = &song->tracks[i];
        printf("Track %u: %s\n", i, track->name);
        printf("  Channel: %u\n", track->channel);
        printf("  Program: %u\n", track->program);
        printf("  Volume: %u (%.2f)\n", track->volume, track->volume_float);
        printf("  Pan: %u (%.2f)\n", track->pan, track->pan_float);
        printf("  Wave type: %u\n", track->wave_type);
        printf("  ADSR: %.3f/%.3f/%.2f/%.3f\n", track->attack_time, track->decay_time, track->sustain_level, track->release_time);
        printf("  Auto normalize: %s\n", track->auto_normalize ? "enabled" : "disabled");
        printf("  Compressor: %.2f/%.1f\n", track->compressor_threshold, track->compressor_ratio);
        printf("  Limiter: %.2f\n", track->limiter_threshold);
        printf("  Polyphony limit: %d\n", track->polyphony_limit);
        printf("  Active voices: %d\n", track->active_voices);
        printf("  Event count: %u\n", track->event_count);
        printf("\n");
    }
}
