#include "fp_renderer.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "voxel_render_common.h"
#include "isometric_renderer.h"
#include "material_worlds.h"
#include "voxel_shape.h"
#include "foliage_bend.h"
#include "mob_models.h"
#include "poly_mesh.h"
#include "shadow_world.h"
#include "fog_of_war.h"
#include "player_controls_types.h"
#include "voxel_mesh.h"
#include "voxel_combat.h"
#include "debris.h"
#include "voxel_debris_volume.h"
#include "particle_effects.h"
#include "actor.h"
#include "mob_ai.h"
#include "gl_occupancy.h"
#include <SDL2/SDL_opengl.h>

typedef struct
{
  Uint32 *color;
  float *depth;
  int stride;
  int w, h;
} FPFrameBuffer;

#define FP_DEPTH_FAR 1e30f

// Forward declarations
static void render_mesh_cpu(SDL_Renderer *ren, const World *world, const VoxelMesh *mesh,
                            const FPCamera *cam, int panel_x, int panel_y, int panel_w, int panel_h,
                            const FogAtlas *fog);
static void render_mesh_gpu_ideal_opt(SDL_Renderer *ren, const World *world, const VoxelMesh *mesh,
                                      const FPCamera *cam, int panel_x, int panel_y, int panel_w, int panel_h);
static void render_raycast_fallback(SDL_Renderer *ren, const World *world, const FPCamera *cam,
                                    int panel_x, int panel_y, int panel_w, int panel_h,
                                    const FogAtlas *fog, const ShadowWorld *cluster);
static inline bool in_radius_cube(int dx, int dy, int dz, int r);
static bool validate_mesh_for_rendering(const VoxelMesh *mesh, const World *world, const char *caller);
static uint8_t fp_light_at(const ShadowWorld *sw, const World *world, int cx, int cy, int cz);
static uint8_t fp_light_voxel(const ShadowWorld *sw, int cx, int cy, int cz);
static uint8_t fp_cached_light(const ShadowWorld *cluster, const World *world, uint32_t key, int cx,
                               int cy, int cz);
static float fp_actor_shadow_at(float wx, float wy, float wz);
static float fp_actor_shadow_smooth(uint32_t key, float wx, float wy, float wz);
static void fp_begin_deferred_fog(const FPCamera *cam);

// Mesh cache structure — full detail plus a stride-2 LOD for mid-distance (Devlog #9/#16).
// When subvoxels are on, sparse parents are omitted from the mesh and listed here so the
// silhouette pass walks tens of cells instead of a 100³ neighbourhood every frame.
typedef struct
{
  int16_t x, y, z;
  uint16_t type;
  uint8_t shape_orient; // packed VoxelShape × orient (0 = full)
  uint8_t yaw_u8;       // decoration yaw (0..255 → 0..2π) for nested re-projection
} FPSparseCell;

typedef struct
{
  FPSparseCell *cells;
  int count;
  int capacity;
} FPSparseCellList;

typedef struct
{
  VoxelMesh mesh;
  VoxelMesh mesh_lod;
  FPSparseCellList sparse;
  bool lod_valid;
  const World *world;
  uint64_t world_hash; // Content token of the world the mesh was built from
  bool valid;
  bool provisional; // true = LOD stand-in; full remesh still owed on the warm path
  Uint64 last_used;
  Uint64 built_at;
} CachedMesh;

// Global state
static FPMode s_fp_mode = FP_MODE_RAY;
static bool s_fp_gpu_mesh_enabled = false;
static SDL_Renderer *s_last_renderer = NULL;
// Borrowed from the game state, for the overlay pass. NULL in the tools and the tests, which have no
// player and nothing in flight.
static const ProjectileSystem *s_fp_projectiles = NULL;
static const DebrisSystem *s_fp_debris = NULL;
static const DebrisVolumeSystem *s_fp_debris_volumes = NULL;
static const FoliageBendField *s_fp_foliage_bend = NULL;

static float s_fp_swing_yaw = 0.0f;
static float s_fp_swing_progress = 0.0f;
static float s_fp_swing_radius = PLAYER_SWING_RADIUS;
static float s_fp_swing_arc = PLAYER_SWING_ARC_RAD;
static bool s_fp_swing_armed = false;
static uint32_t s_fp_swing_strength = 10;

static SDL_Texture *s_fp_tex = NULL;
static int s_fp_tex_w = 0, s_fp_tex_h = 0;
static Uint32 *s_fp_locked_pixels = NULL;
static int s_fp_locked_stride = 0;
static Uint64 s_fp_frame_t0 = 0;

// Mesh cache: active centre + one parked neighbour. Crossing a world boundary and stepping back
// is free when the previous centre is still parked; ring-1 LODs can also promote into active.
static CachedMesh s_mesh_cache = {0};
static CachedMesh s_mesh_cache_prev = {0};
static double s_mesh_build_ms = 0.0;   // Cost of the last rebuild, used to throttle the next one
static bool s_cache_warmed_up = false; // Track if cache has been warmed up
static bool s_mesh_needs_full = false; // Provisional centre mesh awaiting a full remesh
static VoxelMesh s_mob_mesh[MOB_MODEL_COUNT];
static bool s_mob_mesh_valid[MOB_MODEL_COUNT];
// Per sparse template: full detail, stride-2, stride-4 (distance LOD for nested silhouettes).
#define FP_TEMPLATE_LOD_COUNT 3
static VoxelMesh s_template_mesh[MATERIAL_TEMPLATE_COUNT][FP_TEMPLATE_LOD_COUNT];
static bool s_template_mesh_valid[MATERIAL_TEMPLATE_COUNT];

// On-demand nested meshes for shape×orient modifiers on any VoxelType (wedge brick, etc.).
#define FP_SHAPE_MESH_CACHE_CAP 48
typedef struct
{
  uint16_t type;
  uint8_t shape_orient;
  uint8_t lod;
  VoxelMesh mesh;
  bool valid;
  Uint64 last_used;
} FPShapeMeshEntry;
static FPShapeMeshEntry s_shape_mesh_cache[FP_SHAPE_MESH_CACHE_CAP];

// Nested silhouette range + caps. Foliage is dense on home islands; fittings are rarer but need
// longer reach for roofs on the horizon. Caps keep a crowded grove from submitting millions of
// subvoxel quads in one frame — nearest instances always win.
#define FP_SPARSE_FOLIAGE_DIST 20.0f
#define FP_SPARSE_FITTING_DIST 48.0f
#define FP_SPARSE_FOLIAGE_FULL 10.0f
#define FP_SPARSE_FOLIAGE_LOD2 15.0f
#define FP_SPARSE_FITTING_FULL 18.0f
#define FP_SPARSE_FITTING_LOD2 32.0f
#define FP_SPARSE_NEAR_FORCE 6.0f
#define FP_SPARSE_NEAR_FOLIAGE_CAP 14
#define FP_SPARSE_FOLIAGE_CAP 64
#define FP_SPARSE_FITTING_CAP 96
#define FP_SPARSE_QUAD_BUDGET 160000

// Closest-ring (Chebyshev-1) neighbour meshes. Cap covers all 26 ring-1 slots so faces, edges, and
// corners can warm without thrashing. Builds never run inside the raster — they are amortized after
// present. Target quality matches the centre world (full greedy); a coarse lattice fill appears
// first so the horizon is never empty while full remeshes drain one slot at a time.
#define FP_RING1_CACHE_CAP 26
typedef struct
{
  VoxelMesh mesh_lod;
  FPSparseCellList sparse;
  const World *world;
  uint64_t world_hash;
  int shadow_slot; // cluster slot this entry was built for
  bool valid;
  bool full_quality; // false = coarse stand-in; true = same meshing as the centre world
  Uint64 last_used;
  Uint64 built_at;
} Ring1CachedMesh;
static Ring1CachedMesh s_ring1_cache[FP_RING1_CACHE_CAP];
static double s_ring1_build_ms = 0.0;
static Uint64 s_ring1_next_warm_ms = 0;
static Uint64 s_ring1_last_warm_tick = 0;
// When false, skip closest-ring mesh raster (env VERSE_FP_RING1=0). Used for A/B profiling.
static int s_fp_ring1_enabled = -1; // -1 = unread

static bool fp_ring1_enabled(void)
{
  if (s_fp_ring1_enabled < 0)
  {
    const char *e = getenv("VERSE_FP_RING1");
    s_fp_ring1_enabled = !(e && e[0] == '0');
  }
  return s_fp_ring1_enabled != 0;
}

// Frame must be this comfortable before we spend a full (~100ms) neighbour remesh after present.
static const double FP_RING1_FULL_WARM_MS = 10.0;

typedef struct
{
  double centre_ms;
  double ring1_ms;
  double neighbour_ms;
  double lighting_ms;
  double overlay_ms;
  double present_ms;
  int ring1_slots;
  int ring1_quads;
  int ring1_builds;
} FPClusterProfile;

static FPClusterProfile s_fp_cluster_profile;
static int s_fp_profile_enabled = -1;

static bool fp_profile_enabled(void)
{
  if (s_fp_profile_enabled < 0)
  {
    const char *e = getenv("VERSE_FP_PROFILE");
    s_fp_profile_enabled = (e && e[0] && e[0] != '0');
  }
  return s_fp_profile_enabled != 0;
}

