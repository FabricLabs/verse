#include "isometric_renderer.h"
void isometric_renderer_draw_box_wireframe_centered(IsometricRenderer* renderer,
                                                    SDL_Renderer* sdl_renderer,
                                                    int x0, int y0, int z0,
                                                    int x1, int y1, int z1,
                                                    SDL_Color color,
                                                    int screen_center_x,
                                                    int screen_center_y,
                                                    bool thick)
{
  if (!renderer || !sdl_renderer)
    return;
  // Save/override screen center for viewport-local drawing if needed
  int old_cx = renderer->screen_center_x;
  int old_cy = renderer->screen_center_y;
  if (screen_center_x >= 0) renderer->screen_center_x = screen_center_x;
  if (screen_center_y >= 0) renderer->screen_center_y = screen_center_y;

  SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);

  // Use renderer's configured screen center (already computed earlier in render)
  int saved_cx = renderer->screen_center_x;
  int saved_cy = renderer->screen_center_y;

  // Project 8 edge-aligned corners using isometric_world_to_screen
  int sx[8], sy[8];
  // Order: (x0,y0,z0)->0, (x1,y0,z0)->1, (x1,y1,z0)->2, (x0,y1,z0)->3, (x0,y0,z1)->4, (x1,y0,z1)->5, (x1,y1,z1)->6, (x0,y1,z1)->7
  // Use world_index 0 for overlays (matching main view origin)
  isometric_world_to_screen(renderer, x0, y0, z0, 0, &sx[0], &sy[0]);
  isometric_world_to_screen(renderer, x1, y0, z0, 0, &sx[1], &sy[1]);
  isometric_world_to_screen(renderer, x1, y1, z0, 0, &sx[2], &sy[2]);
  isometric_world_to_screen(renderer, x0, y1, z0, 0, &sx[3], &sy[3]);
  isometric_world_to_screen(renderer, x0, y0, z1, 0, &sx[4], &sy[4]);
  isometric_world_to_screen(renderer, x1, y0, z1, 0, &sx[5], &sy[5]);
  isometric_world_to_screen(renderer, x1, y1, z1, 0, &sx[6], &sy[6]);
  isometric_world_to_screen(renderer, x0, y1, z1, 0, &sx[7], &sy[7]);

  // Projection matches the GPU lattice; no extra screen-space fudge.

  // Draw bottom rectangle edges (0-1-2-3-0)
  SDL_RenderDrawLine(sdl_renderer, sx[0], sy[0], sx[1], sy[1]);
  SDL_RenderDrawLine(sdl_renderer, sx[1], sy[1], sx[2], sy[2]);
  SDL_RenderDrawLine(sdl_renderer, sx[2], sy[2], sx[3], sy[3]);
  SDL_RenderDrawLine(sdl_renderer, sx[3], sy[3], sx[0], sy[0]);
  // Top rectangle (4-5-6-7-4)
  SDL_RenderDrawLine(sdl_renderer, sx[4], sy[4], sx[5], sy[5]);
  SDL_RenderDrawLine(sdl_renderer, sx[5], sy[5], sx[6], sy[6]);
  SDL_RenderDrawLine(sdl_renderer, sx[6], sy[6], sx[7], sy[7]);
  SDL_RenderDrawLine(sdl_renderer, sx[7], sy[7], sx[4], sy[4]);
  // Verticals
  SDL_RenderDrawLine(sdl_renderer, sx[0], sy[0], sx[4], sy[4]);
  SDL_RenderDrawLine(sdl_renderer, sx[1], sy[1], sx[5], sy[5]);
  SDL_RenderDrawLine(sdl_renderer, sx[2], sy[2], sx[6], sy[6]);
  SDL_RenderDrawLine(sdl_renderer, sx[3], sy[3], sx[7], sy[7]);
  if (thick) {
    for (int i = 0; i < 8; i++) { sx[i]++; sy[i]++; }
    SDL_RenderDrawLine(sdl_renderer, sx[0], sy[0], sx[1], sy[1]);
    SDL_RenderDrawLine(sdl_renderer, sx[1], sy[1], sx[2], sy[2]);
    SDL_RenderDrawLine(sdl_renderer, sx[2], sy[2], sx[3], sy[3]);
    SDL_RenderDrawLine(sdl_renderer, sx[3], sy[3], sx[0], sy[0]);
    SDL_RenderDrawLine(sdl_renderer, sx[4], sy[4], sx[5], sy[5]);
    SDL_RenderDrawLine(sdl_renderer, sx[5], sy[5], sx[6], sy[6]);
    SDL_RenderDrawLine(sdl_renderer, sx[6], sy[6], sx[7], sy[7]);
    SDL_RenderDrawLine(sdl_renderer, sx[7], sy[7], sx[4], sy[4]);
    SDL_RenderDrawLine(sdl_renderer, sx[0], sy[0], sx[4], sy[4]);
    SDL_RenderDrawLine(sdl_renderer, sx[1], sy[1], sx[5], sy[5]);
    SDL_RenderDrawLine(sdl_renderer, sx[2], sy[2], sx[6], sy[6]);
    SDL_RenderDrawLine(sdl_renderer, sx[3], sy[3], sx[7], sy[7]);
  }

  // Restore screen center if overridden
  if (screen_center_x >= 0) renderer->screen_center_x = old_cx;
  if (screen_center_y >= 0) renderer->screen_center_y = old_cy;
  // Restore original centers
  renderer->screen_center_x = saved_cx;
  renderer->screen_center_y = saved_cy;
}
#include "constants.h"
#include "game_state.h"
#include "fog_of_war.h"
// Editor-local actor overlay (externs provided by world_editor.c)
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <dirent.h>
#include <ctype.h>
#include <SDL2/SDL.h>
#include "actor.h"
#include "mob_ai.h"
#include "spirit_sprite.h"
#include "particle_effects.h"
#include "gpu_voxel_buffer.h"
#include "material_atlas.h"
#include "fluid_surface.h"
#include "fog_volume.h"
#include "mob_models.h"
#include "mob_sprite.h"
#include "poly_mesh.h"
#include "voxel_render_common.h"
#include "isometric_renderer.h"
#include "constants.h"
#include "voxel_combat.h"
#include "debris.h"
#include "item_icon.h"
#include "player_controls_types.h"

static inline bool iso_neighbor_offset_visible(IsometricRenderer *renderer, const WorldOffset *o)
{
  if (!renderer || !o)
    return false;
  if (renderer->hide_wilderness_below_home)
  {
    int64_t target_uz = (int64_t)renderer->player_universe_z + o->dz;
    if (target_uz == 0)
      return false;
  }
  return true;
}

// Horizontal neighbors honor inclusion radius. Clouds one step above/below, and wilderness/town
// ground below the player (including two layers down from home, which is outside a radius-1 cube),
// are always kept as fully rendered scenery so distance terrain is visible from the sky island.
static inline bool iso_edge_world_included(IsometricRenderer *renderer, const WorldOffset *o,
                                           const World *ew)
{
  if (!renderer || !o || !ew)
    return false;
  int horiz = abs(o->dx);
  if (abs(o->dy) > horiz)
    horiz = abs(o->dy);
  if (horiz > renderer->neighbor_inclusion_radius)
    return false;
  const bool vertical_cloud = (ew->generation_type == WORLD_TYPE_CLOUD) && abs(o->dz) == 1;
  const bool ground_below =
      ((ew->generation_type == WORLD_TYPE_WILDERNESS ||
        ew->generation_type == WORLD_TYPE_WFC_TOWN) &&
       o->dz < 0);
  if (vertical_cloud || ground_below)
    return iso_neighbor_offset_visible(renderer, o);
  int chebyshev = horiz;
  if (abs(o->dz) > chebyshev)
    chebyshev = abs(o->dz);
  if (chebyshev > renderer->neighbor_inclusion_radius)
    return false;
  return iso_neighbor_offset_visible(renderer, o);
}

static void particle_effects_render_isometric(World *world,
                                              IsometricRenderer *renderer,
                                              SDL_Renderer *sdl_renderer,
                                              int world_index);

static void projectiles_render_isometric(IsometricRenderer *renderer, SDL_Renderer *sdl_renderer,
                                         int world_index);
static void debris_render_isometric(IsometricRenderer *renderer, SDL_Renderer *sdl_renderer,
                                    int world_index);
static void actors_render_isometric(IsometricRenderer *renderer, SDL_Renderer *sdl_renderer,
                                    int world_index);

static void fluid_surface_absorb_all_splashes(FluidSurface *surf, World *world)
{
  if (!surf || !world)
    return;

  FluidSplash batch[64];
  for (;;)
  {
    const int got = fluid_sim_drain_splashes(world, batch, 64);
    if (got <= 0)
      break;
    for (int i = 0; i < got; i++)
      fluid_surface_splash(surf, world, &batch[i]);
    if (got < 64)
      break;
  }
  for (;;)
  {
    const int got = particle_effects_drain_surface_splashes(world, batch, 64);
    if (got <= 0)
      break;
    for (int i = 0; i < got; i++)
      fluid_surface_splash(surf, world, &batch[i]);
    if (got < 64)
      break;
  }
}

// Solid test straight off the occupancy bitfield, taking the bitfield's pointer and dimensions as
// arguments so a caller in a hot loop can hoist them once instead of re-reading them through three
// pointer hops for every neighbour it probes.
static inline uint8_t occ_solid_at(const uint8_t *bits, uint32_t W, uint32_t H, uint32_t D,
                                   int x, int y, int z)
{
  if (x < 0 || y < 0 || z < 0 || (uint32_t)x >= W || (uint32_t)y >= H || (uint32_t)z >= D)
    return 0u;
  const uint32_t lin = ((uint32_t)z * H + (uint32_t)y) * W + (uint32_t)x;
  return (bits[lin >> 3u] >> (lin & 7u)) & 1u;
}

// Face exposed for drawing? Empty bit → yes. Occupied bit → only if that neighbour is a
// transparent type (foliage, water, …); otherwise the bitfield would hide ground under tall grass.
static inline bool iso_face_exposed_occ(const World *world, const uint8_t *bits, uint32_t W,
                                        uint32_t H, uint32_t D, int x, int y, int z)
{
  if (!occ_solid_at(bits, W, H, D, x, y, z))
    return true;
  if (!world)
    return false;
  const Voxel *adj = world_voxel_cptr_fast(world, x, y, z);
  return adj && voxel_is_transparent_type(adj->type);
}

// Fast occupancy bit read from world's optional bitfield
static inline uint8_t world_occupancy_bit(const World *world, int x, int y, int z)
{
  if (!world || !world->occupancy_bits || !world->occupancy_bits->bits)
    return 0u;
  if (x < 0 || y < 0 || z < 0 || x >= (int)world->width || y >= (int)world->height || z >= (int)world->depth)
    return 0u;
  const uint32_t W = world->occupancy_bits->width;
  const uint32_t H = world->occupancy_bits->height;
  const uint8_t *bits = world->occupancy_bits->bits;
  uint32_t lin = ((uint32_t)z * H + (uint32_t)y) * W + (uint32_t)x;
  return (bits[lin >> 3u] >> (lin & 7u)) & 1u;
}

// Provide weak actor globals so non-editor builds link; editor provides strong defs
#ifdef __APPLE__
__attribute__((weak_import)) Actor g_actors[1];
__attribute__((weak_import)) int g_actor_count;
#else
__attribute__((weak)) Actor g_actors[1];
__attribute__((weak)) int g_actor_count;
#endif

// Initialize world offsets for a 5x5x5 cube around center (dx,dy,dz in [-2..2])
static void init_world_offsets(IsometricRenderer *renderer)
{
  int index = 0;
  // Ensure index 0 is the center (0,0,0)
  renderer->world_offsets[index].dx = 0;
  renderer->world_offsets[index].dy = 0;
  renderer->world_offsets[index].dz = 0;
  index++;

  for (int dy = -2; dy <= 2; dy++)
  {
    for (int dz = -2; dz <= 2; dz++)
    {
      for (int dx = -2; dx <= 2; dx++)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        renderer->world_offsets[index].dx = dx;
        renderer->world_offsets[index].dy = dy;
        renderer->world_offsets[index].dz = dz;
        index++;
      }
    }
  }
}

// Create isometric renderer
IsometricRenderer *isometric_renderer_create(int screen_width, int screen_height)
{
  IsometricRenderer *renderer = calloc(1, sizeof(IsometricRenderer));
  if (!renderer)
    return NULL;

  // Set screen dimensions
  renderer->screen_width = screen_width;
  renderer->screen_height = screen_height;
  renderer->screen_center_x = screen_width / 2;
  renderer->screen_center_y = screen_height / 2;

  // Sub-voxel detail on by default; VERSE_SUBVOXEL=0 draws flat faces instead, which is how the
  // two can be compared without a rebuild.
  {
    const char *env = getenv("VERSE_SUBVOXEL");
    renderer->subvoxel_detail_enabled = !(env && env[0] == '0');
  }

  // How many ripples can be in flight at once — not how much water can be in view. A calm surface
  // has no patch, so a lake costs nothing here; a patch appears where something disturbs the water
  // and goes away when it stops moving. Set a little above the atlas's live-tile count so a wave
  // spreading past what can be drawn in one frame carries on being simulated.
  renderer->water_surface = fluid_surface_create(192);

  // Fog volumes are heavier (32³ vs 32²), so the cache is smaller: enough for a body moving through
  // a cloud cluster and the wake it leaves behind, not for every steam voxel in a cloud world.
  renderer->fog_volume = fog_volume_create(48);

  // Set isometric tile dimensions
  renderer->tile_width = 32;      // Width of isometric diamond
  renderer->tile_height = 16;     // Height of isometric diamond
  renderer->voxel_height = 16;    // Vertical height of voxel
  renderer->render_distance = 1000; // Maxed out for full world visibility
  renderer->zoom_scale = 1.0f;    // Default 1x zoom
  renderer->disable_culling = false; // Default: culling enabled

  // Start modest and grow — a 1M-entry calloc was ~44MB of zeroed pages on every boot and
  // stalled time-to-title; menus and typical play emit well under 128k, and the emit path
  // already realloc-doubles when capacity is exceeded.
  renderer->render_buffer_capacity = 131072;
  renderer->render_buffer = calloc(renderer->render_buffer_capacity, sizeof(VoxelRenderData));
  if (!renderer->render_buffer)
  {
    free(renderer);
    return NULL;
  }

  // Initialize world offsets
  init_world_offsets(renderer);
  for (int i = 0; i < 125; i++)
    renderer->edge_worlds[i] = NULL;
  for (int i = 0; i < 125; i++)
    renderer->world_alpha[i] = 1.0f;
  renderer->auto_center_camera = true;
  // Default: two world steps horizontally so the wilderness rings under the sky island draw as
  // distance terrain. Vertical reach still uses the full offset table (dz -2..+2); inclusion
  // prefers ground below over a second sky ring above.
  renderer->neighbor_inclusion_radius = 2;
  renderer->debug_show_world_bounds = false;
  renderer->greedy_neighbors_top_only = false;
  renderer->show_alignment_baseline = false;
  renderer->show_grid = true;  // Enable grid by default for debugging
  renderer->allow_clear = true;
  renderer->wireframe_mode = false;
  renderer->xray_mode = false;
  foliage_bend_field_clear(&renderer->foliage_bend);

  // Initialize battle arena state
  renderer->movement_target_set = false;
  renderer->show_movement_preview = false;
  renderer->player_moving = false;
  renderer->move_progress = 0.0f;
  renderer->show_player_avatar = true;
  renderer->player_facing_yaw = 0.0f;
  renderer->player_stamina = 100;
  renderer->player_stamina_max = 100;
  renderer->player_attacking = false;
  renderer->player_attack_progress = 0.0f;
  renderer->player_attack_armed = false;
  renderer->player_attack_radius = PLAYER_SWING_RADIUS;
  renderer->player_attack_strength = 10;
  renderer->player_universe_z = (uint64_t)UNIVERSE_HOME_Z;
  // Wilderness under the island is scenery from the sky island, not something to hide until a fall.
  renderer->hide_wilderness_below_home = false;
  renderer->stamina_meter_alpha = 0.0f;

  // Init per-type layer textures to NULL
  for (int i = 0; i < VOXEL_COUNT; i++)
  {
    renderer->per_type_layer_pixels[i] = NULL;
    renderer->per_type_layer_w[i] = 0;
    renderer->per_type_layer_h[i] = 0;
  }

  // Initialize persistent GPU vertex buffer cache
  renderer->gpu_vertices = NULL;
  renderer->gpu_vertex_capacity = 0;

  // Layer texture override init
  renderer->layer_texture_override_active = false;
  renderer->layer_tex_w = 0;
  renderer->layer_tex_h = 0;
  renderer->layer_tex_pixels = NULL;

  return renderer;
}

// Destroy isometric renderer
void isometric_renderer_destroy(IsometricRenderer *renderer)
{
  if (!renderer)
    return;

  if (renderer->render_buffer)
  {
    free(renderer->render_buffer);
  }

  if (renderer->material_atlas)
  {
    material_atlas_destroy(renderer->material_atlas);
    renderer->material_atlas = NULL;
  }
  mob_sprite_shutdown();
  if (renderer->water_surface)
  {
    fluid_surface_destroy(renderer->water_surface);
    renderer->water_surface = NULL;
  }
  if (renderer->fog_volume)
  {
    fog_volume_destroy(renderer->fog_volume);
    renderer->fog_volume = NULL;
  }

  for (int i = 0; i < VOXEL_COUNT; i++)
  {
    if (renderer->per_type_layer_pixels[i])
    {
      free(renderer->per_type_layer_pixels[i]);
      renderer->per_type_layer_pixels[i] = NULL;
      renderer->per_type_layer_w[i] = 0;
      renderer->per_type_layer_h[i] = 0;
    }
  }

  if (renderer->layer_tex_pixels)
  {
    free(renderer->layer_tex_pixels);
  }

  // Free persistent GPU vertex buffer cache
  if (renderer->gpu_vertices)
  {
    free(renderer->gpu_vertices);
    renderer->gpu_vertices = NULL;
    renderer->gpu_vertex_capacity = 0;
  }

  free(renderer);
}

#ifndef CLAMP_U8
#define CLAMP_U8(v) ((Uint8)(((v) < 0) ? 0 : (((v) > 255) ? 255 : (v))))
#endif

// Fast helpers moved to world.h for shared use

// Helper: compute voxel color consistent with add_voxel path
static SDL_Color compute_voxel_base_color(IsometricRenderer *renderer_local, VoxelType type, int world_index_local, int wx, int wy, int wz)
{
  SDL_Color color = isometric_get_voxel_color(type);
  if (type >= 0 && type < VOXEL_COUNT)
  {
    SDL_Color *tpix = renderer_local->per_type_layer_pixels[type];
    int tw2 = renderer_local->per_type_layer_w[type];
    int th2 = renderer_local->per_type_layer_h[type];
    if (tpix && tw2 > 0 && th2 > 0)
    {
      int tx = wx % tw2;
      if (tx < 0)
        tx += tw2;
      int ty = wy % th2;
      if (ty < 0)
        ty += th2;
      color = tpix[ty * tw2 + tx];
    }
  }
  if (renderer_local->layer_texture_override_active && renderer_local->layer_tex_pixels && renderer_local->layer_tex_w > 0 && renderer_local->layer_tex_h > 0)
  {
    if (world_index_local == 0 && renderer_local->game_worlds && renderer_local->game_worlds->home_world)
    {
      World *w0 = renderer_local->game_worlds->home_world;
      if (w0->depth == 1 && w0->width == (uint32_t)renderer_local->layer_tex_w && w0->height == (uint32_t)renderer_local->layer_tex_h)
      {
        int idx = wy * renderer_local->layer_tex_w + wx;
        if (idx >= 0 && idx < renderer_local->layer_tex_w * renderer_local->layer_tex_h)
          color = renderer_local->layer_tex_pixels[idx];
      }
    }
  }
  if (renderer_local->game_worlds && renderer_local->game_worlds->home_world && world_index_local == 0)
  {
    World *w0 = renderer_local->game_worlds->home_world;
    Voxel *vv = world_pos_in_bounds_fast(w0, wx, wy, wz) ? world_voxel_ptr_fast(w0, wx, wy, wz) : NULL;
    if (vv)
    {
      uint8_t wet = voxel_get_quantity_or_wetness(vv);
      if (wet > 0 && wet <= 6)
      {
        float t = (wet >= 6 ? 1.0f : (float)wet / 6.0f);
        int r1 = (int)(color.r * (1.0f - 0.35f * t));
        int g1 = (int)(color.g * (1.0f - 0.20f * t));
        int b1 = (int)(color.b + (255 - color.b) * 0.50f * t);
        color.r = (Uint8)CLAMP_U8(r1);
        color.g = (Uint8)CLAMP_U8(g1);
        color.b = (Uint8)CLAMP_U8(b1);
      }
    }
  }
  return color;
}

// Emit merged top faces for center world only using greedy rectangles
static void emit_top_faces_greedy_center_world(IsometricRenderer *renderer_local,
                                               int cx0_local, int cy0_local,
                                               int half_tw_local, int half_th_local,
                                               int vh_local, int view_z_local,
                                               size_t *vcount_io)
{
  World *w0 = renderer_local->game_worlds ? renderer_local->game_worlds->home_world : NULL;
  if (!w0)
    return;
  const int X = (int)w0->width;
  const int Y = (int)w0->height;
  const int Zmax = (renderer_local->camera_z < 0) ? 0 : (renderer_local->camera_z >= (int)w0->depth ? (int)w0->depth - 1 : renderer_local->camera_z);
  if (X <= 0 || Y <= 0 || Zmax < 0)
    return;
  uint32_t *mask = (uint32_t *)malloc((size_t)X * (size_t)Y * sizeof(uint32_t));
  if (!mask)
    return;
  for (int z = 0; z < (int)w0->depth; z++)
  {
    memset(mask, 0, (size_t)X * (size_t)Y * sizeof(uint32_t));
    for (int y = 0; y < Y; y++)
    {
      for (int x = 0; x < X; x++)
      {
        Voxel *v = world_pos_in_bounds_fast(w0, x, y, z) ? world_voxel_ptr_fast(w0, x, y, z) : NULL;
        if (!v || v->type == VOXEL_AIR)
          continue;
        if (!isometric_should_render_face(renderer_local, w0, x, y, z, 0))
          continue;
        SDL_Color c = compute_voxel_base_color(renderer_local, v->type, 0, x, y, z);
        uint32_t key = ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
        mask[(size_t)y * (size_t)X + (size_t)x] = key;
      }
    }
    for (int y = 0; y < Y; y++)
    {
      for (int x = 0; x < X; x++)
      {
        uint32_t key = mask[(size_t)y * (size_t)X + (size_t)x];
        if (key == 0)
          continue;
        int wspan = 1;
        while (x + wspan < X && mask[(size_t)y * (size_t)X + (size_t)(x + wspan)] == key)
          wspan++;
        int hspan = 1;
        bool expand = true;
        while (y + hspan < Y && expand)
        {
          for (int k = 0; k < wspan; k++)
          {
            if (mask[(size_t)(y + hspan) * (size_t)X + (size_t)(x + k)] != key)
            {
              expand = false;
              break;
            }
          }
          if (expand)
            hspan++;
        }
        SDL_Color c = (SDL_Color){(Uint8)((key >> 16) & 0xFF), (Uint8)((key >> 8) & 0xFF), (Uint8)(key & 0xFF), (Uint8)((key >> 24) & 0xFF)};
        int x0 = x, x1 = x + wspan, y0 = y, y1 = y + hspan;
        // Compute vertical offset in pixels for the current z slice relative to the view slice
        int dz_pixels = (view_z_local - z) * vh_local;
        // Build merged top diamond corners from extreme cells to cover the entire rectangle
        // Top uses (x0,y0) top vertex
        int sx_t_c = cx0_local + ((x0 - y0) * half_tw_local);
        int sy_t_c = cy0_local + ((x0 + y0) * half_th_local) + dz_pixels;
        SDL_FPoint p_top = {(float)sx_t_c, (float)(sy_t_c - half_th_local)};
        // Right uses (x1-1,y0) right vertex
        int sx_r_c = cx0_local + (((x1 - 1) - y0) * half_tw_local);
        int sy_r_c = cy0_local + (((x1 - 1) + y0) * half_th_local) + dz_pixels;
        SDL_FPoint p_right = {(float)(sx_r_c + half_tw_local), (float)sy_r_c};
        // Bottom uses (x1-1,y1-1) bottom vertex
        int sx_b_c = cx0_local + (((x1 - 1) - (y1 - 1)) * half_tw_local);
        int sy_b_c = cy0_local + (((x1 - 1) + (y1 - 1)) * half_th_local) + dz_pixels;
        SDL_FPoint p_bottom = {(float)sx_b_c, (float)(sy_b_c + half_th_local)};
        // Left uses (x0,y1-1) left vertex
        int sx_l_c = cx0_local + ((x0 - (y1 - 1)) * half_tw_local);
        int sy_l_c = cy0_local + ((x0 + (y1 - 1)) * half_th_local) + dz_pixels;
        SDL_FPoint p_left = {(float)(sx_l_c - half_tw_local), (float)sy_l_c};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_top, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_right, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_bottom, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_top, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_bottom, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_left, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        for (int yy = 0; yy < hspan; yy++)
          for (int xx = 0; xx < wspan; xx++)
            mask[(size_t)(y + yy) * (size_t)X + (size_t)(x + xx)] = 0;
        x += wspan - 1;
      }
    }
  }
  free(mask);
}

