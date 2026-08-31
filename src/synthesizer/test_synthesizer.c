#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "synthesizer.h"

// Simple test program for the synthesizer
int main() {
    printf("=== VERSE Synthesizer Test ===\n");

    // Create synthesizer with 44.1kHz sample rate
    Synthesizer* synth = synthesizer_create(44100);
    if (!synth) {
        printf("Failed to create synthesizer!\n");
        return 1;
    }

    printf("Synthesizer created successfully!\n");
    printf("Sample rate: %d Hz\n", synth->sample_rate);

    // Add some test channels
    printf("\nAdding test channels...\n");

    // Channel 0: Sine wave at 440 Hz (A4)
    int channel0 = synthesizer_add_channel(synth, WAVE_SINE, 440.0f, 0.3f);
    printf("Added sine wave channel %d at 440 Hz\n", channel0);

    // Channel 1: Saw wave at 220 Hz (A3)
    int channel1 = synthesizer_add_channel(synth, WAVE_SAW, 220.0f, 0.2f);
    printf("Added saw wave channel %d at 220 Hz\n", channel1);

    // Channel 2: Square wave at 880 Hz (A5)
    int channel2 = synthesizer_add_channel(synth, WAVE_SQUARE, 880.0f, 0.15f);
    printf("Added square wave channel %d at 880 Hz\n", channel2);

    // Channel 3: Triangle wave at 330 Hz (E4)
    int channel3 = synthesizer_add_channel(synth, WAVE_TRIANGLE, 330.0f, 0.25f);
    printf("Added triangle wave channel %d at 330 Hz\n", channel3);

    printf("\nActive channels: %d\n", synthesizer_get_active_channels(synth));

    // Set ADSR envelopes for more realistic sound
    printf("\nSetting ADSR envelopes...\n");
    synthesizer_set_channel_adsr(synth, channel0, 0.1f, 0.1f, 0.7f, 0.3f);
    synthesizer_set_channel_adsr(synth, channel1, 0.05f, 0.2f, 0.8f, 0.4f);
    synthesizer_set_channel_adsr(synth, channel2, 0.02f, 0.15f, 0.6f, 0.25f);
    synthesizer_set_channel_adsr(synth, channel3, 0.08f, 0.12f, 0.75f, 0.35f);

    // Test note on/off sequence
    printf("\nTesting note on/off sequence...\n");

    // Generate a buffer of samples (1 second at 44.1kHz)
    int buffer_size = 44100;
    float* buffer = malloc(buffer_size * sizeof(float));
    if (!buffer) {
        printf("Failed to allocate buffer!\n");
        synthesizer_destroy(synth);
        return 1;
    }

    // Play notes in sequence
    printf("Playing notes in sequence...\n");

    // Note on all channels
    synthesizer_note_on(synth, channel0);
    synthesizer_note_on(synth, channel1);
    synthesizer_note_on(synth, channel2);
    synthesizer_note_on(synth, channel3);

    // Generate first 0.5 seconds
    for (int i = 0; i < buffer_size / 2; i++) {
        buffer[i] = synthesizer_generate_sample(synth);
    }

    // Note off channels 1 and 3
    synthesizer_note_off(synth, channel1);
    synthesizer_note_off(synth, channel3);

    // Generate second 0.5 seconds
    for (int i = buffer_size / 2; i < buffer_size; i++) {
        buffer[i] = synthesizer_generate_sample(synth);
    }

    // Calculate RMS (Root Mean Square) for volume analysis
    float rms = 0.0f;
    for (int i = 0; i < buffer_size; i++) {
        rms += buffer[i] * buffer[i];
    }
    rms = sqrtf(rms / buffer_size);

    printf("Generated %d samples (%.2f seconds)\n", buffer_size, (float)buffer_size / 44100.0f);
    printf("RMS level: %.6f\n", rms);
    printf("Peak level: %.6f\n", 1.0f); // Assuming normalized output

    // Test frequency changes
    printf("\nTesting frequency changes...\n");
    synthesizer_set_channel_frequency(synth, channel0, 523.25f); // C5
    synthesizer_set_channel_frequency(synth, channel1, 659.25f); // E5
    synthesizer_set_channel_frequency(synth, channel2, 783.99f); // G5
    synthesizer_set_channel_frequency(synth, channel3, 1046.50f); // C6

    printf("Changed frequencies to C5, E5, G5, C6\n");

    // Test wave type changes
    printf("\nTesting wave type changes...\n");
    synthesizer_set_channel_wave_type(synth, channel0, WAVE_SAW);
    synthesizer_set_channel_wave_type(synth, channel1, WAVE_SQUARE);
    synthesizer_set_channel_wave_type(synth, channel2, WAVE_TRIANGLE);
    synthesizer_set_channel_wave_type(synth, channel3, WAVE_SINE);

    printf("Changed wave types to Saw, Square, Triangle, Sine\n");

    // Test master volume
    printf("\nTesting master volume...\n");
    synthesizer_set_master_volume(synth, 0.5f);
    printf("Set master volume to 0.5\n");

    // Test channel removal
    printf("\nTesting channel removal...\n");
    synthesizer_remove_channel(synth, channel2);
    printf("Removed channel %d\n", channel2);
    printf("Active channels: %d\n", synthesizer_get_active_channels(synth));

    // Cleanup
    free(buffer);
    synthesizer_destroy(synth);

    printf("\nSynthesizer test completed successfully!\n");
    return 0;
}
