// Headless audit of the first-person renderer.
//
// Three things are checked, because each corresponds to a class of bug that produced a plausible
// looking but wrong image rather than a crash:
//   1. Face quads are planar on the axis they are normal to (geometry).
//   2. Projection agrees with the ray the raycast path builds for the same pixel (camera math).
//   3. A rendered frame has plausible channel statistics (pixel format / palette).
//
// Sparse fittings (door / roof / glass) are also checked under FP_MODE_MESH: transparent bake
// texels must punch through so a wall behind a door panel is visible. FP_MODE_RAY keeps a full-cube
// flat fill on those gaps by design — see fp_shade_ray_hit.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "fp_renderer.h"
#include "shadow_world.h"
#include "voxel_mesh.h"
#include "world.h"

static int g_failures = 0;

static void check(bool ok, const char *what)
{
  printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
  if (!ok)
    g_failures++;
}

// Write the frame out when VERSE_FP_AUDIT_DUMP gives a path prefix, as <prefix>-<mode>.bmp.
//
// The statistics below can tell you that texels are reaching the frame but not whether they look
// like grass, so when one of those checks fails the next question is always what the image actually
// shows. Off by default; the test stays headless and writes nothing unless asked.
static void dump_frame_if_asked(const Uint32 *px, int w, int h, const char *mode_name)
{
  const char *prefix = getenv("VERSE_FP_AUDIT_DUMP");
  if (!prefix || !*prefix)
    return;

  char path[512];
  snprintf(path, sizeof(path), "%s-%s.bmp", prefix, mode_name);

  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA8888);
  if (!s)
    return;
  for (int y = 0; y < h; y++)
    memcpy((Uint8 *)s->pixels + (size_t)y * s->pitch, px + (size_t)y * w, (size_t)w * 4);

  // BMP cannot hold an alpha channel, and the frame is opaque anyway.
  SDL_Surface *rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
  if (rgb)
  {
    printf("       %s frame -> %s\n", SDL_SaveBMP(rgb, path) == 0 ? "wrote" : "failed to write",
           path);
    SDL_FreeSurface(rgb);
  }
  SDL_FreeSurface(s);
}

// A floor with a wall standing on it: enough structure that a correct render must show a
// horizon, a lit top surface, and a shaded vertical face.
static World *build_test_world(void)
{
  World *w = world_create(32, 32, 32);
  if (!w)
    return NULL;

  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
      for (uint32_t z = 0; z < 4; z++)
        world_set_voxel(w, x, y, z, z == 3 ? VOXEL_GRASS : VOXEL_SOIL);

  // Kept low enough that a camera at z=9 sees both its top and its near side.
  for (uint32_t y = 10; y < 22; y++)
    for (uint32_t z = 4; z < 8; z++)
      world_set_voxel(w, 20, y, z, VOXEL_STONE);

  world_refresh_occupancy_bitfield(w);
  return w;
}

static void test_quad_planarity(const World *world)
{
  VoxelMesh mesh;
  voxel_mesh_init(&mesh);
  voxel_mesh_build_all_faces_greedy(world, &mesh);

  check(mesh.count > 0, "mesh builds quads for the test world");

  int face_seen[6] = {0};
  int non_planar = 0;
  int degenerate = 0;

  for (int i = 0; i < mesh.count; i++)
  {
    const VoxelFaceQuad *q = &mesh.quads[i];
    if (q->face < 0 || q->face >= 6)
      continue;
    face_seen[q->face]++;

    float c[4][3];
    fp_renderer_debug_quad_corners(q, c);

    // The axis the face is normal to must be identical across all four corners, and the other
    // two axes must both span a non-zero extent.
    const int axis = (q->face == 0 || q->face == 1) ? 2 : (q->face == 2 || q->face == 4) ? 1 : 0;
    for (int k = 1; k < 4; k++)
    {
      if (fabsf(c[k][axis] - c[0][axis]) > 1e-6f)
      {
        non_planar++;
        break;
      }
    }

    float span[3] = {0.0f, 0.0f, 0.0f};
    for (int a = 0; a < 3; a++)
    {
      float lo = c[0][a], hi = c[0][a];
      for (int k = 1; k < 4; k++)
      {
        if (c[k][a] < lo) lo = c[k][a];
        if (c[k][a] > hi) hi = c[k][a];
      }
      span[a] = hi - lo;
    }
    int spanning = (span[0] > 0.0f) + (span[1] > 0.0f) + (span[2] > 0.0f);
    if (spanning != 2)
      degenerate++;
  }

  printf("       quads=%d faces{+Z=%d -Z=%d +Y=%d +X=%d -Y=%d -X=%d}\n", mesh.count,
         face_seen[0], face_seen[1], face_seen[2], face_seen[3], face_seen[4], face_seen[5]);

  check(non_planar == 0, "every quad is planar on its normal axis");
  check(degenerate == 0, "every quad spans exactly two axes");
  check(face_seen[0] > 0 && face_seen[3] > 0, "top and +X faces are both present");

  voxel_mesh_free(&mesh);
}