// Ensure GPU vertex buffer has room for at least (current + additional) vertices; grow geometrically
static inline int ensure_gpu_vertex_capacity(IsometricRenderer *renderer_local, size_t current_count, size_t additional)
{
  size_t need = current_count + additional;
  if (renderer_local->gpu_vertex_capacity >= need)
    return 1;
  size_t new_cap = renderer_local->gpu_vertex_capacity ? renderer_local->gpu_vertex_capacity : 16384;
  while (new_cap < need)
    new_cap *= 2;
  SDL_Vertex *nb = (SDL_Vertex *)realloc(renderer_local->gpu_vertices, new_cap * sizeof(SDL_Vertex));
  if (!nb)
    return 0;
  renderer_local->gpu_vertices = nb;
  renderer_local->gpu_vertex_capacity = new_cap;
  return 1;
}

// Emit merged top faces for any world index using greedy rectangles and world offsets
static void emit_top_faces_greedy_for_world(IsometricRenderer *renderer_local,
                                            World *world_local,
                                            int world_index_local,
                                            int cx0_local, int cy0_local,
                                            int half_tw_local, int half_th_local,
                                            int vh_local, int view_z_local,
                                            size_t *vcount_io)
{
  if (!renderer_local || !world_local)
    return;
  const int X = (int)world_local->width;
  const int Y = (int)world_local->height;
  if (X <= 0 || Y <= 0)
    return;

  WorldOffset woff = renderer_local->world_offsets[world_index_local];
  const int ox = woff.dx * (int)world_local->width;
  const int oy = woff.dy * (int)world_local->height;
  const int oz = woff.dz * (int)world_local->depth;

  // Respect slice clamp for same-Z neighbors, but allow full depth for vertical neighbors
  int Zmax_inclusive = (woff.dz == 0)
                           ? ((view_z_local < 0) ? -1 : ((view_z_local >= (int)world_local->depth) ? ((int)world_local->depth - 1) : view_z_local))
                           : ((int)world_local->depth - 1);
  if (Zmax_inclusive < 0)
    return;

  uint32_t *mask = (uint32_t *)malloc((size_t)X * (size_t)Y * sizeof(uint32_t));
  if (!mask)
    return;
  for (int z = 0; z < (int)world_local->depth; z++)
  {
    memset(mask, 0, (size_t)X * (size_t)Y * sizeof(uint32_t));
    for (int y = 0; y < Y; y++)
    {
      for (int x = 0; x < X; x++)
      {
        Voxel *v = world_pos_in_bounds_fast(world_local, x, y, z) ? world_voxel_ptr_fast(world_local, x, y, z) : NULL;
        if (!v || v->type == VOXEL_AIR)
          continue;
        // Only consider top faces visible at this slice
        if (!isometric_should_render_face(renderer_local, world_local, x, y, z, 0))
          continue;
        SDL_Color c = compute_voxel_base_color(renderer_local, v->type, world_index_local, x, y, z);
        uint32_t key = ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
        mask[(size_t)y * (size_t)X + (size_t)x] = key;
      }
    }
    for (int y = 0; y < Y; y++)
    {
      for (int x = 0; x < X; x++)
      {
        uint32_t key = mask[(size_t)y * (size_t)X + (size_t)x];
        if (key == 0)
          continue;
        int wspan = 1;
        while (x + wspan < X && mask[(size_t)y * (size_t)X + (size_t)(x + wspan)] == key)
          wspan++;
        int hspan = 1;
        bool expand = true;
        while (y + hspan < Y && expand)
        {
          for (int k = 0; k < wspan; k++)
          {
            if (mask[(size_t)(y + hspan) * (size_t)X + (size_t)(x + k)] != key)
            {
              expand = false;
              break;
            }
          }
          if (expand)
            hspan++;
        }
        SDL_Color c = (SDL_Color){(Uint8)((key >> 16) & 0xFF), (Uint8)((key >> 8) & 0xFF), (Uint8)(key & 0xFF), (Uint8)((key >> 24) & 0xFF)};
        int x0 = x, x1 = x + wspan, y0 = y, y1 = y + hspan;
        // Vertical offset relative to the view slice accounting for world Z offset
        int dz_pixels = (view_z_local - (z + oz)) * vh_local;
        // Build top diamond corners using world offsets in isometric mapping
        int sx_t_c = cx0_local + (((x0 + ox) - (y0 + oy)) * half_tw_local);
        int sy_t_c = cy0_local + (((x0 + ox) + (y0 + oy)) * half_th_local) + dz_pixels;
        SDL_FPoint p_top = {(float)sx_t_c, (float)(sy_t_c - half_th_local)};
        int sx_r_c = cx0_local + ((((x1 - 1) + ox) - (y0 + oy)) * half_tw_local);
        int sy_r_c = cy0_local + ((((x1 - 1) + ox) + (y0 + oy)) * half_th_local) + dz_pixels;
        SDL_FPoint p_right = {(float)(sx_r_c + half_tw_local), (float)sy_r_c};
        int sx_b_c = cx0_local + ((((x1 - 1) + ox) - ((y1 - 1) + oy)) * half_tw_local);
        int sy_b_c = cy0_local + ((((x1 - 1) + ox) + ((y1 - 1) + oy)) * half_th_local) + dz_pixels;
        SDL_FPoint p_bottom = {(float)sx_b_c, (float)(sy_b_c + half_th_local)};
        int sx_l_c = cx0_local + (((x0 + ox) - ((y1 - 1) + oy)) * half_tw_local);
        int sy_l_c = cy0_local + (((x0 + ox) + ((y1 - 1) + oy)) * half_th_local) + dz_pixels;
        SDL_FPoint p_left = {(float)(sx_l_c - half_tw_local), (float)sy_l_c};

        if (!ensure_gpu_vertex_capacity(renderer_local, *vcount_io, 6))
          return;
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_top, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_right, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_bottom, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_top, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_bottom, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = p_left, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        for (int yy = 0; yy < hspan; yy++)
          for (int xx = 0; xx < wspan; xx++)
            mask[(size_t)(y + yy) * (size_t)X + (size_t)(x + xx)] = 0;
        x += wspan - 1;
      }
    }
  }
  free(mask);
}

// Emit merged right faces (face_index=3) per row using greedy spans along X at fixed (y,z)
static void emit_right_faces_greedy_rows_for_world(IsometricRenderer *renderer_local,
                                                   World *world_local,
                                                   int world_index_local,
                                                   int cx0_local, int cy0_local,
                                                   int half_tw_local, int half_th_local,
                                                   int vh_local, int view_z_local,
                                                   size_t *vcount_io)
{
  if (!renderer_local || !world_local)
    return;
  const int X = (int)world_local->width;
  const int Y = (int)world_local->height;
  if (X <= 0 || Y <= 0)
    return;

  WorldOffset woff = renderer_local->world_offsets[world_index_local];
  const int ox = woff.dx * (int)world_local->width;
  const int oy = woff.dy * (int)world_local->height;
  const int oz = woff.dz * (int)world_local->depth;

  int Zmax_inclusive = (woff.dz == 0)
                           ? ((view_z_local < 0) ? -1 : ((view_z_local >= (int)world_local->depth) ? ((int)world_local->depth - 1) : view_z_local))
                           : ((int)world_local->depth - 1);
  if (Zmax_inclusive < 0)
    return;

  uint32_t *mask = (uint32_t *)malloc((size_t)X * (size_t)Y * sizeof(uint32_t));
  if (!mask)
    return;
  for (int z = 0; z < (int)world_local->depth; z++)
  {
    // Build color mask for visible right faces at this z
    for (int y = 0; y < Y; y++)
    {
      for (int x = 0; x < X; x++)
      {
        uint32_t key = 0;
        Voxel *v = world_pos_in_bounds_fast(world_local, x, y, z) ? world_voxel_ptr_fast(world_local, x, y, z) : NULL;
        if (v && v->type != VOXEL_AIR)
        {
          if (isometric_should_render_face(renderer_local, world_local, x, y, z, 3))
          {
            SDL_Color c0 = compute_voxel_base_color(renderer_local, v->type, world_index_local, x, y, z);
            // Right face shading 0.8
            SDL_Color c = (SDL_Color){(Uint8)CLAMP_U8((int)(c0.r * 0.8f)), (Uint8)CLAMP_U8((int)(c0.g * 0.8f)), (Uint8)CLAMP_U8((int)(c0.b * 0.8f)), c0.a};
            key = ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
          }
        }
        mask[(size_t)y * (size_t)X + (size_t)x] = key;
      }
    }
    // For each row, merge spans along X
    for (int y = 0; y < Y; y++)
    {
      int x = 0;
      while (x < X)
      {
        uint32_t key = mask[(size_t)y * (size_t)X + (size_t)x];
        if (key == 0)
        {
          x++;
          continue;
        }
        int x_start = x;
        int x_end = x + 1;
        while (x_end < X && mask[(size_t)y * (size_t)X + (size_t)x_end] == key)
          x_end++;
        // Emit a single quad covering [x_start, x_end) at (y,z)
        SDL_Color c = (SDL_Color){(Uint8)((key >> 16) & 0xFF), (Uint8)((key >> 8) & 0xFF), (Uint8)(key & 0xFF), (Uint8)((key >> 24) & 0xFF)};
        int dz_pixels = (view_z_local - (z + oz)) * vh_local;
        // Left end world-to-screen
        int sxL = cx0_local + (((x_start + ox) - (y + oy)) * half_tw_local);
        int syB_L = cy0_local + (((x_start + ox) + (y + oy)) * half_th_local) + dz_pixels; // base
        int sxR = cx0_local + ((((x_end - 1) + ox) - (y + oy)) * half_tw_local);
        int syB_R = cy0_local + ((((x_end - 1) + ox) + (y + oy)) * half_th_local) + dz_pixels;
        SDL_FPoint top_left = {(float)(sxL), (float)(syB_L + half_th_local)};                // R3 at (x_start,y)
        SDL_FPoint top_right = {(float)(sxR + half_tw_local), (float)(syB_R)};               // R0 at (x_end-1,y)
        SDL_FPoint bottom_right = {(float)(sxR + half_tw_local), (float)(syB_R + vh_local)}; // R1 at (x_end-1,y)
        SDL_FPoint bottom_left = {(float)(sxL), (float)(syB_L + half_th_local + vh_local)};  // R2 at (x_start,y)

        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = top_left, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = top_right, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = bottom_right, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = top_left, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = bottom_right, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = bottom_left, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};

        x = x_end;
      }
    }
  }
  free(mask);
}

// Emit merged left faces (face_index=2) per row using greedy spans along X at fixed (y,z)
static void emit_left_faces_greedy_rows_for_world(IsometricRenderer *renderer_local,
                                                  World *world_local,
                                                  int world_index_local,
                                                  int cx0_local, int cy0_local,
                                                  int half_tw_local, int half_th_local,
                                                  int vh_local, int view_z_local,
                                                  size_t *vcount_io)
{
  if (!renderer_local || !world_local)
    return;
  const int X = (int)world_local->width;
  const int Y = (int)world_local->height;
  if (X <= 0 || Y <= 0)
    return;

  WorldOffset woff = renderer_local->world_offsets[world_index_local];
  const int ox = woff.dx * (int)world_local->width;
  const int oy = woff.dy * (int)world_local->height;
  const int oz = woff.dz * (int)world_local->depth;

  int Zmax_inclusive = (woff.dz == 0)
                           ? ((view_z_local < 0) ? -1 : ((view_z_local >= (int)world_local->depth) ? ((int)world_local->depth - 1) : view_z_local))
                           : ((int)world_local->depth - 1);
  if (Zmax_inclusive < 0)
    return;

  uint32_t *mask = (uint32_t *)malloc((size_t)X * (size_t)Y * sizeof(uint32_t));
  if (!mask)
    return;
  for (int z = 0; z < (int)world_local->depth; z++)
  {
    // Build color mask for visible left faces at this z
    for (int y = 0; y < Y; y++)
    {
      for (int x = 0; x < X; x++)
      {
        uint32_t key = 0;
        Voxel *v = world_pos_in_bounds_fast(world_local, x, y, z) ? world_voxel_ptr_fast(world_local, x, y, z) : NULL;
        if (v && v->type != VOXEL_AIR)
        {
          if (isometric_should_render_face(renderer_local, world_local, x, y, z, 2))
          {
            SDL_Color c0 = compute_voxel_base_color(renderer_local, v->type, world_index_local, x, y, z);
            // Left face shading 0.6
            SDL_Color c = (SDL_Color){(Uint8)CLAMP_U8((int)(c0.r * 0.6f)), (Uint8)CLAMP_U8((int)(c0.g * 0.6f)), (Uint8)CLAMP_U8((int)(c0.b * 0.6f)), c0.a};
            key = ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
          }
        }
        mask[(size_t)y * (size_t)X + (size_t)x] = key;
      }
    }
    // For each row, merge spans along X
    for (int y = 0; y < Y; y++)
    {
      int x = 0;
      while (x < X)
      {
        uint32_t key = mask[(size_t)y * (size_t)X + (size_t)x];
        if (key == 0)
        {
          x++;
          continue;
        }
        int x_start = x;
        int x_end = x + 1;
        while (x_end < X && mask[(size_t)y * (size_t)X + (size_t)x_end] == key)
          x_end++;
        // Emit a single quad covering [x_start, x_end) at (y,z)
        SDL_Color c = (SDL_Color){(Uint8)((key >> 16) & 0xFF), (Uint8)((key >> 8) & 0xFF), (Uint8)(key & 0xFF), (Uint8)((key >> 24) & 0xFF)};
        int dz_pixels = (view_z_local - (z + oz)) * vh_local;
        int sxL = cx0_local + (((x_start + ox) - (y + oy)) * half_tw_local);
        int syB_L = cy0_local + (((x_start + ox) + (y + oy)) * half_th_local) + dz_pixels; // base
        int sxR = cx0_local + ((((x_end - 1) + ox) - (y + oy)) * half_tw_local);
        int syB_R = cy0_local + ((((x_end - 1) + ox) + (y + oy)) * half_th_local) + dz_pixels;
        SDL_FPoint top_left = {(float)(sxL - half_tw_local), (float)(syB_L)};                // L0 at (x_start,y)
        SDL_FPoint top_right = {(float)(sxR), (float)(syB_R + half_th_local)};               // L1 at (x_end-1,y)
        SDL_FPoint bottom_right = {(float)(sxR), (float)(syB_R + half_th_local + vh_local)}; // L2 at (x_end-1,y)
        SDL_FPoint bottom_left = {(float)(sxL - half_tw_local), (float)(syB_L + vh_local)};  // L3 at (x_start,y)

        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = top_left, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = top_right, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = bottom_right, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = top_left, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = bottom_right, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};
        renderer_local->gpu_vertices[(*vcount_io)++] = (SDL_Vertex){.position = bottom_left, .color = c, .tex_coord = (SDL_FPoint){0.0f, 0.0f}};

        x = x_end;
      }
    }
  }
  free(mask);
}

// Map a voxel type name (lowercase) to enum; returns -1 on failure
static int voxel_type_from_name(const char *name)
{
  if (!name)
    return -1;
  for (int t = 0; t < VOXEL_COUNT; t++)
  {
    const char *n = world_voxel_type_name((VoxelType)t);
    if (!n)
      continue;
    // Compare lowercased
    char buf[64];
    size_t ln = strlen(n);
    if (ln >= sizeof(buf))
      ln = sizeof(buf) - 1;
    for (size_t i = 0; i < ln; i++)
      buf[i] = (char)tolower((unsigned char)n[i]);
    buf[ln] = '\0';
    // Also replace spaces with underscores for matching
    for (size_t i = 0; i < ln; i++)
      if (buf[i] == ' ')
        buf[i] = '_';
    if (strcmp(buf, name) == 0)
      return t;
  }
  return -1;
}

// Load all models/layer_*.world textures (32x32x1)
int isometric_renderer_load_layer_textures_from_models(IsometricRenderer *renderer, const char *models_dir)
{
  if (!renderer)
    return 0;
  const char *root = (models_dir && *models_dir) ? models_dir : "models";
  DIR *d = opendir(root);
  if (!d)
    return 0;
  int loaded = 0;
  struct dirent *de;
  while ((de = readdir(d)) != NULL)
  {
    const char *fn = de->d_name;
    if (!fn || strncmp(fn, "layer_", 7) != 0)
      continue;
    size_t len = strlen(fn);
    if (len < 13)
      continue; // minimum like layer_a.world
    if (!(len > 6 && strcmp(fn + (len - 6), ".world") == 0))
      continue;
    // Extract key between prefix and suffix
    char key[128];
    size_t klen = len - 7 /*prefix*/ - 6 /*.world*/;
    if (klen >= sizeof(key))
      klen = sizeof(key) - 1;
    memcpy(key, fn + 7, klen);
    key[klen] = '\0';
    // Normalize: lowercase
    for (size_t i = 0; i < klen; i++)
      key[i] = (char)tolower((unsigned char)key[i]);
    int t = voxel_type_from_name(key);
    if (t < 0 || t >= VOXEL_COUNT)
      continue;
    // Load world
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, fn);
    World *w = world_create(1, 1, 1);
    if (!w)
      continue;
    if (!world_load(w, path))
    {
      world_destroy(w);
      continue;
    }
    if (!(w->width == 32 && w->height == 32 && w->depth == 1))
    {
      world_destroy(w);
      continue;
    }
    // Prefer embedded full-color topDownTexture if present; fallback to per-voxel-type mapping
    SDL_Color *pixels = NULL;
    {
      const char *log = world_get_log(w);
      int tw = 0, th = 0;
      char *hex = NULL;
      // Minimal parse of topDownTexture { encoding:"hex24", width, height, pixelsHex }
      if (log)
      {
        const char *td = strstr(log, "\"topDownTexture\"");
        const char *enc = td ? strstr(td, "\"encoding\":\"hex24\"") : NULL;
        const char *wpos = td ? strstr(td, "\"width\":") : NULL;
        const char *hpos = td ? strstr(td, "\"height\":") : NULL;
        const char *ppos = td ? strstr(td, "\"pixelsHex\":\"") : NULL;
        if (enc && wpos && hpos && ppos)
        {
          tw = atoi(wpos + 9);
          th = atoi(hpos + 10);
          const char *start = ppos + (int)strlen("\"pixelsHex\":\"");
          const char *end = start ? strchr(start, '"') : NULL;
          if (end && end > start)
          {
            size_t len = (size_t)(end - start);
            hex = (char *)malloc(len + 1);
            if (hex)
            {
              memcpy(hex, start, len);
              hex[len] = '\0';
            }
          }
        }
      }
      if (hex && tw == 32 && th == 32)
      {
        // Decode hex24 into pixels
        pixels = (SDL_Color *)malloc(sizeof(SDL_Color) * 32 * 32);
        if (pixels)
        {
          for (int i = 0; i < 32 * 32; i++)
          {
            unsigned int rv = 0, gv = 0, bv = 0;
            const char *p = hex + (size_t)i * 6;
            if (p[0] && p[1] && p[2] && p[3] && p[4] && p[5])
            {
              sscanf(p, "%02X%02X%02X", &rv, &gv, &bv);
            }
            pixels[i].r = (Uint8)rv;
            pixels[i].g = (Uint8)gv;
            pixels[i].b = (Uint8)bv;
            pixels[i].a = 255;
          }
        }
        free(hex);
      }
      if (!pixels)
      {
        // Fallback: derive color per voxel type
        pixels = (SDL_Color *)malloc(sizeof(SDL_Color) * 32 * 32);
        if (!pixels)
        {
          world_destroy(w);
          continue;
        }
        for (uint32_t y = 0; y < 32; y++)
        {
          for (uint32_t x = 0; x < 32; x++)
          {
            Voxel *v = world_pos_in_bounds_fast(w, (int)x, (int)y, 0) ? world_voxel_ptr_fast(w, (int)x, (int)y, 0) : NULL;
            uint8_t r = 0, g = 0, b = 0;
            world_voxel_type_color(v ? v->type : VOXEL_AIR, &r, &g, &b);
            pixels[y * 32 + x] = (SDL_Color){r, g, b, 255};
          }
        }
      }
    }
    // Store on renderer (free any previous)
    if (renderer->per_type_layer_pixels[t])
      free(renderer->per_type_layer_pixels[t]);
    renderer->per_type_layer_pixels[t] = pixels;
    renderer->per_type_layer_w[t] = 32;
    renderer->per_type_layer_h[t] = 32;
    world_destroy(w);
    loaded++;
  }
  closedir(d);
  return loaded;
}

int isometric_renderer_offset_index(IsometricRenderer *renderer, int dx, int dy, int dz)
{
  if (!renderer)
    return -1;
  for (int i = 0; i < 125; i++)
  {
    if (renderer->world_offsets[i].dx == dx && renderer->world_offsets[i].dy == dy && renderer->world_offsets[i].dz == dz)
      return i;
  }
  return -1;
}

// Mass highlight control
void isometric_renderer_set_highlight_type(IsometricRenderer *renderer, int voxel_type)
{
  if (!renderer)
    return;
  renderer->mass_highlight_active = (voxel_type >= 0);
  renderer->highlighted_type = voxel_type;
}

void isometric_renderer_clear_highlight_type(IsometricRenderer *renderer)
{
  if (!renderer)
    return;
  renderer->mass_highlight_active = false;
  renderer->highlighted_type = -1;
}

void isometric_renderer_set_wireframe(IsometricRenderer *renderer, bool enabled)
{
  if (!renderer)
    return;
  renderer->wireframe_mode = enabled;
}

void isometric_renderer_set_xray(IsometricRenderer *renderer, bool enabled)
{
  if (!renderer)
    return;
  renderer->xray_mode = enabled;
}

// Terrain that becomes translucent in x-ray (ores / specials stay opaque).
static bool isometric_xray_is_translucent(VoxelType type)
{
  if (type == VOXEL_AIR || type == VOXEL_WATER || type == VOXEL_STEAM || type == VOXEL_GAS)
    return false;
  if (type >= VOXEL_ORE && type <= VOXEL_ORE_PLATINUM)
    return false;
  if (type == VOXEL_GOLD || type == VOXEL_IRON || type == VOXEL_WORLD)
    return false;
  return true;
}

static Uint8 isometric_apply_xray_alpha(IsometricRenderer *renderer, VoxelType type, Uint8 alpha)
{
  if (!renderer || !renderer->xray_mode)
    return alpha;
  if (!isometric_xray_is_translucent(type))
    return alpha;
  // Keep a readable silhouette without fully obscuring what sits behind.
  Uint8 target = 70;
  return alpha < target ? alpha : target;
}

// Set game worlds
void isometric_renderer_set_game_worlds(IsometricRenderer *renderer, GameWorlds *game_worlds)
{
  renderer->game_worlds = game_worlds;
}

void isometric_renderer_set_fog(IsometricRenderer *renderer, FogAtlas *fog)
{
  if (!renderer)
    return;
  renderer->fog = fog;
}

static World *iso_world_for_index(IsometricRenderer *renderer, int world_index)
{
  if (!renderer || world_index < 0 || world_index >= 125)
    return NULL;
  if (world_index == 0)
    return renderer->game_worlds ? renderer->game_worlds->home_world : NULL;
  return renderer->edge_worlds[world_index];
}

// Set camera position
void isometric_renderer_set_camera(IsometricRenderer *renderer, int x, int y, int z)
{
  isometric_renderer_set_camera_world(renderer, (float)x, (float)y, (float)z);
}

void isometric_renderer_set_camera_world(IsometricRenderer *renderer, float x, float y, float z)
{
  if (!renderer)
    return;
  renderer->camera_world_x = x;
  renderer->camera_world_y = y;
  renderer->camera_world_z = z;
  renderer->camera_x = (int)floorf(x);
  renderer->camera_y = (int)floorf(y);
  renderer->camera_z = (int)floorf(z);
}

void isometric_renderer_set_player_avatar(IsometricRenderer *renderer, float facing_yaw,
                                          bool attacking, float attack_progress, bool armed,
                                          float attack_radius, uint32_t attack_strength,
                                          bool show)
{
  if (!renderer)
    return;
  renderer->player_facing_yaw = facing_yaw;
  renderer->player_attacking = attacking;
  renderer->player_attack_progress = attack_progress;
  renderer->player_attack_armed = armed;
  renderer->player_attack_radius = attack_radius > 0.1f ? attack_radius : PLAYER_SWING_RADIUS;
  renderer->player_attack_strength = attack_strength > 0 ? attack_strength : 10;
  renderer->show_player_avatar = show;
}

void isometric_renderer_set_player_stamina(IsometricRenderer *renderer,
                                           uint32_t stamina, uint32_t max_stamina)
{
  if (!renderer)
    return;
  renderer->player_stamina = stamina;
  renderer->player_stamina_max = max_stamina > 0 ? max_stamina : 1;
}

void isometric_renderer_set_universe_layer(IsometricRenderer *renderer, uint64_t universe_z,
                                           bool hide_wilderness_below_home)
{
  if (!renderer)
    return;
  renderer->player_universe_z = universe_z;
  renderer->hide_wilderness_below_home = hide_wilderness_below_home;
}

