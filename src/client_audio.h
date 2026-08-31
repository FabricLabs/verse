/*
 * client_audio.h - Audio management for the verse client
 *
 * Handles all audio systems including background music, UI sounds,
 * and title hum effects.
 */

#ifndef CLIENT_AUDIO_H
#define CLIENT_AUDIO_H

#include <stdbool.h>

// Forward declarations
struct BackgroundMusicSystem;
struct Songwriter;
struct UISoundSystem;
struct TitleHumSystem;

// Initialize all audio systems
bool client_audio_init(void);

// Shutdown all audio systems
void client_audio_shutdown(void);

// Update audio systems (call each frame)
void client_audio_update(double delta_time);

// UI Sound functions
void play_highlight_sound(void);
void play_select_sound(void);
void play_error_sound(void);
void play_button_sound(void); // Legacy compatibility

// Game SFX (Game Boy–style composed events; no-ops until game_sfx_create)
void play_fireball_cast_sound(void);
void play_fireball_impact_sound(void);
void play_unarmed_swing_sound(void);
void play_unarmed_hit_sound(void);
void play_block_hit_sound(void);
void play_block_break_sound(void);

// Title screen audio
void client_audio_start_title_hum(void);
void client_audio_stop_title_hum(void);

// Background music control
void client_audio_set_music_volume(float volume);
void client_audio_set_effects_volume(float volume);
void client_audio_pause_music(void);
void client_audio_resume_music(void);
void client_audio_play_ambient_track(void);

// Get audio system handles (for settings sync)
struct BackgroundMusicSystem* client_audio_get_music_system(void);
struct UISoundSystem* client_audio_get_ui_system(void);

// Check if audio is initialized
bool client_audio_is_initialized(void);

#endif // CLIENT_AUDIO_H
