/*
 * songwriter_demo_simple.c - Minimal demo implementation for sequencer UI testing
 *
 * This provides a very simple implementation that works with the existing
 * songwriter.h header without conflicts.
 */

#include "songwriter/songwriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Simple demo data structures that match the header
static Songwriter* g_demo_writer = NULL;
static Song* g_demo_song = NULL;

// Demo implementation that matches the actual header signatures
Songwriter* songwriter_create(int sample_rate) {
    Songwriter* writer = malloc(sizeof(Songwriter));
    if (!writer) return NULL;

    memset(writer, 0, sizeof(Songwriter));
    writer->sample_rate = sample_rate;
    writer->master_volume = 1.0f;
    writer->is_playing = false;
    writer->current_tick = 0;

    g_demo_writer = writer;
    printf("✓ Demo Songwriter created (sample rate: %d)\n", sample_rate);
    return writer;
}

void songwriter_destroy(Songwriter* writer) {
    if (!writer) return;

    if (writer->current_song) {
        songwriter_destroy_song(writer->current_song);
    }

    free(writer);
    if (g_demo_writer == writer) {
        g_demo_writer = NULL;
    }
    printf("✓ Demo Songwriter destroyed\n");
}

Song* songwriter_create_song(const char* name, const char* artist, uint32_t tempo) {
    Song* song = malloc(sizeof(Song));
    if (!song) return NULL;

    memset(song, 0, sizeof(Song));
    strncpy(song->name, name ? name : "Untitled", sizeof(song->name) - 1);
    strncpy(song->artist, artist ? artist : "Unknown", sizeof(song->artist) - 1);
    song->tempo = tempo;
    song->time_signature.numerator = 4;
    song->time_signature.denominator = 4;
    song->track_count = 0;
    song->tracks = NULL;

    printf("✓ Demo Song created: %s by %s (tempo: %u)\n", song->name, song->artist, tempo);
    return song;
}

void songwriter_destroy_song(Song* song) {
    if (!song) return;

    if (song->tracks) {
        for (uint8_t i = 0; i < song->track_count; i++) {
            if (song->tracks[i].events) {
                free(song->tracks[i].events);
            }
        }
        free(song->tracks);
    }

    free(song);
    printf("✓ Demo Song destroyed\n");
}

Track* songwriter_create_track(Song* song, const char* name, uint8_t channel) {
    if (!song || !name) return NULL;

    // Reallocate tracks array
    Track* new_tracks = realloc(song->tracks, (song->track_count + 1) * sizeof(Track));
    if (!new_tracks) return NULL;

    song->tracks = new_tracks;

    // Initialize new track
    Track* track = &song->tracks[song->track_count];
    memset(track, 0, sizeof(Track));
    strncpy(track->name, name, sizeof(track->name) - 1);
    track->channel = channel;
    track->event_count = 0;
    track->events = NULL;

    song->track_count++;

    printf("✓ Demo Track created: %s (channel %d)\n", track->name, channel);
    return track;
}

bool songwriter_add_note(Track* track, uint8_t note, uint8_t velocity, uint32_t start_time, uint32_t duration) {
    if (!track) return false;

    // Reallocate events array
    NoteEvent* new_events = realloc(track->events, (track->event_count + 1) * sizeof(NoteEvent));
    if (!new_events) return false;

    track->events = new_events;

    // Add new note event
    NoteEvent* event = &track->events[track->event_count];
    memset(event, 0, sizeof(NoteEvent));
    event->note = note;
    event->velocity = velocity;
    event->start_time = start_time;
    event->duration = duration;
    event->end_time = start_time + duration;
    event->channel = track->channel;
    event->is_note_on = true;

    track->event_count++;

    printf("✓ Demo Note added: note=%d, velocity=%d, time=%u, duration=%u\n",
           note, velocity, start_time, duration);
    return true;
}