void isometric_renderer_set_stamina_meter_alpha(IsometricRenderer *renderer, float alpha)
{
  if (!renderer)
    return;
  if (alpha < 0.0f)
    alpha = 0.0f;
  if (alpha > 1.0f)
    alpha = 1.0f;
  renderer->stamina_meter_alpha = alpha;
}

void isometric_renderer_set_auto_center(IsometricRenderer *renderer, bool enabled)
{
  if (!renderer)
    return;
  renderer->auto_center_camera = enabled;
}

void isometric_renderer_set_screen_size(IsometricRenderer *renderer, int screen_width, int screen_height)
{
  if (!renderer)
    return;
  renderer->screen_width = screen_width;
  renderer->screen_height = screen_height;
  renderer->screen_center_x = screen_width / 2;
  renderer->screen_center_y = screen_height / 2;
}

void isometric_renderer_set_disable_culling(IsometricRenderer *renderer, bool disable)
{
  if (!renderer)
    return;
  renderer->disable_culling = disable;
}

// GPU voxel faces and every overlay (actors, projectiles, particles) share this lattice:
// camera XY is baked into the origin so the camera voxel maps to the window centre, then
// a world point is projected in absolute coordinates. Subtracting camera from the point as
// well used to double-pan overlays toward the far/north corner of the diamond.
static void iso_lattice_origin(const IsometricRenderer *renderer, int *cx, int *cy)
{
  const int half_tw = renderer->tile_width / 2;
  const int half_th = renderer->tile_height / 2;
  *cx = renderer->screen_width / 2 -
        (int)roundf((renderer->camera_world_x - renderer->camera_world_y) * (float)half_tw);
  *cy = renderer->screen_height / 2 -
        (int)roundf((renderer->camera_world_x + renderer->camera_world_y) * (float)half_th);
}

static void iso_world_span(const IsometricRenderer *renderer, int *ww, int *wh, int *wd)
{
  const World *home = (renderer->game_worlds && renderer->game_worlds->home_world)
                          ? renderer->game_worlds->home_world
                          : NULL;
  *ww = home ? (int)home->width : WORLD_SIZE_X;
  *wh = home ? (int)home->height : WORLD_SIZE_Y;
  *wd = home ? (int)home->depth : WORLD_SIZE_Z;
}

// Convert world coordinates to screen coordinates (direct isometric)
void isometric_world_to_screen(IsometricRenderer *renderer,
                               int world_x, int world_y, int world_z,
                               int world_index,
                               int *screen_x, int *screen_y)
{
  if (!renderer || !screen_x || !screen_y)
    return;
  isometric_world_to_screen_float(renderer, (float)world_x, (float)world_y, (float)world_z,
                                  world_index, screen_x, screen_y);
}

void isometric_world_to_screen_float(IsometricRenderer *renderer,
                                     float world_x, float world_y, float world_z,
                                     int world_index,
                                     int *screen_x, int *screen_y)
{
  if (!renderer || !screen_x || !screen_y)
    return;
  if (world_index < 0 || world_index >= 125)
    world_index = 0;

  int ww, wh, wd;
  iso_world_span(renderer, &ww, &wh, &wd);
  const WorldOffset offset = renderer->world_offsets[world_index];
  const float ox = world_x + (float)offset.dx * (float)ww;
  const float oy = world_y + (float)offset.dy * (float)wh;
  const float oz = world_z + (float)offset.dz * (float)wd;

  const int half_tw = renderer->tile_width / 2;
  const int half_th = renderer->tile_height / 2;
  const int vh = renderer->voxel_height;
  int cx0, cy0;
  iso_lattice_origin(renderer, &cx0, &cy0);

  *screen_x = cx0 + (int)roundf((ox - oy) * (float)half_tw);
  *screen_y = cy0 + (int)roundf((ox + oy) * (float)half_th +
                                ((float)renderer->camera_z - oz) * (float)vh);
}

// Convert screen to world coordinates
void isometric_screen_to_world(IsometricRenderer *renderer,
                               int screen_x, int screen_y,
                               int *world_x, int *world_y, int *world_z,
                               int *world_index)
{
  if (!renderer || !world_x || !world_y || !world_z || !world_index)
    return;

  const float half_tw = (float)renderer->tile_width / 2.0f;
  const float half_th = (float)renderer->tile_height / 2.0f;
  if (half_tw < 1.0f || half_th < 1.0f)
  {
    *world_x = 0;
    *world_y = 0;
    *world_z = renderer->camera_z;
    *world_index = 0;
    return;
  }

  int cx0, cy0;
  iso_lattice_origin(renderer, &cx0, &cy0);
  const float fx = (float)(screen_x - cx0) / half_tw;
  const float fy = (float)(screen_y - cy0) / half_th;
  *world_x = (int)roundf((fx + fy) * 0.5f);
  *world_y = (int)roundf((fy - fx) * 0.5f);
  *world_z = renderer->camera_z;
  *world_index = 0;

  int ww, wh, wd;
  iso_world_span(renderer, &ww, &wh, &wd);
  for (int i = 1; i < 125; i++)
  {
    WorldOffset offset = renderer->world_offsets[i];
    int wx = *world_x - offset.dx * ww;
    int wz = *world_z - offset.dz * wd;
    if (wx >= 0 && wx < ww && wz >= 0 && wz < wd)
    {
      *world_index = i;
      *world_x = wx;
      *world_z = wz;
      break;
    }
  }
}

// Get voxel color
SDL_Color isometric_get_voxel_color(VoxelType type)
{
  // Delegate to canonical project color mapping
  uint8_t r = 64, g = 64, b = 64;
  world_voxel_type_color(type, &r, &g, &b);
  return (SDL_Color){r, g, b, 255};
}

// Parse minimal JSON fields without a full JSON parser; look for keys and values by string search.
static bool parse_layer_texture_from_log(const char *log, int *out_w, int *out_h, char **out_pixels_hex)
{
  if (!log)
    return false;
  const char *td = strstr(log, "\"topDownTexture\"");
  if (!td)
    return false;
  const char *enc = strstr(td, "\"encoding\":\"hex24\"");
  if (!enc)
    return false;
  const char *wpos = strstr(td, "\"width\":");
  const char *hpos = strstr(td, "\"height\":");
  const char *ppos = strstr(td, "\"pixelsHex\":\"");
  if (!wpos || !hpos || !ppos)
    return false;
  int w = atoi(wpos + 9);
  int h = atoi(hpos + 10);
  const char *start = ppos + strlen("\"pixelsHex\":\"");
  const char *end = strchr(start, '"');
  if (!end || end <= start)
    return false;
  size_t len = (size_t)(end - start);
  char *hex = (char *)malloc(len + 1);
  if (!hex)
    return false;
  memcpy(hex, start, len);
  hex[len] = '\0';
  if (out_w)
    *out_w = w;
  if (out_h)
    *out_h = h;
  if (out_pixels_hex)
    *out_pixels_hex = hex;
  else
    free(hex);
  return true;
}

static bool hex24_to_pixels(const char *hex, int w, int h, SDL_Color **out_pixels)
{
  if (!hex || w <= 0 || h <= 0)
    return false;
  size_t need = (size_t)w * (size_t)h * 6;
  if (strlen(hex) < need)
    return false;
  SDL_Color *px = (SDL_Color *)malloc((size_t)w * (size_t)h * sizeof(SDL_Color));
  if (!px)
    return false;
  for (int i = 0; i < w * h; i++)
  {
    unsigned int rv = 0, gv = 0, bv = 0;
    const char *p = hex + (size_t)i * 6;
    sscanf(p, "%02X%02X%02X", &rv, &gv, &bv);
    px[i].r = (Uint8)rv;
    px[i].g = (Uint8)gv;
    px[i].b = (Uint8)bv;
    px[i].a = 255;
  }
  *out_pixels = px;
  return true;
}

bool isometric_renderer_try_load_layer_texture(IsometricRenderer *renderer, World *world)
{
  if (!renderer || !world)
    return false;
  const char *log = world_get_log(world);
  int w = 0, h = 0;
  char *hex = NULL;
  if (parse_layer_texture_from_log(log, &w, &h, &hex))
  {
    SDL_Color *px = NULL;
    bool ok = hex24_to_pixels(hex, w, h, &px);
    free(hex);
    if (!ok)
      return false;
    if (renderer->layer_tex_pixels)
      free(renderer->layer_tex_pixels);
    renderer->layer_tex_pixels = px;
    renderer->layer_tex_w = w;
    renderer->layer_tex_h = h;
    renderer->layer_texture_override_active = true;
    return true;
  }
  // Try animatedTexture: pixelsHexFrames array; pick first frame for preview
  const char *at = strstr(log ? log : "", "\"animatedTexture\"");
  if (!at)
    return false;
  const char *enc = strstr(at, "\"encoding\":\"hex24\"");
  const char *wpos = strstr(at, "\"width\":");
  const char *hpos = strstr(at, "\"height\":");
  const char *arr = strstr(at, "\"pixelsHexFrames\":[");
  if (!enc || !wpos || !hpos || !arr)
    return false;
  w = atoi(wpos + 9);
  h = atoi(hpos + 10);
  const char *s0 = strchr(arr, '"');
  if (!s0)
    return false;
  s0++;
  const char *e0 = strchr(s0, '"');
  if (!e0 || e0 <= s0)
    return false;
  size_t len = (size_t)(e0 - s0);
  char *fhex = (char *)malloc(len + 1);
  if (!fhex)
    return false;
  memcpy(fhex, s0, len);
  fhex[len] = '\0';
  SDL_Color *px = NULL;
  bool ok = hex24_to_pixels(fhex, w, h, &px);
  free(fhex);
  if (!ok)
    return false;
  if (renderer->layer_tex_pixels)
    free(renderer->layer_tex_pixels);
  renderer->layer_tex_pixels = px;
  renderer->layer_tex_w = w;
  renderer->layer_tex_h = h;
  renderer->layer_texture_override_active = true;
  return true;
}

// Clear render buffer
void isometric_renderer_clear_buffer(IsometricRenderer *renderer)
{
  renderer->render_buffer_size = 0;
  // Scan counters belong to the frame, not to one world, since a frame scans the origin world plus
  // its loaded neighbours. Clearing them here keeps them summed across that set.
  renderer->scan_visited = 0;
  renderer->scan_air = 0;
  renderer->scan_solid = 0;
  renderer->scan_emitted = 0;
}

// Mirror world_viewer's isometric pick with our sizing/centering
bool isometric_renderer_pick_voxel(IsometricRenderer *renderer,
                                   World *w,
                                   int view_z,
                                   int mx,
                                   int my,
                                   uint32_t *out_x,
                                   uint32_t *out_y,
                                   uint32_t *out_z)
{
  if (!renderer || !w)
    return false;

  const int tw = renderer->tile_width;
  const int th = renderer->tile_height;
  const int vh = renderer->voxel_height;
  int half_tw = tw / 2;
  int half_th = th / 2;
  int cx0 = renderer->screen_center_x;
  int cy0 = renderer->screen_center_y;

  // Center occupied bbox up to view_z (same as GPU path)
  {
    int minx = (int)w->width, miny = (int)w->height, maxx = -1, maxy = -1;
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
        for (int z = 0; z < (int)w->depth; z++)
        {
          Voxel *v = world_pos_in_bounds_fast(w, x, y, z) ? world_voxel_ptr_fast(w, x, y, z) : NULL;
          if (v && v->type != VOXEL_AIR)
          {
            if ((int)x < minx)
              minx = (int)x;
            if ((int)y < miny)
              miny = (int)y;
            if ((int)x > maxx)
              maxx = (int)x;
            if ((int)y > maxy)
              maxy = (int)y;
            break;
          }
        }
    if (maxx >= minx && maxy >= miny)
    {
      float cmx = (minx + maxx) * 0.5f;
      float cmy = (miny + maxy) * 0.5f;
      int center_sx = cx0 + ((int)cmx - (int)cmy) * half_tw;
      int center_sy = cy0 + ((int)cmx + (int)cmy) * half_th;
      cx0 -= (center_sx - renderer->screen_width / 2);
      cy0 -= (center_sy - renderer->screen_height / 2);
    }
  }

  bool any_hit = false;
  uint32_t pick_x = 0, pick_y = 0, pick_z = 0;
  for (uint32_t y = 0; y < w->height; y++)
  {
    for (uint32_t x = 0; x < w->width; x++)
    {
      int sx = cx0 + ((int)x - (int)y) * half_tw;
      int sy_base = cy0 + ((int)x + (int)y) * half_th;
      for (int z0 = 0; z0 < (int)w->depth; z0++)
      {
        Voxel *v = world_pos_in_bounds_fast(w, x, y, z0) ? world_voxel_ptr_fast(w, x, y, z0) : NULL;
        if (!v || v->type == VOXEL_AIR)
          continue;
        int sy = sy_base + (view_z - z0) * vh;

        // Hit against diamond top and side faces (same math as viewer)
        SDL_Point top0 = {sx, sy - half_th};
        SDL_Point top1 = {sx + half_tw, sy};
        SDL_Point top2 = {sx, sy + half_th};
        SDL_Point top3 = {sx - half_tw, sy};

// Barycentric test (C99)
#define IN_TRI(PX, PY, A, B, C) ({ \
          int v0x = (C).x - (A).x, v0y = (C).y - (A).y; \
          int v1x = (B).x - (A).x, v1y = (B).y - (A).y; \
          int v2x = (PX) - (A).x,  v2y = (PY) - (A).y;  \
          int dot00 = v0x * v0x + v0y * v0y; \
          int dot01 = v0x * v1x + v0y * v1y; \
          int dot02 = v0x * v2x + v0y * v2y; \
          int dot11 = v1x * v1x + v1y * v1y; \
          int dot12 = v1x * v2x + v1y * v2y; \
          int denom = dot00 * dot11 - dot01 * dot01; \
          int res = 0; \
          if (denom != 0) { \
            float invDen = 1.0f / (float)denom; \
            float u = (dot11 * dot02 - dot01 * dot12) * invDen; \
            float v = (dot00 * dot12 - dot01 * dot02) * invDen; \
            res = (u >= 0.0f) && (v >= 0.0f) && (u + v <= 1.0f); \
          } \
          res; })

        bool hit = IN_TRI(mx, my, top0, top1, top2) || IN_TRI(mx, my, top0, top2, top3);
        if (!hit)
        {
          SDL_Point R0 = {sx + half_tw, sy};
          SDL_Point R1 = {sx + half_tw, sy + vh};
          SDL_Point R2 = {sx, sy + half_th + vh};
          SDL_Point R3 = {sx, sy + half_th};
          SDL_Point L0 = {sx - half_tw, sy};
          SDL_Point L1 = {sx, sy + half_th};
          SDL_Point L2 = {sx, sy + half_th + vh};
          SDL_Point L3 = {sx - half_tw, sy + vh};
          if (IN_TRI(mx, my, R0, R1, R2) || IN_TRI(mx, my, R0, R2, R3) ||
              IN_TRI(mx, my, L0, L1, L2) || IN_TRI(mx, my, L0, L2, L3))
          {
            hit = true;
          }
        }
        if (hit)
        {
          any_hit = true;
          pick_x = x;
          pick_y = y;
          pick_z = (uint32_t)z0;
        }
      }
    }
  }

  if (any_hit)
  {
    if (out_x)
      *out_x = pick_x;
    if (out_y)
      *out_y = pick_y;
    if (out_z)
      *out_z = pick_z;
    return true;
  }
  return false;
}

// Add voxel to render buffer with distance-based prioritization
void isometric_renderer_add_voxel(IsometricRenderer *renderer,
                                  int world_x, int world_y, int world_z,
                                  int world_index,
                                  VoxelType type,
                                  bool visible_faces[6])
{
  // debug removed

  // Calculate distance from camera for prioritization
  WorldOffset offset = renderer->world_offsets[world_index];
  int abs_x = world_x + offset.dx * WORLD_SIZE_X;
  int abs_y = world_y + offset.dy * WORLD_SIZE_Y;
  int abs_z = world_z + offset.dz * WORLD_SIZE_Z;

  (void)offset;
  (void)abs_x;
  (void)abs_y;
  (void)abs_z;

  int dx = abs_x - renderer->camera_x;
  int dy = abs_y - renderer->camera_y;
  int dz = abs_z - renderer->camera_z;
  int distance_sq = dx * dx + dy * dy + dz * dz;

  (void)distance_sq;

  // Ensure capacity: grow dynamically if needed
  if (renderer->render_buffer_size >= renderer->render_buffer_capacity)
  {
    int new_cap = renderer->render_buffer_capacity * 2;
    if (new_cap < renderer->render_buffer_capacity + 1024)
      new_cap = renderer->render_buffer_capacity + 1024;
    VoxelRenderData *nb = (VoxelRenderData *)realloc(renderer->render_buffer, (size_t)new_cap * sizeof(VoxelRenderData));
    if (nb)
    {
      renderer->render_buffer = nb;
      renderer->render_buffer_capacity = new_cap;
    }
    else
    {
      // If allocation fails, drop this voxel
      return;
    }
  }

  VoxelRenderData *voxel = &renderer->render_buffer[renderer->render_buffer_size];

  voxel->world_x = world_x;
  voxel->world_y = world_y;
  voxel->world_z = world_z;
  voxel->world_index = world_index;
  voxel->type = type;
  voxel->damage = 0;
  voxel->fog_hidden = false;
  voxel->color = isometric_get_voxel_color(type);
  {
    World *fog_world = iso_world_for_index(renderer, world_index);
    if (renderer->fog && fog_world && world_x >= 0 && world_y >= 0 && world_z >= 0 &&
        !fog_is_explored(renderer->fog, fog_world, (uint32_t)world_x, (uint32_t)world_y,
                         (uint32_t)world_z))
    {
      voxel->fog_hidden = true;
      voxel->color = (SDL_Color){0, 0, 0, 255};
    }
  }
  // Apply per-type layer texture color if available (tile by 32x32). Skip when fog-hidden so
  // unexplored cells stay solid black instead of leaking atlas colours.
  if (!voxel->fog_hidden && type >= 0 && type < VOXEL_COUNT)
  {
    SDL_Color *tpix = renderer->per_type_layer_pixels[type];
    int tw = renderer->per_type_layer_w[type];
    int th = renderer->per_type_layer_h[type];
    if (tpix && tw > 0 && th > 0)
    {
      int tx = world_x % tw;
      if (tx < 0)
        tx += tw;
      int ty = world_y % th;
      if (ty < 0)
        ty += th;
      voxel->color = tpix[ty * tw + tx];
    }
  }

  // If layer texture override is active and this looks like a 32x32x1 layer, override the color
  if (!voxel->fog_hidden && renderer->layer_texture_override_active && renderer->layer_tex_pixels &&
      renderer->layer_tex_w > 0 && renderer->layer_tex_h > 0)
  {
    if (world_index == 0 && renderer->game_worlds && renderer->game_worlds->home_world)
    {
      World *w = renderer->game_worlds->home_world;
      if (w->depth == 1 && w->width == (uint32_t)renderer->layer_tex_w && w->height == (uint32_t)renderer->layer_tex_h)
      {
        int idx = world_y * renderer->layer_tex_w + world_x;
        if (idx >= 0 && idx < renderer->layer_tex_w * renderer->layer_tex_h)
        {
          voxel->color = renderer->layer_tex_pixels[idx];
        }
      }
    }
  }

  // Pull damage from the voxel's world so crack overlays can find it for any neighbour.
  {
    World *dmg_world = iso_world_for_index(renderer, world_index);
    if (dmg_world && world_pos_in_bounds_fast(dmg_world, world_x, world_y, world_z))
    {
      Voxel *dv = world_voxel_ptr_fast(dmg_world, world_x, world_y, world_z);
      if (dv)
        voxel->damage = voxel_get_damage(dv);
    }
  }

  // Apply wetness-based blue shift if present
  World *world = renderer->game_worlds ? renderer->game_worlds->home_world : NULL;
  if (world)
  {
    // Map world_index back to absolute coordinates for edge worlds
    WorldOffset offset = renderer->world_offsets[world_index];
    int wx = world_x + offset.dx * WORLD_SIZE_X;
    int wy = world_y + offset.dy * WORLD_SIZE_Y;
    int wz = world_z + offset.dz * WORLD_SIZE_Z;
    (void)wx;
    (void)wy;
    (void)wz;
    // If we only have a single world pointer, assume 0 index is the main world
    if (world_index == 0)
    {
      Voxel *vv = world_pos_in_bounds_fast(world, world_x, world_y, world_z) ? world_voxel_ptr_fast(world, world_x, world_y, world_z) : NULL;
      if (vv)
      {
        uint8_t wet = voxel_get_quantity_or_wetness(vv);
        if (wet > 0 && wet <= 6)
        {
          float t = (wet >= 6 ? 1.0f : (float)wet / 6.0f);
          // Shift toward blue by increasing B and slightly reducing R/G
          int r0 = voxel->color.r;
          int g0 = voxel->color.g;
          int b0 = voxel->color.b;
          int r1 = (int)(r0 * (1.0f - 0.35f * t));
          int g1 = (int)(g0 * (1.0f - 0.20f * t));
          int b1 = (int)(b0 + (255 - b0) * 0.50f * t);
          if (r1 < 0)
            r1 = 0;
          if (r1 > 255)
            r1 = 255;
          if (g1 < 0)
            g1 = 0;
          if (g1 > 255)
            g1 = 255;
          if (b1 < 0)
            b1 = 0;
          if (b1 > 255)
            b1 = 255;
          voxel->color.r = (Uint8)r1;
          voxel->color.g = (Uint8)g1;
          voxel->color.b = (Uint8)b1;
        }
      }
    }
  }

  voxel->color.a = isometric_apply_xray_alpha(renderer, type, voxel->color.a);

  // debug removed

  // Calculate screen position
  isometric_world_to_screen(renderer, world_x, world_y, world_z, world_index,
                            &voxel->screen_x, &voxel->screen_y);

  // debug removed

  // Depth sorting key: weight vertical more so higher Z draws later (in front)
  voxel->depth = abs_x + abs_y + (abs_z * 2);

  // debug removed

  // Copy visible faces
  memcpy(voxel->visible_faces, visible_faces, sizeof(bool) * 6);

  // debug removed

  renderer->render_buffer_size++;
  // debug removed
}

// Comparison function for sorting
static int voxel_depth_compare(const void *a, const void *b)
{
  const VoxelRenderData *va = (const VoxelRenderData *)a;
  const VoxelRenderData *vb = (const VoxelRenderData *)b;
  // Stable ascending order to emulate world_viewer painter's pass (y, then x, z ascending)
  if (vb->depth == va->depth)
    return 0;
  return (va->depth < vb->depth) ? -1 : 1;
}

// Sort render buffer by depth
void isometric_renderer_sort_buffer(IsometricRenderer *renderer)
{
  qsort(renderer->render_buffer, renderer->render_buffer_size,
        sizeof(VoxelRenderData), voxel_depth_compare);
}

// Helper function to fill a triangle using improved scanline with better precision
static void fill_triangle(SDL_Renderer *sdl_renderer, SDL_Point p1, SDL_Point p2, SDL_Point p3)
{
  // Sort points by Y coordinate (p1.y <= p2.y <= p3.y)
  if (p1.y > p2.y)
  {
    SDL_Point temp = p1;
    p1 = p2;
    p2 = temp;
  }
  if (p2.y > p3.y)
  {
    SDL_Point temp = p2;
    p2 = p3;
    p3 = temp;
  }
  if (p1.y > p2.y)
  {
    SDL_Point temp = p1;
    p1 = p2;
    p2 = temp;
  }

  // Handle degenerate triangle
  if (p1.y == p3.y)
    return;

  // Fill the triangle using horizontal lines with improved precision
  for (int y = p1.y; y <= p3.y; y++)
  {
    float x_start, x_end;

    if (y <= p2.y)
    {
      // Upper part of triangle
      if (p2.y == p1.y)
      {
        x_start = p1.x < p2.x ? (float)p1.x : (float)p2.x;
        x_end = p1.x > p2.x ? (float)p1.x : (float)p2.x;
      }
      else
      {
        float t1 = (float)(y - p1.y) / (float)(p2.y - p1.y);
        float t2 = (float)(y - p1.y) / (float)(p3.y - p1.y);
        x_start = (float)p1.x + t1 * (float)(p2.x - p1.x);
        x_end = (float)p1.x + t2 * (float)(p3.x - p1.x);
      }
    }
    else
    {
      // Lower part of triangle
      float t1 = (float)(y - p2.y) / (float)(p3.y - p2.y);
      float t2 = (float)(y - p1.y) / (float)(p3.y - p1.y);
      x_start = (float)p2.x + t1 * (float)(p3.x - p2.x);
      x_end = (float)p1.x + t2 * (float)(p3.x - p1.x);
    }

    if (x_start > x_end)
    {
      float temp = x_start;
      x_start = x_end;
      x_end = temp;
    }

    // Use proper rounding to avoid gaps
    int start_pixel = (int)(x_start + 0.5f);
    int end_pixel = (int)(x_end + 0.5f);

    if (start_pixel <= end_pixel)
    {
      SDL_RenderDrawLine(sdl_renderer, start_pixel, y, end_pixel, y);
    }
  }
}

// Helper function to fill a quad using two triangles
static void fill_quad(SDL_Renderer *sdl_renderer, SDL_Point quad[4])
{
  // Use the other diagonal to avoid potential issues with parallelogram geometry
  fill_triangle(sdl_renderer, quad[0], quad[1], quad[3]);
  fill_triangle(sdl_renderer, quad[1], quad[2], quad[3]);
}

