#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <SDL2/SDL.h>
#include "src/songwriter/songwriter.h"
#include "src/synthesizer/synthesizer.h"

// Test configuration
#define SAMPLE_RATE 44100
#define BUFFER_SIZE 4096
#define TEST_DURATION 10.0f // 10 seconds

// Audio callback for real-time testing
void audio_callback(void* userdata, Uint8* stream, int len) {
    Songwriter* writer = (Songwriter*)userdata;
    if (!writer) return;

    int num_samples = len / sizeof(float);
    float* float_stream = (float*)stream;

    songwriter_generate_buffer_high_quality(writer, float_stream, num_samples);
}

// Initialize SDL audio
bool setup_audio(Songwriter* writer) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL audio initialization failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_F32;
    desired.channels = 1;
    desired.samples = BUFFER_SIZE;
    desired.callback = audio_callback;
    desired.userdata = writer;

    if (SDL_OpenAudio(&desired, &obtained) < 0) {
        printf("SDL audio open failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_PauseAudio(0);
    return true;
}

// Create a test song with enhanced features
Song* create_test_song() {
    Song* song = songwriter_create_song("Enhanced Test Song", "AI Composer", 120);
    if (!song) return NULL;

    // Set enhanced audio quality features
    song->auto_normalize = true;
    song->master_volume = 0.8f;
    song->master_filter_cutoff = 18000.0f;
    song->master_filter_resonance = 0.1f;
    song->compressor_threshold = 0.7f;
    song->compressor_ratio = 3.0f;
    song->limiter_threshold = 0.9f;
    song->delay_time = 0.3f;
    song->delay_feedback = 0.3f;
    song->delay_mix = 0.2f;
    song->reverb_time = 1.5f;
    song->reverb_mix = 0.3f;

    // Create tracks with different characteristics
    Track* piano_track = songwriter_create_track(song, "Piano", 0);
    if (piano_track) {
        piano_track->wave_type = 0; // WAVE_SINE
        piano_track->attack_time = 0.01f;
        piano_track->decay_time = 0.1f;
        piano_track->sustain_level = 0.8f;
        piano_track->release_time = 0.3f;
        piano_track->auto_normalize = true;
        songwriter_add_track(song, piano_track);
    }

    Track* bass_track = songwriter_create_track(song, "Bass", 1);
    if (bass_track) {
        bass_track->wave_type = 2; // WAVE_SQUARE
        bass_track->attack_time = 0.02f;
        bass_track->decay_time = 0.2f;
        bass_track->sustain_level = 0.6f;
        bass_track->release_time = 0.4f;
        songwriter_add_track(song, bass_track);
    }

    Track* lead_track = songwriter_create_track(song, "Lead", 2);
    if (lead_track) {
        lead_track->wave_type = 1; // WAVE_SAW
        lead_track->attack_time = 0.005f;
        lead_track->decay_time = 0.05f;
        lead_track->sustain_level = 0.9f;
        lead_track->release_time = 0.2f;
        songwriter_add_track(song, lead_track);
    }

    // Add notes with enhanced features
    if (piano_track) {
        // Piano melody with articulation
        songwriter_add_note_advanced(piano_track, 60, 0.8f, 0, 480, 1, 0.0f, 0.0f, 0.0f, 0, 20000.0f, 0.0f); // C4
        songwriter_add_note_advanced(piano_track, 62, 0.7f, 480, 480, 1, 0.0f, 0.0f, 0.0f, 0, 20000.0f, 0.0f); // D4
        songwriter_add_note_advanced(piano_track, 64, 0.9f, 960, 480, 2, 0.0f, 0.0f, 0.0f, 0, 20000.0f, 0.0f); // E4
        songwriter_add_note_advanced(piano_track, 65, 0.8f, 1440, 480, 1, 0.0f, 0.0f, 0.0f, 0, 20000.0f, 0.0f); // F4
        songwriter_add_note_advanced(piano_track, 67, 0.9f, 1920, 480, 2, 0.0f, 0.0f, 0.0f, 0, 20000.0f, 0.0f); // G4
        songwriter_add_note_advanced(piano_track, 69, 0.8f, 2400, 480, 1, 0.0f, 0.0f, 0.0f, 0, 20000.0f, 0.0f); // A4
        songwriter_add_note_advanced(piano_track, 71, 0.9f, 2880, 480, 2, 0.0f, 0.0f, 0.0f, 0, 20000.0f, 0.0f); // B4
        songwriter_add_note_advanced(piano_track, 72, 1.0f, 3360, 480, 3, 0.0f, 0.0f, 0.0f, 0, 20000.0f, 0.0f); // C5
    }

    if (bass_track) {
        // Bass line with filter
        songwriter_add_note_advanced(bass_track, 36, 0.9f, 0, 960, 1, 0.0f, 0.0f, 0.0f, 1, 800.0f, 0.3f); // C2
        songwriter_add_note_advanced(bass_track, 38, 0.8f, 960, 960, 1, 0.0f, 0.0f, 0.0f, 1, 800.0f, 0.3f); // D2
        songwriter_add_note_advanced(bass_track, 40, 0.9f, 1920, 960, 1, 0.0f, 0.0f, 0.0f, 1, 800.0f, 0.3f); // E2
        songwriter_add_note_advanced(bass_track, 41, 0.8f, 2880, 960, 1, 0.0f, 0.0f, 0.0f, 1, 800.0f, 0.3f); // F2
    }

    if (lead_track) {
        // Lead with LFO and portamento
        songwriter_add_note_advanced(lead_track, 72, 0.8f, 480, 480, 1, 0.1f, 2.0f, 0.1f, 0, 20000.0f, 0.0f); // C5
        songwriter_add_note_advanced(lead_track, 76, 0.9f, 1440, 480, 2, 0.1f, 2.0f, 0.1f, 0, 20000.0f, 0.0f); // E5
        songwriter_add_note_advanced(lead_track, 79, 1.0f, 2400, 480, 3, 0.1f, 2.0f, 0.1f, 0, 20000.0f, 0.0f); // G5
        songwriter_add_note_advanced(lead_track, 84, 0.9f, 3360, 480, 2, 0.1f, 2.0f, 0.1f, 0, 20000.0f, 0.0f); // C6
    }

    songwriter_update_song_duration(song);
    return song;
}

// Test audio quality features
void test_audio_quality(Songwriter* writer) {
    printf("Testing audio quality features...\n");

    // Create a simple test song
    Song* test_song = songwriter_create_song("Test Song", "Test", 120);
    if (!test_song) {
        printf("Failed to create test song\n");
        return;
    }

    Track* test_track = songwriter_create_track(test_song, "Test", 0);
    if (!test_track) {
        songwriter_destroy_song(test_song);
        printf("Failed to create test track\n");
        return;
    }

    // Add a simple note
    songwriter_add_note(test_track, 60, 100, 0, 480); // C4
    songwriter_add_track(test_song, test_track);
    songwriter_update_song_duration(test_song);

    songwriter_load_song(writer, test_song);
    songwriter_play(writer);

    // Test different wave types
    printf("Testing wave types:\n");
    for (int i = 0; i < 4; i++) {
        synthesizer_set_channel_wave_type(writer->synth, 0, i);
        printf("  Wave type %d: ", i);

        // Generate a short test tone
        float buffer[1024];
        for (int j = 0; j < 1024; j++) {
            buffer[j] = songwriter_generate_sample(writer);
        }

        // Calculate RMS
        float rms = 0.0f;
        for (int j = 0; j < 1024; j++) {
            rms += buffer[j] * buffer[j];
        }
        rms = sqrtf(rms / 1024.0f);
        printf("RMS: %.4f\n", rms);
    }

    // Test filters
    printf("Testing filters:\n");
    for (int i = 1; i <= 4; i++) {
        synthesizer_set_channel_filter(writer->synth, 0, i, 1000.0f, 0.5f);
        printf("  Filter type %d: ", i);

        float buffer[1024];
        for (int j = 0; j < 1024; j++) {
            buffer[j] = songwriter_generate_sample(writer);
        }

        float rms = 0.0f;
        for (int j = 0; j < 1024; j++) {
            rms += buffer[j] * buffer[j];
        }
        rms = sqrtf(rms / 1024.0f);
        printf("RMS: %.4f\n", rms);
    }

        // Test LFO
    printf("Testing LFO:\n");
    synthesizer_set_channel_lfo(writer->synth, 0, 1.0f, 0.1f);
    float buffer[1024];
    for (int j = 0; j < 1024; j++) {
        buffer[j] = songwriter_generate_sample(writer);
    }

    float rms = 0.0f;
    for (int j = 0; j < 1024; j++) {
        rms += buffer[j] * buffer[j];
    }
    rms = sqrtf(rms / 1024.0f);
    printf("  LFO RMS: %.4f\n", rms);

    songwriter_stop(writer);
    // Song is managed by writer after load_song, don't destroy it here
}

// Test sequencer performance
void test_sequencer_performance(Songwriter* writer) {
    printf("Testing sequencer performance...\n");

    Song* song = create_test_song();
    if (!song) {
        printf("Failed to create test song\n");
        return;
    }

    songwriter_load_song(writer, song);
    songwriter_play(writer);

    // Generate audio for 5 seconds and measure performance
    int total_samples = SAMPLE_RATE * 5;
    float* buffer = malloc(sizeof(float) * total_samples);

    if (buffer) {
        clock_t start = clock();

        for (int i = 0; i < total_samples; i++) {
            buffer[i] = songwriter_generate_sample(writer);
        }

        clock_t end = clock();
        double cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;

        printf("Generated %d samples in %.3f seconds\n", total_samples, cpu_time_used);
        printf("Performance: %.1f samples per second\n", total_samples / cpu_time_used);

        // Calculate audio statistics
        float peak = 0.0f;
        float rms = 0.0f;
        for (int i = 0; i < total_samples; i++) {
            float abs_sample = fabsf(buffer[i]);
            if (abs_sample > peak) peak = abs_sample;
            rms += buffer[i] * buffer[i];
        }
        rms = sqrtf(rms / total_samples);

        printf("Audio statistics:\n");
        printf("  Peak: %.4f\n", peak);
        printf("  RMS: %.4f\n", rms);
        printf("  Dynamic range: %.2f dB\n", 20.0f * log10f(peak / rms));

        free(buffer);
    }

    songwriter_stop(writer);
    // Song is managed by writer after load_song, don't destroy it here
}

// Test real-time audio
void test_realtime_audio(Songwriter* writer) {
    printf("Testing real-time audio...\n");

    Song* song = create_test_song();
    if (!song) {
        printf("Failed to create test song\n");
        return;
    }

    songwriter_load_song(writer, song);
    songwriter_play(writer);

    if (!setup_audio(writer)) {
        printf("Failed to setup audio\n");
        return;
    }

    printf("Playing for %d seconds...\n", (int)TEST_DURATION);

    // Monitor audio statistics
    for (int i = 0; i < (int)TEST_DURATION; i++) {
        SDL_Delay(1000);

        float peak, rms, normalization;
        songwriter_get_audio_stats(writer, &peak, &rms, &normalization);

        printf("Time: %ds, Peak: %.4f, RMS: %.4f, Norm: %.4f\n",
               i + 1, peak, rms, normalization);
    }

    SDL_CloseAudio();
    songwriter_stop(writer);
    // Song is managed by writer after load_song, don't destroy it here
}

// Test advanced features
void test_advanced_features(Songwriter* writer) {
    printf("Testing advanced features...\n");

    // Test auto-normalize
    songwriter_set_auto_normalize(writer, true);
    printf("Auto-normalize enabled\n");

    // Test master effects
    songwriter_set_master_volume(writer, 0.8f);
    printf("Master volume set to 0.8\n");

    // Test track controls
    songwriter_set_track_volume(writer, 0, 0.7f);
    songwriter_set_track_pan(writer, 0, 0.3f);
    songwriter_mute_track(writer, 1, true);
    songwriter_solo_track(writer, 2, true);
    printf("Track controls applied\n");

    // Test audio quality controls
    songwriter_set_track_wave_type(writer, 0, 1); // Saw wave
    songwriter_set_track_adsr(writer, 0, 0.01f, 0.1f, 0.8f, 0.2f);
    songwriter_set_track_filter(writer, 0, 1, 2000.0f, 0.5f); // Low-pass filter
    printf("Audio quality controls applied\n");

    // Test timing functions
    float current_time = songwriter_get_current_time(writer);
    float current_beat = songwriter_get_current_beat_float(writer);
    float current_measure = songwriter_get_current_measure_float(writer);
    printf("Timing: %.2fs, Beat: %.2f, Measure: %.2f\n", current_time, current_beat, current_measure);
}

int main() {
    printf("Enhanced Songwriter Test\n");
    printf("=======================\n\n");

    // Create songwriter with enhanced features
    Songwriter* writer = songwriter_create(SAMPLE_RATE);
    if (!writer) {
        printf("Failed to create songwriter\n");
        return 1;
    }

    printf("Songwriter created successfully\n");
    printf("Sample rate: %d Hz\n", SAMPLE_RATE);
    printf("Buffer size: %d samples\n", BUFFER_SIZE);
    printf("\n");

    // Test audio quality
    test_audio_quality(writer);
    printf("\n");

    // Test sequencer performance
    test_sequencer_performance(writer);
    printf("\n");

    // Test advanced features
    test_advanced_features(writer);
    printf("\n");

    // Test real-time audio (optional - requires audio hardware)
    printf("Real-time audio test (press Enter to skip): ");
    if (getchar() != '\n') {
        test_realtime_audio(writer);
    }

    // Cleanup
    songwriter_destroy(writer);
    SDL_Quit();

    printf("\nTest completed successfully!\n");
    return 0;
}
