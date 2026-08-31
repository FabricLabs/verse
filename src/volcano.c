#include "volcano.h"

#include "fluid_sim.h"
#include "lightning_path.h"
#include "universe_biome.h"
#include "voxel.h"

#include <stdlib.h>
#include <string.h>

static uint32_t volcano_hash(uint32_t a, uint32_t b, uint32_t c)
{
  uint32_t h = a * 374761393u + b * 668265263u + c * 2147483647u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

bool world_register_magma_vent_column(World *world, int x, int y)
{
  if (!world || x < 0 || y < 0)
    return false;
  if ((uint32_t)x >= world->width || (uint32_t)y >= world->height)
    return false;
  if (world_has_magma_vent_column(world, x, y))
    return true;
  if (world->magma_vent_column_count >= WORLD_MAGMA_VENT_COLUMN_MAX)
    return false;
  const uint16_t i = world->magma_vent_column_count++;
  world->magma_vent_columns[i][0] = (int16_t)x;
  world->magma_vent_columns[i][1] = (int16_t)y;
  return true;
}

bool world_has_magma_vent_column(const World *world, int x, int y)
{
  if (!world || x < 0 || y < 0)
    return false;
  for (uint16_t i = 0; i < world->magma_vent_column_count; i++)
  {
    if (world->magma_vent_columns[i][0] == (int16_t)x &&
        world->magma_vent_columns[i][1] == (int16_t)y)
      return true;
  }
  return false;
}

void world_clear_magma_vent_columns(World *world)
{
  if (!world)
    return;
  world->magma_vent_column_count = 0;
  world->volcano_present = false;
}

static int column_surface_z(const int *tops, uint32_t w, uint32_t x, uint32_t y)
{
  if (!tops)
    return -1;
  return tops[(size_t)y * w + x];
}

bool wilderness_stamp_volcano(World *world, const int *tops, uint32_t salt)
{
  if (!world || !tops || !world->voxels)
    return false;
  if (world->generation_type != WORLD_TYPE_WILDERNESS)
    return false;
  if (world->width < 16 || world->height < 16 || world->depth < 8)
    return false;
  // Settlements keep their plaza clear of craters.
  if (world->settlement_scale > 0)
    return false;

  const uint32_t W = world->width;
  const uint32_t H = world->height;
  const uint32_t cx = W / 2;
  const uint32_t cy = H / 2;
  UniverseClimate climate = universe_climate_sample(world, cx, cy, -1.0f);
  UniverseBiomeSample biome = universe_biome_classify(&climate);

  // Rare overall; much more likely in volcanic provinces.
  float chance = 0.04f + 0.55f * biome.weights[UNIVERSE_BIOME_VOLCANIC] +
                 0.25f * climate.volcanic;
  if (biome.primary == UNIVERSE_BIOME_VOLCANIC)
    chance += 0.20f;
  const uint32_t roll = volcano_hash(salt, (uint32_t)world->universe_x, (uint32_t)world->universe_y);
  if ((float)(roll & 0xffffu) / 65535.0f > chance)
    return false;

  // Prefer a column that already has a bedrock hotspot so the vent roots into natural magma.
  int root_x = -1, root_y = -1, root_top = -1;
  const uint32_t attempt_salt = salt ^ 0x70c4a001u;
  for (int attempt = 0; attempt < 48; attempt++)
  {
    const uint32_t h = volcano_hash(attempt_salt, (uint32_t)attempt, roll);
    const int x = 4 + (int)(h % (W - 8));
    const int y = 4 + (int)((h >> 8) % (H - 8));
    const int top = column_surface_z(tops, W, (uint32_t)x, (uint32_t)y);
    if (top < 3)
      continue;
    // Bias toward noise hotspots; accept any tall column after enough tries.
    if (world_magma_hotspot_at(world, x, y) || attempt >= 24)
    {
      root_x = x;
      root_y = y;
      root_top = top;
      break;
    }
  }
  if (root_x < 0)
    return false;

  const int z0 = 1; // just above bedrock
  const int z1 = root_top;
  LightningPathPoint path[LIGHTNING_PATH_MAX_POINTS];
  const uint32_t path_seed = volcano_hash(salt, (uint32_t)root_x, (uint32_t)root_y);
  const int n = lightning_path_generate(path_seed, root_x, root_y, z0, root_x, root_y, z1, path,
                                        LIGHTNING_PATH_MAX_POINTS);
  if (n < 2)
    return false;

  world_clear_magma_vent_columns(world);

  for (int i = 0; i < n; i++)
  {
    const int x = path[i].x;
    const int y = path[i].y;
    const int z = path[i].z;
    if (!world_is_position_valid(world, x, y, z))
      continue;
    if (z <= 0)
      continue; // never punch bedrock
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (!v || v->type == VOXEL_BEDROCK)
      continue;
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_MAGMA);
    fluid_sim_touch(world, x, y, z);
    world_register_magma_vent_column(world, x, y);
  }

  // Crater: basalt rim around the surface exit with a magma pool in the middle.
  for (int dy = -2; dy <= 2; dy++)
  {
    for (int dx = -2; dx <= 2; dx++)
    {
      const int x = root_x + dx;
      const int y = root_y + dy;
      if (!world_is_position_valid(world, x, y, z1))
        continue;
      const int r2 = dx * dx + dy * dy;
      if (r2 > 4)
        continue;
      world_register_magma_vent_column(world, x, y);
      if (r2 == 0)
      {
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z1, VOXEL_MAGMA);
        fluid_sim_touch(world, x, y, z1);
        if (z1 + 1 < (int)world->depth)
          world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(z1 + 1), VOXEL_AIR);
      }
      else if (r2 >= 2)
      {
        Voxel *rim = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z1);
        if (rim && rim->type != VOXEL_MAGMA && rim->type != VOXEL_BEDROCK)
          world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z1, VOXEL_STONE_BASALT);
      }
    }
  }

  world->volcano_present = true;
  world->volcano_x = (int16_t)root_x;
  world->volcano_y = (int16_t)root_y;
  world->volcano_surface_z = (int16_t)z1;
  world->volcano_path_seed = path_seed;
  world->volcano_pump_cooldown = 0;
  return true;
}