bool songwriter_load_song(Songwriter* writer, Song* song) {
    if (!writer || !song) return false;

    if (writer->current_song) {
        songwriter_destroy_song(writer->current_song);
    }

    writer->current_song = song;
    writer->current_tick = 0;

    printf("✓ Demo Song loaded: %s (%d tracks)\n", song->name, song->track_count);
    return true;
}

void songwriter_play(Songwriter* writer) {
    if (!writer) return;

    writer->is_playing = true;
    printf("▶️ Demo Songwriter playing\n");
}

void songwriter_pause(Songwriter* writer) {
    if (!writer) return;

    writer->is_playing = false;
    printf("⏸️ Demo Songwriter paused\n");
}

void songwriter_stop(Songwriter* writer) {
    if (!writer) return;

    writer->is_playing = false;
    writer->current_tick = 0;
    printf("⏹️ Demo Songwriter stopped\n");
}

void songwriter_set_tempo(Songwriter* writer, float bpm) {
    if (!writer) return;

    if (writer->current_song) {
        writer->current_song->tempo = (uint32_t)bpm;
    }
    printf("🎵 Demo Tempo set to: %.1f BPM\n", bpm);
}

void songwriter_set_master_volume(Songwriter* writer, float volume) {
    if (!writer) return;

    writer->master_volume = volume;
    printf("🔊 Demo Master volume set to: %.2f\n", volume);
}

// Stub implementations for all other functions
void songwriter_destroy_track(Track* track) {
    if (!track) return;
    printf("✓ Demo Track destroyed\n");
}

bool songwriter_add_track(Song* song, Track* track) {
    if (!song || !track) return false;
    printf("✓ Demo Track added to song\n");
    return true;
}

Track* songwriter_get_track(Song* song, uint8_t track_index) {
    if (!song || track_index >= song->track_count) return NULL;
    return &song->tracks[track_index];
}

bool songwriter_remove_note(Track* track, uint32_t event_index) {
    if (!track || event_index >= track->event_count) return false;
    printf("🗑️ Demo Note removed: %u\n", event_index);
    return true;
}

bool songwriter_modify_note(Track* track, uint32_t event_index, uint8_t note, uint8_t velocity, uint32_t start_time, uint32_t duration) {
    if (!track || event_index >= track->event_count) return false;
    printf("✏️ Demo Note modified: %u\n", event_index);
    return true;
}

void songwriter_update_song_duration(Song* song) {
    if (!song) return;
    printf("📏 Demo Song duration updated\n");
}

bool songwriter_add_note_advanced(Track* track, uint8_t note, float velocity, float start_time, float duration,
                                uint8_t articulation, float portamento_time, float lfo_freq, float lfo_depth,
                                uint8_t filter_type, float filter_cutoff, float filter_resonance) {
    if (!track) return false;
    printf("🎵 Demo Advanced note added\n");
    return true;
}

bool songwriter_modify_note_advanced(Track* track, uint32_t event_index, uint8_t note, float velocity,
                                   float start_time, float duration, uint8_t articulation, float portamento_time,
                                   float lfo_freq, float lfo_depth, uint8_t filter_type, float filter_cutoff,
                                   float filter_resonance) {
    if (!track || event_index >= track->event_count) return false;
    printf("✏️ Demo Advanced note modified\n");
    return true;
}

void songwriter_seek(Songwriter* writer, uint32_t tick) {
    if (!writer) return;
    writer->current_tick = tick;
    printf("⏭️ Demo Seek to: %u\n", tick);
}

void songwriter_set_loop(Songwriter* writer, uint32_t start_tick, uint32_t end_tick) {
    if (!writer) return;
    writer->loop_start = start_tick;
    writer->loop_end = end_tick;
    printf("🔄 Demo Loop set: %u-%u\n", start_tick, end_tick);
}

void songwriter_set_playback_speed(Songwriter* writer, float speed) {
    if (!writer) return;
    writer->playback_speed = speed;
    printf("⚡ Demo Playback speed: %.2fx\n", speed);
}

void songwriter_seek_to_time(Songwriter* writer, float time_seconds) {
    if (!writer) return;
    printf("⏭️ Demo Seek to time: %.2fs\n", time_seconds);
}

