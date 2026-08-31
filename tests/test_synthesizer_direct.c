#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "src/synthesizer/synthesizer.h"

int main() {
    printf("Direct Synthesizer Test\n");
    printf("======================\n\n");

    // Create synthesizer
    Synthesizer* synth = synthesizer_create(44100);
    if (!synth) {
        printf("Failed to create synthesizer\n");
        return 1;
    }

    // Test basic note generation
    printf("Testing basic note generation:\n");

    // Set up channel 0
    synthesizer_set_channel_frequency(synth, 0, 440.0f); // A4
    synthesizer_set_channel_amplitude(synth, 0, 0.5f);
    synthesizer_set_channel_wave_type(synth, 0, WAVE_SINE);

    // Turn on the note
    synthesizer_note_on(synth, 0);

    // Generate some samples
    float buffer[100];
    for (int i = 0; i < 100; i++) {
        buffer[i] = synthesizer_generate_sample(synth);
    }

    // Calculate RMS
    float rms = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < 100; i++) {
        rms += buffer[i] * buffer[i];
        float abs_sample = fabsf(buffer[i]);
        if (abs_sample > peak) peak = abs_sample;
    }
    rms = sqrtf(rms / 100.0f);

    printf("Basic test - Peak: %.4f, RMS: %.4f\n", peak, rms);

    // Test with advanced note on
    printf("\nTesting advanced note generation:\n");

    synthesizer_set_channel_frequency(synth, 1, 880.0f); // A5
    synthesizer_set_channel_amplitude(synth, 1, 0.3f);
    synthesizer_set_channel_wave_type(synth, 1, WAVE_SAW);

    synthesizer_note_on_advanced(synth, 1, 0.8f, 1.0f);

    // Generate more samples
    for (int i = 0; i < 100; i++) {
        buffer[i] = synthesizer_generate_sample(synth);
    }

    // Calculate RMS
    rms = 0.0f;
    peak = 0.0f;
    for (int i = 0; i < 100; i++) {
        rms += buffer[i] * buffer[i];
        float abs_sample = fabsf(buffer[i]);
        if (abs_sample > peak) peak = abs_sample;
    }
    rms = sqrtf(rms / 100.0f);

    printf("Advanced test - Peak: %.4f, RMS: %.4f\n", peak, rms);

    // Check channel states
    printf("\nChannel states:\n");
    for (int i = 0; i < 16; i++) {
        if (synthesizer_is_channel_active(synth, i)) {
            printf("Channel %d: active, amplitude: %.3f\n", i,
                   synthesizer_get_channel_amplitude(synth, i));
        }
    }

    // Test different wave types
    printf("\nTesting different wave types:\n");
    for (int wave = 0; wave < 4; wave++) {
        synthesizer_set_channel_wave_type(synth, 0, wave);

        // Generate samples
        for (int i = 0; i < 100; i++) {
            buffer[i] = synthesizer_generate_sample(synth);
        }

        // Calculate RMS
        rms = 0.0f;
        for (int i = 0; i < 100; i++) {
            rms += buffer[i] * buffer[i];
        }
        rms = sqrtf(rms / 100.0f);

        printf("Wave type %d: RMS: %.4f\n", wave, rms);
    }

    // Cleanup
    synthesizer_destroy(synth);

    return 0;
}
