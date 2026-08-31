#ifndef SONG_EDITOR_STANDALONE_H
#define SONG_EDITOR_STANDALONE_H

#include <SDL2/SDL.h>
#include <stdbool.h>

// Window functions for standalone editor
int window_init(const char* title, int width, int height);
void window_cleanup();
void window_clear();
void window_present();
void window_render_text(const char* text, int x, int y, SDL_Color color);
void window_render_rect(int x, int y, int width, int height, SDL_Color color);
void window_render_line(int x1, int y1, int x2, int y2, SDL_Color color);
bool window_setup_audio();
void window_cleanup_audio();
int window_handle_events();

#endif // SONG_EDITOR_STANDALONE_H