// Draw a single voxel
void isometric_renderer_draw_voxel(IsometricRenderer *renderer,
                                   SDL_Renderer *sdl_renderer,
                                   VoxelRenderData *voxel)
{
  // debug removed

  int x = voxel->screen_x;
  int y = voxel->screen_y;
  int tw = renderer->tile_width;
  int th = renderer->tile_height;
  int vh = renderer->voxel_height;

  (void)tw;
  (void)th;
  (void)vh;

  // Define proper isometric face vertices
  SDL_Point top_face[4] = {
      {x, y - vh},                   // Top
      {x + tw / 2, y - vh + th / 2}, // Right
      {x, y - vh + th},              // Bottom
      {x - tw / 2, y - vh + th / 2}  // Left
  };

  SDL_Point right_face[4] = {
      {x + tw / 2, y - vh + th / 2}, // Top right
      {x + tw / 2, y + th / 2},      // Bottom right
      {x, y + th},                   // Bottom center
      {x, y - vh + th}               // Top center
  };

  SDL_Point left_face[4] = {
      {x - tw / 2, y - vh + th / 2}, // Top left
      {x, y - vh + th},              // Top center
      {x, y + th},                   // Bottom center
      {x - tw / 2, y + th / 2}       // Bottom left
  };

  // debug removed

  // Note: Front and back faces removed - not visible in proper isometric view

  // Draw faces with appropriate shading and per-world alpha
  SDL_Color color = voxel->color;
  Uint8 base_a = color.a;
  float alpha_scale = 1.0f;
  if (voxel->world_index >= 0 && voxel->world_index < 125)
  {
    alpha_scale = renderer->world_alpha[voxel->world_index];
  }
  color.a = (Uint8)((float)base_a * alpha_scale);
  color.a = isometric_apply_xray_alpha(renderer, voxel->type, color.a);
  // debug removed

  const bool wire = renderer->wireframe_mode;

  // Top face (brightest) - draw as filled diamond using triangles
  if (voxel->visible_faces[0])
  {
    SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);

    if (!wire)
    {
      // Fill the diamond shape with horizontal lines
      for (int dy = 0; dy < th; dy++)
      {
        int y_pos = y - vh + dy;
        int left_x, right_x;

        if (dy < th / 2)
        {
          // Upper half of diamond
          left_x = x - (tw / 2) * dy / (th / 2);
          right_x = x + (tw / 2) * dy / (th / 2);
        }
        else
        {
          // Lower half of diamond
          int dy2 = th - dy;
          left_x = x - (tw / 2) * dy2 / (th / 2);
          right_x = x + (tw / 2) * dy2 / (th / 2);
        }

        SDL_RenderDrawLine(sdl_renderer, left_x, y_pos, right_x, y_pos);
      }
    }
  }

  // Right face (medium) - filled parallelogram using triangles
  if (voxel->visible_faces[3])
  {
    SDL_SetRenderDrawColor(sdl_renderer,
                           (Uint8)(color.r * 0.8f), (Uint8)(color.g * 0.8f), (Uint8)(color.b * 0.8f), color.a);
    if (!wire)
      fill_quad(sdl_renderer, right_face);
  }

  // Left face (darkest) - filled parallelogram using triangles
  if (voxel->visible_faces[2])
  {
    SDL_SetRenderDrawColor(sdl_renderer,
                           (Uint8)(color.r * 0.6f), (Uint8)(color.g * 0.6f), (Uint8)(color.b * 0.6f), color.a);
    if (!wire)
      fill_quad(sdl_renderer, left_face);
  }

  // Note: Front and back faces not rendered in isometric view

  // Draw edges — always in wireframe; otherwise only for origin world
  if (wire || voxel->world_index == 0)
  {
    SDL_SetRenderDrawColor(sdl_renderer, wire ? color.r : 0, wire ? color.g : 0,
                           wire ? color.b : 0, wire ? 220 : 128);
    if (voxel->visible_faces[0])
    {
      SDL_RenderDrawLines(sdl_renderer, top_face, 4);
      SDL_RenderDrawLine(sdl_renderer, top_face[3].x, top_face[3].y, top_face[0].x, top_face[0].y);
    }
    if (voxel->visible_faces[3])
    {
      SDL_RenderDrawLines(sdl_renderer, right_face, 4);
      SDL_RenderDrawLine(sdl_renderer, right_face[3].x, right_face[3].y, right_face[0].x, right_face[0].y);
    }
    if (voxel->visible_faces[2])
    {
      SDL_RenderDrawLines(sdl_renderer, left_face, 4);
      SDL_RenderDrawLine(sdl_renderer, left_face[3].x, left_face[3].y, left_face[0].x, left_face[0].y);
    }
  }
  // Note: Front and back face edges not drawn in isometric view

  // Check if this voxel is highlighted
  if (renderer->has_highlighted_voxel &&
      voxel->world_x == renderer->highlighted_x &&
      voxel->world_y == renderer->highlighted_y &&
      voxel->world_z == renderer->highlighted_z)
  {
    // Draw solid white all-edge wireframe around the voxel, independent of visible faces
    SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);

    // Reconstruct top and base diamond vertices consistent with this function's geometry
    int a = tw / 2; // half width
    int b = th / 2; // half height

    SDL_Point top_pts[4] = {
        {x, y - b},        // top
        {x + a, y},        // right
        {x, y + b},        // bottom
        {x - a, y}         // left
    };

    SDL_Point base_pts[4] = {
        {x,     y + vh - b},
        {x + a, y + vh    },
        {x,     y + vh + b},
        {x - a, y + vh    }
    };

    // Helper to draw a 2px-thick segment
    #define DRAW2(p0, p1) \
      SDL_RenderDrawLine(sdl_renderer, (p0).x, (p0).y, (p1).x, (p1).y); \
      SDL_RenderDrawLine(sdl_renderer, (p0).x+1, (p0).y+1, (p1).x+1, (p1).y+1)

    // Top diamond edges
    DRAW2(top_pts[0], top_pts[1]);
    DRAW2(top_pts[1], top_pts[2]);
    DRAW2(top_pts[2], top_pts[3]);
    DRAW2(top_pts[3], top_pts[0]);

    // Base diamond edges
    DRAW2(base_pts[0], base_pts[1]);
    DRAW2(base_pts[1], base_pts[2]);
    DRAW2(base_pts[2], base_pts[3]);
    DRAW2(base_pts[3], base_pts[0]);

    // Vertical edges between corresponding corners
    DRAW2(top_pts[0], base_pts[0]);
    DRAW2(top_pts[1], base_pts[1]);
    DRAW2(top_pts[2], base_pts[2]);
    DRAW2(top_pts[3], base_pts[3]);

    #undef DRAW2
  }
}

// Comprehensive voxel rendering fix - draw proper isometric cube using top/left/right faces
void isometric_renderer_draw_voxel_comprehensive(IsometricRenderer *renderer,
                                                 SDL_Renderer *sdl_renderer,
                                                 VoxelRenderData *voxel)
{
  int x = voxel->screen_x;
  int y = voxel->screen_y;
  const int tw = renderer->tile_width;
  const int th = renderer->tile_height;
  const int vh = renderer->voxel_height;
  const int a = tw / 2; // half width
  const int b = th / 2; // half height

  // Precompute key points for top and base diamonds
  // y is the top-face center from world_to_screen
  SDL_Point top_pts[4] = {
      {x, y - b}, // top
      {x + a, y}, // right
      {x, y + b}, // bottom
      {x - a, y}  // left
  };

  // Base points (underside) are below the top face by voxel height
  SDL_Point base_pts[4] = {
      {x, y + vh - b},
      {x + a, y + vh},
      {x, y + vh + b},
      {x - a, y + vh}};

  // Faces: 0=top, 2=left, 3=right (consistent with visibility indices)
  SDL_Color base_color = voxel->color;

  // Right face (slightly brighter than left). Draw after bottom, but before top.
  if (voxel->visible_faces[3])
  {
    SDL_Point right_face[4] = {
        top_pts[1],  // top-right
        base_pts[1], // base-right
        base_pts[2], // base-bottom
        top_pts[2]   // top-bottom
    };
    SDL_Color c = (SDL_Color){(Uint8)(base_color.r * 0.75f), (Uint8)(base_color.g * 0.75f), (Uint8)(base_color.b * 0.75f), base_color.a};
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
    // Fill with two triangles (0,1,2) and (0,2,3)
    fill_triangle(sdl_renderer, right_face[0], right_face[1], right_face[2]);
    fill_triangle(sdl_renderer, right_face[0], right_face[2], right_face[3]);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 160);
    SDL_RenderDrawLines(sdl_renderer, right_face, 4);
    SDL_RenderDrawLine(sdl_renderer, right_face[3].x, right_face[3].y, right_face[0].x, right_face[0].y);
  }

  // Left face (darker). Draw after right.
  if (voxel->visible_faces[2])
  {
    SDL_Point left_face[4] = {
        top_pts[3],  // top-left
        top_pts[2],  // top-bottom
        base_pts[2], // base-bottom
        base_pts[3]  // base-left
    };
    SDL_Color c = (SDL_Color){(Uint8)(base_color.r * 0.55f), (Uint8)(base_color.g * 0.55f), (Uint8)(base_color.b * 0.55f), base_color.a};
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
    fill_triangle(sdl_renderer, left_face[0], left_face[1], left_face[2]);
    fill_triangle(sdl_renderer, left_face[0], left_face[2], left_face[3]);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 160);
    SDL_RenderDrawLines(sdl_renderer, left_face, 4);
    SDL_RenderDrawLine(sdl_renderer, left_face[3].x, left_face[3].y, left_face[0].x, left_face[0].y);
  }

  // Top face (brightest)
  if (voxel->visible_faces[0])
  {
    SDL_Point top_face[4] = {top_pts[0], top_pts[1], top_pts[2], top_pts[3]};
    SDL_Color c = base_color;
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
    fill_triangle(sdl_renderer, top_face[0], top_face[1], top_face[2]);
    fill_triangle(sdl_renderer, top_face[0], top_face[2], top_face[3]);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 140);
    SDL_RenderDrawLines(sdl_renderer, top_face, 4);
    SDL_RenderDrawLine(sdl_renderer, top_face[3].x, top_face[3].y, top_face[0].x, top_face[0].y);
  }

  // Bottom face (underside) - very dark; draw before sides to prevent overdraw over higher Z
  if (voxel->visible_faces[1])
  {
    SDL_Point bottom_face[4] = {base_pts[0], base_pts[1], base_pts[2], base_pts[3]};
    SDL_Color c = (SDL_Color){(Uint8)(base_color.r * 0.35f), (Uint8)(base_color.g * 0.35f), (Uint8)(base_color.b * 0.35f), base_color.a};
    SDL_SetRenderDrawColor(sdl_renderer, c.r, c.g, c.b, c.a);
    fill_triangle(sdl_renderer, bottom_face[0], bottom_face[1], bottom_face[2]);
    fill_triangle(sdl_renderer, bottom_face[0], bottom_face[2], bottom_face[3]);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 160);
    SDL_RenderDrawLines(sdl_renderer, bottom_face, 4);
    SDL_RenderDrawLine(sdl_renderer, bottom_face[3].x, bottom_face[3].y, bottom_face[0].x, bottom_face[0].y);
  }
  // Optional highlight omitted in compact draw
  // All-edge highlight wireframe for selected voxel (independent of visible faces)
  if (renderer->has_highlighted_voxel &&
      voxel->world_x == renderer->highlighted_x &&
      voxel->world_y == renderer->highlighted_y &&
      voxel->world_z == renderer->highlighted_z)
  {
    // Reuse top/base points computed above
    SDL_Point top_pts[4] = {
        {x, y - b},        // top
        {x + a, y},        // right
        {x, y + b},        // bottom
        {x - a, y}         // left
    };
    SDL_Point base_pts[4] = {
        {x,     y + vh - b},
        {x + a, y + vh    },
        {x,     y + vh + b},
        {x - a, y + vh    }
    };

    SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
    #define DRAW2(p0, p1) \
      SDL_RenderDrawLine(sdl_renderer, (p0).x, (p0).y, (p1).x, (p1).y); \
      SDL_RenderDrawLine(sdl_renderer, (p0).x+1, (p0).y+1, (p1).x+1, (p1).y+1)

    // Top diamond
    DRAW2(top_pts[0], top_pts[1]);
    DRAW2(top_pts[1], top_pts[2]);
    DRAW2(top_pts[2], top_pts[3]);
    DRAW2(top_pts[3], top_pts[0]);
    // Base diamond
    DRAW2(base_pts[0], base_pts[1]);
    DRAW2(base_pts[1], base_pts[2]);
    DRAW2(base_pts[2], base_pts[3]);
    DRAW2(base_pts[3], base_pts[0]);
    // Verticals
    DRAW2(top_pts[0], base_pts[0]);
    DRAW2(top_pts[1], base_pts[1]);
    DRAW2(top_pts[2], base_pts[2]);
    DRAW2(top_pts[3], base_pts[3]);

    #undef DRAW2
  }
}

