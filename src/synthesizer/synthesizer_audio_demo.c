#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <SDL2/SDL.h>
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

// Audio system
#define SAMPLE_RATE 44100
#define BUFFER_SIZE 1024

typedef struct {
    SDL_AudioDeviceID device;
    Synthesizer* synth;
    bool initialized;
} AudioSystem;

// Audio callback function
void audio_callback(void* userdata, Uint8* stream, int len) {
    AudioSystem* audio = (AudioSystem*)userdata;
    if (!audio || !audio->synth) return;

    // Convert buffer size from bytes to samples
    int num_samples = len / sizeof(float);
    float* float_stream = (float*)stream;

    // Generate audio samples
    for (int i = 0; i < num_samples; i++) {
        float sample = synthesizer_generate_sample(audio->synth);
        float_stream[i] = sample;
    }
}

// Initialize audio system
AudioSystem* audio_system_create() {
    AudioSystem* audio = malloc(sizeof(AudioSystem));
    if (!audio) return NULL;

    // Initialize SDL audio
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL audio initialization failed: %s\n", SDL_GetError());
        free(audio);
        return NULL;
    }

    // Create synthesizer
    audio->synth = synthesizer_create(SAMPLE_RATE);
    if (!audio->synth) {
        printf("Failed to create synthesizer\n");
        SDL_Quit();
        free(audio);
        return NULL;
    }

    // Configure audio specification
    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_F32;
    desired.channels = 1;
    desired.samples = BUFFER_SIZE;
    desired.callback = audio_callback;
    desired.userdata = audio;

    // Open audio device
    audio->device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (audio->device == 0) {
        printf("Failed to open audio device: %s\n", SDL_GetError());
        synthesizer_destroy(audio->synth);
        SDL_Quit();
        free(audio);
        return NULL;
    }

    audio->initialized = true;
    printf("Audio system initialized successfully\n");
    printf("Sample rate: %d Hz\n", obtained.freq);
    printf("Buffer size: %d samples\n", obtained.samples);

    return audio;
}

// Destroy audio system
void audio_system_destroy(AudioSystem* audio) {
    if (!audio) return;

    if (audio->initialized) {
        SDL_CloseAudioDevice(audio->device);
    }

    if (audio->synth) {
        synthesizer_destroy(audio->synth);
    }

    SDL_Quit();
    free(audio);
}

// Start audio playback
void audio_system_start(AudioSystem* audio) {
    if (!audio || !audio->initialized) return;
    SDL_PauseAudioDevice(audio->device, 0);
    printf("Audio playback started\n");
}

// Stop audio playback
void audio_system_stop(AudioSystem* audio) {
    if (!audio || !audio->initialized) return;
    SDL_PauseAudioDevice(audio->device, 1);
    printf("Audio playback stopped\n");
}

// Original demo functions
void play_chord_progression(AudioSystem* audio);
void play_arpeggio(AudioSystem* audio);
void play_wave_comparison(AudioSystem* audio);
void play_frequency_sweep(AudioSystem* audio);

// New layered effects tests
void play_note_frequency_sweep(AudioSystem* audio, float start_freq, float end_freq, WaveType wave_type, const char* note_name);
void play_all_notes_sweep(AudioSystem* audio, WaveType wave_type);
void play_all_waves_sweep(AudioSystem* audio);
void play_effects_layered_test(AudioSystem* audio);

