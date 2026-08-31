#include "ui_sounds.h"
#include <stdlib.h>
#include <SDL2/SDL.h>

// Timer callback to stop UI sounds
static Uint32 ui_sound_timer_callback(Uint32 interval, void *param)
{
    UISoundSystem* ui_sounds = (UISoundSystem*)param;
    if (ui_sounds && ui_sounds->synth)
    {
        synthesizer_note_off(ui_sounds->synth, ui_sounds->sound_channel);
    }
    return 0; // One-shot timer
}

// Create UI sound system
UISoundSystem* ui_sounds_create(Synthesizer* synth, int channel)
{
    if (!synth || channel < 0 || channel >= 16)
        return NULL;

    UISoundSystem* ui_sounds = calloc(1, sizeof(UISoundSystem));
    if (!ui_sounds)
        return NULL;

    ui_sounds->synth = synth;
    ui_sounds->sound_channel = channel;
    ui_sounds->enabled = true;

    return ui_sounds;
}

// Destroy UI sound system
void ui_sounds_destroy(UISoundSystem* ui_sounds)
{
    if (ui_sounds)
    {
        // Stop any playing sound
        if (ui_sounds->synth)
        {
            synthesizer_note_off(ui_sounds->synth, ui_sounds->sound_channel);
        }
        free(ui_sounds);
    }
}

// Enable/disable UI sounds
void ui_sounds_set_enabled(UISoundSystem* ui_sounds, bool enabled)
{
    if (ui_sounds)
    {
        ui_sounds->enabled = enabled;
    }
}

// Play highlight sound (soft, gentle bell)
void ui_sounds_play_highlight(UISoundSystem* ui_sounds)
{
    if (!ui_sounds || !ui_sounds->enabled || !ui_sounds->synth)
        return;

    // Soft bell sound: reduce buzz by applying brief fade-in/out via ADSR and lower amplitude
    synthesizer_set_channel_wave_type(ui_sounds->synth, ui_sounds->sound_channel, WAVE_SINE);
    synthesizer_set_channel_adsr(ui_sounds->synth, ui_sounds->sound_channel, 0.003f, 0.01f, 0.0f, 0.05f);
    synthesizer_set_channel_frequency(ui_sounds->synth, ui_sounds->sound_channel, 1200.0f);
    synthesizer_set_channel_amplitude(ui_sounds->synth, ui_sounds->sound_channel, 0.22f);
    synthesizer_note_on(ui_sounds->synth, ui_sounds->sound_channel);

    // Short duration for gentle feel (80ms)
    SDL_AddTimer(80, ui_sound_timer_callback, ui_sounds);
}

// Play select sound (Pokemon-style ding/beep)
void ui_sounds_play_select(UISoundSystem* ui_sounds)
{
    if (!ui_sounds || !ui_sounds->enabled || !ui_sounds->synth)
        return;

    // Classic video game beep with softer edges to avoid buzz
    synthesizer_set_channel_wave_type(ui_sounds->synth, ui_sounds->sound_channel, WAVE_TRIANGLE);
    synthesizer_set_channel_adsr(ui_sounds->synth, ui_sounds->sound_channel, 0.002f, 0.01f, 0.0f, 0.06f);
    synthesizer_set_channel_frequency(ui_sounds->synth, ui_sounds->sound_channel, 800.0f);
    synthesizer_set_channel_amplitude(ui_sounds->synth, ui_sounds->sound_channel, 0.5f);
    synthesizer_note_on(ui_sounds->synth, ui_sounds->sound_channel);

    // Medium duration for classic game feel (120ms)
    SDL_AddTimer(120, ui_sound_timer_callback, ui_sounds);
}

// Play error sound (buzzer/horn)
void ui_sounds_play_error(UISoundSystem* ui_sounds)
{
    if (!ui_sounds || !ui_sounds->enabled || !ui_sounds->synth)
        return;

    // Error sound: keep presence but reduce harsh aliasing
    synthesizer_set_channel_wave_type(ui_sounds->synth, ui_sounds->sound_channel, WAVE_SAW);
    synthesizer_set_channel_adsr(ui_sounds->synth, ui_sounds->sound_channel, 0.002f, 0.02f, 0.0f, 0.08f);
    synthesizer_set_channel_frequency(ui_sounds->synth, ui_sounds->sound_channel, 220.0f);
    synthesizer_set_channel_amplitude(ui_sounds->synth, ui_sounds->sound_channel, 0.65f);
    synthesizer_note_on(ui_sounds->synth, ui_sounds->sound_channel);

    // Longer duration for error emphasis (250ms)
    SDL_AddTimer(250, ui_sound_timer_callback, ui_sounds);
}

// Generic play function
void ui_sounds_play(UISoundSystem* ui_sounds, UISoundType sound_type)
{
    switch (sound_type)
    {
        case UI_SOUND_HIGHLIGHT:
            ui_sounds_play_highlight(ui_sounds);
            break;
        case UI_SOUND_SELECT:
            ui_sounds_play_select(ui_sounds);
            break;
        case UI_SOUND_ERROR:
            ui_sounds_play_error(ui_sounds);
            break;
    }
}
