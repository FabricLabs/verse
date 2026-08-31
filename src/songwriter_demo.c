/*
 * songwriter_demo.c - Simplified songwriter implementation for demo purposes
 *
 * This provides a minimal implementation of the songwriter API without
 * requiring the full synthesizer and effects systems.
 */

#include "songwriter/songwriter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Simplified data structures
struct Song {
    char name[256];
    char author[256];
    uint32_t tempo;
    uint32_t time_signature_numerator;
    uint32_t time_signature_denominator;
    uint32_t track_count;
    Track** tracks;
};

struct Track {
    char name[64];
    uint8_t channel;
    uint32_t event_count;
    NoteEvent* events;
};

struct NoteEvent {
    uint8_t note;
    uint8_t velocity;
    uint32_t start_time;
    uint32_t duration;
    uint8_t channel;
};

struct Songwriter {
    Song* current_song;
    bool is_playing;
    uint32_t current_tick;
    uint32_t sample_rate;
    float master_volume;
};

// Global songwriter instance for demo
static Songwriter* g_demo_songwriter = NULL;

// Demo implementation functions
Songwriter* songwriter_create(uint32_t sample_rate) {
    Songwriter* writer = malloc(sizeof(Songwriter));
    if (!writer) return NULL;

    memset(writer, 0, sizeof(Songwriter));
    writer->sample_rate = sample_rate;
    writer->master_volume = 1.0f;
    writer->is_playing = false;
    writer->current_tick = 0;

    g_demo_songwriter = writer;
    printf("✓ Demo Songwriter created (sample rate: %u)\n", sample_rate);
    return writer;
}

void songwriter_destroy(Songwriter* writer) {
    if (!writer) return;

    if (writer->current_song) {
        songwriter_destroy_song(writer->current_song);
    }

    free(writer);
    if (g_demo_songwriter == writer) {
        g_demo_songwriter = NULL;
    }
    printf("✓ Demo Songwriter destroyed\n");
}

Song* songwriter_create_song(const char* name, const char* author, uint32_t tempo) {
    Song* song = malloc(sizeof(Song));
    if (!song) return NULL;

    memset(song, 0, sizeof(Song));
    strncpy(song->name, name ? name : "Untitled", sizeof(song->name) - 1);
    strncpy(song->author, author ? author : "Unknown", sizeof(song->author) - 1);
    song->tempo = tempo;
    song->time_signature_numerator = 4;
    song->time_signature_denominator = 4;
    song->track_count = 0;
    song->tracks = NULL;

    printf("✓ Demo Song created: %s by %s (tempo: %u)\n", song->name, song->author, tempo);
    return song;
}

