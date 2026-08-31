#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

// Settings structure that maps to WindowState settings
typedef struct {
    int text_speed;               // 0=Slow, 1=Medium, 2=Fast
    int tuning_scale;             // 0=432Hz, 1=440Hz
    bool background_music_enabled; // Background music on/off
    bool menu_sounds_enabled;      // UI/menu sounds on/off
    int master_volume;            // 0-100
    int music_volume;             // 0-100
    int fullscreen;               // 0=windowed, 1=fullscreen
    int scale_factor;             // Window scale factor
} GameSettings;

// Default settings
extern const GameSettings DEFAULT_SETTINGS;

// Settings file operations
bool settings_save(const GameSettings* settings, const char* filename);
bool settings_load(GameSettings* settings, const char* filename);

// Window state integration
void settings_apply_to_window_state(const GameSettings* settings);
void settings_extract_from_window_state(GameSettings* settings);

// Utility functions
void settings_reset_to_defaults(GameSettings* settings);
void settings_print_debug(const GameSettings* settings);

#endif // SETTINGS_H