// Render a single world
void isometric_renderer_render_world(IsometricRenderer *renderer,
                                     World *world,
                                     int world_index,
                                     int offset_x, int offset_y, int offset_z)
{
  if (!world)
  {
    return;
  }

  // Camera position in this world (accounting for world offset)
  int cam_x = renderer->camera_x - offset_x;
  int cam_y = renderer->camera_y - offset_y;
  int cam_z = renderer->camera_z - offset_z;

  // Render full world extents (no distance culling), but skip voxels completely hidden behind neighbors toward the camera
  int min_x = 0;
  int max_x = (int)world->width;
  int min_y = 0;
  int max_y = (int)world->height;
  int min_z = 0;
  // Compute camera Z relative to this world's vertical offset so projected heights are correct
  int local_view_z = renderer->camera_z - offset_z;
  int view_z_clamped = local_view_z;
  if (view_z_clamped < 0)
    view_z_clamped = 0;
  if (view_z_clamped >= (int)world->depth)
    view_z_clamped = (int)world->depth - 1;
  // Render the full occupied depth. view_z_clamped is only for vertical screen projection
  // (sy offset), not for excluding layers above the camera — the player must see overhead
  // voxels when flying or standing beneath terrain.
  int max_z = (int)world->depth;

  // Resolve the occupancy bitfield once instead of revalidating it for every voxel in the scan.
  //
  // It answers "is this cell solid?" out of one bit rather than a 48-byte Voxel, which at 128^3 is
  // a 256KB working set against 96MB. That matters twice over: the scan visits far more air than
  // solid, and the air test was the thing pulling the voxel array through the cache.
  const uint8_t *occ_bits = NULL;
  if (world->occupancy_bits && world->occupancy_bits->bits &&
      world->occupancy_bits->width == world->width &&
      world->occupancy_bits->height == world->height &&
      world->occupancy_bits->depth == world->depth)
    occ_bits = world->occupancy_bits->bits;
  const uint32_t occ_w = world->width;
  const uint32_t occ_h = world->height;
  const uint32_t occ_d = world->depth;

  // Narrow the z sweep to the layers that hold anything. The home island floats, so without this
  // the scan walks every layer of empty sky beneath it before reaching the first voxel.
  if (world->occupied_z_max >= 0)
  {
    if (world->occupied_z_min > min_z)
      min_z = world->occupied_z_min;
    if (world->occupied_z_max + 1 < max_z)
      max_z = world->occupied_z_max + 1;
  }
  if (min_z >= max_z)
    return; // nothing in this world can be on screen

  // Iterate chunks with coarse screen-space culling
  const int cx_count = (int)((world->width + ISO_CHUNK_SIZE_X - 1) / ISO_CHUNK_SIZE_X);
  const int cy_count = (int)((world->height + ISO_CHUNK_SIZE_Y - 1) / ISO_CHUNK_SIZE_Y);
  const int cz_count = (int)((max_z + ISO_CHUNK_SIZE_Z - 1) / ISO_CHUNK_SIZE_Z);

  for (int cz = 0; cz < cz_count; cz++)
  {
    int z0 = cz * ISO_CHUNK_SIZE_Z;
    int z1 = z0 + ISO_CHUNK_SIZE_Z;
    if (z0 < min_z)
      z0 = min_z;
    if (z1 > max_z)
      z1 = max_z;
    if (z0 >= z1)
      continue;

    for (int cy = 0; cy < cy_count; cy++)
    {
      int y0 = cy * ISO_CHUNK_SIZE_Y;
      int y1 = y0 + ISO_CHUNK_SIZE_Y;
      if (y0 < min_y)
        y0 = min_y;
      if (y1 > max_y)
        y1 = max_y;
      if (y0 >= y1)
        continue;

      for (int cx = 0; cx < cx_count; cx++)
      {
        int x0 = cx * ISO_CHUNK_SIZE_X;
        int x1 = x0 + ISO_CHUNK_SIZE_X;
        if (x0 < min_x)
          x0 = min_x;
        if (x1 > max_x)
          x1 = max_x;
        if (x0 >= x1)
          continue;

        // Screen-space AABB must match the GPU draw lattice: absolute world coords with
        // screen_center already baking camera XY (see isometric_renderer_render_gpu).
        // Subtracting camera_world_* here double-applies the pan and culls on-screen chunks.
        int half_tw = renderer->tile_width / 2;
        int half_th = renderer->tile_height / 2;
        int vh = renderer->voxel_height;

        int cx0 = renderer->screen_center_x;
        int cy0 = renderer->screen_center_y;

        // Conservative screen bounds
        int sx_min = INT_MAX, sy_min = INT_MAX, sx_max = INT_MIN, sy_max = INT_MIN;
        int zs[2] = {z0, z1 - 1};
        for (int zi = 0; zi < 2; zi++)
        {
          int zz = zs[zi];
          int pts[4][2] = {
              {x0, y0}, {x1 - 1, y0}, {x1 - 1, y1 - 1}, {x0, y1 - 1}};
          for (int p = 0; p < 4; p++)
          {
            const int abs_x = pts[p][0] + offset_x;
            const int abs_y = pts[p][1] + offset_y;
            const int abs_z = zz + offset_z;
            const int sx = cx0 + (abs_x - abs_y) * half_tw;
            const int sy = cy0 + (abs_x + abs_y) * half_th + (view_z_clamped - abs_z) * vh;
            if (sx < sx_min)
              sx_min = sx;
            if (sy < sy_min)
              sy_min = sy;
            if (sx > sx_max)
              sx_max = sx;
            if (sy > sy_max)
              sy_max = sy;
          }
        }

        // Expand a bit to include diamond faces / voxel height
        sx_min -= half_tw;
        sx_max += half_tw;
        sy_min -= vh + half_th;
        sy_max += vh + half_th;

        // If the chunk's AABB is completely outside the viewport, skip it (unless culling disabled)
        if (!renderer->disable_culling && (sx_max < 0 || sy_max < 0 || sx_min > renderer->screen_width || sy_min > renderer->screen_height))
        {
          continue;
        }

        // Far LOD ring (Devlog #9/#16): beyond ~8 chunks, only the surface heightmap top faces.
        // Applies to centre and neighbour worlds alike — skipping it for the closest ring ballooned
        // the iso neighbor scan past a frame at 120 FPS.
        {
          const int chunk_mx = (x0 + x1) / 2;
          const int chunk_my = (y0 + y1) / 2;
          const int cdx = abs(chunk_mx + offset_x - renderer->camera_x);
          const int cdy = abs(chunk_my + offset_y - renderer->camera_y);
          const int cheb = cdx > cdy ? cdx : cdy;
          if (cheb > ISO_CHUNK_SIZE_X * 8)
          {
            for (int y = y0; y < y1; y++)
            {
              for (int x = x0; x < x1; x++)
              {
                if (!world_pos_in_bounds_fast(world, x, y, 0))
                  continue;
                const int hz = world_height_at_fast(world, x, y);
                if (hz < 0 || hz < z0 || hz >= z1)
                  continue;
                Voxel *voxel = world_voxel_ptr_fast(world, x, y, hz);
                if (!voxel || voxel->type == VOXEL_AIR)
                  continue;
                bool visible_faces[6] = {true, false, false, false, false, false};
                isometric_renderer_add_voxel(renderer, x, y, hz, world_index, voxel->type,
                                             visible_faces);
              }
            }
            continue;
          }
        }

        // Iterate voxels inside chunk
        for (int z = z0; z < z1; z++)
        {
          int xs = x0, xe = x1, ys = y0, ye = y1; // Default to full chunk bounds

          // Aggressive on-screen culling for the center world only. Neighbor worlds (especially
          // vertically stacked cloud layers) use different absolute Z, so the local-space UV
          // window would clip them incorrectly — the chunk AABB above already covers them.
          if (!renderer->disable_culling && offset_x == 0 && offset_y == 0 && offset_z == 0)
          {
            int dz_pixels = (view_z_clamped - z) * vh;
            double u_min_f = ((double)0 - (double)cx0) / (double)half_tw;
            double u_max_f = ((double)renderer->screen_width - (double)cx0) / (double)half_tw;
            if (u_min_f > u_max_f)
            {
              double t = u_min_f;
              u_min_f = u_max_f;
              u_max_f = t;
            }
            int u_min_i = (int)floor(u_min_f) - 1;
            int u_max_i = (int)ceil(u_max_f) + 1;
            double v_min_f = ((double)0 - (double)cy0 - (double)dz_pixels) / (double)half_th;
            double v_max_f = ((double)renderer->screen_height - (double)cy0 - (double)dz_pixels) / (double)half_th;
            if (v_min_f > v_max_f)
            {
              double t = v_min_f;
              v_min_f = v_max_f;
              v_max_f = t;
            }
            int v_min_i = (int)floor(v_min_f) - 1;
            int v_max_i = (int)ceil(v_max_f) + 1;

            // Convert (u,v) ranges to conservative integer x,y windows
            // x in [ceil((u_min+v_min)/2), floor((u_max+v_max)/2)]
            int x_min_from_uv = (int)ceil(((double)u_min_i + (double)v_min_i) * 0.5);
            int x_max_from_uv = (int)floor(((double)u_max_i + (double)v_max_i) * 0.5);
            if (x_min_from_uv > xs)
              xs = x_min_from_uv;
            if (x_max_from_uv + 1 < xe)
              xe = x_max_from_uv + 1;
            // y in [ceil((v_min - u_max)/2), floor((v_max - u_min)/2)]
            int y_min_from_uv = (int)ceil(((double)v_min_i - (double)u_max_i) * 0.5);
            int y_max_from_uv = (int)floor(((double)v_max_i - (double)u_min_i) * 0.5);
            if (y_min_from_uv > ys)
              ys = y_min_from_uv;
            if (y_max_from_uv + 1 < ye)
              ye = y_max_from_uv + 1;

            if (xs >= xe || ys >= ye)
              continue; // nothing visible at this z
          }

          for (int y = ys; y < ye; y++)
          {
            for (int x = xs; x < xe; x++)
            {
              renderer->scan_visited++;

              (void)cam_x;
              (void)cam_y;
              (void)cam_z; // unused in full render mode

              if (!world_pos_in_bounds_fast(world, x, y, z))
              {
                continue;
              }

              // Reject air from the bitfield when it is available, so the common case never
              // touches the voxel array at all.
              if (occ_bits)
              {
                const uint32_t lin = ((uint32_t)z * occ_h + (uint32_t)y) * occ_w + (uint32_t)x;
                if (((occ_bits[lin >> 3u] >> (lin & 7u)) & 1u) == 0u)
                {
                  renderer->scan_air++;
                  continue;
                }
              }

              renderer->scan_solid++;

              bool visible_faces[6] = {false};
              Voxel *voxel = NULL;
              // Prefer bitfield-based visibility when available (avoids neighbor reads)
              if (occ_bits)
              {
                // Visible if adjacent cell is empty in the solid-occupancy bitfield.
                //
                // Do NOT force top faces visible from camera_z: that was a slice-view trick
                // that, once full-depth rendering was enabled, marked every buried cell at or
                // above the camera as drawable and froze the client on solid/scoured worlds.
                //
                // Only the four faces some draw path actually reads are computed. Front (-Y) and
                // back (-X) are never drawn in the isometric view — nothing reads visible_faces[4]
                // or [5] — so probing for them was two lookups per solid voxel spent on an answer
                // no one asks for.
                visible_faces[0] = iso_face_exposed_occ(world, occ_bits, occ_w, occ_h, occ_d, x, y, z + 1); // top
                visible_faces[1] = iso_face_exposed_occ(world, occ_bits, occ_w, occ_h, occ_d, x, y, z - 1); // bottom
                visible_faces[2] = iso_face_exposed_occ(world, occ_bits, occ_w, occ_h, occ_d, x, y + 1, z); // left (+Y)
                visible_faces[3] = iso_face_exposed_occ(world, occ_bits, occ_w, occ_h, occ_d, x + 1, y, z); // right (+X)
              }
              else
              {
                // Without a bitfield the neighbour tests read the voxel array anyway, so there is
                // nothing to defer.
                voxel = world_voxel_ptr_fast(world, x, y, z);
                if (voxel->type == VOXEL_AIR)
                {
                  renderer->scan_solid--;
                  renderer->scan_air++;
                  continue;
                }
                for (int face = 0; face < 6; face++)
                {
                  visible_faces[face] = isometric_should_render_face(renderer, world, x, y, z, face);
                }
              }
              // Only consider faces we actually render in the isometric view: top, bottom, left, right
              bool renderable_visible = (visible_faces[0] || visible_faces[1] || visible_faces[2] || visible_faces[3]);
              if (renderable_visible)
              {
                // The voxel array is read here and nowhere earlier, which is the point: on a solid
                // island most cells are buried, and deciding that from the bitfield costs four bit
                // tests against a 256KB working set instead of dragging a 48-byte Voxel through the
                // cache for a voxel that will never be drawn.
                if (!voxel)
                {
                  voxel = world_voxel_ptr_fast(world, x, y, z);
                  // The bitfield can lag the voxel array for a frame after a write, so confirm.
                  if (voxel->type == VOXEL_AIR)
                  {
                    renderer->scan_solid--;
                    renderer->scan_air++;
                    continue;
                  }
                }
                isometric_renderer_add_voxel(renderer, x, y, z, world_index, voxel->type, visible_faces);
                renderer->scan_emitted++;
                if (renderer->render_buffer_size >= renderer->render_buffer_capacity)
                {
                  // Expand buffer capacity
                  int new_cap = renderer->render_buffer_capacity * 2;
                  if (new_cap < renderer->render_buffer_capacity + 1024)
                    new_cap = renderer->render_buffer_capacity + 1024;
                  VoxelRenderData *nb = (VoxelRenderData *)realloc(renderer->render_buffer, (size_t)new_cap * sizeof(VoxelRenderData));
                  if (nb)
                  {
                    renderer->render_buffer = nb;
                    renderer->render_buffer_capacity = new_cap;
                  }
                  else
                  {
                    // If allocation fails, skip this voxel but continue rendering
                    continue;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// One named type for the render batch. The two helpers below and the caller's
// buffer each used to declare their own anonymous struct, which made passing the
// buffer between them a formal type mismatch even though the layouts agreed.
typedef struct {
  SDL_Rect rect;
  SDL_Color color;
} RenderBatchEntry;

// Helper functions for batch rendering
static void flush_batch_internal(SDL_Renderer *sdl_renderer,
                                RenderBatchEntry *buffer,
                                int *count) {
  if (*count == 0) return;

  // Group by color for minimal state changes
  for (int i = 0; i < *count; i++) {
    SDL_SetRenderDrawColor(sdl_renderer,
                         buffer[i].color.r,
                         buffer[i].color.g,
                         buffer[i].color.b,
                         buffer[i].color.a);
    SDL_RenderFillRect(sdl_renderer, &buffer[i].rect);
  }
  *count = 0;
}

static void add_to_batch_internal(int x, int y, int w, int h, SDL_Color color,
                                 RenderBatchEntry *buffer,
                                 int *count, int max_count, SDL_Renderer *sdl_renderer) {
  if (*count >= max_count) {
    flush_batch_internal(sdl_renderer, buffer, count);
  }

  buffer[*count].rect.x = x;
  buffer[*count].rect.y = y;
  buffer[*count].rect.w = w;
  buffer[*count].rect.h = h;
  buffer[*count].color = color;
  (*count)++;
}

// Main render function - EXTREMELY OPTIMIZED CPU PATH
void isometric_renderer_render(IsometricRenderer *renderer, SDL_Renderer *sdl_renderer)
{
  // X-ray needs alpha blending so translucent terrain composites correctly.
  if (renderer && renderer->xray_mode && sdl_renderer)
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

  if (!renderer || !sdl_renderer || !renderer->game_worlds || !renderer->game_worlds->home_world)
    return;

  World *w = renderer->game_worlds->home_world;

  // Clear background
  SDL_SetRenderDrawColor(sdl_renderer, 20, 22, 35, 255);
  SDL_RenderClear(sdl_renderer);

  // Early exit for invalid screen dimensions
  int screen_w = renderer->screen_width;
  int screen_h = renderer->screen_height;
  if (screen_w <= 0 || screen_h <= 0)
    return;

  // Calculate optimal cell dimensions for screen fit
  float cell_wf = (float)screen_w / (float)w->width;
  float cell_hf = (float)screen_h / (float)w->height;
  int cell_w = (int)floorf(cell_wf);
  int cell_h = (int)floorf(cell_hf);
  if (cell_w < 2) cell_w = 2;
  if (cell_h < 2) cell_h = 2;

  // Get camera Z slice with bounds checking
  int z = renderer->camera_z;
  if (z < 0) z = 0;
  if (z >= (int)w->depth) z = (int)w->depth - 1;

  // Precompute color lookup table for maximum performance
  static SDL_Color color_table[VOXEL_COUNT];
  static bool color_table_initialized = false;
  if (!color_table_initialized) {
    color_table[VOXEL_AIR] = (SDL_Color){25, 25, 50, 255};
    color_table[VOXEL_GRASS] = (SDL_Color){90, 170, 50, 255};
    color_table[VOXEL_SOIL] = (SDL_Color){120, 80, 40, 255};
    color_table[VOXEL_STONE] = (SDL_Color){110, 110, 110, 255};
    color_table[VOXEL_SAND] = (SDL_Color){194, 178, 128, 255};
    color_table[VOXEL_WATER] = (SDL_Color){50, 100, 200, 255};
    color_table[VOXEL_WOOD] = (SDL_Color){130, 80, 40, 255};
    color_table[VOXEL_LEAVES] = (SDL_Color){60, 140, 60, 255};
    color_table_initialized = true;
  }

  // Use chunk-based rendering with aggressive culling
  const int chunk_size_x = ISO_CHUNK_SIZE_X;
  const int chunk_size_y = ISO_CHUNK_SIZE_Y;
  const int chunk_size_z = ISO_CHUNK_SIZE_Z;

  const int cx_count = (int)((w->width + chunk_size_x - 1) / chunk_size_x);
  const int cy_count = (int)((w->height + chunk_size_y - 1) / chunk_size_y);

  // Precompute screen-space bounds for the current Z slice
  int screen_center_x = screen_w / 2;
  int screen_center_y = screen_h / 2;

  // Calculate world bounds that map to screen
  // Using the isometric projection: sx = center_x + (x - y) * cell_w/2, sy = center_y + (x + y) * cell_h/2
  int world_min_x = 0, world_max_x = (int)w->width - 1;
  int world_min_y = 0, world_max_y = (int)w->height - 1;

  // Expand bounds to ensure we cover the entire screen
  int extra_cells = 2;
  world_min_x = (int)fmaxf(0, world_min_x - extra_cells);
  world_max_x = (int)fminf((int)w->width - 1, world_max_x + extra_cells);
  world_min_y = (int)fmaxf(0, world_min_y - extra_cells);
  world_max_y = (int)fminf((int)w->height - 1, world_max_y + extra_cells);

  // Batch rendering: collect all voxels of the same color before drawing
  #define MAX_BATCH_SIZE 1024
  RenderBatchEntry batch_buffer[MAX_BATCH_SIZE];
  int batch_count = 0;

  // Check if occupancy bitfield is available and valid
  bool use_occupancy_bits = (w->occupancy_bits &&
                            w->occupancy_bits->bits &&
                            w->occupancy_bits->width == w->width &&
                            w->occupancy_bits->height == w->height &&
                            w->occupancy_bits->depth == w->depth);

  // Process chunks with aggressive culling
  for (int cy = 0; cy < cy_count; cy++) {
    int y0 = cy * chunk_size_y;
    int y1 = y0 + chunk_size_y;
    if (y0 < world_min_y) y0 = world_min_y;
    if (y1 > world_max_y) y1 = world_max_y;
    if (y0 >= y1) continue;

    for (int cx = 0; cx < cx_count; cx++) {
      int x0 = cx * chunk_size_x;
      int x1 = x0 + chunk_size_x;
      if (x0 < world_min_x) x0 = world_min_x;
      if (x1 > world_max_x) x1 = world_max_x;
      if (x0 >= x1) continue;

      // Quick chunk visibility test using screen-space bounds
      int sx_min = screen_center_x + ((x0 - y1) * cell_w / 2);
      int sx_max = screen_center_x + ((x1 - y0) * cell_w / 2);
      int sy_min = screen_center_y + ((x0 + y0) * cell_h / 2);
      int sy_max = screen_center_y + ((x1 + y1) * cell_h / 2);

      // Expand bounds slightly to account for cell dimensions
      sx_min -= cell_w;
      sx_max += cell_w;
      sy_min -= cell_h;
      sy_max += cell_h;

      // Skip chunk if completely outside screen (unless culling disabled)
      if (!renderer->disable_culling && (sx_max < 0 || sy_max < 0 || sx_min > screen_w || sy_min > screen_h)) {
        continue;
      }

      // Process voxels in this chunk
      for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
          // Skip if outside world bounds
          if (x < 0 || y < 0 || x >= (int)w->width || y >= (int)w->height) {
            continue;
          }

          // Fast voxel access with bounds checking
          Voxel *voxel = world_pos_in_bounds_fast(w, x, y, z) ?
                         world_voxel_ptr_fast(w, x, y, z) : NULL;

          if (!voxel || voxel->type == VOXEL_AIR) {
            continue;
          }

          // Use precomputed color table
          SDL_Color color = color_table[voxel->type];

          // Calculate screen position
          int screen_x = (int)(x * cell_w);
          int screen_y = (int)(y * cell_h);

          // Add to batch for efficient rendering
          add_to_batch_internal(screen_x, screen_y, cell_w - 1, cell_h - 1, color,
                               batch_buffer, &batch_count, MAX_BATCH_SIZE, sdl_renderer);
        }
      }
    }
  }

  // Flush any remaining batched voxels
  flush_batch_internal(sdl_renderer, batch_buffer, &batch_count);

  // Render grid lines efficiently (only if needed)
  if (renderer->show_grid) {
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 40);

    // Batch grid lines by direction
    for (uint32_t gx = 0; gx <= w->width; gx++) {
      int sx = (int)(gx * cell_w);
      SDL_RenderDrawLine(sdl_renderer, sx, 0, sx, screen_h);
    }
    for (uint32_t gy = 0; gy <= w->height; gy++) {
      int sy = (int)(gy * cell_h);
      SDL_RenderDrawLine(sdl_renderer, 0, sy, screen_w, sy);
    }
  }
}

// GPU-batched render of proper isometric voxels using SDL_RenderGeometry
void isometric_renderer_render_gpu(IsometricRenderer *renderer, SDL_Renderer *sdl_renderer)
{
  if (!renderer || !sdl_renderer || !renderer->game_worlds || !renderer->game_worlds->home_world)
    return;

  // X-ray needs alpha blending so translucent terrain composites correctly.
  if (renderer->xray_mode)
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

  // Wireframe uses the CPU face-outline path; GPU batched fills cannot draw edges alone.
  if (renderer->wireframe_mode)
  {
    isometric_renderer_render(renderer, sdl_renderer);
    return;
  }

  World *w = renderer->game_worlds->home_world;

  // Clear background unless caller disabled it (e.g., drawing into PIP viewport)
  if (renderer->allow_clear) {
    SDL_SetRenderDrawColor(sdl_renderer, 20, 22, 35, 255);
    SDL_RenderClear(sdl_renderer);
  }

  // Dynamically size tiles to fit world into screen (respect current zoom_scale)
  {
    const int margin = 32;
    int sum = (int)w->width + (int)w->height;
    if (sum < 2)
      sum = 2;
    int tw_w = (2 * (renderer->screen_width - margin)) / sum;
    int tw_h = (4 * (renderer->screen_height - margin)) / sum; // since th = tw/2
    int tw = tw_w < tw_h ? tw_w : tw_h;
    if (tw < 8)
      tw = 8;
    // Snap to multiple of 4 to keep half/quarter integer
    tw -= (tw % 4);
    if (tw < 8)
      tw = 8;
    // Apply zoom scaling (clamp bounds) while preserving exact 2:1 ratio
    if (renderer->zoom_scale < 0.25f)
      renderer->zoom_scale = 0.25f;
    if (renderer->zoom_scale > 4.0f)
      renderer->zoom_scale = 4.0f;
    float twf = (float)tw * renderer->zoom_scale;
    int tw_rounded = (int)(twf + 0.5f);
    tw_rounded -= (tw_rounded % 4);
    if (tw_rounded < 8)
      tw_rounded = 8;
    tw = tw_rounded;
    int th = tw / 2;
    if (th < 4)
      th = 4;
    renderer->tile_width = tw;
    renderer->tile_height = th;
    renderer->voxel_height = th; // vh = th
  }

  // Auto-center camera only when enabled
  // Disable auto-centering adjustment entirely while we resolve cross-world alignment
  if (false && renderer->auto_center_camera && renderer->neighbor_inclusion_radius == 0)
  {
    int minx = (int)w->width, miny = (int)w->height, maxx = -1, maxy = -1;
    for (uint32_t yy = 0; yy < w->height; yy++)
    {
      for (uint32_t xx = 0; xx < w->width; xx++)
      {
        for (uint32_t zz = 0; zz < w->depth; zz++)
        {
          Voxel *vv = world_pos_in_bounds_fast(w, (int)xx, (int)yy, (int)zz) ? world_voxel_ptr_fast(w, (int)xx, (int)yy, (int)zz) : NULL;
          if (vv && vv->type != VOXEL_AIR)
          {
            if ((int)xx < minx)
              minx = (int)xx;
            if ((int)yy < miny)
              miny = (int)yy;
            if ((int)xx > maxx)
              maxx = (int)xx;
            if ((int)yy > maxy)
              maxy = (int)yy;
            break;
          }
        }
      }
    }
    if (maxx >= minx && maxy >= miny)
    {
      int mx = (minx + maxx) / 2;
      int my = (miny + maxy) / 2;
      renderer->camera_x = mx;
      renderer->camera_y = my;
    }
  }

  // Position screen center so that the camera's world (x,y) maps to the window center
  {
    int half_tw = renderer->tile_width / 2;
    int half_th = renderer->tile_height / 2;
    renderer->screen_center_x = renderer->screen_width / 2 -
        (int)roundf((renderer->camera_world_x - renderer->camera_world_y) * (float)half_tw);
    renderer->screen_center_y = renderer->screen_height / 2 -
        (int)roundf((renderer->camera_world_x + renderer->camera_world_y) * (float)half_th);
  }

  // Build render buffer (visible voxels around camera). Ensure painter's order parity with world_viewer.
  isometric_renderer_clear_buffer(renderer);
  // Center world
  isometric_renderer_render_world(renderer, w, 0, 0, 0, 0);
  // Render attached edge worlds. Horizontal neighbors respect inclusion radius; one vertical
  // step of cloud/sky layers is always drawn as opaque background scenery.
  for (int idx = 1; idx < 125; idx++)
  {
    World *ew = renderer->edge_worlds[idx];
    if (!ew)
      continue;
    WorldOffset o = renderer->world_offsets[idx];
    if (!iso_edge_world_included(renderer, &o, ew))
      continue;
    // Closest-ring neighbours are scenery, not ghosts — keep them fully opaque.
    renderer->world_alpha[idx] = 1.0f;
    int offx = o.dx * (int)ew->width;
    int offy = o.dy * (int)ew->height;
    int offz = o.dz * (int)ew->depth;
    isometric_renderer_render_world(renderer, ew, idx, offx, offy, offz);
    renderer->world_alpha[idx] = 1.0f;
  }
  // Override depth using universe-adjusted absolute coordinates so painter's order is correct across worlds
  for (int i = 0; i < renderer->render_buffer_size; i++)
  {
    VoxelRenderData *v = &renderer->render_buffer[i];
    int idx = v->world_index;
    World *ww = (idx == 0) ? w : ((idx > 0 && idx < 125) ? renderer->edge_worlds[idx] : w);
    int wW = ww ? (int)ww->width : (int)w->width;
    int wH = ww ? (int)ww->height : (int)w->height;
    int wD = ww ? (int)ww->depth : (int)w->depth;
    WorldOffset off = renderer->world_offsets[idx];
    // Absolute positions in the tiled universe grid
    int ax = v->world_x + off.dx * wW;
    int ay = v->world_y + off.dy * wH;
    int az = v->world_z + off.dz * wD;
    // y-major, then x, then z produces stable painter's order with higher z drawn last
    // Use large multiplier to avoid collisions
    int key = ((ay * 8192) + ax) * 2048 + az;
    // Cloud / sky layers above the player are backdrop scenery. Leaving them on the normal
    // ascending-z key paints them *after* the island and they overwrite grass with white.
    if (off.dz > 0 || (ww && ww->generation_type == WORLD_TYPE_CLOUD))
      key -= 0x20000000;
    v->depth = key;
  }
  isometric_renderer_sort_buffer(renderer);

  // Two-pass: count triangles then allocate. We'll compute screen space using the same
  // lattice mapping as world_viewer for visual parity.
  size_t tri_count = 0;
  for (int i = 0; i < renderer->render_buffer_size; i++)
  {
    VoxelRenderData *v = &renderer->render_buffer[i];
    // Faces drawn in isometric: left, right, top (no bottom/front/back)
    if (v->visible_faces[3])
      tri_count += 2; // right
    if (v->visible_faces[2])
      tri_count += 2; // left
    if (v->visible_faces[0])
      tri_count += 2; // top
  }

  if (tri_count == 0)
  {
    // Still draw the spirit avatar so an empty/culled frame isn't a blank character.
    goto draw_player_avatar;
  }

  size_t vert_cap = tri_count * 3;
  // Reuse persistent buffer; grow geometrically when needed
  if (renderer->gpu_vertex_capacity < vert_cap)
  {
    size_t new_cap = renderer->gpu_vertex_capacity ? renderer->gpu_vertex_capacity : 16384;
    while (new_cap < vert_cap)
      new_cap *= 2;
    SDL_Vertex *nb = (SDL_Vertex *)realloc(renderer->gpu_vertices, new_cap * sizeof(SDL_Vertex));
    if (!nb)
    {
      // Fallback to CPU path if out of memory
      isometric_renderer_render(renderer, sdl_renderer);
      goto draw_player_avatar;
    }
    renderer->gpu_vertices = nb;
    renderer->gpu_vertex_capacity = new_cap;
  }

  // Sub-voxel atlas, built on the first frame that reaches this point because it needs a live
  // SDL_Renderer. A failure is recorded so the build is not retried every frame; the renderer then
  // keeps drawing flat faces.
  if (renderer->subvoxel_detail_enabled && !renderer->material_atlas && !renderer->material_atlas_tried)
  {
    renderer->material_atlas_tried = true;
    renderer->material_atlas = material_atlas_create(sdl_renderer);
  }
  MaterialAtlas *const atlas = renderer->subvoxel_detail_enabled ? renderer->material_atlas : NULL;

  // Water surfaces and fog volumes, once per frame before any face is emitted.
  //
  // Collecting fluid arrivals here rather than in the physics tick keeps the wave field on the
  // thread that draws it: the simulation records what landed where and this reads the record after
  // the tick's barrier, so no lock is needed. The wave then advances on wall-clock time, which is
  // what makes ripples independent of how often the fluid steps. Fog volumes share the same clock.
  FluidSurface *const water = atlas ? renderer->water_surface : NULL;
  FogVolume *const fog = atlas ? renderer->fog_volume : NULL;
  if (water || fog)
  {
    const uint32_t now_ms = SDL_GetTicks();
    if (water)
    {
      fluid_surface_absorb_all_splashes(water, w);
      for (int idx = 1; idx < 125; idx++)
        if (renderer->edge_worlds[idx])
          fluid_surface_absorb_all_splashes(water, renderer->edge_worlds[idx]);

      if (renderer->water_surface_last_ms != 0)
        fluid_surface_step(water, (float)(now_ms - renderer->water_surface_last_ms) / 1000.0f);
      renderer->water_surface_last_ms = now_ms;
    }
    if (fog)
    {
      if (renderer->fog_volume_last_ms != 0)
        fog_volume_step(fog, (float)(now_ms - renderer->fog_volume_last_ms) / 1000.0f);
      renderer->fog_volume_last_ms = now_ms;
    }

    material_atlas_live_reset(atlas);
  }
  int water_faces = 0;
  int fog_faces = 0;

  size_t vcount = 0;
  const int tw = renderer->tile_width;
  const int th = renderer->tile_height;
  const int vh = renderer->voxel_height;
  const int a = tw / 2;
  const int b = th / 2;

  // Greedy top pass for all worlds handled below with emit_top_faces_greedy_for_world

  // Use a single screen-center for all worlds to guarantee cross-world alignment
  const int view_z = (renderer->camera_z < 0) ? 0 : (renderer->camera_z >= (int)w->depth ? (int)w->depth - 1 : renderer->camera_z);
  int half_tw = tw / 2;
  int half_th = th / 2;
  int cx0 = renderer->screen_center_x;
  int cy0 = renderer->screen_center_y;

  // Disable greedy tops for center world to match adjacent worlds' rendering behavior

  // Scratch for one baked water surface. Hoisted out of the loop: it is 4KB and every water face
  // overwrites all of it anyway.
  static uint32_t water_texels[FLUID_SURFACE_CELLS];
  // Same idea for fog faces projected from a 32³ volume.
  static uint32_t fog_texels[FOG_VOLUME_DIM * FOG_VOLUME_DIM];
  // Same idea for cracked faces: one 32x32 tile claimed into the live atlas per damaged face.
  static uint32_t crack_texels[MATERIAL_FACE_SIZE * MATERIAL_FACE_SIZE];

  for (int i = 0; i < renderer->render_buffer_size; i++)
  {
    VoxelRenderData *voxel = &renderer->render_buffer[i];
    // Compute screen anchor using world_viewer mapping with universe world offsets
    const int rx_i = voxel->world_x; // no rotation for now
    const int ry_i = voxel->world_y;
    const int vz_i = voxel->world_z;
    WorldOffset woff = renderer->world_offsets[voxel->world_index];
    // Seamless tiling: no extra spacing between worlds beyond exact world dimensions
    const int ox = woff.dx * (int)w->width;  // already exact tile width
    const int oy = woff.dy * (int)w->height; // already exact tile height
    const int oz = woff.dz * (int)w->depth;  // exact tile depth
    const int sx = cx0 + ((rx_i + ox) - (ry_i + oy)) * half_tw;
    // world_viewer draws columns at base y for each x,y and then offsets per z with +vh as z increases downward
    const int sy_base = cy0 + ((rx_i + ox) + (ry_i + oy)) * half_th;
    // Align vertical placement strictly by world Z (no half-tile bias)
    const int adj_vz = vz_i + oz;
    const int sy = sy_base + (view_z - adj_vz) * vh;
    const SDL_Color base = voxel->color;

    // Foliage bend: shear the drawn cube so a nearby body parts the canopy. Screen lean follows the
    // same (dx-dy, dx+dy) basis as world_to_screen; squash shortens the column from the top.
    float bend_lean_x = 0.0f, bend_lean_y = 0.0f, bend_squash = 1.0f;
    if (renderer->foliage_bend.count > 0 && foliage_bend_affects(voxel->type))
    {
      foliage_bend_sample(&renderer->foliage_bend, voxel->type,
                          (float)(rx_i + ox) + 0.5f, (float)(ry_i + oy) + 0.5f,
                          (float)adj_vz + 0.5f, &bend_lean_x, &bend_lean_y, &bend_squash);
    }
    const int lean_sx = (int)lroundf((bend_lean_x - bend_lean_y) * (float)half_tw);
    const int lean_sy = (int)lroundf((bend_lean_x + bend_lean_y) * (float)half_th);
    const int bent_vh = (int)lroundf((float)vh * bend_squash);
    const int top_sx = sx + lean_sx;
    const int top_sy = sy + lean_sy + (vh - bent_vh);

    // Precompute key points for top and base diamonds
    SDL_Point top_pts[4] = {
        {top_sx, top_sy - b},
        {top_sx + a, top_sy},
        {top_sx, top_sy + b},
        {top_sx - a, top_sy}};

    // Bottom face omitted in isometric view

    // Resolve one face's texture coordinates and vertex colour.
    //
    // SDL_RenderGeometry multiplies the vertex colour by the texel, which decides how the two
    // combine. Where a material has a template, the texel already carries the material's colour, so
    // the vertex becomes pure shading — grey scaled by the face's brightness — and the texture
    // supplies the albedo. Where it does not, the UVs point at the atlas's white tile and the
    // vertex keeps the voxel's own shaded colour, so that face renders exactly as it did before.
    //
    // Damaged voxels claim a live cracked tile so fractures are unique per block. When the live
    // atlas is full the face falls back to a darkened flat colour.
    //
    // The trade this makes: a material's template is shared across its whole type family, so the
    // eighteen wood species all render as the same bark rather than eighteen tints of it. Species
    // colour is recoverable later by tinting the template per type; the flat path (VERSE_SUBVOXEL=0)
    // keeps it in the meantime.
#define ISO_FACE_UV(face_id, shade)                                                               \
  MaterialAtlasRect uv;                                                                           \
  SDL_Color c;                                                                                    \
  do                                                                                              \
  {                                                                                               \
    if (voxel->fog_hidden)                                                                        \
    {                                                                                             \
      /* White atlas tile × black vertex = solid fog-of-war black. */                             \
      uv = material_atlas_rect(atlas, VOXEL_AIR, (face_id), NULL);                                \
      c = (SDL_Color){0, 0, 0, 255};                                                              \
      break;                                                                                      \
    }                                                                                             \
    bool textured = false;                                                                        \
    uv = material_atlas_rect(atlas, voxel->type, (face_id), &textured);                            \
    if (voxel->damage > 0 && atlas)                                                               \
    {                                                                                             \
      const uint32_t seed = voxel_crack_seed(voxel->world_x, voxel->world_y, voxel->world_z);      \
      if (voxel_bake_cracked_face(voxel->type, (face_id), voxel->damage, seed, base.r, base.g,    \
                                  base.b, crack_texels) &&                                        \
          material_atlas_live_claim(atlas, crack_texels, &uv))                                    \
      {                                                                                           \
        textured = true;                                                                          \
      }                                                                                           \
      else if (!textured)                                                                         \
      {                                                                                           \
        const float dim = 1.0f - 0.35f * ((float)voxel->damage / 255.0f);                          \
        c = (SDL_Color){CLAMP_U8((int)(base.r * (shade) * dim)),                                  \
                        CLAMP_U8((int)(base.g * (shade) * dim)),                                  \
                        CLAMP_U8((int)(base.b * (shade) * dim)), base.a};                         \
        break;                                                                                    \
      }                                                                                           \
    }                                                                                             \
    if (textured)                                                                                 \
    {                                                                                             \
      const uint8_t s = CLAMP_U8((int)(255.0f * (shade)));                                        \
      /* Sparse materials keep vertex alpha so atlas gaps can blend; solid ground must stay \
       * fully opaque or clouds sorted behind the island show through the lawn. */                  \
      const uint8_t a = world_voxel_type_has_material_gaps(voxel->type) ? base.a : 255;            \
      c = (SDL_Color){s, s, s, a};                                                                \
    }                                                                                             \
    else                                                                                          \
    {                                                                                             \
      c = (SDL_Color){CLAMP_U8((int)(base.r * (shade))), CLAMP_U8((int)(base.g * (shade))),        \
                      CLAMP_U8((int)(base.b * (shade))), base.a};                                  \
    }                                                                                             \
  } while (0)

    // Water is the one material whose faces differ from voxel to voxel: each surface carries its
    // own wave field, so it cannot come from a shared template. Baked here into a tile of its own
    // in the atlas's live block, which keeps the frame to a single draw call all the same.
    bool water_top = false;
    MaterialAtlasRect water_uv = {0.0f, 0.0f, 0.0f, 0.0f};
    World *voxel_world = (voxel->world_index == 0)
                             ? w
                             : ((voxel->world_index > 0 && voxel->world_index < 125)
                                    ? renderer->edge_worlds[voxel->world_index]
                                    : NULL);
    if (water && voxel->type == VOXEL_WATER && voxel->visible_faces[0])
    {
      if (voxel_world &&
          fluid_surface_bake_voxel(water, voxel_world, rx_i, ry_i, vz_i, base.r, base.g, base.b,
                                   water_texels) &&
          material_atlas_live_claim(atlas, water_texels, &water_uv))
      {
        water_top = true;
        water_faces++;
      }
    }

    // Steam / gas: per-voxel fog volumes, same live-atlas path as water. Any visible iso face can
    // carry a projection of the density field.
    bool fog_face[3] = {false, false, false};
    MaterialAtlasRect fog_uv[3] = {{0}, {0}, {0}};
    if (fog && (voxel->type == VOXEL_STEAM || voxel->type == VOXEL_GAS) && voxel_world)
    {
      static const FogFace fog_faces_map[3] = {FOG_FACE_TOP, FOG_FACE_LEFT, FOG_FACE_RIGHT};
      static const int vis_index[3] = {0, 2, 3}; // top, left, right in VoxelRenderData
      for (int fi = 0; fi < 3; fi++)
      {
        if (!voxel->visible_faces[vis_index[fi]])
          continue;
        if (fog_volume_bake_face(fog, voxel_world, rx_i, ry_i, vz_i, fog_faces_map[fi], base.r,
                                 base.g, base.b, fog_texels) &&
            material_atlas_live_claim(atlas, fog_texels, &fog_uv[fi]))
        {
          fog_face[fi] = true;
          fog_faces++;
        }
      }
    }

    // Draw per-voxel faces (right, left, top) for all worlds to guarantee alignment
      // Right face (medium)
      if (voxel->visible_faces[3])
      {
        ISO_FACE_UV(MATERIAL_FACE_RIGHT, 0.8f);
        if (fog_face[2])
        {
          uv = fog_uv[2];
          c = (SDL_Color){255, 255, 255, base.a};
        }
        // Top edge follows the bent lid; base stays planted so the stem shears.
        SDL_FPoint R0 = {(float)(top_sx + half_tw), (float)(top_sy)};
        SDL_FPoint R1 = {(float)(sx + half_tw), (float)(sy + vh)};
        SDL_FPoint R2 = {(float)(sx), (float)(sy + half_th + vh)};
        SDL_FPoint R3 = {(float)(top_sx), (float)(top_sy + half_th)};
        // The quad's corners run R0 top-outer, R1 bottom-outer, R2 bottom-inner, R3 top-inner, so
        // u follows the horizontal edge and v runs down the face.
        const SDL_FPoint t0 = {uv.u0, uv.v0}, t1 = {uv.u0, uv.v1};
        const SDL_FPoint t2 = {uv.u1, uv.v1}, t3 = {uv.u1, uv.v0};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = R0, .color = c, .tex_coord = t0};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = R1, .color = c, .tex_coord = t1};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = R2, .color = c, .tex_coord = t2};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = R0, .color = c, .tex_coord = t0};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = R2, .color = c, .tex_coord = t2};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = R3, .color = c, .tex_coord = t3};
      }
      // Left face (dark)
      if (voxel->visible_faces[2])
      {
        ISO_FACE_UV(MATERIAL_FACE_LEFT, 0.6f);
        if (fog_face[1])
        {
          uv = fog_uv[1];
          c = (SDL_Color){255, 255, 255, base.a};
        }
        SDL_FPoint L0 = {(float)(top_sx - half_tw), (float)(top_sy)};
        SDL_FPoint L1 = {(float)(top_sx), (float)(top_sy + half_th)};
        SDL_FPoint L2 = {(float)(sx), (float)(sy + half_th + vh)};
        SDL_FPoint L3 = {(float)(sx - half_tw), (float)(sy + vh)};
        const SDL_FPoint t0 = {uv.u0, uv.v0}, t1 = {uv.u1, uv.v0};
        const SDL_FPoint t2 = {uv.u1, uv.v1}, t3 = {uv.u0, uv.v1};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = L0, .color = c, .tex_coord = t0};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = L1, .color = c, .tex_coord = t1};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = L2, .color = c, .tex_coord = t2};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = L0, .color = c, .tex_coord = t0};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = L2, .color = c, .tex_coord = t2};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = L3, .color = c, .tex_coord = t3};
      }
      // Top face (brightest). If greedy top for neighbors is enabled, skip top for non-origin worlds
      if (voxel->visible_faces[0])
      {
        ISO_FACE_UV(MATERIAL_FACE_TOP, 1.0f);
        if (water_top)
        {
          // The tile carries the surface's colour and its transparency, so the vertex contributes
          // nothing but the cross-world fade — anything else would shade the water twice.
          uv = water_uv;
          c = (SDL_Color){255, 255, 255, base.a};
        }
        else if (fog_face[0])
        {
          uv = fog_uv[0];
          c = (SDL_Color){255, 255, 255, base.a};
        }
        SDL_FPoint p0 = {(float)top_pts[0].x, (float)top_pts[0].y};
        SDL_FPoint p1 = {(float)top_pts[1].x, (float)top_pts[1].y};
        SDL_FPoint p2 = {(float)top_pts[2].x, (float)top_pts[2].y};
        SDL_FPoint p3 = {(float)top_pts[3].x, (float)top_pts[3].y};
        // The top is a diamond, so its corners map to the tile's corners: north, east, south, west.
        const SDL_FPoint t0 = {uv.u0, uv.v0}, t1 = {uv.u1, uv.v0};
        const SDL_FPoint t2 = {uv.u1, uv.v1}, t3 = {uv.u0, uv.v1};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = p0, .color = c, .tex_coord = t0};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = p1, .color = c, .tex_coord = t1};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = p2, .color = c, .tex_coord = t2};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = p0, .color = c, .tex_coord = t0};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = p2, .color = c, .tex_coord = t2};
        renderer->gpu_vertices[vcount++] = (SDL_Vertex){.position = p3, .color = c, .tex_coord = t3};
      }

  }

  // After per-voxel faces for origin, emit merged faces only for neighbor worlds to reduce geometry
  // Edge worlds
  for (int idx = 1; idx < 125; idx++)
  {
    World *ew = renderer->edge_worlds[idx];
    if (!ew)
      continue;
    WorldOffset o = renderer->world_offsets[idx];
    if (!iso_edge_world_included(renderer, &o, ew))
      continue;
    if (renderer->greedy_neighbors_top_only)
    {
      emit_top_faces_greedy_for_world(renderer, ew, idx,
                                      cx0, cy0,
                                      renderer->tile_width / 2, renderer->tile_height / 2,
                                      renderer->voxel_height,
                                      view_z,
                                      &vcount);
    }
  }

  // This frame's water / fog surfaces, in one upload covering only the rows that were claimed.
  if (water || fog)
  {
    material_atlas_live_upload(atlas);
    renderer->water_faces_drawn = water_faces;
    renderer->fog_faces_drawn = fog_faces;
  }

  // Submit geometry in one go. Every face's UVs land in the same atlas — the white tile for
  // materials without a template — so texturing costs no extra draw calls.
  if (vcount > 0)
  {
    if (SDL_RenderGeometry(sdl_renderer, material_atlas_texture(atlas), renderer->gpu_vertices, (int)vcount, NULL, 0) != 0)
    {
      // Fallback if GPU path fails at runtime — still draw avatar after CPU pass
      isometric_renderer_render(renderer, sdl_renderer);
      goto draw_player_avatar;
    }
  }
  if (renderer->show_alignment_baseline && renderer->game_worlds && renderer->game_worlds->home_world)
  {
    World *w0 = renderer->game_worlds->home_world;
    int sx0a = 0, sy0a = 0, sx0b = 0, sy0b = 0;
    isometric_world_to_screen(renderer, 0, 0, 0, 0, &sx0a, &sy0a);
    isometric_world_to_screen(renderer, (int)w0->width - 1, 0, 0, 0, &sx0b, &sy0b);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 255, 0, 220);
    SDL_RenderDrawLine(sdl_renderer, sx0a, sy0a, sx0b, sy0b);
    SDL_RenderDrawLine(sdl_renderer, sx0a, sy0a - 3, sx0a, sy0a + 3);
    SDL_RenderDrawLine(sdl_renderer, sx0b, sy0b - 3, sx0b, sy0b + 3);

     int idx_px = isometric_renderer_offset_index(renderer, 1, 0, 0);
     if (idx_px > 0)
     {
       World *wx = renderer->edge_worlds[idx_px];
       if (wx)
       {
         int sx1a = 0, sy1a = 0, sx1b = 0, sy1b = 0;
         isometric_world_to_screen(renderer, 0, 0, 0, idx_px, &sx1a, &sy1a);
         isometric_world_to_screen(renderer, (int)wx->width - 1, 0, 0, idx_px, &sx1b, &sy1b);
         SDL_SetRenderDrawColor(sdl_renderer, 0, 128, 255, 220);
         SDL_RenderDrawLine(sdl_renderer, sx1a, sy1a, sx1b, sy1b);
         SDL_RenderDrawLine(sdl_renderer, sx1a, sy1a - 3, sx1a, sy1a + 3);
         SDL_RenderDrawLine(sdl_renderer, sx1b, sy1b - 3, sx1b, sy1b + 3);
       }
     }
   }

   // Record stats
   renderer->last_rendered_voxel_count = renderer->render_buffer_size;

  // Mass highlight: draw a white outline around all voxels of the highlighted type
  if (renderer->mass_highlight_active && renderer->highlighted_type >= 0)
  {
    const int half_tw2 = renderer->tile_width / 2;
    const int half_th2 = renderer->tile_height / 2;
    const int vh2 = renderer->voxel_height;
    const int view_z2 = (renderer->camera_z < 0) ? 0 : (renderer->camera_z >= (int)w->depth ? (int)w->depth - 1 : renderer->camera_z);
    int cx02 = renderer->screen_center_x;
    int cy02 = renderer->screen_center_y;
    // Recenter for current view like above
    {
      int minx2 = (int)w->width, miny2 = (int)w->height, maxx2 = -1, maxy2 = -1;
      for (uint32_t yy = 0; yy < w->height; yy++)
        for (uint32_t xx = 0; xx < w->width; xx++)
          for (int zz = 0; zz < (int)w->depth; zz++)
          {
            Voxel *vv = world_get_voxel(w, xx, yy, (uint32_t)zz);
            if (vv && vv->type != VOXEL_AIR)
            {
              if ((int)xx < minx2)
                minx2 = (int)xx;
              if ((int)yy < miny2)
                miny2 = (int)yy;
              if ((int)xx > maxx2)
                maxx2 = (int)xx;
              if ((int)yy > maxy2)
                maxy2 = (int)yy;
              break;
            }
          }
      if (maxx2 >= minx2 && maxy2 >= miny2)
      {
        float cmx = (minx2 + maxx2) * 0.5f;
        float cmy = (miny2 + maxy2) * 0.5f;
        int center_sx = cx02 + ((int)cmx - (int)cmy) * half_tw2;
        int center_sy = cy02 + ((int)cmx + (int)cmy) * half_th2;
        cx02 -= (center_sx - renderer->screen_width / 2);
        cy02 -= (center_sy - renderer->screen_height / 2);
      }
    }
    SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
    for (int i = 0; i < renderer->render_buffer_size; i++)
    {
      VoxelRenderData *v = &renderer->render_buffer[i];
      if ((int)v->type != renderer->highlighted_type)
        continue;
      int sx = cx02 + (v->world_x - v->world_y) * half_tw2;
      int sy_base = cy02 + (v->world_x + v->world_y) * half_th2;
      int sy = sy_base + (view_z2 - v->world_z) * vh2;
      // Top diamond
      SDL_RenderDrawLine(sdl_renderer, sx, sy - half_th2, sx + half_tw2, sy);
      SDL_RenderDrawLine(sdl_renderer, sx + half_tw2, sy, sx, sy + half_th2);
      SDL_RenderDrawLine(sdl_renderer, sx, sy + half_th2, sx - half_tw2, sy);
      SDL_RenderDrawLine(sdl_renderer, sx - half_tw2, sy, sx, sy - half_th2);
      // Side faces outlines based on visibility
      if (v->visible_faces[3])
      {
        SDL_RenderDrawLine(sdl_renderer, sx + half_tw2, sy, sx + half_tw2, sy + vh2);
        SDL_RenderDrawLine(sdl_renderer, sx + half_tw2, sy + vh2, sx, sy + half_th2 + vh2);
        SDL_RenderDrawLine(sdl_renderer, sx, sy + half_th2 + vh2, sx, sy + half_th2);
      }
      if (v->visible_faces[2])
      {
        SDL_RenderDrawLine(sdl_renderer, sx - half_tw2, sy, sx - half_tw2, sy + vh2);
        SDL_RenderDrawLine(sdl_renderer, sx - half_tw2, sy + vh2, sx, sy + half_th2 + vh2);
        SDL_RenderDrawLine(sdl_renderer, sx, sy + half_th2 + vh2, sx, sy + half_th2);
      }
    }
  }

  // Optional: draw white outline for hovered voxel (aligned to top face with half-voxel correction)
  if (renderer->has_highlighted_voxel)
  {
    int rx = renderer->highlighted_x;
    int ry = renderer->highlighted_y;
    int rz = renderer->highlighted_z;
    const int half_tw = renderer->tile_width / 2;
    const int half_th = renderer->tile_height / 2;
    const int vh = renderer->voxel_height;
    int cx0 = renderer->screen_center_x;
    int cy0 = renderer->screen_center_y;
    const World *w = renderer->game_worlds ? renderer->game_worlds->home_world : NULL;
    if (w)
    {
      int view_z = renderer->camera_z;
      int minx2 = (int)w->width, miny2 = (int)w->height, maxx2 = -1, maxy2 = -1;
      for (uint32_t yy = 0; yy < w->height; yy++)
        for (uint32_t xx = 0; xx < w->width; xx++)
          for (int zz = 0; zz < (int)w->depth; zz++)
          {
            Voxel *vv = world_pos_in_bounds_fast((World *)w, (int)xx, (int)yy, (int)zz) ? world_voxel_ptr_fast((World *)w, (int)xx, (int)yy, (int)zz) : NULL;
            if (vv && vv->type != VOXEL_AIR)
            {
              if ((int)xx < minx2)
                minx2 = (int)xx;
              if ((int)yy < miny2)
                miny2 = (int)yy;
              if ((int)xx > maxx2)
                maxx2 = (int)xx;
              if ((int)yy > maxy2)
                maxy2 = (int)yy;
              break;
            }
          }
      if (maxx2 >= minx2 && maxy2 >= miny2)
      {
        float cmx = (minx2 + maxx2) * 0.5f;
        float cmy = (miny2 + maxy2) * 0.5f;
        int center_sx = cx0 + ((int)cmx - (int)cmy) * half_tw;
        int center_sy = cy0 + ((int)cmx + (int)cmy) * half_th;
        cx0 -= (center_sx - renderer->screen_width / 2);
        cy0 -= (center_sy - renderer->screen_height / 2);
      }
    }
    // Compute screen-space center using local centered origin; apply half-voxel vertical correction
    // Sub-voxel correction: shift left/up by half a voxel in +X to cancel residual bias
    int sx = cx0 + (rx - ry) * half_tw;
    int sy_base = cy0 + (rx + ry) * half_th - (half_th / 2);
    int sy = sy_base + (renderer->camera_z - rz) * vh - vh;
    SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
    // Compute top and base diamond corners around (sx, sy)
    int top_x[4] = {sx, sx + half_tw, sx, sx - half_tw};
    int top_y[4] = {sy - half_th, sy, sy + half_th, sy};
    int base_x[4] = {sx, sx + half_tw, sx, sx - half_tw};
    int base_y[4] = {sy + vh - half_th, sy + vh, sy + vh + half_th, sy + vh};

    // Top diamond edges
    SDL_RenderDrawLine(sdl_renderer, top_x[0], top_y[0], top_x[1], top_y[1]);
    SDL_RenderDrawLine(sdl_renderer, top_x[1], top_y[1], top_x[2], top_y[2]);
    SDL_RenderDrawLine(sdl_renderer, top_x[2], top_y[2], top_x[3], top_y[3]);
    SDL_RenderDrawLine(sdl_renderer, top_x[3], top_y[3], top_x[0], top_y[0]);

    // Base diamond edges
    SDL_RenderDrawLine(sdl_renderer, base_x[0], base_y[0], base_x[1], base_y[1]);
    SDL_RenderDrawLine(sdl_renderer, base_x[1], base_y[1], base_x[2], base_y[2]);
    SDL_RenderDrawLine(sdl_renderer, base_x[2], base_y[2], base_x[3], base_y[3]);
    SDL_RenderDrawLine(sdl_renderer, base_x[3], base_y[3], base_x[0], base_y[0]);

    // Vertical edges
    SDL_RenderDrawLine(sdl_renderer, top_x[0], top_y[0], base_x[0], base_y[0]);
    SDL_RenderDrawLine(sdl_renderer, top_x[1], top_y[1], base_x[1], base_y[1]);
    SDL_RenderDrawLine(sdl_renderer, top_x[2], top_y[2], base_x[2], base_y[2]);
    SDL_RenderDrawLine(sdl_renderer, top_x[3], top_y[3], base_x[3], base_y[3]);
  }

  // Render actors as spheres if present in game state
  // Use weak-import globals so non-editor builds don't fail to link when editor globals are absent
