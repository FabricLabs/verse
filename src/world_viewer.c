// world_viewer.c
// View a MagicaVoxel .vox file layer-by-layer using the same UI as layer_viewer

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "world.h"
#include "model_transformer.h"
#include "constants.h"
#include "universe.h"

#define WINDOW_W 896
#define WINDOW_H 896

// Hover/palette state needed by early functions
static int g_hovered_voxel_type = -1; // palette hover tracking
static SDL_Rect g_palette_item_rects[VOXEL_COUNT];
static int g_palette_layout_valid = 0;
static int g_yaw_index = 0;                         // 0..3, 90° steps about Z
static int g_pitch_index = 0;                       // 0..3, 90° steps about X
static bool g_iso_mode = true;                      // track current view mode for overlay counts
static SDL_Rect g_chart_item_rects[VOXEL_COUNT];    // bar chart hit regions per type
static WorldFace g_current_face = WORLD_FACE_POS_Z; // export target
// Context menu and transform state
static bool g_context_menu_open = false;
static SDL_Rect g_context_menu_rect = {0, 0, 0, 0};
static int g_context_menu_hover_index = -1; // for hover highlight within context menu
static int g_context_menu_source_type = -1; // type under cursor when menu opened
static bool g_swap_mode_active = false;     // awaiting destination swatch selection
static int g_swap_source_type = -1;         // source type to swap from
static bool g_swap_single_target = false;   // when true, apply swap as a single-voxel change
static uint32_t g_swap_target_x = 0;
static uint32_t g_swap_target_y = 0;
static uint32_t g_swap_target_z = 0;
// Runtime clock (viewer-local)
static bool g_clock_running = false;
static uint64_t g_clock_ms = 0;        // accumulated runtime in ms
static Uint32 g_clock_last_change = 0; // for color fade when whole seconds change
static int g_clock_last_whole = -1;
static uint32_t g_clock_epoch_index = 0; // increments every 10 minutes
// Draw submenu and radius input state
static bool g_draw_submenu_open = false;
static SDL_Rect g_draw_submenu_rect = {0, 0, 0, 0};
static bool g_radius_input_active = false;
static char g_radius_input_buf[16] = {0};
static Uint32 g_cursor_blink_start = 0;
// Redraw control
static bool g_voxel_dirty = true;
static int g_view_current_z = 0;   // currently displayed Z layer (for picking)
static Uint64 g_frame_counter = 0; // increments on each full rerender

// Hover state for viewport voxel under mouse
static bool g_have_hovered_voxel = false;
static int g_hovered_world_x = -1;
static int g_hovered_world_y = -1;
static int g_hovered_world_z = -1;
// Whether current hover selection comes from palette/chart instead of viewport
static bool g_palette_or_chart_hover = false;

// Context menu voxel target (when right-clicking over viewport)
static bool g_context_menu_voxel_has_target = false;
static uint32_t g_context_menu_voxel_x = 0;
static uint32_t g_context_menu_voxel_y = 0;
static uint32_t g_context_menu_voxel_z = 0;

// Forward declarations for functions used by draw_frame
static void ensure_font_size_for_grid(const World *w, bool iso_mode);
static void render_slice_isometric(SDL_Renderer *r, World *w, int z);
static void render_slice(SDL_Renderer *r, World *w, int z);
static void draw_overlay(SDL_Renderer *r, const char *vox_path, const World *w, int z);
// Needed by screen picking before its definition later in file
static void compute_iso_tile_dims(const World *w, int *out_tw, int *out_th);

static void draw_frame(SDL_Renderer *ren, const char *vox_path, World *render_world, World *stats_world, int z, bool iso)
{
  ensure_font_size_for_grid(render_world, iso);
  g_view_current_z = z;
  if (g_voxel_dirty)
  {
    // Clear frame fully before rendering to avoid stale pixels on partial redraws
    SDL_SetRenderDrawColor(ren, 20, 22, 35, 255);
    SDL_RenderClear(ren);
    if (iso)
      render_slice_isometric(ren, render_world, z);
    else
      render_slice(ren, render_world, z);
    g_voxel_dirty = false;
    g_frame_counter++;
  }
  // If we have a hovered voxel, set swatch highlight to that type for overlay drawing
  if (g_have_hovered_voxel)
  {
    const Voxel *hv = world_get_voxel(stats_world, (uint32_t)g_hovered_world_x, (uint32_t)g_hovered_world_y, (uint32_t)g_hovered_world_z);
    if (hv)
      g_hovered_voxel_type = (int)hv->type;
  }
  else if (!g_palette_or_chart_hover && !g_swap_mode_active)
  {
    // Clear type highlight when nothing hovered in viewport or palette/chart
    g_hovered_voxel_type = -1;
  }
  draw_overlay(ren, vox_path, stats_world, z);
  SDL_RenderPresent(ren);
}

static inline bool map_rotated_indices(const World *w,
                                       uint32_t in_x, uint32_t in_y, uint32_t in_z,
                                       uint32_t *out_x, uint32_t *out_y, uint32_t *out_z)
{
  // Apply yaw (around Z) in 90° increments
  int ax, ay, az;
  switch (g_yaw_index & 3)
  {
  case 0:
    ax = (int)in_x;
    ay = (int)in_y;
    az = (int)in_z;
    break;
  case 1:
    ax = (int)in_y;
    ay = (int)w->width - 1 - (int)in_x;
    az = (int)in_z;
    break; // +90°
  case 2:
    ax = (int)w->width - 1 - (int)in_x;
    ay = (int)w->height - 1 - (int)in_y;
    az = (int)in_z;
    break; // 180°
  case 3:
    ax = (int)w->height - 1 - (int)in_y;
    ay = (int)in_x;
    az = (int)in_z;
    break; // -90°
  }

  // Apply pitch (around X) in 90° increments, using current ax,ay,az
  int bx = ax, by = ay, bz;
  switch (g_pitch_index & 3)
  {
  case 0:
    bz = az;
    break; // unchanged
  case 1:
    by = az;
    bz = (int)w->height - 1 - ay;
    break; // +90° forward: (y,z) -> (z, H-1-y)
  case 2:
    bz = (int)w->depth - 1 - az;
    by = (int)w->height - 1 - ay;
    break; // 180°
  case 3:
    by = (int)w->depth - 1 - az;
    bz = ay;
    break; // -90°: (y,z) -> (D-1-z, y)
  }

  if (bx < 0 || by < 0 || bz < 0 ||
      bx >= (int)w->width || by >= (int)w->height || bz >= (int)w->depth)
    return false;
  *out_x = (uint32_t)bx;
  *out_y = (uint32_t)by;
  *out_z = (uint32_t)bz;
  return true;
}

// Global UI state
static TTF_Font *g_font = NULL;
static int g_font_px = 0;
static int g_font_force_line_px = -1;     // <=0 auto-size, >0 exact line height target
static const int g_font_virtual_rows = 5; // Visitor virtual pixel rows per glyph
static int g_font_scale = 2;              // Screen px per virtual row (matches 2px separators)

static SDL_Color color_for_voxel(VoxelType t)
{
  uint8_t r = 25, g = 25, b = 50;
  world_voxel_type_color(t, &r, &g, &b);
  return (SDL_Color){r, g, b, 255};
}

static inline bool point_in_diamond(int px, int py, int cx, int cy, int half_w, int half_h)
{
  int dx = abs(px - cx);
  int dy = abs(py - cy);
  if (half_w <= 0 || half_h <= 0)
    return false;
  long lhs = (long)dy * (long)half_w + (long)dx * (long)half_h;
  long rhs = (long)half_w * (long)half_h;
  return lhs <= rhs;
}

static inline bool point_in_triangle(int px, int py, SDL_Point a, SDL_Point b, SDL_Point c)
{
  // Barycentric technique
  int v0x = c.x - a.x, v0y = c.y - a.y;
  int v1x = b.x - a.x, v1y = b.y - a.y;
  int v2x = px - a.x, v2y = py - a.y;
  int dot00 = v0x * v0x + v0y * v0y;
  int dot01 = v0x * v1x + v0y * v1y;
  int dot02 = v0x * v2x + v0y * v2y;
  int dot11 = v1x * v1x + v1y * v1y;
  int dot12 = v1x * v2x + v1y * v2y;
  // Compute barycentric coordinates in integer math; fall back to float if degenerate
  int denom = dot00 * dot11 - dot01 * dot01;
  if (denom == 0)
    return false;
  // Use floating for robustness
  float invDenom = 1.0f / (float)denom;
  float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
  float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
  return (u >= 0.0f) && (v >= 0.0f) && (u + v <= 1.0f);
}

