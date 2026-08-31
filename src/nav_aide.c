#include "nav_aide.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void nav_aide_clear(NavAide *nav)
{
  if (!nav)
    return;
  memset(nav, 0, sizeof(*nav));
}

void nav_aide_set_target(NavAide *nav,
                         uint64_t ux, uint64_t uy, uint64_t uz,
                         float x, float y, float z,
                         const char *label)
{
  if (!nav)
    return;
  nav->active = true;
  nav->target_ux = ux;
  nav->target_uy = uy;
  nav->target_uz = uz;
  nav->target_x = x;
  nav->target_y = y;
  nav->target_z = z;
  if (label && label[0])
  {
    strncpy(nav->label, label, sizeof(nav->label) - 1);
    nav->label[sizeof(nav->label) - 1] = '\0';
  }
  else
  {
    nav->label[0] = '\0';
  }
}

bool nav_aide_active(const NavAide *nav)
{
  return nav && nav->active;
}

bool nav_aide_world_bearing(const NavAide *nav,
                            uint64_t player_ux, uint64_t player_uy, uint64_t player_uz,
                            float player_x, float player_y,
                            uint32_t world_w, uint32_t world_h,
                            float *out_bearing_rad)
{
  if (!nav || !nav->active || !out_bearing_rad || world_w == 0 || world_h == 0)
    return false;

  // Absolute planar position in universe voxel space so neighbouring cells still aim correctly.
  const double abs_px =
      (double)((int64_t)player_ux) * (double)world_w + (double)player_x;
  const double abs_py =
      (double)((int64_t)player_uy) * (double)world_h + (double)player_y;
  const double abs_tx =
      (double)((int64_t)nav->target_ux) * (double)world_w + (double)nav->target_x;
  const double abs_ty =
      (double)((int64_t)nav->target_uy) * (double)world_h + (double)nav->target_y;

  (void)player_uz;
  const double dx = abs_tx - abs_px;
  const double dy = abs_ty - abs_py;
  if (fabs(dx) < 1e-4 && fabs(dy) < 1e-4)
  {
    *out_bearing_rad = 0.0f;
    return true;
  }
  // Same convention as facing_yaw / aim_yaw: atan2(dy, dx), forward = (cos, sin).
  *out_bearing_rad = (float)atan2(dy, dx);
  return true;
}

bool nav_aide_planar_distance(const NavAide *nav,
                              uint64_t player_ux, uint64_t player_uy, uint64_t player_uz,
                              float player_x, float player_y,
                              uint32_t world_w, uint32_t world_h,
                              float *out_distance_m)
{
  if (!nav || !nav->active || !out_distance_m || world_w == 0 || world_h == 0)
    return false;

  const double abs_px =
      (double)((int64_t)player_ux) * (double)world_w + (double)player_x;
  const double abs_py =
      (double)((int64_t)player_uy) * (double)world_h + (double)player_y;
  const double abs_tx =
      (double)((int64_t)nav->target_ux) * (double)world_w + (double)nav->target_x;
  const double abs_ty =
      (double)((int64_t)nav->target_uy) * (double)world_h + (double)nav->target_y;

  (void)player_uz;
  const double dx = abs_tx - abs_px;
  const double dy = abs_ty - abs_py;
  *out_distance_m = (float)sqrt(dx * dx + dy * dy);
  return true;
}

bool nav_aide_format_distance(float distance_m, char *buf, size_t buflen)
{
  if (!buf || buflen < 4 || !(distance_m > NAV_AIDE_DISTANCE_SHOW_M))
    return false;

  if (distance_m >= 1000.0f)
  {
    // One decimal kilometre keeps multi-cell journeys readable at 256×240.
    snprintf(buf, buflen, "%.1fkm", (double)(distance_m / 1000.0f));
  }
  else
  {
    snprintf(buf, buflen, "%.0fm", (double)distance_m);
  }
  return true;
}

float nav_aide_relative_yaw(float bearing_rad, float facing_rad)
{
  float rel = bearing_rad - facing_rad;
  while (rel > (float)M_PI)
    rel -= 2.0f * (float)M_PI;
  while (rel < -(float)M_PI)
    rel += 2.0f * (float)M_PI;
  return rel;
}

// Screen mapping for a facing-relative HUD: relative 0 points UP (forward on screen).
// SDL Y grows downward, so forward = (sin(rel), -cos(rel)).
static inline void nav_aide_screen_dir(float relative_yaw, float *out_fx, float *out_fy)
{
  *out_fx = sinf(relative_yaw);
  *out_fy = -cosf(relative_yaw);
}

static void nav_aide_draw_line(SDL_Renderer *renderer, float x0, float y0, float x1, float y1)
{
  SDL_RenderDrawLine(renderer, (int)lroundf(x0), (int)lroundf(y0), (int)lroundf(x1),
                     (int)lroundf(y1));
}

// Classic chevron / arrowhead: a V pointing along relative_yaw (0 = screen up).
static void nav_aide_draw_chevron(SDL_Renderer *renderer, int cx, int cy, float relative_yaw,
                                  float size, float depth_scale, float width_scale)
{
  float fx, fy;
  nav_aide_screen_dir(relative_yaw, &fx, &fy);
  // Perpendicular in screen space (rotate forward 90° clockwise for a symmetric V).
  const float px = fy;
  const float py = -fx;

  const float tip_x = (float)cx + fx * size;
  const float tip_y = (float)cy + fy * size;
  const float depth = size * depth_scale;
  const float width = size * width_scale;
  const float lx = tip_x - fx * depth + px * width;
  const float ly = tip_y - fy * depth + py * width;
  const float rx = tip_x - fx * depth - px * width;
  const float ry = tip_y - fy * depth - py * width;

  // Slight thickness so the chevron reads at 256×240.
  for (int o = -1; o <= 1; o++)
  {
    const float ox = px * (float)o * 0.35f;
    const float oy = py * (float)o * 0.35f;
    nav_aide_draw_line(renderer, tip_x + ox, tip_y + oy, lx + ox, ly + oy);
    nav_aide_draw_line(renderer, tip_x + ox, tip_y + oy, rx + ox, ry + oy);
  }
}

void nav_aide_render(SDL_Renderer *renderer, int base_w, int base_h,
                     float relative_yaw, const char *label)
{
  if (!renderer || base_w <= 0 || base_h <= 0)
    return;

  // Compact pointer just above the spirit avatar / crosshair (screen center).
  const int cx = base_w / 2;
  const int cy = base_h / 2 - 30;
  const float size = 11.0f;

  // Drop shadow, then bright chevron. A second trailing chevron reads as a directional arrow.
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
  nav_aide_draw_chevron(renderer, cx + 1, cy + 1, relative_yaw, size, 0.85f, 0.70f);
  nav_aide_draw_chevron(renderer, cx + 1, cy + 1, relative_yaw, size * 0.62f, 0.85f, 0.70f);

  SDL_SetRenderDrawColor(renderer, 255, 224, 96, 255);
  nav_aide_draw_chevron(renderer, cx, cy, relative_yaw, size, 0.85f, 0.70f);
  SDL_SetRenderDrawColor(renderer, 255, 240, 160, 255);
  nav_aide_draw_chevron(renderer, cx, cy, relative_yaw, size * 0.62f, 0.85f, 0.70f);

  (void)label; // Label is shown via toast / quest UI; arrow stays uncluttered at 256×240.
}