#if defined(__APPLE__)
#define WEAKSYM __attribute__((weak_import))
#else
#define WEAKSYM __attribute__((weak))
#endif
  extern Actor g_actors[] WEAKSYM;
  extern int g_actor_count WEAKSYM; // defined strongly in editor
  if (false && renderer->game_worlds && renderer->game_worlds->home_world && &g_actor_count && &g_actors && g_actor_count > 0)
  {
    SDL_SetRenderDrawColor(sdl_renderer, 255, 200, 80, 255);
    const int vh = renderer->voxel_height;
    World *w = renderer->game_worlds->home_world;
    for (int i = 0; i < g_actor_count; i++)
    {
      // Snap actors to integer voxel coordinates to match VOXEL_ACTOR marker
      int ax = (int)floor(g_actors[i].x + 0.0001);
      int ay = (int)floor(g_actors[i].y + 0.0001);
      int az = (int)floor(g_actors[i].z + 0.0001);
      if (ax < 0 || ay < 0 || az < 0 || ax >= (int)w->width || ay >= (int)w->height || az >= (int)w->depth)
        continue;
      int sx = 0, sy = 0;
      // Project actor to screen using the same transform as main pass
      isometric_world_to_screen(renderer, ax, ay, az, 0, &sx, &sy);
      // Center sphere slightly above the voxel's top for visibility
      sy -= (vh / 2);
      int r = half_th;
      if (r < 3)
        r = 3;
      // Simple circle to represent sphere
      for (int dy = -r; dy <= r; dy++)
      {
        int span = (int)sqrt((double)(r * r - dy * dy));
        SDL_RenderDrawLine(sdl_renderer, sx - span, sy - dy, sx + span, sy - dy);
      }
    }
  }

  // Draw a red outline around the boundary of the currently selected world (home world)
  if (renderer->game_worlds && renderer->game_worlds->home_world)
  {
    World *w0 = renderer->game_worlds->home_world;
    const int half_tw = renderer->tile_width / 2;
    const int half_th = renderer->tile_height / 2;
    const int vh = renderer->voxel_height;
    int cx0 = renderer->screen_center_x;
    int cy0 = renderer->screen_center_y;
    const int view_z = (renderer->camera_z < 0) ? 0 : (renderer->camera_z >= (int)w0->depth ? (int)w0->depth - 1 : renderer->camera_z);

    // Recenter like the main pass (occupied bbox up to view_z)
    {
      int minx2 = (int)w0->width, miny2 = (int)w0->height, maxx2 = -1, maxy2 = -1;
      for (uint32_t yy = 0; yy < w0->height; yy++)
        for (uint32_t xx = 0; xx < w0->width; xx++)
          for (int zz = 0; zz < (int)w0->depth; zz++)
          {
            Voxel *vv = world_pos_in_bounds_fast(w0, (int)xx, (int)yy, (int)zz) ? world_voxel_ptr_fast(w0, (int)xx, (int)yy, (int)zz) : NULL;
            if (vv && vv->type != VOXEL_AIR)
            {
              if ((int)xx < minx2)
                minx2 = (int)xx;
              if ((int)yy < miny2)
                miny2 = (int)yy;
              if ((int)xx > maxx2)
                maxx2 = (int)xx;
              if ((int)yy > maxy2)
                maxy2 = (int)yy;
              break;
            }
          }
      if (maxx2 >= minx2 && maxy2 >= miny2)
      {
        float cmx = (minx2 + maxx2) * 0.5f;
        float cmy = (miny2 + maxy2) * 0.5f;
        int center_sx = cx0 + ((int)cmx - (int)cmy) * half_tw;
        int center_sy = cy0 + ((int)cmx + (int)cmy) * half_th;
        cx0 -= (center_sx - renderer->screen_width / 2);
        cy0 -= (center_sy - renderer->screen_height / 2);
      }
    }

    if (renderer->debug_show_world_bounds)
    {
      // Corners of the world on current plane (top-face center projection)
    const int x0 = 0, y0 = 0;
    const int x1 = (int)w0->width - 1;
    const int y1 = (int)w0->height - 1;
    SDL_Point p00 = {cx0 + (x0 - y0) * half_tw, cy0 + (x0 + y0) * half_th};
    SDL_Point p10 = {cx0 + (x1 - y0) * half_tw, cy0 + (x1 + y0) * half_th};
    SDL_Point p11 = {cx0 + (x1 - y1) * half_tw, cy0 + (x1 + y1) * half_th};
    SDL_Point p01 = {cx0 + (x0 - y1) * half_tw, cy0 + (x0 + y1) * half_th};

    SDL_SetRenderDrawColor(sdl_renderer, 255, 0, 0, 255);
    SDL_RenderDrawLine(sdl_renderer, p00.x, p00.y, p10.x, p10.y);
    SDL_RenderDrawLine(sdl_renderer, p10.x, p10.y, p11.x, p11.y);
    SDL_RenderDrawLine(sdl_renderer, p11.x, p11.y, p01.x, p01.y);
    SDL_RenderDrawLine(sdl_renderer, p01.x, p01.y, p00.x, p00.y);
    }
  }

