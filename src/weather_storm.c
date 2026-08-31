#include "weather_storm.h"

#include "fire_sim.h"
#include "lightning_path.h"
#include "particle_effects.h"
#include "volcano.h"
#include "voxel.h"

#include <stdlib.h>

static uint32_t storm_hash(uint32_t a, uint32_t b, uint32_t c)
{
  uint32_t h = a * 374761393u + b * 668265263u + c * 2147483647u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

static int column_top_solid_z(World *world, int x, int y)
{
  if (!world || !world_is_position_valid(world, (uint32_t)x, (uint32_t)y, 0))
    return -1;
  for (int z = (int)world->depth - 1; z >= 0; z--)
  {
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (v && v->type != VOXEL_AIR && v->type != VOXEL_STEAM)
      return z;
  }
  return -1;
}

static bool storm_try_ignite_near(World *world, int x, int y, int z)
{
  static const int nbs[7][3] = {
      {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  bool ignited = false;
  for (int i = 0; i < 7; i++)
  {
    const int nx = x + nbs[i][0];
    const int ny = y + nbs[i][1];
    const int nz = z + nbs[i][2];
    if (fire_can_ignite_at(world, nx, ny, nz) &&
        fire_ignite_at(world, nx, ny, nz, "BURNING_HIGH"))
      ignited = true;
  }
  return ignited;
}

bool weather_storm_strike_at(World *world, int x, int y, uint32_t seed)
{
  if (!world || !world->voxels)
    return false;
  if (!world_is_position_valid(world, (uint32_t)x, (uint32_t)y, 0))
    return false;

  const int top = column_top_solid_z(world, x, y);
  if (top < 0)
    return false;

  // Bolt descends from open sky into the impact column — same jagged grammar as a volcano vent.
  const int z_sky = (int)world->depth - 1;
  const int z_hit = top + 1 < (int)world->depth ? top + 1 : top;
  LightningPathPoint path[LIGHTNING_PATH_MAX_POINTS];
  const int jx = (int)((seed >> 3) & 3u) - 1;
  const int jy = (int)((seed >> 7) & 3u) - 1;
  int sx = x + jx;
  int sy = y + jy;
  if (sx < 0)
    sx = 0;
  if (sy < 0)
    sy = 0;
  if ((uint32_t)sx >= world->width)
    sx = (int)world->width - 1;
  if ((uint32_t)sy >= world->height)
    sy = (int)world->height - 1;

  const int n = lightning_path_generate(seed ? seed : 1u, sx, sy, z_sky, x, y, z_hit, path,
                                        LIGHTNING_PATH_MAX_POINTS);
  if (n < 2)
    return false;

  bool ignited = storm_try_ignite_near(world, x, y, top);
  const int start = n > 6 ? n - 6 : 0;
  for (int i = start; i < n; i++)
  {
    if (storm_try_ignite_near(world, path[i].x, path[i].y, path[i].z))
      ignited = true;
  }
  return ignited;
}

void weather_storm_tick(World *world, uint32_t tick_salt)
{
  if (!world)
    return;

  // Volcano pumps are independent of the weather overlay — vents erupt on their own clock.
  volcano_try_pump(world, tick_salt ^ 0x70c4a0u);

  const bool forced = particle_effects_thunderstorm_enabled(world) ||
                      particle_effects_thunderstorm_universe_enabled();
  const bool storming = forced || particle_effects_heavy_rain_enabled(world);
  if (!storming)
    return;

  const uint32_t h =
      storm_hash(tick_salt, (uint32_t)world->universe_x, (uint32_t)world->universe_y);
  if (forced)
  {
    if ((h & 0x7u) != 0u)
      return;
  }
  else if ((h & 0x1fu) != 0u)
  {
    return;
  }

  const uint32_t W = world->width;
  const uint32_t H = world->height;
  if (W < 4 || H < 4)
    return;
  const int x = 1 + (int)((h >> 8) % (W - 2));
  const int y = 1 + (int)((h >> 16) % (H - 2));
  weather_storm_strike_at(world, x, y, h ^ 0x11ce01u);
}