// Project a point, then rebuild the ray the raycast path would fire through that pixel. A
// correct projection puts the point on that ray.
static void test_projection_matches_ray(void)
{
  const int tw = 200, th = 150;
  FPCamera cam = {.x = 4.0f, .y = 6.0f, .z = 5.0f, .yaw = 0.7f, .pitch = -0.2f, .fov_deg = 60.0f};

  const float pts[4][3] = {
      {14.0f, 6.5f, 5.0f}, {20.0f, 12.0f, 8.0f}, {9.0f, 2.0f, 3.0f}, {30.0f, 20.0f, 12.0f}};

  float worst = 0.0f;
  for (int i = 0; i < 4; i++)
  {
    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    if (!fp_renderer_debug_project(&cam, tw, th, pts[i][0], pts[i][1], pts[i][2], &sx, &sy, &depth))
      continue;

    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    fp_renderer_debug_pixel_ray(&cam, tw, th, sx, sy, &rx, &ry, &rz);

    // Point should lie along the ray at parameter `depth` (measured along forward).
    const float dx = pts[i][0] - cam.x;
    const float dy = pts[i][1] - cam.y;
    const float dz = pts[i][2] - cam.z;
    const float err_x = fabsf(rx * depth - dx);
    const float err_y = fabsf(ry * depth - dy);
    const float err_z = fabsf(rz * depth - dz);
    const float err = fmaxf(err_x, fmaxf(err_y, err_z));
    if (err > worst)
      worst = err;
  }

  printf("       worst projection/ray disagreement: %.5f world units\n", worst);
  check(worst < 1e-3f, "projection is the exact inverse of pixel ray generation");
}

