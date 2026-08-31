#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "src/songwriter/songwriter.h"
#include "src/synthesizer/synthesizer.h"

int main() {
    printf("Debug Songwriter Test\n");
    printf("====================\n\n");

    // Create songwriter
    Songwriter* writer = songwriter_create(44100);
    if (!writer) {
        printf("Failed to create songwriter\n");
        return 1;
    }

    // Create a simple song
    Song* song = songwriter_create_song("Debug Test", "Test", 120);
    Track* track = songwriter_create_track(song, "Piano", 0);

    // Add a single note
    printf("Adding note: C4 (MIDI 60) at tick 0, duration 480\n");
    songwriter_add_note(track, 60, 100, 0, 480);

    // Verify the note was added
    printf("Track event count: %u\n", track->event_count);
    if (track->event_count > 0) {
        NoteEvent* event = &track->events[0];
        printf("Event details:\n");
        printf("  Note: %u\n", event->note);
        printf("  Velocity: %u (%.3f)\n", event->velocity, event->velocity_float);
        printf("  Start time: %u\n", event->start_time);
        printf("  Duration: %u (%.3f)\n", event->duration, event->duration_float);
        printf("  End time: %u\n", event->end_time);
        printf("  Is note on: %s\n", event->is_note_on ? "Yes" : "No");
        printf("  Channel: %u\n", event->channel);
    }

    // Add track to song
    songwriter_add_track(song, track);
    songwriter_update_song_duration(song);

    // Load song
    songwriter_load_song(writer, song);
    songwriter_play(writer);

    // Check initial state
    printf("\nInitial state:\n");
    printf("  Current tick: %u\n", writer->current_tick);
    printf("  Next event tick: %u\n", writer->next_event_tick);
    printf("  Scheduled event count: %u\n", writer->scheduled_event_count);
    printf("  Track event indices[0]: %u\n", writer->track_event_indices[0]);

    // Generate a few samples and check state
    printf("\nGenerating samples and checking state:\n");
    for (int i = 0; i < 10; i++) {
        float sample = songwriter_generate_sample(writer);
        printf("Sample %d: tick=%u, next_event=%u, scheduled=%u, sample=%.6f\n",
               i, writer->current_tick, writer->next_event_tick,
               writer->scheduled_event_count, sample);

        // Check if channel is active
        if (synthesizer_is_channel_active(writer->synth, 0)) {
            printf("  Channel 0 active! Amplitude: %.3f\n",
                   synthesizer_get_channel_amplitude(writer->synth, 0));
        }
    }

    // Try directly triggering a note on the synthesizer
    printf("\nDirect synthesizer test:\n");
    synthesizer_set_channel_frequency(writer->synth, 0, 440.0f);
    synthesizer_set_channel_amplitude(writer->synth, 0, 0.5f);
    synthesizer_note_on(writer->synth, 0);

    float test_buffer[100];
    for (int i = 0; i < 100; i++) {
        test_buffer[i] = synthesizer_generate_sample(writer->synth);
    }

    float test_rms = 0.0f;
    for (int i = 0; i < 100; i++) {
        test_rms += test_buffer[i] * test_buffer[i];
    }
    test_rms = sqrtf(test_rms / 100.0f);
    printf("Direct synthesizer RMS: %.4f\n", test_rms);

    // Cleanup
    songwriter_destroy(writer);

    return 0;
}
