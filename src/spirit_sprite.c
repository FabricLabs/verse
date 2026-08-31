#include "spirit_sprite.h"
#include "player_controls_types.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

static SDL_Texture *g_spirit_texture = NULL;
static SDL_Texture *g_crosshair_texture = NULL;

static SDL_Texture *create_texture_from_rgba(SDL_Renderer *renderer,
                                             const unsigned char *pixels,
                                             int width, int height)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void *)pixels, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return NULL;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!texture)
        return NULL;

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

static void draw_spirit_pixels(unsigned char *pixels, int width, int height)
{
    const float cx = (width - 1) * 0.5f;
    const float cy = (height - 1) * 0.5f;
    const float radius = (float)width * 0.42f;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            int idx = (y * width + x) * 4;

            if (dist > radius)
            {
                pixels[idx] = 0;
                pixels[idx + 1] = 0;
                pixels[idx + 2] = 0;
                pixels[idx + 3] = 0;
                continue;
            }

            float t = dist / radius;
            float core = 1.0f - t;
            core = core * core;

            unsigned char r = (unsigned char)(90 + core * 120);
            unsigned char g = (unsigned char)(170 + core * 70);
            unsigned char b = (unsigned char)(220 + core * 35);
            unsigned char a = (unsigned char)(40 + core * 200);

            if (dist < radius * 0.22f)
            {
                r = (unsigned char)(220 + core * 35);
                g = (unsigned char)(235 + core * 20);
                b = 255;
                a = (unsigned char)(120 + core * 135);
            }

            pixels[idx] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = a;
        }
    }
}

static void draw_crosshair_pixels(unsigned char *pixels, int width, int height)
{
    memset(pixels, 0, (size_t)width * (size_t)height * 4);

    const int cx = width / 2;
    const int cy = height / 2;
    const int gap = 2;
    const int arm = 5;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            bool on_cross = false;
            if (x == cx && (y <= cy - gap || y >= cy + gap))
                on_cross = true;
            if (y == cy && (x <= cx - gap || x >= cx + gap))
                on_cross = true;

            bool on_tip = false;
            if (x == cx && (y == cy - arm || y == cy + arm))
                on_tip = true;
            if (y == cy && (x == cx - arm || x == cx + arm))
                on_tip = true;

            bool center_dot = (x == cx && y == cy);

            if (!on_cross && !on_tip && !center_dot)
                continue;

            int idx = (y * width + x) * 4;
            if (center_dot || on_tip)
            {
                pixels[idx] = 255;
                pixels[idx + 1] = 255;
                pixels[idx + 2] = 255;
                pixels[idx + 3] = 255;
            }
            else
            {
                pixels[idx] = 255;
                pixels[idx + 1] = 210;
                pixels[idx + 2] = 70;
                pixels[idx + 3] = 230;
            }
        }
    }
}

bool spirit_sprite_init(SDL_Renderer *renderer)
{
    if (!renderer)
        return false;

    spirit_sprite_shutdown();

    unsigned char *spirit_pixels = calloc(SPIRIT_SPRITE_SIZE * SPIRIT_SPRITE_SIZE, 4);
    unsigned char *crosshair_pixels = calloc(SPIRIT_CROSSHAIR_SIZE * SPIRIT_CROSSHAIR_SIZE, 4);
    if (!spirit_pixels || !crosshair_pixels)
    {
        free(spirit_pixels);
        free(crosshair_pixels);
        return false;
    }

    draw_spirit_pixels(spirit_pixels, SPIRIT_SPRITE_SIZE, SPIRIT_SPRITE_SIZE);
    draw_crosshair_pixels(crosshair_pixels, SPIRIT_CROSSHAIR_SIZE, SPIRIT_CROSSHAIR_SIZE);

    g_spirit_texture = create_texture_from_rgba(renderer, spirit_pixels,
                                                SPIRIT_SPRITE_SIZE, SPIRIT_SPRITE_SIZE);
    g_crosshair_texture = create_texture_from_rgba(renderer, crosshair_pixels,
                                                   SPIRIT_CROSSHAIR_SIZE, SPIRIT_CROSSHAIR_SIZE);

    free(spirit_pixels);
    free(crosshair_pixels);

    return g_spirit_texture && g_crosshair_texture;
}

void spirit_sprite_shutdown(void)
{
    if (g_spirit_texture)
    {
        SDL_DestroyTexture(g_spirit_texture);
        g_spirit_texture = NULL;
    }
    if (g_crosshair_texture)
    {
        SDL_DestroyTexture(g_crosshair_texture);
        g_crosshair_texture = NULL;
    }
}

SDL_Texture *spirit_sprite_get_texture(void)
{
    return g_spirit_texture;
}

SDL_Texture *spirit_sprite_get_crosshair_texture(void)
{
    return g_crosshair_texture;
}