// Audit one frame rendered through `mode`.
//
// Both modes are checked because they share nothing but the camera: the raycast path samples a
// material per pixel from the voxel it hit, the mesh path interpolates UVs across a greedy-merged
// quad. Texturing one and not the other is an easy mistake to make and an invisible one, since
// FP_MODE_RAY is the default and the mesh path is what a test reaches for by name.
static void test_rendered_frame(const World *world, FPMode mode, const char *mode_name)
{
  printf("[frame: %s]\n", mode_name);

  SDL_Window *win = SDL_CreateWindow("fp-audit", 0, 0, 320, 240, SDL_WINDOW_HIDDEN);
  SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
  if (!ren)
  {
    printf("  skip  no SDL renderer available (%s)\n", SDL_GetError());
    if (win)
      SDL_DestroyWindow(win);
    return;
  }

  const int W = 320, H = 240;
  SDL_Texture *target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, W, H);
  check(target != NULL, "render target created");
  if (!target)
    return;

  SDL_SetRenderTarget(ren, target);

  // Raised and angled down so the stone wall shows both its top (+Z) and its near side (-X).
  // Seeing one material at two orientations is what makes the face shading testable.
  FPCamera cam = {.x = 8.0f, .y = 16.0f, .z = 9.0f, .yaw = 0.0f, .pitch = -0.35f, .fov_deg = 60.0f};

  GameWorlds gw = {0};
  gw.home_world = (World *)world;

  fp_renderer_set_mode(mode);
  fp_renderer_enable_gpu_mesh(false);
  fp_renderer_render_neighbors(ren, &gw, &cam, 0, 0, W, H, 0, NULL);

  Uint32 *px = (Uint32 *)malloc((size_t)W * H * sizeof(Uint32));
  check(SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA8888, px, W * 4) == 0,
        "frame read back");

  dump_frame_if_asked(px, W, H, mode_name);

  long sum_r = 0, sum_g = 0, sum_b = 0;
  int r_pinned = 0, sky_pixels = 0, green_pixels = 0;
  int distinct_surface = 0;
  int neutral_lumas[64];
  int neutral_luma_count = 0;

  // Exact-colour set, small because the scene uses two materials and no fog at this range.
  Uint32 surface_colors[64];
  int surface_color_count = 0;

  // Colours seen on the grass, tracked separately. The whole grass floor greedy-merges into one
  // quad at one distance, so flat shading paints every pixel of it the same colour; more than one
  // can only come from the material's baked texels.
  Uint32 green_colors[64];
  int green_color_count = 0;

  for (int i = 0; i < W * H; i++)
  {
    const Uint32 v = px[i];
    const int r = (int)((v >> 24) & 0xFF);
    const int g = (int)((v >> 16) & 0xFF);
    const int b = (int)((v >> 8) & 0xFF);
    sum_r += r;
    sum_g += g;
    sum_b += b;
    if (r == 255)
      r_pinned++;

    if (r == 20 && g == 22 && b == 35)
    {
      sky_pixels++;
      continue;
    }

    if (g > r + 10 && g > b + 10)
    {
      green_pixels++;
      bool green_known = false;
      for (int k = 0; k < green_color_count; k++)
        if (green_colors[k] == v)
        {
          green_known = true;
          break;
        }
      if (!green_known && green_color_count < 64)
        green_colors[green_color_count++] = v;
    }

    bool known = false;
    for (int k = 0; k < surface_color_count; k++)
      if (surface_colors[k] == v)
      {
        known = true;
        break;
      }
    if (!known && surface_color_count < 64)
    {
      surface_colors[surface_color_count++] = v;
      distinct_surface++;

      // Near-neutral means stone. Two neutral luminances can only come from the same material
      // being shaded differently per face orientation.
      if (abs(r - g) < 12 && abs(g - b) < 12 && r > 30)
      {
        const int luma = (r + g + b) / 3;
        bool dup = false;
        for (int k = 0; k < neutral_luma_count; k++)
          if (abs(neutral_lumas[k] - luma) < 4)
          {
            dup = true;
            break;
          }
        if (!dup && neutral_luma_count < 64)
          neutral_lumas[neutral_luma_count++] = luma;
      }
    }
  }

  const int total = W * H;
  printf("       mean rgb = (%.1f, %.1f, %.1f), r==255 in %.1f%% of pixels\n",
         (double)sum_r / total, (double)sum_g / total, (double)sum_b / total,
         100.0 * r_pinned / total);
  printf("       sky %.1f%%, green %.1f%%, %d distinct surface colours, %d stone luminances\n",
         100.0 * sky_pixels / total, 100.0 * green_pixels / total,
         distinct_surface, neutral_luma_count);
  printf("       %d distinct colours on the grass\n", green_color_count);
  for (int k = 0; k < surface_color_count && k < 8; k++)
    printf("         surface #%d rgb(%u,%u,%u)\n", k,
           (unsigned)((surface_colors[k] >> 24) & 0xFF),
           (unsigned)((surface_colors[k] >> 16) & 0xFF),
           (unsigned)((surface_colors[k] >> 8) & 0xFF));

  // The red-tint bug pinned red at 255 everywhere; a correct frame of grass and stone has red
  // below green and no saturated pixels at all.
  check(r_pinned == 0, "red channel is never pinned at maximum");
  check(sum_g > sum_r, "grass scene is greener than it is red");
  check(green_pixels > total / 20, "grass reads as green over a meaningful area");
  check(sky_pixels > total / 100, "some sky is visible above the horizon");
  check(sky_pixels < total * 4 / 5, "the world occupies most of the lower frame");
  check(distinct_surface >= 3, "at least three distinct surface colours are drawn");
  check(neutral_luma_count >= 2, "stone is shaded differently per face orientation");

  // Sub-voxel detail. Flat shading can only produce one colour per material-and-orientation pair,
  // and this scene has three materials over six orientations — eighteen at the absolute most, and
  // far fewer in practice because only some of those pairs face the camera. Exceeding it means the
  // 32x32x32 material worlds reached the frame.
  check(distinct_surface > 18, "material detail from the nested worlds reaches the frame");
  check(green_color_count > 4, "the grass floor is textured rather than one flat green");

  free(px);
  SDL_SetRenderTarget(ren, NULL);
  SDL_DestroyTexture(target);
  fp_renderer_release_renderer();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
}