static double fp_now_ms(void)
{
  return 1000.0 * (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

// Dynamic resolution (Devlog #17): scale the FP buffer down when the last frame was slow.
static float s_dyn_res_scale = 1.0f;
static double s_last_frame_ms = 0.0;
static const double FP_DYN_RES_BUDGET_MS = 22.0;
static const double FP_DYN_RES_RECOVER_MS = 14.0;

// When set, closest-ring mesh fill only writes sky pixels (depth still at FP_DEPTH_FAR).
static bool s_fp_fill_sky_only = false;
// Cluster path softens the centre frame before neighbour fill; present then skips a second pass.
static bool s_fp_fxaa_already = false;

// Octree cache structure
typedef struct
{
  Octree *octree;
  const World *world;
  uint64_t world_hash; // Hash of world data to detect changes
  bool valid;
  Uint64 last_used;
} CachedOctree;

// Octree cache
static CachedOctree s_octree_cache = {0};
static const int OCTREE_CACHE_MAX_AGE_MS = 5000; // Cache for 5 seconds (octrees are expensive to build)

typedef struct
{
  float fwd_x, fwd_y, fwd_z;
  float right_x, right_y, right_z;
  float up_x, up_y, up_z;
  float half_tan;
  float aspect;
} FPCameraBasis;

static void fp_raster_mesh(const FPFrameBuffer *fb, const FPCamera *cam, const FPCameraBasis *basis,
                           const World *world, const VoxelMesh *mesh, const FogAtlas *fog,
                           float ox, float oy, float oz, float scale, float yaw, float pivot_x,
                           float pivot_y, bool use_materials, uint8_t tint_r, uint8_t tint_g,
                           uint8_t tint_b);
static void fp_raster_cluster_ring1(const FPFrameBuffer *fb, const FPCamera *cam,
                                    const FPCameraBasis *basis, const ShadowWorld *cluster,
                                    const FogAtlas *fog);
static void fp_raster_cluster_neighbours(const FPFrameBuffer *fb, const FPCamera *cam,
                                         const FPCameraBasis *basis, const ShadowWorld *cluster,
                                         const FogAtlas *fog_atlas);

static float *s_depth_buffer = NULL;
static int s_depth_cap = 0;
static int32_t *s_voxel_id = NULL;
static int s_voxel_id_cap = 0;
static float s_fog_eye[3];
static bool s_defer_atmo_fog = false;
static bool s_fp_lighting = false;
static bool s_fp_gpu_occupancy = false;
static int s_light_ox = 0, s_light_oy = 0, s_light_oz = 0;
static GlOccupancyTexture s_gl_occupancy;

static inline int32_t fp_pack_vox(int x, int y, int z)
{
  return (int32_t)(((uint32_t)(x & 0x3FF) << 20) | ((uint32_t)(y & 0x3FF) << 10) |
                   (uint32_t)(z & 0x3FF));
}

static inline void fp_unpack_vox(int32_t packed, int *x, int *y, int *z)
{
  *x = (int)((uint32_t)packed >> 20) & 0x3FF;
  *y = (int)((uint32_t)packed >> 10) & 0x3FF;
  *z = (int)(uint32_t)packed & 0x3FF;
}

void fp_renderer_set_lighting(bool enable)
{
  s_fp_lighting = enable;
}

void fp_renderer_set_gpu_occupancy(bool enable)
{
  s_fp_gpu_occupancy = enable;
}

// Keep a GL_TEXTURE_3D occupancy volume in sync when a context exists. Live lighting still runs on
// the CPU (ShadowWorld); this is the Teardown-style volume for a future fullscreen GPU pass and
// only uploads when fp_renderer_set_gpu_occupancy(true) (or FP_GPU_OCCUPANCY=1) opts in.
static void fp_sync_gl_occupancy(const World *world)
{
  if (!s_fp_gpu_occupancy)
    return;
  if (!world || !world->occupancy_bits)
    return;
  if (!SDL_GL_GetCurrentContext())
    return;
  if (s_gl_occupancy.tex && s_gl_occupancy.revision == world->voxel_revision &&
      s_gl_occupancy.width == world->occupancy_bits->width)
    return;
  if (!s_gl_occupancy.tex && s_gl_occupancy.width == 0 && s_gl_occupancy.height == 0)
    gl_occupancy_init(&s_gl_occupancy);
  if (gl_occupancy_upload(&s_gl_occupancy, world->occupancy_bits, world->voxel_revision))
    (void)gl_occupancy_consume_bind(&s_gl_occupancy);
}

static bool fp_debug_enabled(void)
{
  static int cached = -1;
  if (cached < 0)
    cached = (getenv("FP_RENDERER_DEBUG") != NULL);
  return cached;
}

#define FP_DBG(...)                     \
  do                                    \
  {                                     \
    if (fp_debug_enabled())             \
      printf(__VA_ARGS__);              \
  } while (0)

// The streaming texture is SDL_PIXELFORMAT_RGBA8888, a packed format whose 32-bit value is
// 0xRRGGBBAA. Packing the bytes in memory order instead put 0xFF in the red channel for every
// pixel, which is why the whole view came out tinted red.
static inline Uint32 fp_rgba8888(Uint8 r, Uint8 g, Uint8 b)
{
  return ((Uint32)r << 24) | ((Uint32)g << 16) | ((Uint32)b << 8) | 0xFFu;
}

// Sky/fog colour, shared by every FP path so the horizon and the panel clear agree.
#define FP_SKY_R 20
#define FP_SKY_G 22
#define FP_SKY_B 35

// How much of the sky colour a surface at this distance has faded into: 0 near, 1 at the far edge.
// Hermite smoothstep keeps the first third of the band clearer than a linear fade (near-field
// fidelity) and steepens into the fog wall so the limiter reads as atmosphere rather than a clip.
static inline float fp_fog_factor(float dist)
{
  if (dist <= FP_FOG_START)
    return 0.0f;
  float t = (dist - FP_FOG_START) / (FP_FOG_END - FP_FOG_START);
  if (t >= 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

static inline void fp_mix_fog(float fog, uint8_t *r, uint8_t *g, uint8_t *b)
{
  if (fog <= 0.0f)
    return;
  const float inv = 1.0f - fog;
  *r = (uint8_t)((float)*r * inv + (float)FP_SKY_R * fog);
  *g = (uint8_t)((float)*g * inv + (float)FP_SKY_G * fog);
  *b = (uint8_t)((float)*b * inv + (float)FP_SKY_B * fog);
}

static inline void fp_apply_fog(float dist, uint8_t *r, uint8_t *g, uint8_t *b)
{
  fp_mix_fog(fp_fog_factor(dist), r, g, b);
}

// Unexplored voxels still occlude, but they take the atmospheric fog colour — never a black
// silhouette inside the view cone. The reveal pass is supposed to have covered this frustum
// already; this is the fallback when a ray-grid hole or a range mismatch leaks through.
static inline void fp_shade_unexplored(uint8_t *r, uint8_t *g, uint8_t *b)
{
  *r = FP_SKY_R;
  *g = FP_SKY_G;
  *b = FP_SKY_B;
}

static uint64_t world_content_token(const World *world)
{
  if (!world)
    return 0;
  return world->voxel_revision ^
         ((uint64_t)world->width << 16) ^
         ((uint64_t)world->height << 32) ^
         ((uint64_t)world->depth << 48);
}

static void fp_compute_target_size(int panel_w, int panel_h, int *out_w, int *out_h, float *out_scale)
{
  const int area = panel_w * panel_h;
  float scale = 1.0f;
  if (area > 1000000)
    scale = 0.33f;
  else if (area > 360000)
    scale = 0.5f;

  static int s_fixed_res = -1;
  if (s_fixed_res < 0)
  {
    const char *e = getenv("VERSE_FP_FIXED_RES");
    s_fixed_res = (e && e[0] && e[0] != '0') ? 1 : 0;
  }

  // Adaptive dyn-res on top of the static area scale (skipped when FIXED_RES for deterministic audits).
  if (!s_fixed_res)
  {
    if (s_last_frame_ms > FP_DYN_RES_BUDGET_MS)
      s_dyn_res_scale = fmaxf(0.5f, s_dyn_res_scale * 0.92f);
    else if (s_last_frame_ms > 0.0 && s_last_frame_ms < FP_DYN_RES_RECOVER_MS)
      s_dyn_res_scale = fminf(1.0f, s_dyn_res_scale * 1.03f);
    scale *= s_dyn_res_scale;
  }

  *out_w = (int)fmaxf(1.0f, floorf((float)panel_w * scale));
  *out_h = (int)fmaxf(1.0f, floorf((float)panel_h * scale));
  if (out_scale)
    *out_scale = scale;
}

static bool fp_ensure_streaming_texture(SDL_Renderer *ren, int target_w, int target_h)
{
  if (s_last_renderer == ren && s_fp_tex && s_fp_tex_w == target_w && s_fp_tex_h == target_h)
    return true;

  if (s_fp_tex)
    SDL_DestroyTexture(s_fp_tex);

  // fp_rgba8888 packs for this exact format, so it is requested rather than negotiated.
  s_fp_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
                               target_w, target_h);
  if (!s_fp_tex)
    return false;

  SDL_SetTextureBlendMode(s_fp_tex, SDL_BLENDMODE_NONE);
  s_fp_tex_w = target_w;
  s_fp_tex_h = target_h;
  s_last_renderer = ren;
  return true;
}

static void fp_camera_build_basis(const FPCamera *cam, FPCameraBasis *basis)
{
  const float cy = cosf(cam->yaw);
  const float sy = sinf(cam->yaw);
  const float cp = cosf(cam->pitch);
  const float sp = sinf(cam->pitch);

  basis->fwd_x = cy * cp;
  basis->fwd_y = sy * cp;
  basis->fwd_z = sp;

  float len = sqrtf(basis->fwd_x * basis->fwd_x + basis->fwd_y * basis->fwd_y +
                    basis->fwd_z * basis->fwd_z);
  if (len > 0.0f)
  {
    basis->fwd_x /= len;
    basis->fwd_y /= len;
    basis->fwd_z /= len;
  }

  basis->up_x = 0.0f;
  basis->up_y = 0.0f;
  basis->up_z = 1.0f;
  if (fabsf(basis->fwd_z) > 0.98f)
  {
    basis->up_x = 1.0f;
    basis->up_y = 0.0f;
    basis->up_z = 0.0f;
  }

  basis->right_x = basis->up_y * basis->fwd_z - basis->up_z * basis->fwd_y;
  basis->right_y = basis->up_z * basis->fwd_x - basis->up_x * basis->fwd_z;
  basis->right_z = basis->up_x * basis->fwd_y - basis->up_y * basis->fwd_x;
  len = sqrtf(basis->right_x * basis->right_x + basis->right_y * basis->right_y +
             basis->right_z * basis->right_z);
  if (len > 0.0f)
  {
    basis->right_x /= len;
    basis->right_y /= len;
    basis->right_z /= len;
  }

  basis->up_x = basis->fwd_y * basis->right_z - basis->fwd_z * basis->right_y;
  basis->up_y = basis->fwd_z * basis->right_x - basis->fwd_x * basis->right_z;
  basis->up_z = basis->fwd_x * basis->right_y - basis->fwd_y * basis->right_x;
  len = sqrtf(basis->up_x * basis->up_x + basis->up_y * basis->up_y + basis->up_z * basis->up_z);
  if (len > 0.0f)
  {
    basis->up_x /= len;
    basis->up_y /= len;
    basis->up_z /= len;
  }

  // Bank the view around the look axis. Positive roll = right wing down (horizon tips clockwise).
  if (fabsf(cam->roll) > 1e-6f)
  {
    const float cr = cosf(cam->roll);
    const float sr = sinf(cam->roll);
    const float rx = basis->right_x;
    const float ry = basis->right_y;
    const float rz = basis->right_z;
    const float ux = basis->up_x;
    const float uy = basis->up_y;
    const float uz = basis->up_z;
    basis->right_x = rx * cr - ux * sr;
    basis->right_y = ry * cr - uy * sr;
    basis->right_z = rz * cr - uz * sr;
    basis->up_x = rx * sr + ux * cr;
    basis->up_y = ry * sr + uy * cr;
    basis->up_z = rz * sr + uz * cr;
  }

  basis->half_tan = tanf(cam->fov_deg * (float)M_PI / 180.0f * 0.5f);
  basis->aspect = 1.0f;
}

// Camera space: +x right, +y up, +z forward. u and v ride along so the near clip and the
// triangulation carry surface coordinates without a second parallel array to keep in step.
typedef struct
{
  float x, y, z;
  float u, v;
  float wx, wy, wz; // world position, interpolated through the near clip with u and v
} FPCameraPoint;

#define FP_NEAR_PLANE 0.05f

static inline void fp_to_camera_space(const FPCamera *cam, const FPCameraBasis *basis,
                                     float wx, float wy, float wz, FPCameraPoint *out)
{
  const float dx = wx - cam->x;
  const float dy = wy - cam->y;
  const float dz = wz - cam->z;
  out->x = dx * basis->right_x + dy * basis->right_y + dz * basis->right_z;
  out->y = dx * basis->up_x + dy * basis->up_y + dz * basis->up_z;
  out->z = dx * basis->fwd_x + dy * basis->fwd_y + dz * basis->fwd_z;
  out->u = 0.0f;
  out->v = 0.0f;
  out->wx = wx;
  out->wy = wy;
  out->wz = wz;
}

// Exact inverse of the ray the raycast path builds for a pixel, which is
// fwd + nx*half_tan*aspect*right + ny*half_tan*up. So screen position divides by the tangent;
// the previous version multiplied by it and applied aspect on the wrong side, shrinking the
// projected world by roughly 2.6x at a 60 degree FOV and disagreeing with the ray path.
static inline void fp_project_camera_point(const FPCameraBasis *basis, const FPCameraPoint *p,
                                           int target_w, int target_h,
                                           float *sx, float *sy, float *depth)
{
  const float inv_z = 1.0f / p->z;
  const float nx = (p->x * inv_z) / (basis->half_tan * basis->aspect);
  const float ny = (p->y * inv_z) / basis->half_tan;
  *sx = ((float)target_w * 0.5f) * (1.0f + nx);
  *sy = ((float)target_h * 0.5f) * (1.0f - ny);
  *depth = p->z;
}

// Sutherland-Hodgman clip against z >= near. Without this, a quad with one corner behind the
// camera was projected from uninitialized screen coordinates, which is what smeared geometry
// across the view whenever the player stood next to a wall.
static int fp_clip_near(const FPCameraPoint *in, int in_count, FPCameraPoint *out)
{
  int out_count = 0;
  for (int i = 0; i < in_count; i++)
  {
    const FPCameraPoint *a = &in[i];
    const FPCameraPoint *b = &in[(i + 1) % in_count];
    const bool a_in = a->z >= FP_NEAR_PLANE;
    const bool b_in = b->z >= FP_NEAR_PLANE;

    if (a_in)
      out[out_count++] = *a;

    if (a_in != b_in)
    {
      const float denom = b->z - a->z;
      const float t = (fabsf(denom) > 1e-9f) ? ((FP_NEAR_PLANE - a->z) / denom) : 0.0f;
      out[out_count].x = a->x + (b->x - a->x) * t;
      out[out_count].y = a->y + (b->y - a->y) * t;
      out[out_count].z = FP_NEAR_PLANE;
      out[out_count].u = a->u + (b->u - a->u) * t;
      out[out_count].v = a->v + (b->v - a->v) * t;
      out[out_count].wx = a->wx + (b->wx - a->wx) * t;
      out[out_count].wy = a->wy + (b->wy - a->wy) * t;
      out[out_count].wz = a->wz + (b->wz - a->wz) * t;
      out_count++;
    }
  }
  return out_count;
}

static float fp_edge(float ax, float ay, float bx, float by, float cx, float cy)
{
  return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

// What a rasterised triangle needs to know about the surface it covers.
//
// A flat surface leaves `type` at VOXEL_AIR and every pixel takes `flat`. A textured one carries
// the material, and the filler resolves a texel per pixel from the surface coordinates the vertices
// carry. Keeping both in one descriptor is what lets the two cases share a single rasteriser.
typedef struct
{
  Uint32 flat;    // packed fallback colour, already shaded and fogged
  VoxelType type; // VOXEL_AIR when this surface has no material template
  MaterialFace face;
  float shade; // directional key for the face, applied to the sampled albedo
  float fog;   // 0 = unfogged, 1 = fully faded into the sky
  uint8_t damage;
  uint32_t crack_seed;
  bool lit; // write a voxel id so the occupancy lighting pass can find this surface
  float nx, ny, nz; // face normal, used to land packed ids inside the owning voxel
} FPSurface;

// Screen-space vertex. u and v are divided through by depth so they can be interpolated linearly
// across the triangle; `iw` carries the reciprocal needed to undo that per pixel.
typedef struct
{
  float x, y, z;
  float u_over_z, v_over_z, iw;
  float wx_over_z, wy_over_z, wz_over_z;
} FPScreenVertex;

static inline void fp_fill_write_voxel_id(int idx, const FPSurface *surf,
                                         const FPScreenVertex *v0, const FPScreenVertex *v1,
                                         const FPScreenVertex *v2, float bw0, float bw1, float bw2,
                                         float inv_iw)
{
  const float wx = (bw0 * v0->wx_over_z + bw1 * v1->wx_over_z + bw2 * v2->wx_over_z) * inv_iw;
  const float wy = (bw0 * v0->wy_over_z + bw1 * v1->wy_over_z + bw2 * v2->wy_over_z) * inv_iw;
  const float wz = (bw0 * v0->wz_over_z + bw1 * v1->wz_over_z + bw2 * v2->wz_over_z) * inv_iw;
  // Nudge inside the face so floor() lands on the owning voxel, not its neighbour.
  const int vx = (int)floorf(wx - surf->nx * 0.01f) + s_light_ox;
  const int vy = (int)floorf(wy - surf->ny * 0.01f) + s_light_oy;
  const int vz = (int)floorf(wz - surf->nz * 0.01f) + s_light_oz;
  s_voxel_id[idx] = fp_pack_vox(vx, vy, vz);
}

static void fp_fill_triangle(float *depth_buf, Uint32 *color_buf, int stride, int w, int h,
                             const FPScreenVertex *v0, const FPScreenVertex *v1,
                             const FPScreenVertex *v2, const FPSurface *surf)
{
  const float sx0 = v0->x, sy0 = v0->y, sz0 = v0->z;
  const float sx1 = v1->x, sy1 = v1->y, sz1 = v1->z;
  const float sx2 = v2->x, sy2 = v2->y, sz2 = v2->z;

  const float area = fp_edge(sx0, sy0, sx1, sy1, sx2, sy2);
  if (fabsf(area) < 1e-6f)
    return;

  const bool textured = (surf->type != VOXEL_AIR);
  const bool write_id = surf->lit && s_voxel_id != NULL;
  const Uint32 color = surf->flat;

  const float inv_area = 1.0f / area;
  int x0 = (int)floorf(fminf(sx0, fminf(sx1, sx2)));
  int x1 = (int)ceilf(fmaxf(sx0, fmaxf(sx1, sx2)));
  int y0 = (int)floorf(fminf(sy0, fminf(sy1, sy2)));
  int y1 = (int)ceilf(fmaxf(sy0, fmaxf(sy1, sy2)));
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= w)
    x1 = w - 1;
  if (y1 >= h)
    y1 = h - 1;
  if (x0 > x1 || y0 > y1)
    return;

  const bool ccw = area > 0.0f;

  for (int y = y0; y <= y1; y++)
  {
    const float py = (float)y + 0.5f;
    for (int x = x0; x <= x1; x++)
    {
      const float px = (float)x + 0.5f;
      const float w0 = fp_edge(sx1, sy1, sx2, sy2, px, py);
      const float w1 = fp_edge(sx2, sy2, sx0, sy0, px, py);
      const float w2 = fp_edge(sx0, sy0, sx1, sy1, px, py);
      if (ccw)
      {
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
          continue;
      }
      else
      {
        if (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f)
          continue;
      }

      const float bw0 = w0 * inv_area;
      const float bw1 = w1 * inv_area;
      const float bw2 = w2 * inv_area;
      const float z = bw0 * sz0 + bw1 * sz1 + bw2 * sz2;
      const int idx = y * stride + x;
      // Neighbour mesh pass: never overwrite centre (or nearer) geometry — only fill leftover sky.
      if (s_fp_fill_sky_only && depth_buf[idx] < FP_DEPTH_FAR)
        continue;
      if (z >= depth_buf[idx])
        continue;

      if (!textured)
      {
        depth_buf[idx] = z;
        color_buf[idx] = color;
        if (write_id)
        {
          const float iw = bw0 * v0->iw + bw1 * v1->iw + bw2 * v2->iw;
          if (fabsf(iw) > 1e-9f)
            fp_fill_write_voxel_id(idx, surf, v0, v1, v2, bw0, bw1, bw2, 1.0f / iw);
        }
        continue;
      }

      // Perspective-correct surface coordinates. Interpolating u and v directly would swim across
      // the long quads greedy meshing produces, sliding the grain over the surface as the camera
      // turns; dividing by the interpolated reciprocal depth pins it to the geometry.
      const float iw = bw0 * v0->iw + bw1 * v1->iw + bw2 * v2->iw;
      if (fabsf(iw) < 1e-9f)
        continue;
      const float inv_iw = 1.0f / iw;
      const float u = (bw0 * v0->u_over_z + bw1 * v1->u_over_z + bw2 * v2->u_over_z) * inv_iw;
      const float v = (bw0 * v0->v_over_z + bw1 * v1->v_over_z + bw2 * v2->v_over_z) * inv_iw;

      uint8_t tr, tg, tb;
      if (!material_worlds_sample(surf->type, surf->face, u, v, &tr, &tg, &tb))
      {
        // Sparse material bakes leave transparent texels. Punching them to FAR depth used to be
        // desirable for see-through canopies, but the cluster neighbour pass treats FAR as "the
        // centre world missed this ray" and fills foreign worlds into those pixels — so a leaf
        // gap or undrawn tall-grass cell became a portal across the world boundary. Match the
        // ray path: stand in with the flat shaded colour and still write depth so neighbours
        // cannot claim the pixel. Nested silhouette meshes (use_materials=false) still show
        // real geometric gaps between subvoxel quads.
        depth_buf[idx] = z;
        color_buf[idx] = color;
        if (write_id)
          fp_fill_write_voxel_id(idx, surf, v0, v1, v2, bw0, bw1, bw2, inv_iw);
        continue;
      }

      if (surf->damage > 0)
        voxel_crack_modulate(surf->damage, surf->crack_seed, u, v, &tr, &tg, &tb);

      tr = (uint8_t)((float)tr * surf->shade);
      tg = (uint8_t)((float)tg * surf->shade);
      tb = (uint8_t)((float)tb * surf->shade);
      // When occupancy lighting is on, fog after the lighting multiply so dark cells do not
      // wash toward fog colour before irradiance is applied.
      if (!surf->lit)
        fp_mix_fog(surf->fog, &tr, &tg, &tb);

      depth_buf[idx] = z;
      color_buf[idx] = fp_rgba8888(tr, tg, tb);
      if (write_id)
        fp_fill_write_voxel_id(idx, surf, v0, v1, v2, bw0, bw1, bw2, inv_iw);
    }
  }
}

// Quad bounds are exclusive lattice coordinates (see voxel_mesh.h): the axis a face is normal to
// spans exactly one unit, and the face itself lies on one end of it. All four corners must share
// that fixed coordinate. The previous ring mixed both ends on every face type, so each quad was a
// twisted ribbon cutting diagonally through its voxel rather than a flat face — hence a view with
// hints of structure but no readable surfaces.
static void fp_quad_world_corners(const VoxelFaceQuad *q, float corners[4][3])
{
  const float x0 = (float)q->x0, x1 = (float)q->x1;
  const float y0 = (float)q->y0, y1 = (float)q->y1;
  const float z0 = (float)q->z0, z1 = (float)q->z1;

  switch (q->face)
  {
  case 0: // +Z (top) on the z1 plane, spanning x and y
  case 1: // -Z (bottom) on the z0 plane
  {
    const float z = (q->face == 0) ? z1 : z0;
    corners[0][0] = x0; corners[0][1] = y0; corners[0][2] = z;
    corners[1][0] = x1; corners[1][1] = y0; corners[1][2] = z;
    corners[2][0] = x1; corners[2][1] = y1; corners[2][2] = z;
    corners[3][0] = x0; corners[3][1] = y1; corners[3][2] = z;
    break;
  }
  case 2: // +Y on the y1 plane, spanning x and z
  case 4: // -Y on the y0 plane
  {
    const float y = (q->face == 2) ? y1 : y0;
    corners[0][0] = x0; corners[0][1] = y; corners[0][2] = z0;
    corners[1][0] = x1; corners[1][1] = y; corners[1][2] = z0;
    corners[2][0] = x1; corners[2][1] = y; corners[2][2] = z1;
    corners[3][0] = x0; corners[3][1] = y; corners[3][2] = z1;
    break;
  }
  case 3: // +X on the x1 plane, spanning y and z
  default: // 5: -X on the x0 plane
  {
    const float x = (q->face == 3) ? x1 : x0;
    corners[0][0] = x; corners[0][1] = y0; corners[0][2] = z0;
    corners[1][0] = x; corners[1][1] = y1; corners[1][2] = z0;
    corners[2][0] = x; corners[2][1] = y1; corners[2][2] = z1;
    corners[3][0] = x; corners[3][1] = y0; corners[3][2] = z1;
    break;
  }
  }
}

static void fp_face_normal(int face, float *nx, float *ny, float *nz)
{
  *nx = 0.0f;
  *ny = 0.0f;
  *nz = 0.0f;
  switch (face)
  {
  case 0: *nz = 1.0f; break;
  case 1: *nz = -1.0f; break;
  case 2: *ny = 1.0f; break;
  case 3: *nx = 1.0f; break;
  case 4: *ny = -1.0f; break;
  default: *nx = -1.0f; break;
  }
}

// Fixed directional key so the six orientations read as different surfaces. Flat per-voxel colour
// alone makes a corner between two faces invisible, which reads as mush rather than structure.
static float fp_axis_shade(int axis, bool positive)
{
  if (axis == 2)
    return positive ? 1.00f : 0.48f; // top lit, bottom in shadow
  if (axis == 0)
    return positive ? 0.88f : 0.70f; // +X / -X
  return positive ? 0.80f : 0.62f;   // +Y / -Y
}

// Mesh quad face indices, per voxel_mesh.h: 0 +Z, 1 -Z, 2 +Y, 3 +X, 4 -Y, 5 -X.
static float fp_face_shade(int face)
{
  switch (face)
  {
  case 0: return fp_axis_shade(2, true);
  case 1: return fp_axis_shade(2, false);
  case 2: return fp_axis_shade(1, true);
  case 3: return fp_axis_shade(0, true);
  case 4: return fp_axis_shade(1, false);
  default: return fp_axis_shade(0, false);
  }
}

// ---------------------------------------------------------------------------------------------
// Sub-voxel surface detail
//
// Every material has a nested 32x32x32 world describing what its surface is made of, baked once
// into one texture per cube face (see material_worlds.h). The isometric renderer hands those bakes
// to the GPU as an atlas; this renderer rasterises into a CPU pixel buffer, so it samples the same
// bakes directly instead. Same source of truth, no SDL texture in the way, and the two views agree
// on what grass looks like because they are reading the same texels.
// ---------------------------------------------------------------------------------------------

// -1 = follow VERSE_SUBVOXEL; 0 = force off; 1 = force on.
static int s_fp_subvoxel_override = -1;
static int s_fp_subvoxel_cached = -1;
static FPMaterialFrameStats s_material_frame_stats;

static bool fp_subvoxel_enabled(void)
{
  // VERSE_SUBVOXEL=0 turns detail off, matching the isometric renderer, so the two can be compared
  // and so a failed template build degrades to the flat faces this renderer drew before.
  // fp_renderer_set_subvoxel_enabled overrides the env for A/B benches in-process.
  if (s_fp_subvoxel_override == 0)
    return false;
  if (s_fp_subvoxel_override == 1)
    return material_worlds_init();
  if (s_fp_subvoxel_cached < 0)
  {
    const char *off = getenv("VERSE_SUBVOXEL");
    s_fp_subvoxel_cached = (off && strcmp(off, "0") == 0) ? 0 : (material_worlds_init() ? 1 : 0);
  }
  return s_fp_subvoxel_cached == 1;
}

void fp_renderer_set_subvoxel_enabled(int enabled)
{
  s_fp_subvoxel_override = enabled < 0 ? -1 : (enabled ? 1 : 0);
  s_fp_subvoxel_cached = -1;
  // Parent mesh emission (skip vs keep sparse AABBs) depends on this flag.
  fp_renderer_invalidate_cache();
}

void fp_renderer_clear_material_frame_stats(void)
{
  memset(&s_material_frame_stats, 0, sizeof(s_material_frame_stats));
}

const FPMaterialFrameStats *fp_renderer_material_frame_stats(void)
{
  return &s_material_frame_stats;
}

static MaterialFace fp_material_face_for_mesh_face(int face)
{
  switch (face)
  {
  case 0: return MATERIAL_FACE_TOP;
  case 1: return MATERIAL_FACE_BOTTOM;
  case 2: return MATERIAL_FACE_LEFT;
  case 3: return MATERIAL_FACE_RIGHT;
  case 4: return MATERIAL_FACE_BACK;
  default: return MATERIAL_FACE_FRONT;
  }
}

static float fp_material_face_shade(MaterialFace face)
{
  switch (face)
  {
  case MATERIAL_FACE_TOP:    return fp_axis_shade(2, true);
  case MATERIAL_FACE_BOTTOM: return fp_axis_shade(2, false);
  case MATERIAL_FACE_LEFT:   return fp_axis_shade(1, true);
  case MATERIAL_FACE_BACK:   return fp_axis_shade(1, false);
  case MATERIAL_FACE_RIGHT:  return fp_axis_shade(0, true);
  default:                   return fp_axis_shade(0, false); // FRONT
  }
}

// Where a world-space point on a face lands in that face's (u,v), in voxel units.
//
// The axis flips match the parameterisation material_worlds bakes with, so the grain sits the same
// way up here as it does in the isometric view. Whole units are one tile: a coordinate spanning
// several voxels tiles the material along the surface rather than stretching one tile over the run,
// which is what lets a greedy-merged quad keep per-voxel detail.
static inline void fp_face_uv(MaterialFace face, float px, float py, float pz, float *u, float *v)
{
  switch (face)
  {
  case MATERIAL_FACE_TOP:    *u = px;  *v = py;  break;
  case MATERIAL_FACE_BOTTOM: *u = px;  *v = -py; break;
  case MATERIAL_FACE_LEFT:   *u = px;  *v = -pz; break;
  case MATERIAL_FACE_BACK:   *u = -px; *v = -pz; break;
  case MATERIAL_FACE_RIGHT:  *u = py;  *v = -pz; break;
  default:                   *u = -py; *v = -pz; break; // FRONT
  }
}

// Which face of voxel (vx,vy,vz) a ray entered through, and where on it.
//
// world_raycast_first_hit reports the voxel the ray stopped in but not how it got there, and the
// face is what decides which bake to read. Recovering it from the voxel's own box costs three
// divides and leaves the raycaster's signature alone. Returns false when the ray starts inside the
// voxel, where there is no entry face to sample.
static bool fp_ray_entry_face(float ox, float oy, float oz, float dx, float dy, float dz,
                              int vx, int vy, int vz,
                              MaterialFace *out_face, float *out_u, float *out_v)
{
  const float o[3] = {ox, oy, oz};
  const float d[3] = {dx, dy, dz};
  const float lo[3] = {(float)vx, (float)vy, (float)vz};

  // Slab test: the entry point is the latest of the three per-axis near crossings, and the axis
  // that produced it is the one the ray crossed to get in.
  float t_near = -1e30f;
  int axis = -1;
  for (int i = 0; i < 3; i++)
  {
    if (fabsf(d[i]) < 1e-9f)
      continue; // parallel to this pair of slabs, so it cannot be the face crossed
    const float inv = 1.0f / d[i];
    float t0 = (lo[i] - o[i]) * inv;
    float t1 = (lo[i] + 1.0f - o[i]) * inv;
    if (t0 > t1)
    {
      const float swap = t0;
      t0 = t1;
      t1 = swap;
    }
    if (t0 > t_near)
    {
      t_near = t0;
      axis = i;
    }
  }

  if (axis < 0 || t_near < 0.0f)
    return false;

  // Travelling down an axis means entering through that axis's high side, whose outward normal
  // points the other way.
  *out_face = material_face_for_normal(axis, d[axis] < 0.0f);
  fp_face_uv(*out_face, ox + dx * t_near, oy + dy * t_near, oz + dz * t_near, out_u, out_v);
  return true;
}

// Colour for a ray hit: the material's baked texel where there is one, the flat palette colour
// otherwise. The origin and direction must be in the hit world's local space, as the raycast was.
//
// The flat fallback is the shared palette, so an untextured material still matches the isometric
// view and the minimap.
//
// A transparent texel means the bake found nothing solid at that spot — a gap in a canopy or an
// angled roof / hung door / glass pane / furniture silhouette. Seeing through it would mean
// resuming the ray, which for the multi-world path means redoing the whole nearest-hit search, so
// the flat colour stands in and the silhouette stays a full cube on this path. The mesh path does
// the same for parent AABBs (writing flat + depth on a miss) so the cluster neighbour pass cannot
// treat a sparse bake gap as empty sky and paint a foreign world into it. Nested silhouette meshes
// still show real geometric gaps between subvoxel quads.
static void fp_shade_ray_hit(const Voxel *voxel, bool textured,
                             float ox, float oy, float oz, float dx, float dy, float dz,
                             int hx, int hy, int hz,
                             uint8_t *r, uint8_t *g, uint8_t *b)
{
  if (!voxel)
  {
    *r = FP_SKY_R;
    *g = FP_SKY_G;
    *b = FP_SKY_B;
    return;
  }

  world_voxel_type_color(voxel->type, r, g, b);
  if (!textured)
    return;

  MaterialFace face;
  float u = 0.0f, v = 0.0f;
  if (!fp_ray_entry_face(ox, oy, oz, dx, dy, dz, hx, hy, hz, &face, &u, &v))
    return;

  uint8_t tr, tg, tb;
  if (!material_worlds_sample(voxel->type, face, u, v, &tr, &tg, &tb))
    return;

  const uint8_t dmg = voxel_get_damage(voxel);
  if (dmg > 0)
    voxel_crack_modulate(dmg, voxel_crack_seed(hx, hy, hz), u, v, &tr, &tg, &tb);

  // The bake is albedo with only geometry-derived shading in it, so the directional key is applied
  // here exactly as it is to a flat face. Occupancy lighting replaces that key on the lit path.
  const float shade = s_fp_lighting ? 1.0f : fp_material_face_shade(face);
  *r = (uint8_t)((float)tr * shade);
  *g = (uint8_t)((float)tg * shade);
  *b = (uint8_t)((float)tb * shade);
}

// Simple hash function for world data
static uint64_t calculate_world_hash(const World *world)
{
  if (!world)
    return 0;

  // Comprehensive hash based on world dimensions and content
  uint64_t hash = 0;
  hash = hash * 31 + world->width;
  hash = hash * 31 + world->height;
  hash = hash * 31 + world->depth;

  // Hash more voxels for better change detection
  // Sample every 8th voxel to balance performance vs accuracy
  int sample_stride = 8;
  int samples = 0;
  int max_samples = 1000; // Increased from 100 to 1000

  for (int z = 0; z < world->depth && samples < max_samples; z += sample_stride)
  {
    for (int y = 0; y < world->height && samples < max_samples; y += sample_stride)
    {
      for (int x = 0; x < world->width && samples < max_samples; x += sample_stride)
      {
        const Voxel *v = world_voxel_cptr_fast(world, x, y, z);
        if (v)
        {
          hash = hash * 31 + v->type;
        }
        samples++;
      }
    }
  }

  return hash;
}

static void fp_sparse_list_clear(FPSparseCellList *list)
{
  if (!list)
    return;
  free(list->cells);
  list->cells = NULL;
  list->count = 0;
  list->capacity = 0;
}

static bool fp_sparse_list_push(FPSparseCellList *list, int x, int y, int z, VoxelType type,
                                uint8_t shape_orient, uint8_t yaw_u8)
{
  if (!list)
    return false;
  if (list->count >= list->capacity)
  {
    const int cap = list->capacity < 64 ? 64 : list->capacity * 2;
    FPSparseCell *n =
        (FPSparseCell *)realloc(list->cells, (size_t)cap * sizeof(FPSparseCell));
    if (!n)
      return false;
    list->cells = n;
    list->capacity = cap;
  }
  list->cells[list->count].x = (int16_t)x;
  list->cells[list->count].y = (int16_t)y;
  list->cells[list->count].z = (int16_t)z;
  list->cells[list->count].type = (uint16_t)type;
  list->cells[list->count].shape_orient = shape_orient;
  list->cells[list->count].yaw_u8 = yaw_u8;
  list->count++;
  return true;
}

// Collect sparse parents once at remesh time. Per-frame draw walks this list (often empty).
static void fp_sparse_list_rebuild(FPSparseCellList *list, const World *world)
{
  fp_sparse_list_clear(list);
  if (!world || !fp_subvoxel_enabled())
    return;
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
      {
        const Voxel *v = world_voxel_cptr_fast(world, (int)x, (int)y, (int)z);
        if (!v || !world_voxel_omits_parent_aabb(v))
          continue;
        const uint8_t so = voxel_get_field(v, VOXEL_FIELD_SHAPE_ORIENT);
        const uint8_t yaw = voxel_get_yaw_u8(v);
        if (!fp_sparse_list_push(list, (int)x, (int)y, (int)z, v->type, so, yaw))
          return;
      }
}

static void fp_sparse_list_copy(FPSparseCellList *dst, const FPSparseCellList *src)
{
  fp_sparse_list_clear(dst);
  if (!dst || !src || src->count <= 0)
    return;
  dst->cells = (FPSparseCell *)malloc((size_t)src->count * sizeof(FPSparseCell));
  if (!dst->cells)
    return;
  memcpy(dst->cells, src->cells, (size_t)src->count * sizeof(FPSparseCell));
  dst->count = src->count;
  dst->capacity = src->count;
}

static void fp_cached_mesh_free_contents(CachedMesh *c)
{
  if (!c || !c->valid)
    return;
  voxel_mesh_free(&c->mesh);
  if (c->lod_valid)
    voxel_mesh_free(&c->mesh_lod);
  fp_sparse_list_clear(&c->sparse);
  c->valid = false;
  c->lod_valid = false;
  c->provisional = false;
  c->world = NULL;
  c->world_hash = 0;
}

static void fp_park_centre_mesh(void)
{
  if (!s_mesh_cache.valid)
    return;
  fp_cached_mesh_free_contents(&s_mesh_cache_prev);
  s_mesh_cache_prev = s_mesh_cache;
  memset(&s_mesh_cache, 0, sizeof(s_mesh_cache));
}

static void fp_mesh_lod_from(const VoxelMesh *src, VoxelMesh *dst, int stride)
{
  voxel_mesh_free(dst);
  voxel_mesh_init(dst);
  if (!src || src->count <= 0 || stride < 2)
  {
    if (src)
      voxel_mesh_copy(dst, src);
    return;
  }
  voxel_mesh_copy(dst, src);
  int write = 0;
  int face_writes[6] = {0};
  for (int f = 0; f < 6; f++)
  {
    const int start = dst->face_start[f];
    const int n = dst->face_count[f];
    dst->face_start[f] = write;
    for (int i = 0; i < n; i++)
    {
      const VoxelFaceQuad *q = &dst->quads[start + i];
      if ((q->x0 % stride) != 0 || (q->y0 % stride) != 0 || (q->z0 % stride) != 0)
        continue;
      if (write != start + i)
        dst->quads[write] = *q;
      write++;
      face_writes[f]++;
    }
    dst->face_count[f] = face_writes[f];
  }
  dst->count = write;
}

// Bump when parent mesh emission rules change so warm caches rebuild (e.g. sparse skip).
#define FP_MESH_SCHEMA 4ull // 4: vegetation keeps parent AABB under skip_sparse

static uint64_t fp_centre_world_token(const World *world)
{
  return world_content_token(world) ^ (FP_MESH_SCHEMA * 0x9E3779B97F4A7C15ull) ^
         (fp_subvoxel_enabled() ? 0xA5A5A5A5A5A5A5A5ull : 0ull);
}

static void fp_build_full_centre_mesh(const World *world, uint64_t world_token, Uint64 now)
{
  fp_cached_mesh_free_contents(&s_mesh_cache);

  const Uint64 build_start = SDL_GetPerformanceCounter();
  const bool skip_sparse = fp_subvoxel_enabled();
  voxel_mesh_init(&s_mesh_cache.mesh);
  voxel_mesh_build_all_faces_greedy_ex(world, &s_mesh_cache.mesh, skip_sparse);
  // Derive LOD from the full mesh — a second greedy build used to double remesh cost.
  voxel_mesh_init(&s_mesh_cache.mesh_lod);
  fp_mesh_lod_from(&s_mesh_cache.mesh, &s_mesh_cache.mesh_lod, 2);
  s_mesh_cache.lod_valid = true;
  if (skip_sparse)
    fp_sparse_list_rebuild(&s_mesh_cache.sparse, world);
  else
    fp_sparse_list_clear(&s_mesh_cache.sparse);
  s_mesh_build_ms = (double)(SDL_GetPerformanceCounter() - build_start) * 1000.0 /
                    (double)SDL_GetPerformanceFrequency();

  s_mesh_cache.world = world;
  s_mesh_cache.world_hash = world_token;
  s_mesh_cache.valid = true;
  s_mesh_cache.provisional = false;
  s_mesh_cache.last_used = now;
  s_mesh_cache.built_at = now;
  s_mesh_needs_full = false;

  FP_DBG("[fp_renderer] mesh_cache: built %d quads (+%d lod, %d sparse) in %.2fms\n",
         s_mesh_cache.mesh.count, s_mesh_cache.mesh_lod.count, s_mesh_cache.sparse.count,
         s_mesh_build_ms);
}

// Promote a ring-1 mesh into the centre cache. Full-quality neighbours become the new centre with
// no provisional remesh; coarse stand-ins still need a deferred full build.
static bool fp_try_promote_ring1_to_centre(const World *world, uint64_t world_token, Uint64 now)
{
  for (int i = 0; i < FP_RING1_CACHE_CAP; i++)
  {
    Ring1CachedMesh *e = &s_ring1_cache[i];
    if (!e->valid || e->world != world)
      continue;

    fp_cached_mesh_free_contents(&s_mesh_cache);
    voxel_mesh_init(&s_mesh_cache.mesh);
    voxel_mesh_copy(&s_mesh_cache.mesh, &e->mesh_lod);
    voxel_mesh_init(&s_mesh_cache.mesh_lod);
    if (e->full_quality)
      fp_mesh_lod_from(&s_mesh_cache.mesh, &s_mesh_cache.mesh_lod, 2);
    else
      voxel_mesh_copy(&s_mesh_cache.mesh_lod, &e->mesh_lod);
    s_mesh_cache.lod_valid = true;
    fp_sparse_list_copy(&s_mesh_cache.sparse, &e->sparse);
    s_mesh_cache.world = world;
    s_mesh_cache.world_hash = world_token;
    s_mesh_cache.valid = true;
    s_mesh_cache.provisional = !e->full_quality;
    s_mesh_cache.last_used = now;
    s_mesh_cache.built_at = now;
    s_mesh_needs_full = !e->full_quality;
    FP_DBG("[fp_renderer] mesh_cache: promoted ring1 slot %d (%d quads, full=%d)\n",
           e->shadow_slot, s_mesh_cache.mesh.count, (int)e->full_quality);
    return true;
  }
  return false;
}

// Get cached mesh or build new one if needed
static VoxelMesh *get_cached_mesh(const World *world)
{
  const Uint64 now = SDL_GetTicks();
  const uint64_t world_token = fp_centre_world_token(world);

  // Hit on the active centre cache.
  if (s_mesh_cache.valid && s_mesh_cache.world == world)
  {
    if (s_mesh_cache.world_hash == world_token)
    {
      s_mesh_cache.last_used = now;
      return &s_mesh_cache.mesh;
    }

    // The world changed. A full remesh of a 128^3 world costs tens of milliseconds, and the
    // physics tick nudges fluid voxels about once a second, so rebuilding on every change would
    // stutter continuously. Serve the slightly stale mesh until a rebuild is affordable: capping
    // meshing at roughly an eighth of wall-clock time is far less visible than the freeze.
    const Uint64 min_interval = (Uint64)fmax(250.0, s_mesh_build_ms * 8.0);
    if (now - s_mesh_cache.built_at < min_interval)
    {
      s_mesh_cache.last_used = now;
      return &s_mesh_cache.mesh;
    }
    // Fall through to rebuild in place (same world pointer).
  }
  else if (s_mesh_cache.valid && s_mesh_cache.world != world)
  {
    // World boundary: park the old centre, then try the parked neighbour or a warm ring-1 LOD
    // before paying for a synchronous full remesh.
    fp_park_centre_mesh();

    if (s_mesh_cache_prev.valid && s_mesh_cache_prev.world == world)
    {
      s_mesh_cache = s_mesh_cache_prev;
      memset(&s_mesh_cache_prev, 0, sizeof(s_mesh_cache_prev));
      s_mesh_cache.last_used = now;
      if (s_mesh_cache.world_hash != world_token)
        s_mesh_needs_full = true;
      FP_DBG("[fp_renderer] mesh_cache: restored parked centre mesh (%d quads)\n",
             s_mesh_cache.mesh.count);
      return &s_mesh_cache.mesh;
    }

    if (fp_try_promote_ring1_to_centre(world, world_token, now))
      return &s_mesh_cache.mesh;

    // Cold boundary: emit a coarse lattice stand-in (~stride-2, ~tens of ms) and finish the
    // full remesh on the warm path. Paying ~100ms+ here is the hitch at an unprepared seam.
    fp_cached_mesh_free_contents(&s_mesh_cache);
    {
      const Uint64 build_start = SDL_GetPerformanceCounter();
      const bool skip_sparse = fp_subvoxel_enabled();
      voxel_mesh_init(&s_mesh_cache.mesh);
      voxel_mesh_build_all_faces_greedy_lod_ex(world, &s_mesh_cache.mesh, 2, skip_sparse);
      voxel_mesh_init(&s_mesh_cache.mesh_lod);
      voxel_mesh_copy(&s_mesh_cache.mesh_lod, &s_mesh_cache.mesh);
      s_mesh_cache.lod_valid = true;
      if (skip_sparse)
        fp_sparse_list_rebuild(&s_mesh_cache.sparse, world);
      else
        fp_sparse_list_clear(&s_mesh_cache.sparse);
      s_mesh_build_ms = (double)(SDL_GetPerformanceCounter() - build_start) * 1000.0 /
                        (double)SDL_GetPerformanceFrequency();
      s_mesh_cache.world = world;
      s_mesh_cache.world_hash = world_token;
      s_mesh_cache.valid = true;
      s_mesh_cache.provisional = true;
      s_mesh_cache.last_used = now;
      s_mesh_cache.built_at = now;
      s_mesh_needs_full = true;
      FP_DBG("[fp_renderer] mesh_cache: coarse provisional %d quads in %.2fms (full remesh deferred)\n",
             s_mesh_cache.mesh.count, s_mesh_build_ms);
      return &s_mesh_cache.mesh;
    }
  }

  fp_build_full_centre_mesh(world, world_token, now);
  return &s_mesh_cache.mesh;
}

// Distance-based LOD selection for the centre world mesh.
// Stride-2 LOD punches holes in contiguous surfaces, so it is only safe for very distant
// centre-world views (camera near a far corner). Prefer full mesh for normal play.
static const VoxelMesh *get_cached_mesh_lod(const World *world, float cam_dist_to_origin)
{
  VoxelMesh *full = get_cached_mesh(world);
  if (!full || !s_mesh_cache.lod_valid)
    return full;
  if (cam_dist_to_origin > (float)WORLD_SIZE_X * 1.75f)
    return &s_mesh_cache.mesh_lod;
  return full;
}

// Clean up mesh cache
static void cleanup_mesh_cache(void)
{
  fp_cached_mesh_free_contents(&s_mesh_cache);
  fp_cached_mesh_free_contents(&s_mesh_cache_prev);
  s_mesh_needs_full = false;
  for (int i = 0; i < FP_RING1_CACHE_CAP; i++)
  {
    if (s_ring1_cache[i].valid)
    {
      voxel_mesh_free(&s_ring1_cache[i].mesh_lod);
      fp_sparse_list_clear(&s_ring1_cache[i].sparse);
      s_ring1_cache[i].valid = false;
      s_ring1_cache[i].world = NULL;
      s_ring1_cache[i].shadow_slot = -1;
    }
  }
}

// Look up a cached mesh for a ring-1 neighbour. Builds only when allow_build is set
// (post-frame warm path) — never during raster. `want_full` requests centre-quality meshing.
static int fp_ring1_find_slot(const World *world, int shadow_slot)
{
  for (int i = 0; i < FP_RING1_CACHE_CAP; i++)
  {
    const Ring1CachedMesh *e = &s_ring1_cache[i];
    if (e->valid && e->world == world && e->shadow_slot == shadow_slot)
      return i;
  }
  return -1;
}

static int fp_ring1_alloc_slot(void)
{
  int slot_i = -1;
  Uint64 oldest = UINT64_MAX;
  for (int i = 0; i < FP_RING1_CACHE_CAP; i++)
  {
    if (!s_ring1_cache[i].valid)
      return i;
    if (s_ring1_cache[i].last_used < oldest)
    {
      oldest = s_ring1_cache[i].last_used;
      slot_i = i;
    }
  }
  return slot_i;
}

static void fp_ring1_store_mesh(Ring1CachedMesh *e, const World *world, int shadow_slot,
                                uint64_t token, Uint64 now, bool full_quality)
{
  if (e->valid)
  {
    voxel_mesh_free(&e->mesh_lod);
    fp_sparse_list_clear(&e->sparse);
  }

  const Uint64 build_start = SDL_GetPerformanceCounter();
  const bool skip_sparse = fp_subvoxel_enabled();
  voxel_mesh_init(&e->mesh_lod);
  if (full_quality)
    voxel_mesh_build_all_faces_greedy_ex(world, &e->mesh_lod, skip_sparse);
  else
    voxel_mesh_build_all_faces_greedy_lod_ex(world, &e->mesh_lod, 2, skip_sparse);
  if (skip_sparse)
    fp_sparse_list_rebuild(&e->sparse, world);
  else
    fp_sparse_list_clear(&e->sparse);
  s_ring1_build_ms = (double)(SDL_GetPerformanceCounter() - build_start) * 1000.0 /
                     (double)SDL_GetPerformanceFrequency();
  e->world = world;
  e->world_hash = token;
  e->shadow_slot = shadow_slot;
  e->valid = true;
  e->full_quality = full_quality;
  e->last_used = now;
  e->built_at = now;
  const Uint64 cool_ms =
      full_quality ? (Uint64)fmax(150.0, s_ring1_build_ms * 3.0)
                   : (Uint64)fmax(40.0, s_ring1_build_ms * 4.0);
  s_ring1_next_warm_ms = now + cool_ms;
  FP_DBG("[fp_renderer] ring1_mesh: slot %d %s %d quads (%d sparse) in %.2fms\n", shadow_slot,
         full_quality ? "FULL" : "coarse", e->mesh_lod.count, e->sparse.count, s_ring1_build_ms);
}

// Copy a full centre / parked mesh into the ring-1 cache — free when the player just crossed a
// boundary and the left-behind world is still resident.
static bool fp_try_seed_ring1_from_centre(const World *world, int shadow_slot, uint64_t token,
                                          Uint64 now)
{
  const CachedMesh *src = NULL;
  if (s_mesh_cache.valid && s_mesh_cache.world == world && !s_mesh_cache.provisional)
    src = &s_mesh_cache;
  else if (s_mesh_cache_prev.valid && s_mesh_cache_prev.world == world &&
           !s_mesh_cache_prev.provisional)
    src = &s_mesh_cache_prev;
  if (!src || src->mesh.count <= 0)
    return false;

  int idx = fp_ring1_find_slot(world, shadow_slot);
  if (idx < 0)
    idx = fp_ring1_alloc_slot();
  if (idx < 0)
    return false;

  Ring1CachedMesh *e = &s_ring1_cache[idx];
  if (e->valid)
  {
    voxel_mesh_free(&e->mesh_lod);
    fp_sparse_list_clear(&e->sparse);
  }
  voxel_mesh_init(&e->mesh_lod);
  voxel_mesh_copy(&e->mesh_lod, &src->mesh);
  fp_sparse_list_copy(&e->sparse, &src->sparse);
  e->world = world;
  e->world_hash = token;
  e->shadow_slot = shadow_slot;
  e->valid = true;
  e->full_quality = true;
  e->last_used = now;
  e->built_at = now;
  FP_DBG("[fp_renderer] ring1_mesh: seeded slot %d from centre cache (%d quads)\n", shadow_slot,
         e->mesh_lod.count);
  return true;
}

static const VoxelMesh *get_ring1_cached_mesh_ex(const World *world, int shadow_slot,
                                                bool allow_build, bool want_full)
{
  if (!world || shadow_slot < 0)
    return NULL;

  const Uint64 now = SDL_GetTicks();
  const uint64_t token = world_content_token(world) ^ (FP_MESH_SCHEMA * 0x9E3779B97F4A7C15ull) ^
                         (fp_subvoxel_enabled() ? 0xA5A5A5A5A5A5A5A5ull : 0ull);

  int idx = fp_ring1_find_slot(world, shadow_slot);
  if (idx >= 0)
  {
    Ring1CachedMesh *e = &s_ring1_cache[idx];
    if (e->world_hash == token)
    {
      e->last_used = now;
      // Upgrade a coarse stand-in when the warm path asks for full quality.
      if (want_full && !e->full_quality && allow_build)
      {
        fp_ring1_store_mesh(e, world, shadow_slot, token, now, true);
        return &e->mesh_lod;
      }
      return &e->mesh_lod;
    }
    const Uint64 min_interval = (Uint64)fmax(2000.0, s_ring1_build_ms * 20.0);
    if (now - e->built_at < min_interval)
    {
      e->last_used = now;
      return &e->mesh_lod; // slightly stale is fine for the horizon
    }
    if (!allow_build)
      return NULL;
  }
  else if (!allow_build)
  {
    if (fp_try_seed_ring1_from_centre(world, shadow_slot, token, now))
    {
      idx = fp_ring1_find_slot(world, shadow_slot);
      return idx >= 0 ? &s_ring1_cache[idx].mesh_lod : NULL;
    }
    return NULL;
  }

  if (!allow_build)
    return NULL;

  if (idx < 0)
  {
    if (fp_try_seed_ring1_from_centre(world, shadow_slot, token, now))
    {
      idx = fp_ring1_find_slot(world, shadow_slot);
      if (idx >= 0)
      {
        if (want_full && !s_ring1_cache[idx].full_quality)
          fp_ring1_store_mesh(&s_ring1_cache[idx], world, shadow_slot, token, now, true);
        return &s_ring1_cache[idx].mesh_lod;
      }
    }
    idx = fp_ring1_alloc_slot();
  }
  if (idx < 0)
    return NULL;

  // Full quality matches the centre world. Coarse only for a first-presence fill when the
  // just-presented frame was already busy — then the warm path upgrades it.
  const bool full = want_full || s_last_frame_ms <= FP_RING1_FULL_WARM_MS;
  fp_ring1_store_mesh(&s_ring1_cache[idx], world, shadow_slot, token, now, full);
  return &s_ring1_cache[idx].mesh_lod;
}

static const VoxelMesh *get_ring1_cached_mesh(const World *world, int shadow_slot, bool allow_build)
{
  return get_ring1_cached_mesh_ex(world, shadow_slot, allow_build, /*want_full=*/true);
}

// After present: if the last frame was comfortable, upgrade a provisional centre mesh or warm
// ring-1 neighbours toward centre-quality meshes (faces, then edges, then corners).
static void fp_warm_ring1_meshes(const ShadowWorld *cluster)
{
  if (!cluster)
    return;
  const Uint64 now = SDL_GetTicks();
  if (now < s_ring1_next_warm_ms)
    return;
  // At most one warm per millisecond — a tight bench loop would otherwise rebuild every call.
  if (now == s_ring1_last_warm_tick)
    return;
  // Only warm when the visible frame was well under the dyn-res budget.
  if (s_last_frame_ms > FP_DYN_RES_RECOVER_MS)
    return;

  // Prefer finishing the centre world the player is standing in before horizon LODs.
  if (s_mesh_needs_full && s_mesh_cache.valid && s_mesh_cache.world)
  {
    const World *centre = s_mesh_cache.world;
    const uint64_t token = fp_centre_world_token(centre);
    s_ring1_last_warm_tick = now;
    fp_build_full_centre_mesh(centre, token, now);
    const Uint64 cool_ms = (Uint64)fmax(100.0, s_mesh_build_ms * 2.0);
    s_ring1_next_warm_ms = now + cool_ms;
    return;
  }

  if (!fp_ring1_enabled())
    return;

  const bool can_full = s_last_frame_ms <= FP_RING1_FULL_WARM_MS;

  // Pass 1: upgrade coarse stand-ins to full (centre) quality — faces first.
  if (can_full)
  {
    for (int prefer = 1; prefer <= 3; prefer++)
    {
      for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
      {
        if (slot == SHADOW_CENTRE_SLOT || !(cluster->loaded_mask & SHADOW_SLOT_BIT(slot)))
          continue;
        int dx = 0, dy = 0, dz = 0;
        shadow_slot_offsets(slot, &dx, &dy, &dz);
        if (abs(dx) + abs(dy) + abs(dz) != prefer)
          continue;
        const World *w = shadow_world_slot_world(cluster, slot);
        if (!w)
          continue;
        const int idx = fp_ring1_find_slot(w, slot);
        if (idx < 0 || s_ring1_cache[idx].full_quality)
          continue;
        s_ring1_last_warm_tick = now;
        get_ring1_cached_mesh_ex(w, slot, true, /*want_full=*/true);
        return;
      }
    }
  }

  // Pass 2: fill any missing ring-1 slot. Prefer full when the frame was cheap; otherwise a
  // coarse lattice so the silhouette appears without hitching the next frame.
  for (int prefer = 1; prefer <= 3; prefer++)
  {
    for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
    {
      if (slot == SHADOW_CENTRE_SLOT || !(cluster->loaded_mask & SHADOW_SLOT_BIT(slot)))
        continue;
      int dx = 0, dy = 0, dz = 0;
      shadow_slot_offsets(slot, &dx, &dy, &dz);
      if (abs(dx) + abs(dy) + abs(dz) != prefer)
        continue;
      const World *w = shadow_world_slot_world(cluster, slot);
      if (!w)
        continue;
      if (get_ring1_cached_mesh_ex(w, slot, false, false))
        continue;
      s_ring1_last_warm_tick = now;
      get_ring1_cached_mesh_ex(w, slot, true, /*want_full=*/can_full);
      return;
    }
  }
}

// Get cached octree or build new one if needed
static Octree *get_cached_octree(const World *world)
{
  Uint64 current_time = SDL_GetTicks();
  uint64_t world_hash = calculate_world_hash(world);

  FP_DBG("[fp_renderer] octree_cache: checking cache (world=%p, hash=0x%llx, valid=%d)\n",
         (void *)world, (unsigned long long)world_hash, s_octree_cache.valid);

  // Check if cache is valid and up-to-date
  if (s_octree_cache.valid &&
      s_octree_cache.world == world &&
      s_octree_cache.world_hash == world_hash &&
      (current_time - s_octree_cache.last_used) < OCTREE_CACHE_MAX_AGE_MS)
  {
    s_octree_cache.last_used = current_time;
    FP_DBG("[fp_renderer] octree_cache: HIT - exact match (age=%llums)\n",
           (unsigned long long)(current_time - s_octree_cache.last_used));
    return s_octree_cache.octree;
  }

  // Check if we have a cached octree with the same hash (content-based cache)
  if (s_octree_cache.valid &&
      s_octree_cache.world_hash == world_hash &&
      (current_time - s_octree_cache.last_used) < OCTREE_CACHE_MAX_AGE_MS)
  {
    s_octree_cache.last_used = current_time;
    FP_DBG("[fp_renderer] octree_cache: HIT - content match (world pointer changed, but content identical)\n");
    return s_octree_cache.octree;
  }

  // Cache miss or expired, rebuild octree
  FP_DBG("[fp_renderer] octree_cache: MISS - building new octree\n");

  // Free old octree if it exists
  if (s_octree_cache.valid)
  {
    octree_destroy(s_octree_cache.octree);
  }

  // Build new octree
  Uint64 build_start = SDL_GetPerformanceCounter();
  Uint64 freq = SDL_GetPerformanceFrequency();

  // Safety check: ensure world is valid before building octree
  if (!world || !world->voxels || world->width <= 0 || world->height <= 0 || world->depth <= 0)
  {
    FP_DBG("[fp_renderer] octree_cache: SKIPPING - invalid world (ptr=%p, voxels=%p, dims=%dx%dx%d)\n",
           (void *)world, world ? (void *)world->voxels : NULL,
           world ? world->width : -1, world ? world->height : -1, world ? world->depth : -1);
    return NULL;
  }

  Octree *octree = octree_create(8, 1000000); // Max depth 8, pool size 1000000 for complex worlds
  if (octree)
  {
    octree_build_from_world(octree, world);
  }

  Uint64 build_end = SDL_GetPerformanceCounter();
  double build_ms = (double)(build_end - build_start) * 1000.0 / (double)freq;

  // Update cache
  s_octree_cache.octree = octree;
  s_octree_cache.world = world;
  s_octree_cache.world_hash = world_hash;
  s_octree_cache.valid = (octree != NULL);
  s_octree_cache.last_used = current_time;

  if (octree)
  {
    FP_DBG("[fp_renderer] octree_cache: built octree with %d nodes (%d leaves) in %.2fms\n",
           octree->node_count, octree->leaf_count, build_ms);
  }
  else
  {
    FP_DBG("[fp_renderer] octree_cache: FAILED to build octree\n");
  }

  return octree;
}

// Clean up octree cache
static void cleanup_octree_cache(void)
{
  if (s_octree_cache.valid)
  {
    octree_destroy(s_octree_cache.octree);
    s_octree_cache.valid = false;
    s_octree_cache.world = NULL;
    s_octree_cache.world_hash = 0;
  }
}

// Simple OpenGL initialization for mesh rendering
void fp_renderer_gl_init(void)
{
  // Enable depth testing for proper 3D rendering
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  // Enable backface culling for performance
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  // Note: Don't clear mesh cache here - let it persist during the program run
  // The cache will be automatically managed by get_cached_mesh()
}

// Let go of everything owned by the SDL_Renderer we were last handed.
//
// fp_ensure_streaming_texture recognises "same renderer as last time" by pointer, which stops being
// true the moment a renderer is destroyed: the allocator hands the same address back for the next
// one, the stale check passes, and we upload frames into a texture that died with its renderer —
// a black first-person panel and a use-after-free. Anything that destroys a renderer it has drawn
// through must call this first. Every tool today keeps a single renderer for its whole run, so
// nothing hits this in practice; the audit test renders the same scene through two of them to
// compare the ray and mesh paths, which is how it turned up.
void fp_renderer_set_projectiles(const ProjectileSystem *projectiles)
{
  s_fp_projectiles = projectiles;
}

void fp_renderer_set_debris(const DebrisSystem *debris)
{
  s_fp_debris = debris;
}

void fp_renderer_set_debris_volumes(const DebrisVolumeSystem *volumes)
{
  s_fp_debris_volumes = volumes;
}

void fp_renderer_set_swing(float yaw, float progress, float radius, float arc_rad, bool armed,
                           uint32_t strength)
{
  s_fp_swing_yaw = yaw;
  s_fp_swing_progress = progress;
  s_fp_swing_radius = radius > 0.1f ? radius : PLAYER_SWING_RADIUS;
  s_fp_swing_arc = arc_rad > 0.1f ? arc_rad : PLAYER_SWING_ARC_RAD;
  s_fp_swing_armed = armed;
  s_fp_swing_strength = strength > 0 ? strength : 10;
}

void fp_renderer_set_foliage_bend(const FoliageBendField *field)
{
  s_fp_foliage_bend = field;
}

void fp_renderer_release_renderer(void)
{
  if (s_fp_tex)
    SDL_DestroyTexture(s_fp_tex);
  s_fp_tex = NULL;
  s_fp_tex_w = 0;
  s_fp_tex_h = 0;
  s_last_renderer = NULL;
  gl_occupancy_destroy(&s_gl_occupancy);
}

// Invalidate mesh cache (call when world changes significantly)
void fp_renderer_invalidate_cache(void)
{
  cleanup_mesh_cache();
  s_cache_warmed_up = false;
  // Clear the recorded build cost too, so the rebuild throttle cannot defer the very rebuild an
  // explicit invalidation is asking for.
  s_mesh_build_ms = 0.0;
  FP_DBG("[fp_renderer] mesh_cache: invalidated and warm-up reset\n");
}

// Force immediate cache invalidation and rebuild
void fp_renderer_force_cache_rebuild(const World *world)
{
  FP_DBG("[fp_renderer] mesh_cache: FORCING immediate cache rebuild\n");

  // Clear existing cache
  cleanup_mesh_cache();
  s_cache_warmed_up = false;

  // Force rebuild on next access
  if (world)
  {
    // Invalidate by changing the world pointer
    s_mesh_cache.world = NULL;
    s_mesh_cache.world_hash = 0;
    s_mesh_cache.valid = false;
  }
}

// Reset warm-up state (useful for testing)
void fp_renderer_reset_warmup(void)
{
  s_cache_warmed_up = false;
  FP_DBG("[fp_renderer] mesh_cache: warm-up state reset\n");
}

// Set a pre-built mesh in the cache (useful for forcing fresh mesh usage)
void fp_renderer_set_cached_mesh(const World *world, const VoxelMesh *mesh)
{
  // Free old mesh if it exists
  if (s_mesh_cache.valid)
  {
    voxel_mesh_free(&s_mesh_cache.mesh);
    if (s_mesh_cache.lod_valid)
      voxel_mesh_free(&s_mesh_cache.mesh_lod);
    fp_sparse_list_clear(&s_mesh_cache.sparse);
  }

  // Copy the provided mesh to our cache
  voxel_mesh_init(&s_mesh_cache.mesh);
  voxel_mesh_copy(&s_mesh_cache.mesh, mesh);
  voxel_mesh_init(&s_mesh_cache.mesh_lod);
  voxel_mesh_copy(&s_mesh_cache.mesh_lod, mesh);
  s_mesh_cache.lod_valid = true;
  // Callers of set_cached_mesh typically already emitted sparse AABBs into `mesh`; do not also
  // instance nested silhouettes on top. Live play uses get_cached_mesh, which rebuilds the list.
  fp_sparse_list_clear(&s_mesh_cache.sparse);

  // Token must come from the same function get_cached_mesh compares against, or the caller's mesh
  // is treated as stale on the very next frame.
  s_mesh_cache.world = world;
  s_mesh_cache.world_hash = world_content_token(world) ^ (FP_MESH_SCHEMA * 0x9E3779B97F4A7C15ull) ^
                            (fp_subvoxel_enabled() ? 0xA5A5A5A5A5A5A5A5ull : 0ull);
  s_mesh_cache.valid = true;
  s_mesh_cache.last_used = SDL_GetTicks();
  s_mesh_cache.built_at = s_mesh_cache.last_used;
  s_cache_warmed_up = true;

  FP_DBG("[fp_renderer] mesh_cache: set pre-built mesh with %d quads (%d sparse)\n", mesh->count,
         s_mesh_cache.sparse.count);
}

// Octree spatial acceleration functions
void fp_renderer_set_world_octree(const World *world, Octree *octree)
{
  if (!world || !octree)
    return;

  // Free existing octree if it exists
  if (s_octree_cache.valid)
  {
    octree_destroy(s_octree_cache.octree);
  }

  // Set the provided octree
  s_octree_cache.octree = octree;
  s_octree_cache.world = world;
  s_octree_cache.world_hash = calculate_world_hash(world);
  s_octree_cache.valid = true;
  s_octree_cache.last_used = SDL_GetTicks();

  FP_DBG("[fp_renderer] set_world_octree: set octree with %d nodes (%d leaves) for world %p (hash=0x%llx)\n",
         octree->node_count, octree->leaf_count, (void *)world, (unsigned long long)s_octree_cache.world_hash);
}

Octree *fp_renderer_get_world_octree(const World *world)
{
  if (!world)
    return NULL;

  // Return cached octree if available and valid
  if (s_octree_cache.valid && s_octree_cache.world == world)
  {
    return s_octree_cache.octree;
  }

  // Build new octree if needed
  return get_cached_octree(world);
}

void fp_renderer_build_world_octree(const World *world)
{
  if (!world)
    return;

  FP_DBG("[fp_renderer] build_world_octree: building octree for world %p\n", (void *)world);
  get_cached_octree(world);
}

void fp_renderer_debug_quad_corners(const VoxelFaceQuad *quad, float corners[4][3])
{
  if (quad)
    fp_quad_world_corners(quad, corners);
}

bool fp_renderer_debug_project(const FPCamera *cam, int target_w, int target_h,
                               float wx, float wy, float wz,
                               float *screen_x, float *screen_y, float *depth)
{
  if (!cam || target_w <= 0 || target_h <= 0)
    return false;

  FPCameraBasis basis;
  fp_camera_build_basis(cam, &basis);
  basis.aspect = (float)target_w / (float)target_h;

  FPCameraPoint p;
  fp_to_camera_space(cam, &basis, wx, wy, wz, &p);
  if (p.z < FP_NEAR_PLANE)
    return false;

  fp_project_camera_point(&basis, &p, target_w, target_h, screen_x, screen_y, depth);
  return true;
}

void fp_renderer_debug_pixel_ray(const FPCamera *cam, int target_w, int target_h,
                                 float screen_x, float screen_y,
                                 float *dir_x, float *dir_y, float *dir_z)
{
  if (!cam || target_w <= 0 || target_h <= 0)
    return;

  FPCameraBasis basis;
  fp_camera_build_basis(cam, &basis);
  basis.aspect = (float)target_w / (float)target_h;

  const float nx = (2.0f * screen_x / (float)target_w) - 1.0f;
  const float ny = 1.0f - (2.0f * screen_y / (float)target_h);
  const float ax = basis.half_tan * basis.aspect;
  const float ay = basis.half_tan;

  *dir_x = basis.fwd_x + nx * ax * basis.right_x + ny * ay * basis.up_x;
  *dir_y = basis.fwd_y + nx * ax * basis.right_y + ny * ay * basis.up_y;
  *dir_z = basis.fwd_z + nx * ax * basis.right_z + ny * ay * basis.up_z;
}

void fp_renderer_set_mode(FPMode mode)
{
  s_fp_mode = mode;
}

void fp_renderer_enable_gpu_mesh(bool enable)
{
  s_fp_gpu_mesh_enabled = enable;
}

// Main rendering function
void fp_renderer_render(SDL_Renderer *ren,
                        const World *world,
                        const FPCamera *cam,
                        int panel_x, int panel_y, int panel_w, int panel_h,
                        const FogAtlas *fog)
{
  if (!ren || !world || !cam || panel_w <= 0 || panel_h <= 0)
    return;

  if (s_fp_mode == FP_MODE_RAY)
  {
    const bool opaque_bg = (panel_w > 600 && panel_h > 400);
    SDL_SetRenderDrawBlendMode(ren, opaque_bg ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, FP_SKY_R, FP_SKY_G, FP_SKY_B, opaque_bg ? 255 : 160);
    SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
    SDL_RenderFillRect(ren, &panel);

    GameWorlds temp = {0};
    temp.home_world = (World *)world;
    fp_renderer_render_neighbors(ren, &temp, cam, panel_x, panel_y, panel_w, panel_h, 0, fog);
    return;
  }

  if (!world->voxels || world->width == 0 || world->height == 0 || world->depth == 0)
  {
    render_raycast_fallback(ren, world, cam, panel_x, panel_y, panel_w, panel_h, fog, NULL);
    return;
  }

  VoxelMesh *mesh = get_cached_mesh(world);
  s_cache_warmed_up = true;
  if (mesh && mesh->count > 0)
  {
    if (s_fp_gpu_mesh_enabled)
      render_mesh_gpu_ideal_opt(ren, world, mesh, cam, panel_x, panel_y, panel_w, panel_h);
    else
      render_mesh_cpu(ren, world, mesh, cam, panel_x, panel_y, panel_w, panel_h, fog);
    return;
  }

  render_raycast_fallback(ren, world, cam, panel_x, panel_y, panel_w, panel_h, fog, NULL);
}

static bool validate_mesh_for_rendering(const VoxelMesh *mesh, const World *world, const char *caller)
{
  if (!mesh || !world || !mesh->quads || mesh->count <= 0)
  {
    FP_DBG("[fp_renderer] %s: mesh unusable (mesh=%p world=%p quads=%p count=%d)\n",
           caller, (const void *)mesh, (const void *)world,
           mesh ? (const void *)mesh->quads : NULL, mesh ? mesh->count : 0);
    return false;
  }
  return true;
}

// Clear the panel, size the render target, and lock it. On success every pixel is sky at
// FP_DEPTH_FAR and the caller owes a matching fp_frame_present.
static bool fp_frame_lock(SDL_Renderer *ren, int panel_x, int panel_y, int panel_w, int panel_h,
                          FPFrameBuffer *fb)
{
  const bool opaque_bg = (panel_w > 600 && panel_h > 400);
  SDL_SetRenderDrawBlendMode(ren, opaque_bg ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren, FP_SKY_R, FP_SKY_G, FP_SKY_B, opaque_bg ? 255 : 160);
  SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
  SDL_RenderFillRect(ren, &panel);

  int target_w = 0, target_h = 0;
  fp_compute_target_size(panel_w, panel_h, &target_w, &target_h, NULL);
  if (!fp_ensure_streaming_texture(ren, target_w, target_h))
    return false;

  void *pixels = NULL;
  int pitch = 0;
  if (SDL_LockTexture(s_fp_tex, NULL, &pixels, &pitch) != 0 || !pixels)
    return false;

  const int stride = (pitch > 0) ? (pitch / (int)sizeof(Uint32)) : target_w;
  const int pixel_count = stride * target_h;
  if (s_depth_cap < pixel_count)
  {
    float *nb = (float *)realloc(s_depth_buffer, (size_t)pixel_count * sizeof(float));
    if (!nb)
    {
      SDL_UnlockTexture(s_fp_tex);
      return false;
    }
    s_depth_buffer = nb;
    s_depth_cap = pixel_count;
  }
  if (s_voxel_id_cap < pixel_count)
  {
    int32_t *nb = (int32_t *)realloc(s_voxel_id, (size_t)pixel_count * sizeof(int32_t));
    if (!nb)
    {
      SDL_UnlockTexture(s_fp_tex);
      return false;
    }
    s_voxel_id = nb;
    s_voxel_id_cap = pixel_count;
  }

  fb->color = (Uint32 *)pixels;
  fb->depth = s_depth_buffer;
  fb->stride = stride;
  fb->w = target_w;
  fb->h = target_h;
  s_fp_locked_pixels = fb->color;
  s_fp_locked_stride = stride;
  s_fp_frame_t0 = SDL_GetPerformanceCounter();

  const Uint32 sky = fp_rgba8888(FP_SKY_R, FP_SKY_G, FP_SKY_B);
  for (int i = 0; i < pixel_count; i++)
  {
    fb->depth[i] = FP_DEPTH_FAR;
    fb->color[i] = sky;
    s_voxel_id[i] = -1;
  }
  return true;
}

// Draw whatever the player has in flight into the frame, as depth-tested billboards.
//
// Done here, into the CPU buffers before they are unlocked, rather than as SDL draw calls over the
// finished frame, because this is the only place with the depth buffer: a fireball behind a hill has
// to be hidden by it, and a pass that ran after the present would have nothing left to test against.
// The sprite is a radial falloff evaluated per pixel — a fireball is a glow, and a glow is cheaper to
// compute than to store.
// `occlusion_world` is only consulted when the frame has no depth buffer, which is the case for the
// raycast path: there, visibility is decided once per projectile by casting a ray at it, rather than
// per pixel. Coarser — a fireball half behind a corner is either wholly drawn or wholly hidden — but
// it costs one ray per projectile instead of a depth buffer the raycast path does not otherwise need.
static void fp_draw_projectiles(const FPFrameBuffer *fb, const FPCamera *cam,
                                const World *occlusion_world)
{
  if (!s_fp_projectiles || !fb || !fb->color)
    return;

  for (int i = 0; i < PROJECTILE_MAX; i++)
  {
    const Projectile *p = &s_fp_projectiles->items[i];
    if (!p->active)
      continue;

    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    if (!fp_renderer_debug_project(cam, fb->w, fb->h, p->x, p->y, p->z, &sx, &sy, &depth))
      continue; // behind the near plane

    if (!fb->depth)
    {
      if (!occlusion_world)
        continue;

      const float dx = p->x - cam->x, dy = p->y - cam->y, dz = p->z - cam->z;
      const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
      if (dist > 0.01f)
      {
        int hx = -1, hy = -1, hz = -1;
        const int steps = (int)dist + 2;
        if (world_raycast_first_hit(occlusion_world, cam->x, cam->y, cam->z, dx / dist, dy / dist,
                                    dz / dist, steps, &hx, &hy, &hz))
        {
          const float hdx = (float)hx + 0.5f - cam->x;
          const float hdy = (float)hy + 0.5f - cam->y;
          const float hdz = (float)hz + 0.5f - cam->z;
          if (sqrtf(hdx * hdx + hdy * hdy + hdz * hdz) < dist)
            continue; // a wall between the camera and the fireball
        }
      }
    }

    // Radius in pixels from the projectile's world radius, so it grows as it approaches. The
    // half-height of the viewport over the depth is the same perspective divide the projection uses.
    float radius = (p->radius * 2.5f) * ((float)fb->h * 0.5f) / (depth > 0.01f ? depth : 0.01f);
    if (radius < 1.5f)
      radius = 1.5f;
    if (radius > (float)fb->h) // a fireball at point blank should not repaint the whole screen
      radius = (float)fb->h;

    float life_ref = PLAYER_FIREBALL_LIFE_S;
    if (p->kind == PROJECTILE_ICE_BOLT)
      life_ref = PLAYER_ICE_BOLT_LIFE_S;
    else if (p->kind == PROJECTILE_MAGIC_MISSILE)
      life_ref = PLAYER_MAGIC_MISSILE_LIFE_S;
    else if (p->kind == PROJECTILE_LIGHTNING)
      life_ref = PLAYER_LIGHTNING_BOLT_LIFE_S;
    else if (p->kind == PROJECTILE_SHADOW_STRIKE)
      life_ref = PLAYER_SHADOW_STRIKE_LIFE_S;
    else if (p->kind == PROJECTILE_METEOR)
      life_ref = PLAYER_METEOR_LIFE_S;
    float fade = p->life / life_ref;
    if (fade > 1.0f)
      fade = 1.0f;
    if (fade < 0.25f)
      fade = 0.25f;

    const int x0 = (int)floorf(sx - radius), x1 = (int)ceilf(sx + radius);
    const int y0 = (int)floorf(sy - radius), y1 = (int)ceilf(sy + radius);
    const float r2 = radius * radius;

    for (int y = (y0 < 0 ? 0 : y0); y <= y1 && y < fb->h; y++)
    {
      for (int x = (x0 < 0 ? 0 : x0); x <= x1 && x < fb->w; x++)
      {
        const float dx = (float)x + 0.5f - sx;
        const float dy = (float)y + 0.5f - sy;
        const float d2 = dx * dx + dy * dy;
        if (d2 > r2)
          continue;

        const int idx = y * fb->stride + x;
        if (fb->depth && fb->depth[idx] < depth)
          continue; // something solid is in front of it

        // Brightest at the core, falling to nothing at the rim.
        const float t = 1.0f - sqrtf(d2 / r2);
        const float a = t * t * fade;

        uint8_t sr, sg, sb;
        if (p->kind == PROJECTILE_ICE_BOLT)
        {
          sr = (uint8_t)(160.0f + 95.0f * t);
          sg = (uint8_t)(200.0f + 55.0f * t);
          sb = 255;
        }
        else if (p->kind == PROJECTILE_MAGIC_MISSILE)
        {
          sr = (uint8_t)(180.0f + 75.0f * t);
          sg = (uint8_t)(100.0f + 80.0f * t);
          sb = 255;
        }
        else if (p->kind == PROJECTILE_LIGHTNING)
        {
          sr = (uint8_t)(200.0f + 55.0f * t);
          sg = (uint8_t)(220.0f + 35.0f * t);
          sb = 255;
        }
        else if (p->kind == PROJECTILE_SHADOW_STRIKE)
        {
          sr = (uint8_t)(160.0f + 60.0f * t);
          sg = (uint8_t)(40.0f + 40.0f * t);
          sb = (uint8_t)(180.0f + 75.0f * t);
        }
        else if (p->kind == PROJECTILE_METEOR)
        {
          sr = 255;
          sg = (uint8_t)(80.0f + 140.0f * t);
          sb = (uint8_t)(20.0f + 80.0f * t * t);
        }
        else
        {
          sr = 255;
          sg = (uint8_t)(120.0f + 120.0f * t);
          sb = (uint8_t)(40.0f + 150.0f * t * t);
        }

        const Uint32 dst = fb->color[idx];
        const uint8_t dr = (uint8_t)((dst >> 24) & 0xFFu);
        const uint8_t dg = (uint8_t)((dst >> 16) & 0xFFu);
        const uint8_t db = (uint8_t)((dst >> 8) & 0xFFu);

        const uint8_t orr = (uint8_t)(dr + (sr - dr) * a);
        const uint8_t og = (uint8_t)(dg + (sg - dg) * a);
        const uint8_t ob = (uint8_t)(db + (sb - db) * a);

        fb->color[idx] = ((Uint32)orr << 24) | ((Uint32)og << 16) | ((Uint32)ob << 8) | 0xFFu;
        // Deliberately not written to the depth buffer: a fireball is a glow, not a surface, so it
        // should not occlude the one behind it.
      }
    }
  }
}

// Dust, rain, and flame particles as depth-tested filled squares — same chunky look as iso
// (size_px screen rects, flat alpha), not soft perspective billboards.
static void fp_draw_particles(const FPFrameBuffer *fb, const FPCamera *cam, const World *world)
{
  if (!world || !world->particle_effects || !fb || !fb->color)
    return;

  const WorldParticleEffects *effects = world->particle_effects;
  for (int i = 0; i < effects->particle_count; i++)
  {
    const Particle *p = &effects->particles[i];
    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    if (!fp_renderer_debug_project(cam, fb->w, fb->h, p->x, p->y, p->z, &sx, &sy, &depth))
      continue;

    float fade = (p->max_life > 0.0f) ? (p->life / p->max_life) : 0.0f;
    if (fade < 0.0f)
      fade = 0.0f;
    if (fade > 1.0f)
      fade = 1.0f;
    const float alpha = ((float)p->a / 255.0f) * fade;
    if (alpha < 0.02f)
      continue;

    const int radius = (int)(p->size_px + 0.5f);
    const int cx = (int)(sx + 0.5f);
    const int cy = (int)(sy + 0.5f);
    const int x0 = cx - radius, x1 = cx + radius;
    const int y0 = cy - radius, y1 = cy + radius;

    for (int y = (y0 < 0 ? 0 : y0); y <= y1 && y < fb->h; y++)
    {
      for (int x = (x0 < 0 ? 0 : x0); x <= x1 && x < fb->w; x++)
      {
        const int idx = y * fb->stride + x;
        if (fb->depth && fb->depth[idx] < depth)
          continue;

        const Uint32 dst = fb->color[idx];
        const uint8_t dr = (uint8_t)((dst >> 24) & 0xFFu);
        const uint8_t dg = (uint8_t)((dst >> 16) & 0xFFu);
        const uint8_t db = (uint8_t)((dst >> 8) & 0xFFu);

        const uint8_t orr = (uint8_t)(dr + (int)(((int)p->r - (int)dr) * alpha));
        const uint8_t og = (uint8_t)(dg + (int)(((int)p->g - (int)dg) * alpha));
        const uint8_t ob = (uint8_t)(db + (int)(((int)p->b - (int)db) * alpha));

        fb->color[idx] = ((Uint32)orr << 24) | ((Uint32)og << 16) | ((Uint32)ob << 8) | 0xFFu;
      }
    }
  }
}

static void fp_draw_debris(const FPFrameBuffer *fb, const FPCamera *cam, const World *occlusion_world)
{
  if (!s_fp_debris || !fb || !fb->color)
    return;

  for (int i = 0; i < DEBRIS_MAX; i++)
  {
    const DebrisPiece *d = &s_fp_debris->items[i];
    if (!d->active)
      continue;

    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    if (!fp_renderer_debug_project(cam, fb->w, fb->h, d->x, d->y, d->z, &sx, &sy, &depth))
      continue;

    if (!fb->depth && occlusion_world)
    {
      const float dx = d->x - cam->x, dy = d->y - cam->y, dz = d->z - cam->z;
      const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
      if (dist > 0.01f)
      {
        int hx = -1, hy = -1, hz = -1;
        const int steps = (int)dist + 2;
        if (world_raycast_first_hit(occlusion_world, cam->x, cam->y, cam->z, dx / dist, dy / dist,
                                    dz / dist, steps, &hx, &hy, &hz))
        {
          const float hdx = (float)hx + 0.5f - cam->x;
          const float hdy = (float)hy + 0.5f - cam->y;
          const float hdz = (float)hz + 0.5f - cam->z;
          if (sqrtf(hdx * hdx + hdy * hdy + hdz * hdz) < dist)
            continue;
        }
      }
    }

    float radius = DEBRIS_SIZE * ((float)fb->h * 0.5f) / (depth > 0.01f ? depth : 0.01f);
    if (radius < 1.0f)
      radius = 1.0f;
    if (radius > 24.0f)
      radius = 24.0f;

    const int x0 = (int)floorf(sx - radius), x1 = (int)ceilf(sx + radius);
    const int y0 = (int)floorf(sy - radius), y1 = (int)ceilf(sy + radius);
    const float r2 = radius * radius;

    for (int y = (y0 < 0 ? 0 : y0); y <= y1 && y < fb->h; y++)
    {
      for (int x = (x0 < 0 ? 0 : x0); x <= x1 && x < fb->w; x++)
      {
        const float pdx = (float)x + 0.5f - sx;
        const float pdy = (float)y + 0.5f - sy;
        if (pdx * pdx + pdy * pdy > r2)
          continue;
        const int idx = y * fb->stride + x;
        if (fb->depth && fb->depth[idx] < depth)
          continue;
        fb->color[idx] = ((Uint32)d->r << 24) | ((Uint32)d->g << 16) | ((Uint32)d->b << 8) | 0xFFu;
        if (fb->depth)
          fb->depth[idx] = depth;
      }
    }
  }
}

static void fp_draw_sphere(const FPFrameBuffer *fb, const FPCamera *cam, const World *occlusion_world,
                           float wx, float wy, float wz, float radius, uint8_t sr, uint8_t sg,
                           uint8_t sb);

static void fp_draw_volume_face(const FPFrameBuffer *fb, const FPCamera *cam,
                                const FPCameraBasis *basis, const float c[4][3], float nx, float ny,
                                float nz, uint8_t r, uint8_t g, uint8_t b, float light)
{
  const float cx = (c[0][0] + c[1][0] + c[2][0] + c[3][0]) * 0.25f;
  const float cy = (c[0][1] + c[1][1] + c[2][1] + c[3][1]) * 0.25f;
  const float cz = (c[0][2] + c[1][2] + c[2][2] + c[3][2]) * 0.25f;
  const float to_cam_x = cam->x - cx;
  const float to_cam_y = cam->y - cy;
  const float to_cam_z = cam->z - cz;
  if (nx * to_cam_x + ny * to_cam_y + nz * to_cam_z <= 0.0f)
    return;

  float shade = (0.42f + 0.58f * fmaxf(0.0f, nx * 0.25f + ny * 0.15f + nz * 0.96f)) * light;
  uint8_t cr = (uint8_t)fminf(255.0f, (float)r * shade);
  uint8_t cg = (uint8_t)fminf(255.0f, (float)g * shade);
  uint8_t cb = (uint8_t)fminf(255.0f, (float)b * shade);
  // Volumes draw after the lit pass, so light-then-fog is correct here in one step.
  const float dist = sqrtf(to_cam_x * to_cam_x + to_cam_y * to_cam_y + to_cam_z * to_cam_z);
  fp_mix_fog(fp_fog_factor(dist), &cr, &cg, &cb);

  FPCameraPoint cam_pts[4];
  for (int i = 0; i < 4; i++)
    fp_to_camera_space(cam, basis, c[i][0], c[i][1], c[i][2], &cam_pts[i]);

  FPCameraPoint clipped[8];
  const int clipped_count = fp_clip_near(cam_pts, 4, clipped);
  if (clipped_count < 3)
    return;

  FPSurface surf;
  memset(&surf, 0, sizeof(surf));
  surf.flat = fp_rgba8888(cr, cg, cb);
  surf.type = VOXEL_AIR;
  surf.shade = shade;
  surf.nx = nx;
  surf.ny = ny;
  surf.nz = nz;

  FPScreenVertex verts[8];
  for (int ci = 0; ci < clipped_count; ci++)
  {
    fp_project_camera_point(basis, &clipped[ci], fb->w, fb->h, &verts[ci].x, &verts[ci].y,
                            &verts[ci].z);
    const float iw = (fabsf(verts[ci].z) > 1e-9f) ? (1.0f / verts[ci].z) : 0.0f;
    verts[ci].iw = iw;
    verts[ci].u_over_z = 0.0f;
    verts[ci].v_over_z = 0.0f;
    verts[ci].wx_over_z = clipped[ci].wx * iw;
    verts[ci].wy_over_z = clipped[ci].wy * iw;
    verts[ci].wz_over_z = clipped[ci].wz * iw;
  }
  for (int ci = 2; ci < clipped_count; ci++)
    fp_fill_triangle(fb->depth, fb->color, fb->stride, fb->w, fb->h, &verts[0], &verts[ci - 1],
                     &verts[ci], &surf);
}

static void fp_draw_debris_volumes(const FPFrameBuffer *fb, const FPCamera *cam,
                                   const World *occlusion_world, const ShadowWorld *cluster)
{
  if (!s_fp_debris_volumes || !fb || !fb->color || !cam)
    return;

  const DebrisVolume *vols[DEBRIS_VOLUME_MAX];
  const int n = voxel_debris_volume_gather(s_fp_debris_volumes, occlusion_world, vols,
                                           DEBRIS_VOLUME_MAX);
  if (n <= 0)
    return;

  // Ray path has no depth buffer; keep the cheap sphere proxy there.
  if (!fb->depth)
  {
    for (int v = 0; v < n; v++)
    {
      const DebrisVolume *vol = vols[v];
      for (int i = 0; i < vol->cell_count; i++)
      {
      const DebrisVolumeCell *c = &vol->cells[i];
      float wx, wy, wz;
      voxel_debris_volume_cell_world(vol, c, &wx, &wy, &wz);
      wx += 0.5f;
      wy += 0.5f;
      wz += 0.5f;
      uint8_t r = 140, g = 140, b = 140;
      material_worlds_sample(c->type, MATERIAL_FACE_TOP, 0.5f, 0.5f, &r, &g, &b);
      fp_draw_sphere(fb, cam, occlusion_world, wx, wy, wz, 0.42f, r, g, b);
      }
    }
    return;
  }

  FPCameraBasis local;
  fp_camera_build_basis(cam, &local);
  local.aspect = (fb->h > 0) ? ((float)fb->w / (float)fb->h) : 1.0f;

  for (int v = 0; v < n; v++)
  {
    const DebrisVolume *vol = vols[v];
    for (int i = 0; i < vol->cell_count; i++)
    {
      const DebrisVolumeCell *cell = &vol->cells[i];
      float x0, y0, z0;
      voxel_debris_volume_cell_world(vol, cell, &x0, &y0, &z0);
      const float x1 = x0 + 1.0f, y1 = y0 + 1.0f, z1 = z0 + 1.0f;
      const float cx = x0 + 0.5f, cy = y0 + 0.5f, cz = z0 + 0.5f;

      float light = 1.0f;
      if (s_fp_lighting)
      {
        const int lx = (int)floorf(cx) + (cluster ? s_light_ox : 0);
        const int ly = (int)floorf(cy) + (cluster ? s_light_oy : 0);
        const int lz = (int)floorf(cz) + (cluster ? s_light_oz : 0);
        const uint32_t key = (uint32_t)fp_pack_vox(lx, ly, lz);
        const uint8_t L = fp_cached_light(cluster, occlusion_world, key, lx, ly, lz);
        light = (float)L / 255.0f * fp_actor_shadow_smooth(key, cx, cy, cz);
      }

      static const struct
      {
        float nx, ny, nz;
        MaterialFace face;
      } faces[6] = {
          {0, 0, 1, MATERIAL_FACE_TOP},    {0, 0, -1, MATERIAL_FACE_BOTTOM},
          {0, 1, 0, MATERIAL_FACE_LEFT},   {0, -1, 0, MATERIAL_FACE_BACK},
          {1, 0, 0, MATERIAL_FACE_RIGHT},  {-1, 0, 0, MATERIAL_FACE_FRONT},
      };
      for (int f = 0; f < 6; f++)
      {
        uint8_t r = 140, g = 140, b = 140;
        material_worlds_sample(cell->type, faces[f].face, 0.5f, 0.5f, &r, &g, &b);
        float face[4][3];
        const float nx = faces[f].nx, ny = faces[f].ny, nz = faces[f].nz;
        if (nz > 0.5f)
        {
          face[0][0] = x0; face[0][1] = y0; face[0][2] = z1;
          face[1][0] = x1; face[1][1] = y0; face[1][2] = z1;
          face[2][0] = x1; face[2][1] = y1; face[2][2] = z1;
          face[3][0] = x0; face[3][1] = y1; face[3][2] = z1;
        }
        else if (nz < -0.5f)
        {
          face[0][0] = x0; face[0][1] = y0; face[0][2] = z0;
          face[1][0] = x0; face[1][1] = y1; face[1][2] = z0;
          face[2][0] = x1; face[2][1] = y1; face[2][2] = z0;
          face[3][0] = x1; face[3][1] = y0; face[3][2] = z0;
        }
        else if (ny > 0.5f)
        {
          face[0][0] = x0; face[0][1] = y1; face[0][2] = z0;
          face[1][0] = x0; face[1][1] = y1; face[1][2] = z1;
          face[2][0] = x1; face[2][1] = y1; face[2][2] = z1;
          face[3][0] = x1; face[3][1] = y1; face[3][2] = z0;
        }
        else if (ny < -0.5f)
        {
          face[0][0] = x0; face[0][1] = y0; face[0][2] = z0;
          face[1][0] = x1; face[1][1] = y0; face[1][2] = z0;
          face[2][0] = x1; face[2][1] = y0; face[2][2] = z1;
          face[3][0] = x0; face[3][1] = y0; face[3][2] = z1;
        }
        else if (nx > 0.5f)
        {
          face[0][0] = x1; face[0][1] = y0; face[0][2] = z0;
          face[1][0] = x1; face[1][1] = y1; face[1][2] = z0;
          face[2][0] = x1; face[2][1] = y1; face[2][2] = z1;
          face[3][0] = x1; face[3][1] = y0; face[3][2] = z1;
        }
        else
        {
          face[0][0] = x0; face[0][1] = y0; face[0][2] = z0;
          face[1][0] = x0; face[1][1] = y0; face[1][2] = z1;
          face[2][0] = x0; face[2][1] = y1; face[2][2] = z1;
          face[3][0] = x0; face[3][1] = y1; face[3][2] = z0;
        }
        fp_draw_volume_face(fb, cam, &local, face, nx, ny, nz, r, g, b, light);
      }
    }
  }
}

// World-space melee overlay. Armed: translucent damage sector. Unarmed: a pill (cylinder +
// rounded ends) that spawns with the swing and sweeps the damage arc, then despawns with it.
static void fp_draw_capsule(const FPFrameBuffer *fb, const FPCamera *cam, float x0, float y0,
                            float z0, float x1, float y1, float z1, float radius, uint8_t sr,
                            uint8_t sg, uint8_t sb)
{
  const float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
  const float len = sqrtf(dx * dx + dy * dy + dz * dz);
  if (len < 1e-4f)
  {
    fp_draw_sphere(fb, cam, NULL, x0, y0, z0, radius, sr, sg, sb);
    return;
  }
  int steps = (int)(len / fmaxf(radius * 0.35f, 0.04f)) + 1;
  if (steps < 4)
    steps = 4;
  if (steps > 28)
    steps = 28;
  for (int i = 0; i <= steps; i++)
  {
    float u = (float)i / (float)steps;
    fp_draw_sphere(fb, cam, NULL, x0 + dx * u, y0 + dy * u, z0 + dz * u, radius, sr, sg, sb);
  }
}

static void fp_draw_swing_sector(const FPFrameBuffer *fb, const FPCamera *cam)
{
  const float half = s_fp_swing_arc * 0.5f;
  const float inner = PLAYER_SWING_INNER_RADIUS;
  const float outer = s_fp_swing_radius;
  const float z = cam->z - 0.6f;
  const float progress = s_fp_swing_progress;

  float bright = 0.55f;
  if (progress >= PLAYER_SWING_HIT_START && progress <= PLAYER_SWING_HIT_END)
    bright = 1.0f;
  else if (progress < PLAYER_SWING_HIT_START)
    bright = 0.4f + 0.35f * (progress / PLAYER_SWING_HIT_START);
  else
    bright = 0.55f * (1.0f - (progress - PLAYER_SWING_HIT_END) / (1.0f - PLAYER_SWING_HIT_END));

  const uint8_t sr = 170, sg = 220, sb = 255;
  const int rays = 20;
  const int rings = 6;
  for (int i = 0; i <= rays; i++)
  {
    float t = -half + (2.0f * half) * ((float)i / (float)rays);
    float ang = s_fp_swing_yaw + t;
    float c = cosf(ang), sn = sinf(ang);

    float sx0 = 0.0f, sy0 = 0.0f, d0 = 0.0f;
    float sx1 = 0.0f, sy1 = 0.0f, d1 = 0.0f;
    const bool ok0 = fp_renderer_debug_project(cam, fb->w, fb->h, cam->x + c * inner,
                                               cam->y + sn * inner, z, &sx0, &sy0, &d0);
    const bool ok1 = fp_renderer_debug_project(cam, fb->w, fb->h, cam->x + c * outer,
                                               cam->y + sn * outer, z, &sx1, &sy1, &d1);
    if (!ok0 || !ok1)
      continue;

    for (int ring = 0; ring <= rings; ring++)
    {
      float u = (float)ring / (float)rings;
      float sx = sx0 + (sx1 - sx0) * u;
      float sy = sy0 + (sy1 - sy0) * u;
      float depth = d0 + (d1 - d0) * u;
      float fade = (0.35f + 0.65f * u) * bright;
      if (i == 0 || i == rays)
        fade = fminf(1.0f, fade + 0.35f);

      uint8_t a = (uint8_t)(210.0f * fade);
      int px = (int)sx, py = (int)sy;
      if (px < 0 || py < 0 || px >= fb->w || py >= fb->h)
        continue;
      const int idx = py * fb->stride + px;
      if (fb->depth && fb->depth[idx] + 0.05f < depth)
        continue;

      const Uint32 dst = fb->color[idx];
      const uint8_t dr = (uint8_t)((dst >> 24) & 0xFFu);
      const uint8_t dg = (uint8_t)((dst >> 16) & 0xFFu);
      const uint8_t db = (uint8_t)((dst >> 8) & 0xFFu);
      const float af = (float)a / 255.0f;
      const uint8_t orr = (uint8_t)(dr + (int)((sr - dr) * af));
      const uint8_t og = (uint8_t)(dg + (int)((sg - dg) * af));
      const uint8_t ob = (uint8_t)(db + (int)((sb - db) * af));
      fb->color[idx] = ((Uint32)orr << 24) | ((Uint32)og << 16) | ((Uint32)ob << 8) | 0xFFu;
    }
  }

  for (int i = 0; i < rays; i++)
  {
    float t0 = -half + (2.0f * half) * ((float)i / (float)rays);
    float t1 = -half + (2.0f * half) * ((float)(i + 1) / (float)rays);
    float a0 = s_fp_swing_yaw + t0, a1 = s_fp_swing_yaw + t1;
    float sx0, sy0, d0, sx1, sy1, d1;
    if (!fp_renderer_debug_project(cam, fb->w, fb->h, cam->x + cosf(a0) * outer,
                                   cam->y + sinf(a0) * outer, z, &sx0, &sy0, &d0) ||
        !fp_renderer_debug_project(cam, fb->w, fb->h, cam->x + cosf(a1) * outer,
                                   cam->y + sinf(a1) * outer, z, &sx1, &sy1, &d1))
      continue;

    int steps = (int)(fabsf(sx1 - sx0) + fabsf(sy1 - sy0)) + 1;
    if (steps > 32)
      steps = 32;
    for (int s = 0; s <= steps; s++)
    {
      float u = (float)s / (float)steps;
      int px = (int)(sx0 + (sx1 - sx0) * u);
      int py = (int)(sy0 + (sy1 - sy0) * u);
      float depth = d0 + (d1 - d0) * u;
      if (px < 0 || py < 0 || px >= fb->w || py >= fb->h)
        continue;
      const int idx = py * fb->stride + px;
      if (fb->depth && fb->depth[idx] + 0.05f < depth)
        continue;
      const Uint32 dst = fb->color[idx];
      const uint8_t dr = (uint8_t)((dst >> 24) & 0xFFu);
      const uint8_t dg = (uint8_t)((dst >> 16) & 0xFFu);
      const uint8_t db = (uint8_t)((dst >> 8) & 0xFFu);
      const float af = 0.85f * bright;
      const uint8_t orr = (uint8_t)(dr + (int)((255 - dr) * af));
      const uint8_t og = (uint8_t)(dg + (int)((255 - dg) * af));
      const uint8_t ob = (uint8_t)(db + (int)((255 - db) * af));
      fb->color[idx] = ((Uint32)orr << 24) | ((Uint32)og << 16) | ((Uint32)ob << 8) | 0xFFu;
    }
  }
}

// Unarmed fist/arm: a pill spanning [inner, outer] that sweeps facing ± half_arc with progress.
static void fp_draw_swing_pill(const FPFrameBuffer *fb, const FPCamera *cam)
{
  const float half = s_fp_swing_arc * 0.5f;
  const float inner = PLAYER_SWING_INNER_RADIUS;
  const float outer = s_fp_swing_radius;
  const float progress = s_fp_swing_progress;
  // Sweep the full damage sector over the animation lifetime.
  const float ang = s_fp_swing_yaw - half + (2.0f * half) * progress;
  const float c = cosf(ang), sn = sinf(ang);
  // Slightly below the eye and biased to the right so it reads as an arm in view.
  const float right_x = -sinf(s_fp_swing_yaw);
  const float right_y = cosf(s_fp_swing_yaw);
  const float hand_z = cam->z - 0.35f;
  const float side = 0.12f;
  const float x0 = cam->x + c * inner + right_x * side;
  const float y0 = cam->y + sn * inner + right_y * side;
  const float x1 = cam->x + c * outer + right_x * side * 0.35f;
  const float y1 = cam->y + sn * outer + right_y * side * 0.35f;

  float pill_r = PLAYER_UNARMED_PILL_RADIUS_BASE +
                 PLAYER_UNARMED_PILL_RADIUS_PER_STR * (float)s_fp_swing_strength;
  if (pill_r < 0.08f)
    pill_r = 0.08f;
  if (pill_r > 0.32f)
    pill_r = 0.32f;

  // Brighten through the hit window so the punch reads when damage lands.
  uint8_t sr = 255, sg = 170, sb = 90;
  if (progress >= PLAYER_SWING_HIT_START && progress <= PLAYER_SWING_HIT_END)
  {
    sr = 255;
    sg = 220;
    sb = 160;
  }

  fp_draw_capsule(fb, cam, x0, y0, hand_z, x1, y1, hand_z, pill_r, sr, sg, sb);
  // Larger rounded tip at the striking end.
  fp_draw_sphere(fb, cam, NULL, x1, y1, hand_z, pill_r * 1.25f, sr, sg, sb);
}

static void fp_draw_swing(const FPFrameBuffer *fb, const FPCamera *cam)
{
  if (!fb || !fb->color || !cam || s_fp_swing_progress <= 0.0f)
    return;

  if (s_fp_swing_armed)
    fp_draw_swing_sector(fb, cam);
  else
    fp_draw_swing_pill(fb, cam);
}

static void fp_draw_sphere(const FPFrameBuffer *fb, const FPCamera *cam, const World *occlusion_world,
                           float wx, float wy, float wz, float radius, uint8_t sr, uint8_t sg,
                           uint8_t sb)
{
  float sx = 0.0f, sy = 0.0f, depth = 0.0f;
  if (!fp_renderer_debug_project(cam, fb->w, fb->h, wx, wy, wz, &sx, &sy, &depth))
    return;

  if (!fb->depth)
  {
    if (!occlusion_world)
      return;
    const float dx = wx - cam->x, dy = wy - cam->y, dz = wz - cam->z;
    const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist > 0.01f)
    {
      int hx = -1, hy = -1, hz = -1;
      const int steps = (int)dist + 2;
      if (world_raycast_first_hit(occlusion_world, cam->x, cam->y, cam->z, dx / dist, dy / dist,
                                  dz / dist, steps, &hx, &hy, &hz))
      {
        const float hdx = (float)hx + 0.5f - cam->x;
        const float hdy = (float)hy + 0.5f - cam->y;
        const float hdz = (float)hz + 0.5f - cam->z;
        if (sqrtf(hdx * hdx + hdy * hdy + hdz * hdz) < dist - 0.4f)
          return;
      }
    }
  }

  float pix = (radius * 2.5f) * ((float)fb->h * 0.5f) / (depth > 0.01f ? depth : 0.01f);
  if (pix < 2.0f)
    pix = 2.0f;
  if (pix > (float)fb->h * 0.35f)
    pix = (float)fb->h * 0.35f;

  const int x0 = (int)floorf(sx - pix), x1 = (int)ceilf(sx + pix);
  const int y0 = (int)floorf(sy - pix), y1 = (int)ceilf(sy + pix);
  const float r2 = pix * pix;

  for (int y = (y0 < 0 ? 0 : y0); y <= y1 && y < fb->h; y++)
  {
    for (int x = (x0 < 0 ? 0 : x0); x <= x1 && x < fb->w; x++)
    {
      const float pdx = (float)x + 0.5f - sx;
      const float pdy = (float)y + 0.5f - sy;
      const float d2 = pdx * pdx + pdy * pdy;
      if (d2 > r2)
        continue;
      const int idx = y * fb->stride + x;
      if (fb->depth && fb->depth[idx] < depth)
        continue;
      const float t = 1.0f - sqrtf(d2 / r2);
      const float a = 0.35f + 0.65f * t * t;
      const Uint32 dst = fb->color[idx];
      const uint8_t dr = (uint8_t)((dst >> 24) & 0xFFu);
      const uint8_t dg = (uint8_t)((dst >> 16) & 0xFFu);
      const uint8_t db = (uint8_t)((dst >> 8) & 0xFFu);
      const uint8_t orr = (uint8_t)(dr + (int)((sr - dr) * a));
      const uint8_t og = (uint8_t)(dg + (int)((sg - dg) * a));
      const uint8_t ob = (uint8_t)(db + (int)((sb - db) * a));
      fb->color[idx] = ((Uint32)orr << 24) | ((Uint32)og << 16) | ((Uint32)ob << 8) | 0xFFu;
    }
  }
}

static const VoxelMesh *fp_mob_mesh(const MobModel *model)
{
  if (!model || !model->world || model->kind < 0 || model->kind >= MOB_MODEL_COUNT)
    return NULL;
  const int k = (int)model->kind;
  if (!s_mob_mesh_valid[k])
  {
    voxel_mesh_free(&s_mob_mesh[k]);
    voxel_mesh_init(&s_mob_mesh[k]);
    voxel_mesh_build_all_faces_greedy(model->world, &s_mob_mesh[k]);
    s_mob_mesh_valid[k] = true;
  }
  return s_mob_mesh[k].count > 0 ? &s_mob_mesh[k] : NULL;
}

// Greedy mesh of a sparse material template world (bush sphere, roof shell, stair wedge, …).
// Built with skip_sparse=false so nested leaf/bush cells still emit faces. Distance LOD keeps
// stride-2 / stride-4 copies so far shrubs cost a few hundred quads instead of ~8k.
static const VoxelMesh *fp_template_mesh_lod(MaterialTemplateKind kind, int lod)
{
  if (kind < 0 || kind >= MATERIAL_TEMPLATE_COUNT || !material_template_kind_is_sparse(kind))
    return NULL;
  if (lod < 0)
    lod = 0;
  if (lod >= FP_TEMPLATE_LOD_COUNT)
    lod = FP_TEMPLATE_LOD_COUNT - 1;
  if (!s_template_mesh_valid[kind])
  {
    const MaterialTemplate *t = material_worlds_get(kind);
    if (!t || !t->world)
      return NULL;
    for (int i = 0; i < FP_TEMPLATE_LOD_COUNT; i++)
      voxel_mesh_free(&s_template_mesh[kind][i]);
    voxel_mesh_init(&s_template_mesh[kind][0]);
    voxel_mesh_build_all_faces_greedy_ex(t->world, &s_template_mesh[kind][0], false);
    fp_mesh_lod_from(&s_template_mesh[kind][0], &s_template_mesh[kind][1], 2);
    fp_mesh_lod_from(&s_template_mesh[kind][0], &s_template_mesh[kind][2], 4);
    s_template_mesh_valid[kind] = true;
  }
  return s_template_mesh[kind][lod].count > 0 ? &s_template_mesh[kind][lod] : NULL;
}

// Build (or fetch) a nested mesh for a shape×orient modifier on an arbitrary material.
static const VoxelMesh *fp_shaped_mesh_lod(VoxelType type, uint8_t shape_orient, int lod)
{
  if (lod < 0)
    lod = 0;
  if (lod >= FP_TEMPLATE_LOD_COUNT)
    lod = FP_TEMPLATE_LOD_COUNT - 1;
  const VoxelShape shape = voxel_unpack_shape(shape_orient);
  if (shape == VOXEL_SHAPE_FULL)
    return NULL;
  const uint8_t orient = voxel_unpack_orient(shape_orient);
  const Uint64 now = SDL_GetPerformanceCounter();

  int free_slot = -1;
  int lru_slot = 0;
  Uint64 lru_t = ~(Uint64)0;
  for (int i = 0; i < FP_SHAPE_MESH_CACHE_CAP; i++)
  {
    FPShapeMeshEntry *e = &s_shape_mesh_cache[i];
    if (e->valid && e->type == (uint16_t)type && e->shape_orient == shape_orient &&
        e->lod == (uint8_t)lod)
    {
      e->last_used = now;
      return e->mesh.count > 0 ? &e->mesh : NULL;
    }
    if (!e->valid && free_slot < 0)
      free_slot = i;
    if (!e->valid)
      continue;
    if (e->last_used < lru_t)
    {
      lru_t = e->last_used;
      lru_slot = i;
    }
  }

  const int slot = (free_slot >= 0) ? free_slot : lru_slot;
  FPShapeMeshEntry *dest = &s_shape_mesh_cache[slot];
  if (dest->valid)
    voxel_mesh_free(&dest->mesh);

  World *w = world_create(MATERIAL_WORLD_SIZE, MATERIAL_WORLD_SIZE, MATERIAL_WORLD_SIZE);
  if (!w)
    return NULL;

  const MaterialTemplate *tmpl = material_worlds_for_voxel(type);
  if (tmpl && tmpl->world && (int)tmpl->world->width == MATERIAL_WORLD_SIZE &&
      (int)tmpl->world->height == MATERIAL_WORLD_SIZE &&
      (int)tmpl->world->depth == MATERIAL_WORLD_SIZE)
  {
    const size_t n =
        (size_t)MATERIAL_WORLD_SIZE * MATERIAL_WORLD_SIZE * MATERIAL_WORLD_SIZE;
    for (size_t i = 0; i < n; i++)
      w->voxels[i].type = tmpl->world->voxels[i].type;
    voxel_shape_carve_world(w, shape, orient);
  }
  else
  {
    voxel_shape_fill_world(w, shape, orient, type);
  }

  VoxelMesh full;
  voxel_mesh_init(&full);
  voxel_mesh_build_all_faces_greedy_ex(w, &full, false);
  world_destroy(w);

  voxel_mesh_init(&dest->mesh);
  if (lod == 0)
    voxel_mesh_copy(&dest->mesh, &full);
  else
    fp_mesh_lod_from(&full, &dest->mesh, lod == 1 ? 2 : 4);
  voxel_mesh_free(&full);

  dest->type = (uint16_t)type;
  dest->shape_orient = shape_orient;
  dest->lod = (uint8_t)lod;
  dest->valid = true;
  dest->last_used = now;
  return dest->mesh.count > 0 ? &dest->mesh : NULL;
}

static bool fp_sparse_type_is_foliage_like(VoxelType type)
{
  return world_voxel_type_is_foliage(type) || voxel_type_is_bush(type) || type == VOXEL_FUNGUS ||
         type == VOXEL_FEATHER;
}

static int fp_sparse_lod_for_dist(bool foliage, float dist)
{
  if (foliage)
  {
    if (dist <= FP_SPARSE_FOLIAGE_FULL)
      return 0;
    if (dist <= FP_SPARSE_FOLIAGE_LOD2)
      return 1;
    if (dist <= FP_SPARSE_FOLIAGE_DIST)
      return 2;
    return -1;
  }
  if (dist <= FP_SPARSE_FITTING_FULL)
    return 0;
  if (dist <= FP_SPARSE_FITTING_LOD2)
    return 1;
  if (dist <= FP_SPARSE_FITTING_DIST)
    return 2;
  return -1;
}

// Draw true nested silhouettes for sparse parent voxels listed at remesh time (bushes as round
// clumps, roofs as shells, stairs as wedges) instead of punched AABB cubes.
static void fp_draw_sparse_material_instances(const FPFrameBuffer *fb, const FPCamera *cam,
                                              const FPCameraBasis *basis, const World *world,
                                              const FPSparseCellList *list, float world_ox,
                                              float world_oy, float world_oz)
{
  if (!fb || !cam || !basis || !world || !list || list->count <= 0 || !fp_subvoxel_enabled())
    return;
  if (!material_worlds_init())
    return;

  const double t_scan0 = fp_now_ms();

  const float cam_lx = cam->x - world_ox;
  const float cam_ly = cam->y - world_oy;
  const float cam_lz = cam->z - world_oz;
  const float scale = 1.0f / (float)MATERIAL_WORLD_SIZE;

  int candidates = list->count;
  int drawn = 0;
  int quads = 0;
  int foliage_drawn = 0;
  int fitting_drawn = 0;
  double draw_ms = 0.0;

  // Pass 0: always draw the near band. Pass 1: mid/far with per-class caps + quad budget.
  for (int pass = 0; pass < 2; pass++)
  {
    for (int i = 0; i < list->count; i++)
    {
      if (quads >= FP_SPARSE_QUAD_BUDGET)
        break;

      const FPSparseCell *cell = &list->cells[i];
      const VoxelType type = (VoxelType)cell->type;
      const bool foliage = fp_sparse_type_is_foliage_like(type);

      const float dx = (float)cell->x + 0.5f - cam_lx;
      const float dy = (float)cell->y + 0.5f - cam_ly;
      const float dz = (float)cell->z + 0.5f - cam_lz;
      const float dist_sq = dx * dx + dy * dy + dz * dz;
      const float dist = sqrtf(dist_sq);
      const bool near = dist <= FP_SPARSE_NEAR_FORCE;
      if (pass == 0 && !near)
        continue;
      if (pass == 1 && near)
        continue;

      const int lod0 = fp_sparse_lod_for_dist(foliage, dist);
      if (lod0 < 0)
        continue;
      int lod = lod0;

      if (pass == 0 && foliage)
      {
        // Near band is uncapped for correctness at the player's feet, but a dense grove still
        // drops to coarser LOD / stops after a soft cap so one step into shrubs cannot blow the
        // frame.
        if (foliage_drawn >= FP_SPARSE_NEAR_FOLIAGE_CAP)
          continue;
        if (foliage_drawn >= FP_SPARSE_NEAR_FOLIAGE_CAP / 2 && lod < 1)
          lod = 1;
      }

      if (pass == 1)
      {
        if (foliage && foliage_drawn >= FP_SPARSE_FOLIAGE_CAP)
          continue;
        if (!foliage && fitting_drawn >= FP_SPARSE_FITTING_CAP)
          continue;
      }

      const MaterialTemplate *tmpl = material_worlds_for_voxel(type);
      const VoxelMesh *mesh = NULL;
      if (voxel_unpack_shape(cell->shape_orient) != VOXEL_SHAPE_FULL)
        mesh = fp_shaped_mesh_lod(type, cell->shape_orient, lod);
      else if (tmpl)
        mesh = fp_template_mesh_lod(tmpl->kind, lod);
      if (!mesh)
        continue;

      uint8_t tr, tg, tb;
      world_voxel_type_color(type, &tr, &tg, &tb);
      const float yaw = voxel_yaw_radians_from_u8(cell->yaw_u8);
      // Nested mesh is authored in local subvoxel space; yaw re-projects that high-res
      // silhouette into world XY so bushes/trees/shaped props get continuous variance.
      const double t_d0 = fp_now_ms();
      fp_raster_mesh(fb, cam, basis, NULL, mesh, NULL, world_ox + (float)cell->x,
                     world_oy + (float)cell->y, world_oz + (float)cell->z, scale, yaw, 0.5f, 0.5f,
                     false, tr, tg, tb);
      draw_ms += fp_now_ms() - t_d0;
      drawn++;
      quads += mesh->count;
      if (foliage)
        foliage_drawn++;
      else
        fitting_drawn++;
    }
  }

  s_material_frame_stats.sparse_scan_ms += fp_now_ms() - t_scan0 - draw_ms;
  s_material_frame_stats.sparse_draw_ms += draw_ms;
  s_material_frame_stats.sparse_candidates += candidates;
  s_material_frame_stats.sparse_drawn += drawn;
  s_material_frame_stats.sparse_quads += quads;
}

static bool fp_draw_actor_poly(const FPFrameBuffer *fb, const FPCamera *cam,
                               const FPCameraBasis *basis, const Actor *a)
{
  if (!fb || !fb->depth || !cam || !a)
    return false;
  const char *mesh_name = mob_actor_mesh_name(a);
  if (!mesh_name)
    return false;

  const float adx = (float)a->x - cam->x;
  const float ady = (float)a->y - cam->y;
  const float adz = (float)a->z - cam->z;
  const float actor_dist = sqrtf(adx * adx + ady * ady + adz * adz);
  const int lod = poly_mesh_lod_stride(actor_dist);
  if (lod <= 0)
    return false; // sphere / voxel impostor

  const PolyMesh *mesh = poly_mesh_get(mesh_name);
  if (!mesh || mesh->vertex_count == 0 || mesh->vertex_count > 8192)
  {
    // Lazy load once; subsequent draws hit the cache without re-scanning the directory.
    poly_mesh_init(NULL);
    mesh = poly_mesh_get(mesh_name);
    if (!mesh || mesh->vertex_count == 0 || mesh->vertex_count > 8192)
      return false;
  }

  static float xyz[8192 * 3];
  // Mid/far LOD: nearest keyframe is enough; lerp is invisible at that screen size.
  if (!poly_mesh_sample_ex(mesh, mob_actor_anim_clip(a), mob_actor_anim_time(a), xyz, lod > 1))
    return false;

  FPCameraBasis local;
  const FPCameraBasis *b = basis;
  if (!b)
  {
    fp_camera_build_basis(cam, &local);
    local.aspect = (fb->h > 0) ? ((float)fb->w / (float)fb->h) : 1.0f;
    b = &local;
  }

  const float yaw = mob_actor_facing_yaw(a);
  const float roll = mob_actor_facing_roll(a);
  const float c = cosf(yaw);
  const float s = sinf(yaw);
  const float cr = cosf(roll);
  const float sr = sinf(roll);
  const float ox = (float)a->x;
  const float oy = (float)a->y;
  const float oz = (float)a->z;
  const float view_dist_sq = FP_VIEW_DISTANCE * FP_VIEW_DISTANCE;
  const uint32_t tri_step = 3u * (uint32_t)lod;

  for (uint32_t t = 0; t + 2 < mesh->index_count; t += tri_step)
  {
    const uint32_t i0 = mesh->indices[t];
    const uint32_t i1 = mesh->indices[t + 1];
    const uint32_t i2 = mesh->indices[t + 2];
    if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count)
      continue;

    float w[3][3];
    const uint32_t idx[3] = {i0, i1, i2};
    for (int k = 0; k < 3; k++)
    {
      const float lx = xyz[idx[k] * 3];
      const float ly0 = xyz[idx[k] * 3 + 1];
      const float lz0 = xyz[idx[k] * 3 + 2];
      // Roll around local forward (+X): positive = right wing down.
      const float ly = ly0 * cr + lz0 * sr;
      const float lz = -ly0 * sr + lz0 * cr;
      w[k][0] = ox + lx * c - ly * s;
      w[k][1] = oy + lx * s + ly * c;
      w[k][2] = oz + lz;
    }

    const float cx = (w[0][0] + w[1][0] + w[2][0]) * (1.0f / 3.0f);
    const float cy = (w[0][1] + w[1][1] + w[2][1]) * (1.0f / 3.0f);
    const float cz = (w[0][2] + w[1][2] + w[2][2]) * (1.0f / 3.0f);
    const float to_cam_x = cx - cam->x;
    const float to_cam_y = cy - cam->y;
    const float to_cam_z = cz - cam->z;
    const float dist_sq = to_cam_x * to_cam_x + to_cam_y * to_cam_y + to_cam_z * to_cam_z;
    if (dist_sq > view_dist_sq)
      continue;

    const float e1x = w[1][0] - w[0][0], e1y = w[1][1] - w[0][1], e1z = w[1][2] - w[0][2];
    const float e2x = w[2][0] - w[0][0], e2y = w[2][1] - w[0][1], e2z = w[2][2] - w[0][2];
    float nx = e1y * e2z - e1z * e2y;
    float ny = e1z * e2x - e1x * e2z;
    float nz = e1x * e2y - e1y * e2x;
    if (nx * to_cam_x + ny * to_cam_y + nz * to_cam_z > 0.0f)
      continue;
    const float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
    float shade = 0.55f;
    if (nlen > 1e-6f)
    {
      nx /= nlen;
      ny /= nlen;
      nz /= nlen;
      shade = 0.42f + 0.58f * fmaxf(0.0f, nx * 0.25f + ny * 0.15f + nz * 0.96f);
    }

    FPCameraPoint cam_pts[3];
    for (int k = 0; k < 3; k++)
      fp_to_camera_space(cam, b, w[k][0], w[k][1], w[k][2], &cam_pts[k]);

    FPCameraPoint clipped[8];
    const int clipped_count = fp_clip_near(cam_pts, 3, clipped);
    if (clipped_count < 3)
      continue;

    const uint8_t *col = &mesh->colors[i0 * 3];
    uint8_t r = (uint8_t)fminf(255.0f, (float)col[0] * shade);
    uint8_t g = (uint8_t)fminf(255.0f, (float)col[1] * shade);
    uint8_t bcol = (uint8_t)fminf(255.0f, (float)col[2] * shade);
    fp_mix_fog(fp_fog_factor(sqrtf(dist_sq)), &r, &g, &bcol);

    FPSurface surf;
    surf.flat = fp_rgba8888(r, g, bcol);
    surf.type = VOXEL_AIR;
    surf.face = 0;
    surf.shade = shade;
    surf.fog = 0.0f;
    surf.damage = 0;
    surf.crack_seed = 0;
    surf.lit = false;
    surf.nx = 0.0f;
    surf.ny = 0.0f;
    surf.nz = 1.0f;

    FPScreenVertex verts[8];
    for (int ci = 0; ci < clipped_count; ci++)
    {
      fp_project_camera_point(b, &clipped[ci], fb->w, fb->h, &verts[ci].x, &verts[ci].y,
                              &verts[ci].z);
      const float iw = (fabsf(verts[ci].z) > 1e-9f) ? (1.0f / verts[ci].z) : 0.0f;
      verts[ci].iw = iw;
      verts[ci].u_over_z = 0.0f;
      verts[ci].v_over_z = 0.0f;
      verts[ci].wx_over_z = 0.0f;
      verts[ci].wy_over_z = 0.0f;
      verts[ci].wz_over_z = 0.0f;
    }
    for (int ci = 2; ci < clipped_count; ci++)
      fp_fill_triangle(fb->depth, fb->color, fb->stride, fb->w, fb->h, &verts[0],
                       &verts[ci - 1], &verts[ci], &surf);
  }
  return true;
}

static bool fp_draw_actor_model(const FPFrameBuffer *fb, const FPCamera *cam,
                                const FPCameraBasis *basis, const Actor *a)
{
  if (!fb || !fb->depth || !cam || !basis || !a)
    return false;
  if (!mob_models_enabled())
    return false;
  mob_models_init(NULL);
  const MobModel *model = mob_models_for_actor(a);
  if (!model)
    return false;
  const VoxelMesh *mesh = fp_mob_mesh(model);
  if (!mesh)
    return false;

  const float scale = 1.0f / (float)MOB_MODEL_SUBVOXELS;
  const float half_x = (float)model->section_x * scale * 0.5f;
  const float half_y = (float)model->section_y * scale * 0.5f;
  const float ox = (float)a->x - half_x;
  const float oy = (float)a->y - half_y;
  const float oz = (float)a->z;
  float yaw = 0.0f;
  const float vx = (float)a->velocity_x;
  const float vy = (float)a->velocity_y;
  if (vx * vx + vy * vy > 0.0025f)
    yaw = atan2f(vy, vx);

  uint8_t tr = 255, tg = 255, tb = 255;
  if (mob_actor_is_bird(a))
    mob_actor_bird_color(a, &tr, &tg, &tb);

  fp_raster_mesh(fb, cam, basis, NULL, mesh, NULL, ox, oy, oz, scale, yaw, half_x, half_y, false, tr,
                 tg, tb);
  return true;
}

static void fp_draw_actors(const FPFrameBuffer *fb, const FPCamera *cam, const FPCameraBasis *basis,
                           const World *world, bool mesh_models)
{
  if (!fb || !fb->color || !world || !world->runtime_actors)
    return;

  for (int i = 0; i < world->runtime_actor_count; i++)
  {
    const Actor *a = &world->runtime_actors[i];
    if (!a->is_active)
      continue;
    if (a->is_controlled)
      continue;

    const float dx = (float)a->x - cam->x;
    const float dy = (float)a->y - cam->y;
    const float dz = (float)a->z - cam->z;
    const float dist_sq = dx * dx + dy * dy + dz * dz;
    if (dist_sq < 0.6f * 0.6f)
      continue;
    if (dist_sq > FP_VIEW_DISTANCE * FP_VIEW_DISTANCE)
      continue;

    if (fp_draw_actor_poly(fb, cam, basis, a))
      continue;

    if (mesh_models && fp_draw_actor_model(fb, cam, basis, a))
      continue;

    uint8_t br = 132, bg = 86, bb = 48;
    if (mob_actor_is_bird(a))
    {
      mob_actor_bird_color(a, &br, &bg, &bb);
      const BirdKind kind = mob_actor_bird_kind(a);
      const float body_r = kind == BIRD_KIND_SPARROW ? 0.18f :
                           kind == BIRD_KIND_GULL ? 0.28f : 0.22f;
      const float wing = kind == BIRD_KIND_GULL ? 0.42f :
                         kind == BIRD_KIND_SPARROW ? 0.26f : 0.34f;
      float fx = (float)a->velocity_x;
      float fy = (float)a->velocity_y;
      float flen = sqrtf(fx * fx + fy * fy);
      if (flen < 0.05f)
      {
        fx = 1.0f;
        fy = 0.0f;
      }
      else
      {
        fx /= flen;
        fy /= flen;
      }
      const float wx = -fy * wing;
      const float wy = fx * wing;
      const float z = (float)a->z + (a->is_flying ? 0.12f : 0.08f);
      fp_draw_sphere(fb, cam, world, (float)a->x + wx, (float)a->y + wy, z, wing * 0.45f,
                     (uint8_t)(br * 8 / 10), (uint8_t)(bg * 8 / 10), (uint8_t)(bb * 8 / 10));
      fp_draw_sphere(fb, cam, world, (float)a->x - wx, (float)a->y - wy, z, wing * 0.45f,
                     (uint8_t)(br * 8 / 10), (uint8_t)(bg * 8 / 10), (uint8_t)(bb * 8 / 10));
      fp_draw_sphere(fb, cam, world, (float)a->x, (float)a->y, z, body_r, br, bg, bb);
      fp_draw_sphere(fb, cam, world, (float)a->x + fx * body_r, (float)a->y + fy * body_r,
                     z + body_r * 0.6f, body_r * 0.55f,
                     (uint8_t)(br + 20 > 255 ? 255 : br + 20),
                     (uint8_t)(bg + 16 > 255 ? 255 : bg + 16),
                     (uint8_t)(bb + 8 > 255 ? 255 : bb + 8));
    }
    else
    {
      fp_draw_sphere(fb, cam, world, (float)a->x, (float)a->y, (float)a->z + 0.15f, 0.45f, br, bg, bb);
      fp_draw_sphere(fb, cam, world, (float)a->x, (float)a->y, (float)a->z + 0.85f, 0.28f,
                     (uint8_t)(br + 20), (uint8_t)(bg + 16), (uint8_t)(bb + 8));
    }
  }
}

// Screen-space hurt health bar (matches isometric: full opacity, then fade in the last second).
// Drawn as overlay chrome — no depth test — so the actor's own mesh cannot hide it.
static void fp_blit_health_bar(const FPFrameBuffer *fb, int cx, int top_y, int bar_w, int bar_h,
                               uint32_t health, uint32_t max_hp, float alpha)
{
  if (!fb || !fb->color || alpha <= 0.02f || bar_w < 4 || bar_h < 1)
    return;
  if (alpha > 1.0f)
    alpha = 1.0f;
  if (max_hp == 0)
    max_hp = 100u;
  float t = (float)health / (float)max_hp;
  if (t > 1.0f)
    t = 1.0f;
  if (t < 0.0f)
    t = 0.0f;

  const int fill = (int)(t * (float)bar_w + 0.5f);
  const int bx = cx - bar_w / 2;
  const int by = top_y - bar_h - 2;

  const float a_back = 0.82f * alpha;
  const float a_fill = 0.95f * alpha;

  for (int y = by - 1; y <= by + bar_h; y++)
  {
    if (y < 0 || y >= fb->h)
      continue;
    for (int x = bx - 1; x <= bx + bar_w; x++)
    {
      if (x < 0 || x >= fb->w)
        continue;
      const bool border = (y == by - 1 || y == by + bar_h || x == bx - 1 || x == bx + bar_w);
      const bool in_track = !border && y >= by && y < by + bar_h && x >= bx && x < bx + bar_w;
      const bool in_fill = in_track && fill > 0 && x < bx + fill;
      if (!border && !in_track)
        continue;

      uint8_t sr = 18, sg = 14, sb = 10;
      float a = a_back;
      if (in_fill)
      {
        sr = t > 0.5f ? 70 : 220;
        sg = t > 0.25f ? 200 : 55;
        sb = 48;
        a = a_fill;
      }

      const int idx = y * fb->stride + x;
      const Uint32 dst = fb->color[idx];
      const uint8_t dr = (uint8_t)((dst >> 24) & 0xFFu);
      const uint8_t dg = (uint8_t)((dst >> 16) & 0xFFu);
      const uint8_t db = (uint8_t)((dst >> 8) & 0xFFu);
      const uint8_t orr = (uint8_t)(dr + (int)(((int)sr - (int)dr) * a));
      const uint8_t og = (uint8_t)(dg + (int)(((int)sg - (int)dg) * a));
      const uint8_t ob = (uint8_t)(db + (int)(((int)sb - (int)db) * a));
      fb->color[idx] = ((Uint32)orr << 24) | ((Uint32)og << 16) | ((Uint32)ob << 8) | 0xFFu;
    }
  }
}

static void fp_draw_actor_health_bars(const FPFrameBuffer *fb, const FPCamera *cam,
                                      const World *world)
{
  if (!fb || !fb->color || !cam || !world || !world->runtime_actors)
    return;

  for (int i = 0; i < world->runtime_actor_count; i++)
  {
    const Actor *a = &world->runtime_actors[i];
    if (!a->is_active || a->is_controlled)
      continue;
    if (a->hurt_display_ttl <= 0.0f)
      continue;

    const float dx = (float)a->x - cam->x;
    const float dy = (float)a->y - cam->y;
    const float dz = (float)a->z - cam->z;
    const float dist_sq = dx * dx + dy * dy + dz * dz;
    // Keep chrome visible in melee; only skip when practically inside the camera.
    if (dist_sq < 0.12f * 0.12f)
      continue;
    const float range = ACTOR_HURT_DISPLAY_RANGE;
    if (dist_sq > range * range)
      continue;

    // Anchor above the silhouette. Birds are small; poly/voxel walkers are ~1–2 units tall.
    const float head_lift = mob_actor_is_bird(a) ? 0.55f : 1.65f;
    const float head_z = (float)a->z + head_lift;
    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    if (!fp_renderer_debug_project(cam, fb->w, fb->h, (float)a->x, (float)a->y, head_z, &sx, &sy,
                                   &depth))
      continue;
    // Allow bars that sit just above/beside the frame (close-range tall mobs).
    if (sx < -40.0f || sx > (float)fb->w + 40.0f || sy < -40.0f || sy > (float)fb->h + 40.0f)
      continue;

    // Perspective-sized bar so downscaled FP targets still read after upscale.
    float bar_w_f = 22.0f * ((float)fb->h * 0.55f) / (depth > 0.35f ? depth : 0.35f);
    if (bar_w_f < 14.0f)
      bar_w_f = 14.0f;
    if (bar_w_f > 48.0f)
      bar_w_f = 48.0f;
    const int bar_w = (int)(bar_w_f + 0.5f);
    const int bar_h = bar_w >= 28 ? 4 : 3;

    float alpha = 1.0f;
    if (a->hurt_display_ttl < ACTOR_HURT_FADE_SECONDS)
      alpha = a->hurt_display_ttl / ACTOR_HURT_FADE_SECONDS;
    {
      const float dist = sqrtf(dist_sq);
      const float fade_start = ACTOR_HURT_DISPLAY_FADE_START;
      if (dist > fade_start && range > fade_start)
      {
        float d_alpha = 1.0f - (dist - fade_start) / (range - fade_start);
        if (d_alpha < 0.0f)
          d_alpha = 0.0f;
        alpha *= d_alpha;
      }
    }
    if (alpha <= 0.02f)
      continue;
    fp_blit_health_bar(fb, (int)(sx + 0.5f), (int)(sy + 0.5f), bar_w, bar_h, a->health,
                       actor_max_health(a), alpha);
  }
}

// Cheap FXAA-style edge soften (Devlog #4): blend pixels that differ strongly from a neighbour.
// Only softens geometry–geometry edges. Softening against sky would let a later neighbour fill
// bleed into centre-world pixels and break the sky-only neighbour contract the cluster test
// asserts.
static void fp_apply_fxaa(Uint32 *color, int stride, int w, int h)
{
  if (!color || w < 3 || h < 3)
    return;
  const float *depth = s_depth_buffer;
  // In-place horizontal then vertical 1-pixel soften on high-contrast edges only.
  for (int y = 1; y < h - 1; y++)
  {
    Uint32 *row = color + y * stride;
    for (int x = 1; x < w - 1; x++)
    {
      if (depth)
      {
        const int di = y * stride + x;
        const float dc = depth[di], dl = depth[di - 1], dr = depth[di + 1];
        // Skip sky and depth discontinuities (centre silhouette vs neighbour fill).
        if (dc >= FP_DEPTH_FAR || dl >= FP_DEPTH_FAR || dr >= FP_DEPTH_FAR)
          continue;
        if (fabsf(dc - dl) > 2.0f || fabsf(dc - dr) > 2.0f)
          continue;
      }
      const Uint32 c = row[x];
      const Uint32 l = row[x - 1];
      const Uint32 r = row[x + 1];
      const int cr = (int)((c >> 24) & 0xFFu), cg = (int)((c >> 16) & 0xFFu),
                cb = (int)((c >> 8) & 0xFFu);
      const int lr = (int)((l >> 24) & 0xFFu), lg = (int)((l >> 16) & 0xFFu),
                lb = (int)((l >> 8) & 0xFFu);
      const int rr = (int)((r >> 24) & 0xFFu), rg = (int)((r >> 16) & 0xFFu),
                rb = (int)((r >> 8) & 0xFFu);
      const int dL = abs(cr - lr) + abs(cg - lg) + abs(cb - lb);
      const int dR = abs(cr - rr) + abs(cg - rg) + abs(cb - rb);
      if (dL < 40 && dR < 40)
        continue;
      const int nr = (cr * 2 + lr + rr) / 4;
      const int ng = (cg * 2 + lg + rg) / 4;
      const int nb = (cb * 2 + lb + rb) / 4;
      row[x] = fp_rgba8888((uint8_t)nr, (uint8_t)ng, (uint8_t)nb);
    }
  }
  for (int y = 1; y < h - 1; y++)
  {
    Uint32 *row = color + y * stride;
    Uint32 *row_u = color + (y - 1) * stride;
    Uint32 *row_d = color + (y + 1) * stride;
    for (int x = 1; x < w - 1; x++)
    {
      if (depth)
      {
        const int di = y * stride + x;
        const float dc = depth[di], du = depth[di - stride], dd = depth[di + stride];
        if (dc >= FP_DEPTH_FAR || du >= FP_DEPTH_FAR || dd >= FP_DEPTH_FAR)
          continue;
        if (fabsf(dc - du) > 2.0f || fabsf(dc - dd) > 2.0f)
          continue;
      }
      const Uint32 c = row[x];
      const Uint32 u = row_u[x];
      const Uint32 d = row_d[x];
      const int cr = (int)((c >> 24) & 0xFFu), cg = (int)((c >> 16) & 0xFFu),
                cb = (int)((c >> 8) & 0xFFu);
      const int ur = (int)((u >> 24) & 0xFFu), ug = (int)((u >> 16) & 0xFFu),
                ub = (int)((u >> 8) & 0xFFu);
      const int dr = (int)((d >> 24) & 0xFFu), dg = (int)((d >> 16) & 0xFFu),
                db = (int)((d >> 8) & 0xFFu);
      const int dU = abs(cr - ur) + abs(cg - ug) + abs(cb - ub);
      const int dD = abs(cr - dr) + abs(cg - dg) + abs(cb - db);
      if (dU < 40 && dD < 40)
        continue;
      const int nr = (cr * 2 + ur + dr) / 4;
      const int ng = (cg * 2 + ug + dg) / 4;
      const int nb = (cb * 2 + ub + db) / 4;
      row[x] = fp_rgba8888((uint8_t)nr, (uint8_t)ng, (uint8_t)nb);
    }
  }
}

// Soft vegetation impostors (Devlog #22): tip blades / leaf flecks near the camera.
// Covers low grass, tall grass, bushes, and canopy leaves — not just a few generic greens.
static void fp_plot_veg_pixel(FPFrameBuffer *fb, int px, int py, float depth, uint8_t r, uint8_t g,
                              uint8_t b)
{
  if (!fb || !fb->color || !fb->depth || px < 0 || py < 0 || px >= fb->w || py >= fb->h)
    return;
  const int idx = py * fb->stride + px;
  if (fb->depth[idx] + 0.18f < depth)
    return;
  fb->color[idx] = fp_rgba8888(r, g, b);
  fb->depth[idx] = depth;
}

static void fp_draw_grass_impostors(const FPFrameBuffer *fb, const FPCamera *cam, const World *world)
{
  if (!fb || !cam || !world || !fb->color || !fb->depth)
    return;

  const int cx0 = (int)floorf(cam->x);
  const int cy0 = (int)floorf(cam->y);
  const int rad = 12;
  static int s_still_foliage = -1;
  if (s_still_foliage < 0)
  {
    const char *e = getenv("VERSE_FP_STILL_FOLIAGE");
    s_still_foliage = (e && e[0] && e[0] != '0') ? 1 : 0;
  }
  const float wind_t = s_still_foliage ? 0.0f : ((float)SDL_GetTicks() * 0.0018f);

  for (int dy = -rad; dy <= rad; dy++)
    for (int dx = -rad; dx <= rad; dx++)
    {
      const int x = cx0 + dx;
      const int y = cy0 + dy;
      if (!world_pos_in_bounds_fast((World *)world, x, y, 0))
        continue;
      const int z_top = world_height_at_fast((World *)world, x, y);
      if (z_top < 0)
        continue;

      // Surface tip plus a short column above/below so canopy flecks and bush crowns still show
      // when they sit a voxel above the terrain heightmap.
      const int z0 = z_top > 2 ? z_top - 2 : 0;
      const int z1 = z_top + 1;
      for (int z = z0; z <= z1; z++)
      {
        if (!world_pos_in_bounds_fast((World *)world, x, y, z))
          continue;
        const Voxel *v = world_voxel_cptr_fast(world, x, y, z);
        if (!v || !voxel_type_is_vegetation(v->type))
          continue;

        const VoxelType t = v->type;
        const uint32_t h = voxel_crack_seed(x, y, z);
        const float phase = (float)(h & 1023u) * (6.2831853f / 1024.0f);
        const float sway = 0.12f * sinf(wind_t + phase);

        float tip_lift = 0.28f;
        int blades = 1;
        float depth_max = 26.0f;
        if (voxel_type_is_low_grass(t))
        {
          tip_lift = 0.22f + 0.10f * ((float)(h & 255u) / 255.0f);
          blades = 1 + (int)((h >> 8) & 1u);
          depth_max = 22.0f;
        }
        else if (t == VOXEL_GRASS_TALL)
        {
          tip_lift = 0.55f + 0.35f * ((float)(h & 255u) / 255.0f);
          blades = 2 + (int)((h >> 6) & 1u);
          depth_max = 30.0f;
        }
        else if (voxel_type_is_bush(t))
        {
          tip_lift = 0.40f + 0.25f * ((float)(h & 255u) / 255.0f);
          blades = 3 + (int)((h >> 4) & 1u);
          depth_max = 28.0f;
        }
        else // leaves
        {
          tip_lift = 0.18f + 0.20f * ((float)(h & 255u) / 255.0f);
          blades = 2 + (int)((h >> 5) & 1u);
          depth_max = 32.0f;
        }

        uint8_t br = 40, bg = 110, bb = 35;
        voxel_type_color(t, &br, &bg, &bb);

        for (int b = 0; b < blades; b++)
        {
          const uint32_t hb = h ^ (uint32_t)(b * 0x9E3779B9u);
          const float ox = 0.35f + 0.30f * ((float)(hb & 255u) / 255.0f) + sway;
          const float oy = 0.35f + 0.30f * ((float)((hb >> 8) & 255u) / 255.0f) -
                           0.08f * cosf(wind_t * 0.7f + phase);
          const float tip_z = (float)z + 1.0f + tip_lift * (0.7f + 0.3f * ((float)(b + 1) / (float)blades));

          float sx = 0.0f, sy = 0.0f, depth = 0.0f;
          if (!fp_renderer_debug_project(cam, fb->w, fb->h, (float)x + ox, (float)y + oy, tip_z,
                                         &sx, &sy, &depth))
            continue;
          if (depth < 0.45f || depth > depth_max)
            continue;

          const int px = (int)(sx + 0.5f);
          const int py = (int)(sy + 0.5f);
          if (px < 1 || py < 1 || px >= fb->w - 1 || py >= fb->h - 1)
            continue;

          const int shade = (int)((hb >> 16) & 31u) - 12;
          int rr = (int)br + shade;
          int gg = (int)bg + shade / 2;
          int rb = (int)bb + shade / 3;
          if (rr < 0)
            rr = 0;
          if (gg < 0)
            gg = 0;
          if (rb < 0)
            rb = 0;
          if (rr > 255)
            rr = 255;
          if (gg > 255)
            gg = 255;
          if (rb > 255)
            rb = 255;

          fp_plot_veg_pixel((FPFrameBuffer *)fb, px, py, depth, (uint8_t)rr, (uint8_t)gg,
                            (uint8_t)rb);

          // Stem / lower fleck for tall props and bushes so tips aren't floating dots.
          if (t == VOXEL_GRASS_TALL || voxel_type_is_bush(t))
          {
            const int stem_h = (t == VOXEL_GRASS_TALL) ? 2 : 1;
            for (int s = 1; s <= stem_h; s++)
            {
              fp_plot_veg_pixel((FPFrameBuffer *)fb, px, py + s, depth + 0.04f * (float)s,
                                (uint8_t)(rr * 3 / 4), (uint8_t)(gg * 3 / 4), (uint8_t)rb);
            }
          }
          else if (voxel_type_is_low_grass(t) || voxel_type_is_leaves(t))
          {
            fp_plot_veg_pixel((FPFrameBuffer *)fb, px, py + 1, depth + 0.05f,
                              (uint8_t)(rr * 3 / 4), (uint8_t)(gg * 3 / 4), (uint8_t)rb);
          }
        }
      }
    }
}

static void fp_frame_present(SDL_Renderer *ren, int panel_x, int panel_y, int panel_w, int panel_h)
{
  if (s_fp_locked_pixels && s_fp_tex_w > 0 && s_fp_tex_h > 0 && !s_fp_fxaa_already)
    fp_apply_fxaa(s_fp_locked_pixels, s_fp_locked_stride, s_fp_tex_w, s_fp_tex_h);

  SDL_UnlockTexture(s_fp_tex);
  s_fp_locked_pixels = NULL;
  SDL_Rect dst = {panel_x, panel_y, panel_w, panel_h};
  SDL_RenderCopy(ren, s_fp_tex, NULL, &dst);

  if (s_fp_frame_t0 != 0)
  {
    const Uint64 t1 = SDL_GetPerformanceCounter();
    s_last_frame_ms = (double)(t1 - s_fp_frame_t0) * 1000.0 /
                      (double)SDL_GetPerformanceFrequency();
    s_fp_frame_t0 = 0;
  }
}

// Rasterise a mesh's quads into the frame buffers.
//
// `ox,oy,oz,scale,yaw` place the mesh in world space: identity is origin 0, scale 1, yaw 0.
// Mob models pass scale 1/32 and an actor origin. `use_materials` is the world-geometry path;
// nested models already *are* the detail, so they stay flat-shaded and skip fog lookups that
// would treat model-local coordinates as world voxels.
static void fp_raster_mesh(const FPFrameBuffer *fb, const FPCamera *cam, const FPCameraBasis *basis,
                           const World *world, const VoxelMesh *mesh, const FogAtlas *fog,
                           float ox, float oy, float oz, float scale, float yaw, float pivot_x,
                           float pivot_y, bool use_materials, uint8_t tint_r, uint8_t tint_g,
                           uint8_t tint_b)
{
  if (!fb || !cam || !basis || !mesh || !mesh->quads)
    return;

  const float view_dist_sq = FP_VIEW_DISTANCE * FP_VIEW_DISTANCE;
  const bool textured = use_materials && fp_subvoxel_enabled();
  const bool identity = (scale == 1.0f && yaw == 0.0f && ox == 0.0f && oy == 0.0f && oz == 0.0f);
  const float c = identity ? 1.0f : cosf(yaw);
  const float s = identity ? 0.0f : sinf(yaw);

  // Walk face buckets when present (Devlog #7 layout). Do NOT directional-cull whole buckets
  // against the camera→origin vector: a 128³ world straddles every octant, so that cull drops
  // the facing sides of voxels on the far side of the camera and looks like missing/transparent
  // faces. Per-quad backface tests below still reject faces that point away from the eye.
  const int bucketed = (mesh->face_count[0] + mesh->face_count[1] + mesh->face_count[2] +
                        mesh->face_count[3] + mesh->face_count[4] + mesh->face_count[5]) > 0;

  for (int face = 0; face < 6; face++)
  {
    const int begin = bucketed ? mesh->face_start[face] : (face == 0 ? 0 : mesh->count);
    const int end = bucketed ? (mesh->face_start[face] + mesh->face_count[face])
                             : (face == 0 ? mesh->count : 0);

    for (int qi = begin; qi < end; qi++)
  {
    const VoxelFaceQuad *q = &mesh->quads[qi];

    const bool fog_hidden = use_materials && fog && world && q->x0 >= 0 && q->y0 >= 0 &&
                            q->z0 >= 0 &&
                            !fog_is_explored(fog, world, (uint32_t)q->x0, (uint32_t)q->y0,
                                             (uint32_t)q->z0);

    float corners[4][3];
    fp_quad_world_corners(q, corners);

    // Shear foliage around bodies before placing the quad in the world. Identity path is the
    // terrain mesh; nested mob models skip this (use_materials is false for them).
    if (use_materials && s_fp_foliage_bend && s_fp_foliage_bend->count > 0 &&
        foliage_bend_affects(q->type))
    {
      float nx, ny, nz;
      fp_face_normal(q->face, &nx, &ny, &nz);
      for (int k = 0; k < 4; k++)
      {
        // Nudge inside the face so floor() lands on the owning voxel even on shared edges.
        const float px = corners[k][0] - nx * 0.01f;
        const float py = corners[k][1] - ny * 0.01f;
        const float pz = corners[k][2] - nz * 0.01f;
        const int ox = (int)floorf(px);
        const int oy = (int)floorf(py);
        const int oz = (int)floorf(pz);
        foliage_bend_apply_corner(s_fp_foliage_bend, q->type, ox, oy, oz, &corners[k][0],
                                  &corners[k][1], &corners[k][2]);
      }
    }

    if (!identity)
    {
      for (int k = 0; k < 4; k++)
      {
        const float lx = corners[k][0] * scale - pivot_x;
        const float ly = corners[k][1] * scale - pivot_y;
        const float lz = corners[k][2] * scale;
        corners[k][0] = ox + pivot_x + lx * c - ly * s;
        corners[k][1] = oy + pivot_y + lx * s + ly * c;
        corners[k][2] = oz + lz;
      }
    }

    const float center_x = (corners[0][0] + corners[2][0]) * 0.5f;
    const float center_y = (corners[0][1] + corners[2][1]) * 0.5f;
    const float center_z = (corners[0][2] + corners[2][2]) * 0.5f;
    const float to_cam_x = center_x - cam->x;
    const float to_cam_y = center_y - cam->y;
    const float to_cam_z = center_z - cam->z;

    const float dist_sq = to_cam_x * to_cam_x + to_cam_y * to_cam_y + to_cam_z * to_cam_z;
    if (dist_sq > view_dist_sq)
      continue;

    float nx, ny, nz;
    fp_face_normal(q->face, &nx, &ny, &nz);
    {
      const float nwx = nx * c - ny * s;
      const float nwy = nx * s + ny * c;
      nx = nwx;
      ny = nwy;
    }
    if (nx * to_cam_x + ny * to_cam_y + nz * to_cam_z > 0.0f)
      continue;

    const MaterialFace mface = fp_material_face_for_mesh_face(q->face);

    FPCameraPoint cam_pts[4];
    for (int ci = 0; ci < 4; ci++)
    {
      fp_to_camera_space(cam, basis, corners[ci][0], corners[ci][1], corners[ci][2], &cam_pts[ci]);
      fp_face_uv(mface, corners[ci][0], corners[ci][1], corners[ci][2], &cam_pts[ci].u, &cam_pts[ci].v);
    }

    FPCameraPoint clipped[8];
    const int clipped_count = fp_clip_near(cam_pts, 4, clipped);
    if (clipped_count < 3)
      continue;

    const bool terrain = use_materials && (scale == 1.0f);
    const float shade = (s_fp_lighting && terrain) ? 1.0f : fp_face_shade(q->face);
    const float atmo_fog = use_materials ? fp_fog_factor(sqrtf(dist_sq)) : 0.0f;

    uint8_t r = (uint8_t)((float)q->color.r * shade * (float)tint_r / 255.0f);
    uint8_t g = (uint8_t)((float)q->color.g * shade * (float)tint_g / 255.0f);
    uint8_t b = (uint8_t)((float)q->color.b * shade * (float)tint_b / 255.0f);
    if (fog_hidden)
      fp_shade_unexplored(&r, &g, &b);
    else if (use_materials && !(s_fp_lighting && terrain))
      fp_mix_fog(atmo_fog, &r, &g, &b);

    FPSurface surf;
    surf.flat = fp_rgba8888(r, g, b);
    surf.type = (fog_hidden || !textured || !material_worlds_for_voxel(q->type)) ? VOXEL_AIR
                                                                                : q->type;
    surf.face = mface;
    surf.shade = shade;
    surf.fog = fog_hidden ? 1.0f : atmo_fog;
    surf.damage = q->damage;
    surf.crack_seed = voxel_crack_seed(q->x0, q->y0, q->z0);
    // Unexplored fog stays baked in albedo; only explored surfaces take occupancy lighting.
    // Closest-ring neighbours use non-zero ox/oy/oz but are still terrain (scale 1).
    surf.lit = s_fp_lighting && terrain && !fog_hidden;
    surf.nx = nx;
    surf.ny = ny;
    surf.nz = nz;

    FPScreenVertex verts[8];
    for (int ci = 0; ci < clipped_count; ci++)
    {
      fp_project_camera_point(basis, &clipped[ci], fb->w, fb->h, &verts[ci].x, &verts[ci].y,
                              &verts[ci].z);
      const float iw = (fabsf(verts[ci].z) > 1e-9f) ? (1.0f / verts[ci].z) : 0.0f;
      verts[ci].iw = iw;
      verts[ci].u_over_z = clipped[ci].u * iw;
      verts[ci].v_over_z = clipped[ci].v * iw;
      verts[ci].wx_over_z = clipped[ci].wx * iw;
      verts[ci].wy_over_z = clipped[ci].wy * iw;
      verts[ci].wz_over_z = clipped[ci].wz * iw;
    }

    for (int ci = 2; ci < clipped_count; ci++)
      fp_fill_triangle(fb->depth, fb->color, fb->stride, fb->w, fb->h, &verts[0],
                       &verts[ci - 1], &verts[ci], &surf);
  }
  }
}

static void render_mesh_cpu(SDL_Renderer *ren, const World *world, const VoxelMesh *mesh,
                            const FPCamera *cam, int panel_x, int panel_y, int panel_w, int panel_h,
                            const FogAtlas *fog)
{
  if (!ren || !world || !mesh || !cam || panel_w <= 0 || panel_h <= 0)
    return;

  FPFrameBuffer fb;
  if (!fp_frame_lock(ren, panel_x, panel_y, panel_w, panel_h, &fb))
    return;

  FPCameraBasis basis;
  fp_camera_build_basis(cam, &basis);
  basis.aspect = (fb.h > 0) ? ((float)fb.w / (float)fb.h) : 1.0f;

  fp_renderer_clear_material_frame_stats();
  fp_raster_mesh(&fb, cam, &basis, world, mesh, fog, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, true,
                 255, 255, 255);
  {
    const FPSparseCellList *sparse =
        (s_mesh_cache.valid && s_mesh_cache.world == world) ? &s_mesh_cache.sparse : NULL;
    fp_draw_sparse_material_instances(&fb, cam, &basis, world, sparse, 0.0f, 0.0f, 0.0f);
  }
  fp_draw_projectiles(&fb, cam, NULL);
  fp_draw_debris(&fb, cam, NULL);
  fp_draw_debris_volumes(&fb, cam, world, NULL);
  fp_draw_particles(&fb, cam, world);
  fp_draw_swing(&fb, cam);
  fp_draw_actors(&fb, cam, &basis, world, true);
  fp_draw_actor_health_bars(&fb, cam, world);
  fp_frame_present(ren, panel_x, panel_y, panel_w, panel_h);
}

// Neighbour rays are traced on a coarser grid than the mesh raster, and each result fills its
// block. Everything they can reach is at least the distance to a world boundary away, so at three
// pixels the samples are still finer than a voxel is wide on screen out to about seventy voxels.
// A silhouette refine then re-traces sky pixels that touch a filled surface so the horizon does
// not stair-step at the coarse stride. (Stride 2 + refine passes the cheap check but pushes the
// empty-sky pathological case near the 15 ms absolute ceiling.)
#define FP_NEIGHBOUR_RAY_STRIDE 3

// Every cluster slot except the one the camera is standing in.
#define FP_NEIGHBOUR_SLOTS \
  ((((ShadowSlotMask)1 << SHADOW_SLOT_COUNT) - 1) & ~SHADOW_SLOT_BIT(SHADOW_CENTRE_SLOT))

typedef struct
{
  float eye[3];
  float box_lo[3];
  float box_hi[3];
  float cluster_hi[3];
  float band_bottom;
  float band_top;
  float ax;
  float ay;
  bool textured;
} FPNeighbourRayCtx;

// Trace one sky pixel into the neighbour ring. Returns false on miss / empty stretch.
static bool fp_neighbour_trace_pixel(const FPFrameBuffer *fb, const FPCameraBasis *basis,
                                     const ShadowWorld *cluster, const FogAtlas *fog_atlas,
                                     const FPNeighbourRayCtx *ctx, int px, int py, float *out_depth,
                                     Uint32 *out_color, int32_t *out_vox_id)
{
  if (!fb || !basis || !cluster || !ctx || px < 0 || py < 0 || px >= fb->w || py >= fb->h)
    return false;

  const float ndc_x = (2.0f * ((float)px + 0.5f) / (float)fb->w) - 1.0f;
  const float ndc_y = 1.0f - (2.0f * ((float)py + 0.5f) / (float)fb->h);
  float dir[3] = {
      basis->fwd_x + ndc_y * ctx->ay * basis->up_x + ndc_x * ctx->ax * basis->right_x,
      basis->fwd_y + ndc_y * ctx->ay * basis->up_y + ndc_x * ctx->ax * basis->right_y,
      basis->fwd_z + ndc_y * ctx->ay * basis->up_z + ndc_x * ctx->ax * basis->right_z,
  };

  const float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
  if (len < 1e-6f)
    return false;
  const float inv_len = 1.0f / len;
  dir[0] *= inv_len;
  dir[1] *= inv_len;
  dir[2] *= inv_len;

  float t_exit = FP_FOG_END;
  float t_cluster = FP_FOG_END;
  for (int i = 0; i < 3; i++)
  {
    if (fabsf(dir[i]) < 1e-9f)
      continue;
    const float inv = 1.0f / dir[i];
    const float t_box = ((dir[i] > 0.0f ? ctx->box_hi[i] : ctx->box_lo[i]) - ctx->eye[i]) * inv;
    if (t_box < t_exit)
      t_exit = t_box;
    const float t_out = ((dir[i] > 0.0f ? ctx->cluster_hi[i] : 0.0f) - ctx->eye[i]) * inv;
    if (t_out < t_cluster)
      t_cluster = t_out;
  }
  if (t_exit < 0.0f)
    t_exit = 0.0f;

  float t_begin = t_exit;
  float t_end = (t_cluster < FP_FOG_END) ? t_cluster : FP_FOG_END;

  if (fabsf(dir[2]) < 1e-9f)
  {
    if (ctx->eye[2] < ctx->band_bottom || ctx->eye[2] >= ctx->band_top)
      return false;
  }
  else
  {
    const float inv_z = 1.0f / dir[2];
    float t_lo = (ctx->band_bottom - ctx->eye[2]) * inv_z;
    float t_hi = (ctx->band_top - ctx->eye[2]) * inv_z;
    if (t_lo > t_hi)
    {
      const float swap = t_lo;
      t_lo = t_hi;
      t_hi = swap;
    }
    if (t_lo > t_begin)
      t_begin = t_lo;
    if (t_hi < t_end)
      t_end = t_hi;
  }

  const float reach = t_end - t_begin;
  if (reach <= 1.0f)
    return false;

  const float t_start = (t_begin > 0.01f) ? (t_begin - 0.01f) : 0.0f;
  const float start[3] = {ctx->eye[0] + dir[0] * t_start, ctx->eye[1] + dir[1] * t_start,
                          ctx->eye[2] + dir[2] * t_start};
  const int max_steps = (int)(reach * 1.7321f) + 3;

  ShadowRayResult res;
  if (!shadow_world_raycast(cluster, start[0], start[1], start[2], dir[0], dir[1], dir[2],
                            max_steps, FP_NEIGHBOUR_SLOTS, &res))
    return false;

  int lx = 0, ly = 0, lz = 0;
  const int slot = shadow_world_route(cluster, res.cx, res.cy, res.cz, &lx, &ly, &lz);
  if (slot < 0)
    return false;
  const World *hit_world = shadow_world_slot_world(cluster, slot);
  if (!hit_world)
    return false;

  uint8_t r = 0, g = 0, b = 0;
  const bool fog_hidden = fog_atlas && lx >= 0 && ly >= 0 && lz >= 0 &&
                          !fog_is_explored(fog_atlas, hit_world, (uint32_t)lx, (uint32_t)ly,
                                           (uint32_t)lz);
  if (fog_hidden)
    fp_shade_unexplored(&r, &g, &b);
  else
  {
    fp_shade_ray_hit(world_voxel_cptr_fast(hit_world, lx, ly, lz), ctx->textured, start[0],
                     start[1], start[2], dir[0], dir[1], dir[2], res.cx, res.cy, res.cz, &r, &g,
                     &b);

    const float to_hit[3] = {(float)res.cx + 0.5f - ctx->eye[0], (float)res.cy + 0.5f - ctx->eye[1],
                             (float)res.cz + 0.5f - ctx->eye[2]};
    const float dist =
        sqrtf(to_hit[0] * to_hit[0] + to_hit[1] * to_hit[1] + to_hit[2] * to_hit[2]);
    if (!s_fp_lighting)
      fp_apply_fog(dist, &r, &g, &b);
  }

  const float to_hit[3] = {(float)res.cx + 0.5f - ctx->eye[0], (float)res.cy + 0.5f - ctx->eye[1],
                           (float)res.cz + 0.5f - ctx->eye[2]};
  *out_depth = to_hit[0] * basis->fwd_x + to_hit[1] * basis->fwd_y + to_hit[2] * basis->fwd_z;
  *out_color = fp_rgba8888(r, g, b);
  if (out_vox_id)
    *out_vox_id = (s_fp_lighting && !fog_hidden) ? fp_pack_vox(res.cx, res.cy, res.cz) : 0;
  return true;
}

// Mesh-raster the closest ring (Chebyshev distance 1) before the coarse neighbour ray fill.
// Cached LOD meshes for any ring-1 slot are drawn; warm prefers faces, then edges, then corners.
static void fp_raster_cluster_ring1(const FPFrameBuffer *fb, const FPCamera *cam,
                                    const FPCameraBasis *basis, const ShadowWorld *cluster,
                                    const FogAtlas *fog)
{
  if (!fb || !cam || !basis || !cluster || !fp_ring1_enabled())
    return;

  int slots_drawn = 0;
  int quads_drawn = 0;

  // Raster any ring-1 slot that already has a cached mesh. Missing meshes stay on the coarse
  // neighbour-ray path until the post-frame warm fills them. Sky-only fill never overwrites
  // centre-world pixels.
  s_fp_fill_sky_only = true;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (slot == SHADOW_CENTRE_SLOT || !(cluster->loaded_mask & SHADOW_SLOT_BIT(slot)))
      continue;

    int dx = 0, dy = 0, dz = 0;
    shadow_slot_offsets(slot, &dx, &dy, &dz);
    const int manh = abs(dx) + abs(dy) + abs(dz);
    if (manh < 1 || manh > 3)
      continue;

    const World *w = shadow_world_slot_world(cluster, slot);
    if (!w)
      continue;

    const VoxelMesh *mesh = get_ring1_cached_mesh(w, slot, false);
    if (!mesh || mesh->count <= 0)
      continue;

    const float ox = (float)(dx * (int)cluster->world_w);
    const float oy = (float)(dy * (int)cluster->world_h);
    const float oz = (float)(dz * (int)cluster->world_d);
    fp_raster_mesh(fb, cam, basis, w, mesh, fog, ox, oy, oz, 1.0f, 0.0f, 0.0f, 0.0f, true, 255, 255,
                   255);
    {
      const FPSparseCellList *sparse = NULL;
      for (int i = 0; i < FP_RING1_CACHE_CAP; i++)
      {
        const Ring1CachedMesh *e = &s_ring1_cache[i];
        if (e->valid && e->world == w && e->shadow_slot == slot)
        {
          sparse = &e->sparse;
          break;
        }
      }
      fp_draw_sparse_material_instances(fb, cam, basis, w, sparse, ox, oy, oz);
    }
    slots_drawn++;
    quads_drawn += mesh->count;
  }
  s_fp_fill_sky_only = false;

  s_fp_cluster_profile.ring1_slots = slots_drawn;
  s_fp_cluster_profile.ring1_quads = quads_drawn;
  s_fp_cluster_profile.ring1_builds = 0;
}