int main() {
    printf("=== VERSE Enhanced Synthesizer Audio Demo ===\n");

    // Create audio system
    AudioSystem* audio = audio_system_create();
    if (!audio) {
        printf("Failed to create audio system!\n");
        return 1;
    }

    // Start audio playback
    audio_system_start(audio);

    printf("Synthesizer initialized at 44.1kHz\n\n");

    // Original demos
    printf("=== ORIGINAL DEMOS ===\n");

    printf("Demo 1: Chord Progression (C major -> F major -> G major -> C major)\n");
    play_chord_progression(audio);
    SDL_Delay(2000);

    printf("\nDemo 2: C Major Arpeggio\n");
    play_arpeggio(audio);
    SDL_Delay(2000);

    printf("\nDemo 3: Wave Type Comparison (440 Hz)\n");
    play_wave_comparison(audio);
    SDL_Delay(2000);

    printf("\nDemo 4: Frequency Sweep (220 Hz to 880 Hz)\n");
    play_frequency_sweep(audio);
    SDL_Delay(2000);

    // New layered effects tests
    printf("\n=== LAYERED EFFECTS TESTS ===\n");

    printf("\nTest 1: A4 to A5 frequency sweep (sine wave)\n");
    play_note_frequency_sweep(audio, A4, A5, WAVE_SINE, "A");
    SDL_Delay(1000);

    printf("\nTest 2: B4 to B5 frequency sweep (sine wave)\n");
    play_note_frequency_sweep(audio, B4, B5, WAVE_SINE, "B");
    SDL_Delay(1000);

    printf("\nTest 3: C5 to C6 frequency sweep (sine wave)\n");
    play_note_frequency_sweep(audio, C5, C6, WAVE_SINE, "C");
    SDL_Delay(1000);

    printf("\nTest 4: D5 to D6 frequency sweep (sine wave)\n");
    play_note_frequency_sweep(audio, D5, D5 * 2, WAVE_SINE, "D");
    SDL_Delay(1000);

    printf("\nTest 5: E5 to E6 frequency sweep (sine wave)\n");
    play_note_frequency_sweep(audio, E5, E5 * 2, WAVE_SINE, "E");
    SDL_Delay(1000);

    printf("\nTest 6: F5 to F6 frequency sweep (sine wave)\n");
    play_note_frequency_sweep(audio, F5, F5 * 2, WAVE_SINE, "F");
    SDL_Delay(1000);

    printf("\nTest 7: G5 to G6 frequency sweep (sine wave)\n");
    play_note_frequency_sweep(audio, G5, G5 * 2, WAVE_SINE, "G");
    SDL_Delay(1000);

    printf("\nTest 8: All notes sweep (sine wave)\n");
    play_all_notes_sweep(audio, WAVE_SINE);
    SDL_Delay(2000);

    printf("\nTest 9: All notes sweep (saw wave)\n");
    play_all_notes_sweep(audio, WAVE_SAW);
    SDL_Delay(2000);

    printf("\nTest 10: All notes sweep (square wave)\n");
    play_all_notes_sweep(audio, WAVE_SQUARE);
    SDL_Delay(2000);

    printf("\nTest 11: All notes sweep (triangle wave)\n");
    play_all_notes_sweep(audio, WAVE_TRIANGLE);
    SDL_Delay(2000);

    printf("\nTest 12: All wave types sweep\n");
    play_all_waves_sweep(audio);
    SDL_Delay(2000);

    printf("\nTest 13: Layered effects test\n");
    play_effects_layered_test(audio);
    SDL_Delay(2000);

    // Finale: Creative combination
    printf("\n=== FINALE: CREATIVE COMBINATION ===\n");
    printf("Combining original demos with layered effects...\n");

    // Complex finale combining multiple techniques
    for (int wave = 0; wave < 4; wave++) {
        WaveType wave_types[] = {WAVE_SINE, WAVE_SAW, WAVE_SQUARE, WAVE_TRIANGLE};
        const char* wave_names[] = {"Sine", "Saw", "Square", "Triangle"};

        printf("Finale %s wave section...\n", wave_names[wave]);

        // Create multiple channels for complex harmony
        int channels[4];
        for (int i = 0; i < 4; i++) {
            channels[i] = synthesizer_add_channel(audio->synth, wave_types[wave],
                                                A4 * (i + 1), 0.15f);
            synthesizer_set_channel_adsr(audio->synth, channels[i], 0.05f, 0.1f, 0.7f, 0.2f);
        }

        // Play complex chord progression
        for (int i = 0; i < 4; i++) {
            synthesizer_note_on(audio->synth, channels[i]);
        }

        SDL_Delay(1000);

        // Sweep frequencies
        for (int step = 0; step < 12; step++) {
            float freq_multiplier = 1.0f + (step * 0.1f);
            for (int i = 0; i < 4; i++) {
                synthesizer_set_channel_frequency(audio->synth, channels[i],
                                                A4 * (i + 1) * freq_multiplier);
            }
            SDL_Delay(200);
        }

        // Note off
        for (int i = 0; i < 4; i++) {
            synthesizer_note_off(audio->synth, channels[i]);
        }

        SDL_Delay(500);

        // Clean up channels
        for (int i = 0; i < 4; i++) {
            synthesizer_remove_channel(audio->synth, channels[i]);
        }
    }

    // Stop audio and cleanup
    audio_system_stop(audio);
    audio_system_destroy(audio);

    printf("\n🎵 Enhanced synthesizer demo completed!\n");
    return 0;
}

