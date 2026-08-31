#ifndef VERSE_CONSOLE_H
#define VERSE_CONSOLE_H

#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// In-game developer console (tilde / backtick). When open it captures keyboard input so
// gameplay controls do not fire; when closed it is invisible and costs nothing.

#define CONSOLE_INPUT_MAX 256
#define CONSOLE_HISTORY_LINES 32
#define CONSOLE_HISTORY_LINE_MAX 160

typedef struct Console Console;

Console *console_create(void);
void console_destroy(Console *console);

bool console_is_open(const Console *console);
void console_toggle(Console *console);
void console_close(Console *console);

// Returns true if the key was consumed (caller should not forward it to gameplay).
bool console_handle_key(Console *console, SDL_Keycode key);

// Append UTF-8 text from SDL_TEXTINPUT while the console is open.
void console_handle_text(Console *console, const char *text);

// Draw the console overlay into the current SDL render target (base resolution).
void console_render(Console *console, SDL_Renderer *renderer, TTF_Font *font,
                    int screen_w, int screen_h);

// Debug render flags driven by console commands (read by the isometric renderer).
bool console_wireframe_enabled(void);
bool console_xray_enabled(void);

// Fog of war on the world map (M). Play views are unaffected. Toggle with `fog`.
bool console_fog_enabled(void);

#endif // VERSE_CONSOLE_H
