#ifndef VERSE_GAMEPAD_H
#define VERSE_GAMEPAD_H

#include <SDL2/SDL.h>
#include <stdbool.h>

struct GameState;

// Xbox / generic gamepad via SDL's Game Controller API (Bluetooth or USB).

void gamepad_init(void);
void gamepad_shutdown(void);

// Hotplug + button edges for UI (A/B/Start/D-pad → keys via the window key callback).
void gamepad_handle_event(const SDL_Event *event);

// True when at least one opened controller is connected.
bool gamepad_is_active(void);

// OR stick/button state into PlayerControls (call after keyboard poll).
void gamepad_apply_movement(struct GameState *state);

// True while any pad's left trigger is held past the fire threshold (town portal).
bool gamepad_town_portal_held(void);

// Right-stick look, attack edges, hotbar, inventory/journal/menu one-shots.
// dt_seconds scales stick look; call once per gameplay frame.
void gamepad_apply_actions(struct GameState *state, double dt_seconds);

#endif // VERSE_GAMEPAD_H