// Original demo functions (adapted for audio output)
void play_chord_progression(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    // Clear any existing channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(audio->synth, i);
    }

    // C major chord (C-E-G)
    int c_chord[3] = {
        synthesizer_add_channel(audio->synth, WAVE_SINE, C4, 0.2f),
        synthesizer_add_channel(audio->synth, WAVE_SINE, E4, 0.2f),
        synthesizer_add_channel(audio->synth, WAVE_SINE, G4, 0.2f)
    };

    // Set ADSR for smooth transitions
    for (int i = 0; i < 3; i++) {
        synthesizer_set_channel_adsr(audio->synth, c_chord[i], 0.1f, 0.1f, 0.8f, 0.3f);
    }

    // Play C major
    printf("Playing C major chord...\n");
    for (int i = 0; i < 3; i++) {
        synthesizer_note_on(audio->synth, c_chord[i]);
    }
    SDL_Delay(1000);

    // Change to F major (F-A-C)
    printf("Playing F major chord...\n");
    synthesizer_set_channel_frequency(audio->synth, c_chord[0], F4);
    synthesizer_set_channel_frequency(audio->synth, c_chord[1], A4);
    synthesizer_set_channel_frequency(audio->synth, c_chord[2], C5);
    SDL_Delay(1000);

    // Change to G major (G-B-D)
    printf("Playing G major chord...\n");
    synthesizer_set_channel_frequency(audio->synth, c_chord[0], G4);
    synthesizer_set_channel_frequency(audio->synth, c_chord[1], B4);
    synthesizer_set_channel_frequency(audio->synth, c_chord[2], D5);
    SDL_Delay(1000);

    // Return to C major
    printf("Playing C major chord...\n");
    synthesizer_set_channel_frequency(audio->synth, c_chord[0], C4);
    synthesizer_set_channel_frequency(audio->synth, c_chord[1], E4);
    synthesizer_set_channel_frequency(audio->synth, c_chord[2], G4);
    SDL_Delay(1000);

    // Note off
    for (int i = 0; i < 3; i++) {
        synthesizer_note_off(audio->synth, c_chord[i]);
    }
    SDL_Delay(500);

    // Clean up
    for (int i = 0; i < 3; i++) {
        synthesizer_remove_channel(audio->synth, c_chord[i]);
    }
}

void play_arpeggio(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    // Clear any existing channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(audio->synth, i);
    }

    // C major arpeggio notes
    float notes[] = {C4, E4, G4, C5, G4, E4};
    int num_notes = 6;

    // Create a single channel for the arpeggio
    int channel = synthesizer_add_channel(audio->synth, WAVE_TRIANGLE, notes[0], 0.3f);
    synthesizer_set_channel_adsr(audio->synth, channel, 0.05f, 0.1f, 0.7f, 0.2f);

    // Play each note in sequence
    for (int i = 0; i < num_notes; i++) {
        printf("Playing note %d: %.1f Hz\n", i + 1, notes[i]);
        synthesizer_set_channel_frequency(audio->synth, channel, notes[i]);
        synthesizer_note_on(audio->synth, channel);
        SDL_Delay(500);
        synthesizer_note_off(audio->synth, channel);
        SDL_Delay(100);
    }

    // Clean up
    synthesizer_remove_channel(audio->synth, channel);
}

