#ifndef FP_RENDERER_H
#define FP_RENDERER_H

#include <SDL2/SDL.h>
#include "constants.h"
#include "world.h"
#include "voxel_mesh.h"
#include "octree.h"
#include "projectile.h"
#include "debris.h"
#include "voxel_debris_volume.h"
#include "foliage_bend.h"

struct FogAtlas;

typedef struct {
  float x, y, z;     // camera position in world space
  float yaw, pitch;  // radians; yaw around +Z (0 = +X), pitch up positive
  float roll;        // radians; bank around look axis (positive = right wing down)
  float fov_deg;     // vertical field of view in degrees
} FPCamera;

// Atmospheric fog is the first-person view limiter. Near voxels stay unfogged so fidelity holds;
// geometry past FP_FOG_END is culled rather than drawn as a black fog-of-war wall. Reveal uses a
// slightly larger cone and range so the hide-mask stays behind this wall.
//
// End is three world widths so a sightline from near one edge of the player's world can cross the
// current world, the neighbour, and one more beyond — matching the radius-2 shadow cluster.
#define FP_CAMERA_FOV_DEG 60.0f
#define FP_FOG_START ((float)WORLD_SIZE_X)
#define FP_FOG_END ((float)(3 * WORLD_SIZE_X))
#define FP_VIEW_DISTANCE FP_FOG_END
#define FP_FOG_REVEAL_FOV_SCALE 1.2f
#define FP_FOG_REVEAL_RANGE (FP_FOG_END + (float)WORLD_SIZE_X)

// Last-frame material / sparse-silhouette costs for gameplay framerate benches.
typedef struct
{
  double sparse_scan_ms;   // walk nearby cells looking for sparse parents
  double sparse_draw_ms;   // raster nested template meshes
  int sparse_candidates;   // sparse cells inside the scan box
  int sparse_drawn;        // instances actually rasterised
  int sparse_quads;        // quads submitted across those instances
} FPMaterialFrameStats;

// Rendering mode for first-person renderer
typedef enum {
  FP_MODE_RAY = 0,   // per-pixel ray cast (reference-correct)
  FP_MODE_MESH = 1   // mesh-driven raster (optimized)
} FPMode;

// Set FP renderer mode (affects fp_renderer_render)
void fp_renderer_set_mode(FPMode mode);

// Toggle GPU triangle submission for mesh mode (default off for parity).
void fp_renderer_enable_gpu_mesh(bool enable);
// Initialize OpenGL resources for FP mesh rendering (FBO/depth); safe to call multiple times
void fp_renderer_gl_init(void);

// Release the cached texture belonging to the renderer we last drew through. Must be called before
// SDL_DestroyRenderer, or the next renderer — which the allocator may place at the same address —
// looks like a cache hit and inherits a dead texture.
void fp_renderer_release_renderer(void);

// Hand the renderer the projectiles to overlay on the frame, depth-tested against the world. A
// borrowed pointer, expected to stay valid for the frames it is set for; NULL clears it.
//
// A setter rather than another render parameter because there are four render entry points and all of
// them would have had to grow one, which is also how mode and GPU submission are handled here.
void fp_renderer_set_projectiles(const ProjectileSystem *projectiles);

// Occupancy sun + AO lighting on the cluster mesh path. Default off so the audit hashes stay
// stable; the live game turns it on. FP_LIGHTING=0 in the environment forces it back off.
void fp_renderer_set_lighting(bool enable);

// Upload world occupancy to GL_TEXTURE_3D for a future fullscreen GPU lighting pass.
// Default off — live lighting is CPU/ShadowWorld; enabling without a consumer is wasted bandwidth.
// FP_GPU_OCCUPANCY=1 turns it on from the live window.
void fp_renderer_set_gpu_occupancy(bool enable);

// Broken voxel pieces on the ground. Same ownership story as projectiles.
void fp_renderer_set_debris(const DebrisSystem *debris);