// A flat world of the size the shadow cluster wants, optionally with a wall standing near its -X
// edge. As a neighbour to the east that wall is the thing whose appearance proves the cluster is
// being drawn; the floor proves the ground carries on past the boundary instead of falling away
// into sky.
static World *build_cluster_slot(bool with_wall)
{
  World *w = world_create(64, 64, 64);
  if (!w)
    return NULL;

  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
      for (uint32_t z = 0; z < 4; z++)
        world_set_voxel(w, x, y, z, z == 3 ? VOXEL_GRASS : VOXEL_SOIL);

  if (with_wall)
  {
    // Ten high, so it stands above the eye and therefore above the horizon, but leaves sky above
    // itself for the frame to still contain some.
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < 8; x++)
        for (uint32_t z = 4; z < 14; z++)
          world_set_voxel(w, x, y, z, VOXEL_STONE);
  }

  world_refresh_occupancy_bitfield(w);
  return w;
}

// Render the cluster twice from a spot near the eastern boundary, once with the world to the east
// attached and once without, and compare.
//
// Two things are being established. The obvious one is that a neighbour is drawn at all. The other
// is the invariant the whole approach rests on: the neighbour pass runs after the centre world and
// is only allowed to touch pixels the centre world left as sky. If it ever painted over centre
// geometry, the near world would flicker with things standing behind it, so the pixels the centre
// world drew are required to come out bit-identical either way.
static void test_cluster_neighbours(void)
{
  printf("[cluster]\n");

  SDL_Window *win = SDL_CreateWindow("fp-cluster", 0, 0, 320, 240, SDL_WINDOW_HIDDEN);
  SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
  if (!ren)
  {
    printf("  skip  no SDL renderer available (%s)\n", SDL_GetError());
    if (win)
      SDL_DestroyWindow(win);
    return;
  }

  const int W = 320, H = 240;
  SDL_Texture *target =
      SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, H);
  World *centre = build_cluster_slot(false);
  World *east = build_cluster_slot(true);
  ShadowWorld *cluster = shadow_world_create(64, 64, 64);
  Uint32 *before = (Uint32 *)malloc((size_t)W * H * sizeof(Uint32));
  Uint32 *after = (Uint32 *)malloc((size_t)W * H * sizeof(Uint32));

  if (!target || !centre || !east || !cluster || !before || !after)
  {
    check(false, "cluster fixture built");
    goto done;
  }

  SDL_SetRenderTarget(ren, target);
  shadow_world_set_centre(cluster, 8, 8, 8);
  shadow_world_attach(cluster, 0, 0, 0, centre);
  shadow_world_refresh(cluster);

  // Level, so the horizon sits on the middle row and anything drawn above it is standing geometry
  // rather than ground. Far enough back from the boundary at x=64 that the centre world still owns
  // the lower frame.
  FPCamera cam = {.x = 40.0f, .y = 32.0f, .z = 6.0f, .yaw = 0.0f, .pitch = 0.0f, .fov_deg = 60.0f};

  fp_renderer_set_mode(FP_MODE_MESH);
  fp_renderer_enable_gpu_mesh(false);

  fp_renderer_render_cluster(ren, cluster, &cam, 0, 0, W, H, NULL);
  check(SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA8888, before, W * 4) == 0,
        "frame without neighbours read back");

  shadow_world_attach(cluster, 1, 0, 0, east);
  shadow_world_refresh(cluster);
  fp_renderer_render_cluster(ren, cluster, &cam, 0, 0, W, H, NULL);
  check(SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA8888, after, W * 4) == 0,
        "frame with the eastern world read back");

  dump_frame_if_asked(before, W, H, "cluster-alone");
  dump_frame_if_asked(after, W, H, "cluster-neighbour");

  // The frame's clear colour, rgb(20,22,35), packed the way RGBA8888 reads back.
  const Uint32 sky = ((Uint32)20 << 24) | ((Uint32)22 << 16) | ((Uint32)35 << 8) | 0xFFu;
  int sky_before = 0, sky_after = 0, filled = 0, filled_above_horizon = 0, clobbered = 0;

  // The wall stands flush against the boundary, which is the awkward case for the material sampler:
  // a ray that begins exactly on the plane can begin inside the wall's first cell, and a cell the
  // ray was already inside has no entry face to read a texel from, so the whole wall comes out one
  // flat grey.
  //
  // Counting colours is too blunt to catch that, since a flat wall still fog-bands into several.
  // What separates the two is the hue: stone's palette colour is neutral, the face key scales all
  // three channels alike, and fog blends towards a sky whose blue exceeds its red, so no amount of
  // flat shading can put blue below red. A texel can, and stone's bake does.
  Uint32 gained_colors[64];
  int gained_color_count = 0;
  int warm_gained = 0;

  for (int i = 0; i < W * H; i++)
  {
    const bool was_sky = (before[i] == sky);
    const bool is_sky = (after[i] == sky);
    sky_before += was_sky;
    sky_after += is_sky;
    if (was_sky && !is_sky)
    {
      filled++;
      if (i / W < H / 2)
        filled_above_horizon++;

      bool known = false;
      for (int k = 0; k < gained_color_count; k++)
        if (gained_colors[k] == after[i])
        {
          known = true;
          break;
        }
      if (!known && gained_color_count < 64)
        gained_colors[gained_color_count++] = after[i];

      const int r = (int)((after[i] >> 24) & 0xFF);
      const int b = (int)((after[i] >> 8) & 0xFF);
      if (b < r - 8)
        warm_gained++;
    }
    if (!was_sky && after[i] != before[i])
      clobbered++;
  }

  const int total = W * H;
  printf("       sky %.1f%% alone -> %.1f%% with the eastern world\n", 100.0 * sky_before / total,
         100.0 * sky_after / total);
  printf("       %d pixels gained (%d of them above the horizon), %d centre pixels changed\n",
         filled, filled_above_horizon, clobbered);
  printf("       %d distinct colours across the gained pixels, %d carrying texel hue\n",
         gained_color_count, warm_gained);
  for (int k = 0; k < gained_color_count && k < 8; k++)
    printf("         gained #%d rgb(%u,%u,%u)\n", k,
           (unsigned)((gained_colors[k] >> 24) & 0xFF), (unsigned)((gained_colors[k] >> 16) & 0xFF),
           (unsigned)((gained_colors[k] >> 8) & 0xFF));

  check(sky_before > total / 4, "the world edge leaves a lot of sky when nothing is beyond it");
  check(filled > total / 50, "the eastern world fills a meaningful part of that sky");
  check(filled_above_horizon > 0, "its wall is drawn standing above the horizon");
  check(clobbered == 0, "neighbours never overwrite what the centre world drew");
  check(warm_gained > 0, "a neighbour flush against the boundary is textured, not flat");

