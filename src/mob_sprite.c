#include "mob_sprite.h"

#include "world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  SDL_Texture *tex;
  int tile_width;
  int foot_x, foot_y;
  int w, h;
} MobSpriteCache;

static MobSpriteCache g_cache[MOB_MODEL_COUNT];

void mob_sprite_shutdown(void)
{
  for (int i = 0; i < MOB_MODEL_COUNT; i++)
  {
    if (g_cache[i].tex)
    {
      SDL_DestroyTexture(g_cache[i].tex);
      g_cache[i].tex = NULL;
    }
    g_cache[i].tile_width = 0;
  }
}

static uint32_t pack_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
  return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void put_px(uint32_t *px, int w, int h, int x, int y, uint32_t c)
{
  if (x < 0 || y < 0 || x >= w || y >= h)
    return;
  px[y * w + x] = c;
}

// Barycentric fill. Degenerate (sub-pixel) triangles are skipped; the caller
// stamps a centre pixel so the silhouette survives at low zoom.
static void fill_tri(uint32_t *px, int w, int h,
                     float x0, float y0, float x1, float y1, float x2, float y2,
                     uint32_t color)
{
  const int minx = (int)floorf(fminf(x0, fminf(x1, x2)));
  const int maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
  const int miny = (int)floorf(fminf(y0, fminf(y1, y2)));
  const int maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
  const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (fabsf(area) < 0.25f)
    return;
  const float inv = 1.0f / area;
  for (int y = miny; y <= maxy; y++)
  {
    for (int x = minx; x <= maxx; x++)
    {
      const float pxp = (float)x + 0.5f;
      const float pyp = (float)y + 0.5f;
      const float w0 = ((x1 - pxp) * (y2 - pyp) - (x2 - pxp) * (y1 - pyp)) * inv;
      const float w1 = ((x2 - pxp) * (y0 - pyp) - (x0 - pxp) * (y2 - pyp)) * inv;
      const float w2 = ((x0 - pxp) * (y1 - pyp) - (x1 - pxp) * (y0 - pyp)) * inv;
      if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
        put_px(px, w, h, x, y, color);
    }
  }
}

static void fill_quad(uint32_t *px, int w, int h,
                      float x0, float y0, float x1, float y1,
                      float x2, float y2, float x3, float y3,
                      uint32_t color)
{
  fill_tri(px, w, h, x0, y0, x1, y1, x2, y2, color);
  fill_tri(px, w, h, x0, y0, x2, y2, x3, y3, color);
}

static bool neighbor_air(const World *w, int x, int y, int z)
{
  if (!world_pos_in_bounds_fast(w, x, y, z))
    return true;
  return world_voxel_cptr_fast(w, x, y, z)->type == VOXEL_AIR;
}

static uint32_t shaded(VoxelType type, float shade)
{
  uint8_t r, g, b;
  world_voxel_type_color(type, &r, &g, &b);
  return pack_argb(255,
                   (uint8_t)((float)r * shade),
                   (uint8_t)((float)g * shade),
                   (uint8_t)((float)b * shade));
}