// Draw the worlds around the player into the pixels the centre world left as sky.
//
// Running this after the centre world, and only where the centre world drew nothing, is exact
// rather than an approximation. The centre world fills a convex box that the camera sits inside, so
// a straight ray is within that box for one unbroken stretch beginning at the eye and never
// re-enters once it leaves. Every centre-world hit is therefore nearer than every neighbour hit:
// where the mesh raster put a surface, no neighbour could have shown through it, and where it left
// sky, the neighbour is the first thing along the ray.
//
// That ordering is also what makes this affordable, because it leaves only the sky to pay for, and
// each of those rays is then trimmed to the stretch that could actually hold something visible: it
// starts where the ray leaves the centre box, so the cluster's own two million centre cells are
// never stepped over, and it ends at the nearest of the fog wall, the far side of the cluster, and
// the band of layers the neighbours occupy.
static void fp_raster_cluster_neighbours(const FPFrameBuffer *fb, const FPCamera *cam,
                                         const FPCameraBasis *basis, const ShadowWorld *cluster,
                                         const FogAtlas *fog_atlas)
{
  // Neighbours stream in behind the player's arrival, so for the first frames after a transition
  // there is genuinely nothing out there to draw.
  if (!(cluster->loaded_mask & FP_NEIGHBOUR_SLOTS))
    return;

  // When every loaded neighbour already has a centre-quality ring-1 mesh, the sky-only mesh pass
  // above covered them. Tracing rays through that sky only rediscovers the same surfaces (or
  // misses into empty air) and is the pathological cost in the cluster bench.
  if (fp_ring1_enabled())
  {
    int loaded = 0, meshed_full = 0;
    for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
    {
      if (slot == SHADOW_CENTRE_SLOT || !(cluster->loaded_mask & SHADOW_SLOT_BIT(slot)))
        continue;
      loaded++;
      const World *w = shadow_world_slot_world(cluster, slot);
      if (!w)
        continue;
      const int idx = fp_ring1_find_slot(w, slot);
      if (idx >= 0 && s_ring1_cache[idx].full_quality)
        meshed_full++;
    }
    if (loaded > 0 && meshed_full >= loaded)
      return;
  }

  int origin_x = 0, origin_y = 0, origin_z = 0;
  shadow_world_slot_origin(cluster, SHADOW_CENTRE_SLOT, &origin_x, &origin_y, &origin_z);

  // Cluster space, in which the centre world is the box the eye sits inside.
  const float eye[3] = {cam->x + (float)origin_x, cam->y + (float)origin_y,
                        cam->z + (float)origin_z};
  const float box_lo[3] = {(float)origin_x, (float)origin_y, (float)origin_z};
  const float box_hi[3] = {box_lo[0] + (float)cluster->world_w, box_lo[1] + (float)cluster->world_h,
                           box_lo[2] + (float)cluster->world_d};

  // The band of cluster-space z the loaded neighbours could possibly occupy. Each world keeps a
  // conservative range of the layers that hold anything — never narrower than the truth, and -1
  // when it cannot say — so outside this band there is provably nothing to find.
  //
  // This is the bound that matters. A ray that hits something stops there and is cheap; a ray that
  // hits nothing is the one that spends its whole step budget, and sky is nothing but those.
  float band_top = -1.0f;
  float band_bottom = (float)cluster->extent_z + 1.0f;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (slot == SHADOW_CENTRE_SLOT || !(cluster->loaded_mask & SHADOW_SLOT_BIT(slot)))
      continue;
    const World *w = shadow_world_slot_world(cluster, slot);
    if (!w)
      continue;

    int slot_z = 0;
    shadow_world_slot_origin(cluster, slot, NULL, NULL, &slot_z);
    const int z_min = (w->occupied_z_min >= 0) ? w->occupied_z_min : 0;
    const int z_max = (w->occupied_z_max >= 0) ? w->occupied_z_max : (int)cluster->world_d - 1;

    // A voxel at index z fills up to z+1, so the top of the band is one layer above the last one.
    const float top = (float)(slot_z + z_max + 1);
    const float bottom = (float)(slot_z + z_min);
    if (top > band_top)
      band_top = top;
    if (bottom < band_bottom)
      band_bottom = bottom;
  }

  if (band_top <= band_bottom)
    return; // every loaded neighbour is empty

  FPNeighbourRayCtx ctx;
  ctx.eye[0] = eye[0];
  ctx.eye[1] = eye[1];
  ctx.eye[2] = eye[2];
  ctx.box_lo[0] = box_lo[0];
  ctx.box_lo[1] = box_lo[1];
  ctx.box_lo[2] = box_lo[2];
  ctx.box_hi[0] = box_hi[0];
  ctx.box_hi[1] = box_hi[1];
  ctx.box_hi[2] = box_hi[2];
  ctx.cluster_hi[0] = (float)cluster->extent_x;
  ctx.cluster_hi[1] = (float)cluster->extent_y;
  ctx.cluster_hi[2] = (float)cluster->extent_z;
  ctx.band_bottom = band_bottom;
  ctx.band_top = band_top;
  ctx.ax = basis->half_tan * basis->aspect;
  ctx.ay = basis->half_tan;
  ctx.textured = fp_subvoxel_enabled();

  const int step = FP_NEIGHBOUR_RAY_STRIDE;
  // Hits closer than this on a mixed (centre+sky) block get per-pixel samples — sharpens the
  // world-boundary silhouette without paying full resolution across the whole horizon.
  const float near_hi_res = (float)cluster->world_w * 1.25f;
  // Refine only against neighbour fills — centre silhouettes are already handled by seed-from-sky,
  // and refining them casts the empty-sky pathological case into the absolute ms ceiling.
  const float neighbour_depth_min = (float)cluster->world_w * 0.55f;

  for (int block_y = 0; block_y < fb->h; block_y += step)
  {
    const int block_h = (block_y + step <= fb->h) ? step : (fb->h - block_y);
    for (int block_x = 0; block_x < fb->w; block_x += step)
    {
      const int block_w = (block_x + step <= fb->w) ? step : (fb->w - block_x);

      int seed_x = -1, seed_y = -1;
      bool mixed = false;
      for (int y = block_y; y < block_y + block_h; y++)
        for (int x = block_x; x < block_x + block_w; x++)
        {
          if (fb->depth[y * fb->stride + x] < FP_DEPTH_FAR)
            mixed = true;
          else if (seed_x < 0)
          {
            seed_x = x;
            seed_y = y;
          }
        }
      if (seed_x < 0)
        continue;

      float depth = 0.0f;
      Uint32 packed = 0;
      int32_t vox_id = 0;
      if (!fp_neighbour_trace_pixel(fb, basis, cluster, fog_atlas, &ctx, seed_x, seed_y, &depth,
                                    &packed, &vox_id))
        continue;

      // Boundary silhouette blocks with a near hit: per-pixel. Near pure-sky hits: 2px subfill
      // (sharper closest-ring terrain without the full-horizon per-pixel bill). Farther: block fill.
      const bool hires = mixed && depth < near_hi_res;
      const bool near_sub = !hires && depth < near_hi_res;
      if (hires)
      {
        for (int y = block_y; y < block_y + block_h; y++)
          for (int x = block_x; x < block_x + block_w; x++)
          {
            const int idx = y * fb->stride + x;
            if (fb->depth[idx] < FP_DEPTH_FAR)
              continue;
            if (x == seed_x && y == seed_y)
            {
              fb->depth[idx] = depth;
              fb->color[idx] = packed;
              if (s_fp_lighting && s_voxel_id && vox_id)
                s_voxel_id[idx] = vox_id;
              continue;
            }
            float d2 = 0.0f;
            Uint32 c2 = 0;
            int32_t v2 = 0;
            if (!fp_neighbour_trace_pixel(fb, basis, cluster, fog_atlas, &ctx, x, y, &d2, &c2, &v2))
              continue;
            fb->depth[idx] = d2;
            fb->color[idx] = c2;
            if (s_fp_lighting && s_voxel_id && v2)
              s_voxel_id[idx] = v2;
          }
      }
      else if (near_sub)
      {
        const int sub = 2;
        for (int y0 = block_y; y0 < block_y + block_h; y0 += sub)
        {
          const int sub_h = (y0 + sub <= block_y + block_h) ? sub : (block_y + block_h - y0);
          for (int x0 = block_x; x0 < block_x + block_w; x0 += sub)
          {
            const int sub_w = (x0 + sub <= block_x + block_w) ? sub : (block_x + block_w - x0);
            int sx = -1, sy = -1;
            for (int y = y0; y < y0 + sub_h && sx < 0; y++)
              for (int x = x0; x < x0 + sub_w; x++)
                if (fb->depth[y * fb->stride + x] >= FP_DEPTH_FAR)
                {
                  sx = x;
                  sy = y;
                  break;
                }
            if (sx < 0)
              continue;
            float d2 = depth;
            Uint32 c2 = packed;
            int32_t v2 = vox_id;
            if (sx != seed_x || sy != seed_y)
            {
              if (!fp_neighbour_trace_pixel(fb, basis, cluster, fog_atlas, &ctx, sx, sy, &d2, &c2,
                                            &v2))
                continue;
            }
            for (int y = y0; y < y0 + sub_h; y++)
              for (int x = x0; x < x0 + sub_w; x++)
              {
                const int idx = y * fb->stride + x;
                if (fb->depth[idx] < FP_DEPTH_FAR)
                  continue;
                fb->depth[idx] = d2;
                fb->color[idx] = c2;
                if (s_fp_lighting && s_voxel_id && v2)
                  s_voxel_id[idx] = v2;
              }
          }
        }
      }
      else
      {
        for (int y = block_y; y < block_y + block_h; y++)
          for (int x = block_x; x < block_x + block_w; x++)
          {
            const int idx = y * fb->stride + x;
            if (fb->depth[idx] < FP_DEPTH_FAR)
              continue;
            fb->depth[idx] = depth;
            fb->color[idx] = packed;
            if (s_fp_lighting && s_voxel_id && vox_id)
              s_voxel_id[idx] = vox_id;
          }
      }
    }
  }

  // Silhouette refine against neighbour fills only (not centre geometry): cleans hit/miss tile
  // edges without paying empty long rays along the centre skyline.
  for (int y = 0; y < fb->h; y++)
  {
    for (int x = 0; x < fb->w; x++)
    {
      const int idx = y * fb->stride + x;
      if (fb->depth[idx] < FP_DEPTH_FAR)
        continue;
      bool border = false;
      const int nidx[4] = {x > 0 ? idx - 1 : -1, x + 1 < fb->w ? idx + 1 : -1,
                           y > 0 ? idx - fb->stride : -1,
                           y + 1 < fb->h ? idx + fb->stride : -1};
      for (int ni = 0; ni < 4; ni++)
      {
        if (nidx[ni] < 0)
          continue;
        const float nd = fb->depth[nidx[ni]];
        if (nd >= neighbour_depth_min && nd < FP_DEPTH_FAR)
        {
          border = true;
          break;
        }
      }
      if (!border)
        continue;

      float d = 0.0f;
      Uint32 packed = 0;
      int32_t vox_id = 0;
      if (!fp_neighbour_trace_pixel(fb, basis, cluster, fog_atlas, &ctx, x, y, &d, &packed, &vox_id))
        continue;
      fb->depth[idx] = d;
      fb->color[idx] = packed;
      if (s_fp_lighting && s_voxel_id && vox_id)
        s_voxel_id[idx] = vox_id;
    }
  }
}