void play_wave_comparison(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    // Clear any existing channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(audio->synth, i);
    }

    // Create channels for different wave types
    int sine_ch = synthesizer_add_channel(audio->synth, WAVE_SINE, A4, 0.25f);
    int saw_ch = synthesizer_add_channel(audio->synth, WAVE_SAW, A4, 0.25f);
    int square_ch = synthesizer_add_channel(audio->synth, WAVE_SQUARE, A4, 0.25f);
    int triangle_ch = synthesizer_add_channel(audio->synth, WAVE_TRIANGLE, A4, 0.25f);

    // Set ADSR for all channels
    synthesizer_set_channel_adsr(audio->synth, sine_ch, 0.1f, 0.1f, 0.8f, 0.3f);
    synthesizer_set_channel_adsr(audio->synth, saw_ch, 0.1f, 0.1f, 0.8f, 0.3f);
    synthesizer_set_channel_adsr(audio->synth, square_ch, 0.1f, 0.1f, 0.8f, 0.3f);
    synthesizer_set_channel_adsr(audio->synth, triangle_ch, 0.1f, 0.1f, 0.8f, 0.3f);

    // Play each wave type separately
    const char* wave_names[] = {"Sine", "Saw", "Square", "Triangle"};
    int channels[] = {sine_ch, saw_ch, square_ch, triangle_ch};

    for (int w = 0; w < 4; w++) {
        printf("Playing %s wave...\n", wave_names[w]);
        synthesizer_note_on(audio->synth, channels[w]);
        SDL_Delay(1000);
        synthesizer_note_off(audio->synth, channels[w]);
        SDL_Delay(200);
    }

    // Clean up
    for (int i = 0; i < 4; i++) {
        synthesizer_remove_channel(audio->synth, channels[i]);
    }
}

void play_frequency_sweep(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    // Clear any existing channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(audio->synth, i);
    }

    // Create a channel for the sweep
    int channel = synthesizer_add_channel(audio->synth, WAVE_SAW, 220.0f, 0.2f);
    synthesizer_set_channel_adsr(audio->synth, channel, 0.05f, 0.1f, 0.8f, 0.2f);

    printf("Sweeping from 220 Hz to 880 Hz...\n");
    synthesizer_note_on(audio->synth, channel);

    // Sweep over 3 seconds
    float start_freq = 220.0f;
    float end_freq = 880.0f;
    int sweep_steps = 60; // 60 steps over 3 seconds

    for (int i = 0; i < sweep_steps; i++) {
        float progress = (float)i / sweep_steps;
        float current_freq = start_freq + (end_freq - start_freq) * progress;
        synthesizer_set_channel_frequency(audio->synth, channel, current_freq);
        SDL_Delay(50); // 50ms per step = 3 seconds total
    }

    synthesizer_note_off(audio->synth, channel);
    SDL_Delay(500);

    // Clean up
    synthesizer_remove_channel(audio->synth, channel);
}

// New layered effects test functions
void play_note_frequency_sweep(AudioSystem* audio, float start_freq, float end_freq, WaveType wave_type, const char* note_name) {
    if (!audio || !audio->synth) return;

    // Clear any existing channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(audio->synth, i);
    }

    // Create channel for the sweep
    int channel = synthesizer_add_channel(audio->synth, wave_type, start_freq, 0.25f);
    synthesizer_set_channel_adsr(audio->synth, channel, 0.05f, 0.1f, 0.8f, 0.2f);

    printf("Sweeping %s note from %.1f Hz to %.1f Hz...\n", note_name, start_freq, end_freq);
    synthesizer_note_on(audio->synth, channel);

    // Sweep over 2 seconds
    int sweep_steps = 40;
    for (int i = 0; i < sweep_steps; i++) {
        float progress = (float)i / sweep_steps;
        float current_freq = start_freq + (end_freq - start_freq) * progress;
        synthesizer_set_channel_frequency(audio->synth, channel, current_freq);
        SDL_Delay(50); // 50ms per step = 2 seconds total
    }

    synthesizer_note_off(audio->synth, channel);
    SDL_Delay(300);

    // Clean up
    synthesizer_remove_channel(audio->synth, channel);
}

void play_all_notes_sweep(AudioSystem* audio, WaveType wave_type) {
    if (!audio || !audio->synth) return;

    const char* wave_names[] = {"Sine", "Saw", "Square", "Triangle"};
    printf("Playing all notes sweep with %s wave...\n", wave_names[wave_type]);

    float notes[] = {A4, B4, C5, D5, E5, F5, G5};
    const char* note_names[] = {"A", "B", "C", "D", "E", "F", "G"};
    int num_notes = 7;

    for (int i = 0; i < num_notes; i++) {
        float start_freq = notes[i];
        float end_freq = notes[i] * 2.0f; // One octave up
        play_note_frequency_sweep(audio, start_freq, end_freq, wave_type, note_names[i]);
        SDL_Delay(200);
    }
}