done:
  free(before);
  free(after);
  if (cluster)
    shadow_world_destroy(cluster);
  if (centre)
    world_destroy(centre);
  if (east)
    world_destroy(east);
  SDL_SetRenderTarget(ren, NULL);
  if (target)
    SDL_DestroyTexture(target);
  fp_renderer_release_renderer();
  fp_renderer_invalidate_cache();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
}

static double measure_cluster_frames(SDL_Renderer *ren, const ShadowWorld *cluster,
                                     const FPCamera *cam, int w, int h)
{
  const int frames = 30;
  fp_renderer_render_cluster(ren, cluster, cam, 0, 0, w, h, NULL); // mesh build outside the timing
  const Uint64 t0 = SDL_GetPerformanceCounter();
  for (int i = 0; i < frames; i++)
    fp_renderer_render_cluster(ren, cluster, cam, 0, 0, w, h, NULL);
  const Uint64 t1 = SDL_GetPerformanceCounter();
  return 1000.0 * (double)(t1 - t0) / (double)SDL_GetPerformanceFrequency() / frames;
}

// Rolling ground on top of the floor: the shape a real neighbour has, where a ray aimed anywhere
// near the horizon meets something within a few voxels and stops there.
static void add_hills(World *w)
{
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      const uint32_t top = 5 + (uint32_t)(5.0 * (1.0 + sinf((float)x * 0.19f) * cosf((float)y * 0.23f)));
      for (uint32_t z = 4; z < top; z++)
        world_set_voxel(w, x, y, z, VOXEL_SOIL);
      world_set_voxel(w, x, y, top, VOXEL_GRASS);
    }
  world_refresh_occupancy_bitfield(w);
}