// Pass-2 falling islands (transformed voxel volumes). NULL clears.
void fp_renderer_set_debris_volumes(const DebrisVolumeSystem *volumes);

// Melee overlay for the current frame. progress 0 clears it.
// strength scales the unarmed fist/arm pill thickness; ignored when armed.
void fp_renderer_set_swing(float yaw, float progress, float radius, float arc_rad, bool armed,
                           uint32_t strength);

// Bodies that shear foliage this frame. Borrowed pointer; NULL clears.
void fp_renderer_set_foliage_bend(const FoliageBendField *field);

// Force nested material detail on/off for A/B benches. -1 restores VERSE_SUBVOXEL env behaviour.
void fp_renderer_set_subvoxel_enabled(int enabled);

// Stats from the most recent mesh/cluster frame's sparse silhouette pass (zeroed each draw).
void fp_renderer_clear_material_frame_stats(void);
const FPMaterialFrameStats *fp_renderer_material_frame_stats(void);

// Invalidate mesh cache (call when world changes significantly)
void fp_renderer_invalidate_cache(void);

// Reset warm-up state (useful for testing)
void fp_renderer_reset_warmup(void);

// Set a pre-built mesh in the cache (useful for forcing fresh mesh usage)
void fp_renderer_set_cached_mesh(const World *world, const VoxelMesh *mesh);

// Octree spatial acceleration functions
void fp_renderer_set_world_octree(const World *world, Octree *octree);
Octree* fp_renderer_get_world_octree(const World *world);
void fp_renderer_build_world_octree(const World *world);

// Render a simple first-person view into the given SDL rectangle.
// Uses CPU ray casting against the voxel grid.
void fp_renderer_render(SDL_Renderer *ren,
                        const World *world,
                        const FPCamera *cam,
                        int panel_x, int panel_y, int panel_w, int panel_h,
                        const struct FogAtlas *fog);

// Camera/geometry internals exposed for the headless audit in fp_renderer_audit_test.c. The
// projection and the pixel-ray generator must stay exact inverses of each other, and quads must
// stay planar; both are easy to break silently, so they are testable rather than private.
void fp_renderer_debug_quad_corners(const VoxelFaceQuad *quad, float corners[4][3]);

bool fp_renderer_debug_project(const FPCamera *cam, int target_w, int target_h,
                               float wx, float wy, float wz,
                               float *screen_x, float *screen_y, float *depth);

void fp_renderer_debug_pixel_ray(const FPCamera *cam, int target_w, int target_h,
                                 float screen_x, float screen_y,
                                 float *dir_x, float *dir_y, float *dir_z);

// Render first-person view including neighbor worlds around the home world.
// neighbor_radius: 0 = only home, 1 = immediate neighbors (3x3x3 cube), 2 = include next ring (if available).
// Uses simple per-pixel nearest-hit selection across included worlds.
void fp_renderer_render_neighbors(SDL_Renderer *ren,
                                  const GameWorlds *game_worlds,
                                  const FPCamera *cam,
                                  int panel_x, int panel_y, int panel_w, int panel_h,
                                  int neighbor_radius,
                                  const struct FogAtlas *fog);

struct ShadowWorld;

// Render a first-person view of the radius-2 shadow cluster: the world the camera stands in, drawn
// from its greedy mesh, plus whichever surrounding worlds have streamed in (up to the 5x5x5).
//
// The camera is in the centre world's own coordinates. Neighbours cost only the pixels the centre
// world leaves as sky, so a view of a hillside pays almost nothing for them and a view over a world
// boundary pays for the horizon it actually shows.
void fp_renderer_render_cluster(SDL_Renderer *ren,
                                const struct ShadowWorld *cluster,
                                const FPCamera *cam,
                                int panel_x, int panel_y, int panel_w, int panel_h,
                                const struct FogAtlas *fog);

#endif // FP_RENDERER_H
