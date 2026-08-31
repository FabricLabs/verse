#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "src/songwriter/songwriter.h"
#include "src/synthesizer/synthesizer.h"

int main() {
    printf("Simple Songwriter Test\n");
    printf("=====================\n\n");

    // Create songwriter
    Songwriter* writer = songwriter_create(44100);
    if (!writer) {
        printf("Failed to create songwriter\n");
        return 1;
    }

    printf("Songwriter created\n");

    // Create a simple song
    Song* song = songwriter_create_song("Simple Test", "Test", 120);
    if (!song) {
        printf("Failed to create song\n");
        songwriter_destroy(writer);
        return 1;
    }

    printf("Song created\n");

    // Create a track
    Track* track = songwriter_create_track(song, "Piano", 0);
    if (!track) {
        printf("Failed to create track\n");
        songwriter_destroy_song(song);
        songwriter_destroy(writer);
        return 1;
    }

    printf("Track created\n");

    // Add some notes
    printf("Adding notes...\n");
    songwriter_add_note(track, 60, 100, 0, 480);    // C4
    songwriter_add_note(track, 64, 100, 480, 480);  // E4
    songwriter_add_note(track, 67, 100, 960, 480);  // G4
    songwriter_add_note(track, 72, 100, 1440, 480); // C5

    // Add track to song
    if (!songwriter_add_track(song, track)) {
        printf("Failed to add track to song\n");
        songwriter_destroy_track(track);
        songwriter_destroy_song(song);
        songwriter_destroy(writer);
        return 1;
    }

    printf("Track added to song\n");

    // Update song duration
    songwriter_update_song_duration(song);
    printf("Song duration updated: %u ticks\n", song->total_ticks);

    // Load song into songwriter
    if (!songwriter_load_song(writer, song)) {
        printf("Failed to load song\n");
        songwriter_destroy(writer);
        return 1;
    }

    printf("Song loaded\n");

    // Enable looping
    songwriter_set_loop(writer, 0, song->total_ticks);
    printf("Looping enabled\n");

    // Start playback
    songwriter_play(writer);
    printf("Playback started\n");

    // Generate some samples and check if we're getting audio
    printf("\nGenerating audio samples...\n");
    float buffer[44100]; // 1 second of audio
    int silent_samples = 0;
    int active_samples = 0;

    for (int i = 0; i < 44100; i++) {
        buffer[i] = songwriter_generate_sample(writer);
        if (fabsf(buffer[i]) < 0.0001f) {
            silent_samples++;
        } else {
            active_samples++;
        }
    }

    printf("Silent samples: %d\n", silent_samples);
    printf("Active samples: %d\n", active_samples);

    // Calculate RMS
    float rms = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < 44100; i++) {
        rms += buffer[i] * buffer[i];
        float abs_sample = fabsf(buffer[i]);
        if (abs_sample > peak) peak = abs_sample;
    }
    rms = sqrtf(rms / 44100.0f);

    printf("Peak: %.4f\n", peak);
    printf("RMS: %.4f\n", rms);

    // Check current state
    printf("\nCurrent state:\n");
    printf("Is playing: %s\n", songwriter_is_playing(writer) ? "Yes" : "No");
    printf("Current tick: %u\n", songwriter_get_current_tick(writer));
    printf("Current beat: %u\n", songwriter_get_current_beat(writer));
    printf("Current measure: %u\n", songwriter_get_current_measure(writer));

    // Check synthesizer state
    printf("\nSynthesizer state:\n");
    printf("Active channels: %d\n", synthesizer_get_active_channels(writer->synth));
    for (int i = 0; i < 16; i++) {
        if (synthesizer_is_channel_active(writer->synth, i)) {
            printf("Channel %d: active, amplitude: %.4f\n", i,
                   synthesizer_get_channel_amplitude(writer->synth, i));
        }
    }

    // Stop playback
    songwriter_stop(writer);
    printf("\nPlayback stopped\n");

    // Cleanup
    songwriter_destroy(writer);
    printf("Cleanup completed\n");

    return 0;
}