void songwriter_destroy_song(Song* song) {
    if (!song) return;

    if (song->tracks) {
        for (uint32_t i = 0; i < song->track_count; i++) {
            if (song->tracks[i]) {
                if (song->tracks[i]->events) {
                    free(song->tracks[i]->events);
                }
                free(song->tracks[i]);
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
    Track** new_tracks = realloc(song->tracks, (song->track_count + 1) * sizeof(Track*));
    if (!new_tracks) return NULL;

    song->tracks = new_tracks;

    // Create new track
    Track* track = malloc(sizeof(Track));
    if (!track) return NULL;

    memset(track, 0, sizeof(Track));
    strncpy(track->name, name, sizeof(track->name) - 1);
    track->channel = channel;
    track->event_count = 0;
    track->events = NULL;

    song->tracks[song->track_count] = track;
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
    event->note = note;
    event->velocity = velocity;
    event->start_time = start_time;
    event->duration = duration;
    event->channel = track->channel;

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

bool songwriter_play(Songwriter* writer) {
    if (!writer) return false;

    writer->is_playing = true;
    printf("▶️ Demo Songwriter playing\n");
    return true;
}

bool songwriter_pause(Songwriter* writer) {
    if (!writer) return false;

    writer->is_playing = false;
    printf("⏸️ Demo Songwriter paused\n");
    return true;
}

bool songwriter_stop(Songwriter* writer) {
    if (!writer) return false;

    writer->is_playing = false;
    writer->current_tick = 0;
    printf("⏹️ Demo Songwriter stopped\n");
    return true;
}

void songwriter_set_tempo(Songwriter* writer, uint32_t tempo) {
    if (!writer) return;

    if (writer->current_song) {
        writer->current_song->tempo = tempo;
    }
    printf("🎵 Demo Tempo set to: %u BPM\n", tempo);
}

void songwriter_set_master_volume(Songwriter* writer, float volume) {
    if (!writer) return;

    writer->master_volume = volume;
    printf("🔊 Demo Master volume set to: %.2f\n", volume);
}

// Stub implementations for other functions
bool songwriter_save_song(Song* song, const char* filename) {
    if (!song || !filename) return false;
    printf("💾 Demo Song saved to: %s\n", filename);
    return true;
}

bool songwriter_load_song_from_file(Songwriter* writer, const char* filename) {
    if (!writer || !filename) return false;
    printf("📁 Demo Song loaded from: %s\n", filename);
    return true;
}

bool songwriter_is_playing(Songwriter* writer) {
    return writer && writer->is_playing;
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

uint32_t songwriter_get_sample_rate(Songwriter* writer) {
    return writer ? writer->sample_rate : 44100;
}

// All other functions are stubs that return success or do nothing
bool songwriter_remove_track(Song* song, uint32_t track_index) {
    if (!song || track_index >= song->track_count) return false;
    printf("➖ Demo Track removed: %d\n", track_index);
    return true;
}

bool songwriter_rename_track(Track* track, const char* name) {
    if (!track || !name) return false;
    strncpy(track->name, name, sizeof(track->name) - 1);
    printf("✏️ Demo Track renamed to: %s\n", name);
    return true;
}

bool songwriter_remove_note(Track* track, uint32_t note_index) {
    if (!track || note_index >= track->event_count) return false;
    printf("🗑️ Demo Note removed: %d\n", note_index);
    return true;
}

bool songwriter_modify_note(Track* track, uint32_t note_index, uint8_t note, uint8_t velocity, uint32_t start_time, uint32_t duration) {
    if (!track || note_index >= track->event_count) return false;
    printf("✏️ Demo Note modified: %d\n", note_index);
    return true;
}

void songwriter_set_track_volume(Track* track, float volume) {
    if (!track) return;
    printf("🔊 Demo Track volume set to: %.2f\n", volume);
}

void songwriter_set_track_pan(Track* track, float pan) {
    if (!track) return;
    printf("🎛️ Demo Track pan set to: %.2f\n", pan);
}

void songwriter_set_track_mute(Track* track, bool mute) {
    if (!track) return;
    printf("🔇 Demo Track mute: %s\n", mute ? "ON" : "OFF");
}

void songwriter_set_track_solo(Track* track, bool solo) {
    if (!track) return;
    printf("🎤 Demo Track solo: %s\n", solo ? "ON" : "OFF");
}

void songwriter_set_track_wave_type(Track* track, uint8_t wave_type) {
    if (!track) return;
    printf("🌊 Demo Track wave type set to: %d\n", wave_type);
}

void songwriter_set_track_filter(Track* track, uint8_t filter_type, float cutoff, float resonance) {
    if (!track) return;
    printf("🎛️ Demo Track filter: type=%d, cutoff=%.2f, resonance=%.2f\n", filter_type, cutoff, resonance);
}

void songwriter_set_track_adsr(Track* track, float attack, float decay, float sustain, float release) {
    if (!track) return;
    printf("🎚️ Demo Track ADSR: A=%.2f, D=%.2f, S=%.2f, R=%.2f\n", attack, decay, sustain, release);
}

void songwriter_record_note_on(Songwriter* writer, uint8_t note, uint8_t velocity) {
    if (!writer) return;
    printf("🎹 Demo Note ON: note=%d, velocity=%d\n", note, velocity);
}

void songwriter_record_note_off(Songwriter* writer, uint8_t note) {
    if (!writer) return;
    printf("🎹 Demo Note OFF: note=%d\n", note);
}

void songwriter_start_recording(Songwriter* writer) {
    if (!writer) return;
    printf("⏺️ Demo Recording started\n");
}

void songwriter_stop_recording(Songwriter* writer) {
    if (!writer) return;
    printf("⏹️ Demo Recording stopped\n");
}

bool songwriter_is_recording(Songwriter* writer) {
    return false; // Demo doesn't support recording
}

void songwriter_set_loop(Songwriter* writer, uint32_t start_tick, uint32_t end_tick) {
    if (!writer) return;
    printf("🔄 Demo Loop set: %u-%u\n", start_tick, end_tick);
}

void songwriter_set_loop_enabled(Songwriter* writer, bool enabled) {
    if (!writer) return;
    printf("🔄 Demo Loop: %s\n", enabled ? "ON" : "OFF");
}

bool songwriter_is_loop_enabled(Songwriter* writer) {
    return false; // Demo doesn't support looping
}

uint32_t songwriter_get_loop_start(Songwriter* writer) {
    return 0;
}

uint32_t songwriter_get_loop_end(Songwriter* writer) {
    return 0;
}

void songwriter_seek(Songwriter* writer, uint32_t tick) {
    if (!writer) return;
    writer->current_tick = tick;
    printf("⏭️ Demo Seek to: %u\n", tick);
}

void songwriter_set_time_signature(Song* song, uint32_t numerator, uint32_t denominator) {
    if (!song) return;
    song->time_signature_numerator = numerator;
    song->time_signature_denominator = denominator;
    printf("🎼 Demo Time signature: %u/%u\n", numerator, denominator);
}

uint32_t songwriter_get_time_signature_numerator(Song* song) {
    return song ? song->time_signature_numerator : 4;
}

uint32_t songwriter_get_time_signature_denominator(Song* song) {
    return song ? song->time_signature_denominator : 4;
}

uint32_t songwriter_get_tempo(Song* song) {
    return song ? song->tempo : 120;
}

const char* songwriter_get_song_name(Song* song) {
    return song ? song->name : "Unknown";
}

const char* songwriter_get_song_author(Song* song) {
    return song ? song->author : "Unknown";
}

uint32_t songwriter_get_track_count(Song* song) {
    return song ? song->track_count : 0;
}

Track* songwriter_get_track(Song* song, uint32_t index) {
    if (!song || index >= song->track_count) return NULL;
    return song->tracks[index];
}

const char* songwriter_get_track_name(Track* track) {
    return track ? track->name : "Unknown";
}

uint8_t songwriter_get_track_channel(Track* track) {
    return track ? track->channel : 0;
}

uint32_t songwriter_get_track_event_count(Track* track) {
    return track ? track->event_count : 0;
}

NoteEvent* songwriter_get_track_event(Track* track, uint32_t index) {
    if (!track || index >= track->event_count) return NULL;
    return &track->events[index];
}

uint8_t songwriter_get_event_note(NoteEvent* event) {
    return event ? event->note : 0;
}

uint8_t songwriter_get_event_velocity(NoteEvent* event) {
    return event ? event->velocity : 0;
}

uint32_t songwriter_get_event_start_time(NoteEvent* event) {
    return event ? event->start_time : 0;
}

uint32_t songwriter_get_event_duration(NoteEvent* event) {
    return event ? event->duration : 0;
}

uint8_t songwriter_get_event_channel(NoteEvent* event) {
    return event ? event->channel : 0;
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

void songwriter_print_stats(Songwriter* writer) {
    if (!writer) return;

    printf("📊 Demo Songwriter Stats:\n");
    printf("  - Sample rate: %u Hz\n", writer->sample_rate);
    printf("  - Master volume: %.2f\n", writer->master_volume);
    printf("  - Playing: %s\n", writer->is_playing ? "Yes" : "No");
    printf("  - Current tick: %u\n", writer->current_tick);

    if (writer->current_song) {
        printf("  - Current song: %s by %s\n", writer->current_song->name, writer->current_song->author);
        printf("  - Tempo: %u BPM\n", writer->current_song->tempo);
        printf("  - Time signature: %u/%u\n", writer->current_song->time_signature_numerator, writer->current_song->time_signature_denominator);
        printf("  - Track count: %u\n", writer->current_song->track_count);

        for (uint32_t i = 0; i < writer->current_song->track_count; i++) {
            Track* track = writer->current_song->tracks[i];
            printf("    Track %u: %s (channel %d, %u events)\n", i, track->name, track->channel, track->event_count);
        }
    }
}
