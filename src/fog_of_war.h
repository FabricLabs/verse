#ifndef FOG_OF_WAR_H
#define FOG_OF_WAR_H

#include <stdint.h>
#include <stdbool.h>
#include "world.h"

struct ShadowWorld;

typedef struct FogAtlas FogAtlas;

FogAtlas *fog_atlas_create(void);
void fog_atlas_destroy(FogAtlas *atlas);
void fog_atlas_clear(FogAtlas *atlas);
uint64_t fog_atlas_revision(const FogAtlas *atlas);

// Number of allocated 16³ chunks (for tests and diagnostics).
size_t fog_atlas_chunk_count(const FogAtlas *atlas);

bool fog_is_explored(const FogAtlas *atlas, const World *w,
                     uint32_t x, uint32_t y, uint32_t z);
void fog_mark_explored(FogAtlas *atlas, const World *w,
                       uint32_t x, uint32_t y, uint32_t z);

// Reveal voxels visible from the player's eyes within a vertical FOV cone, with wall occlusion.
// eye_* are in the centre world's local coordinates; cluster provides cross-world LOS.
// Pass a cone slightly larger than the camera frustum so first-person pixels never hit the hide-mask.
// Expensive (thousands of rays) — do not call every frame unless play views hide unexplored voxels.
void fog_reveal_from_view(FogAtlas *atlas, const struct ShadowWorld *cluster,
                          float eye_x, float eye_y, float eye_z,
                          float yaw, float pitch, float fov_deg_v,
                          float aspect, float max_range);

// Cheap map exploration: mark a sphere around the player across the loaded cluster. Early-outs
// when the player has not moved. Suitable for every gameplay frame when FoW is map-only.
void fog_reveal_around(FogAtlas *atlas, const struct ShadowWorld *cluster,
                       float px, float py, float pz, int radius);

#endif // FOG_OF_WAR_H
