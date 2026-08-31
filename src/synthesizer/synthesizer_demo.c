#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "synthesizer.h"

// Musical note frequencies (A4 = 440 Hz)
#define C4 261.63f
#define D4 293.66f
#define E4 329.63f
#define F4 349.23f
#define G4 392.00f
#define A4 440.00f
#define B4 493.88f
#define C5 523.25f
#define D5 587.33f
#define E5 659.25f
#define F5 698.46f
#define G5 783.99f
#define A5 880.00f
#define B5 987.77f
#define C6 1046.50f

// Demo functions
void play_chord_progression(Synthesizer* synth);
void play_arpeggio(Synthesizer* synth);
void play_wave_comparison(Synthesizer* synth);
void play_frequency_sweep(Synthesizer* synth);

int main() {
    printf("=== VERSE Synthesizer Demo ===\n");

    // Create synthesizer
    Synthesizer* synth = synthesizer_create(44100);
    if (!synth) {
        printf("Failed to create synthesizer!\n");
        return 1;
    }

    printf("Synthesizer initialized at 44.1kHz\n\n");

    // Demo 1: Chord Progression
    printf("Demo 1: Chord Progression (C major -> F major -> G major -> C major)\n");
    play_chord_progression(synth);

    // Demo 2: Arpeggio
    printf("\nDemo 2: C Major Arpeggio\n");
    play_arpeggio(synth);

    // Demo 3: Wave Comparison
    printf("\nDemo 3: Wave Type Comparison (440 Hz)\n");
    play_wave_comparison(synth);

    // Demo 4: Frequency Sweep
    printf("\nDemo 4: Frequency Sweep (220 Hz to 880 Hz)\n");
    play_frequency_sweep(synth);

    // Cleanup
    synthesizer_destroy(synth);

    printf("\nSynthesizer demo completed!\n");
    return 0;
}

void play_chord_progression(Synthesizer* synth) {
    // Clear any existing channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(synth, i);
    }

    // C major chord (C-E-G)
    int c_chord[3] = {
        synthesizer_add_channel(synth, WAVE_SINE, C4, 0.2f),
        synthesizer_add_channel(synth, WAVE_SINE, E4, 0.2f),
        synthesizer_add_channel(synth, WAVE_SINE, G4, 0.2f)
    };

    // Set ADSR for smooth transitions
    for (int i = 0; i < 3; i++) {
        synthesizer_set_channel_adsr(synth, c_chord[i], 0.1f, 0.1f, 0.8f, 0.3f);
    }

    // Play C major
    printf("Playing C major chord...\n");
    for (int i = 0; i < 3; i++) {
        synthesizer_note_on(synth, c_chord[i]);
    }

    // Generate 1 second of audio
    for (int i = 0; i < 44100; i++) {
        synthesizer_generate_sample(synth);
    }

    // Change to F major (F-A-C)
    printf("Playing F major chord...\n");
    synthesizer_set_channel_frequency(synth, c_chord[0], F4);
    synthesizer_set_channel_frequency(synth, c_chord[1], A4);
    synthesizer_set_channel_frequency(synth, c_chord[2], C5);

    for (int i = 0; i < 44100; i++) {
        synthesizer_generate_sample(synth);
    }

    // Change to G major (G-B-D)
    printf("Playing G major chord...\n");
    synthesizer_set_channel_frequency(synth, c_chord[0], G4);
    synthesizer_set_channel_frequency(synth, c_chord[1], B4);
    synthesizer_set_channel_frequency(synth, c_chord[2], D5);

    for (int i = 0; i < 44100; i++) {
        synthesizer_generate_sample(synth);
    }

    // Return to C major
    printf("Playing C major chord...\n");
    synthesizer_set_channel_frequency(synth, c_chord[0], C4);
    synthesizer_set_channel_frequency(synth, c_chord[1], E4);
    synthesizer_set_channel_frequency(synth, c_chord[2], G4);

    for (int i = 0; i < 44100; i++) {
        synthesizer_generate_sample(synth);
    }

    // Note off
    for (int i = 0; i < 3; i++) {
        synthesizer_note_off(synth, c_chord[i]);
    }

    // Generate release
    for (int i = 0; i < 22050; i++) {
        synthesizer_generate_sample(synth);
    }
}