// Price the neighbour pass against the centre world it is added to, over two scenes.
//
// The first is what play looks like: rolling ground in every direction, where most rays meet
// something quickly. The second is the pathological one, and it is the bound worth knowing. Its
// neighbours hold a single thin spire each, which is the awkward shape: the spire lifts the band of
// layers the neighbours occupy far above the eye, so no ray can be dismissed for leaving that band,
// while the air stays almost entirely empty, so the rays that survive find nothing and run their
// step budget to the end. Both scenes look at a level horizon, which puts half the frame in sky and
// makes every one of those pixels a neighbour ray.
//
// Only a ratio is asserted, against the centre world's own raster as a yardstick for how fast this
// machine is. The absolute figures belong in the log, where they are worth reading, but turning a
// wall clock into a pass condition just makes the suite fail on a loaded machine.
static void test_cluster_cost(void)
{
  printf("[cluster cost]\n");

  SDL_Window *win = SDL_CreateWindow("fp-cost", 0, 0, 256, 240, SDL_WINDOW_HIDDEN);
  SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
  if (!ren)
  {
    printf("  skip  no SDL renderer available (%s)\n", SDL_GetError());
    if (win)
      SDL_DestroyWindow(win);
    return;
  }

  // The resolution the game actually draws first person at.
  const int W = 256, H = 240;
  SDL_Texture *target =
      SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, H);
  ShadowWorld *cluster = shadow_world_create(64, 64, 64);
  World *slots[9] = {0};
  for (int i = 0; i < 9; i++)
  {
    slots[i] = build_cluster_slot(false);
    if (slots[i])
      add_hills(slots[i]);
  }

  if (!target || !cluster || !slots[8])
  {
    check(false, "cost fixture built");
    goto done;
  }

  SDL_SetRenderTarget(ren, target);
  shadow_world_set_centre(cluster, 8, 8, 8);
  shadow_world_attach(cluster, 0, 0, 0, slots[8]);
  shadow_world_refresh(cluster);

  // Level, and high enough to stand clear of the hills.
  FPCamera cam = {.x = 32.0f, .y = 32.0f, .z = 17.0f, .yaw = 0.4f, .pitch = 0.0f, .fov_deg = 60.0f};

  fp_renderer_set_mode(FP_MODE_MESH);
  fp_renderer_enable_gpu_mesh(false);

  const double centre_ms = measure_cluster_frames(ren, cluster, &cam, W, H);

  int attached = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if ((dx || dy) && shadow_world_attach(cluster, dx, dy, 0, slots[attached]))
        attached++;
  shadow_world_refresh(cluster);

  const double hills_ms = measure_cluster_frames(ren, cluster, &cam, W, H);

  // This is the frame worth looking at when judging the coarser grid the neighbour rays are traced
  // on: rolling ground running to the horizon across three world boundaries.
  Uint32 *px = (Uint32 *)malloc((size_t)W * H * sizeof(Uint32));
  if (px && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA8888, px, W * 4) == 0)
    dump_frame_if_asked(px, W, H, "cluster-hills");
  free(px);

  for (int i = 0; i < attached; i++)
  {
    for (uint32_t z = 4; z < 48; z++)
      world_set_voxel(slots[i], 32, 32, z, VOXEL_STONE);
    world_refresh_occupancy_bitfield(slots[i]);
  }
  shadow_world_refresh(cluster);

  const double spire_ms = measure_cluster_frames(ren, cluster, &cam, W, H);

  printf("       %dx%d, centre world alone: %.2f ms\n", W, H, centre_ms);
  printf("       + %d neighbours of rolling ground: %.2f ms (+%.2f ms)\n", attached, hills_ms,
         hills_ms - centre_ms);
  printf("       + %d neighbours of empty air over a spire: %.2f ms (+%.2f ms)\n", attached,
         spire_ms, spire_ms - centre_ms);

  check(hills_ms > 0.0 && hills_ms < centre_ms * 2.0, "ordinary terrain beyond the boundary is cheap");
  // A 1ms centre raster (Apple Silicon, -O2) makes a 6ms empty-sky neighbour pass look like 6x,
  // which is still a handful of milliseconds. Fail on a true blow-up: either many times the
  // centre world, or slow in absolute terms.
  check(spire_ms > 0.0 && (spire_ms < centre_ms * 5.0 || spire_ms < 15.0),
        "even the pathological case stays the same order as the centre world");

