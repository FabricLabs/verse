#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <SDL2/SDL.h>
#include "synthesizer.h"

// Simple integration example showing how synthesizer could work with SDL audio
// This is a demonstration of the concept - actual integration would require
// more sophisticated audio handling

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 1024

typedef struct {
    Synthesizer* synth;
    SDL_AudioDeviceID audio_device;
    bool audio_initialized;
} AudioSystem;

// Audio callback for SDL
void audio_callback(void* userdata, Uint8* stream, int len) {
    AudioSystem* audio = (AudioSystem*)userdata;
    if (!audio || !audio->synth) return;

    // Convert buffer size from bytes to samples
    int num_samples = len / sizeof(float);
    float* float_stream = (float*)stream;

    // Generate audio samples
    synthesizer_generate_buffer(audio->synth, float_stream, num_samples);
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
    audio->audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (audio->audio_device == 0) {
        printf("Failed to open audio device: %s\n", SDL_GetError());
        synthesizer_destroy(audio->synth);
        SDL_Quit();
        free(audio);
        return NULL;
    }

    audio->audio_initialized = true;
    printf("Audio system initialized successfully\n");
    printf("Sample rate: %d Hz\n", obtained.freq);
    printf("Buffer size: %d samples\n", obtained.samples);

    return audio;
}

// Destroy audio system
void audio_system_destroy(AudioSystem* audio) {
    if (!audio) return;

    if (audio->audio_initialized) {
        SDL_CloseAudioDevice(audio->audio_device);
    }

    if (audio->synth) {
        synthesizer_destroy(audio->synth);
    }

    SDL_Quit();
    free(audio);
}

// Start audio playback
void audio_system_start(AudioSystem* audio) {
    if (!audio || !audio->audio_initialized) return;

    SDL_PauseAudioDevice(audio->audio_device, 0);
    printf("Audio playback started\n");
}

// Stop audio playback
void audio_system_stop(AudioSystem* audio) {
    if (!audio || !audio->audio_initialized) return;

    SDL_PauseAudioDevice(audio->audio_device, 1);
    printf("Audio playback stopped\n");
}

// Demo functions
void demo_basic_notes(AudioSystem* audio);
void demo_chord_progression(AudioSystem* audio);
void demo_wave_types(AudioSystem* audio);

int main() {
    printf("=== VERSE Synthesizer Integration Example ===\n");

    // Create audio system
    AudioSystem* audio = audio_system_create();
    if (!audio) {
        printf("Failed to create audio system\n");
        return 1;
    }

    // Start audio playback
    audio_system_start(audio);

    // Demo 1: Basic notes
    printf("\nDemo 1: Basic Notes (C major scale)\n");
    demo_basic_notes(audio);

    // Demo 2: Chord progression
    printf("\nDemo 2: Chord Progression\n");
    demo_chord_progression(audio);

    // Demo 3: Wave types
    printf("\nDemo 3: Wave Type Comparison\n");
    demo_wave_types(audio);

    // Stop audio and cleanup
    audio_system_stop(audio);
    audio_system_destroy(audio);

    printf("\nIntegration example completed!\n");
    return 0;
}

void demo_basic_notes(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    // C major scale frequencies
    float scale[] = {261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f, 523.25f};
    int num_notes = 8;

    // Create a channel for playing notes
    int channel = synthesizer_add_channel(audio->synth, WAVE_SINE, scale[0], 0.3f);
    synthesizer_set_channel_adsr(audio->synth, channel, 0.05f, 0.1f, 0.7f, 0.2f);

    // Play each note in the scale
    for (int i = 0; i < num_notes; i++) {
        printf("Playing note %d: %.1f Hz\n", i + 1, scale[i]);

        synthesizer_set_channel_frequency(audio->synth, channel, scale[i]);
        synthesizer_note_on(audio->synth, channel);

        // Play for 0.5 seconds
        SDL_Delay(500);

        synthesizer_note_off(audio->synth, channel);

        // Small gap between notes
        SDL_Delay(100);
    }

    // Clean up
    synthesizer_remove_channel(audio->synth, channel);
}

void demo_chord_progression(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    // Create channels for a chord
    int channels[3];
    channels[0] = synthesizer_add_channel(audio->synth, WAVE_SINE, 261.63f, 0.2f); // C
    channels[1] = synthesizer_add_channel(audio->synth, WAVE_SINE, 329.63f, 0.2f); // E
    channels[2] = synthesizer_add_channel(audio->synth, WAVE_SINE, 392.00f, 0.2f); // G

    // Set ADSR for all channels
    for (int i = 0; i < 3; i++) {
        synthesizer_set_channel_adsr(audio->synth, channels[i], 0.1f, 0.1f, 0.8f, 0.3f);
    }

    // Play C major chord
    printf("Playing C major chord...\n");
    for (int i = 0; i < 3; i++) {
        synthesizer_note_on(audio->synth, channels[i]);
    }
    SDL_Delay(1000);

    // Change to F major (F-A-C)
    printf("Playing F major chord...\n");
    synthesizer_set_channel_frequency(audio->synth, channels[0], 349.23f); // F
    synthesizer_set_channel_frequency(audio->synth, channels[1], 440.00f); // A
    synthesizer_set_channel_frequency(audio->synth, channels[2], 523.25f); // C
    SDL_Delay(1000);

    // Change to G major (G-B-D)
    printf("Playing G major chord...\n");
    synthesizer_set_channel_frequency(audio->synth, channels[0], 392.00f); // G
    synthesizer_set_channel_frequency(audio->synth, channels[1], 493.88f); // B
    synthesizer_set_channel_frequency(audio->synth, channels[2], 587.33f); // D
    SDL_Delay(1000);

    // Note off all channels
    for (int i = 0; i < 3; i++) {
        synthesizer_note_off(audio->synth, channels[i]);
    }

    // Wait for release
    SDL_Delay(500);

    // Clean up
    for (int i = 0; i < 3; i++) {
        synthesizer_remove_channel(audio->synth, channels[i]);
    }
}

void demo_wave_types(AudioSystem* audio) {
    if (!audio || !audio->synth) return;

    // Create channels for different wave types
    int channels[4];
    channels[0] = synthesizer_add_channel(audio->synth, WAVE_SINE, 440.0f, 0.25f);
    channels[1] = synthesizer_add_channel(audio->synth, WAVE_SAW, 440.0f, 0.25f);
    channels[2] = synthesizer_add_channel(audio->synth, WAVE_SQUARE, 440.0f, 0.25f);
    channels[3] = synthesizer_add_channel(audio->synth, WAVE_TRIANGLE, 440.0f, 0.25f);

    const char* wave_names[] = {"Sine", "Saw", "Square", "Triangle"};

    // Play each wave type separately
    for (int i = 0; i < 4; i++) {
        printf("Playing %s wave...\n", wave_names[i]);

        synthesizer_note_on(audio->synth, channels[i]);
        SDL_Delay(1000);
        synthesizer_note_off(audio->synth, channels[i]);
        SDL_Delay(200);
    }

    // Clean up
    for (int i = 0; i < 4; i++) {
        synthesizer_remove_channel(audio->synth, channels[i]);
    }
}