// Map screen coordinates to world voxel under cursor. Returns true and writes indices on success.
static bool pick_voxel_from_screen(const World *w, int mx, int my, bool iso, int view_z,
                                   uint32_t *out_x, uint32_t *out_y, uint32_t *out_z)
{
  if (!w || !w->voxels)
    return false;
  if (!iso)
  {
    int cell_w = WINDOW_W / (int)w->width;
    int cell_h = WINDOW_H / (int)w->height;
    if (cell_w <= 0 || cell_h <= 0)
      return false;
    int gx = mx / cell_w;
    int gy = my / cell_h;
    if (gx < 0 || gy < 0 || gx >= (int)w->width || gy >= (int)w->height)
      return false;
    uint32_t vx = 0, vy = 0, vz = 0;
    if (!map_rotated_indices(w, (uint32_t)gx, (uint32_t)gy, (uint32_t)view_z, &vx, &vy, &vz))
      return false;
    const Voxel *v = world_get_voxel((World *)w, vx, vy, vz);
    if (!v || v->type == VOXEL_AIR)
      return false;
    if (out_x)
      *out_x = vx;
    if (out_y)
      *out_y = vy;
    if (out_z)
      *out_z = vz;
    return true;
  }
  else
  {
    int tw, th;
    compute_iso_tile_dims(w, &tw, &th);
    int half_tw = tw / 2;
    int half_th = th / 2;
    int vh = th;
    int cx0 = WINDOW_W / 2;
    int cy0 = WINDOW_H / 2;

    // Match centering used in render_slice_isometric: center occupied bbox up to view_z
    // Use the same exact loop structure and math; keep variables local to avoid reuse
    {
      int minx = (int)w->width, miny = (int)w->height, maxx = -1, maxy = -1;
      for (uint32_t y2 = 0; y2 < w->height; y2++)
      {
        for (uint32_t x2 = 0; x2 < w->width; x2++)
        {
          for (int zl = 0; zl <= view_z && zl < (int)w->depth; zl++)
          {
            Voxel *vv = world_get_voxel((World *)w, x2, y2, (uint32_t)zl);
            if (vv && vv->type != VOXEL_AIR)
            {
              if ((int)x2 < minx)
                minx = (int)x2;
              if ((int)y2 < miny)
                miny = (int)y2;
              if ((int)x2 > maxx)
                maxx = (int)x2;
              if ((int)y2 > maxy)
                maxy = (int)y2;
              break;
            }
          }
        }
      }
      if (maxx >= minx && maxy >= miny)
      {
        float cmx = (minx + maxx) * 0.5f;
        float cmy = (miny + maxy) * 0.5f;
        int center_sx = cx0 + ((int)cmx - (int)cmy) * half_tw;
        int center_sy = cy0 + ((int)cmx + (int)cmy) * half_th + (view_z * vh) / 2;
        cx0 -= (center_sx - WINDOW_W / 2);
        cy0 -= (center_sy - WINDOW_H / 2);
      }
    }
    // Iterate in the same order as rendering (y, then x). Within a column, scan z ascending
    // and keep the last hit so the pick respects draw order (front-most wins).
    bool any_hit = false;
    uint32_t pick_x = 0, pick_y = 0, pick_z = 0;
    for (uint32_t y = 0; y < w->height; y++)
    {
      for (uint32_t x = 0; x < w->width; x++)
      {
        uint32_t rx_i = x, ry_i = y, rz_i = 0;
        if (!map_rotated_indices(w, x, y, 0, &rx_i, &ry_i, &rz_i))
          continue;
        int sx = cx0 + ((int)rx_i - (int)ry_i) * half_tw;
        int sy_base = cy0 + ((int)rx_i + (int)ry_i) * half_th;
        // Scan z ascending to mirror renderer's layering
        for (int z0 = 0; z0 <= view_z; z0++)
        {
          uint32_t vx_i = x, vy_i = y, vz_i = (uint32_t)z0;
          if (!map_rotated_indices(w, x, y, (uint32_t)z0, &vx_i, &vy_i, &vz_i))
            continue;
          const Voxel *v = world_get_voxel((World *)w, vx_i, vy_i, vz_i);
          if (!v || v->type == VOXEL_AIR)
            continue;
          int sy = sy_base + (view_z - (int)vz_i) * vh;
          // Hit test against top diamond (two triangles)
          SDL_Point top0 = (SDL_Point){sx, sy - half_th};
          SDL_Point top1 = (SDL_Point){sx + half_tw, sy};
          SDL_Point top2 = (SDL_Point){sx, sy + half_th};
          SDL_Point top3 = (SDL_Point){sx - half_tw, sy};
          bool hit = false;
          if (point_in_triangle(mx, my, top0, top1, top2) || point_in_triangle(mx, my, top0, top2, top3))
          {
            hit = true;
          }
          else
          {
            // Include side faces so visible blocks on higher layers are pickable across their body
            SDL_Point R0 = (SDL_Point){sx + half_tw, sy};
            SDL_Point R1 = (SDL_Point){sx + half_tw, sy + vh};
            SDL_Point R2 = (SDL_Point){sx, sy + half_th + vh};
            SDL_Point R3 = (SDL_Point){sx, sy + half_th};
            SDL_Point L0 = (SDL_Point){sx - half_tw, sy};
            SDL_Point L1 = (SDL_Point){sx, sy + half_th};
            SDL_Point L2 = (SDL_Point){sx, sy + half_th + vh};
            SDL_Point L3 = (SDL_Point){sx - half_tw, sy + vh};
            if (point_in_triangle(mx, my, R0, R1, R2) || point_in_triangle(mx, my, R0, R2, R3) ||
                point_in_triangle(mx, my, L0, L1, L2) || point_in_triangle(mx, my, L0, L2, L3))
            {
              hit = true;
            }
          }
          if (hit)
          {
            any_hit = true;
            pick_x = vx_i;
            pick_y = vy_i;
            pick_z = vz_i;
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
}

static void swap_voxel_types(World *w, VoxelType source, VoxelType dest)
{
  if (!w || !w->voxels)
    return;
  if (source == dest)
    return;
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
      {
        Voxel *v = world_get_voxel(w, x, y, z);
        if (!v)
          continue;
        if (v->type == source)
          v->type = dest;
      }
}

static const char *face_name(WorldFace f)
{
  switch (f)
  {
  case WORLD_FACE_POS_X:
    return "+X";
  case WORLD_FACE_NEG_X:
    return "-X";
  case WORLD_FACE_POS_Y:
    return "+Y";
  case WORLD_FACE_NEG_Y:
    return "-Y";
  case WORLD_FACE_POS_Z:
    return "+Z";
  case WORLD_FACE_NEG_Z:
    return "-Z";
  default:
    return "?";
  }
}

static bool write_ppm_rgb24(const char *path, const uint8_t *data, uint32_t w, uint32_t h, uint32_t stride)
{
  if (!path || !data || w == 0 || h == 0)
    return false;
  FILE *fp = fopen(path, "wb");
  if (!fp)
    return false;
  // P6 PPM header
  fprintf(fp, "P6\n%u %u\n255\n", (unsigned)w, (unsigned)h);
  for (uint32_t y = 0; y < h; y++)
  {
    const uint8_t *row = data + (size_t)y * stride;
    fwrite(row, 1, (size_t)w * 3, fp);
  }
  fclose(fp);
  return true;
}

static void write_le16(FILE *fp, uint16_t v)
{
  fputc((int)(v & 0xFF), fp);
  fputc((int)((v >> 8) & 0xFF), fp);
}
static void write_le32(FILE *fp, uint32_t v)
{
  fputc((int)(v & 0xFF), fp);
  fputc((int)((v >> 8) & 0xFF), fp);
  fputc((int)((v >> 16) & 0xFF), fp);
  fputc((int)((v >> 24) & 0xFF), fp);
}

static bool write_bmp_rgb24(const char *path, const uint8_t *data, uint32_t w, uint32_t h, uint32_t stride)
{
  if (!path || !data || w == 0 || h == 0)
    return false;
  FILE *fp = fopen(path, "wb");
  if (!fp)
    return false;
  uint32_t rowSize = ((w * 3u + 3u) / 4u) * 4u;
  uint32_t imgSize = rowSize * h;
  uint32_t fileSize = 14u + 40u + imgSize;
  // BITMAPFILEHEADER
  fputc('B', fp);
  fputc('M', fp);
  write_le32(fp, fileSize);
  write_le16(fp, 0);
  write_le16(fp, 0);
  write_le32(fp, 14u + 40u);
  // BITMAPINFOHEADER
  write_le32(fp, 40u);     // biSize
  write_le32(fp, w);       // biWidth
  write_le32(fp, h);       // biHeight (bottom-up)
  write_le16(fp, 1u);      // biPlanes
  write_le16(fp, 24u);     // biBitCount
  write_le32(fp, 0u);      // biCompression (BI_RGB)
  write_le32(fp, imgSize); // biSizeImage
  write_le32(fp, 2835u);   // biXPelsPerMeter (~72 DPI)
  write_le32(fp, 2835u);   // biYPelsPerMeter
  write_le32(fp, 0u);      // biClrUsed
  write_le32(fp, 0u);      // biClrImportant
  // Pixel data bottom-up, BGR with padding
  uint8_t pad[3] = {0, 0, 0};
  for (int yy = (int)h - 1; yy >= 0; yy--)
  {
    const uint8_t *src = data + (size_t)yy * stride;
    for (uint32_t x = 0; x < w; x++)
    {
      const uint8_t *p = src + x * 3u;
      fputc((int)p[2 - 2], fp); // B
      fputc((int)p[1], fp);     // G
      fputc((int)p[0], fp);     // R
    }
    uint32_t written = w * 3u;
    uint32_t needPad = (rowSize - written);
    if (needPad)
      fwrite(pad, 1, needPad, fp);
  }
  fclose(fp);
  return true;
}

// Rarity ranking for palette sorting (lower rank = more common)
static int rarity_rank_for_type(VoxelType t)
{
  switch (t)
  {
  case VOXEL_AIR:
    return 0;
  case VOXEL_SOIL:
  case VOXEL_SOIL_CLAY:
  case VOXEL_SOIL_LOAM:
  case VOXEL_SOIL_SILT:
    return 1;
  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
    return 2;
  case VOXEL_STONE_BASALT:
  case VOXEL_STONE_GRANITE:
  case VOXEL_STONE_LIMESTONE:
  case VOXEL_STONE:
    return 3;
  case VOXEL_WOOD:
  case VOXEL_WOOD_OAK:
  case VOXEL_WOOD_BIRCH:
  case VOXEL_WOOD_PINE:
    return 4;
  case VOXEL_LEAVES:
  case VOXEL_SAND:
    return 5;
  case VOXEL_BUSH:
    return 6;
  case VOXEL_ORE_COPPER:
  case VOXEL_ORE_SILVER:
  case VOXEL_ORE_GOLD:
    return 7;
  case VOXEL_CRYSTAL:
  case VOXEL_CRYSTAL_BLUE:
  case VOXEL_CRYSTAL_GREEN:
  case VOXEL_CRYSTAL_RED:
    return 8;
  case VOXEL_COPPER:
  case VOXEL_SILVER:
  case VOXEL_GOLD:
    return 9;
  case VOXEL_BEDROCK:
    return 10;
  case VOXEL_SPRING:
    return 11;
  default:
    return 12;
  }
}

static void render_slice(SDL_Renderer *r, World *w, int z)
{
  SDL_SetRenderDrawColor(r, 20, 22, 35, 255);
  SDL_RenderClear(r);

  int cell_w = WINDOW_W / (int)w->width;
  int cell_h = WINDOW_H / (int)w->height;
  if (cell_w < 2)
    cell_w = 2;
  if (cell_h < 2)
    cell_h = 2;

  for (uint32_t y = 0; y < w->height; y++)
  {
    for (uint32_t x = 0; x < w->width; x++)
    {
      uint32_t vx_i = x, vy_i = y, vz_i = (uint32_t)z;
      if (!map_rotated_indices(w, x, y, (uint32_t)z, &vx_i, &vy_i, &vz_i))
        continue;
      Voxel *v = world_get_voxel(w, vx_i, vy_i, vz_i);
      SDL_Color c = v ? color_for_voxel(v->type) : (SDL_Color){10, 10, 20, 255};
      SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
      SDL_Rect rect = {(int)(x * cell_w), (int)(y * cell_h), cell_w - 1, cell_h - 1};
      SDL_RenderFillRect(r, &rect);

      // Strong highlight for exact hovered voxel in orthographic view (disabled during swap mode)
      if (!g_swap_mode_active && g_have_hovered_voxel && (int)vx_i == g_hovered_world_x && (int)vy_i == g_hovered_world_y && (int)vz_i == g_hovered_world_z)
      {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &rect);
        SDL_Rect inset = {rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2};
        if (inset.w > 0 && inset.h > 0)
          SDL_RenderDrawRect(r, &inset);
      }
      else if (!g_swap_mode_active && g_palette_or_chart_hover && v && g_hovered_voxel_type >= 0 && (int)v->type == g_hovered_voxel_type)
      {
        // While hovering a swatch or chart segment, outline all voxels of that type (legacy behavior)
        SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
        SDL_RenderDrawRect(r, &rect);
      }
    }
  }

  // Draw grid lines for easier reading
  SDL_SetRenderDrawColor(r, 0, 0, 0, 40);
  for (uint32_t gx = 0; gx <= w->width; gx++)
  {
    int sx = (int)(gx * cell_w);
    SDL_RenderDrawLine(r, sx, 0, sx, WINDOW_H);
  }
  for (uint32_t gy = 0; gy <= w->height; gy++)
  {
    int sy = (int)(gy * cell_h);
    SDL_RenderDrawLine(r, 0, sy, WINDOW_W, sy);
  }

  // present handled by caller to allow overlay
}

/* duplicate globals removed */

static const char *basename_cstr(const char *path)
{
  if (!path || !*path)
    return NULL;
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

static void draw_text(SDL_Renderer *r, const char *text, int x, int y, SDL_Color color)
{
  if (!g_font || !text)
    return;
  SDL_Surface *surface = TTF_RenderText_Solid(g_font, text, color);
  if (!surface)
    return;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surface);
  if (tex)
  {
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
  }
  SDL_FreeSurface(surface);
}

static void draw_overlay(SDL_Renderer *r, const char *vox_path, const World *w, int z)
{
  SDL_Color white = {255, 255, 255, 255};
  // Top-left: file/seed name
  const char *name = basename_cstr(vox_path);
  if (!name || !*name)
  {
    if (w && w->seed_id[0])
      name = w->seed_id;
    else
      name = "Generated";
  }
  draw_text(r, name, 8, 6, white);
  // One-line: total non-air voxel count cumulative up to and including current z
  size_t total_count = 0;
  if (w && w->voxels)
  {
    if (!g_iso_mode)
    {
      for (uint32_t z0 = 0; z0 <= (uint32_t)z && z0 < w->depth; z0++)
        for (uint32_t y = 0; y < w->height; y++)
          for (uint32_t x = 0; x < w->width; x++)
          {
            const Voxel *v = world_get_voxel((World *)w, x, y, z0);
            if (v && v->type != VOXEL_AIR)
              total_count++;
          }
    }
    else
    {
      for (uint32_t z0 = 0; z0 < w->depth; z0++)
        for (uint32_t y = 0; y < w->height; y++)
          for (uint32_t x = 0; x < w->width; x++)
          {
            uint32_t rx, ry, rz;
            if (!map_rotated_indices(w, x, y, z0, &rx, &ry, &rz))
              continue;
            if ((int)rz > z)
              continue;
            const Voxel *v = world_get_voxel((World *)w, rx, ry, rz);
            if (v && v->type != VOXEL_AIR)
              total_count++;
          }
    }
  }
  char total_buf[96];
  int name_h = g_font ? TTF_FontHeight(g_font) : 12;
  snprintf(total_buf, sizeof(total_buf), "voxels: %zu", total_count);
  draw_text(r, total_buf, 8, 6 + name_h + 2, white);

  // Clock line directly below voxels (flash yellow on whole-second change; decimal stays white)
  Uint32 now = SDL_GetTicks();
  int line_h = g_font ? TTF_FontHeight(g_font) : 12;
  double secs = (double)g_clock_ms / 1000.0;
  int whole = (int)secs;
  int tenth = (int)((secs - whole) * 10.0 + 0.0001);
  if (whole != g_clock_last_whole)
  {
    g_clock_last_whole = whole;
    g_clock_last_change = now;
  }
  // Only the digit '1' (the ones place) should flash yellow, decimal stays white
  Uint8 cr = 255, cg = 255, cb = 255;
  if (now - g_clock_last_change < 1000)
  {
    float t = (float)(now - g_clock_last_change) / 1000.0f; // 0..1
    // interpolate blue from 0->255 to fade yellow->white
    cb = (Uint8)(t * 255.0f);
  }
  // Clock text: prefix (white), whole seconds (fading color), decimal and tenths (white)
  const int cy = 6 + name_h + 2 + line_h + 2;
  const char *prefix = "Clock: ";
  int prefix_w = 0, prefix_h = 0;
  if (g_font)
    TTF_SizeText(g_font, prefix, &prefix_w, &prefix_h);
  draw_text(r, prefix, 8, cy, white);

  char whole_str[32];
  snprintf(whole_str, sizeof(whole_str), "%d", whole);
  int whole_w = 0, whole_h = 0;
  if (g_font)
    TTF_SizeText(g_font, whole_str, &whole_w, &whole_h);
  SDL_Color whole_col = (SDL_Color){cr, cg, cb, 255};
  draw_text(r, whole_str, 8 + prefix_w, cy, whole_col);

  const char *dot = ".";
  int dot_w = 0, dot_h = 0;
  if (g_font)
    TTF_SizeText(g_font, dot, &dot_w, &dot_h);
  draw_text(r, dot, 8 + prefix_w + whole_w, cy, white);

  char dec_buf[8];
  snprintf(dec_buf, sizeof(dec_buf), "%d", tenth);
  draw_text(r, dec_buf, 8 + prefix_w + whole_w + dot_w, cy, white);
  // Status line one below
  const char *status = g_clock_running ? "[running]" : "[stopped]";
  draw_text(r, status, 8, 6 + name_h + 2 + line_h + 2 + line_h + 2, white);

  // Top-right: coordinates (WxHxD z:Z)
  char buf[128];
  snprintf(buf, sizeof(buf), "%ux%ux%u  z:%d", w->width, w->height, w->depth, z);
  // measure
  int tw = 0, th = 0;
  if (g_font)
  {
    TTF_SizeText(g_font, buf, &tw, &th);
  }
  int x = WINDOW_W - tw - 8;
  if (x < 8)
    x = 8;
  draw_text(r, buf, x, 6, white);

  // Palette box under coordinates (top-right)
  // Layout: bordered rectangle, rows of swatches with voxel names and counts
  const int box_margin = 6;
  const int swatch_size = 12;
  const int swatch_gap = 4;
  const int text_gap = 6;
  const int cols = 2;
  const int types_count = (int)VOXEL_COUNT;

  int name_w = 120;
  int row_h = (swatch_size > line_h ? swatch_size : line_h) + 2;
  int rows = (types_count + cols - 1) / cols;
  int box_w = cols * (swatch_size + swatch_gap + name_w) + box_margin * 2 - swatch_gap;
  const int chart_h = 12;
  int box_h = rows * row_h + (rows - 1) * swatch_gap + box_margin * 2 + 6 + chart_h + 2; // reduce bottom padding
  int box_x = WINDOW_W - box_w - 8;
  int box_y = 6 + (th > 0 ? th : 16) + 8; // below coords line

  // Border and background
  SDL_Rect box = {box_x, box_y, box_w, box_h};
  SDL_SetRenderDrawColor(r, 0, 0, 0, 120);
  SDL_RenderFillRect(r, &box);
  SDL_SetRenderDrawColor(r, 255, 255, 255, 180);
  SDL_RenderDrawRect(r, &box);
  g_palette_layout_valid = 1;
  g_palette_layout_valid = 1;

  // Precompute counts per type cumulative up to and including current z
  size_t counts[VOXEL_COUNT];
  for (int i = 0; i < (int)VOXEL_COUNT; i++)
    counts[i] = 0;
  if (w && w->voxels)
  {
    if (!g_iso_mode)
    {
      for (uint32_t z0 = 0; z0 <= (uint32_t)z && z0 < w->depth; z0++)
        for (uint32_t y = 0; y < w->height; y++)
          for (uint32_t x = 0; x < w->width; x++)
          {
            const Voxel *v = world_get_voxel((World *)w, x, y, z0);
            if (v)
              counts[v->type]++;
          }
    }
    else
    {
      for (uint32_t z0 = 0; z0 < w->depth; z0++)
        for (uint32_t y = 0; y < w->height; y++)
          for (uint32_t x = 0; x < w->width; x++)
          {
            uint32_t rx, ry, rz;
            if (!map_rotated_indices(w, x, y, z0, &rx, &ry, &rz))
              continue;
            if ((int)rz > z)
              continue;
            const Voxel *v = world_get_voxel((World *)w, rx, ry, rz);
            if (v)
              counts[v->type]++;
          }
    }
  }

  // Rarity sort: build sorted list
  int sorted[VOXEL_COUNT];
  for (int i = 0; i < types_count; i++)
    sorted[i] = i;
  for (int i = 0; i < types_count - 1; i++)
  {
    int best = i;
    for (int j = i + 1; j < types_count; j++)
    {
      int rj = rarity_rank_for_type((VoxelType)sorted[j]);
      int rb = rarity_rank_for_type((VoxelType)sorted[best]);
      if (rj < rb || (rj == rb && strcmp(world_voxel_type_name((VoxelType)sorted[j]), world_voxel_type_name((VoxelType)sorted[best])) < 0))
        best = j;
    }
    int tmp = sorted[i];
    sorted[i] = sorted[best];
    sorted[best] = tmp;
  }

  // Draw swatches with names
  int cursor_x = box_x + box_margin;
  int cursor_y = box_y + box_margin;
  for (int si = 0; si < types_count; si++)
  {
    int i = sorted[si];
    uint8_t rr = 128, gg = 128, bb = 128;
    world_voxel_type_color((VoxelType)i, &rr, &gg, &bb);
    SDL_SetRenderDrawColor(r, rr, gg, bb, 255);
    SDL_Rect sw = {cursor_x, cursor_y, swatch_size, swatch_size};
    SDL_RenderFillRect(r, &sw);
    if (g_hovered_voxel_type >= 0 && i == g_hovered_voxel_type)
      SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    else
      SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_RenderDrawRect(r, &sw);

    // Name text (voxel type name) and count on same line (aligned)
    const char *label = world_voxel_type_name((VoxelType)i);
    char textbuf[128];
    snprintf(textbuf, sizeof(textbuf), "%s (%zu)", label, counts[i]);
    draw_text(r, textbuf, cursor_x + swatch_size + text_gap, cursor_y - 2, white);

    // Store hit rect
    g_palette_item_rects[i] = (SDL_Rect){cursor_x, cursor_y, swatch_size + text_gap + name_w, row_h};

    // Advance
    if (((si + 1) % cols) == 0)
    {
      cursor_x = box_x + box_margin;
      cursor_y += row_h + swatch_gap;
    }
    else
    {
      cursor_x += (swatch_size + swatch_gap + name_w);
    }
  }

  // Draw distribution bar chart at current z
  int chart_y = box_y + box_margin + rows * row_h + (rows - 1) * swatch_gap + 6;
  int chart_x = box_x + box_margin;
  int chart_w = box_w - box_margin * 2;
  SDL_Rect chart_bg = {chart_x, chart_y, chart_w, chart_h};
  SDL_SetRenderDrawColor(r, 20, 20, 30, 220);
  SDL_RenderFillRect(r, &chart_bg);
  SDL_SetRenderDrawColor(r, 255, 255, 255, 180);
  SDL_RenderDrawRect(r, &chart_bg);
  size_t total_non_air = 0;
  for (int ti = 0; ti < (int)VOXEL_COUNT; ti++)
    if (ti != VOXEL_AIR)
      total_non_air += counts[ti];
  if (total_non_air > 0)
  {
    // Follow palette rarity order for segments
    int seg_sorted[VOXEL_COUNT];
    for (int i = 0; i < types_count; i++)
      seg_sorted[i] = i;
    for (int i = 0; i < types_count - 1; i++)
    {
      int best = i;
      for (int j = i + 1; j < types_count; j++)
      {
        int rj = rarity_rank_for_type((VoxelType)seg_sorted[j]);
        int rb = rarity_rank_for_type((VoxelType)seg_sorted[best]);
        if (rj < rb)
          best = j;
      }
      int tmp = seg_sorted[i];
      seg_sorted[i] = seg_sorted[best];
      seg_sorted[best] = tmp;
    }
    int run_x = chart_x + 1;
    for (int idx = 0; idx < types_count; idx++)
    {
      int t = seg_sorted[idx];
      if (t == VOXEL_AIR)
        continue;
      size_t c = counts[t];
      if (c == 0)
        continue;
      int seg_w = (int)((double)c / (double)total_non_air * (double)(chart_w - 2));
      if (seg_w <= 0)
        continue;
      uint8_t rr = 0, gg = 0, bb = 0;
      world_voxel_type_color((VoxelType)t, &rr, &gg, &bb);
      if (g_hovered_voxel_type >= 0 && t == g_hovered_voxel_type)
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
      else
        SDL_SetRenderDrawColor(r, rr, gg, bb, 255);
      SDL_Rect seg = {run_x, chart_y + 1, seg_w, chart_h - 2};
      SDL_RenderFillRect(r, &seg);
      // Store hit rect for hover detection
      g_chart_item_rects[t] = seg;
      run_x += seg_w;
    }
  }

  // Swap prompt label
  if (g_swap_mode_active && g_swap_source_type >= 0)
  {
    const char *src_name = world_voxel_type_name((VoxelType)g_swap_source_type);
    char prompt[160];
    snprintf(prompt, sizeof(prompt), "Select the destination voxel type. (from %s)", src_name ? src_name : "?");
    // Draw outside the bordered palette box, a few pixels below
    int prompt_y = box_y + box_h + 8;
    draw_text(r, prompt, box_x, prompt_y, white);
  }

  // Radius input prompt (outside palette box) when active
  if (g_radius_input_active)
  {
    // Compose text: "Radius: <buf><cursor>"
    char label[64];
    snprintf(label, sizeof(label), "Radius: %s", g_radius_input_buf);
    int py = box_y + box_h + 8 + (g_swap_mode_active ? (line_h + 6) : 0);
    draw_text(r, label, box_x, py, white);
    // Blinking cursor after text
    if (g_font)
    {
      int w_lbl = 0, h_lbl = 0;
      TTF_SizeText(g_font, label, &w_lbl, &h_lbl);
      Uint32 t = SDL_GetTicks();
      bool show = ((t / 500) % 2) == 0; // 2 Hz blink
      if (show)
      {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDrawLine(r, box_x + w_lbl + 1, py, box_x + w_lbl + 1, py + h_lbl);
      }
    }
  }

  // Context menu overlay (items: Start/Stop clock, Draw..., Swap.../Change..., Delete, Autocrop)
  if (g_context_menu_open)
  {
    const bool can_swap = (g_context_menu_source_type >= 0);
    const bool has_voxel_tgt = g_context_menu_voxel_has_target;
    const char *item_clock = g_clock_running ? "Stop!" : "Start...";
    const char *item_draw = "Draw...";
    const char *item_swap = has_voxel_tgt ? "Change..." : "Swap...";
    const char *item_delete = "Delete";
    const char *item_crop = "Autocrop";
    int mw_clock = 0, mw_draw = 0, mw_swap = 0, mh_tmp = 0;
    int mw_crop = 0;
    int pad = 6;
    int item_h = line_h + 4;
    if (g_font)
    {
      TTF_SizeText(g_font, item_clock, &mw_clock, &mh_tmp);
      TTF_SizeText(g_font, item_draw, &mw_draw, &mh_tmp);
      TTF_SizeText(g_font, item_swap, &mw_swap, &mh_tmp);
      TTF_SizeText(g_font, item_crop, &mw_crop, &mh_tmp);
    }
    int maxw = mw_clock;
    if (mw_draw > maxw)
      maxw = mw_draw;
    if (can_swap)
    {
      if (mw_swap > maxw)
        maxw = mw_swap;
    }
    if (mw_crop > maxw)
      maxw = mw_crop;
    int menu_w = (maxw + pad * 2 + 8);
    int items = 1 /*clock*/ + 1 /*draw*/ + (can_swap ? 1 : 0) + (has_voxel_tgt ? 1 : 0) + 1 /*crop*/;
    int menu_h = items * item_h + pad * 2 + (items - 1) * 2;
    // Clamp menu within window bounds
    if (g_context_menu_rect.x + menu_w > WINDOW_W)
      g_context_menu_rect.x = WINDOW_W - menu_w - 4;
    if (g_context_menu_rect.y + menu_h > WINDOW_H)
      g_context_menu_rect.y = WINDOW_H - menu_h - 4;
    g_context_menu_rect.w = menu_w;
    g_context_menu_rect.h = menu_h;
    // Background
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_RenderFillRect(r, &g_context_menu_rect);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
    SDL_RenderDrawRect(r, &g_context_menu_rect);
    // Item rects
    int iy = g_context_menu_rect.y + pad;
    int ix = g_context_menu_rect.x + pad;
    int iw = menu_w - pad * 2;
    int item_index = 0;
    // 0: Clock toggle
    SDL_Rect it_clock = {ix, iy, iw, item_h};
    if (g_context_menu_hover_index == item_index)
    {
      SDL_SetRenderDrawColor(r, 40, 40, 60, 220);
      SDL_RenderFillRect(r, &it_clock);
    }
    draw_text(r, item_clock, it_clock.x + 2, it_clock.y + 2, white);
    iy += item_h + 2;
    item_index++;
    // 1: Draw...
    SDL_Rect it_draw = {ix, iy, iw, item_h};
    if (g_context_menu_hover_index == item_index)
    {
      SDL_SetRenderDrawColor(r, 40, 40, 60, 220);
      SDL_RenderFillRect(r, &it_draw);
    }
    draw_text(r, item_draw, it_draw.x + 2, it_draw.y + 2, white);
    iy += item_h + 2;
    item_index++;
    // 2: Swap... (optional)
    if (can_swap)
    {
      SDL_Rect it_swap = {ix, iy, iw, item_h};
      if (g_context_menu_hover_index == item_index)
      {
        SDL_SetRenderDrawColor(r, 40, 40, 60, 220);
        SDL_RenderFillRect(r, &it_swap);
      }
      draw_text(r, item_swap, it_swap.x + 2, it_swap.y + 2, white);
      iy += item_h + 2;
      item_index++;
    }
    // 3: Delete (only when voxel target exists)
    if (has_voxel_tgt)
    {
      SDL_Rect it_del = {ix, iy, iw, item_h};
      if (g_context_menu_hover_index == item_index)
      {
        SDL_SetRenderDrawColor(r, 40, 40, 60, 220);
        SDL_RenderFillRect(r, &it_del);
      }
      draw_text(r, item_delete, it_del.x + 2, it_del.y + 2, white);
      iy += item_h + 2;
      item_index++;
    }
    // last: Autocrop
    SDL_Rect it_crop = {ix, iy, iw, item_h};
    if (g_context_menu_hover_index == item_index)
    {
      SDL_SetRenderDrawColor(r, 40, 40, 60, 220);
      SDL_RenderFillRect(r, &it_crop);
    }
    draw_text(r, item_crop, it_crop.x + 2, it_crop.y + 2, white);
  }
}

static void compute_iso_tile_dims(const World *w, int *out_tw, int *out_th)
{
  const int margin = 32;
  int sum = (int)w->width + (int)w->height;
  if (sum < 2)
    sum = 2;
  int tw_w = (2 * (WINDOW_W - margin)) / sum;
  int tw_h = (4 * (WINDOW_H - margin)) / sum; // since th = tw/2
  int tw = tw_w < tw_h ? tw_w : tw_h;
  if (tw < 8)
    tw = 8;
  // Snap to a multiple of 4 to keep half/quarter pixels aligned and avoid drift
  tw -= (tw % 4);
  if (tw < 8)
    tw = 8;
  int th = tw / 2; // exact half after snapping ensures integer alignment
  if (th < 4)
    th = 4;
  *out_tw = tw;
  *out_th = th;
}

static void fill_triangle_impl(SDL_Renderer *rr, SDL_Point p1, SDL_Point p2, SDL_Point p3, SDL_Color color)
{
  if (p1.y > p2.y)
  {
    SDL_Point t = p1;
    p1 = p2;
    p2 = t;
  }
  if (p2.y > p3.y)
  {
    SDL_Point t = p2;
    p2 = p3;
    p3 = t;
  }
  if (p1.y > p2.y)
  {
    SDL_Point t = p1;
    p1 = p2;
    p2 = t;
  }
  if (p1.y == p3.y)
    return;
  SDL_SetRenderDrawColor(rr, color.r, color.g, color.b, color.a);
  for (int y = p1.y; y <= p3.y; y++)
  {
    float x_start, x_end;
    if (y <= p2.y)
    {
      float t1 = (p2.y == p1.y) ? 0.0f : (float)(y - p1.y) / (float)(p2.y - p1.y);
      float t2 = (p3.y == p1.y) ? 0.0f : (float)(y - p1.y) / (float)(p3.y - p1.y);
      x_start = p1.x + t1 * (p2.x - p1.x);
      x_end = p1.x + t2 * (p3.x - p1.x);
    }
    else
    {
      float t1 = (p3.y == p2.y) ? 0.0f : (float)(y - p2.y) / (float)(p3.y - p2.y);
      float t2 = (p3.y == p1.y) ? 0.0f : (float)(y - p1.y) / (float)(p3.y - p1.y);
      x_start = p2.x + t1 * (p3.x - p2.x);
      x_end = p1.x + t2 * (p3.x - p1.x);
    }
    if (x_start > x_end)
    {
      float tmp = x_start;
      x_start = x_end;
      x_end = tmp;
    }
    int xs = (int)(x_start + 0.5f);
    int xe = (int)(x_end + 0.5f);
    SDL_RenderDrawLine(rr, xs, y, xe, y);
  }
}

static void render_slice_isometric(SDL_Renderer *r, World *w, int z)
{
  SDL_SetRenderDrawColor(r, 20, 22, 35, 255);
  SDL_RenderClear(r);

  int tw, th;
  compute_iso_tile_dims(w, &tw, &th);
  int half_tw = tw / 2;
  int half_th = th / 2;
  int vh = th; // voxel vertical height in pixels
  int cx0 = WINDOW_W / 2;
  int cy0 = WINDOW_H / 2;

  // Center model on screen by estimating its projected center at current z
  // Compute bounding box of occupied voxels up to z and center it
  int minx = (int)w->width, miny = (int)w->height, maxx = -1, maxy = -1;
  for (uint32_t y = 0; y < w->height; y++)
  {
    for (uint32_t x = 0; x < w->width; x++)
    {
      for (int zl = 0; zl <= z && zl < (int)w->depth; zl++)
      {
        Voxel *v = world_get_voxel(w, x, y, (uint32_t)zl);
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
    }
  }
  if (maxx >= minx && maxy >= miny)
  {
    float mx = (minx + maxx) * 0.5f;
    float my = (miny + maxy) * 0.5f;
    // Shift center so that (mx,my) projects to window center
    int center_sx = cx0 + ((int)mx - (int)my) * half_tw;
    int center_sy = cy0 + ((int)mx + (int)my) * half_th + (z * vh) / 2;
    cx0 -= (center_sx - WINDOW_W / 2);
    cy0 -= (center_sy - WINDOW_H / 2);
  }

  // use fill_triangle_impl()

  // Precompute rotation center in grid space
  (void)w; // suppress unused parameter warnings for precomputed centers

  for (uint32_t y = 0; y < w->height; y++)
  {
    for (uint32_t x = 0; x < w->width; x++)
    {
      // Use rotated indices for stable lattice mapping
      uint32_t rx_i = x, ry_i = y, rz_i = 0;
      if (!map_rotated_indices(w, x, y, 0, &rx_i, &ry_i, &rz_i))
        continue;

      int sx = cx0 + ((int)rx_i - (int)ry_i) * half_tw;
      int sy_base = cy0 + ((int)rx_i + (int)ry_i) * half_th;

      for (uint32_t z0 = 0; z0 < w->depth; z0++)
      {
        uint32_t vx_i = x, vy_i = y, vz_i = z0;
        if (!map_rotated_indices(w, x, y, z0, &vx_i, &vy_i, &vz_i))
          continue;
        if ((int)vz_i > z)
          continue; // respect current level in transformed vertical axis
        Voxel *v = world_get_voxel(w, vx_i, vy_i, vz_i);
        if (!v || v->type == VOXEL_AIR)
          continue;

        SDL_Color c = color_for_voxel(v->type);
        int sy = sy_base + (z - (int)vz_i) * vh;

        SDL_Color right_c = {(Uint8)(c.r * 0.8f), (Uint8)(c.g * 0.8f), (Uint8)(c.b * 0.8f), 255};
        SDL_Color left_c = {(Uint8)(c.r * 0.6f), (Uint8)(c.g * 0.6f), (Uint8)(c.b * 0.6f), 255};

        SDL_Point R0 = {sx + half_tw, sy};
        SDL_Point R1 = {sx + half_tw, sy + vh};
        SDL_Point R2 = {sx, sy + half_th + vh};
        SDL_Point R3 = {sx, sy + half_th};
        fill_triangle_impl(r, R0, R1, R2, right_c);
        fill_triangle_impl(r, R0, R2, R3, right_c);

        SDL_Point L0 = {sx - half_tw, sy};
        SDL_Point L1 = {sx, sy + half_th};
        SDL_Point L2 = {sx, sy + half_th + vh};
        SDL_Point L3 = {sx - half_tw, sy + vh};
        fill_triangle_impl(r, L0, L1, L2, left_c);
        fill_triangle_impl(r, L0, L2, L3, left_c);

        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        for (int dy = -half_th; dy <= half_th; dy++)
        {
          int span = (tw * (half_th - abs(dy))) / th;
          if (span < 0)
            span = 0;
          int x1 = sx - span;
          int x2 = sx + span;
          SDL_RenderDrawLine(r, x1, sy + dy, x2, sy + dy);
        }

        if (g_palette_or_chart_hover && g_hovered_voxel_type >= 0 && g_hovered_voxel_type == (int)v->type)
          SDL_SetRenderDrawColor(r, 255, 255, 255, 220);
        else
          SDL_SetRenderDrawColor(r, 0, 0, 0, 90);
        SDL_RenderDrawLine(r, sx, sy - half_th, sx + half_tw, sy);
        SDL_RenderDrawLine(r, sx + half_tw, sy, sx, sy + half_th);
        SDL_RenderDrawLine(r, sx, sy + half_th, sx - half_tw, sy);
        SDL_RenderDrawLine(r, sx - half_tw, sy, sx, sy - half_th);
        SDL_RenderDrawLine(r, R0.x, R0.y, R1.x, R1.y);
        SDL_RenderDrawLine(r, R1.x, R1.y, R2.x, R2.y);
        SDL_RenderDrawLine(r, R2.x, R2.y, R3.x, R3.y);
        SDL_RenderDrawLine(r, R3.x, R3.y, R0.x, R0.y);
        SDL_RenderDrawLine(r, L0.x, L0.y, L3.x, L3.y);
        SDL_RenderDrawLine(r, L3.x, L3.y, L2.x, L2.y);
        SDL_RenderDrawLine(r, L2.x, L2.y, L1.x, L1.y);
        SDL_RenderDrawLine(r, L1.x, L1.y, L0.x, L0.y);

        // Strong highlight for exact hovered voxel in isometric view (disabled during swap mode)
        if (!g_swap_mode_active && g_have_hovered_voxel && (int)vx_i == g_hovered_world_x && (int)vy_i == g_hovered_world_y && (int)vz_i == g_hovered_world_z)
        {
          SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
          SDL_RenderDrawLine(r, sx, sy - half_th, sx + half_tw, sy);
          SDL_RenderDrawLine(r, sx + half_tw, sy, sx, sy + half_th);
          SDL_RenderDrawLine(r, sx, sy + half_th, sx - half_tw, sy);
          SDL_RenderDrawLine(r, sx - half_tw, sy, sx, sy - half_th);
        }
      }
    }
  }

  // present handled by caller to allow overlay
}

static int round_to_multiple(int value, int mult)
{
  if (mult <= 0)
    return value;
  int rem = value % mult;
  if (rem == 0)
    return value;
  int up = value + (mult - rem);
  int down = value - rem;
  // choose nearest; prefer up
  return (value - down) < (up - value) ? down : up;
}

static void ensure_font_size_for_grid(const World *w, bool iso_mode)
{
  (void)w;
  (void)iso_mode; // parameters currently unused; kept for future adaptive sizing
  int desired_line_px = (g_font_force_line_px > 0) ? g_font_force_line_px : 0;
  if (desired_line_px <= 0)
  {
    // compute by virtual rows * scale regardless of view
    desired_line_px = g_font_virtual_rows * g_font_scale;
    if (desired_line_px <= 0)
      desired_line_px = 12;
  }
  if (g_font && g_font_px == desired_line_px)
    return;

  if (g_font)
  {
    TTF_CloseFont(g_font);
    g_font = NULL;
  }

  int size_param = desired_line_px;
  // Try to match exact line height by measuring and adjusting once or twice
  for (int attempt = 0; attempt < 3; attempt++)
  {
    TTF_Font *f = TTF_OpenFont("assets/fonts/visitor-tt2-brk.ttf", size_param);
    if (!f)
    {
      f = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", size_param);
    }
    if (!f)
      break;
    int h = TTF_FontHeight(f);
    if (h == desired_line_px)
    {
      g_font = f;
      g_font_px = desired_line_px;
      TTF_SetFontHinting(g_font, TTF_HINTING_MONO);
      TTF_SetFontKerning(g_font, 0);
      return;
    }
    // Adjust and try again
    size_param += (desired_line_px - h);
    if (size_param < 6)
      size_param = 6;
    if (size_param > 72)
      size_param = 72;
    TTF_CloseFont(f);
  }
  // Fallback: open with desired param even if exact height cannot be matched
  g_font = TTF_OpenFont("assets/fonts/visitor-tt2-brk.ttf", size_param);
  if (!g_font)
    g_font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", size_param);
  if (g_font)
  {
    TTF_SetFontHinting(g_font, TTF_HINTING_MONO);
    TTF_SetFontKerning(g_font, 0);
  }
  g_font_px = desired_line_px;
}

static const char *default_vox_if_present(void)
{
  // Try a few common sample paths if no argument provided
  const char *candidates[] = {
      "models/Crocsoldier/Crocsoldier.vox",
      "models/lizardmonitorpackage/lizardmonitor.vox",
      "models/package 3/Snakewizard.vox",
      NULL};
  for (int i = 0; candidates[i]; i++)
  {
    FILE *f = fopen(candidates[i], "rb");
    if (f)
    {
      fclose(f);
      return candidates[i];
    }
  }
  return NULL;
}

int main(int argc, char **argv)
{
  const char *vox_path = (argc > 1) ? argv[1] : NULL;
  bool from_world_file = false;
  bool generate_new = (vox_path == NULL);
  if (vox_path)
  {
    size_t vox_len = strlen(vox_path);
    if (vox_len > 6 && strcmp(vox_path + (vox_len - 6), ".world") == 0)
      from_world_file = true;
  }

  uint32_t sx = 0, sy = 0, sz = 0;
  if (!from_world_file && !generate_new)
  {
    if (!model_transformer_probe_vox_dimensions(vox_path, &sx, &sy, &sz) || sx == 0 || sy == 0 || sz == 0)
    {
      printf("Failed to read SIZE from .vox: %s\n", vox_path);
      return 1;
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    printf("SDL init failed: %s\n", SDL_GetError());
    return 1;
  }
  if (TTF_Init() < 0)
  {
    printf("TTF init failed: %s\n", TTF_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Window *win = SDL_CreateWindow("VERSE World Viewer (.vox)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
  if (!win)
  {
    printf("Window creation failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren)
  {
    printf("Renderer creation failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  // Allow env override for line height: WORLD_VIEWER_FONT_PX
  const char *env_px = getenv("WORLD_VIEWER_FONT_PX");
  if (env_px && *env_px)
  {
    int v = atoi(env_px);
    if (v > 0)
      g_font_force_line_px = v;
  }
  // Allow env override for scale of virtual rows: WORLD_VIEWER_FONT_SCALE
  const char *env_scale = getenv("WORLD_VIEWER_FONT_SCALE");
  if (env_scale && *env_scale)
  {
    int s = atoi(env_scale);
    if (s >= 1 && s <= 10)
      g_font_scale = s;
  }

  // Load Visitor TTF (fallback to system font)
  ensure_font_size_for_grid(NULL, false);

  World *world = NULL;
  // Universe for multi-world rendering
  Universe uni;
  bool have_uni = false;
  if (from_world_file)
  {
    // Create dummy, then load and replace
    world = world_create(1, 1, 1);
  }
  else if (generate_new)
  {
    // Default cube size for generated worlds from constants
    sx = sx ? sx : WORLD_SIZE_X;
    sy = sy ? sy : WORLD_SIZE_Y;
    sz = sz ? sz : WORLD_SIZE_Z;
    world = world_create(sx, sy, sz);
  }
  else
  {
    world = world_create(sx, sy, sz);
  }
  if (!world)
  {
    printf("Failed to create world: %ux%ux%u\n", sx, sy, sz);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    if (g_font)
    {
      TTF_CloseFont(g_font);
      g_font = NULL;
    }
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  if (from_world_file)
  {
    if (!world_load(world, vox_path))
    {
      printf("Failed to load world file: %s\n", vox_path);
      world_destroy(world);
      SDL_DestroyRenderer(ren);
      SDL_DestroyWindow(win);
      if (g_font)
      {
        TTF_CloseFont(g_font);
        g_font = NULL;
      }
      TTF_Quit();
      SDL_Quit();
      return 1;
    }
    sx = world->width;
    sy = world->height;
    sz = world->depth;
    if (world->seed_id[0])
    {
      printf("World seed_id: %s\n", world->seed_id);
    }

    // Initialize universe and place origin + adjacent edge worlds (N,S,E,W) for rendering only
    if (world->seed_id[0])
    {
      if (universe_init(&uni, world->seed_id, WORLD_AFFINITY, 1))
      {
        have_uni = true;
        universe_place(&uni, 0, 0, 0, world);
        // Generate and place 4 adjacent wilderness worlds
        const int dx[4] = {1,-1, 0, 0};
        const int dy[4] = {0, 0, 1,-1};
        for (int i = 0; i < 4; i++)
        {
          World* w2 = world_create(world->width, world->height, world->depth);
          if (!w2) continue;
          // Derive a neighbor seed id by appending direction tag
          char neigh_seed[80];
          snprintf(neigh_seed, sizeof(neigh_seed), "%s_%c", world->seed_id, (i==0?'E':i==1?'W':i==2?'N':'S'));
          world_generate_with_type(w2, neigh_seed, WORLD_TYPE_WILDERNESS);
          universe_place_adjacent(&uni, 0,0,0, dx[i], dy[i], 0, w2);
        }
      }
    }
  }
  else if (generate_new)
  {
    // Generate a fresh wilderness world of default size
    if (world)
    {
      // Build a simple random 64-hex seed from time()
      char seed[65];
      unsigned int t = (unsigned int)time(NULL);
      for (int i = 0; i < 32; i++)
      {
        unsigned int v = (t = t * 1103515245u + 12345u);
        sprintf(seed + i * 2, "%02x", (unsigned)(v & 0xFF));
      }
      seed[64] = '\0';
      world_generate_with_type(world, seed, WORLD_TYPE_WILDERNESS);
      sx = world->width;
      sy = world->height;
      sz = world->depth;
      printf("Generated wilderness world. seed_id: %s\n", world->seed_id);
    }
  }
  else
  {
    if (!model_transformer_load_vox(world, vox_path, 0, 0, 0, VOXEL_STONE))
    {
      printf("Failed to load .vox data: %s\n", vox_path);
      world_destroy(world);
      SDL_DestroyRenderer(ren);
      SDL_DestroyWindow(win);
      if (g_font)
      {
        TTF_CloseFont(g_font);
        g_font = NULL;
      }
      TTF_Quit();
      SDL_Quit();
      return 1;
    }
  }

  // Start at layer 0 for faster initial rendering
  int z = 0;
  bool running = true;
  bool iso = true; // default to isometric view
  printf("Loaded: %s (%ux%ux%u). Controls: PageUp/]/= increase Z, PageDown/[/- decrease Z, v toggle view, ESC to quit.\n",
         vox_path, sx, sy, sz);

  g_voxel_dirty = true;
  // For now, render and compute stats from current world; adjacent worlds are not included in stats
  draw_frame(ren, vox_path, world, world, z, iso);
  printf("Viewing Z layer: %d (0..%u)\n", z, world->depth - 1);

  Uint32 last_ticks = SDL_GetTicks();
  float avg_dt_ms = 0.0f;
  while (running)
  {
    // FPS reporting (one-line): previous dt and running average
    Uint32 now = SDL_GetTicks();
    Uint32 dt = now - last_ticks;
    last_ticks = now;
    // Update viewer runtime clock
    static Uint32 last_tick = 0;
    if (last_tick == 0)
      last_tick = now;
    if (g_clock_running)
    {
      g_clock_ms += (uint64_t)((now - last_tick) * (uint64_t)BASE_TIME_FACTOR);
    }
    last_tick = now;
    // Epoch every 10 minutes: 5% chance to bloom FLOWERING into FLOWER
    uint32_t new_epoch = (uint32_t)(g_clock_ms / 600000ULL);
    if (new_epoch > g_clock_epoch_index && world)
    {
      g_clock_epoch_index = new_epoch;
      // Attempt random flowering -> flower
      // Seed a simple PRNG based on epoch index
      uint32_t s = 1469598103u ^ g_clock_epoch_index;
      s *= 16777619u;
      if (g_clock_epoch_index >= MIN_EPOCHS_FOR_BLOOM)
      {
        for (int tries = 0; tries < 256; tries++)
        {
          s = s * 1664525u + 1013904223u;
          uint32_t rx = (s >> 8);
          s = s * 1664525u + 1013904223u;
          uint32_t ry = (s >> 8);
          s = s * 1664525u + 1013904223u;
          uint32_t rz = (s >> 8);
          uint32_t x = (world->width > 0) ? (rx % world->width) : 0;
          uint32_t y = (world->height > 0) ? (ry % world->height) : 0;
          uint32_t z = (world->depth > 0) ? (rz % world->depth) : 0;
          Voxel *v = world_get_voxel(world, x, y, z);
          if (v && v->type == VOXEL_GRASS_SHARP)
          {
            // 5% chance
            s = s * 1664525u + 1013904223u;
            if ((s % 100u) < 5u)
            {
              if (world_has_voxel_condition((World *)world, x, y, z, "FLOWERING"))
              {
                world_set_voxel((World *)world, x, y, z, VOXEL_BUSH);
                world_remove_voxel_condition((World *)world, x, y, z, "FLOWERING");
                break;
              }
            }
          }
        }
      }
      // Toast: The world ticks on...
      // TODO: fix that this flickers in and immediately disappears (probably 100ms timer)
      const char *toast = "The world ticks on...";
      // Use a minimal internal toast renderer at top-left following verse-client style
      if (g_font && toast && *toast)
      {
        // Clear small area and draw text (no shadow animation here to keep viewer simple)
        int tw = 0, th = 0;
        TTF_SizeText(g_font, toast, &tw, &th);
        SDL_Rect bg = {8, 6, tw + 6, th + 4};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
        SDL_RenderFillRect(ren, &bg);
        draw_text(ren, toast, 11, 8, (SDL_Color){255, 255, 255, 255});
        SDL_RenderPresent(ren);
      }
    }
    if (avg_dt_ms <= 0.0f)
      avg_dt_ms = (float)dt;
    else
      avg_dt_ms = avg_dt_ms * 0.9f + (float)dt * 0.1f;
    (void)dt;
    (void)avg_dt_ms; // suppress unused warnings in viewer
    // printf("FPS: %.1f  dt:%ums  avg: %.1f fps (%.1f ms)\n", inst_fps, (unsigned)dt, avg_fps, avg_dt_ms);

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
      if (e.type == SDL_QUIT)
        running = false;
      if (e.type == SDL_KEYDOWN)
      {
        SDL_Keycode key = e.key.keysym.sym;
        if (key == SDLK_SPACE)
        {
          g_clock_running = !g_clock_running;
        }
        if (g_radius_input_active)
        {
          if (key == SDLK_ESCAPE)
          {
            g_radius_input_active = false;
            draw_frame(ren, vox_path, world, world, z, iso);
            continue;
          }
          if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
          {
            // Parse integer radius and draw sphere
            int radius = atoi(g_radius_input_buf);
            if (radius < 0)
              radius = 0;
            world_draw_sphere(world, (uint32_t)radius, VOXEL_STONE);
            g_radius_input_active = false;
            // Re-render
            draw_frame(ren, vox_path, world, world, z, iso);
            continue;
          }
          // Only accept digits and backspace
          if (key >= SDLK_0 && key <= SDLK_9)
          {
            size_t len = strlen(g_radius_input_buf);
            if (len < sizeof(g_radius_input_buf) - 1)
            {
              g_radius_input_buf[len] = (char)('0' + (key - SDLK_0));
              g_radius_input_buf[len + 1] = '\0';
            }
            ensure_font_size_for_grid(world, iso);
            if (iso)
              render_slice_isometric(ren, world, z);
            else
              render_slice(ren, world, z);
            draw_overlay(ren, vox_path, world, z);
            SDL_RenderPresent(ren);
            continue;
          }
          if (key == SDLK_BACKSPACE)
          {
            size_t len = strlen(g_radius_input_buf);
            if (len > 0)
              g_radius_input_buf[len - 1] = '\0';
            ensure_font_size_for_grid(world, iso);
            if (iso)
              render_slice_isometric(ren, world, z);
            else
              render_slice(ren, world, z);
            draw_overlay(ren, vox_path, world, z);
            SDL_RenderPresent(ren);
            continue;
          }
          // Ignore all other keys while in input
        }
        if (key == SDLK_ESCAPE)
        {
          if (g_context_menu_open)
          {
            g_context_menu_open = false;
            g_context_menu_hover_index = -1;
            ensure_font_size_for_grid(world, iso);
            if (iso)
              render_slice_isometric(ren, world, z);
            else
              render_slice(ren, world, z);
            draw_overlay(ren, vox_path, world, z);
            SDL_RenderPresent(ren);
            continue;
          }
          if (g_swap_mode_active)
          {
            g_swap_mode_active = false;
            g_swap_source_type = -1;
            ensure_font_size_for_grid(world, iso);
            if (iso)
              render_slice_isometric(ren, world, z);
            else
              render_slice(ren, world, z);
            draw_overlay(ren, vox_path, world, z);
            SDL_RenderPresent(ren);
            continue;
          }
          running = false;
        }
        if (key == SDLK_v)
        {
          iso = !iso;
          g_iso_mode = iso;
          g_voxel_dirty = true;
          draw_frame(ren, vox_path, world, world, z, iso);
          continue;
        }
        if (key == SDLK_a || key == SDLK_d)
        {
          if (key == SDLK_a)
            g_yaw_index = (g_yaw_index + 3) & 3;
          else
            g_yaw_index = (g_yaw_index + 1) & 3;
          g_voxel_dirty = true;
          draw_frame(ren, vox_path, world, world, z, iso);
          continue;
        }
        if (key == SDLK_w || key == SDLK_s)
        {
          if (key == SDLK_w)
            g_pitch_index = (g_pitch_index + 1) & 3;
          else
            g_pitch_index = (g_pitch_index + 3) & 3;
          // After pitch change, clamp current z within new transformed vertical range
          if (z < 0)
            z = 0;
          if (z >= (int)world->depth)
            z = (int)world->depth - 1;
          g_voxel_dirty = true;
          draw_frame(ren, vox_path, world, world, z, iso);
          continue;
        }
        if (key == SDLK_p)
        {
          // Cycle export face for debugging
          g_current_face = (WorldFace)(((int)g_current_face + 1) % 6);
          printf("Export face set to %s. Press 'e' to dump.\n", face_name(g_current_face));
          continue;
        }
        if (key == SDLK_e)
        {
          // Export the current slice in the rotated vertical axis to a PPM/BMP file
          uint8_t *fb = NULL;
          uint32_t fw = 0, fh = 0, fs = 0;
          uint32_t slice = (uint32_t)z;
          if (world_face_to_rgb24(world, g_current_face, slice, &fb, &fw, &fh, &fs))
          {
            char path_ppm[128];
            snprintf(path_ppm, sizeof(path_ppm), "face_%s_slice_%u.ppm", face_name(g_current_face), (unsigned)slice);
            if (write_ppm_rgb24(path_ppm, fb, fw, fh, fs))
              printf("Wrote %s (%ux%u)\n", path_ppm, (unsigned)fw, (unsigned)fh);
            else
              printf("Failed to write PPM file.\n");
            char path_bmp[128];
            snprintf(path_bmp, sizeof(path_bmp), "face_%s_slice_%u.bmp", face_name(g_current_face), (unsigned)slice);
            if (write_bmp_rgb24(path_bmp, fb, fw, fh, fs))
              printf("Wrote %s (%ux%u)\n", path_bmp, (unsigned)fw, (unsigned)fh);
            else
              printf("Failed to write BMP file.\n");
            free(fb);
          }
          else
          {
            printf("world_face_to_rgb24 failed for face %s slice %u\n", face_name(g_current_face), (unsigned)slice);
          }
          continue;
        }
        if (key == SDLK_PAGEUP || key == SDLK_RIGHTBRACKET || key == SDLK_EQUALS || key == SDLK_PLUS)
        {
          if (z < (int)world->depth - 1)
            z++;
          g_voxel_dirty = true;
          draw_frame(ren, vox_path, world, world, z, iso);
          printf("Viewing Z layer: %d\n", z);
        }
        if (key == SDLK_PAGEDOWN || key == SDLK_LEFTBRACKET || key == SDLK_MINUS)
        {
          if (z > 0)
            z--;
          g_voxel_dirty = true;
          draw_frame(ren, vox_path, world, world, z, iso);
          printf("Viewing Z layer: %d\n", z);
        }
      }
      // Hover palette/chart tracking (outside keydown)
      if (e.type == SDL_MOUSEMOTION)
      {
        int mx = e.motion.x;
        int my = e.motion.y;
        // Update viewport hover voxel first
        // Debounce hover selection to reduce flicker: prefer stability across frames
        static int last_hx = -1, last_hy = -1, last_hz = -1;
        static int stable_hx = -1, stable_hy = -1, stable_hz = -1;
        static Uint32 last_change_ms = 0;
        // Track last mouse pixel to avoid redundant recomputation
        static int last_mx = -1, last_my = -1;
        // Reset stabilization when view parameters change
        static int last_view_z = -9999;
        static int last_yaw = -9999;
        static int last_pitch = -9999;
        static bool last_iso = false;
        if (last_view_z != z || last_yaw != g_yaw_index || last_pitch != g_pitch_index || last_iso != iso)
        {
          last_view_z = z;
          last_yaw = g_yaw_index;
          last_pitch = g_pitch_index;
          last_iso = iso;
          last_hx = last_hy = last_hz = -1;
          stable_hx = stable_hy = stable_hz = -1;
          last_change_ms = 0;
          last_mx = last_my = -1;
        }
        uint32_t hx, hy, hz;
        bool had_hover = g_have_hovered_voxel;
        int prev_hx = g_hovered_world_x, prev_hy = g_hovered_world_y, prev_hz = g_hovered_world_z;
        bool picked = false;
        if (mx != last_mx || my != last_my)
        {
          picked = pick_voxel_from_screen(world, mx, my, iso, z, &hx, &hy, &hz);
          last_mx = mx;
          last_my = my;
        }
        else if (g_have_hovered_voxel)
        {
          // Reuse current stable hover when mouse hasn't moved
          hx = (uint32_t)g_hovered_world_x;
          hy = (uint32_t)g_hovered_world_y;
          hz = (uint32_t)g_hovered_world_z;
        }
        if (picked)
        {
          int nx = (int)hx, ny = (int)hy, nz = (int)hz;
          Uint32 now_ms = SDL_GetTicks();
          if (nx != last_hx || ny != last_hy || nz != last_hz)
          {
            // Candidate changed; start/update stabilization window (25ms)
            last_hx = nx;
            last_hy = ny;
            last_hz = nz;
            last_change_ms = now_ms;
          }
          // Accept candidate after small dwell time to avoid jitter between adjacent voxels
          Uint32 dwell_ms = 25;
          if ((now_ms - last_change_ms) >= dwell_ms)
          {
            stable_hx = last_hx;
            stable_hy = last_hy;
            stable_hz = last_hz;
          }
          if (stable_hx >= 0)
          {
            g_have_hovered_voxel = true;
            g_hovered_world_x = stable_hx;
            g_hovered_world_y = stable_hy;
            g_hovered_world_z = stable_hz;
          }
        }
        else
        {
          g_have_hovered_voxel = false;
          g_hovered_world_x = g_hovered_world_y = g_hovered_world_z = -1;
          last_hx = last_hy = last_hz = -1;
          stable_hx = stable_hy = stable_hz = -1;
        }
        // If a voxel is hovered, sync palette hover type to that voxel type
        if (g_have_hovered_voxel)
        {
          const Voxel *v = world_get_voxel(world, (uint32_t)g_hovered_world_x, (uint32_t)g_hovered_world_y, (uint32_t)g_hovered_world_z);
          if (v)
            g_hovered_voxel_type = (int)v->type;
        }
        // Always redraw when hover coords change (force full rerender to avoid artifacts)
        if (had_hover != g_have_hovered_voxel || prev_hx != g_hovered_world_x || prev_hy != g_hovered_world_y || prev_hz != g_hovered_world_z)
        {
          g_voxel_dirty = true;
          draw_frame(ren, vox_path, world, world, z, iso);
        }
        if (!g_palette_layout_valid)
        {
          continue;
        }
        // Context menu hover handling takes precedence
        if (g_context_menu_open)
        {
          g_context_menu_hover_index = -1;
          if (mx >= g_context_menu_rect.x && mx < g_context_menu_rect.x + g_context_menu_rect.w &&
              my >= g_context_menu_rect.y && my < g_context_menu_rect.y + g_context_menu_rect.h)
          {
            // Determine which item under cursor
            int pad = 6;
            int line_h = g_font ? TTF_FontHeight(g_font) : 12;
            int item_h = line_h + 4;
            int items = 2 + (g_context_menu_source_type >= 0 ? 1 : 0); // this is approximate; selection is resolved on click
            int rel_y = my - (g_context_menu_rect.y + pad);
            int idx = rel_y / (item_h + 2);
            if (idx >= 0 && idx < items)
              g_context_menu_hover_index = idx;
          }
          // Do not update palette hover while menu open
          draw_frame(ren, vox_path, world, world, z, iso);
          continue;
        }
        int new_hover = -1;
        for (int i = 0; i < (int)VOXEL_COUNT; i++)
        {
          SDL_Rect hr = g_palette_item_rects[i];
          if (mx >= hr.x && mx < hr.x + hr.w && my >= hr.y && my < hr.y + hr.h)
          {
            new_hover = i;
            break;
          }
        }
        if (new_hover < 0)
        {
          for (int i = 0; i < (int)VOXEL_COUNT; i++)
          {
            SDL_Rect cr = g_chart_item_rects[i];
            if (cr.w > 0 && cr.h > 0 && mx >= cr.x && mx < cr.x + cr.w && my >= cr.y && my < cr.y + cr.h)
            {
              new_hover = i;
              break;
            }
          }
        }
        // Track whether the hover originated from palette or chart
        bool prev_palette_or_chart = g_palette_or_chart_hover;
        // During swap, suppress type-wide outline even if hovering palette/chart
        g_palette_or_chart_hover = (!g_swap_mode_active && new_hover >= 0);
        if (new_hover != g_hovered_voxel_type)
        {
          if (new_hover >= 0)
          {
            // printf("hoverin: %s\n", world_voxel_type_name((VoxelType)new_hover));
          }
          else if (g_hovered_voxel_type >= 0)
          {
            // printf("hoverout: %s\n", world_voxel_type_name((VoxelType)g_hovered_voxel_type));
          }
          g_hovered_voxel_type = new_hover;
          g_voxel_dirty = true;
          draw_frame(ren, vox_path, world, world, z, iso);
        }
        else if (prev_palette_or_chart != g_palette_or_chart_hover)
        {
          // Redraw to toggle mass-outline mode when entering/exiting palette/chart hover
          g_voxel_dirty = true;
          draw_frame(ren, vox_path, world, world, z, iso);
        }
      }
      // Mouse button handling for context menu and swap mode
      if (e.type == SDL_MOUSEBUTTONDOWN)
      {
        int mx = e.button.x;
        int my = e.button.y;
        if (e.button.button == SDL_BUTTON_RIGHT)
        {
          // Open context menu at cursor; include Change/Delete when a voxel is targeted
          g_context_menu_open = true;
          g_context_menu_hover_index = -1;
          g_context_menu_source_type = (g_hovered_voxel_type >= 0 ? g_hovered_voxel_type : -1);
          g_context_menu_rect.x = mx;
          g_context_menu_rect.y = my;
          // Determine voxel target at click location
          uint32_t tx, ty, tz;
          if (pick_voxel_from_screen(world, mx, my, iso, z, &tx, &ty, &tz))
          {
            g_context_menu_voxel_has_target = true;
            g_context_menu_voxel_x = tx;
            g_context_menu_voxel_y = ty;
            g_context_menu_voxel_z = tz;
          }
          else
          {
            g_context_menu_voxel_has_target = false;
          }
          // Close any ongoing swap mode
          g_swap_mode_active = false;
          g_swap_source_type = -1;
          draw_frame(ren, vox_path, world, world, z, iso);
          continue;
        }
        if (e.button.button == SDL_BUTTON_LEFT)
        {
          if (g_radius_input_active)
          {
            // Any click exits input mode
            g_radius_input_active = false;
            draw_frame(ren, vox_path, world, world, z, iso);
            continue;
          }
          if (g_context_menu_open)
          {
            // If clicked inside menu rect, select first (and only) option: Swap...
            bool inside = (mx >= g_context_menu_rect.x && mx < g_context_menu_rect.x + g_context_menu_rect.w &&
                           my >= g_context_menu_rect.y && my < g_context_menu_rect.y + g_context_menu_rect.h);
            if (inside)
            {
              // Clicked an item: 0=clock, 1=draw, 2=swap/change (if available), 3=delete (if available), last=crop
              int pad = 6;
              int line_h = g_font ? TTF_FontHeight(g_font) : 12;
              int item_h = line_h + 4;
              int items = 3 + (g_context_menu_source_type >= 0 ? 1 : 0) + (g_context_menu_voxel_has_target ? 1 : 0);
              int rel_y = my - (g_context_menu_rect.y + pad);
              int idx = rel_y / (item_h + 2);
              if (idx < 0)
                idx = 0;
              if (idx >= items)
                idx = items - 1;
              if (idx == 0)
              {
                // Toggle clock
                g_clock_running = !g_clock_running;
                g_context_menu_open = false;
                g_context_menu_hover_index = -1;
              }
              else if (idx == 1)
              {
                // Open Draw submenu near main menu
                g_draw_submenu_open = true;
                g_draw_submenu_rect.x = g_context_menu_rect.x + g_context_menu_rect.w + 6;
                g_draw_submenu_rect.y = g_context_menu_rect.y + 6 + (item_h + 2) * 1; // align with Draw row
                g_context_menu_open = false;                                          // close main menu when opening submenu
                g_context_menu_hover_index = -1;
              }
              else if (g_context_menu_source_type >= 0 && idx == 2)
              {
                // Swap... or Change...
                g_swap_mode_active = true;
                g_swap_source_type = g_context_menu_source_type;
                g_swap_single_target = g_context_menu_voxel_has_target;
                if (g_swap_single_target)
                {
                  g_swap_target_x = g_context_menu_voxel_x;
                  g_swap_target_y = g_context_menu_voxel_y;
                  g_swap_target_z = g_context_menu_voxel_z;
                }
                g_context_menu_open = false;
                g_context_menu_hover_index = -1;
              }
              else if (g_context_menu_voxel_has_target && ((g_context_menu_source_type >= 0 && idx == 3) || (g_context_menu_source_type < 0 && idx == 2)))
              {
                // Delete voxel -> set to AIR
                world_set_voxel(world, g_context_menu_voxel_x, g_context_menu_voxel_y, g_context_menu_voxel_z, VOXEL_AIR);
                g_context_menu_open = false;
                g_context_menu_hover_index = -1;
                g_voxel_dirty = true;
                // Reset hover stabilization since underlying content changed
                g_have_hovered_voxel = false;
                g_hovered_world_x = g_hovered_world_y = g_hovered_world_z = -1;
              }
              else
              {
                // Autocrop
                if (world_autocrop(world))
                {
                  if (z >= (int)world->depth)
                    z = (int)world->depth - 1;
                }
                g_context_menu_open = false;
                g_context_menu_hover_index = -1;
              }
            }
            else
            {
              // Click outside closes menu
              g_context_menu_open = false;
              g_context_menu_hover_index = -1;
            }
            g_voxel_dirty = true;
            draw_frame(ren, vox_path, world, world, z, iso);
            continue;
          }
          // Draw submenu click handling
          if (g_draw_submenu_open)
          {
            bool inside = (mx >= g_draw_submenu_rect.x && mx < g_draw_submenu_rect.x + g_draw_submenu_rect.w &&
                           my >= g_draw_submenu_rect.y && my < g_draw_submenu_rect.y + g_draw_submenu_rect.h);
            if (inside)
            {
              // Only one item: Sphere...
              g_radius_input_active = true;
              g_radius_input_buf[0] = '\0';
              g_cursor_blink_start = SDL_GetTicks();
            }
            g_draw_submenu_open = false;
            ensure_font_size_for_grid(world, iso);
            if (iso)
              render_slice_isometric(ren, world, z);
            else
              render_slice(ren, world, z);
            draw_overlay(ren, vox_path, world, z);
            SDL_RenderPresent(ren);
            continue;
          }
          if (g_swap_mode_active)
          {
            // Choose destination by current hover if valid
            if (g_hovered_voxel_type >= 0)
            {
              VoxelType dst = (VoxelType)g_hovered_voxel_type;
              VoxelType src = (VoxelType)g_swap_source_type;
              if (g_swap_single_target)
              {
                // Apply single-voxel change
                world_set_voxel(world, g_swap_target_x, g_swap_target_y, g_swap_target_z, dst);
              }
              else
              {
                swap_voxel_types(world, src, dst);
              }
              g_swap_mode_active = false;
              g_swap_source_type = -1;
              g_swap_single_target = false;
              // Re-render after transformation
              g_voxel_dirty = true;
              // Reset hover stabilization since geometry may have changed
              g_have_hovered_voxel = false;
              g_hovered_world_x = g_hovered_world_y = g_hovered_world_z = -1;
              draw_frame(ren, vox_path, world, world, z, iso);
              continue;
            }
          }
        }
      }
    }
    // Periodic redraw at 100ms cadence so the visual timer updates smoothly but not every frame
    static Uint32 last_redraw = 0;
    if (now - last_redraw >= 100)
    {
      ensure_font_size_for_grid(world, iso);
      if (iso)
        render_slice_isometric(ren, world, z);
      else
        render_slice(ren, world, z);
      draw_overlay(ren, vox_path, world, z);
      SDL_RenderPresent(ren);
      last_redraw = now;
    }
    SDL_Delay(10);
  }

  world_destroy(world);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  if (g_font)
  {
    TTF_CloseFont(g_font);
    g_font = NULL;
  }
  TTF_Quit();
  SDL_Quit();
  return 0;
}