draw_player_avatar:
  // Drifting dust in deep open air (after voxel geometry, before spirit overlay).
  particle_effects_render_isometric(w, renderer, sdl_renderer, 0);
  for (int pidx = 1; pidx < 125; pidx++)
  {
    World *ew = renderer->edge_worlds[pidx];
    if (!ew)
      continue;
    WorldOffset po = renderer->world_offsets[pidx];
    if (!iso_edge_world_included(renderer, &po, ew))
      continue;
    particle_effects_render_isometric(ew, renderer, sdl_renderer, pidx);
  }

  // Mobs sit with the world they walk on, under fireballs and the spirit overlay.
  actors_render_isometric(renderer, sdl_renderer, 0);

  // Fireballs sit above the dust and below the avatar: a projectile the player has cast should be
  // drawn over the world it is flying through, but not over the spirit that cast it.
  projectiles_render_isometric(renderer, sdl_renderer, 0);
  debris_render_isometric(renderer, sdl_renderer, 0);

  // Player avatar (spirit) at the window center — camera tracks the player, so this is
  // where the controlled character sits. Do NOT use renderer->screen_center_*: that field
  // is rewritten as a projection origin during the GPU pass and is usually far off-screen.
  if (renderer->show_player_avatar)
  {
    const int vh = renderer->voxel_height;
    int cx = renderer->screen_width / 2;
    int cy = renderer->screen_height / 2 - vh / 2 - spirit_sprite_hover_bob_px();

    if (renderer->hero_selected)
    {
      SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 200);
      int ring_r = SPIRIT_SPRITE_SIZE / 2 + 6;
      for (int a = 0; a < 16; a++)
      {
        float a0 = (float)a * (2.0f * (float)M_PI / 16.0f);
        float a1 = (float)(a + 1) * (2.0f * (float)M_PI / 16.0f);
        int x0 = cx + (int)(cosf(a0) * (float)ring_r);
        int y0 = cy + (int)(sinf(a0) * (float)ring_r * 0.5f);
        int x1 = cx + (int)(cosf(a1) * (float)ring_r);
        int y1 = cy + (int)(sinf(a1) * (float)ring_r * 0.5f);
        SDL_RenderDrawLine(sdl_renderer, x0, y0, x1, y1);
      }
    }

    spirit_sprite_draw_avatar(sdl_renderer, cx, cy, renderer->player_facing_yaw);

    if (renderer->player_stamina_max > 0 && renderer->stamina_meter_alpha > 0.01f)
    {
      spirit_sprite_draw_stamina_meter(sdl_renderer, cx, cy,
                                       (float)renderer->player_stamina,
                                       (float)renderer->player_stamina_max,
                                       renderer->stamina_meter_alpha);
    }

    if (renderer->player_attacking)
    {
      const float arc = renderer->player_attack_armed ? PLAYER_SWING_ARC_ARMED
                                                      : PLAYER_SWING_ARC_RAD;
      const float radius = renderer->player_attack_radius;
      const float inner = PLAYER_SWING_INNER_RADIUS;
      const float half = arc * 0.5f;
      const float progress = renderer->player_attack_progress;
      const float px = renderer->camera_world_x;
      const float py = renderer->camera_world_y;
      const float pz = renderer->camera_world_z;

      spirit_sprite_draw_swing(sdl_renderer, cx, cy, renderer->player_facing_yaw, progress, arc,
                               renderer->player_attack_armed, true);

      SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

      if (!renderer->player_attack_armed)
      {
        // Unarmed: world pill sweeping the damage sector (same geometry as FP / apply_swing_damage).
        const float ang = renderer->player_facing_yaw - half + (2.0f * half) * progress;
        const float c = cosf(ang), sn = sinf(ang);
        float pill_r =
            PLAYER_UNARMED_PILL_RADIUS_BASE +
            PLAYER_UNARMED_PILL_RADIUS_PER_STR * (float)renderer->player_attack_strength;
        if (pill_r < 0.08f)
          pill_r = 0.08f;
        if (pill_r > 0.32f)
          pill_r = 0.32f;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        isometric_world_to_screen_float(renderer, px + c * inner, py + sn * inner, pz, 0, &x0, &y0);
        isometric_world_to_screen_float(renderer, px + c * radius, py + sn * radius, pz, 0, &x1,
                                        &y1);

        const bool hit = progress >= PLAYER_SWING_HIT_START && progress <= PLAYER_SWING_HIT_END;
        SDL_SetRenderDrawColor(sdl_renderer, 255, hit ? 220 : 140, hit ? 160 : 60, 220);

        const int steps = 14;
        for (int s = 0; s <= steps; s++)
        {
          float u = (float)s / (float)steps;
          int sx = x0 + (int)((float)(x1 - x0) * u);
          int sy = y0 + (int)((float)(y1 - y0) * u);
          // Approximate pill thickness in screen space from world pill radius.
          int pr = (int)(pill_r * (float)renderer->tile_width * renderer->zoom_scale);
          if (pr < 2)
            pr = 2;
          if (s == steps)
            pr = (int)(pr * 1.25f);
          for (int dy = -pr; dy <= pr; dy++)
          {
            for (int dx = -pr; dx <= pr; dx++)
            {
              if (dx * dx + dy * dy > pr * pr)
                continue;
              SDL_RenderDrawPoint(sdl_renderer, sx + dx, sy + dy);
            }
          }
        }
      }
      else
      {
        // Armed: full damage sector wedge.
        float bright = 0.55f;
        if (progress >= PLAYER_SWING_HIT_START && progress <= PLAYER_SWING_HIT_END)
          bright = 1.0f;
        else if (progress < PLAYER_SWING_HIT_START)
          bright = 0.4f + 0.35f * (progress / PLAYER_SWING_HIT_START);
        else
          bright = 0.55f * (1.0f - (progress - PLAYER_SWING_HIT_END) /
                                       (1.0f - PLAYER_SWING_HIT_END));

        const Uint8 a_fill = (Uint8)(140.0f * bright);
        const Uint8 a_edge = (Uint8)(220.0f * bright);
        SDL_SetRenderDrawColor(sdl_renderer, 160, 210, 255, a_fill);

        const int wedge_steps = 18;
        int prev_ox = 0, prev_oy = 0, prev_ix = 0, prev_iy = 0;
        bool have_prev = false;
        for (int s = 0; s <= wedge_steps; s++)
        {
          float t = -half + (2.0f * half) * ((float)s / (float)wedge_steps);
          float ang = renderer->player_facing_yaw + t;
          float c = cosf(ang), sn = sinf(ang);
          int ox = 0, oy = 0, ix = 0, iy = 0;
          isometric_world_to_screen_float(renderer, px + c * radius, py + sn * radius, pz, 0, &ox,
                                          &oy);
          isometric_world_to_screen_float(renderer, px + c * inner, py + sn * inner, pz, 0, &ix,
                                          &iy);
          SDL_RenderDrawLine(sdl_renderer, ix, iy, ox, oy);
          if (have_prev)
          {
            SDL_RenderDrawLine(sdl_renderer, prev_ox, prev_oy, ox, oy);
            SDL_RenderDrawLine(sdl_renderer, prev_ix, prev_iy, ix, iy);
          }
          prev_ox = ox;
          prev_oy = oy;
          prev_ix = ix;
          prev_iy = iy;
          have_prev = true;
        }

        SDL_SetRenderDrawColor(sdl_renderer, 220, 240, 255, a_edge);
        for (int e = 0; e < 2; e++)
        {
          float t = (e == 0) ? -half : half;
          float ang = renderer->player_facing_yaw + t;
          float c = cosf(ang), sn = sinf(ang);
          int ox = 0, oy = 0, ix = 0, iy = 0;
          isometric_world_to_screen_float(renderer, px + c * radius, py + sn * radius, pz, 0, &ox,
                                          &oy);
          isometric_world_to_screen_float(renderer, px + c * inner, py + sn * inner, pz, 0, &ix,
                                          &iy);
          SDL_RenderDrawLine(sdl_renderer, ix, iy, ox, oy);
          SDL_RenderDrawLine(sdl_renderer, ix + 1, iy, ox + 1, oy);
        }
      }
    }
  }
}

int isometric_renderer_get_last_rendered_count(IsometricRenderer *renderer)
{
  return renderer ? renderer->last_rendered_voxel_count : 0;
}

// Voxel interaction functions
void isometric_renderer_set_highlighted_voxel(IsometricRenderer *renderer, int x, int y, int z)
{
  if (!renderer)
    return;

  renderer->has_highlighted_voxel = true;
  renderer->highlighted_x = x;
  renderer->highlighted_y = y;
  renderer->highlighted_z = z;
}

void isometric_renderer_clear_highlighted_voxel(IsometricRenderer *renderer)
{
  if (!renderer)
    return;

  renderer->has_highlighted_voxel = false;
}

// Hero selection functions
void isometric_renderer_set_hero_selected(IsometricRenderer *renderer, bool selected)
{
  if (!renderer)
    return;

  renderer->hero_selected = selected;
}

void isometric_renderer_clear_all_selections(IsometricRenderer *renderer)
{
  if (!renderer)
    return;

  renderer->has_highlighted_voxel = false;
  renderer->hero_selected = false;
  renderer->selected_actor_id = 0;
}

void isometric_renderer_set_selected_actor(IsometricRenderer *renderer, uint32_t actor_id)
{
  if (!renderer)
    return;
  renderer->selected_actor_id = actor_id;
  if (actor_id != 0)
    renderer->hero_selected = false;
}

uint32_t isometric_renderer_selected_actor(const IsometricRenderer *renderer)
{
  return renderer ? renderer->selected_actor_id : 0;
}

uint32_t isometric_renderer_pick_actor(IsometricRenderer *renderer, World *world, int world_index,
                                       int screen_x, int screen_y, int pick_radius_px)
{
  if (!renderer || !world || !world->runtime_actors || world->runtime_actor_count <= 0)
    return 0;
  if (pick_radius_px < 4)
    pick_radius_px = 4;
  const int r2 = pick_radius_px * pick_radius_px;
  uint32_t best_id = 0;
  int best_d2 = r2 + 1;
  for (int i = 0; i < world->runtime_actor_count; i++)
  {
    const Actor *a = &world->runtime_actors[i];
    if (!a->is_active || a->health == 0 || a->id == 0)
      continue;
    int sx = 0, sy = 0;
    isometric_world_to_screen_float(renderer, (float)a->x, (float)a->y, (float)a->z, world_index,
                                    &sx, &sy);
    const int dx = screen_x - sx;
    const int dy = screen_y - sy;
    const int d2 = dx * dx + dy * dy;
    if (d2 <= r2 && d2 < best_d2)
    {
      best_d2 = d2;
      best_id = a->id;
    }
  }
  return best_id;
}

// Improved face culling for proper cube rendering
bool isometric_should_render_face(IsometricRenderer *renderer, World *world, int x, int y, int z, int face_index)
{
  if (!renderer || !world)
  {
    return false;
  }

  // Check adjacent voxel for this face using fast math
  int adj_x = x, adj_y = y, adj_z = z;
  switch (face_index)
  {
  case 0:
    adj_z++;
    break; // top
  case 1:
    adj_z--;
    break; // bottom
  case 2:
    adj_y++;
    break; // left (+Y)
  case 3:
    adj_x++;
    break; // right (+X)
  case 4:
    adj_y--;
    break; // front (-Y)
  case 5:
    adj_x--;
    break; // back (-X)
  default:
    return false;
  }

  // Slice-aware top exposure (only if culling is enabled)
  if (face_index == 0 && !renderer->disable_culling)
  {
    int view_z = renderer->camera_z;
    if (adj_z > view_z)
      return true;
  }

  // If occupancy bitfield is available and matches dimensions, use it to determine visibility
  if (world->occupancy_bits && world->occupancy_bits->bits &&
      world->occupancy_bits->width == world->width &&
      world->occupancy_bits->height == world->height &&
      world->occupancy_bits->depth == world->depth)
  {
    if (!world_pos_in_bounds_fast(world, adj_x, adj_y, adj_z))
      return true;
    const uint32_t W = world->occupancy_bits->width;
    const uint32_t H = world->occupancy_bits->height;
    const uint32_t D = world->occupancy_bits->depth;
    const uint8_t *bits = world->occupancy_bits->bits;
    return iso_face_exposed_occ(world, bits, W, H, D, adj_x, adj_y, adj_z);
  }

  // Fallback: adjacent voxel transparent?
  if (!world_pos_in_bounds_fast(world, adj_x, adj_y, adj_z))
    return true;
  Voxel *adjacent = world_voxel_ptr_fast(world, adj_x, adj_y, adj_z);
  VoxelType t = adjacent ? adjacent->type : VOXEL_AIR;
  return voxel_is_transparent_type(t);
}

// Mouse input handling for battle arena
bool isometric_renderer_handle_mouse_click(IsometricRenderer *renderer, int screen_x, int screen_y, int button)
{
  if (!renderer)
    return false;

  if (button == SDL_BUTTON_RIGHT)
  {
    return isometric_renderer_handle_right_click_move(renderer, screen_x, screen_y);
  }
  else if (button == SDL_BUTTON_LEFT)
  {
    return isometric_renderer_handle_left_click_select(renderer, screen_x, screen_y);
  }

  return false;
}

// Right-click movement for battle arena
bool isometric_renderer_handle_right_click_move(IsometricRenderer *renderer, int screen_x, int screen_y)
{
  if (!renderer)
    return false;

  // Convert screen coordinates to world coordinates
  int world_x, world_y, world_z, world_index;
  isometric_screen_to_world(renderer, screen_x, screen_y, &world_x, &world_y, &world_z, &world_index);

  // Find the ground level at this X,Y position
  if (renderer->game_worlds && renderer->game_worlds->home_world)
  {
    World *world = renderer->game_worlds->home_world;

    // Find highest walkable surface at this X,Y coordinate
    for (int z = world->depth - 1; z >= 0; z--)
    {
      if (world_is_position_valid(world, world_x, world_y, z))
      {
        Voxel *voxel = world_pos_in_bounds_fast(world, world_x, world_y, z) ? world_voxel_ptr_fast(world, world_x, world_y, z) : NULL;
        if (voxel && (voxel->type == VOXEL_GRASS || voxel->type == VOXEL_SOIL || voxel->type == VOXEL_STONE))
        {
          // Check if we can move to the position above this solid block
          int target_z = z + 1;
          if (isometric_renderer_can_move_to(renderer, world_x, world_y, target_z))
          {
            renderer->movement_target_set = true;
            renderer->movement_target_x = world_x;
            renderer->movement_target_y = world_y;
            renderer->movement_target_z = target_z;
            renderer->show_movement_preview = true;
            return true;
          }
        }
      }
    }
  }

  return false;
}

// Left-click selection for battle arena
bool isometric_renderer_handle_left_click_select(IsometricRenderer *renderer, int screen_x, int screen_y)
{
  if (!renderer)
    return false;

  // Convert screen coordinates to world coordinates
  int world_x, world_y, world_z, world_index;
  isometric_screen_to_world(renderer, screen_x, screen_y, &world_x, &world_y, &world_z, &world_index);

  // Check if we clicked on the player (hero)
  int player_screen_x, player_screen_y;
  isometric_world_to_screen(renderer, renderer->camera_x, renderer->camera_y, renderer->camera_z, 0,
                            &player_screen_x, &player_screen_y);

  // Check if click is within player bounds (sphere around player)
  int dx = screen_x - player_screen_x;
  int dy = screen_y - player_screen_y;
  int distance_sq = dx * dx + dy * dy;

  if (distance_sq <= 64)
  { // 8 pixel radius squared
    // Player selected
    renderer->hero_selected = !renderer->hero_selected; // Toggle selection
    isometric_renderer_clear_highlighted_voxel(renderer);
    return true;
  }

  // Otherwise try to select/highlight a voxel
  if (renderer->game_worlds && renderer->game_worlds->home_world)
  {
    World *world = renderer->game_worlds->home_world;

    // Find the first solid voxel at this position
    for (int z = world->depth - 1; z >= 0; z--)
    {
      if (world_is_position_valid(world, world_x, world_y, z))
      {
        Voxel *voxel = world_pos_in_bounds_fast(world, world_x, world_y, z) ? world_voxel_ptr_fast(world, world_x, world_y, z) : NULL;
        if (voxel && voxel->type != VOXEL_AIR)
        {
          isometric_renderer_set_highlighted_voxel(renderer, world_x, world_y, z);
          renderer->hero_selected = false; // Clear hero selection
          return true;
        }
      }
    }
  }

  // Clear all selections if nothing was clicked
  isometric_renderer_clear_all_selections(renderer);
  return false;
}

// Movement validation for battle arena
bool isometric_renderer_can_move_to(IsometricRenderer *renderer, int world_x, int world_y, int world_z)
{
  if (!renderer || !renderer->game_worlds || !renderer->game_worlds->home_world)
    return false;

  World *world = renderer->game_worlds->home_world;

  // Check if position is within world bounds
  if (!world_is_position_valid(world, world_x, world_y, world_z))
    return false;

  // Check if the target position is air (walkable)
  Voxel *target = world_pos_in_bounds_fast(world, world_x, world_y, world_z) ? world_voxel_ptr_fast(world, world_x, world_y, world_z) : NULL;
  if (!target || target->type != VOXEL_AIR)
    return false;

  // If player is flying, allow movement without requiring ground below.
  // In standalone tools, assume not flying to avoid external dependencies.
  bool is_flying = false;
  if (is_flying)
    return true;

  // Otherwise require solid ground within 2 blocks below
  for (int z = world_z - 1; z >= world_z - 2 && z >= 0; z--)
  {
    Voxel *ground = world_pos_in_bounds_fast(world, world_x, world_y, z) ? world_voxel_ptr_fast(world, world_x, world_y, z) : NULL;
    if (ground && (ground->type == VOXEL_GRASS || ground->type == VOXEL_SOIL || ground->type == VOXEL_STONE))
    {
      return true; // Found solid ground
    }
  }

  return false; // No solid ground nearby
}

// Show movement preview
void isometric_renderer_show_movement_preview(IsometricRenderer *renderer, int target_x, int target_y, int target_z)
{
  if (!renderer)
    return;

  renderer->movement_target_set = true;
  renderer->movement_target_x = target_x;
  renderer->movement_target_y = target_y;
  renderer->movement_target_z = target_z;
  renderer->show_movement_preview = true;
}

static void particle_effects_render_isometric(World *world,
                                     IsometricRenderer *renderer,
                                     SDL_Renderer *sdl_renderer,
                                     int world_index)
{
  if (!world || !world->particle_effects || !renderer || !sdl_renderer)
    return;

  WorldParticleEffects *effects = world->particle_effects;
  if (effects->particle_count <= 0)
    return;

  for (int i = 0; i < effects->particle_count; i++)
  {
    const Particle *p = &effects->particles[i];
    int sx = 0;
    int sy = 0;
    isometric_world_to_screen_float(renderer, p->x, p->y, p->z, world_index, &sx, &sy);

    const float fade = (p->max_life > 0.0f) ? (p->life / p->max_life) : 0.0f;
    const uint8_t a = (uint8_t)((float)p->a * fade);
    const int radius = (int)(p->size_px + 0.5f);
    SDL_SetRenderDrawColor(sdl_renderer, p->r, p->g, p->b, a);
    SDL_Rect rect = {sx - radius, sy - radius, radius * 2 + 1, radius * 2 + 1};
    SDL_RenderFillRect(sdl_renderer, &rect);
  }
}

// Actor.z is the air voxel the feet occupy (top of the solid below). GPU voxel Z's top
// diamond is drawn at integer Z, so the feet plane is one voxel below the projected top.
static void iso_actor_to_screen(IsometricRenderer *renderer, float x, float y, float z,
                                int world_index, int *sx, int *sy)
{
  isometric_world_to_screen_float(renderer, x, y, z, world_index, sx, sy);
  if (sy)
    *sy += renderer->voxel_height;
}

static void iso_fill_quad(SDL_Renderer *sdl, int x0, int y0, int x1, int y1,
                          int x2, int y2, int x3, int y3,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
  SDL_Vertex v[6];
  const int xs[4] = {x0, x1, x2, x3};
  const int ys[4] = {y0, y1, y2, y3};
  const int idx[6] = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 6; i++)
  {
    v[i].position.x = (float)xs[idx[i]];
    v[i].position.y = (float)ys[idx[i]];
    v[i].color.r = r;
    v[i].color.g = g;
    v[i].color.b = b;
    v[i].color.a = a;
    v[i].tex_coord.x = 0.0f;
    v[i].tex_coord.y = 0.0f;
  }
  SDL_RenderGeometry(sdl, NULL, v, 6, NULL, 0);
}

static void iso_draw_actor_shadow(IsometricRenderer *renderer, SDL_Renderer *sdl, int sx, int sy)
{
  const int a = renderer->tile_width / 3;
  const int b = renderer->tile_height / 3;
  const int aw = a < 4 ? 4 : a;
  const int bh = b < 3 ? 3 : b;
  iso_fill_quad(sdl, sx, sy - bh, sx + aw, sy, sx, sy + bh, sx - aw, sy, 8, 6, 4, 160);
}

// 3x5 glyphs. Each row is 3 bits, bit 0 = left column; rows pack from the top.
#define ISO_G(r0, r1, r2, r3, r4) \
  ((uint16_t)((r0) | ((r1) << 3) | ((r2) << 6) | ((r3) << 9) | ((r4) << 12)))

static uint16_t iso_glyph_3x5(char ch)
{
  if (ch >= 'a' && ch <= 'z')
    ch = (char)(ch - 'a' + 'A');
  switch (ch)
  {
    case '0': return ISO_G(7, 5, 5, 5, 7);
    case '1': return ISO_G(2, 3, 2, 2, 7);
    case '2': return ISO_G(7, 4, 7, 1, 7);
    case '3': return ISO_G(7, 4, 7, 4, 7);
    case '4': return ISO_G(5, 5, 7, 4, 4);
    case '5': return ISO_G(7, 1, 7, 4, 7);
    case '6': return ISO_G(7, 1, 7, 5, 7);
    case '7': return ISO_G(7, 4, 2, 2, 2);
    case '8': return ISO_G(7, 5, 7, 5, 7);
    case '9': return ISO_G(7, 5, 7, 4, 7);
    case 'A': return ISO_G(2, 5, 7, 5, 5);
    case 'B': return ISO_G(3, 5, 3, 5, 3);
    case 'C': return ISO_G(7, 1, 1, 1, 7);
    case 'D': return ISO_G(3, 5, 5, 5, 3);
    case 'E': return ISO_G(7, 1, 3, 1, 7);
    case 'F': return ISO_G(7, 1, 3, 1, 1);
    case 'G': return ISO_G(7, 1, 5, 5, 7);
    case 'H': return ISO_G(5, 5, 7, 5, 5);
    case 'I': return ISO_G(7, 2, 2, 2, 7);
    case 'J': return ISO_G(4, 4, 4, 5, 7);
    case 'K': return ISO_G(5, 5, 3, 5, 5);
    case 'L': return ISO_G(1, 1, 1, 1, 7);
    case 'M': return ISO_G(5, 7, 7, 5, 5);
    case 'N': return ISO_G(7, 5, 5, 5, 5);
    case 'O': return ISO_G(7, 5, 5, 5, 7);
    case 'P': return ISO_G(7, 5, 7, 1, 1);
    case 'Q': return ISO_G(7, 5, 5, 7, 4);
    case 'R': return ISO_G(3, 5, 3, 5, 5);
    case 'S': return ISO_G(7, 1, 7, 4, 7);
    case 'T': return ISO_G(7, 2, 2, 2, 2);
    case 'U': return ISO_G(5, 5, 5, 5, 7);
    case 'V': return ISO_G(5, 5, 5, 5, 2);
    case 'W': return ISO_G(5, 5, 7, 7, 5);
    case 'X': return ISO_G(5, 5, 2, 5, 5);
    case 'Y': return ISO_G(5, 5, 2, 2, 2);
    case 'Z': return ISO_G(7, 4, 2, 1, 7);
    case '-': return ISO_G(0, 0, 7, 0, 0);
    case '\'': return ISO_G(2, 2, 0, 0, 0);
    default: return 0;
  }
}

static void iso_draw_label(SDL_Renderer *sdl, int x, int y, const char *text)
{
  if (!text || !text[0])
    return;
  int len = 0;
  for (const char *p = text; *p && len < 24; p++)
    len++;
  const int px = 2; // 2x2 pixels per glyph bit so names stay readable at world-fit zoom
  const int gw = 4 * px;
  const int gh = 5 * px;
  const int w = len * gw - px;
  const int bx = x - w / 2;
  const int by = y;
  SDL_SetRenderDrawColor(sdl, 8, 8, 10, 180);
  SDL_Rect plate = {bx - 2, by - 1, w + 4, gh + 2};
  SDL_RenderFillRect(sdl, &plate);
  SDL_SetRenderDrawColor(sdl, 245, 240, 220, 255);
  int cx = bx;
  for (int i = 0; i < len; i++)
  {
    const uint16_t bits = iso_glyph_3x5(text[i]);
    for (int row = 0; row < 5; row++)
    {
      for (int col = 0; col < 3; col++)
      {
        if (bits & (uint16_t)(1u << (row * 3 + col)))
        {
          SDL_Rect dot = {cx + col * px, by + row * px, px, px};
          SDL_RenderFillRect(sdl, &dot);
        }
      }
    }
    cx += gw;
  }
}

static void iso_draw_health_bar(SDL_Renderer *sdl, int sx, int top_y, uint32_t health,
                                uint32_t max_hp, float alpha)
{
  if (alpha <= 0.02f)
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
  const int bar_w = 14;
  const int fill = (int)(t * (float)bar_w + 0.5f);
  const int bx = sx - bar_w / 2;
  const int by = top_y - 4;
  const Uint8 a_back = (Uint8)(200.0f * alpha + 0.5f);
  const Uint8 a_fill = (Uint8)(230.0f * alpha + 0.5f);
  SDL_SetRenderDrawColor(sdl, 20, 16, 12, a_back);
  SDL_Rect back = {bx - 1, by - 1, bar_w + 2, 4};
  SDL_RenderFillRect(sdl, &back);
  if (fill > 0)
  {
    uint8_t r = t > 0.5f ? 70 : 200;
    uint8_t g = t > 0.25f ? 180 : 50;
    uint8_t b = 50;
    SDL_SetRenderDrawColor(sdl, r, g, b, a_fill);
    SDL_Rect hp = {bx, by, fill, 2};
    SDL_RenderFillRect(sdl, &hp);
  }
}

