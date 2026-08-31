/*
 * client_input.h - Input handling for the verse client
 *
 * Manages keyboard, mouse, and button input processing.
 */

#ifndef CLIENT_INPUT_H
#define CLIENT_INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>

// Button IDs (using different values to avoid conflicts)
#define CLIENT_BUTTON_NEW_GAME 100
#define CLIENT_BUTTON_CONTINUE 101
#define CLIENT_BUTTON_LOAD_GAME 102
#define CLIENT_BUTTON_SETTINGS 103
#define CLIENT_BUTTON_EXIT 104

// Initialize input system
void client_input_init(void);

// Input handlers
void handle_key_press(int key);
void handle_button_click(int button_id);
void handle_mouse_click(int x, int y, int button);
void handle_mouse_motion(int x, int y);
void handle_mouse_wheel(int x, int y, int delta);
void handle_text_input(const char* text);

// Check if we should exit
bool client_input_should_exit(void);

// Reset exit flag
void client_input_reset_exit(void);

#endif // CLIENT_INPUT_H
