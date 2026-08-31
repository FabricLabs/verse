#ifndef NAV_AIDE_H
#define NAV_AIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL2/SDL.h>

// Reusable screen-space guidance pointer. Missions set a world-space target; the HUD draws a
// chevron toward it relative to the player's facing. Clear when the objective is done.

typedef struct NavAide
{
  bool active;
  uint64_t target_ux, target_uy, target_uz;
  float target_x, target_y, target_z;
  char label[64];
} NavAide;

void nav_aide_clear(NavAide *nav);
void nav_aide_set_target(NavAide *nav,
                         uint64_t ux, uint64_t uy, uint64_t uz,
                         float x, float y, float z,
                         const char *label);
bool nav_aide_active(const NavAide *nav);

// Bearing from the player toward the target, in world radians (atan2(dy, dx)).
// Same convention as facing_yaw / aim_yaw: forward = (cos, sin).
bool nav_aide_world_bearing(const NavAide *nav,
                            uint64_t player_ux, uint64_t player_uy, uint64_t player_uz,
                            float player_x, float player_y,
                            uint32_t world_w, uint32_t world_h,
                            float *out_bearing_rad);

// Planar distance in metres (1 voxel = 1 m) from the player to the target.
bool nav_aide_planar_distance(const NavAide *nav,
                              uint64_t player_ux, uint64_t player_uy, uint64_t player_uz,
                              float player_x, float player_y,
                              uint32_t world_w, uint32_t world_h,
                              float *out_distance_m);

// Distance label is shown only beyond this range so nearby objectives stay uncluttered.
#define NAV_AIDE_DISTANCE_SHOW_M 50.0f

// Writes a compact distance string ("51m", "1.2km"). Returns true when distance_m is above
// NAV_AIDE_DISTANCE_SHOW_M and the buffer was filled.
bool nav_aide_format_distance(float distance_m, char *buf, size_t buflen);

// bearing − facing, wrapped to (−π, π]. Relative 0 means "straight ahead".
float nav_aide_relative_yaw(float bearing_rad, float facing_rad);

// Draw a small chevron near screen center. relative_yaw 0 points UP (forward on the HUD).
void nav_aide_render(SDL_Renderer *renderer, int base_w, int base_h,
                     float relative_yaw, const char *label);

#endif // NAV_AIDE_H
