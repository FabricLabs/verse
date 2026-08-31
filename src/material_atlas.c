#include "material_atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Row 0 column 0 is the white tile; templates start at row 1. Keeping it at the origin is a
// contract the greedy-merge paths rely on — see the header.
#define ATLAS_WHITE_ROW 0
#define ATLAS_MATERIAL_ROWS (MATERIAL_TEMPLATE_COUNT + 1)

// The live block: tiles rewritten every frame, one per per-voxel surface drawn. Eight to a row so
// a frame that draws only a handful of water faces uploads one row rather than the whole block.
#define ATLAS_LIVE_COLS 8
#define ATLAS_LIVE_ROWS 16
#define ATLAS_LIVE_CAPACITY (ATLAS_LIVE_COLS * ATLAS_LIVE_ROWS)

#define ATLAS_COLS ATLAS_LIVE_COLS
#define ATLAS_ROWS (ATLAS_MATERIAL_ROWS + ATLAS_LIVE_ROWS)
#define ATLAS_W (ATLAS_COLS * MATERIAL_FACE_SIZE)
#define ATLAS_H (ATLAS_ROWS * MATERIAL_FACE_SIZE)

struct MaterialAtlas
{
  SDL_Texture *texture;

  // CPU side of the live block, at full atlas width so an upload is one contiguous run of rows.
  uint32_t *live_staging;
  int live_claimed; // tiles handed out this frame
};

// Inset the UVs by half a texel.
//
// Without this, sampling at exactly the tile edge picks up the neighbouring tile — grass bleeding a
// line of bark along one side of every face — because the tiles share edges in one texture and the
// sampler interpolates across them.
static MaterialAtlasRect rect_for_cell(int col, int row)
{
  const float half_u = 0.5f / (float)ATLAS_W;
  const float half_v = 0.5f / (float)ATLAS_H;
  const float tile_u = (float)MATERIAL_FACE_SIZE / (float)ATLAS_W;
  const float tile_v = (float)MATERIAL_FACE_SIZE / (float)ATLAS_H;

  MaterialAtlasRect r;
  r.u0 = (float)col * tile_u + half_u;
  r.v0 = (float)row * tile_v + half_v;
  r.u1 = (float)(col + 1) * tile_u - half_u;
  r.v1 = (float)(row + 1) * tile_v - half_v;
  return r;
}

MaterialAtlas *material_atlas_create(SDL_Renderer *sdl)
{
  if (!sdl)
    return NULL;
  if (!material_worlds_init())
    return NULL;

  MaterialAtlas *atlas = (MaterialAtlas *)calloc(1, sizeof(MaterialAtlas));
  if (!atlas)
    return NULL;

  uint32_t *staging = (uint32_t *)calloc((size_t)ATLAS_W * ATLAS_H, sizeof(uint32_t));
  if (!staging)
  {
    free(atlas);
    return NULL;
  }

  // White tile: opaque, so a face pointing here renders its own colour unchanged.
  for (int v = 0; v < MATERIAL_FACE_SIZE; v++)
    for (int u = 0; u < MATERIAL_FACE_SIZE; u++)
      staging[(ATLAS_WHITE_ROW * MATERIAL_FACE_SIZE + v) * ATLAS_W + u] = 0xFFFFFFFFu;

  for (int kind = 0; kind < MATERIAL_TEMPLATE_COUNT; kind++)
  {
    const MaterialTemplate *t = material_worlds_get((MaterialTemplateKind)kind);
    if (!t || !t->baked)
      continue;
    const int row = kind + 1;
    for (int face = 0; face < MATERIAL_FACE_COUNT; face++)
    {
      const uint32_t *src = t->bake.texels[face];
      for (int v = 0; v < MATERIAL_FACE_SIZE; v++)
      {
        uint32_t *dst = &staging[(row * MATERIAL_FACE_SIZE + v) * ATLAS_W + face * MATERIAL_FACE_SIZE];
        memcpy(dst, &src[v * MATERIAL_FACE_SIZE], MATERIAL_FACE_SIZE * sizeof(uint32_t));
      }
    }
  }

  atlas->live_staging = (uint32_t *)calloc((size_t)ATLAS_W * ATLAS_LIVE_ROWS * MATERIAL_FACE_SIZE,
                                           sizeof(uint32_t));
  if (!atlas->live_staging)
  {
    free(staging);
    free(atlas);
    return NULL;
  }

  atlas->texture = SDL_CreateTexture(sdl, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                     ATLAS_W, ATLAS_H);
  if (!atlas->texture)
  {
    printf("material_atlas: texture creation failed: %s\n", SDL_GetError());
    free(staging);
    free(atlas->live_staging);
    free(atlas);
    return NULL;
  }

  if (SDL_UpdateTexture(atlas->texture, NULL, staging, ATLAS_W * (int)sizeof(uint32_t)) != 0)
  {
    printf("material_atlas: texture upload failed: %s\n", SDL_GetError());
    SDL_DestroyTexture(atlas->texture);
    free(staging);
    free(atlas->live_staging);
    free(atlas);
    return NULL;
  }

  // Leaves bake with transparent texels where the canopy has gaps, and those need to blend rather
  // than draw as black.
  SDL_SetTextureBlendMode(atlas->texture, SDL_BLENDMODE_BLEND);
  // Nearest sampling: the whole point is that a sub-voxel is a visible unit, and bilinear would
  // smear the grain into mush at the small tile sizes the isometric view uses.
  SDL_SetTextureScaleMode(atlas->texture, SDL_ScaleModeNearest);

  free(staging);
  printf("material_atlas: %dx%d atlas built for %d materials, %d live tiles\n", ATLAS_W, ATLAS_H,
         MATERIAL_TEMPLATE_COUNT, ATLAS_LIVE_CAPACITY);
  return atlas;
}