static bool bake(SDL_Renderer *sdl, const MobModel *model, int tile_width,
                 int tile_height, int voxel_height, MobSpriteCache *out)
{
  const World *w = model->world;
  const float a = (float)tile_width * 0.5f / (float)MOB_MODEL_SUBVOXELS;
  const float b = (float)tile_height * 0.5f / (float)MOB_MODEL_SUBVOXELS;
  const float vh = (float)voxel_height / (float)MOB_MODEL_SUBVOXELS;
  const int sx = model->section_x;
  const int sy = model->section_y;
  const int sz = model->section_z;

  float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
  for (int y = 0; y < sy; y++)
    for (int x = 0; x < sx; x++)
      for (int z = 0; z < sz; z++)
      {
        const Voxel *v = world_voxel_cptr_fast(w, x, y, z);
        if (!v || v->type == VOXEL_AIR)
          continue;
        const float ox = ((float)x - (float)y) * a;
        const float oy = ((float)x + (float)y) * b - (float)z * vh;
        const float xs[6] = {ox, ox + a, ox - a, ox, ox + a, ox - a};
        const float ys[6] = {oy - b, oy, oy, oy + b + vh, oy + vh, oy + vh};
        for (int i = 0; i < 6; i++)
        {
          if (xs[i] < min_x) min_x = xs[i];
          if (xs[i] > max_x) max_x = xs[i];
          if (ys[i] < min_y) min_y = ys[i];
          if (ys[i] > max_y) max_y = ys[i];
        }
      }
  if (max_x < min_x)
    return false;

  const int pad = 2;
  const int img_w = (int)ceilf(max_x - min_x) + pad * 2 + 1;
  const int img_h = (int)ceilf(max_y - min_y) + pad * 2 + 1;
  if (img_w <= 0 || img_h <= 0 || img_w > 2048 || img_h > 2048)
    return false;

  uint32_t *px = (uint32_t *)calloc((size_t)img_w * (size_t)img_h, sizeof(uint32_t));
  if (!px)
    return false;

  const float ox0 = min_x - (float)pad;
  const float oy0 = min_y - (float)pad;

  // Painter's order matches the isometric terrain path: y-major, then x, then z
  // ascending so higher voxels cover lower ones.
  for (int y = 0; y < sy; y++)
  {
    for (int x = 0; x < sx; x++)
    {
      for (int z = 0; z < sz; z++)
      {
        const Voxel *v = world_voxel_cptr_fast(w, x, y, z);
        if (!v || v->type == VOXEL_AIR)
          continue;
        const float ox = ((float)x - (float)y) * a - ox0;
        const float oy = ((float)x + (float)y) * b - (float)z * vh - oy0;
        const VoxelType t = v->type;

        if (neighbor_air(w, x + 1, y, z))
          fill_quad(px, img_w, img_h,
                    ox + a, oy, ox + a, oy + vh,
                    ox, oy + b + vh, ox, oy + b,
                    shaded(t, 0.8f));
        if (neighbor_air(w, x, y + 1, z))
          fill_quad(px, img_w, img_h,
                    ox - a, oy, ox, oy + b,
                    ox, oy + b + vh, ox - a, oy + vh,
                    shaded(t, 0.6f));
        if (neighbor_air(w, x, y, z + 1))
          fill_quad(px, img_w, img_h,
                    ox, oy - b, ox + a, oy,
                    ox, oy + b, ox - a, oy,
                    shaded(t, 1.0f));
        put_px(px, img_w, img_h, (int)floorf(ox), (int)floorf(oy), shaded(t, 1.0f));
      }
    }
  }

  SDL_Texture *tex = SDL_CreateTexture(sdl, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC, img_w, img_h);
  if (!tex)
  {
    free(px);
    return false;
  }
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  if (SDL_UpdateTexture(tex, NULL, px, img_w * (int)sizeof(uint32_t)) != 0)
  {
    SDL_DestroyTexture(tex);
    free(px);
    return false;
  }
  free(px);

  const float fx = (float)sx * 0.5f;
  const float fy = (float)sy * 0.5f;
  const float foot_sx = (fx - fy) * a - ox0;
  const float foot_sy = (fx + fy) * b - oy0;

  if (out->tex)
    SDL_DestroyTexture(out->tex);
  out->tex = tex;
  out->tile_width = tile_width;
  out->w = img_w;
  out->h = img_h;
  out->foot_x = (int)lroundf(foot_sx);
  out->foot_y = (int)lroundf(foot_sy);
  return true;
}

bool mob_sprite_draw(SDL_Renderer *sdl, const MobModel *model,
                     int tile_width, int tile_height, int voxel_height,
                     int feet_sx, int feet_sy,
                     uint8_t cr, uint8_t cg, uint8_t cb,
                     SDL_Rect *out_dest)
{
  if (!sdl || !model || !model->world || tile_width <= 0)
    return false;
  if (model->kind < 0 || model->kind >= MOB_MODEL_COUNT)
    return false;

  MobSpriteCache *slot = &g_cache[model->kind];
  if (!slot->tex || slot->tile_width != tile_width)
  {
    if (!bake(sdl, model, tile_width, tile_height, voxel_height, slot))
      return false;
  }

  SDL_Rect dest = {feet_sx - slot->foot_x, feet_sy - slot->foot_y, slot->w, slot->h};
  SDL_SetTextureAlphaMod(slot->tex, 255);
  // Dark silhouette first so the figure reads against grass and sky.
  SDL_SetTextureColorMod(slot->tex, 16, 12, 10);
  const int ox[4] = {-1, 1, 0, 0};
  const int oy[4] = {0, 0, -1, 1};
  for (int i = 0; i < 4; i++)
  {
    SDL_Rect rim = {dest.x + ox[i], dest.y + oy[i], dest.w, dest.h};
    SDL_RenderCopy(sdl, slot->tex, NULL, &rim);
  }
  SDL_SetTextureColorMod(slot->tex, cr, cg, cb);
  const bool ok = SDL_RenderCopy(sdl, slot->tex, NULL, &dest) == 0;
  SDL_SetTextureColorMod(slot->tex, 255, 255, 255);
  if (ok && out_dest)
    *out_dest = dest;
  return ok;
}