#define FP_LIGHT_CACHE 4096
#define FP_LIGHT_UNIQUE_CAP 3072
#define FP_SUN_DX 0.38f
#define FP_SUN_DY 0.16f
#define FP_SUN_DZ 0.91f
// Temporal EMA: keep most of last frame's irradiance; clamp per-frame jumps so sparse
// occupancy samples cannot strobe (binary sun/AO rays are the main flicker source).
#define FP_LIGHT_TEMPORAL_NUM 7
#define FP_LIGHT_TEMPORAL_DEN 8
#define FP_LIGHT_TEMPORAL_CLAMP 6
// AO directions rotate every N frames so the same hemisphere samples stick briefly.
#define FP_LIGHT_AO_PHASE_SHIFT 3

// Orthographic sun depth of movers (actors + debris). Occupancy rays never see them; terrain
// lighting samples this map instead (Teardown part-2 dynamic shadow split).
#define FP_ACTOR_SHADOW_RES 128
#define FP_ACTOR_SHADOW_EXTENT 48.0f
#define FP_ACTOR_SHADOW_FAR 96.0f
#define FP_ACTOR_SHADOW_BIAS 0.35f
#define FP_ACTOR_SHADOW_DARK 0.58f
#define FP_ACTOR_SHADOW_PENUMBRA 1.15f // soft depth band (world units along sun)
#define FP_ACTOR_SHADOW_PCF 1.1f      // PCF tap radius in shadow-map texels
#define FP_DEBRIS_VOL_SHADOW_BUDGET 48 // max sphere splats per debris volume

