#include "settings.h"
#include "window.h"
#include "constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Default settings values
const GameSettings DEFAULT_SETTINGS = {
    .text_speed = 1,               // Medium
    .tuning_scale = 1,             // 440Hz
    .background_music_enabled = true,
    .menu_sounds_enabled = true,
    .master_volume = DEFAULT_MASTER_VOLUME, // from constants
    .music_volume = 30,            // 30%
    .fullscreen = 0,               // Windowed
    .scale_factor = 2              // 2x scale
};

// Settings file magic header to validate file format
#define SETTINGS_MAGIC "VERSE_SETTINGS_V1"
#define SETTINGS_VERSION 1

typedef struct {
    char magic[18];                // "VERSE_SETTINGS_V1\0"
    int version;
    GameSettings settings;
} SettingsFile;

bool settings_save(const GameSettings* settings, const char* filename) {
    if (!settings || !filename) {
        printf("Settings save failed: invalid parameters\n");
        return false;
    }

    FILE* file = fopen(filename, "wb");
    if (!file) {
        printf("Settings save failed: could not open file '%s'\n", filename);
        return false;
    }

    SettingsFile settings_file = {0};
    strncpy(settings_file.magic, SETTINGS_MAGIC, sizeof(settings_file.magic) - 1);
    settings_file.version = SETTINGS_VERSION;
    settings_file.settings = *settings;

    size_t written = fwrite(&settings_file, sizeof(SettingsFile), 1, file);
    fclose(file);

    if (written != 1) {
        printf("Settings save failed: write error\n");
        return false;
    }

    printf("Settings saved to '%s'\n", filename);
    return true;
}

bool settings_load(GameSettings* settings, const char* filename) {
    if (!settings || !filename) {
        printf("Settings load failed: invalid parameters\n");
        return false;
    }

    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Settings load: file '%s' not found, using defaults\n", filename);
        *settings = DEFAULT_SETTINGS;
        return false; // File doesn't exist, caller should use defaults
    }

    SettingsFile settings_file = {0};
    size_t read = fread(&settings_file, sizeof(SettingsFile), 1, file);
    fclose(file);

    if (read != 1) {
        printf("Settings load failed: read error, using defaults\n");
        *settings = DEFAULT_SETTINGS;
        return false;
    }

    // Validate magic header
    if (strncmp(settings_file.magic, SETTINGS_MAGIC, strlen(SETTINGS_MAGIC)) != 0) {
        printf("Settings load failed: invalid file format, using defaults\n");
        *settings = DEFAULT_SETTINGS;
        return false;
    }

    // Check version compatibility
    if (settings_file.version != SETTINGS_VERSION) {
        printf("Settings load warning: version mismatch (file=%d, expected=%d), using defaults\n",
               settings_file.version, SETTINGS_VERSION);
        *settings = DEFAULT_SETTINGS;
        return false;
    }

    // Validate loaded settings values
    GameSettings loaded = settings_file.settings;

    // Clamp values to valid ranges
    if (loaded.text_speed < 0 || loaded.text_speed > 2) loaded.text_speed = 1;
    if (loaded.tuning_scale < 0 || loaded.tuning_scale > 1) loaded.tuning_scale = 1;
    if (loaded.master_volume < 0 || loaded.master_volume > 100) loaded.master_volume = 100;
    if (loaded.music_volume < 0 || loaded.music_volume > 100) loaded.music_volume = 30;
    if (loaded.fullscreen < 0 || loaded.fullscreen > 1) loaded.fullscreen = 0;
    if (loaded.scale_factor < 1 || loaded.scale_factor > 4) loaded.scale_factor = 2;

    *settings = loaded;
    printf("Settings loaded from '%s'\n", filename);
    return true;
}

void settings_apply_to_window_state(const GameSettings* settings) {
    if (!settings) return;

    extern WindowState window_state;

    window_state.text_speed = settings->text_speed;
    window_state.tuning_scale = settings->tuning_scale;
    window_state.background_music_enabled = settings->background_music_enabled;
    window_state.menu_sounds_enabled = settings->menu_sounds_enabled;
    window_state.master_volume = settings->master_volume;
    window_state.music_volume = settings->music_volume;
    window_state.fullscreen = settings->fullscreen;
    window_state.scale_factor = settings->scale_factor;

    printf("Applied settings to window state\n");
}

void settings_extract_from_window_state(GameSettings* settings) {
    if (!settings) return;

    extern WindowState window_state;

    settings->text_speed = window_state.text_speed;
    settings->tuning_scale = window_state.tuning_scale;
    settings->background_music_enabled = window_state.background_music_enabled;
    settings->menu_sounds_enabled = window_state.menu_sounds_enabled;
    settings->master_volume = window_state.master_volume;
    settings->music_volume = window_state.music_volume;
    settings->fullscreen = window_state.fullscreen;
    settings->scale_factor = window_state.scale_factor;
}

void settings_reset_to_defaults(GameSettings* settings) {
    if (!settings) return;
    *settings = DEFAULT_SETTINGS;
    printf("Settings reset to defaults\n");
}

void settings_print_debug(const GameSettings* settings) {
    if (!settings) return;

    printf("=== Game Settings ===\n");
    printf("Text Speed: %d (0=Slow, 1=Medium, 2=Fast)\n", settings->text_speed);
    printf("Tuning Scale: %d (0=432Hz, 1=440Hz)\n", settings->tuning_scale);
    printf("Background Music: %s\n", settings->background_music_enabled ? "ON" : "OFF");
    printf("Menu Sounds: %s\n", settings->menu_sounds_enabled ? "ON" : "OFF");
    printf("Master Volume: %d%%\n", settings->master_volume);
    printf("Music Volume: %d%%\n", settings->music_volume);
    printf("Fullscreen: %s\n", settings->fullscreen ? "ON" : "OFF");
    printf("Scale Factor: %dx\n", settings->scale_factor);
    printf("====================\n");
}