static void iso_draw_facing(SDL_Renderer *sdl, int sx, int sy, float yaw, int length)
{
  float dx = cosf(yaw);
  float dy = sinf(yaw);
  float ssx = dx - dy;
  float ssy = (dx + dy) * 0.5f;
  float slen = sqrtf(ssx * ssx + ssy * ssy);
  if (slen < 0.0001f)
    slen = 1.0f;
  ssx /= slen;
  ssy /= slen;
  const int tip_x = sx + (int)(ssx * (float)length);
  const int tip_y = sy + (int)(ssy * (float)length);
  SDL_SetRenderDrawColor(sdl, 250, 245, 220, 220);
  SDL_RenderDrawLine(sdl, sx, sy, tip_x, tip_y);
  const float px = -ssy;
  const float py = ssx;
  const int wing = length / 3 < 2 ? 2 : length / 3;
  SDL_RenderDrawLine(sdl, tip_x, tip_y,
                     tip_x - (int)(ssx * (float)wing + px * (float)wing),
                     tip_y - (int)(ssy * (float)wing + py * (float)wing));
  SDL_RenderDrawLine(sdl, tip_x, tip_y,
                     tip_x - (int)(ssx * (float)wing - px * (float)wing),
                     tip_y - (int)(ssy * (float)wing - py * (float)wing));
}

static void iso_draw_actor_chrome(IsometricRenderer *renderer, SDL_Renderer *sdl, const Actor *a,
                                  int sx, int sy, const SDL_Rect *dest, float scale)
{
  (void)scale;
  const int top = dest ? dest->y : sy - renderer->voxel_height;
  const bool selected = (renderer->selected_actor_id != 0 && a->id == renderer->selected_actor_id);
  const bool speaking =
      (renderer->dialogue_speaker_id != 0 && a->id == renderer->dialogue_speaker_id);

  // Health bar only while recently hurt and near the camera; fades with time and distance.
  if (a->hurt_display_ttl > 0.0f)
  {
    const float dx = (float)a->x - renderer->camera_world_x;
    const float dy = (float)a->y - renderer->camera_world_y;
    const float dz = (float)a->z - renderer->camera_world_z;
    const float dist_sq = dx * dx + dy * dy + dz * dz;
    const float range = ACTOR_HURT_DISPLAY_RANGE;
    if (dist_sq <= range * range)
    {
      float alpha = 1.0f;
      if (a->hurt_display_ttl < ACTOR_HURT_FADE_SECONDS)
        alpha = a->hurt_display_ttl / ACTOR_HURT_FADE_SECONDS;
      const float dist = sqrtf(dist_sq);
      const float fade_start = ACTOR_HURT_DISPLAY_FADE_START;
      if (dist > fade_start && range > fade_start)
      {
        float d_alpha = 1.0f - (dist - fade_start) / (range - fade_start);
        if (d_alpha < 0.0f)
          d_alpha = 0.0f;
        alpha *= d_alpha;
      }
      if (alpha > 0.02f)
        iso_draw_health_bar(sdl, sx, top, a->health, actor_max_health(a), alpha);
    }
  }
  else if (selected)
  {
    // Selected entities keep a dim full bar so the pick target is clear.
    iso_draw_health_bar(sdl, sx, top, a->health, actor_max_health(a), 0.55f);
  }

  // Nameplates only for the clicked/selected entity (or the active talker).
  if (selected || speaking)
    iso_draw_label(sdl, sx, top - 12, a->name);

  // Corpse loot prompt when the player (camera) is close enough to press F.
  if (a->is_active && a->health == 0)
  {
    const float dx = (float)a->x - renderer->camera_world_x;
    const float dy = (float)a->y - renderer->camera_world_y;
    const float dz = (float)a->z - renderer->camera_world_z;
    const float loot_r = 3.5f;
    if (dx * dx + dy * dy + dz * dz <= loot_r * loot_r)
      iso_draw_label(sdl, sx, top - 24, "F TO LOOT");
  }

  iso_draw_facing(sdl, sx, sy, mob_actor_facing_yaw(a),
                  renderer->tile_width / 3 < 6 ? 6 : renderer->tile_width / 3);

  if (a->is_controlled)
  {
    SDL_SetRenderDrawColor(sdl, 180, 220, 255, 200);
    SDL_Rect rim = dest ? (SDL_Rect){dest->x - 2, dest->y - 2, dest->w + 4, dest->h + 4}
                        : (SDL_Rect){sx - 6, top - 2, 12, sy - top + 4};
    SDL_RenderDrawRect(sdl, &rim);
  }

  if (speaking)
  {
    const int bubble_w = (int)(10.0f * scale + 0.5f);
    const int bubble_h = (int)(7.0f * scale + 0.5f);
    const int bx = sx - bubble_w / 2;
    const int by = top - bubble_h - 14;
    SDL_SetRenderDrawColor(sdl, 250, 248, 235, 230);
    SDL_Rect bubble = {bx, by, bubble_w < 4 ? 4 : bubble_w, bubble_h < 3 ? 3 : bubble_h};
    SDL_RenderFillRect(sdl, &bubble);
    SDL_SetRenderDrawColor(sdl, 40, 36, 30, 220);
    SDL_RenderDrawRect(sdl, &bubble);
    const int dot = (int)(1.0f * scale + 0.5f) < 1 ? 1 : (int)(1.0f * scale + 0.5f);
    const int dy_dots = by + bubble_h / 2 - dot / 2;
    SDL_SetRenderDrawColor(sdl, 40, 36, 30, 255);
    SDL_Rect d0 = {bx + bubble_w / 4 - dot, dy_dots, dot, dot};
    SDL_Rect d1 = {sx - dot / 2, dy_dots, dot, dot};
    SDL_Rect d2 = {bx + (bubble_w * 3) / 4, dy_dots, dot, dot};
    SDL_RenderFillRect(sdl, &d0);
    SDL_RenderFillRect(sdl, &d1);
    SDL_RenderFillRect(sdl, &d2);
  }
}

static bool iso_draw_actor_poly(IsometricRenderer *renderer, SDL_Renderer *sdl, const Actor *a,
                                int world_index, int feet_sx, int feet_sy, SDL_Rect *out_dest)
{
  const char *mesh_name = mob_actor_mesh_name(a);
  if (!mesh_name)
    return false;

  const float cdx = (float)a->x - renderer->camera_world_x;
  const float cdy = (float)a->y - renderer->camera_world_y;
  const float cdz = (float)a->z - renderer->camera_world_z;
  const float actor_dist = sqrtf(cdx * cdx + cdy * cdy + cdz * cdz);
  const int lod = poly_mesh_lod_stride(actor_dist);
  if (lod <= 0)
    return false;

  const PolyMesh *mesh = poly_mesh_get(mesh_name);
  if (!mesh || mesh->vertex_count == 0 || mesh->vertex_count > 8192)
  {
    poly_mesh_init(NULL);
    mesh = poly_mesh_get(mesh_name);
    if (!mesh || mesh->vertex_count == 0 || mesh->vertex_count > 8192)
      return false;
  }

  static float xyz[8192 * 3];
  if (!poly_mesh_sample_ex(mesh, mob_actor_anim_clip(a), mob_actor_anim_time(a), xyz, lod > 1))
    return false;

  const float yaw = mob_actor_facing_yaw(a);
  const float roll = mob_actor_facing_roll(a);
  const float c = cosf(yaw);
  const float s = sinf(yaw);
  const float cr = cosf(roll);
  const float sr = sinf(roll);
  const int vh = renderer->voxel_height;
  int min_sx = feet_sx, max_sx = feet_sx, min_sy = feet_sy, max_sy = feet_sy;
  bool any = false;
  const uint32_t tri_step = 3u * (uint32_t)lod;

  for (int pass = 0; pass < 2; pass++)
  {
    for (uint32_t t = 0; t + 2 < mesh->index_count; t += tri_step)
    {
      const uint32_t ia = mesh->indices[t];
      const uint32_t ib = mesh->indices[t + 1];
      const uint32_t ic = mesh->indices[t + 2];
      if (ia >= mesh->vertex_count || ib >= mesh->vertex_count || ic >= mesh->vertex_count)
        continue;
      SDL_Vertex verts[3];
      const uint32_t ids[3] = {ia, ib, ic};
      float wx[3], wy[3], wz[3];
      int psx[3], psy[3];
      for (int k = 0; k < 3; k++)
      {
        const float lx = xyz[ids[k] * 3];
        const float ly0 = xyz[ids[k] * 3 + 1];
        const float lz0 = xyz[ids[k] * 3 + 2];
        const float ly = ly0 * cr + lz0 * sr;
        const float lz = -ly0 * sr + lz0 * cr;
        wx[k] = (float)a->x + lx * c - ly * s;
        wy[k] = (float)a->y + lx * s + ly * c;
        wz[k] = (float)a->z + lz;
        isometric_world_to_screen_float(renderer, wx[k], wy[k], wz[k], world_index, &psx[k], &psy[k]);
        psy[k] += vh;
      }
      const float area = (float)(psx[1] - psx[0]) * (float)(psy[2] - psy[0]) -
                         (float)(psy[1] - psy[0]) * (float)(psx[2] - psx[0]);
      if (area < 0.0f)
        continue;

      float shade = 0.7f;
      const uint8_t *col = &mesh->colors[ia * 3];
      if (pass == 1)
      {
        const float e1x = wx[1] - wx[0], e1y = wy[1] - wy[0], e1z = wz[1] - wz[0];
        const float e2x = wx[2] - wx[0], e2y = wy[2] - wy[0], e2z = wz[2] - wz[0];
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        const float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nlen > 1e-6f)
          shade = 0.45f + 0.55f * fmaxf(0.0f, (nx * 0.2f + ny * 0.35f + nz * 0.9f) / nlen);
      }

      float cx = ((float)psx[0] + (float)psx[1] + (float)psx[2]) / 3.0f;
      float cy = ((float)psy[0] + (float)psy[1] + (float)psy[2]) / 3.0f;
      for (int k = 0; k < 3; k++)
      {
        float px = (float)psx[k];
        float py = (float)psy[k];
        if (pass == 0)
        {
          float ox = px - cx;
          float oy = py - cy;
          float olen = sqrtf(ox * ox + oy * oy);
          if (olen > 0.001f)
          {
            px += ox / olen * 1.6f;
            py += oy / olen * 1.6f;
          }
        }
        verts[k].position.x = px;
        verts[k].position.y = py;
        if (pass == 0)
        {
          verts[k].color.r = 16;
          verts[k].color.g = 12;
          verts[k].color.b = 10;
          verts[k].color.a = 255;
        }
        else
        {
          verts[k].color.r = (Uint8)fminf(255.0f, (float)col[0] * shade);
          verts[k].color.g = (Uint8)fminf(255.0f, (float)col[1] * shade);
          verts[k].color.b = (Uint8)fminf(255.0f, (float)col[2] * shade);
          verts[k].color.a = 255;
        }
        verts[k].tex_coord.x = 0.0f;
        verts[k].tex_coord.y = 0.0f;
        if (pass == 1)
        {
          if (!any || psx[k] < min_sx) min_sx = psx[k];
          if (!any || psx[k] > max_sx) max_sx = psx[k];
          if (!any || psy[k] < min_sy) min_sy = psy[k];
          if (!any || psy[k] > max_sy) max_sy = psy[k];
          any = true;
        }
      }
      SDL_RenderGeometry(sdl, NULL, verts, 3, NULL, 0);
    }
  }

  if (!any)
    return false;
  if (out_dest)
  {
    out_dest->x = min_sx;
    out_dest->y = min_sy;
    out_dest->w = max_sx - min_sx + 1;
    out_dest->h = max_sy - min_sy + 1;
  }
  (void)feet_sx;
  (void)feet_sy;
  return true;
}

static void iso_draw_actor_fallback(IsometricRenderer *renderer, SDL_Renderer *sdl, const Actor *a,
                                    int sx, int sy, uint8_t br, uint8_t bg, uint8_t bb,
                                    bool bird, SDL_Rect *out_dest)
{
  const int tw = renderer->tile_width > 0 ? renderer->tile_width : 32;
  const int th = renderer->tile_height > 0 ? renderer->tile_height : 16;
  const int vh = renderer->voxel_height > 0 ? renderer->voxel_height : 8;
  const BirdKind kind = bird ? mob_actor_bird_kind(a) : BIRD_KIND_CROW;
  const int body_h = bird ? (vh * 2 / 3 < 8 ? 8 : vh * 2 / 3) : (vh < 12 ? 12 : vh);
  const int body_w = bird ? (tw / 3 < 6 ? 6 : tw / 3) : (tw / 2 < 8 ? 8 : tw / 2);
  const int head = bird ? (body_w / 2 < 3 ? 3 : body_w / 2) : (body_w / 3 < 4 ? 4 : body_w / 3);
  const int top = sy - body_h - head;

  iso_fill_quad(sdl, sx, sy - 2, sx + body_w / 2, sy - body_h / 4,
                sx, sy - body_h / 2, sx - body_w / 2, sy - body_h / 4,
                (uint8_t)(br * 7 / 10), (uint8_t)(bg * 7 / 10), (uint8_t)(bb * 7 / 10), 255);

  if (bird)
  {
    const int wing = kind == BIRD_KIND_GULL ? body_w : (kind == BIRD_KIND_SPARROW ? body_w * 2 / 3 : body_w * 5 / 6);
    const int wy = sy - body_h / 2 - (a->is_flying ? th / 4 : 0);
    iso_fill_quad(sdl, sx - wing, wy, sx, wy - 2, sx, wy + 2, sx - wing, wy + 1,
                  (uint8_t)(br * 8 / 10), (uint8_t)(bg * 8 / 10), (uint8_t)(bb * 8 / 10), 230);
    iso_fill_quad(sdl, sx + wing, wy, sx, wy - 2, sx, wy + 2, sx + wing, wy + 1,
                  (uint8_t)(br * 8 / 10), (uint8_t)(bg * 8 / 10), (uint8_t)(bb * 8 / 10), 230);
  }

  iso_fill_quad(sdl, sx, sy - body_h / 2 - 2, sx + body_w / 2, sy - body_h,
                sx, top + head, sx - body_w / 2, sy - body_h,
                br, bg, bb, 255);
  iso_fill_quad(sdl, sx, top, sx + head / 2, top + head / 2,
                sx, top + head, sx - head / 2, top + head / 2,
                (uint8_t)(br + 24 > 255 ? 255 : br + 24),
                (uint8_t)(bg + 18 > 255 ? 255 : bg + 18),
                (uint8_t)(bb + 12 > 255 ? 255 : bb + 12), 255);

  if (out_dest)
  {
    out_dest->x = sx - body_w / 2 - 1;
    out_dest->y = top;
    out_dest->w = body_w + 2;
    out_dest->h = sy - top + 1;
  }
}

static double iso_actor_depth_key(const Actor *a)
{
  return a->y * 8192.0 + a->x + a->z * (1.0 / 2048.0);
}

static void actors_render_isometric(IsometricRenderer *renderer, SDL_Renderer *sdl_renderer,
                                    int world_index)
{
  if (!renderer || !sdl_renderer)
    return;

  World *world = NULL;
  if (renderer->game_worlds && renderer->game_worlds->home_world)
    world = renderer->game_worlds->home_world;
  if (!world || !world->runtime_actors || world->runtime_actor_count <= 0)
    return;

  SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
  const float scale = renderer->zoom_scale > 0.0f ? renderer->zoom_scale : 1.0f;

  int order[256];
  const int count = world->runtime_actor_count < 256 ? world->runtime_actor_count : 256;
  int n = 0;
  for (int i = 0; i < count; i++)
  {
    if (world->runtime_actors[i].is_active)
      order[n++] = i;
  }
  if (n == 0)
    return;
  for (int i = 1; i < n; i++)
  {
    const int key = order[i];
    const double dk = iso_actor_depth_key(&world->runtime_actors[key]);
    int j = i - 1;
    while (j >= 0 && iso_actor_depth_key(&world->runtime_actors[order[j]]) > dk)
    {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = key;
  }

  for (int oi = 0; oi < n; oi++)
  {
    const Actor *a = &world->runtime_actors[order[oi]];

    int sx = 0, sy = 0;
    iso_actor_to_screen(renderer, (float)a->x, (float)a->y, (float)a->z, world_index, &sx, &sy);

    const bool inhabited = a->is_controlled;
    const bool bird = mob_actor_is_bird(a);
    uint8_t br = inhabited ? 168 : 132;
    uint8_t bg = inhabited ? 118 : 86;
    uint8_t bb = inhabited ? 72 : 48;
    if (bird)
    {
      mob_actor_bird_color(a, &br, &bg, &bb);
      if (inhabited)
      {
        br = (uint8_t)(br + (255 - br) / 4);
        bg = (uint8_t)(bg + (255 - bg) / 4);
        bb = (uint8_t)(bb + (255 - bb) / 4);
      }
    }

    iso_draw_actor_shadow(renderer, sdl_renderer, sx, sy);

    SDL_Rect dest = {sx - 4, sy - renderer->voxel_height, 8, renderer->voxel_height};
    bool drawn = iso_draw_actor_poly(renderer, sdl_renderer, a, world_index, sx, sy, &dest);
    if (!drawn && mob_models_enabled())
    {
      mob_models_init(NULL);
      const MobModel *model = mob_models_for_actor(a);
      if (model)
      {
        uint8_t mr = bird ? br : 255;
        uint8_t mg = bird ? bg : 255;
        uint8_t mb = bird ? bb : 255;
        drawn = mob_sprite_draw(sdl_renderer, model, renderer->tile_width, renderer->tile_height,
                                renderer->voxel_height, sx, sy, mr, mg, mb, &dest);
      }
    }
    if (!drawn)
      iso_draw_actor_fallback(renderer, sdl_renderer, a, sx, sy, br, bg, bb, bird, &dest);

    iso_draw_actor_chrome(renderer, sdl_renderer, a, sx, sy, &dest, scale);
  }
}

// In-flight projectiles, drawn as a hot core inside a couple of dimmer haloes.
//
// Concentric rects rather than a sprite: it costs three draws, needs no texture to load or free, and
// at the scale a fireball occupies on an isometric screen — a handful of pixels — a glow reads better
// than any detail would. The projectile's own world position is projected with the same helper the
// particles use, so it sits in the scene at the right depth rather than floating over it.
static void projectiles_render_isometric(IsometricRenderer *renderer, SDL_Renderer *sdl_renderer,
                                         int world_index)
{
  if (!renderer || !sdl_renderer || !renderer->projectiles)
    return;

  SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < PROJECTILE_MAX; i++)
  {
    const Projectile *p = &renderer->projectiles->items[i];
    if (!p->active)
      continue;

    int sx = 0, sy = 0;
    isometric_world_to_screen_float(renderer, p->x, p->y, p->z, world_index, &sx, &sy);

    // Scale with the zoom so a fireball stays the same size relative to the voxels around it.
    const float scale = renderer->zoom_scale > 0.0f ? renderer->zoom_scale : 1.0f;
    int core = (int)(3.0f * scale + 0.5f);
    if (core < 2)
      core = 2;

    // Fades as it burns out, so a fireball about to expire visibly weakens.
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

    typedef struct { int r; uint8_t cr, cg, cb, ca; } ProjRing;
    const ProjRing fire_rings[] = {
        {core * 3, 200, 60, 10, (uint8_t)(60.0f * fade)},
        {core * 2, 240, 130, 30, (uint8_t)(140.0f * fade)},
        {core, 255, 240, 190, (uint8_t)(255.0f * fade)},
    };
    const ProjRing ice_rings[] = {
        {core * 3, 40, 90, 180, (uint8_t)(70.0f * fade)},
        {core * 2, 120, 190, 240, (uint8_t)(150.0f * fade)},
        {core, 230, 250, 255, (uint8_t)(255.0f * fade)},
    };
    const ProjRing missile_rings[] = {
        {core * 3, 90, 40, 160, (uint8_t)(70.0f * fade)},
        {core * 2, 170, 100, 230, (uint8_t)(150.0f * fade)},
        {core, 240, 210, 255, (uint8_t)(255.0f * fade)},
    };
    const ProjRing lightning_rings[] = {
        {core * 3, 40, 80, 200, (uint8_t)(70.0f * fade)},
        {core * 2, 180, 210, 255, (uint8_t)(160.0f * fade)},
        {core, 255, 255, 220, (uint8_t)(255.0f * fade)},
    };
    const ProjRing shadow_rings[] = {
        {core * 3, 60, 10, 80, (uint8_t)(80.0f * fade)},
        {core * 2, 140, 40, 160, (uint8_t)(150.0f * fade)},
        {core, 200, 120, 220, (uint8_t)(255.0f * fade)},
    };
    const ProjRing meteor_rings[] = {
        {core * 4, 120, 40, 10, (uint8_t)(90.0f * fade)},
        {core * 3, 200, 80, 20, (uint8_t)(160.0f * fade)},
        {core * 2, 255, 180, 60, (uint8_t)(220.0f * fade)},
        {core, 255, 240, 200, (uint8_t)(255.0f * fade)},
    };
    const ProjRing *rings = fire_rings;
    size_t ring_count = 3;
    if (p->kind == PROJECTILE_ICE_BOLT)
      rings = ice_rings;
    else if (p->kind == PROJECTILE_MAGIC_MISSILE)
      rings = missile_rings;
    else if (p->kind == PROJECTILE_LIGHTNING)
      rings = lightning_rings;
    else if (p->kind == PROJECTILE_SHADOW_STRIKE)
      rings = shadow_rings;
    else if (p->kind == PROJECTILE_METEOR)
    {
      rings = meteor_rings;
      ring_count = 4;
    }

    for (size_t k = 0; k < ring_count; k++)
    {
      SDL_SetRenderDrawColor(sdl_renderer, rings[k].cr, rings[k].cg, rings[k].cb, rings[k].ca);
      SDL_Rect rect = {sx - rings[k].r, sy - rings[k].r, rings[k].r * 2 + 1, rings[k].r * 2 + 1};
      SDL_RenderFillRect(sdl_renderer, &rect);
    }
  }
}

// Broken voxel chunks on the ground / in flight. Icon inventory drops use the item atlas;
// voxel debris stays as small shaded cubes at DEBRIS_SIZE.
static void debris_render_isometric(IsometricRenderer *renderer, SDL_Renderer *sdl_renderer,
                                    int world_index)
{
  if (!renderer || !sdl_renderer || !renderer->debris)
    return;

  SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
  const float scale = renderer->zoom_scale > 0.0f ? renderer->zoom_scale : 1.0f;
  const int half = (int)(DEBRIS_SIZE * 0.5f * (float)renderer->tile_width * scale + 0.5f);
  const int h = half < 1 ? 1 : half;
  const int icon_sz = (int)(10.0f * scale + 0.5f);
  const int iz = icon_sz < 8 ? 8 : (icon_sz > 24 ? 24 : icon_sz);

  for (int i = 0; i < DEBRIS_MAX; i++)
  {
    const DebrisPiece *d = &renderer->debris->items[i];
    if (!d->active)
      continue;

    int sx = 0, sy = 0;
    isometric_world_to_screen_float(renderer, d->x, d->y, d->z, world_index, &sx, &sy);

    if (d->icon_sprite && d->item != ITEM_NONE && item_icon_texture(d->item))
    {
      // Soft ground shadow under the bobbing icon.
      SDL_SetRenderDrawColor(sdl_renderer, 8, 6, 4, 140);
      SDL_Rect shadow = {sx - iz / 2, sy + 1, iz, iz / 3};
      SDL_RenderFillRect(sdl_renderer, &shadow);

      const float bob = d->settled ? (sinf((float)SDL_GetTicks() * 0.004f + (float)i) * 1.5f) : 0.0f;
      item_icon_draw(sdl_renderer, d->item, sx - iz / 2, sy - iz + (int)bob, iz);
      continue;
    }

    SDL_SetRenderDrawColor(sdl_renderer, (Uint8)(d->r * 3 / 4), (Uint8)(d->g * 3 / 4),
                           (Uint8)(d->b * 3 / 4), 230);
    SDL_Rect shadow = {sx - h, sy + h / 2, h * 2 + 1, h + 1};
    SDL_RenderFillRect(sdl_renderer, &shadow);

    SDL_SetRenderDrawColor(sdl_renderer, d->r, d->g, d->b, 255);
    SDL_Rect body = {sx - h, sy - h, h * 2 + 1, h * 2 + 1};
    SDL_RenderFillRect(sdl_renderer, &body);

    SDL_SetRenderDrawColor(sdl_renderer,
                           (Uint8)(d->r < 40 ? 0 : d->r - 40),
                           (Uint8)(d->g < 40 ? 0 : d->g - 40),
                           (Uint8)(d->b < 40 ? 0 : d->b - 40), 255);
    SDL_RenderDrawRect(sdl_renderer, &body);
  }
}