int spirit_sprite_hover_bob_px(void)
{
    // ~PLAYER_SPIRIT_HOVER_BOB_AMP voxels at default iso vh (16) → a few soft pixels.
    const float t = (float)SDL_GetTicks() * 0.001f;
    return (int)lroundf(sinf(t * (2.0f * (float)M_PI * PLAYER_SPIRIT_HOVER_BOB_HZ)) * 3.5f);
}

void spirit_sprite_draw_crosshair(SDL_Renderer *renderer, int center_x, int center_y)
{
    if (!renderer || !g_crosshair_texture)
        return;

    SDL_Rect dst = {
        center_x - SPIRIT_CROSSHAIR_SIZE / 2,
        center_y - SPIRIT_CROSSHAIR_SIZE / 2,
        SPIRIT_CROSSHAIR_SIZE,
        SPIRIT_CROSSHAIR_SIZE};
    SDL_RenderCopy(renderer, g_crosshair_texture, NULL, &dst);
}

void spirit_sprite_draw_avatar(SDL_Renderer *renderer, int center_x, int center_y, float facing_yaw)
{
    if (!renderer)
        return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (g_spirit_texture)
    {
        SDL_Rect dst = {
            center_x - SPIRIT_SPRITE_SIZE / 2,
            center_y - SPIRIT_SPRITE_SIZE / 2,
            SPIRIT_SPRITE_SIZE,
            SPIRIT_SPRITE_SIZE};
        SDL_RenderCopy(renderer, g_spirit_texture, NULL, &dst);
    }
    else
    {
        const int r = SPIRIT_SPRITE_SIZE / 2 - 2;
        for (int dy = -r; dy <= r; dy++)
        {
            int span = (int)sqrtf((float)(r * r - dy * dy));
            int shade = 180 + (r - abs(dy)) * 2;
            if (shade > 255)
                shade = 255;
            SDL_SetRenderDrawColor(renderer, (Uint8)(shade / 2), (Uint8)shade, 255, 220);
            SDL_RenderDrawLine(renderer, center_x - span, center_y + dy, center_x + span, center_y + dy);
        }
    }

    // Facing arrow in isometric screen space (same basis as world_to_screen).
    float dx = cosf(facing_yaw);
    float dy = sinf(facing_yaw);
    float sx = dx - dy;
    float sy = (dx + dy) * 0.5f;
    float len = sqrtf(sx * sx + sy * sy);
    if (len < 1e-4f)
        len = 1.0f;
    sx /= len;
    sy /= len;

    int arrow_len = 16;
    int tip_x = center_x + (int)(sx * (float)arrow_len);
    int tip_y = center_y + (int)(sy * (float)arrow_len);

    int back_len = arrow_len - 6;
    int back_x = center_x + (int)(sx * (float)back_len);
    int back_y = center_y + (int)(sy * (float)back_len);

    float px = -sy, py = sx; // perpendicular in screen space
    int wing = 5;
    int left_x = back_x + (int)(px * (float)wing);
    int left_y = back_y + (int)(py * (float)wing);
    int right_x = back_x - (int)(px * (float)wing);
    int right_y = back_y - (int)(py * (float)wing);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int o = -1; o <= 1; o++)
    {
        SDL_RenderDrawLine(renderer, center_x + o, center_y, tip_x + o, tip_y);
        SDL_RenderDrawLine(renderer, center_x, center_y + o, tip_x, tip_y + o);
    }
    SDL_RenderDrawLine(renderer, tip_x, tip_y, left_x, left_y);
    SDL_RenderDrawLine(renderer, tip_x, tip_y, right_x, right_y);
    SDL_RenderDrawLine(renderer, left_x, left_y, right_x, right_y);
}