void play_arpeggio(Synthesizer* synth) {
    // Clear channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(synth, i);
    }

    // C major arpeggio notes
    float notes[] = {C4, E4, G4, C5, G4, E4};
    int num_notes = 6;

    // Create a single channel for the arpeggio
    int channel = synthesizer_add_channel(synth, WAVE_TRIANGLE, notes[0], 0.3f);
    synthesizer_set_channel_adsr(synth, channel, 0.05f, 0.1f, 0.7f, 0.2f);

    // Play each note in sequence
    for (int i = 0; i < num_notes; i++) {
        printf("Playing note %d: %.1f Hz\n", i + 1, notes[i]);
        synthesizer_set_channel_frequency(synth, channel, notes[i]);
        synthesizer_note_on(synth, channel);

        // Generate 0.5 seconds per note
        for (int j = 0; j < 22050; j++) {
            synthesizer_generate_sample(synth);
        }

        synthesizer_note_off(synth, channel);

        // Small gap between notes
        for (int j = 0; j < 2205; j++) {
            synthesizer_generate_sample(synth);
        }
    }
}

void play_wave_comparison(Synthesizer* synth) {
    // Clear channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(synth, i);
    }

    // Create channels for different wave types
    int sine_ch = synthesizer_add_channel(synth, WAVE_SINE, A4, 0.25f);
    int saw_ch = synthesizer_add_channel(synth, WAVE_SAW, A4, 0.25f);
    int square_ch = synthesizer_add_channel(synth, WAVE_SQUARE, A4, 0.25f);
    int triangle_ch = synthesizer_add_channel(synth, WAVE_TRIANGLE, A4, 0.25f);

    // Set ADSR for all channels
    synthesizer_set_channel_adsr(synth, sine_ch, 0.1f, 0.1f, 0.8f, 0.3f);
    synthesizer_set_channel_adsr(synth, saw_ch, 0.1f, 0.1f, 0.8f, 0.3f);
    synthesizer_set_channel_adsr(synth, square_ch, 0.1f, 0.1f, 0.8f, 0.3f);
    synthesizer_set_channel_adsr(synth, triangle_ch, 0.1f, 0.1f, 0.8f, 0.3f);

    // Play each wave type separately
    const char* wave_names[] = {"Sine", "Saw", "Square", "Triangle"};
    int channels[] = {sine_ch, saw_ch, square_ch, triangle_ch};

    for (int w = 0; w < 4; w++) {
        printf("Playing %s wave...\n", wave_names[w]);

        // Note on only this channel
        synthesizer_note_on(synth, channels[w]);

        // Generate 1 second
        for (int i = 0; i < 44100; i++) {
            synthesizer_generate_sample(synth);
        }

        synthesizer_note_off(synth, channels[w]);

        // Small gap
        for (int i = 0; i < 2205; i++) {
            synthesizer_generate_sample(synth);
        }
    }
}

void play_frequency_sweep(Synthesizer* synth) {
    // Clear channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(synth, i);
    }

    // Create a channel for the sweep
    int channel = synthesizer_add_channel(synth, WAVE_SAW, 220.0f, 0.2f);
    synthesizer_set_channel_adsr(synth, channel, 0.05f, 0.1f, 0.8f, 0.2f);

    printf("Sweeping from 220 Hz to 880 Hz...\n");
    synthesizer_note_on(synth, channel);

    // Sweep over 3 seconds
    float start_freq = 220.0f;
    float end_freq = 880.0f;
    int sweep_samples = 44100 * 3; // 3 seconds

    for (int i = 0; i < sweep_samples; i++) {
        // Calculate current frequency (linear sweep)
        float progress = (float)i / sweep_samples;
        float current_freq = start_freq + (end_freq - start_freq) * progress;

        synthesizer_set_channel_frequency(synth, channel, current_freq);
        synthesizer_generate_sample(synth);
    }

    synthesizer_note_off(synth, channel);

    // Generate release
    for (int i = 0; i < 22050; i++) {
        synthesizer_generate_sample(synth);
    }
}
