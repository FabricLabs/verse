#include "item_icon.h"

#include <stdio.h>
#include <string.h>

#include "item_icon_data.inc"

static SDL_Renderer *s_renderer = NULL;
static SDL_Texture *s_textures[ITEM_ICON_COUNT];
static bool s_ready = false;

bool item_icon_init(SDL_Renderer *renderer)
{
    if (!renderer)
        return false;
    if (s_ready && s_renderer == renderer)
        return true;

    item_icon_shutdown();
    s_renderer = renderer;

    if (ITEM_ICON_COUNT < ITEM_COUNT)
    {
        printf("item_icon: atlas count %d < ITEM_COUNT %d\n", ITEM_ICON_COUNT, (int)ITEM_COUNT);
        return false;
    }

    for (int i = 0; i < ITEM_COUNT; i++)
    {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            (void *)s_item_icon_rgba[i], ITEM_ICON_SIZE, ITEM_ICON_SIZE, 32,
            ITEM_ICON_SIZE * 4, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
        {
            printf("item_icon: surface %d failed: %s\n", i, SDL_GetError());
            item_icon_shutdown();
            return false;
        }
        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (!tex)
        {
            printf("item_icon: texture %d failed: %s\n", i, SDL_GetError());
            item_icon_shutdown();
            return false;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
        s_textures[i] = tex;
    }

    s_ready = true;
    return true;
}

void item_icon_shutdown(void)
{
    for (int i = 0; i < ITEM_ICON_COUNT; i++)
    {
        if (s_textures[i])
        {
            SDL_DestroyTexture(s_textures[i]);
            s_textures[i] = NULL;
        }
    }
    s_renderer = NULL;
    s_ready = false;
}

SDL_Texture *item_icon_texture(ItemId id)
{
    if (!s_ready || id <= ITEM_NONE || id >= ITEM_COUNT)
        return NULL;
    return s_textures[id];
}

void item_icon_draw(SDL_Renderer *renderer, ItemId id, int x, int y, int size)
{
    if (!renderer || size <= 0)
        return;
    SDL_Texture *tex = item_icon_texture(id);
    if (!tex)
        return;
    SDL_Rect dst = {x, y, size, size};
    SDL_RenderCopy(renderer, tex, NULL, &dst);
}