static uint64_t s_light_cache_rev = 0;
static uint32_t s_light_cache_key[FP_LIGHT_CACHE];
static uint8_t s_light_cache_val[FP_LIGHT_CACHE];
static uint32_t s_light_frame = 0;
// Previous-frame actor-shadow scales keyed like the light cache (reduces mover cutout flicker).
static uint32_t s_ash_cache_key[FP_LIGHT_CACHE];
static uint8_t s_ash_cache_val[FP_LIGHT_CACHE];

static float s_actor_shadow[FP_ACTOR_SHADOW_RES * FP_ACTOR_SHADOW_RES];
static bool s_actor_shadow_ready = false;
static float s_ash_cx = 0.0f, s_ash_cy = 0.0f, s_ash_cz = 0.0f;
static float s_ash_rx = 1.0f, s_ash_ry = 0.0f, s_ash_rz = 0.0f;
static float s_ash_ux = 0.0f, s_ash_uy = 1.0f, s_ash_uz = 0.0f;
static float s_ash_fx = 0.0f, s_ash_fy = 0.0f, s_ash_fz = -1.0f;

static void fp_hemi_dir(uint32_t h, float nx, float ny, float nz, float *dx, float *dy, float *dz)
{
  float tx = (fabsf(nx) < 0.9f) ? 0.0f : 1.0f;
  float ty = (fabsf(nx) < 0.9f) ? 1.0f : 0.0f;
  float tz = 0.0f;
  float bx = ny * tz - nz * ty;
  float by = nz * tx - nx * tz;
  float bz = nx * ty - ny * tx;
  const float bl = sqrtf(bx * bx + by * by + bz * bz);
  if (bl < 1e-6f)
  {
    *dx = nx;
    *dy = ny;
    *dz = nz;
    return;
  }
  bx /= bl;
  by /= bl;
  bz /= bl;
  tx = by * nz - bz * ny;
  ty = bz * nx - bx * nz;
  tz = bx * ny - by * nx;

  const float u = (float)(h & 0xFFFFu) / 65535.0f;
  const float v = (float)((h >> 16) & 0xFFFFu) / 65535.0f;
  const float r = sqrtf(u);
  const float phi = 6.2831853f * v;
  const float hx = r * cosf(phi);
  const float hy = r * sinf(phi);
  const float hz = sqrtf(fmaxf(0.0f, 1.0f - u));
  *dx = tx * hx + bx * hy + nx * hz;
  *dy = ty * hx + by * hy + ny * hz;
  *dz = tz * hx + bz * hy + nz * hz;
}