void spirit_sprite_draw_stamina_meter(SDL_Renderer *renderer, int center_x, int center_y,
                                      float stamina, float max_stamina, float alpha)
{
    if (!renderer || max_stamina <= 0.0f || alpha <= 0.01f)
        return;

    if (alpha > 1.0f)
        alpha = 1.0f;
    const Uint8 a_bg = (Uint8)(180.0f * alpha);
    const Uint8 a_fg = (Uint8)(255.0f * alpha);

    float ratio = stamina / max_stamina;
    if (ratio < 0.0f)
        ratio = 0.0f;
    if (ratio > 1.0f)
        ratio = 1.0f;

    const int radius = SPIRIT_SPRITE_SIZE / 2 + 5;
    const int arc_steps = 24;
    const float start_angle = (float)M_PI * 0.75f;
    const float end_angle = (float)M_PI * 2.25f;
    const float filled_end = start_angle + (end_angle - start_angle) * ratio;

    SDL_SetRenderDrawColor(renderer, 60, 70, 90, a_bg);
    for (int i = 0; i < arc_steps; i++)
    {
        float t0 = start_angle + (end_angle - start_angle) * ((float)i / (float)arc_steps);
        float t1 = start_angle + (end_angle - start_angle) * ((float)(i + 1) / (float)arc_steps);
        int x0 = center_x + (int)(cosf(t0) * (float)radius);
        int y0 = center_y + (int)(sinf(t0) * (float)radius * 0.5f);
        int x1 = center_x + (int)(cosf(t1) * (float)radius);
        int y1 = center_y + (int)(sinf(t1) * (float)radius * 0.5f);
        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
    }

    if (ratio > 0.01f)
    {
        unsigned char r = (unsigned char)(80 + (1.0f - ratio) * 120.0f);
        unsigned char g = (unsigned char)(160 + ratio * 80.0f);
        SDL_SetRenderDrawColor(renderer, r, g, 255, a_fg);
        int filled_steps = (int)(arc_steps * ratio);
        if (filled_steps < 1)
            filled_steps = 1;
        for (int i = 0; i < filled_steps; i++)
        {
            float t0 = start_angle + (filled_end - start_angle) * ((float)i / (float)filled_steps);
            float t1 = start_angle + (filled_end - start_angle) * ((float)(i + 1) / (float)filled_steps);
            int x0 = center_x + (int)(cosf(t0) * (float)radius);
            int y0 = center_y + (int)(sinf(t0) * (float)radius * 0.5f);
            int x1 = center_x + (int)(cosf(t1) * (float)radius);
            int y1 = center_y + (int)(sinf(t1) * (float)radius * 0.5f);
            SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
            SDL_RenderDrawLine(renderer, x0, y0 + 1, x1, y1 + 1);
        }
    }
}

void spirit_sprite_draw_swing(SDL_Renderer *renderer, int center_x, int center_y,
                              float facing_yaw, float progress, float arc_rad, bool armed,
                              bool isometric)
{
    if (!renderer || progress <= 0.0f)
        return;

    if (progress > 1.0f)
        progress = 1.0f;
    if (arc_rad < 0.2f)
        arc_rad = 0.2f;

    // Schematic of the *full* damage sector (facing ± half_arc). Progress only drives brightness.
    const float half = arc_rad * 0.5f;
    const float y_scale = isometric ? 0.5f : 1.0f;
    const int inner_r = SPIRIT_SPRITE_SIZE / 2 + 2;
    const int outer_r = inner_r + (armed ? 16 : 12);

    float bright = 0.55f;
    if (progress >= PLAYER_SWING_HIT_START && progress <= PLAYER_SWING_HIT_END)
        bright = 1.0f;
    else if (progress < PLAYER_SWING_HIT_START)
        bright = 0.4f + 0.3f * (progress / PLAYER_SWING_HIT_START);
    else
        bright = 0.55f * (1.0f - (progress - PLAYER_SWING_HIT_END) /
                                      (1.0f - PLAYER_SWING_HIT_END));

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const int steps = 16;
    for (int i = 0; i <= steps; i++)
    {
        float t = -half + (2.0f * half) * ((float)i / (float)steps);
        float a = facing_yaw + t;
        float dx = cosf(a), dy = sinf(a);
        float sx, sy;
        if (isometric)
        {
            float ix = dx - dy, iy = (dx + dy) * y_scale;
            float l = sqrtf(ix * ix + iy * iy);
            if (l < 1e-4f)
                l = 1.0f;
            sx = ix / l;
            sy = iy / l;
        }
        else
        {
            sx = dx;
            sy = -dy;
        }

        Uint8 alpha = (Uint8)((armed ? 170.0f : 150.0f) * bright);
        if (armed)
            SDL_SetRenderDrawColor(renderer, 180, 220, 255, alpha);
        else
            SDL_SetRenderDrawColor(renderer, 255, 140, 60, alpha);

        int x0 = center_x + (int)(sx * (float)inner_r);
        int y0 = center_y + (int)(sy * (float)inner_r);
        int x1 = center_x + (int)(sx * (float)outer_r);
        int y1 = center_y + (int)(sy * (float)outer_r);
        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);

        if (i > 0)
        {
            float t0 = -half + (2.0f * half) * ((float)(i - 1) / (float)steps);
            float a0 = facing_yaw + t0;
            float dx0 = cosf(a0), dy0 = sinf(a0);
            float sx0, sy0;
            if (isometric)
            {
                float ix = dx0 - dy0, iy = (dx0 + dy0) * y_scale;
                float l = sqrtf(ix * ix + iy * iy);
                if (l < 1e-4f)
                    l = 1.0f;
                sx0 = ix / l;
                sy0 = iy / l;
            }
            else
            {
                sx0 = dx0;
                sy0 = -dy0;
            }
            int px0 = center_x + (int)(sx0 * (float)outer_r);
            int py0 = center_y + (int)(sy0 * (float)outer_r);
            SDL_RenderDrawLine(renderer, px0, py0, x1, y1);
            int qx0 = center_x + (int)(sx0 * (float)inner_r);
            int qy0 = center_y + (int)(sy0 * (float)inner_r);
            SDL_RenderDrawLine(renderer, qx0, qy0, x0, y0);
        }
    }
}
