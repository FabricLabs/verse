#include "dynamic_music.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 1024

// Audio callback for SDL
void audio_callback(void* userdata, Uint8* stream, int len) {
    DynamicMusicSystem* music = (DynamicMusicSystem*)userdata;
    if (!music) return;

    int num_samples = len / sizeof(float);
    float* float_stream = (float*)stream;

    // Generate audio samples
    dynamic_music_generate_buffer(music, float_stream, num_samples);
}

// Initialize SDL audio
bool init_audio(DynamicMusicSystem* music, SDL_AudioDeviceID* audio_device) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL audio initialization failed: %s\n", SDL_GetError());
        return false;
    }

    // Configure audio specification
    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_F32;
    desired.channels = 1;
    desired.samples = BUFFER_SIZE;
    desired.callback = audio_callback;
    desired.userdata = music;

    // Open audio device
    *audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (*audio_device == 0) {
        printf("Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }

    printf("Audio system initialized successfully\n");
    return true;
}

// Demo function for each game state
void demo_pregame_state(DynamicMusicSystem* music) {
    printf("\n=== PREGAME STATE (Main Menu) ===\n");
    printf("Calm, welcoming music with ambient pad and gentle melody\n");
    dynamic_music_set_game_state(music, MUSIC_STATE_PREGAME);
    SDL_Delay(5000); // Wait 5 seconds using SDL_Delay instead of sleep
}

void demo_tutorial_state(DynamicMusicSystem* music) {
    printf("\n=== TUTORIAL STATE ===\n");
    printf("Guiding, educational music with clear structure\n");
    dynamic_music_set_game_state(music, MUSIC_STATE_TUTORIAL);
    SDL_Delay(5000);
}

void demo_home_state(DynamicMusicSystem* music) {
    printf("\n=== HOME STATE (Safe Haven) ===\n");
    printf("Comfortable, safe haven music with warm harmonies\n");
    dynamic_music_set_game_state(music, MUSIC_STATE_HOME);
    SDL_Delay(5000);
}

void demo_explore_state(DynamicMusicSystem* music) {
    printf("\n=== EXPLORE STATE ===\n");
    printf("Adventurous, mysterious exploration music\n");
    dynamic_music_set_game_state(music, MUSIC_STATE_EXPLORE);
    SDL_Delay(5000);
}

void demo_combat_state(DynamicMusicSystem* music) {
    printf("\n=== COMBAT STATE ===\n");
    printf("Intense, fast-paced battle music\n");
    dynamic_music_set_game_state(music, MUSIC_STATE_COMBAT);
    SDL_Delay(5000);
}

// Demo adaptive parameters
void demo_health_adaptation(DynamicMusicSystem* music) {
    printf("\n=== HEALTH ADAPTATION DEMO ===\n");

    // Start in combat state
    dynamic_music_set_game_state(music, MUSIC_STATE_COMBAT);

    printf("High health (100%%): Normal intensity\n");
    dynamic_music_update_health(music, 1.0f);
    SDL_Delay(3000);

    printf("Medium health (50%%): Normal intensity\n");
    dynamic_music_update_health(music, 0.5f);
    SDL_Delay(3000);

    printf("Critical health (10%%): Very low intensity\n");
    dynamic_music_update_health(music, 0.1f);
    SDL_Delay(3000);
}

void demo_battle_time_adaptation(DynamicMusicSystem* music) {
    printf("\n=== BATTLE TIME ADAPTATION DEMO ===\n");

    // Start in combat state
    dynamic_music_set_game_state(music, MUSIC_STATE_COMBAT);

    printf("Short battle (15s): High intensity\n");
    dynamic_music_update_battle_time(music, 15.0f);
    SDL_Delay(3000);

    printf("Medium battle (60s): Normal intensity\n");
    dynamic_music_update_battle_time(music, 60.0f);
    SDL_Delay(3000);

    printf("Long battle (150s): Reduced intensity\n");
    dynamic_music_update_battle_time(music, 150.0f);
    SDL_Delay(3000);
}

void demo_player_level_adaptation(DynamicMusicSystem* music) {
    printf("\n=== PLAYER LEVEL ADAPTATION DEMO ===\n");

    // Start in explore state
    dynamic_music_set_game_state(music, MUSIC_STATE_EXPLORE);

    printf("Low level (3): Simplified music\n");
    dynamic_music_update_player_level(music, 3);
    SDL_Delay(3000);

    printf("Medium level (10): Normal complexity\n");
    dynamic_music_update_player_level(music, 10);
    SDL_Delay(3000);

    printf("High level (20): Enhanced complexity\n");
    dynamic_music_update_player_level(music, 20);
    SDL_Delay(3000);
}

// Main demo function
void run_dynamic_music_demo(DynamicMusicSystem* music) {
    printf("=== VERSE Dynamic Music System Demo ===\n");
    printf("Demonstrating 5-instrument adaptive background music\n\n");

    // Demo each game state
    demo_pregame_state(music);
    demo_tutorial_state(music);
    demo_home_state(music);
    demo_explore_state(music);
    demo_combat_state(music);

    // Demo adaptive parameters
    demo_health_adaptation(music);
    demo_battle_time_adaptation(music);
    demo_player_level_adaptation(music);

    printf("\n=== Demo Complete ===\n");
}

int main() {
    printf("=== VERSE Dynamic Music System Test ===\n");

    // Create dynamic music system
    DynamicMusicSystem* music = dynamic_music_create(SAMPLE_RATE);
    if (!music) {
        printf("Failed to create dynamic music system\n");
        return 1;
    }

    // Initialize SDL audio
    SDL_AudioDeviceID audio_device;
    if (!init_audio(music, &audio_device)) {
        printf("Failed to initialize audio\n");
        dynamic_music_destroy(music);
        return 1;
    }

    // Start audio playback
    SDL_PauseAudioDevice(audio_device, 0);
    printf("Audio playback started\n");

    // Run the demo
    run_dynamic_music_demo(music);

    // Stop audio and cleanup
    SDL_PauseAudioDevice(audio_device, 1);
    SDL_CloseAudioDevice(audio_device);
    dynamic_music_destroy(music);
    SDL_Quit();

    printf("Dynamic music system test completed successfully!\n");
    return 0;
}