static float fp_occupancy_visible(const ShadowWorld *sw, float ox, float oy, float oz, float dx,
                                  float dy, float dz, int max_steps)
{
  ShadowRayResult res;
  if (shadow_world_raycast_occupancy(sw, ox, oy, oz, dx, dy, dz, max_steps, 0u, &res))
    return 0.0f;
  return 1.0f;
}

static float fp_world_visible(const World *world, float ox, float oy, float oz, float dx, float dy,
                              float dz, int max_steps)
{
  if (world && world->occupancy_bits)
    return gpu_voxel_buffer_visible(world->occupancy_bits, ox, oy, oz, dx, dy, dz, max_steps);
  int hx = -1, hy = -1, hz = -1;
  if (world_raycast_first_hit(world, ox, oy, oz, dx, dy, dz, max_steps, &hx, &hy, &hz))
    return 0.0f;
  return 1.0f;
}

// Warmth contributed when an AO ray lands on glowing material (Teardown-style emissive palette).
static float fp_emissive_strength(VoxelType t)
{
  switch (t)
  {
  case VOXEL_MAGMA:
    return 1.0f;
  case VOXEL_CRYSTAL:
  case VOXEL_CRYSTAL_RED:
  case VOXEL_CRYSTAL_GREEN:
  case VOXEL_CRYSTAL_BLUE:
    return 0.55f;
  default:
    return 0.0f;
  }
}

// 0 = mirror, 1 = fully diffuse. Placeholder roughness until palette materials carry it.
static float fp_roughness(VoxelType t)
{
  if (t == VOXEL_WATER || t == VOXEL_ICE)
    return 0.12f;
  if (voxel_type_is_glass(t) || t == VOXEL_CRYSTAL || t == VOXEL_CRYSTAL_RED ||
      t == VOXEL_CRYSTAL_GREEN || t == VOXEL_CRYSTAL_BLUE)
    return 0.18f;
  if (t == VOXEL_MAGMA)
    return 0.35f;
  if (voxel_type_is_vegetation(t))
    return 0.92f;
  if (t >= VOXEL_WOOD && t <= VOXEL_WOOD_REDWOOD)
    return 0.72f;
  if (t == VOXEL_PLANK)
    return 0.72f;
  return 0.55f; // stone / soil / generic
}

static uint8_t fp_damage_at(const ShadowWorld *sw, const World *world, int cx, int cy, int cz)
{
  if (sw)
  {
    int lx = 0, ly = 0, lz = 0;
    const int slot = shadow_world_route(sw, cx, cy, cz, &lx, &ly, &lz);
    if (slot < 0)
      return 0;
    const World *w = shadow_world_slot_world(sw, slot);
    if (!w || !world_pos_in_bounds_fast((World *)w, lx, ly, lz))
      return 0;
    const Voxel *v = world_voxel_cptr_fast(w, lx, ly, lz);
    return v ? voxel_get_damage(v) : 0;
  }
  if (world && world_pos_in_bounds_fast((World *)world, cx, cy, cz))
  {
    const Voxel *v = world_voxel_cptr_fast(world, cx, cy, cz);
    return v ? voxel_get_damage(v) : 0;
  }
  return 0;
}

static float fp_ao_sample(const ShadowWorld *sw, const World *world, float ox, float oy, float oz,
                          float dx, float dy, float dz, int max_steps, float *emissive_out)
{
  if (emissive_out)
    *emissive_out = 0.0f;
  if (sw)
  {
    ShadowRayResult res;
    if (!shadow_world_raycast_occupancy(sw, ox, oy, oz, dx, dy, dz, max_steps, 0u, &res))
      return 1.0f; // sky
    if (emissive_out)
      *emissive_out = fp_emissive_strength(shadow_world_type_at(sw, res.cx, res.cy, res.cz));
    return 0.0f;
  }
  if (world && world->occupancy_bits)
  {
    int hx = -1, hy = -1, hz = -1;
    if (!gpu_voxel_buffer_raycast(world->occupancy_bits, ox, oy, oz, dx, dy, dz, max_steps, &hx,
                                  &hy, &hz))
      return 1.0f;
    if (emissive_out && world_pos_in_bounds_fast((World *)world, hx, hy, hz))
    {
      const Voxel *v = world_voxel_cptr_fast(world, hx, hy, hz);
      if (v)
        *emissive_out = fp_emissive_strength(v->type);
    }
    return 0.0f;
  }
  int hx = -1, hy = -1, hz = -1;
  if (!world_raycast_first_hit(world, ox, oy, oz, dx, dy, dz, max_steps, &hx, &hy, &hz))
    return 1.0f;
  if (emissive_out && world && world_pos_in_bounds_fast((World *)world, hx, hy, hz))
  {
    const Voxel *v = world_voxel_cptr_fast(world, hx, hy, hz);
    if (v)
      *emissive_out = fp_emissive_strength(v->type);
  }
  return 0.0f;
}

