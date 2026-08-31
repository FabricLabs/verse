#ifndef UI_SOUNDS_H
#define UI_SOUNDS_H

#include "synthesizer/synthesizer.h"

// UI Sound types
typedef enum {
    UI_SOUND_HIGHLIGHT,  // Soft, gentle bell for button highlighting
    UI_SOUND_SELECT,     // Pokemon-style ding/beep for button selection
    UI_SOUND_ERROR       // Buzzer/horn sound for errors
} UISoundType;

// UI Sound system structure
typedef struct {
    Synthesizer* synth;
    int sound_channel;    // Channel reserved for UI sounds
    bool enabled;
} UISoundSystem;

// Initialize the UI sound system
UISoundSystem* ui_sounds_create(Synthesizer* synth, int channel);
void ui_sounds_destroy(UISoundSystem* ui_sounds);

// Enable/disable UI sounds
void ui_sounds_set_enabled(UISoundSystem* ui_sounds, bool enabled);

// Play specific UI sounds
void ui_sounds_play_highlight(UISoundSystem* ui_sounds);
void ui_sounds_play_select(UISoundSystem* ui_sounds);
void ui_sounds_play_error(UISoundSystem* ui_sounds);

// Generic play function
void ui_sounds_play(UISoundSystem* ui_sounds, UISoundType sound_type);

#endif // UI_SOUNDS_H
