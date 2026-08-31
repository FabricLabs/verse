#ifndef MOB_SPRITE_H
#define MOB_SPRITE_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "mob_models.h"

// Isometric sprite bake for a mob model. Cached per (kind, tile_width) so a zoom
// change rebuilds and a frame of walking does not. SDL-only; the nested world
// lives in mob_models.

void mob_sprite_shutdown(void);

// Draw the model with its feet on (feet_sx, feet_sy). ColorMod applies `cr,cg,cb`
// as a multiply (255,255,255 is identity). On success fills *out_dest with the
// blit rectangle so the caller can draw a rim or a dialogue bubble.
bool mob_sprite_draw(SDL_Renderer *sdl, const MobModel *model,
                     int tile_width, int tile_height, int voxel_height,
                     int feet_sx, int feet_sy,
                     uint8_t cr, uint8_t cg, uint8_t cb,
                     SDL_Rect *out_dest);

#endif // MOB_SPRITE_H