void material_atlas_destroy(MaterialAtlas *atlas)
{
  if (!atlas)
    return;
  if (atlas->texture)
    SDL_DestroyTexture(atlas->texture);
  free(atlas->live_staging);
  free(atlas);
}

SDL_Texture *material_atlas_texture(const MaterialAtlas *atlas)
{
  return atlas ? atlas->texture : NULL;
}

MaterialAtlasRect material_atlas_rect(const MaterialAtlas *atlas, VoxelType type,
                                      MaterialFace face, bool *has_detail)
{
  if (has_detail)
    *has_detail = false;

  if (!atlas || face < 0 || face >= MATERIAL_FACE_COUNT)
    return rect_for_cell(0, ATLAS_WHITE_ROW);

  const MaterialTemplate *t = material_worlds_for_voxel(type);
  if (!t)
    return rect_for_cell(0, ATLAS_WHITE_ROW);

  if (has_detail)
    *has_detail = true;
  return rect_for_cell((int)face, (int)t->kind + 1);
}

int material_atlas_live_capacity(const MaterialAtlas *atlas)
{
  return atlas ? ATLAS_LIVE_CAPACITY : 0;
}

void material_atlas_live_reset(MaterialAtlas *atlas)
{
  if (atlas)
    atlas->live_claimed = 0;
}

bool material_atlas_live_claim(MaterialAtlas *atlas, const uint32_t *texels,
                               MaterialAtlasRect *out_rect)
{
  if (!atlas || !atlas->live_staging || !texels || !out_rect)
    return false;
  if (atlas->live_claimed >= ATLAS_LIVE_CAPACITY)
    return false;

  const int slot = atlas->live_claimed++;
  const int col = slot % ATLAS_LIVE_COLS;
  const int row = slot / ATLAS_LIVE_COLS;

  for (int v = 0; v < MATERIAL_FACE_SIZE; v++)
  {
    uint32_t *dst = &atlas->live_staging[(size_t)(row * MATERIAL_FACE_SIZE + v) * ATLAS_W +
                                         (size_t)col * MATERIAL_FACE_SIZE];
    memcpy(dst, &texels[(size_t)v * MATERIAL_FACE_SIZE], MATERIAL_FACE_SIZE * sizeof(uint32_t));
  }

  *out_rect = rect_for_cell(col, ATLAS_MATERIAL_ROWS + row);
  return true;
}

void material_atlas_live_upload(MaterialAtlas *atlas)
{
  if (!atlas || !atlas->texture || !atlas->live_staging)
    return;

  // Only the rows that were filled. A frame drawing a dozen water faces touches one row of the
  // block; uploading all sixteen every frame would cost half a megabyte for nothing.
  const int rows_used = (atlas->live_claimed + ATLAS_LIVE_COLS - 1) / ATLAS_LIVE_COLS;
  if (rows_used == 0)
    return;

  const SDL_Rect region = {0, ATLAS_MATERIAL_ROWS * MATERIAL_FACE_SIZE, ATLAS_W,
                           rows_used * MATERIAL_FACE_SIZE};
  SDL_UpdateTexture(atlas->texture, &region, atlas->live_staging, ATLAS_W * (int)sizeof(uint32_t));
}
