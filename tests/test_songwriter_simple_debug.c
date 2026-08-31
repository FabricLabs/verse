#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "src/songwriter/songwriter.h"
#include "src/synthesizer/synthesizer.h"

int main() {
    printf("Simple Debug Test\n");
    printf("=================\n\n");

    // Create songwriter
    Songwriter* writer = songwriter_create(44100);
    if (!writer) {
        printf("Failed to create songwriter\n");
        return 1;
    }

    // Create a simple song
    Song* song = songwriter_create_song("Simple Test", "Test", 120);
    Track* track = songwriter_create_track(song, "Piano", 0);

    // Add a single note
    songwriter_add_note(track, 60, 100, 0, 480); // C4
    songwriter_add_track(song, track);
    songwriter_update_song_duration(song);

    // Load and play
    songwriter_load_song(writer, song);
    songwriter_play(writer);

    // Test the synthesizer directly first
    printf("Direct synthesizer test:\n");
    synthesizer_set_channel_frequency(writer->synth, 0, 261.63f); // C4
    synthesizer_set_channel_amplitude(writer->synth, 0, 0.5f);
    synthesizer_note_on(writer->synth, 0);

    float direct_sample = synthesizer_generate_sample(writer->synth);
    printf("Direct sample: %.6f\n", direct_sample);
    printf("Channel 0 active: %s\n", synthesizer_is_channel_active(writer->synth, 0) ? "Yes" : "No");
    printf("Channel 0 amplitude: %.3f\n", synthesizer_get_channel_amplitude(writer->synth, 0));

    // Turn off the direct note
    synthesizer_note_off(writer->synth, 0);

    printf("\nSongwriter test:\n");

    // Generate a few samples through songwriter
    for (int i = 0; i < 5; i++) {
        float sample = songwriter_generate_sample(writer);

        // Check channel state inside synthesizer
        Channel* channel = &writer->synth->channels[0];
        printf("Sample %d: %.6f, channel active: %d, note_on: %d, freq: %.2f, amp: %.3f, envelope: %.3f\n",
               i, sample, channel->active, channel->note_on, channel->frequency,
               channel->amplitude, channel->envelope_level);
    }

    // Cleanup
    songwriter_destroy(writer);

    return 0;
}
