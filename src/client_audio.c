/*
 * client_audio.c - Audio management implementation
 */

#include "client_audio.h"
#include "background_music.h"
#include "songwriter/songwriter.h"
#include "ui_sounds.h"
#include "game_sfx.h"
#include "title_hum.h"
#include "client_state.h"
#include <stdio.h>

// Static audio system instances
static BackgroundMusicSystem *g_background_music = NULL;
static Songwriter *g_songwriter = NULL;
static UISoundSystem *g_ui_sounds = NULL;
static GameSfxSystem *g_game_sfx = NULL;
static TitleHumSystem *g_title_hum = NULL;
static bool g_audio_initialized = false;

// Initialize all audio systems
bool client_audio_init(void) {
    if (g_audio_initialized) {
        return true;
    }

    // Initialize background music first (needed for UI sounds)
    g_background_music = background_music_create(44100);
    if (!g_background_music) {
        printf("Failed to initialize background music\n");
        return false;
    }

    // Initialize UI sounds
    g_ui_sounds = ui_sounds_create(g_background_music->synth, 10);
    if (!g_ui_sounds) {
        printf("Failed to initialize UI sounds\n");
        background_music_destroy(g_background_music);
        g_background_music = NULL;
        return false;
    }

    g_game_sfx = game_sfx_create(g_background_music->synth, 11, 12);
    if (!g_game_sfx) {
        printf("Failed to initialize game SFX\n");
        ui_sounds_destroy(g_ui_sounds);
        background_music_destroy(g_background_music);
        g_ui_sounds = NULL;
        g_background_music = NULL;
        return false;
    }

    // Initialize title hum
    g_title_hum = title_hum_create(44100);
    if (!g_title_hum) {
        printf("Failed to initialize title hum\n");
        game_sfx_destroy(g_game_sfx);
        ui_sounds_destroy(g_ui_sounds);
        background_music_destroy(g_background_music);
        g_game_sfx = NULL;
        g_ui_sounds = NULL;
        g_background_music = NULL;
        return false;
    }

    // Initialize songwriter
    g_songwriter = songwriter_create(44100);
    if (!g_songwriter) {
        printf("Failed to initialize songwriter\n");
        title_hum_destroy(g_title_hum);
        game_sfx_destroy(g_game_sfx);
        ui_sounds_destroy(g_ui_sounds);
        background_music_destroy(g_background_music);
        g_title_hum = NULL;
        g_game_sfx = NULL;
        g_ui_sounds = NULL;
        g_background_music = NULL;
        return false;
    }

    g_audio_initialized = true;
    printf("Audio systems initialized successfully\n");
    return true;
}

// Shutdown all audio systems
void client_audio_shutdown(void) {
    if (!g_audio_initialized) {
        return;
    }

    // Cleanup in reverse order
    if (g_background_music) {
        background_music_destroy(g_background_music);
        g_background_music = NULL;
    }

    if (g_songwriter) {
        songwriter_destroy(g_songwriter);
        g_songwriter = NULL;
    }

    if (g_title_hum) {
        title_hum_destroy(g_title_hum);
        g_title_hum = NULL;
    }

    if (g_ui_sounds) {
        ui_sounds_destroy(g_ui_sounds);
        g_ui_sounds = NULL;
    }

    if (g_game_sfx) {
        game_sfx_destroy(g_game_sfx);
        g_game_sfx = NULL;
    }

    g_audio_initialized = false;
    printf("Audio systems shut down\n");
}

// Update audio systems
void client_audio_update(double delta_time) {
    if (!g_audio_initialized) {
        return;
    }

    if (g_background_music) {
        float combat = 0.0f;
        GameState *gs = client_state_get_game();
        if (gs && gs->game_started)
            combat = game_state_music_intensity(gs);
        background_music_set_intensity(g_background_music, combat);
        background_music_update(g_background_music, (float)delta_time);
    }
}

// UI Sound functions
void play_highlight_sound(void) {
    if (g_ui_sounds) {
        ui_sounds_play_highlight(g_ui_sounds);
    }
}

void play_select_sound(void) {
    if (g_ui_sounds) {
        ui_sounds_play_select(g_ui_sounds);
    }
}

void play_error_sound(void) {
    if (g_ui_sounds) {
        ui_sounds_play_error(g_ui_sounds);
    }
}

// Legacy function for compatibility
void play_button_sound(void) {
    play_select_sound();
}

// Title screen audio
void client_audio_start_title_hum(void) {
    if (g_title_hum) {
        title_hum_start(g_title_hum);
    }
}

void client_audio_stop_title_hum(void) {
    if (g_title_hum) {
        title_hum_stop(g_title_hum);
    }
}

// Background music control
void client_audio_set_music_volume(float volume) {
    if (g_background_music) {
        background_music_set_master_volume(g_background_music, volume);
    }
}

void client_audio_set_effects_volume(float volume) {
    if (g_game_sfx) {
        game_sfx_set_volume(g_game_sfx, volume);
    }
}

void client_audio_pause_music(void) {
    if (g_background_music) {
        background_music_pause(g_background_music);
    }
}

void client_audio_resume_music(void) {
    if (g_background_music) {
        background_music_resume(g_background_music);
    }
}

void client_audio_play_ambient_track(void) {
    if (g_background_music) {
        background_music_play_ambient(g_background_music);
    }
}

// Get audio system handles
struct BackgroundMusicSystem* client_audio_get_music_system(void) {
    return g_background_music;
}

struct UISoundSystem* client_audio_get_ui_system(void) {
    return g_ui_sounds;
}

// Check if audio is initialized
bool client_audio_is_initialized(void) {
    return g_audio_initialized;
}
