#ifndef VERSE_ITEM_ICON_H
#define VERSE_ITEM_ICON_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "item.h"

#define ITEM_ICON_PX 16

bool item_icon_init(SDL_Renderer *renderer);
void item_icon_shutdown(void);

// Texture for catalog id, or NULL for empty / unload.
SDL_Texture *item_icon_texture(ItemId id);

// Draw icon scaled into a square of `size` pixels (nearest-neighbour via SDL).
void item_icon_draw(SDL_Renderer *renderer, ItemId id, int x, int y, int size);

#endif // VERSE_ITEM_ICON_H