bool volcano_try_pump(World *world, uint32_t tick_salt)
{
  if (!world || !world->volcano_present)
    return false;
  if (world->volcano_pump_cooldown > 0)
  {
    world->volcano_pump_cooldown--;
    return false;
  }

  // Sparse random eruptions — roughly once every many weather ticks.
  const uint32_t h = volcano_hash(tick_salt, (uint32_t)world->volcano_x, world->volcano_path_seed);
  if ((h & 0xffu) > 18u) // ~7% per weather tick while a volcano is present
    return false;

  const int x = world->volcano_x;
  const int y = world->volcano_y;
  int z = world->volcano_surface_z;
  if (z < 1)
    z = 1;
  // Spill a few cells of magma at and above the crater so viscosity can run them downhill.
  static const int dx[5] = {0, 1, -1, 0, 0};
  static const int dy[5] = {0, 0, 0, 1, -1};
  bool spilled = false;
  for (int i = 0; i < 5; i++)
  {
    const int px = x + dx[i];
    const int py = y + dy[i];
    int pz = z + (i == 0 ? 1 : 0);
    if (pz >= (int)world->depth)
      pz = z;
    if (!world_is_position_valid(world, px, py, pz))
      continue;
    Voxel *v = world_get_voxel(world, (uint32_t)px, (uint32_t)py, (uint32_t)pz);
    if (!v || v->type == VOXEL_BEDROCK)
      continue;
    if (v->type != VOXEL_AIR && v->type != VOXEL_MAGMA && v->type != VOXEL_STONE_BASALT &&
        v->type != VOXEL_GRASS && v->type != VOXEL_GRASS_TALL)
      continue;
    world_set_voxel(world, (uint32_t)px, (uint32_t)py, (uint32_t)pz, VOXEL_MAGMA);
    fluid_sim_touch(world, px, py, pz);
    world_register_magma_vent_column(world, px, py);
    spilled = true;
  }
  if (spilled)
    world->volcano_pump_cooldown = 8; // quiet period after an eruption
  return spilled;
}
