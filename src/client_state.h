/*
 * client_state.h - Client state management
 *
 * Manages global game state and provides access to subsystems.
 */

#ifndef CLIENT_STATE_H
#define CLIENT_STATE_H

#include <stdbool.h>
#include "game_state.h"

// Initialize client state
bool client_state_init(void);

// Shutdown client state
void client_state_shutdown(void);

// Get game state
GameState* client_state_get_game(void);

// Title screen state
bool client_state_is_title_screen(void);
void client_state_set_title_screen(bool show);
float client_state_get_title_fade(void);
void client_state_set_title_fade(float alpha);
unsigned int client_state_get_title_start_time(void);
void client_state_set_title_start_time(unsigned int time);

// Exit handling
bool client_state_should_exit(void);
void client_state_request_exit(void);
void client_state_cancel_exit(void);

// Window state updates
void client_state_sync_with_window(void);

#endif // CLIENT_STATE_H
