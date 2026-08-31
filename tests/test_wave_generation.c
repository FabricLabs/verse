#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "src/synthesizer/synthesizer.h"

int main() {
    printf("Wave Generation Test\n");
    printf("====================\n\n");

    // Test wave generation functions directly
    printf("Testing wave generation functions:\n");

    // Test with different phase values
    float phases[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    for (int i = 0; i < 5; i++) {
        float phase = phases[i];
        printf("\nPhase: %.2f\n", phase);

        float sine = generate_sine_wave(440.0f, phase, 1.0f);
        float saw = generate_saw_wave(440.0f, phase, 1.0f);
        float square = generate_square_wave(440.0f, phase, 1.0f);
        float triangle = generate_triangle_wave(440.0f, phase, 1.0f);

        printf("  Sine: %.4f\n", sine);
        printf("  Saw: %.4f\n", saw);
        printf("  Square: %.4f\n", square);
        printf("  Triangle: %.4f\n", triangle);
    }

    // Test synthesizer generation
    printf("\nTesting synthesizer generation:\n");

    Synthesizer* synth = synthesizer_create(44100);
    if (!synth) {
        printf("Failed to create synthesizer\n");
        return 1;
    }

    // Set up a channel
    synth->channels[0].frequency = 440.0f;
    synth->channels[0].amplitude = 0.5f;
    synth->channels[0].wave_type = WAVE_SINE;
    synth->channels[0].active = true;
    synth->channels[0].note_on = true;
    synth->channels[0].envelope_level = 1.0f;
    synth->channels[0].phase = 0.0f;

    // Generate a few samples manually
    printf("\nManual phase stepping:\n");
    for (int i = 0; i < 10; i++) {
        float phase = synth->channels[0].phase;
        float wave = generate_sine_wave(synth->channels[0].frequency, phase, 1.0f);
        float sample = wave * 0.5f; // amplitude

        printf("Sample %d: phase=%.4f, wave=%.4f, sample=%.4f\n",
               i, phase, wave, sample);

        // Update phase
        float delta_time = 1.0f / synth->sample_rate;
        float phase_increment = synth->channels[0].frequency * delta_time;
        synth->channels[0].phase += phase_increment;
        if (synth->channels[0].phase >= 1.0f) {
            synth->channels[0].phase -= 1.0f;
        }
    }

    // Reset phase and test actual generation
    printf("\nActual synthesizer generation:\n");
    synth->channels[0].phase = 0.0f;
    synth->channels[0].envelope_time = 0.0f;
    synth->channels[0].attack_time = 0.0f; // Instant attack
    synth->channels[0].decay_time = 0.1f;
    synth->channels[0].sustain_level = 1.0f;
    synth->channels[0].release_time = 0.1f;

    for (int i = 0; i < 10; i++) {
        float sample = synthesizer_generate_sample(synth);
        printf("Sample %d: %.6f (phase: %.4f)\n", i, sample, synth->channels[0].phase);
    }

    synthesizer_destroy(synth);

    return 0;
}