void songwriter_sync_to_external(Songwriter* writer, float tempo, float phase) {
    if (!writer) return;
    printf("🔄 Demo Sync to external: tempo=%.2f, phase=%.2f\n", tempo, phase);
}

void songwriter_set_auto_normalize(Songwriter* writer, bool enabled) {
    if (!writer) return;
    writer->auto_normalize = enabled;
    printf("📏 Demo Auto-normalize: %s\n", enabled ? "ON" : "OFF");
}

void songwriter_start_recording(Songwriter* writer, Track* track) {
    if (!writer) return;
    writer->is_recording = true;
    printf("⏺️ Demo Recording started\n");
}

void songwriter_stop_recording(Songwriter* writer) {
    if (!writer) return;
    writer->is_recording = false;
    printf("⏹️ Demo Recording stopped\n");
}

void songwriter_record_note_on(Songwriter* writer, uint8_t note, uint8_t velocity) {
    if (!writer) return;
    printf("🎹 Demo Note ON: note=%d, velocity=%d\n", note, velocity);
}

void songwriter_record_note_off(Songwriter* writer, uint8_t note) {
    if (!writer) return;
    printf("🎹 Demo Note OFF: note=%d\n", note);
}

bool songwriter_save_song(Song* song, const char* filename) {
    if (!song || !filename) return false;
    printf("💾 Demo Song saved to: %s\n", filename);
    return true;
}

Song* songwriter_load_song_from_file(const char* filename) {
    if (!filename) return NULL;
    printf("📁 Demo Song loaded from: %s\n", filename);
    return NULL; // Demo doesn't actually load files
}

// All other functions are minimal stubs
void songwriter_set_track_volume(Songwriter* writer, uint8_t track_index, float volume) {
    if (!writer) return;
    printf("🔊 Demo Track %d volume: %.2f\n", track_index, volume);
}

void songwriter_set_track_pan(Songwriter* writer, uint8_t track_index, float pan) {
    if (!writer) return;
    printf("🎛️ Demo Track %d pan: %.2f\n", track_index, pan);
}

void songwriter_set_track_mute(Songwriter* writer, uint8_t track_index, bool mute) {
    if (!writer) return;
    printf("🔇 Demo Track %d mute: %s\n", track_index, mute ? "ON" : "OFF");
}

void songwriter_set_track_solo(Songwriter* writer, uint8_t track_index, bool solo) {
    if (!writer) return;
    printf("🎤 Demo Track %d solo: %s\n", track_index, solo ? "ON" : "OFF");
}

void songwriter_set_track_wave_type(Songwriter* writer, uint8_t track_index, uint8_t wave_type) {
    if (!writer) return;
    printf("🌊 Demo Track %d wave type: %d\n", track_index, wave_type);
}

void songwriter_set_track_filter(Songwriter* writer, uint8_t track_index, uint8_t filter_type, float cutoff, float resonance) {
    if (!writer) return;
    printf("🎛️ Demo Track %d filter: type=%d, cutoff=%.2f, resonance=%.2f\n", track_index, filter_type, cutoff, resonance);
}

void songwriter_set_track_adsr(Songwriter* writer, uint8_t track_index, float attack, float decay, float sustain, float release) {
    if (!writer) return;
    printf("🎚️ Demo Track %d ADSR: A=%.2f, D=%.2f, S=%.2f, R=%.2f\n", track_index, attack, decay, sustain, release);
}

void songwriter_set_track_compressor(Songwriter* writer, uint8_t track_index, float threshold, float ratio) {
    if (!writer) return;
    printf("🗜️ Demo Track %d compressor: threshold=%.2f, ratio=%.2f\n", track_index, threshold, ratio);
}

void songwriter_set_track_limiter(Songwriter* writer, uint8_t track_index, float threshold) {
    if (!writer) return;
    printf("🚫 Demo Track %d limiter: threshold=%.2f\n", track_index, threshold);
}