void play_all_waves_sweep(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    printf("Playing all wave types sweep...\n");

    WaveType wave_types[] = {WAVE_SINE, WAVE_SAW, WAVE_SQUARE, WAVE_TRIANGLE};
    const char* wave_names[] = {"Sine", "Saw", "Square", "Triangle"};

    for (int wave = 0; wave < 4; wave++) {
        printf("Wave type: %s\n", wave_names[wave]);
        play_all_notes_sweep(audio, wave_types[wave]);
        SDL_Delay(500);
    }
}

void play_effects_layered_test(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    printf("Playing layered effects test...\n");

    // Clear any existing channels
    for (int i = 0; i < 16; i++) {
        synthesizer_remove_channel(audio->synth, i);
    }

    // Layer 1: Multiple sine waves at different frequencies
    printf("Layer 1: Multiple sine waves...\n");
    int channels[4];
    for (int i = 0; i < 4; i++) {
        channels[i] = synthesizer_add_channel(audio->synth, WAVE_SINE, A4 * (i + 1), 0.1f);
        synthesizer_set_channel_adsr(audio->synth, channels[i], 0.1f, 0.2f, 0.6f, 0.4f);
    }

    for (int i = 0; i < 4; i++) {
        synthesizer_note_on(audio->synth, channels[i]);
    }
    SDL_Delay(2000);

    // Layer 2: Add saw wave
    printf("Layer 2: Adding saw wave...\n");
    int saw_ch = synthesizer_add_channel(audio->synth, WAVE_SAW, A4, 0.15f);
    synthesizer_set_channel_adsr(audio->synth, saw_ch, 0.05f, 0.1f, 0.7f, 0.3f);
    synthesizer_note_on(audio->synth, saw_ch);
    SDL_Delay(2000);

    // Layer 3: Add square wave
    printf("Layer 3: Adding square wave...\n");
    int square_ch = synthesizer_add_channel(audio->synth, WAVE_SQUARE, A4 * 2, 0.12f);
    synthesizer_set_channel_adsr(audio->synth, square_ch, 0.05f, 0.1f, 0.7f, 0.3f);
    synthesizer_note_on(audio->synth, square_ch);
    SDL_Delay(2000);

    // Layer 4: Add triangle wave
    printf("Layer 4: Adding triangle wave...\n");
    int triangle_ch = synthesizer_add_channel(audio->synth, WAVE_TRIANGLE, A4 * 3, 0.1f);
    synthesizer_set_channel_adsr(audio->synth, triangle_ch, 0.05f, 0.1f, 0.7f, 0.3f);
    synthesizer_note_on(audio->synth, triangle_ch);
    SDL_Delay(2000);

    // Frequency modulation
    printf("Layer 5: Frequency modulation...\n");
    for (int step = 0; step < 20; step++) {
        float mod = 1.0f + 0.5f * sin(step * 0.3f);
        for (int i = 0; i < 4; i++) {
            synthesizer_set_channel_frequency(audio->synth, channels[i], A4 * (i + 1) * mod);
        }
        synthesizer_set_channel_frequency(audio->synth, saw_ch, A4 * mod);
        synthesizer_set_channel_frequency(audio->synth, square_ch, A4 * 2 * mod);
        synthesizer_set_channel_frequency(audio->synth, triangle_ch, A4 * 3 * mod);
        SDL_Delay(100);
    }

    // Note off all channels
    for (int i = 0; i < 4; i++) {
        synthesizer_note_off(audio->synth, channels[i]);
    }
    synthesizer_note_off(audio->synth, saw_ch);
    synthesizer_note_off(audio->synth, square_ch);
    synthesizer_note_off(audio->synth, triangle_ch);
    SDL_Delay(1000);

    // Clean up
    for (int i = 0; i < 4; i++) {
        synthesizer_remove_channel(audio->synth, channels[i]);
    }
    synthesizer_remove_channel(audio->synth, saw_ch);
    synthesizer_remove_channel(audio->synth, square_ch);
    synthesizer_remove_channel(audio->synth, triangle_ch);
}