static void fp_estimate_normal(const ShadowWorld *sw, const World *world, int cx, int cy, int cz,
                               float *nx, float *ny, float *nz, float *occ_ao)
{
  // Implicit surface normal from the occupancy gradient (Devlog #17/#22): empty neighbours pull
  // the normal outward. Face-axis-only normals stay as a thin-wall fallback when every face is
  // buried. 26-neighbourhood gives smoother rocks than the old 6-tap.
  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  int empty = 0;
  int face_empty = 0;
  for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        bool solid = false;
        if (sw)
          solid = shadow_world_sample(sw, cx + dx, cy + dy, cz + dz) == SHADOW_SOLID;
        else if (world)
          solid = world_is_solid_fast((World *)world, cx + dx, cy + dy, cz + dz);
        if (!solid)
        {
          // Face taps weigh more than edge/corner so thin slabs keep a sensible normal.
          const int manhattan = (dx != 0 ? 1 : 0) + (dy != 0 ? 1 : 0) + (dz != 0 ? 1 : 0);
          const float w = (manhattan == 1) ? 1.0f : (manhattan == 2 ? 0.35f : 0.2f);
          ax += (float)dx * w;
          ay += (float)dy * w;
          az += (float)dz * w;
          empty++;
          if (manhattan == 1)
            face_empty++;
        }
      }
  *nx = 0.0f;
  *ny = 0.0f;
  *nz = 1.0f;
  if (empty > 0)
  {
    const float len = sqrtf(ax * ax + ay * ay + az * az);
    if (len > 1e-5f)
    {
      *nx = ax / len;
      *ny = ay / len;
      *nz = az / len;
    }
  }
  *occ_ao = (face_empty <= 0) ? 0.12f : (float)face_empty / 6.0f;
}

static void fp_sun_jitter(uint32_t h, float sdx, float sdy, float sdz, float soft, float *ox,
                          float *oy, float *oz)
{
  // Build a small basis perpendicular to the sun and offset the ray direction for penumbra.
  float tx = (fabsf(sdx) < 0.9f) ? 0.0f : 1.0f;
  float ty = (fabsf(sdx) < 0.9f) ? 1.0f : 0.0f;
  float tz = 0.0f;
  float bx = sdy * tz - sdz * ty;
  float by = sdz * tx - sdx * tz;
  float bz = sdx * ty - sdy * tx;
  float bl = sqrtf(bx * bx + by * by + bz * bz);
  if (bl < 1e-6f)
  {
    *ox = sdx;
    *oy = sdy;
    *oz = sdz;
    return;
  }
  bx /= bl;
  by /= bl;
  bz /= bl;
  tx = by * sdz - bz * sdy;
  ty = bz * sdx - bx * sdz;
  tz = bx * sdy - by * sdx;
  const float u = ((float)(h & 0xFFFFu) / 65535.0f) * 2.0f - 1.0f;
  const float v = ((float)((h >> 16) & 0xFFFFu) / 65535.0f) * 2.0f - 1.0f;
  float dx = sdx + (tx * u + bx * v) * soft;
  float dy = sdy + (ty * u + by * v) * soft;
  float dz = sdz + (tz * u + bz * v) * soft;
  const float len = sqrtf(dx * dx + dy * dy + dz * dz);
  *ox = dx / len;
  *oy = dy / len;
  *oz = dz / len;
}

static uint8_t fp_light_at(const ShadowWorld *sw, const World *world, int cx, int cy, int cz)
{
  float nx, ny, nz, occ_ao;
  fp_estimate_normal(sw, world, cx, cy, cz, &nx, &ny, &nz, &occ_ao);

  const float slen = sqrtf(FP_SUN_DX * FP_SUN_DX + FP_SUN_DY * FP_SUN_DY + FP_SUN_DZ * FP_SUN_DZ);
  const float sdx = FP_SUN_DX / slen;
  const float sdy = FP_SUN_DY / slen;
  const float sdz = FP_SUN_DZ / slen;
  const float n_dot_l = fmaxf(0.0f, nx * sdx + ny * sdy + nz * sdz);

  // Along the sun axis fog distance is enough; √3·fog was over-stepping open-sky misses.
  const int sun_steps = (int)FP_FOG_END + 8;
  float sun_vis = 0.0f;
  float sun_centre = 0.0f;
  // Soft sun: stable centre ray + one seed-fixed cone sample (no per-frame rotation).
  // Temporal hash on the soft ray was the main strobing source at penumbra edges.
  const uint32_t seed = voxel_crack_seed(cx, cy, cz);
  if (n_dot_l > 0.02f)
  {
    const float base_ox = (float)cx + 0.5f + sdx * 0.52f;
    const float base_oy = (float)cy + 0.5f + sdy * 0.52f;
    const float base_oz = (float)cz + 0.5f + sdz * 0.52f;
    sun_centre = sw ? fp_occupancy_visible(sw, base_ox, base_oy, base_oz, sdx, sdy, sdz, sun_steps)
                    : fp_world_visible(world, base_ox, base_oy, base_oz, sdx, sdy, sdz, sun_steps);
    float jx, jy, jz;
    fp_sun_jitter(seed, sdx, sdy, sdz, 0.048f, &jx, &jy, &jz);
    const float soft =
        sw ? fp_occupancy_visible(sw, base_ox, base_oy, base_oz, jx, jy, jz, sun_steps)
           : fp_world_visible(world, base_ox, base_oy, base_oz, jx, jy, jz, sun_steps);
    sun_vis = 0.55f * sun_centre + 0.45f * soft;
  }

  // AO: neighbour occupancy (stable) + VVAO density (Devlog #15) + two hemisphere rays.
  const uint32_t ao_phase = s_light_frame / (uint32_t)FP_LIGHT_AO_PHASE_SHIFT;
  uint32_t h = seed ^ (ao_phase * 0x9e3779b9u);
  float ao_ray = 0.0f;
  float emissive = 0.0f;
  for (int i = 0; i < 2; i++)
  {
    float dx, dy, dz;
    fp_hemi_dir(h ^ (uint32_t)(i * 0x9e3779b9u), nx, ny, nz, &dx, &dy, &dz);
    const float ox = (float)cx + 0.5f + dx * 0.52f;
    const float oy = (float)cy + 0.5f + dy * 0.52f;
    const float oz = (float)cz + 0.5f + dz * 0.52f;
    float e = 0.0f;
    ao_ray += fp_ao_sample(sw, world, ox, oy, oz, dx, dy, dz, 18, &e);
    if (e > emissive)
      emissive = e;
    h = h * 1664525u + 1013904223u;
  }

  // VVAO: fullness > 0.5 darkens; flat ground ≈ 0.5 so it stays neutral.
  float vvao = 1.0f;
  if (sw)
  {
    const float fullness =
        shadow_world_density_sample(sw, (float)cx + 0.5f, (float)cy + 0.5f, (float)cz + 0.5f);
    const float occluded = fmaxf(0.0f, fullness - 0.5f) * 2.0f; // 0..1
    vvao = 1.0f - 0.72f * occluded;
    // Brick glow probe (Devlog #19): cheap secondary emissive without another full ray.
    const float brick_glow =
        shadow_world_emissive_sample(sw, (float)cx + 0.5f + nx * 1.5f, (float)cy + 0.5f + ny * 1.5f,
                                     (float)cz + 0.5f + nz * 1.5f);
    if (brick_glow > emissive)
      emissive = fmaxf(emissive, brick_glow * 0.85f);
  }
  // Lean on stable neighbour + VVAO so AO does not strobe when rays rotate.
  const float ao = 0.40f * occ_ao + 0.35f * vvao + 0.25f * (ao_ray * 0.5f);

  // Self-glow: standing on / being magma or crystal stays lit even in shadow.
  VoxelType self = VOXEL_AIR;
  if (sw)
    self = shadow_world_type_at(sw, cx, cy, cz);
  else if (world && world_pos_in_bounds_fast((World *)world, cx, cy, cz))
  {
    const Voxel *v = world_voxel_cptr_fast(world, cx, cy, cz);
    if (v)
      self = v->type;
  }
  const float self_glow = fp_emissive_strength(self);
  // Cracks raise roughness and mute specular so damaged faces stop flashing highlights.
  const float crack = (float)fp_damage_at(sw, world, cx, cy, cz) * (1.0f / 255.0f);
  float roughness = fp_roughness(self);
  roughness = fminf(1.0f, roughness + 0.55f * crack);
  const float gloss = 1.0f - roughness;

  // Specular occlusion: use the stable centre sun ray so highlights do not flicker with the cone.
  float spec = 0.0f;
  if (gloss > 0.08f && n_dot_l > 0.05f && sun_centre > 0.02f)
  {
    const float rx = 2.0f * n_dot_l * nx - sdx;
    const float ry = 2.0f * n_dot_l * ny - sdy;
    const float rz = 2.0f * n_dot_l * nz - sdz;
    const float rlen = sqrtf(rx * rx + ry * ry + rz * rz);
    if (rlen > 1e-5f)
    {
      const float ox = (float)cx + 0.5f + nx * 0.52f;
      const float oy = (float)cy + 0.5f + ny * 0.52f;
      const float oz = (float)cz + 0.5f + nz * 0.52f;
      const int spec_steps = 18;
      const float spec_vis =
          sw ? fp_occupancy_visible(sw, ox, oy, oz, rx / rlen, ry / rlen, rz / rlen, spec_steps)
             : fp_world_visible(world, ox, oy, oz, rx / rlen, ry / rlen, rz / rlen, spec_steps);
      const float lobe = powf(n_dot_l, 1.0f + 5.0f * gloss);
      spec = gloss * spec_vis * sun_centre * lobe;
    }
  }

  float light = 0.12f + 0.32f * ao + 0.44f * sun_vis * n_dot_l + 0.16f * spec +
                0.35f * emissive + 0.55f * self_glow;
  // Cracked faces sit slightly in their own crevice shadow.
  light *= 1.0f - 0.18f * crack;
  if (light < 0.08f)
    light = 0.08f;
  if (light > 1.0f)
    light = 1.0f;
  return (uint8_t)(light * 255.0f + 0.5f);
}

static uint8_t fp_light_voxel(const ShadowWorld *sw, int cx, int cy, int cz)
{
  return fp_light_at(sw, NULL, cx, cy, cz);
}

static uint64_t fp_cluster_light_rev(const ShadowWorld *cluster)
{
  uint64_t rev = 0;
  for (int i = 0; i < SHADOW_SLOT_COUNT; i++)
    rev ^= cluster->slot_revision[i] + (uint64_t)(i + 1) * 0x9e3779b97f4a7c15ull;
  return rev;
}

static uint8_t fp_temporal_blend(uint8_t prev, uint8_t fresh)
{
  int blended = ((int)prev * FP_LIGHT_TEMPORAL_NUM + (int)fresh) / FP_LIGHT_TEMPORAL_DEN;
  const int d = blended - (int)prev;
  if (d > FP_LIGHT_TEMPORAL_CLAMP)
    blended = (int)prev + FP_LIGHT_TEMPORAL_CLAMP;
  else if (d < -FP_LIGHT_TEMPORAL_CLAMP)
    blended = (int)prev - FP_LIGHT_TEMPORAL_CLAMP;
  if (blended < 0)
    blended = 0;
  if (blended > 255)
    blended = 255;
  return (uint8_t)blended;
}

static uint8_t fp_cached_light(const ShadowWorld *cluster, const World *world, uint32_t key, int cx,
                               int cy, int cz)
{
  const uint32_t idx = (key * 0x9e3779b1u) & (FP_LIGHT_CACHE - 1);
  const uint8_t fresh = cluster ? fp_light_voxel(cluster, cx, cy, cz)
                                : fp_light_at(NULL, world, cx, cy, cz);
  if (s_light_cache_key[idx] == key)
  {
    const uint8_t blended = fp_temporal_blend(s_light_cache_val[idx], fresh);
    s_light_cache_val[idx] = blended;
    return blended;
  }
  s_light_cache_key[idx] = key;
  s_light_cache_val[idx] = fresh;
  return fresh;
}

static uint8_t fp_blur_light(uint32_t key, uint8_t centre)
{
  int cx, cy, cz;
  fp_unpack_vox((int32_t)key, &cx, &cy, &cz);
  const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  int sum = centre * 2;
  int weight = 2;
  for (int i = 0; i < 6; i++)
  {
    const uint32_t nk = (uint32_t)fp_pack_vox(cx + dirs[i][0], cy + dirs[i][1], cz + dirs[i][2]);
    const uint32_t idx = (nk * 0x9e3779b1u) & (FP_LIGHT_CACHE - 1);
    if (s_light_cache_key[idx] == nk)
    {
      sum += s_light_cache_val[idx];
      weight++;
    }
  }
  return (uint8_t)(sum / weight);
}

static void fp_ash_basis_from_sun(void)
{
  const float slen = sqrtf(FP_SUN_DX * FP_SUN_DX + FP_SUN_DY * FP_SUN_DY + FP_SUN_DZ * FP_SUN_DZ);
  // Light travels from the sun toward the scene; the orthographic camera looks along that travel.
  s_ash_fx = -FP_SUN_DX / slen;
  s_ash_fy = -FP_SUN_DY / slen;
  s_ash_fz = -FP_SUN_DZ / slen;
  float rx = -s_ash_fy, ry = s_ash_fx, rz = 0.0f;
  float rl = sqrtf(rx * rx + ry * ry + rz * rz);
  if (rl < 1e-5f)
  {
    rx = 1.0f;
    ry = 0.0f;
    rz = 0.0f;
    rl = 1.0f;
  }
  s_ash_rx = rx / rl;
  s_ash_ry = ry / rl;
  s_ash_rz = rz / rl;
  s_ash_ux = s_ash_fy * s_ash_rz - s_ash_fz * s_ash_ry;
  s_ash_uy = s_ash_fz * s_ash_rx - s_ash_fx * s_ash_rz;
  s_ash_uz = s_ash_fx * s_ash_ry - s_ash_fy * s_ash_rx;
}

static void fp_ash_project(float wx, float wy, float wz, float *u, float *v, float *depth)
{
  const float dx = wx - s_ash_cx;
  const float dy = wy - s_ash_cy;
  const float dz = wz - s_ash_cz;
  *u = dx * s_ash_rx + dy * s_ash_ry + dz * s_ash_rz;
  *v = dx * s_ash_ux + dy * s_ash_uy + dz * s_ash_uz;
  *depth = dx * s_ash_fx + dy * s_ash_fy + dz * s_ash_fz;
}

static void fp_ash_splat_sphere(float wx, float wy, float wz, float radius)
{
  float u, v, depth;
  fp_ash_project(wx, wy, wz, &u, &v, &depth);
  const float extent = FP_ACTOR_SHADOW_EXTENT;
  if (u < -extent - radius || u > extent + radius || v < -extent - radius || v > extent + radius)
    return;
  if (depth < -radius || depth > FP_ACTOR_SHADOW_FAR + radius)
    return;

  const float inv = (float)FP_ACTOR_SHADOW_RES / (2.0f * extent);
  const float cx = (u + extent) * inv;
  const float cy = (v + extent) * inv;
  const float pix_r = radius * inv + 0.75f;
  const int x0 = (int)floorf(cx - pix_r);
  const int x1 = (int)ceilf(cx + pix_r);
  const int y0 = (int)floorf(cy - pix_r);
  const int y1 = (int)ceilf(cy + pix_r);
  const float r2 = pix_r * pix_r;
  const float caster_z = depth - radius * 0.85f;

  for (int y = (y0 < 0 ? 0 : y0); y <= y1 && y < FP_ACTOR_SHADOW_RES; y++)
  {
    for (int x = (x0 < 0 ? 0 : x0); x <= x1 && x < FP_ACTOR_SHADOW_RES; x++)
    {
      const float pdx = (float)x + 0.5f - cx;
      const float pdy = (float)y + 0.5f - cy;
      if (pdx * pdx + pdy * pdy > r2)
        continue;
      const int idx = y * FP_ACTOR_SHADOW_RES + x;
      if (caster_z < s_actor_shadow[idx])
        s_actor_shadow[idx] = caster_z;
    }
  }
}

// Fill the sun-space depth map from actor proxies and flying debris. Origins shift local world
// coordinates into the same space as packed lighting voxel ids.
static void fp_build_actor_shadow_map(const FPCamera *cam, const World *world, int ox, int oy, int oz)
{
  s_actor_shadow_ready = false;
  if (!cam || !s_fp_lighting)
    return;

  fp_ash_basis_from_sun();
  s_ash_cx = cam->x + (float)ox;
  s_ash_cy = cam->y + (float)oy;
  s_ash_cz = cam->z + (float)oz;

  for (int i = 0; i < FP_ACTOR_SHADOW_RES * FP_ACTOR_SHADOW_RES; i++)
    s_actor_shadow[i] = FP_ACTOR_SHADOW_FAR;

  bool any = false;
  if (world && world->runtime_actors)
  {
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
      const Actor *a = &world->runtime_actors[i];
      if (!a->is_active || a->is_controlled)
        continue;
      const float wx = (float)a->x + (float)ox;
      const float wy = (float)a->y + (float)oy;
      const float wz = (float)a->z + (float)oz;
      const float dx = wx - s_ash_cx, dy = wy - s_ash_cy, dz = wz - s_ash_cz;
      if (dx * dx + dy * dy + dz * dz > FP_ACTOR_SHADOW_EXTENT * FP_ACTOR_SHADOW_EXTENT * 2.25f)
        continue;

      float body_r = 0.45f;
      const char *mesh_name = mob_actor_mesh_name(a);
      if (mesh_name && mesh_name[0])
      {
        // Coarse capsule from the baked mesh height so goleling/pigeon cast real silhouettes
        // instead of two tiny spheres.
        const PolyMesh *mesh = poly_mesh_get(mesh_name);
        if (!mesh)
        {
          poly_mesh_init(NULL);
          mesh = poly_mesh_get(mesh_name);
        }
        float h = (mesh && mesh->height > 0.2f) ? mesh->height : 1.6f;
        if (h > 4.0f)
          h = 4.0f;
        const float r = fmaxf(0.32f, h * 0.28f);
        for (float t = r * 0.6f; t <= h - r * 0.4f; t += r * 1.15f)
          fp_ash_splat_sphere(wx, wy, wz + t, r);
        fp_ash_splat_sphere(wx, wy, wz + h * 0.55f, r * 1.2f);
      }
      else if (mob_actor_is_bird(a))
      {
        const BirdKind kind = mob_actor_bird_kind(a);
        body_r = kind == BIRD_KIND_SPARROW ? 0.22f :
                 kind == BIRD_KIND_GULL ? 0.36f : 0.28f;
        fp_ash_splat_sphere(wx, wy, wz, body_r);
        // Wings as a wider, flatter proxy so grounded shadows read as birds, not dots.
        fp_ash_splat_sphere(wx, wy, wz + 0.05f, body_r * 1.6f);
      }
      else
      {
        fp_ash_splat_sphere(wx, wy, wz + 0.15f, body_r);
        fp_ash_splat_sphere(wx, wy, wz + 0.85f, 0.28f);
      }
      any = true;
    }
  }

  if (s_fp_debris)
  {
    for (int i = 0; i < DEBRIS_MAX; i++)
    {
      const DebrisPiece *d = &s_fp_debris->items[i];
      if (!d->active)
        continue;
      const float wx = d->x + (float)ox;
      const float wy = d->y + (float)oy;
      const float wz = d->z + (float)oz;
      fp_ash_splat_sphere(wx, wy, wz, fmaxf(DEBRIS_SIZE * 1.5f, 0.12f));
      any = true;
    }
  }

  if (s_fp_debris_volumes)
  {
    const DebrisVolume *vols[DEBRIS_VOLUME_MAX];
    const int vn = voxel_debris_volume_gather(s_fp_debris_volumes, NULL, vols, DEBRIS_VOLUME_MAX);
    for (int v = 0; v < vn; v++)
    {
      const DebrisVolume *vol = vols[v];
      const int n = vol->cell_count;
      if (n <= 0)
        continue;
      // Budget splats: dense chunks cast a hull proxy instead of one sphere per cell.
      const int stride = (n > FP_DEBRIS_VOL_SHADOW_BUDGET) ? ((n + FP_DEBRIS_VOL_SHADOW_BUDGET - 1) /
                                                              FP_DEBRIS_VOL_SHADOW_BUDGET)
                                                           : 1;
      const float r = (stride > 1) ? fminf(0.85f, 0.45f + 0.08f * (float)stride) : 0.45f;
      for (int i = 0; i < n; i += stride)
      {
        const DebrisVolumeCell *c = &vol->cells[i];
        float wx, wy, wz;
        voxel_debris_volume_cell_world(vol, c, &wx, &wy, &wz);
        fp_ash_splat_sphere(wx + 0.5f + (float)ox, wy + 0.5f + (float)oy, wz + 0.5f + (float)oz, r);
      }
      any = true;
    }
  }

  s_actor_shadow_ready = any;
}

// Bilinear depth lookup in the mover shadow map. Returns FAR when out of bounds / empty.
static float fp_ash_depth_at(float u, float v)
{
  const float extent = FP_ACTOR_SHADOW_EXTENT;
  if (u < -extent || u > extent || v < -extent || v > extent)
    return FP_ACTOR_SHADOW_FAR;
  const float inv = (float)FP_ACTOR_SHADOW_RES / (2.0f * extent);
  const float fx = (u + extent) * inv - 0.5f;
  const float fy = (v + extent) * inv - 0.5f;
  const int x0 = (int)floorf(fx);
  const int y0 = (int)floorf(fy);
  if (x0 < 0 || y0 < 0 || x0 + 1 >= FP_ACTOR_SHADOW_RES || y0 + 1 >= FP_ACTOR_SHADOW_RES)
    return FP_ACTOR_SHADOW_FAR;
  const float tx = fx - (float)x0;
  const float ty = fy - (float)y0;
  const float z00 = s_actor_shadow[y0 * FP_ACTOR_SHADOW_RES + x0];
  const float z10 = s_actor_shadow[y0 * FP_ACTOR_SHADOW_RES + x0 + 1];
  const float z01 = s_actor_shadow[(y0 + 1) * FP_ACTOR_SHADOW_RES + x0];
  const float z11 = s_actor_shadow[(y0 + 1) * FP_ACTOR_SHADOW_RES + x0 + 1];
  const float z0 = z00 + (z10 - z00) * tx;
  const float z1 = z01 + (z11 - z01) * tx;
  return z0 + (z1 - z0) * ty;
}

// Soft occlusion: 1 = fully lit; FP_ACTOR_SHADOW_DARK when fully in mover shadow.
// PCF taps + a depth penumbra band so silhouettes are not hard cutouts.
static float fp_actor_shadow_at(float wx, float wy, float wz)
{
  if (!s_actor_shadow_ready)
    return 1.0f;
  float u, v, depth;
  fp_ash_project(wx, wy, wz, &u, &v, &depth);
  const float extent = FP_ACTOR_SHADOW_EXTENT;
  if (u < -extent || u > extent || v < -extent || v > extent)
    return 1.0f;
  if (depth < 0.0f || depth > FP_ACTOR_SHADOW_FAR)
    return 1.0f;

  const float texel = (2.0f * extent) / (float)FP_ACTOR_SHADOW_RES;
  const float tap = texel * FP_ACTOR_SHADOW_PCF;
  // Centre + 4 diagonals.
  static const float ou[5] = {0.0f, -0.7f, 0.7f, -0.7f, 0.7f};
  static const float ov[5] = {0.0f, -0.7f, -0.7f, 0.7f, 0.7f};
  float occ = 0.0f;
  for (int i = 0; i < 5; i++)
  {
    const float caster = fp_ash_depth_at(u + ou[i] * tap, v + ov[i] * tap);
    if (caster >= FP_ACTOR_SHADOW_FAR - 1e-3f)
      continue;
    const float delta = depth - (caster + FP_ACTOR_SHADOW_BIAS);
    if (delta <= 0.0f)
      continue;
    float t = delta / FP_ACTOR_SHADOW_PENUMBRA;
    if (t > 1.0f)
      t = 1.0f;
    occ += t;
  }
  occ *= 0.2f;
  if (occ <= 0.0f)
    return 1.0f;
  return 1.0f - occ * (1.0f - FP_ACTOR_SHADOW_DARK);
}

// Temporal EMA on the mover-shadow factor so PCF / map rebuild noise does not strobe terrain.
static float fp_actor_shadow_smooth(uint32_t key, float wx, float wy, float wz)
{
  const float fresh = fp_actor_shadow_at(wx, wy, wz);
  const uint8_t fresh_u = (uint8_t)(fresh * 255.0f + 0.5f);
  const uint32_t idx = (key * 0x9e3779b1u) & (FP_LIGHT_CACHE - 1);
  if (s_ash_cache_key[idx] == key)
  {
    const uint8_t blended = fp_temporal_blend(s_ash_cache_val[idx], fresh_u);
    s_ash_cache_val[idx] = blended;
    return (float)blended / 255.0f;
  }
  s_ash_cache_key[idx] = key;
  s_ash_cache_val[idx] = fresh_u;
  return fresh;
}

// Occupancy sun + hemisphere AO, once per unique visible voxel, then multiplied onto albedo.
// Screen-area budget: tiny footprints inherit neighbour light when the unique set is large (half-res
// policy). Albedo-guided bilateral on the lighting scale kills leftover cube-to-cube grain.
static void fp_apply_occupancy_lighting(const FPFrameBuffer *fb, const ShadowWorld *cluster,
                                        const World *world)
{
  if (!fb || !s_fp_lighting || !s_voxel_id || !fb->color)
    return;
  if (!cluster && !world)
    return;

  const uint64_t rev = cluster ? fp_cluster_light_rev(cluster)
                               : (world ? world->voxel_revision : 0);
  // Do not wipe the irradiance cache when the world revises — that discarded all temporal
  // history and flashed every lit surface. Keys refresh in place via EMA as voxels are resampled.
  s_light_cache_rev = rev;
  s_light_frame++;

  const int pixels = fb->stride * fb->h;
  uint32_t unique[FP_LIGHT_UNIQUE_CAP];
  uint8_t unique_lit[FP_LIGHT_UNIQUE_CAP];
  uint16_t unique_hits[FP_LIGHT_UNIQUE_CAP];
  int unique_n = 0;
  uint16_t seen[FP_LIGHT_CACHE];
  memset(seen, 0xFF, sizeof(seen));

  for (int i = 0; i < pixels; i++)
  {
    const int32_t packed = s_voxel_id[i];
    if (packed < 0)
      continue;
    const uint32_t key = (uint32_t)packed;
    const uint32_t slot = (key * 0x9e3779b1u) & (FP_LIGHT_CACHE - 1);
    const uint16_t prior = seen[slot];
    if (prior != 0xFFFFu && unique[prior] == key)
    {
      if (unique_hits[prior] < 0xFFFFu)
        unique_hits[prior]++;
      continue;
    }
    if (unique_n >= FP_LIGHT_UNIQUE_CAP)
      continue; // over budget: later pixels inherit via neighbour blur
    seen[slot] = (uint16_t)unique_n;
    unique[unique_n] = key;
    unique_hits[unique_n] = 1;
    unique_lit[unique_n] = 0;
    unique_n++;
  }

  // Estimate only for voxels that cover enough screen, or while the set is small.
  const bool crowded = unique_n > (FP_LIGHT_UNIQUE_CAP * 2) / 3;
  for (int i = 0; i < unique_n; i++)
  {
    int cx, cy, cz;
    fp_unpack_vox((int32_t)unique[i], &cx, &cy, &cz);
    if (crowded && unique_hits[i] < 3)
    {
      // Reuse last frame's estimate when available instead of a mid-grey placeholder that
      // pops as the unique set changes with camera motion.
      const uint32_t idx = (unique[i] * 0x9e3779b1u) & (FP_LIGHT_CACHE - 1);
      if (s_light_cache_key[idx] == unique[i])
        unique_lit[i] = s_light_cache_val[idx];
      else
        unique_lit[i] = 128;
    }
    else
      unique_lit[i] = fp_cached_light(cluster, world, unique[i], cx, cy, cz);
  }

  for (int i = 0; i < unique_n; i++)
    unique_lit[i] = fp_blur_light(unique[i], unique_lit[i]);

  uint8_t *scales = (uint8_t *)malloc((size_t)pixels);
  if (!scales)
  {
    // Fallback: apply without bilateral.
    for (int i = 0; i < pixels; i++)
    {
      const int32_t packed = s_voxel_id[i];
      if (packed < 0)
        continue;
      const uint32_t key = (uint32_t)packed;
      const uint32_t slot = (key * 0x9e3779b1u) & (FP_LIGHT_CACHE - 1);
      const uint16_t u = seen[slot];
      int cx, cy, cz;
      fp_unpack_vox(packed, &cx, &cy, &cz);
      uint8_t L;
      if (u != 0xFFFFu && unique[u] == key)
        L = unique_lit[u];
      else
        L = fp_blur_light(key, fp_cached_light(cluster, world, key, cx, cy, cz));
      const float scale = (float)L / 255.0f *
                          fp_actor_shadow_smooth(key, (float)cx + 0.5f, (float)cy + 0.5f,
                                                 (float)cz + 0.5f);
      const Uint32 c = fb->color[i];
      uint8_t r = (uint8_t)((float)((c >> 24) & 0xFFu) * scale);
      uint8_t g = (uint8_t)((float)((c >> 16) & 0xFFu) * scale);
      uint8_t b = (uint8_t)((float)((c >> 8) & 0xFFu) * scale);
      if (s_defer_atmo_fog)
      {
        float fog;
        if (fb->depth)
          fog = fp_fog_factor(fb->depth[i]);
        else
        {
          const float dx = (float)(cx - s_light_ox) + 0.5f - s_fog_eye[0];
          const float dy = (float)(cy - s_light_oy) + 0.5f - s_fog_eye[1];
          const float dz = (float)(cz - s_light_oz) + 0.5f - s_fog_eye[2];
          fog = fp_fog_factor(sqrtf(dx * dx + dy * dy + dz * dz));
        }
        fp_mix_fog(fog, &r, &g, &b);
      }
      fb->color[i] = fp_rgba8888(r, g, b);
    }
    return;
  }

  for (int i = 0; i < pixels; i++)
  {
    const int32_t packed = s_voxel_id[i];
    if (packed < 0)
    {
      scales[i] = 255;
      continue;
    }
    const uint32_t key = (uint32_t)packed;
    const uint32_t slot = (key * 0x9e3779b1u) & (FP_LIGHT_CACHE - 1);
    const uint16_t u = seen[slot];
    int cx, cy, cz;
    fp_unpack_vox(packed, &cx, &cy, &cz);
    uint8_t L;
    if (u != 0xFFFFu && unique[u] == key)
      L = unique_lit[u];
    else
      L = fp_blur_light(key, fp_cached_light(cluster, world, key, cx, cy, cz));
    const float ash =
        fp_actor_shadow_smooth(key, (float)cx + 0.5f, (float)cy + 0.5f, (float)cz + 0.5f);
    scales[i] = (uint8_t)(fminf(255.0f, (float)L * ash + 0.5f));
  }

  // Albedo-guided bilateral: average lighting scales across similar albedo + depth neighbours.
  for (int y = 1; y < fb->h - 1; y++)
  {
    for (int x = 1; x < fb->w - 1; x++)
    {
      const int i = y * fb->stride + x;
      if (s_voxel_id[i] < 0)
        continue;
      const Uint32 c0 = fb->color[i];
      const int r0 = (int)((c0 >> 24) & 0xFFu);
      const int g0 = (int)((c0 >> 16) & 0xFFu);
      const int b0 = (int)((c0 >> 8) & 0xFFu);
      const float d0 = fb->depth ? fb->depth[i] : 0.0f;
      int sum = (int)scales[i] * 4;
      int wsum = 4;
      const int offs[4] = {-1, 1, -fb->stride, fb->stride};
      for (int k = 0; k < 4; k++)
      {
        const int j = i + offs[k];
        if (s_voxel_id[j] < 0)
          continue;
        const Uint32 c1 = fb->color[j];
        const int dr = r0 - (int)((c1 >> 24) & 0xFFu);
        const int dg = g0 - (int)((c1 >> 16) & 0xFFu);
        const int db = b0 - (int)((c1 >> 8) & 0xFFu);
        if (dr * dr + dg * dg + db * db > 48 * 48)
          continue;
        if (fb->depth)
        {
          const float dd = fabsf(d0 - fb->depth[j]);
          if (dd > 0.08f * fmaxf(1.0f, d0))
            continue;
        }
        sum += scales[j];
        wsum++;
      }
      scales[i] = (uint8_t)(sum / wsum);
    }
  }

  for (int i = 0; i < pixels; i++)
  {
    if (s_voxel_id[i] < 0)
      continue;
    const float scale = (float)scales[i] / 255.0f;
    const Uint32 c = fb->color[i];
    uint8_t r = (uint8_t)((float)((c >> 24) & 0xFFu) * scale);
    uint8_t g = (uint8_t)((float)((c >> 16) & 0xFFu) * scale);
    uint8_t b = (uint8_t)((float)((c >> 8) & 0xFFu) * scale);
    if (s_defer_atmo_fog)
    {
      float fog;
      if (fb->depth)
        fog = fp_fog_factor(fb->depth[i]);
      else
      {
        int cx, cy, cz;
        fp_unpack_vox(s_voxel_id[i], &cx, &cy, &cz);
        const float dx = (float)(cx - s_light_ox) + 0.5f - s_fog_eye[0];
        const float dy = (float)(cy - s_light_oy) + 0.5f - s_fog_eye[1];
        const float dz = (float)(cz - s_light_oz) + 0.5f - s_fog_eye[2];
        fog = fp_fog_factor(sqrtf(dx * dx + dy * dy + dz * dz));
      }
      fp_mix_fog(fog, &r, &g, &b);
    }
    fb->color[i] = fp_rgba8888(r, g, b);
  }
  free(scales);
}