done:
  if (cluster)
    shadow_world_destroy(cluster);
  for (int i = 0; i < 9; i++)
    if (slots[i])
      world_destroy(slots[i]);
  SDL_SetRenderTarget(ren, NULL);
  if (target)
    SDL_DestroyTexture(target);
  fp_renderer_release_renderer();
  fp_renderer_invalidate_cache();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
}

// Door panel sits in the X mid-plane. Looking along +Y hits the thin edge (mostly transparent
// bake). A red wall north of the door must show through those gaps under mesh mode; a solid stone
// cube in the same cell must not.
static World *build_sparse_fitting_world(VoxelType blocker)
{
  World *w = world_create(32, 32, 32);
  if (!w)
    return NULL;

  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
      for (uint32_t z = 0; z < 4; z++)
        world_set_voxel(w, x, y, z, z == 3 ? VOXEL_GRASS : VOXEL_SOIL);

  // Wide occluder so the back wall cannot peek around the sides under FOV.
  for (int dx = -1; dx <= 1; dx++)
    for (uint32_t z = 4; z < 8; z++)
      world_set_voxel(w, (uint32_t)(16 + dx), 16, z, blocker);

  // Narrow wall only directly behind the blocker so a solid cube fully occludes it while a
  // sparse door/roof still reveals red through bake gaps.
  for (uint32_t z = 4; z < 8; z++)
    world_set_voxel(w, 16, 22, z, VOXEL_WOOL_RED);

  world_refresh_occupancy_bitfield(w);
  return w;
}

static int count_reddish_pixels(const Uint32 *px, int n)
{
  int red = 0;
  for (int i = 0; i < n; i++)
  {
    const int r = (int)((px[i] >> 24) & 0xFF);
    const int g = (int)((px[i] >> 16) & 0xFF);
    const int b = (int)((px[i] >> 8) & 0xFF);
    if (r > g + 25 && r > b + 25 && r > 80)
      red++;
  }
  return red;
}

