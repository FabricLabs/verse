#ifndef VERSE_SPIRIT_SPRITE_H
#define VERSE_SPIRIT_SPRITE_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#define SPIRIT_SPRITE_SIZE   32
#define SPIRIT_CROSSHAIR_SIZE 16
#define SPIRIT_DEFAULT_TURN_SPEED 240.0f

bool spirit_sprite_init(SDL_Renderer *renderer);
void spirit_sprite_shutdown(void);

SDL_Texture *spirit_sprite_get_texture(void);
SDL_Texture *spirit_sprite_get_crosshair_texture(void);

void spirit_sprite_draw_crosshair(SDL_Renderer *renderer, int center_x, int center_y);
void spirit_sprite_draw_avatar(SDL_Renderer *renderer, int center_x, int center_y, float facing_yaw);
void spirit_sprite_draw_stamina_meter(SDL_Renderer *renderer, int center_x, int center_y,
                                      float stamina, float max_stamina, float alpha);
// Screen-pixel idle bob for the spirit HUD cluster (avatar + meters).
int spirit_sprite_hover_bob_px(void);

// Screen-space swipe fan around the spirit (iso HUD / optional FP overlay).
// progress 0..1 sweeps the blade across the arc; armed uses a longer, cooler-coloured fan.
void spirit_sprite_draw_swing(SDL_Renderer *renderer, int center_x, int center_y,
                              float facing_yaw, float progress, float arc_rad, bool armed,
                              bool isometric);

#endif // VERSE_SPIRIT_SPRITE_H
