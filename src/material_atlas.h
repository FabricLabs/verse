#ifndef MATERIAL_ATLAS_H
#define MATERIAL_ATLAS_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "material_worlds.h"
#include "voxel.h"

// The GPU side of sub-voxel rendering: every material template's baked faces packed into one
// texture, so the renderer can draw voxel faces as textured quads and still submit the whole frame
// in a single SDL_RenderGeometry call.
//
// This is deliberately separate from material_worlds.c, which stays free of SDL so the template
// generation and baking can be tested headlessly.
//
// Layout is one row per template plus a leading row holding a single opaque white tile. The white
// tile is what keeps the frame down to one draw call: a material with no template yet points its
// UVs there, so its face comes out exactly as it did before texturing — the texture multiplies its
// colour by white — and does not have to be batched separately.
//
// The white tile is guaranteed to sit at the atlas origin, so UV (0,0) always samples opaque white.
// The greedy-merge paths depend on that: their quads span runs of voxels, which a single 32x32 tile
// cannot cover without texture wrapping that SDL2 does not expose, so they stay flat-shaded and
// leave their tex_coords at zero. Do not move the white tile off the origin.
//
// Below the material rows sits a block of tiles that are rewritten every frame, for surfaces that
// are not the same on every voxel of their material: water, whose top face carries a live wave
// field per voxel (see fluid_surface.h), and steam/gas fog volumes projected onto their visible
// faces (see fog_volume.h). A frame claims one tile per live surface it draws and uploads only the
// rows it filled.

typedef struct MaterialAtlas MaterialAtlas;

typedef struct
{
  float u0, v0, u1, v1; // normalised, ready for SDL_Vertex.tex_coord
} MaterialAtlasRect;

// Build the atlas. Calls material_worlds_init if needed. Returns NULL if the texture cannot be
// created, in which case the caller should carry on drawing flat faces.
MaterialAtlas *material_atlas_create(SDL_Renderer *sdl);
void material_atlas_destroy(MaterialAtlas *atlas);

SDL_Texture *material_atlas_texture(const MaterialAtlas *atlas);

// UVs for one face of one material. When the material has no template this returns the white tile
// and sets *has_detail to false; the caller should then keep the face's existing flat colour
// rather than letting the texture supply it.
MaterialAtlasRect material_atlas_rect(const MaterialAtlas *atlas, VoxelType type,
                                      MaterialFace face, bool *has_detail);

// How many per-voxel surfaces one frame can carry.
int material_atlas_live_capacity(const MaterialAtlas *atlas);

// Release last frame's per-voxel tiles. Call once before drawing.
void material_atlas_live_reset(MaterialAtlas *atlas);

// Copy one 32x32 ARGB8888 tile into the live block and return where it landed. False when the
// frame's tiles are used up, in which case the caller should fall back to a flat face.
bool material_atlas_live_claim(MaterialAtlas *atlas, const uint32_t *texels,
                               MaterialAtlasRect *out_rect);

// Push this frame's claimed tiles to the GPU. Call after the last claim and before drawing.
void material_atlas_live_upload(MaterialAtlas *atlas);

#endif // MATERIAL_ATLAS_H