static void test_sparse_mesh_punch_through(void)
{
  printf("[sparse-fittings]\n");

  SDL_Window *win = SDL_CreateWindow("fp-sparse", 0, 0, 320, 240, SDL_WINDOW_HIDDEN);
  SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
  if (!ren)
  {
    printf("  skip  no SDL renderer available (%s)\n", SDL_GetError());
    if (win)
      SDL_DestroyWindow(win);
    return;
  }

  const int W = 320, H = 240;
  SDL_Texture *target =
      SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, H);
  World *door_w = build_sparse_fitting_world(VOXEL_DOOR);
  World *stone_w = build_sparse_fitting_world(VOXEL_STONE);
  World *roof_w = build_sparse_fitting_world(VOXEL_ROOF_TILE);
  Uint32 *door_px = (Uint32 *)malloc((size_t)W * H * sizeof(Uint32));
  Uint32 *stone_px = (Uint32 *)malloc((size_t)W * H * sizeof(Uint32));
  Uint32 *roof_px = (Uint32 *)malloc((size_t)W * H * sizeof(Uint32));

  if (!target || !door_w || !stone_w || !roof_w || !door_px || !stone_px || !roof_px)
  {
    check(false, "sparse fitting fixture built");
    goto done;
  }

  // South of the door, looking +Y so the thin edge faces the camera. Narrow FOV so the
  // occluder fills the sightline to the back wall.
  FPCamera cam = {.x = 16.5f, .y = 10.0f, .z = 5.5f, .yaw = 1.5707963f, .pitch = 0.0f,
                  .fov_deg = 35.0f};
  GameWorlds gw = {0};

  SDL_SetRenderTarget(ren, target);
  fp_renderer_set_mode(FP_MODE_MESH);
  fp_renderer_enable_gpu_mesh(false);

  gw.home_world = door_w;
  fp_renderer_render_neighbors(ren, &gw, &cam, 0, 0, W, H, 0, NULL);
  check(SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA8888, door_px, W * 4) == 0,
        "door frame read back");
  dump_frame_if_asked(door_px, W, H, "sparse-door");

  gw.home_world = stone_w;
  fp_renderer_invalidate_cache();
  fp_renderer_render_neighbors(ren, &gw, &cam, 0, 0, W, H, 0, NULL);
  check(SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA8888, stone_px, W * 4) == 0,
        "stone frame read back");

  gw.home_world = roof_w;
  fp_renderer_invalidate_cache();
  fp_renderer_render_neighbors(ren, &gw, &cam, 0, 0, W, H, 0, NULL);
  check(SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA8888, roof_px, W * 4) == 0,
        "roof frame read back");
  dump_frame_if_asked(roof_px, W, H, "sparse-roof");

  const int total = W * H;
  const int door_red = count_reddish_pixels(door_px, total);
  const int stone_red = count_reddish_pixels(stone_px, total);
  const int roof_red = count_reddish_pixels(roof_px, total);
  printf("       reddish pixels: door %d, stone %d, roof %d (of %d)\n", door_red, stone_red,
         roof_red, total);

  // Nested silhouettes (not punched AABB cubes): a closed door panel viewed face-on is opaque
  // wood, so it must occlude the back wall like stone. The roof remains a thin shell with air
  // above/below the pitch, so it still reveals more of the wall than a solid cube.
  check(door_red < total / 200,
        "closed door nested mesh occludes the back wall (not a transparent punched cube)");
  check(stone_red < total / 200, "solid stone cube occludes the back wall");
  check(roof_red > stone_red * 2 + 50,
        "roof shell gaps also reveal the wall behind more than stone");
  check(door_red <= stone_red + total / 400,
        "door face-on occlusion is in the same ballpark as stone");

done:
  free(door_px);
  free(stone_px);
  free(roof_px);
  if (door_w)
    world_destroy(door_w);
  if (stone_w)
    world_destroy(stone_w);
  if (roof_w)
    world_destroy(roof_w);
  SDL_SetRenderTarget(ren, NULL);
  if (target)
    SDL_DestroyTexture(target);
  fp_renderer_release_renderer();
  fp_renderer_invalidate_cache();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
}

int main(void)
{
  // Detail is on by default; set it so the frame assertions hold in a bare environment. Not an
  // overwrite, because forcing it would silently ignore VERSE_SUBVOXEL=0 and make the flat render
  // impossible to reproduce for comparison — the two paths have to stay separable from outside.
  setenv("VERSE_SUBVOXEL", "1", 0);
  // Freeze vegetation tip sway so before/after neighbour frames compare bit-identically on centre
  // pixels (the cluster invariant). Live play leaves wind on.
  setenv("VERSE_FP_STILL_FOLIAGE", "1", 0);
  // Lock internal resolution — dyn-res changing between the two cluster frames would reshuffle
  // every pixel and falsely fail the overwrite check.
  setenv("VERSE_FP_FIXED_RES", "1", 0);

  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  if (SDL_Init(SDL_INIT_VIDEO) != 0)
  {
    printf("SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  World *world = build_test_world();
  if (!world)
  {
    printf("failed to build test world\n");
    return 1;
  }

  printf("fp renderer audit\n");
  printf("[geometry]\n");
  test_quad_planarity(world);
  printf("[camera]\n");
  test_projection_matches_ray();
  test_rendered_frame(world, FP_MODE_RAY, "ray");
  test_rendered_frame(world, FP_MODE_MESH, "mesh");
  test_cluster_neighbours();
  test_cluster_cost();
  test_sparse_mesh_punch_through();

  world_destroy(world);
  SDL_Quit();

  printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures,
         g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