static void fp_begin_deferred_fog(const FPCamera *cam)
{
  s_defer_atmo_fog = s_fp_lighting && cam != NULL;
  if (cam)
  {
    s_fog_eye[0] = cam->x;
    s_fog_eye[1] = cam->y;
    s_fog_eye[2] = cam->z;
  }
}

void fp_renderer_render_cluster(SDL_Renderer *ren, const ShadowWorld *cluster, const FPCamera *cam,
                               int panel_x, int panel_y, int panel_w, int panel_h,
                               const FogAtlas *fog)
{
  if (!ren || !cluster || !cam || panel_w <= 0 || panel_h <= 0)
    return;

  const World *centre = shadow_world_slot_world(cluster, SHADOW_CENTRE_SLOT);
  if (!centre)
    return;

  const float cam_dist = sqrtf(cam->x * cam->x + cam->y * cam->y + cam->z * cam->z);
  const VoxelMesh *mesh = get_cached_mesh_lod(centre, cam_dist);
  if (!mesh)
  {
    render_raycast_fallback(ren, centre, cam, panel_x, panel_y, panel_w, panel_h, fog, cluster);
    return;
  }

  FPFrameBuffer fb;
  if (!fp_frame_lock(ren, panel_x, panel_y, panel_w, panel_h, &fb))
    return;

  FPCameraBasis basis;
  fp_camera_build_basis(cam, &basis);
  basis.aspect = (fb.h > 0) ? ((float)fb.w / (float)fb.h) : 1.0f;

  shadow_world_slot_origin(cluster, SHADOW_CENTRE_SLOT, &s_light_ox, &s_light_oy, &s_light_oz);
  fp_begin_deferred_fog(cam);

  memset(&s_fp_cluster_profile, 0, sizeof(s_fp_cluster_profile));
  fp_renderer_clear_material_frame_stats();
  const bool profile = fp_profile_enabled();
  double t0 = 0.0, t1 = 0.0;
  if (profile)
    t0 = fp_now_ms();

  fp_raster_mesh(&fb, cam, &basis, centre, mesh, fog, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, true,
                 255, 255, 255);
  {
    const FPSparseCellList *sparse =
        (s_mesh_cache.valid && s_mesh_cache.world == centre) ? &s_mesh_cache.sparse : NULL;
    fp_draw_sparse_material_instances(&fb, cam, &basis, centre, sparse, 0.0f, 0.0f, 0.0f);
  }
  if (profile)
  {
    t1 = fp_now_ms();
    s_fp_cluster_profile.centre_ms = t1 - t0;
    t0 = t1;
  }

  // Soften the centre frame against sky *before* neighbours fill that sky. FXAA after neighbour
  // fill would bleed their colours into the centre silhouette and break the sky-only contract.
  fp_draw_grass_impostors(&fb, cam, centre);
  fp_apply_fxaa(fb.color, fb.stride, fb.w, fb.h);
  s_fp_fxaa_already = true;

  fp_raster_cluster_ring1(&fb, cam, &basis, cluster, fog);
  if (profile)
  {
    t1 = fp_now_ms();
    s_fp_cluster_profile.ring1_ms = t1 - t0;
    t0 = t1;
  }

  fp_raster_cluster_neighbours(&fb, cam, &basis, cluster, fog);
  if (profile)
  {
    t1 = fp_now_ms();
    s_fp_cluster_profile.neighbour_ms = t1 - t0;
    t0 = t1;
  }

  if (s_fp_lighting)
  {
    fp_sync_gl_occupancy(centre);
    fp_build_actor_shadow_map(cam, centre, s_light_ox, s_light_oy, s_light_oz);
    fp_apply_occupancy_lighting(&fb, cluster, NULL);
  }
  if (profile)
  {
    t1 = fp_now_ms();
    s_fp_cluster_profile.lighting_ms = t1 - t0;
    t0 = t1;
  }

  s_defer_atmo_fog = false;
  fp_draw_projectiles(&fb, cam, NULL);
  fp_draw_debris(&fb, cam, NULL);
  fp_draw_debris_volumes(&fb, cam, centre, cluster);
  fp_draw_particles(&fb, cam, centre);
  fp_draw_swing(&fb, cam);
  fp_draw_actors(&fb, cam, &basis, centre, true);
  fp_draw_actor_health_bars(&fb, cam, centre);
  if (profile)
  {
    t1 = fp_now_ms();
    s_fp_cluster_profile.overlay_ms = t1 - t0;
    t0 = t1;
  }

  fp_frame_present(ren, panel_x, panel_y, panel_w, panel_h);
  s_fp_fxaa_already = false;
  if (profile)
  {
    t1 = fp_now_ms();
    s_fp_cluster_profile.present_ms = t1 - t0;
    fprintf(stderr,
            "[fp_profile] centre=%.2f ring1=%.2f (slots=%d quads=%d builds=%d) neigh=%.2f "
            "light=%.2f overlay=%.2f present=%.2f ms\n",
            s_fp_cluster_profile.centre_ms, s_fp_cluster_profile.ring1_ms,
            s_fp_cluster_profile.ring1_slots, s_fp_cluster_profile.ring1_quads,
            s_fp_cluster_profile.ring1_builds, s_fp_cluster_profile.neighbour_ms,
            s_fp_cluster_profile.lighting_ms, s_fp_cluster_profile.overlay_ms,
            s_fp_cluster_profile.present_ms);
  }
  // Amortize LOD builds after the frame is on screen, and only when the frame was fast.
  fp_warm_ring1_meshes(cluster);
}

// GPU-accelerated mesh rendering for ideal_opt test case
static void render_mesh_gpu_ideal_opt(SDL_Renderer *ren, const World *world, const VoxelMesh *mesh,
                                      const FPCamera *cam, int panel_x, int panel_y, int panel_w, int panel_h)
{
  Uint64 start = SDL_GetPerformanceCounter();
  Uint64 freq = SDL_GetPerformanceFrequency();

  // Debug output removed - enable with --debug flag if needed

  // SANITY CHECK: Validate mesh before proceeding
  if (!validate_mesh_for_rendering(mesh, world, "render_mesh_gpu_ideal_opt"))
  {
    FP_DBG("[fp_renderer] ideal_opt: mesh validation failed, using CPU mesh path\n");
    render_mesh_cpu(ren, world, mesh, cam, panel_x, panel_y, panel_w, panel_h, NULL);
    return;
  }

  SDL_Window *window = SDL_RenderGetWindow(ren);
  if (!window)
  {
    FP_DBG("[fp_renderer] ideal_opt: no window, using CPU mesh path\n");
    render_mesh_cpu(ren, world, mesh, cam, panel_x, panel_y, panel_w, panel_h, NULL);
    return;
  }

  SDL_GLContext glctx = SDL_GL_GetCurrentContext();
  if (!glctx)
  {
    FP_DBG("[fp_renderer] ideal_opt: no GL context, using CPU mesh path\n");
    render_mesh_cpu(ren, world, mesh, cam, panel_x, panel_y, panel_w, panel_h, NULL);
    return;
  }

  // Create a texture for off-screen rendering
  GLuint color_texture;
  glGenTextures(1, &color_texture);
  glBindTexture(GL_TEXTURE_2D, color_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, panel_w, panel_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Save current OpenGL state
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();

  // Get window dimensions for coordinate calculations
  int window_w, window_h;
  SDL_GetWindowSize(window, &window_w, &window_h);

  // Set viewport to panel size for PIP rendering (resize FP camera to panel)
  glViewport(0, 0, panel_w, panel_h);

  // Debug: OpenGL viewport info removed

  // Clear to sky color
  glClearColor(0.078f, 0.086f, 0.137f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Set up 3D projection matrix
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  const float fov_rad = cam->fov_deg * (float)M_PI / 180.0f;
  const float aspect = (float)panel_w / (float)panel_h; // Use panel aspect ratio for PIP
  const float near_plane = 0.1f;
  const float far_plane = 1000.0f;

  // Use gluPerspective equivalent
  float f = 1.0f / tanf(fov_rad * 0.5f);
  float matrix[16] = {
      f / aspect, 0.0f, 0.0f, 0.0f,
      0.0f, f, 0.0f, 0.0f,
      0.0f, 0.0f, (far_plane + near_plane) / (near_plane - far_plane), -1.0f,
      0.0f, 0.0f, (2.0f * far_plane * near_plane) / (near_plane - far_plane), 0.0f};
  glLoadMatrixf(matrix);

  // Set up modelview matrix
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  // Camera setup
  const float cy = cosf(cam->yaw);
  const float sy = sinf(cam->yaw);
  const float cp = cosf(cam->pitch);
  const float sp = sinf(cam->pitch);

  float fwd_x = cy * cp;
  float fwd_y = sy * cp;
  float fwd_z = sp;

  // Normalize forward vector
  float len = sqrtf(fwd_x * fwd_x + fwd_y * fwd_y + fwd_z * fwd_z);
  if (len > 0.0f)
  {
    fwd_x /= len;
    fwd_y /= len;
    fwd_z /= len;
  }

  // Build camera basis
  float up_x = 0.0f, up_y = 0.0f, up_z = 1.0f;
  if (fabsf(fwd_z) > 0.98f)
  {
    up_x = 1.0f;
    up_y = 0.0f;
    up_z = 0.0f;
  }

  float right_x, right_y, right_z;
  right_x = up_y * fwd_z - up_z * fwd_y;
  right_y = up_z * fwd_x - up_x * fwd_z;
  right_z = up_x * fwd_y - up_y * fwd_x;

  len = sqrtf(right_x * right_x + right_y * right_y + right_z * right_z);
  if (len > 0.0f)
  {
    right_x /= len;
    right_y /= len;
    right_z /= len;
  }

  up_x = fwd_y * right_z - fwd_z * right_y;
  up_y = fwd_z * right_x - fwd_x * right_z;
  up_z = fwd_x * right_y - fwd_y * right_x;

  len = sqrtf(up_x * up_x + up_y * up_y + up_z * up_z);
  if (len > 0.0f)
  {
    up_x /= len;
    up_y /= len;
    up_z /= len;
  }

  if (fabsf(cam->roll) > 1e-6f)
  {
    const float cr = cosf(cam->roll);
    const float sr = sinf(cam->roll);
    const float rx = right_x, ry = right_y, rz = right_z;
    const float ux = up_x, uy = up_y, uz = up_z;
    right_x = rx * cr - ux * sr;
    right_y = ry * cr - uy * sr;
    right_z = rz * cr - uz * sr;
    up_x = rx * sr + ux * cr;
    up_y = ry * sr + uy * cr;
    up_z = rz * sr + uz * cr;
  }

  // Create view matrix
  float view_matrix[16] = {
      right_x, up_x, -fwd_x, 0.0f,
      right_y, up_y, -fwd_y, 0.0f,
      right_z, up_z, -fwd_z, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f};
  glMultMatrixf(view_matrix);
  glTranslatef(-cam->x, -cam->y, -cam->z);

  // Scale world coordinates for first-person view
  // World is 128x128x128, use 1:1 scale for proper first-person perspective
  const float world_scale = 1.0f; // No scaling - world appears at normal size
  glScalef(world_scale, world_scale, world_scale);

  // Enable depth testing for proper 3D ordering
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  // Disable lighting for raw colors
  glDisable(GL_LIGHTING);
  glDisable(GL_LIGHT0);
  glDisable(GL_COLOR_MATERIAL);

  // Enable blending for better color handling
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glDisable(GL_CULL_FACE);

  glBegin(GL_TRIANGLES);

  for (int i = 0; i < mesh->count; i++)
  {
    const VoxelFaceQuad *quad = &mesh->quads[i];

    float corners[4][3];
    fp_quad_world_corners(quad, corners);

    const float center_x = (corners[0][0] + corners[2][0]) * 0.5f;
    const float center_y = (corners[0][1] + corners[2][1]) * 0.5f;
    const float center_z = (corners[0][2] + corners[2][2]) * 0.5f;
    const float cull_dx = center_x - cam->x;
    const float cull_dy = center_y - cam->y;
    const float cull_dz = center_z - cam->z;
    const float distance_sq = cull_dx * cull_dx + cull_dy * cull_dy + cull_dz * cull_dz;
    if (distance_sq > FP_VIEW_DISTANCE * FP_VIEW_DISTANCE)
      continue;

    float nx, ny, nz;
    fp_face_normal(quad->face, &nx, &ny, &nz);
    if (nx * cull_dx + ny * cull_dy + nz * cull_dz > 0.0f)
      continue;

    uint8_t r = quad->color.r, g = quad->color.g, b = quad->color.b;
    const float shade = fp_face_shade(quad->face);
    r = (uint8_t)((float)r * shade);
    g = (uint8_t)((float)g * shade);
    b = (uint8_t)((float)b * shade);
    fp_apply_fog(sqrtf(distance_sq), &r, &g, &b);
    glColor3f((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f);

    glVertex3f(corners[0][0], corners[0][1], corners[0][2]);
    glVertex3f(corners[1][0], corners[1][1], corners[1][2]);
    glVertex3f(corners[2][0], corners[2][1], corners[2][2]);
    glVertex3f(corners[0][0], corners[0][1], corners[0][2]);
    glVertex3f(corners[2][0], corners[2][1], corners[2][2]);
    glVertex3f(corners[3][0], corners[3][1], corners[3][2]);
  }

  // Close the batch rendering
  glEnd();

  GLenum error = glGetError();
  if (error != GL_NO_ERROR)
    FP_DBG("[fp_renderer] ideal_opt: OpenGL error: 0x%x\n", error);

  unsigned char *pixels = malloc((size_t)panel_w * (size_t)panel_h * 4);
  glReadPixels(0, 0, panel_w, panel_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

  unsigned char *flipped_pixels = malloc((size_t)panel_w * (size_t)panel_h * 4);
  for (int y = 0; y < panel_h; y++) {
    int src_y = panel_h - 1 - y;  // Flip Y coordinate
    for (int x = 0; x < panel_w; x++) {
      int src_idx = (src_y * panel_w + x) * 4;
      int dst_idx = (y * panel_w + x) * 4;
      flipped_pixels[dst_idx + 0] = pixels[src_idx + 0];  // R
      flipped_pixels[dst_idx + 1] = pixels[src_idx + 1];  // G
      flipped_pixels[dst_idx + 2] = pixels[src_idx + 2];  // B
      flipped_pixels[dst_idx + 3] = pixels[src_idx + 3];  // A
    }
  }

  // Create SDL2 surface from flipped pixels
  SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(flipped_pixels, panel_w, panel_h, 32,
                                                  panel_w * 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

  if (surface)
  {
    // Create SDL2 texture from surface
    SDL_Texture *texture = SDL_CreateTextureFromSurface(ren, surface);

    if (texture)
    {
      // Render the texture to the SDL2 renderer at the panel position
      SDL_Rect dst_rect = {panel_x, panel_y, panel_w, panel_h};
      SDL_RenderCopy(ren, texture, NULL, &dst_rect);
      SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
  }

  free(flipped_pixels);
  free(pixels);

  // Restore OpenGL state
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glPopAttrib();

  // Clean up texture
  glDeleteTextures(1, &color_texture);

  Uint64 end = SDL_GetPerformanceCounter();
  FP_DBG("[fp_renderer] gpu mesh: %d quads in %.2fms\n", mesh->count,
         (double)(end - start) * 1000.0 / (double)freq);

  // Dumping the mesh to assets/ is a debugging aid, not something a normal run should do.
  static bool mesh_exported = false;
  if (fp_debug_enabled() && !mesh_exported)
  {
    const char *skip_export = getenv("UNIVERSE_REF_SKIP_MESH_EXPORT");
    if (!(skip_export && strcmp(skip_export, "1") == 0))
      voxel_mesh_export_obj(mesh, "assets/voxel_mesh_debug.obj");
    mesh_exported = true;
  }
}

// Raycast fallback rendering (optimized DDA; no per-pixel normalization).
// When lighting is on, voxel ids are recorded and occupancy sun+AO is applied — same estimator
// as the mesh path. `cluster` is preferred when the caller has one; otherwise a single-world
// occupancy walk is used.
static void render_raycast_fallback(SDL_Renderer *ren, const World *world, const FPCamera *cam,
                                    int panel_x, int panel_y, int panel_w, int panel_h,
                                    const FogAtlas *fog, const ShadowWorld *cluster)
{
  const bool opaque_bg = (panel_w > 600 && panel_h > 400);
  SDL_SetRenderDrawBlendMode(ren, opaque_bg ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren, FP_SKY_R, FP_SKY_G, FP_SKY_B, opaque_bg ? 255 : 160);
  SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
  SDL_RenderFillRect(ren, &panel);

  FPCameraBasis basis;
  fp_camera_build_basis(cam, &basis);

  int target_w = 0, target_h = 0;
  fp_compute_target_size(panel_w, panel_h, &target_w, &target_h, NULL);
  if (!fp_ensure_streaming_texture(ren, target_w, target_h))
    return;

  void *pixels = NULL;
  int pitch = 0;
  if (SDL_LockTexture(s_fp_tex, NULL, &pixels, &pitch) != 0 || !pixels)
    return;

  const int stride = pitch / (int)sizeof(Uint32);
  const int pixel_count = stride * target_h;
  if (s_fp_lighting)
  {
    if (s_voxel_id_cap < pixel_count)
    {
      int32_t *nb = (int32_t *)realloc(s_voxel_id, (size_t)pixel_count * sizeof(int32_t));
      if (nb)
      {
        s_voxel_id = nb;
        s_voxel_id_cap = pixel_count;
      }
    }
    if (s_voxel_id)
      for (int i = 0; i < pixel_count; i++)
        s_voxel_id[i] = -1;
    s_light_ox = s_light_oy = s_light_oz = 0;
    if (cluster)
      shadow_world_slot_origin(cluster, SHADOW_CENTRE_SLOT, &s_light_ox, &s_light_oy, &s_light_oz);
  }
  fp_begin_deferred_fog(cam);

  basis.aspect = (target_h > 0) ? ((float)target_w / (float)target_h) : 1.0f;
  const float ax = basis.half_tan * basis.aspect;
  const float ay = basis.half_tan;
  const float origin_eps = 0.01f;
  const int max_steps = (int)(world->width + world->height + world->depth) + 16;
  const Uint32 sky = fp_rgba8888(FP_SKY_R, FP_SKY_G, FP_SKY_B);
  const bool textured = fp_subvoxel_enabled();

  for (int py = 0; py < target_h; py++)
  {
    Uint32 *row32 = (Uint32 *)((uint8_t *)pixels + py * pitch);
    const float ny = 1.0f - (2.0f * ((float)py + 0.5f) / (float)target_h);
    const float row_rx = basis.fwd_x + ny * ay * basis.up_x;
    const float row_ry = basis.fwd_y + ny * ay * basis.up_y;
    const float row_rz = basis.fwd_z + ny * ay * basis.up_z;

    for (int px = 0; px < target_w; px++)
    {
      const float nx = (2.0f * ((float)px + 0.5f) / (float)target_w) - 1.0f;
      const float rx = row_rx + nx * ax * basis.right_x;
      const float ry = row_ry + nx * ax * basis.right_y;
      const float rz = row_rz + nx * ax * basis.right_z;

      int hx = -1, hy = -1, hz = -1;
      const float ox = cam->x + rx * origin_eps;
      const float oy = cam->y + ry * origin_eps;
      const float oz = cam->z + rz * origin_eps;
      const bool hit = world_raycast_first_hit(world, ox, oy, oz, rx, ry, rz, max_steps, &hx, &hy, &hz);

      if (!hit || hx < 0)
      {
        row32[px] = sky;
        continue;
      }

      const Voxel *v = world_voxel_cptr_fast(world, hx, hy, hz);
      uint8_t r, g, b;
      if (fog && hx >= 0 && hy >= 0 && hz >= 0 &&
          !fog_is_explored(fog, world, (uint32_t)hx, (uint32_t)hy, (uint32_t)hz))
      {
        fp_shade_unexplored(&r, &g, &b);
      }
      else
      {
        fp_shade_ray_hit(v, textured, ox, oy, oz, rx, ry, rz, hx, hy, hz, &r, &g, &b);

        const float dx = ((float)hx + 0.5f) - cam->x;
        const float dy = ((float)hy + 0.5f) - cam->y;
        const float dz = ((float)hz + 0.5f) - cam->z;
        const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (!s_fp_lighting)
          fp_apply_fog(dist, &r, &g, &b);
      }
      row32[px] = fp_rgba8888(r, g, b);
      if (s_fp_lighting && s_voxel_id &&
          !(fog && hx >= 0 && hy >= 0 && hz >= 0 &&
            !fog_is_explored(fog, world, (uint32_t)hx, (uint32_t)hy, (uint32_t)hz)))
        s_voxel_id[py * stride + px] =
            fp_pack_vox(hx + s_light_ox, hy + s_light_oy, hz + s_light_oz);
    }
  }

  const FPFrameBuffer ray_fb = {.color = (Uint32 *)pixels,
                                .depth = NULL,
                                .stride = stride,
                                .w = target_w,
                                .h = target_h};
  if (s_fp_lighting)
  {
    fp_sync_gl_occupancy(world);
    fp_build_actor_shadow_map(cam, world, s_light_ox, s_light_oy, s_light_oz);
    fp_apply_occupancy_lighting(&ray_fb, cluster, cluster ? NULL : world);
  }
  s_defer_atmo_fog = false;
  fp_draw_projectiles(&ray_fb, cam, world);
  fp_draw_debris(&ray_fb, cam, world);
  fp_draw_debris_volumes(&ray_fb, cam, world, cluster);
  fp_draw_particles(&ray_fb, cam, world);
  fp_draw_swing(&ray_fb, cam);
  fp_draw_actors(&ray_fb, cam, NULL, world, false);
  fp_draw_actor_health_bars(&ray_fb, cam, world);

  SDL_UnlockTexture(s_fp_tex);
  SDL_Rect dst = {panel_x, panel_y, panel_w, panel_h};
  SDL_RenderCopy(ren, s_fp_tex, NULL, &dst);
}

// Neighbor rendering function
void fp_renderer_render_neighbors(SDL_Renderer *ren,
                                  const GameWorlds *game_worlds,
                                  const FPCamera *cam,
                                  int panel_x, int panel_y, int panel_w, int panel_h,
                                  int neighbor_radius,
                                  const FogAtlas *fog)
{
  if (!ren || !game_worlds || !game_worlds->home_world || !cam || panel_w <= 0 || panel_h <= 0)
    return;

  // If optimized mesh mode and only center world requested, delegate to single-world renderer
  if (s_fp_mode == FP_MODE_MESH && neighbor_radius <= 0)
  {
    fp_renderer_render(ren, game_worlds->home_world, cam, panel_x, panel_y, panel_w, panel_h, fog);
    return;
  }

  // Background panel
  const bool opaque_bg = (panel_w > 600 && panel_h > 400);
  SDL_SetRenderDrawBlendMode(ren, opaque_bg ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren, FP_SKY_R, FP_SKY_G, FP_SKY_B, opaque_bg ? 255 : 160);
  SDL_Rect panel = {panel_x, panel_y, panel_w, panel_h};
  SDL_RenderFillRect(ren, &panel);

  FPCameraBasis basis;
  fp_camera_build_basis(cam, &basis);

  // Build array of included worlds with their offsets
  const World *worlds[125];
  int offsets[125][3];
  int wc = 0;

  // Center world first
  worlds[wc] = game_worlds->home_world;
  offsets[wc][0] = 0;
  offsets[wc][1] = 0;
  offsets[wc][2] = 0;
  wc++;

  // Add adjacent worlds
  for (int i = 0; i < 26 && wc < 125; i++)
  {
    const World *w = game_worlds->adjacent_home_worlds[i];
    if (!w)
      continue;

    // Map i -> dx,dy,dz as in isometric renderer's 3x3x3 neighborhood (-1..1)
    int dz = (i / 9) - 1;
    int rem = i % 9;
    int dy = (rem / 3) - 1;
    int dx = (rem % 3) - 1;

    if (!in_radius_cube(dx, dy, dz, neighbor_radius))
      continue;

    worlds[wc] = w;
    offsets[wc][0] = dx;
    offsets[wc][1] = dy;
    offsets[wc][2] = dz;
    wc++;
  }

  int target_w = 0, target_h = 0;
  fp_compute_target_size(panel_w, panel_h, &target_w, &target_h, NULL);
  if (!fp_ensure_streaming_texture(ren, target_w, target_h))
    return;

  void *pixels = NULL;
  int pitch = 0;
  if (SDL_LockTexture(s_fp_tex, NULL, &pixels, &pitch) != 0 || !pixels)
    return;

  basis.aspect = (target_h > 0) ? ((float)target_w / (float)target_h) : 1.0f;
  const float ax = basis.half_tan * basis.aspect;
  const float ay = basis.half_tan;
  const float origin_eps = 0.01f;
  const Uint32 sky = fp_rgba8888(FP_SKY_R, FP_SKY_G, FP_SKY_B);
  const bool textured = fp_subvoxel_enabled();

  for (int py = 0; py < target_h; py++)
  {
    Uint32 *row32 = (Uint32 *)((uint8_t *)pixels + py * pitch);
    const float ny = 1.0f - (2.0f * ((float)py + 0.5f) / (float)target_h);
    const float row_rx = basis.fwd_x + ny * ay * basis.up_x;
    const float row_ry = basis.fwd_y + ny * ay * basis.up_y;
    const float row_rz = basis.fwd_z + ny * ay * basis.up_z;

    for (int px = 0; px < target_w; px++)
    {
      const float nx = (2.0f * ((float)px + 0.5f) / (float)target_w) - 1.0f;
      const float rx = row_rx + nx * ax * basis.right_x;
      const float ry = row_ry + nx * ax * basis.right_y;
      const float rz = row_rz + nx * ax * basis.right_z;

      float best_t = 1e30f;
      int hx_best = -1, hy_best = -1, hz_best = -1;
      const World *w_best = NULL;
      int off_best[3] = {0, 0, 0};
      // The winning world's local ray origin, kept so the face the ray came in through can be
      // recovered in the same space the hit was reported in.
      float lo_best[3] = {0.0f, 0.0f, 0.0f};

      for (int wi = 0; wi < wc; wi++)
      {
        const World *w = worlds[wi];
        const int ox = offsets[wi][0] * (int)w->width;
        const int oy = offsets[wi][1] * (int)w->height;
        const int oz = offsets[wi][2] * (int)w->depth;

        const float lox = (cam->x + rx * origin_eps) - (float)ox;
        const float loy = (cam->y + ry * origin_eps) - (float)oy;
        const float loz = (cam->z + rz * origin_eps) - (float)oz;

        Octree *octree = NULL;
        if (s_octree_cache.valid && s_octree_cache.world == w)
          octree = s_octree_cache.octree;
        else if (s_octree_cache.valid &&
                 s_octree_cache.world_hash == world_content_token(w))
          octree = s_octree_cache.octree;

        if (octree)
        {
          const float ray_origin[3] = {lox, loy, loz};
          const float ray_direction[3] = {rx, ry, rz};
          if (octree_ray_spatial_cull(octree, ray_origin, ray_direction, 50.0f))
            continue;
        }

        int hx = -1, hy = -1, hz = -1;
        const int max_steps = (int)(w->width + w->height + w->depth) + 16;
        if (!world_raycast_first_hit(w, lox, loy, loz, rx, ry, rz, max_steps, &hx, &hy, &hz))
          continue;

        const float cx = ((float)hx + 0.5f) + (float)ox - cam->x;
        const float cy = ((float)hy + 0.5f) + (float)oy - cam->y;
        const float cz = ((float)hz + 0.5f) + (float)oz - cam->z;
        const float t = cx * rx + cy * ry + cz * rz;

        if (t > 0.0f && t < best_t)
        {
          const Voxel *vv = world_voxel_cptr_fast(w, hx, hy, hz);
          if (vv && vv->type == VOXEL_ACTOR)
            continue;

          best_t = t;
          hx_best = hx;
          hy_best = hy;
          hz_best = hz;
          w_best = w;
          off_best[0] = ox;
          off_best[1] = oy;
          off_best[2] = oz;
          lo_best[0] = lox;
          lo_best[1] = loy;
          lo_best[2] = loz;
        }
      }

      if (!w_best || hx_best < 0)
      {
        row32[px] = sky;
        continue;
      }

      const Voxel *v = world_voxel_cptr_fast(w_best, hx_best, hy_best, hz_best);
      uint8_t r, g, b;
      if (fog && hx_best >= 0 && hy_best >= 0 && hz_best >= 0 &&
          !fog_is_explored(fog, w_best, (uint32_t)hx_best, (uint32_t)hy_best, (uint32_t)hz_best))
      {
        fp_shade_unexplored(&r, &g, &b);
      }
      else
      {
        fp_shade_ray_hit(v, textured, lo_best[0], lo_best[1], lo_best[2], rx, ry, rz, hx_best,
                         hy_best, hz_best, &r, &g, &b);

        const float dx = ((float)hx_best + 0.5f) + (float)off_best[0] - cam->x;
        const float dy = ((float)hy_best + 0.5f) + (float)off_best[1] - cam->y;
        const float dz = ((float)hz_best + 0.5f) + (float)off_best[2] - cam->z;
        const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        fp_apply_fog(dist, &r, &g, &b);
      }
      row32[px] = fp_rgba8888(r, g, b);
    }
  }

  // This is the path FP_MODE_RAY actually takes — fp_renderer_render forwards to it with a radius of
  // zero rather than using render_raycast_fallback — so the overlay has to be here too, or fireballs
  // are invisible in ray mode. Occlusion is tested against the centre world; a projectile is in the
  // world the player is standing in.
  {
    const FPFrameBuffer ray_fb = {.color = (Uint32 *)pixels,
                                  .depth = NULL,
                                  .stride = pitch / (int)sizeof(Uint32),
                                  .w = target_w,
                                  .h = target_h};
    fp_draw_projectiles(&ray_fb, cam, game_worlds->home_world);
    fp_draw_debris(&ray_fb, cam, game_worlds->home_world);
    fp_draw_debris_volumes(&ray_fb, cam, game_worlds->home_world, NULL);
    fp_draw_particles(&ray_fb, cam, game_worlds->home_world);
    fp_draw_swing(&ray_fb, cam);
    fp_draw_actors(&ray_fb, cam, NULL, game_worlds->home_world, false);
    fp_draw_actor_health_bars(&ray_fb, cam, game_worlds->home_world);
  }

  SDL_UnlockTexture(s_fp_tex);
  SDL_Rect dst = {panel_x, panel_y, panel_w, panel_h};
  SDL_RenderCopy(ren, s_fp_tex, NULL, &dst);
}

// Helper function for neighbor rendering
static inline bool in_radius_cube(int dx, int dy, int dz, int r)
{
  if (r <= 0)
    return (dx == 0 && dy == 0 && dz == 0);
  int md = dx < 0 ? -dx : dx;
  if ((dy < 0 ? -dy : dy) > md)
    md = (dy < 0 ? -dy : dy);
  if ((dz < 0 ? -dz : dz) > md)
    md = (dz < 0 ? -dz : dz);
  return md <= r;
}