void songwriter_set_master_filter(Songwriter* writer, uint8_t filter_type, float cutoff, float resonance) {
    if (!writer) return;
    printf("🎛️ Demo Master filter: type=%d, cutoff=%.2f, resonance=%.2f\n", filter_type, cutoff, resonance);
}

void songwriter_set_master_compressor(Songwriter* writer, float threshold, float ratio) {
    if (!writer) return;
    printf("🗜️ Demo Master compressor: threshold=%.2f, ratio=%.2f\n", threshold, ratio);
}

void songwriter_set_master_limiter(Songwriter* writer, float threshold) {
    if (!writer) return;
    printf("🚫 Demo Master limiter: threshold=%.2f\n", threshold);
}

void songwriter_set_delay(Songwriter* writer, float time, float feedback, float mix) {
    if (!writer) return;
    printf("🔄 Demo Delay: time=%.2f, feedback=%.2f, mix=%.2f\n", time, feedback, mix);
}

void songwriter_set_reverb(Songwriter* writer, float time, float mix) {
    if (!writer) return;
    printf("🌊 Demo Reverb: time=%.2f, mix=%.2f\n", time, mix);
}

// Audio generation stub
float songwriter_generate_sample(Songwriter* writer) {
    if (!writer || !writer->is_playing) return 0.0f;

    // Simple demo: generate a sine wave based on current tick
    static float phase = 0.0f;
    float frequency = 440.0f; // A4
    float sample = 0.1f * sinf(phase);
    phase += 2.0f * M_PI * frequency / writer->sample_rate;
    if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;

    return sample * writer->master_volume;
}

void songwriter_generate_audio(Songwriter* writer, float* buffer, uint32_t sample_count) {
    if (!writer || !buffer) return;

    for (uint32_t i = 0; i < sample_count; i++) {
        buffer[i] = songwriter_generate_sample(writer);
    }
}

// Query functions
bool songwriter_is_playing(Songwriter* writer) {
    return writer && writer->is_playing;
}

bool songwriter_is_recording(Songwriter* writer) {
    return writer && writer->is_recording;
}

uint32_t songwriter_get_current_tick(Songwriter* writer) {
    return writer ? writer->current_tick : 0;
}

Song* songwriter_get_current_song(Songwriter* writer) {
    return writer ? writer->current_song : NULL;
}

float songwriter_get_master_volume(Songwriter* writer) {
    return writer ? writer->master_volume : 0.0f;
}

int songwriter_get_sample_rate(Songwriter* writer) {
    return writer ? writer->sample_rate : 44100;
}

void songwriter_print_stats(Songwriter* writer) {
    if (!writer) return;

    printf("📊 Demo Songwriter Stats:\n");
    printf("  - Sample rate: %d Hz\n", writer->sample_rate);
    printf("  - Master volume: %.2f\n", writer->master_volume);
    printf("  - Playing: %s\n", writer->is_playing ? "Yes" : "No");
    printf("  - Recording: %s\n", writer->is_recording ? "Yes" : "No");
    printf("  - Current tick: %u\n", writer->current_tick);
    printf("  - Playback speed: %.2fx\n", writer->playback_speed);
    printf("  - Loop: %s (%u-%u)\n", writer->loop_enabled ? "ON" : "OFF", writer->loop_start, writer->loop_end);

    if (writer->current_song) {
        printf("  - Current song: %s by %s\n", writer->current_song->name, writer->current_song->artist);
        printf("  - Tempo: %u BPM\n", writer->current_song->tempo);
        printf("  - Time signature: %d/%d\n", writer->current_song->time_signature.numerator, writer->current_song->time_signature.denominator);
        printf("  - Track count: %d\n", writer->current_song->track_count);

        for (uint8_t i = 0; i < writer->current_song->track_count; i++) {
            Track* track = &writer->current_song->tracks[i];
            printf("    Track %d: %s (channel %d, %u events)\n", i, track->name, track->channel, track->event_count);
        }
    }
}
