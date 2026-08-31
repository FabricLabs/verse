#include "water_erosion.h"

#include "fluid_sim.h"
#include "universe_biome.h"
#include "voxel.h"

#include <stdlib.h>
#include <string.h>

// Reference trickle: continuous lateral exchange of this many levels per tick is "full flow".
#define WATER_EROSION_FLOW_REF 8

// Softest / hardest durability anchors used to lerp carve time (mirrors voxel_combat ranges).
#define WATER_EROSION_DUR_SOFT 12
#define WATER_EROSION_DUR_HARD 252

// Gen rivulets: sparse downhill traces on slopes, one-voxel-deep channels, water only where
// runoff pools. No blanket storm abrasion — that chewed the surface into jagged pits.
#define WATER_EROSION_RIVULET_MAX_LEN 48
#define WATER_EROSION_RIVULET_WATER_LEVEL 80
#define WATER_EROSION_RIVULET_SETTLE_STEPS 120

static int g_erosion_rate_scale = 1;

void water_erosion_set_rate_scale(int scale)
{
  // 0 disables transfer abrasion (used while gen rivulets settle without further carving).
  g_erosion_rate_scale = scale < 0 ? 0 : scale;
}

int water_erosion_rate_scale(void)
{
  return g_erosion_rate_scale;
}

// Durability proxy for carve-time mapping. Bedrock and fluids return 0 (never abrade).
static uint8_t erosion_durability(VoxelType type)
{
  switch (type)
  {
  case VOXEL_AIR:
  case VOXEL_BEDROCK:
  case VOXEL_WATER:
  case VOXEL_MAGMA:
  case VOXEL_STEAM:
  case VOXEL_SPRING:
  case VOXEL_SPRING_WATER:
  case VOXEL_SPRING_MAGMA:
  case VOXEL_SPRING_STEAM:
  case VOXEL_SPRING_OIL:
  case VOXEL_SPRING_GAS:
  case VOXEL_OIL:
  case VOXEL_GAS:
  case VOXEL_ICE:
    return 0;

  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
  case VOXEL_GRASS_TALL:
    return 12;

  case VOXEL_SOIL:
  case VOXEL_SOIL_CLAY:
  case VOXEL_SOIL_LOAM:
  case VOXEL_SOIL_SILT:
  case VOXEL_SAND:
  case VOXEL_SAND_BASALT:
  case VOXEL_SAND_GRANITE:
  case VOXEL_SAND_LIMESTONE:
  case VOXEL_SAND_SANDSTONE:
    return 72;

  case VOXEL_STONE_SANDSTONE:
  case VOXEL_GRAVEL:
  case VOXEL_GRAVEL_BASALT:
  case VOXEL_GRAVEL_GRANITE:
  case VOXEL_GRAVEL_LIMESTONE:
  case VOXEL_GRAVEL_SANDSTONE:
    return 108;

  case VOXEL_STONE:
  case VOXEL_STONE_LIMESTONE:
    return 180;

  case VOXEL_STONE_GRANITE:
  case VOXEL_STONE_BASALT:
    return 252;

  default:
    return 144;
  }
}

static int carve_ticks_for_durability(uint8_t dur)
{
  const int min_ticks = WATER_EROSION_MIN_MS / WATER_EROSION_TICK_MS;
  const int max_ticks = WATER_EROSION_MAX_MS / WATER_EROSION_TICK_MS;
  if (dur <= WATER_EROSION_DUR_SOFT)
    return min_ticks;
  if (dur >= WATER_EROSION_DUR_HARD)
    return max_ticks;
  const int span = WATER_EROSION_DUR_HARD - WATER_EROSION_DUR_SOFT;
  const int t_span = max_ticks - min_ticks;
  return min_ticks + (t_span * (int)(dur - WATER_EROSION_DUR_SOFT)) / span;
}

// One wear step softer / toward channel bed. AIR means the voxel is gone.
static VoxelType erosion_residue(VoxelType type)
{
  switch (type)
  {
  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
  case VOXEL_GRASS_TALL:
  case VOXEL_BUSH:
  case VOXEL_BUSH_FERN:
  case VOXEL_BUSH_VINES:
  case VOXEL_BUSH_THORNS:
  case VOXEL_BUSH_BLUEBERRY:
  case VOXEL_BUSH_BLACKBERRY:
  case VOXEL_BUSH_RASPBERRY:
  case VOXEL_BUSH_STRAWBERRY:
    return VOXEL_AIR;

  case VOXEL_SOIL:
  case VOXEL_SOIL_CLAY:
  case VOXEL_SOIL_LOAM:
  case VOXEL_SOIL_SILT:
    return VOXEL_SAND;

  case VOXEL_SAND:
  case VOXEL_SAND_BASALT:
  case VOXEL_SAND_GRANITE:
  case VOXEL_SAND_LIMESTONE:
  case VOXEL_SAND_SANDSTONE:
    return VOXEL_AIR;

  case VOXEL_STONE_SANDSTONE:
    return VOXEL_GRAVEL_SANDSTONE;
  case VOXEL_STONE_BASALT:
    return VOXEL_GRAVEL_BASALT;
  case VOXEL_STONE_GRANITE:
    return VOXEL_GRAVEL_GRANITE;
  case VOXEL_STONE_LIMESTONE:
    return VOXEL_GRAVEL_LIMESTONE;
  case VOXEL_STONE:
    return VOXEL_GRAVEL;

  case VOXEL_GRAVEL:
  case VOXEL_GRAVEL_BASALT:
  case VOXEL_GRAVEL_GRANITE:
  case VOXEL_GRAVEL_LIMESTONE:
  case VOXEL_GRAVEL_SANDSTONE:
    return VOXEL_SAND;

  default:
    return VOXEL_AIR;
  }
}

static uint32_t erosion_hash(uint32_t x, uint32_t y, uint32_t z, uint64_t step)
{
  uint32_t h = x * 374761393u + y * 668265263u + z * 362437u;
  h ^= (uint32_t)step * 2246822519u;
  h ^= h >> 13;
  h *= 1274126177u;
  return h ^ (h >> 16);
}

bool water_erosion_abrade(World *world, int x, int y, int z, int flow_amount,
                          uint64_t step_index)
{
  if (!world || !world->voxels || flow_amount <= 0 || g_erosion_rate_scale <= 0)
    return false;
  if (!world_pos_in_bounds_fast(world, x, y, z))
    return false;

  Voxel *v = world_voxel_ptr_fast(world, x, y, z);
  const uint8_t dur = erosion_durability(v->type);
  if (dur == 0)
    return false;

  const int carve_ticks = carve_ticks_for_durability(dur);
  const int scale = g_erosion_rate_scale;
  int flow = flow_amount;
  if (flow > 64)
    flow = 64;

  const uint32_t numer = (uint32_t)flow * (uint32_t)scale * 255u;
  const uint32_t denom = (uint32_t)carve_ticks * (uint32_t)WATER_EROSION_FLOW_REF;
  if (denom == 0u || numer == 0u)
    return false;
  uint32_t add = numer / denom;
  if (add == 0u)
  {
    const uint32_t phase = erosion_hash((uint32_t)x, (uint32_t)y, (uint32_t)z, step_index) % denom;
    if (phase >= numer)
      return false;
    add = 1u;
  }

  const uint32_t cur = voxel_get_damage(v);
  const uint32_t next = cur + add;
  if (next < 255u)
  {
    voxel_set_damage(v, (uint8_t)next);
    return false;
  }

  const VoxelType next_type = erosion_residue(v->type);
  if (next_type == VOXEL_AIR)
  {
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_AIR);
  }
  else
  {
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, next_type);
    Voxel *worn = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (worn)
      voxel_set_damage(worn, 0);
  }
  return true;
}

void water_erosion_on_water_transfer(World *world, int ax, int ay, int az, int bx, int by, int bz,
                                     int flow_amount, uint64_t step_index)
{
  (void)ax;
  (void)ay;
  (void)az;
  if (!world || flow_amount < 4)
    return;

  // Only scour the bed under the destination. Abrading both ends of every lateral hop dug
  // double-wide, jagged trenches; a single bed cell keeps channels rivulet-thin.
  if (bz > 0)
    water_erosion_abrade(world, bx, by, bz - 1, flow_amount, step_index);
}

// ---------------------------------------------------------------------------
// Wilderness rivulets — shallow downhill traces on mountain slopes

static uint32_t rivulet_hash(uint32_t x, uint32_t y, uint32_t salt)
{
  uint32_t h = x * 374761393u ^ y * 668265263u ^ salt;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

static int column_surface_z(const World *world, uint32_t x, uint32_t y)
{
  if (!world || !world->voxels)
    return -1;
  for (int z = (int)world->depth - 1; z >= 0; z--)
  {
    const Voxel *v = world_voxel_cptr_fast(world, (int)x, (int)y, z);
    if (!v || v->type == VOXEL_AIR)
      continue;
    // Skip standing water so tops track the solid bed under a rivulet.
    if (v->type == VOXEL_WATER)
      continue;
    return z;
  }
  return -1;
}

static void weather_refresh_tops(World *world, int *tops)
{
  if (!world || !tops)
    return;
  const uint32_t W = world->width;
  const uint32_t H = world->height;
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
      tops[(size_t)y * W + x] = column_surface_z(world, x, y);
}

// Largest drop to a 4-neighbour. Flat ground returns 0; a slope that can host a rivulet is >= 1.
static int column_downhill_relief(const int *tops, uint32_t W, uint32_t H, uint32_t x, uint32_t y)
{
  const int here = tops[(size_t)y * W + x];
  if (here < 0)
    return 0;
  int best = 0;
  static const int dx[4] = {1, -1, 0, 0};
  static const int dy[4] = {0, 0, 1, -1};
  for (int i = 0; i < 4; i++)
  {
    const int nx = (int)x + dx[i];
    const int ny = (int)y + dy[i];
    if (nx < 0 || ny < 0 || nx >= (int)W || ny >= (int)H)
      continue;
    const int n = tops[(size_t)ny * W + (size_t)nx];
    if (n < 0)
      continue;
    const int drop = here - n;
    if (drop > best)
      best = drop;
  }
  return best;
}

// Steepest 4-neighbour descent. Returns false at a local sink (no strictly lower neighbour).
static bool steepest_descent(const int *tops, uint32_t W, uint32_t H, int x, int y, int *ox,
                             int *oy)
{
  const int here = tops[(size_t)y * W + (size_t)x];
  if (here < 0)
    return false;
  int best_drop = 0;
  int bx = x, by = y;
  static const int dx[4] = {1, -1, 0, 0};
  static const int dy[4] = {0, 0, 1, -1};
  for (int i = 0; i < 4; i++)
  {
    const int nx = x + dx[i];
    const int ny = y + dy[i];
    if (nx < 0 || ny < 0 || nx >= (int)W || ny >= (int)H)
      continue;
    const int n = tops[(size_t)ny * W + (size_t)nx];
    if (n < 0)
      continue;
    const int drop = here - n;
    if (drop > best_drop)
    {
      best_drop = drop;
      bx = nx;
      by = ny;
    }
  }
  if (best_drop <= 0)
    return false;
  *ox = bx;
  *oy = by;
  return true;
}

// Remove at most one surface voxel — rivulets are shallow grooves, not canyons.
static bool carve_rivulet_cell(World *world, int *tops, uint8_t *carved, uint32_t x, uint32_t y)
{
  const uint32_t W = world->width;
  const size_t idx = (size_t)y * W + x;
  if (carved[idx])
    return false;

  int top = tops[idx];
  if (top <= 0)
    return false;

  Voxel *v = world_get_voxel(world, x, y, (uint32_t)top);
  if (!v || v->type == VOXEL_BEDROCK || v->type == VOXEL_WATER || v->type == VOXEL_AIR)
    return false;
  if (erosion_durability(v->type) == 0)
    return false;

  world_set_voxel(world, x, y, (uint32_t)top, VOXEL_AIR);
  carved[idx] = 1;
  tops[idx] = top - 1;
  return true;
}

static void place_rivulet_water(World *world, int *tops, uint32_t x, uint32_t y)
{
  int top = tops[(size_t)y * world->width + x];
  int place_z = top + 1;
  if (place_z < 1 || place_z >= (int)world->depth)
    return;

  Voxel *cell = world_get_voxel(world, x, y, (uint32_t)place_z);
  if (!cell || (cell->type != VOXEL_AIR && cell->type != VOXEL_WATER))
    return;

  world_set_voxel(world, x, y, (uint32_t)place_z, VOXEL_WATER);
  Voxel *w = world_get_voxel(world, x, y, (uint32_t)place_z);
  if (w)
    voxel_set_quantity(w, (uint8_t)WATER_EROSION_RIVULET_WATER_LEVEL);
  fluid_sim_touch(world, (int)x, (int)y, place_z);
}

static void weather_settle_no_erosion(World *world)
{
  const int prev = g_erosion_rate_scale;
  water_erosion_set_rate_scale(0);
  for (int i = 0; i < WATER_EROSION_RIVULET_SETTLE_STEPS; i++)
  {
    const FluidStepStats stats = fluid_sim_step(world, 1 << 20);
    if (stats.at_rest)
      break;
  }
  water_erosion_set_rate_scale(prev);
}

void water_erosion_simulate_weather(World *world, uint32_t salt, int *tops)
{
  if (!world || !world->voxels || !tops)
    return;
  if (world->width == 0 || world->height == 0 || world->depth < 2)
    return;

  const uint32_t W = world->width;
  const uint32_t H = world->height;
  uint8_t *carved = (uint8_t *)calloc((size_t)W * H, 1);
  if (!carved)
    return;

  // Trace a few downhill grooves from elevated slopes. Prefer alpine / highland climate so
  // lowland meadows stay intact and only mountain faces pick up rivulets.
  for (uint32_t y = 1; y + 1 < H; y++)
  {
    for (uint32_t x = 1; x + 1 < W; x++)
    {
      const int top = tops[(size_t)y * W + x];
      if (top < 2)
        continue;

      const int relief = column_downhill_relief(tops, W, H, x, y);
      if (relief < 1)
        continue;

      float elev = (world->depth > 1) ? (float)top / (float)(world->depth - 1) : 0.5f;
      // Hills and mountains only — skip broad flats and low plains.
      if (elev < 0.32f && relief < 2)
        continue;

      UniverseClimate climate = universe_climate_sample(world, x, y, elev);
      UniverseBiomeSample biome = universe_biome_classify(&climate);

      float chance = 0.012f + 0.04f * climate.elevation + 0.05f * biome.weights[UNIVERSE_BIOME_ALPINE];
      if (relief >= 2)
        chance += 0.025f;
      if (biome.primary == UNIVERSE_BIOME_DESERT)
        chance *= 0.15f;
      if (biome.primary == UNIVERSE_BIOME_WETLAND)
        chance *= 0.35f; // wetlands get ponds elsewhere; keep mountain focus here

      const uint32_t h = rivulet_hash(x, y, salt ^ 0x51eed01eu);
      if ((float)(h & 0xffffu) / 65535.0f > chance)
        continue;

      // Trace the whole downhill path first, then carve — mutating tops mid-walk made channels
      // jitter sideways into jagged trenches.
      int path_x[WATER_EROSION_RIVULET_MAX_LEN];
      int path_y[WATER_EROSION_RIVULET_MAX_LEN];
      int path_len = 0;
      int cx = (int)x, cy = (int)y;
      for (int step = 0; step < WATER_EROSION_RIVULET_MAX_LEN; step++)
      {
        path_x[path_len] = cx;
        path_y[path_len] = cy;
        path_len++;
        int nx = cx, ny = cy;
        if (!steepest_descent(tops, W, H, cx, cy, &nx, &ny))
          break;
        cx = nx;
        cy = ny;
      }

      if (path_len < 3)
        continue;

      for (int i = 0; i < path_len; i++)
        carve_rivulet_cell(world, tops, carved, (uint32_t)path_x[i], (uint32_t)path_y[i]);

      // Water collects at the runout / local sink.
      place_rivulet_water(world, tops, (uint32_t)path_x[path_len - 1],
                          (uint32_t)path_y[path_len - 1]);
      if (path_len >= 8)
        place_rivulet_water(world, tops, (uint32_t)path_x[path_len / 2],
                            (uint32_t)path_y[path_len / 2]);
    }
  }

  // Let seeded water settle into the carved grooves without further bed abrasion.
  weather_settle_no_erosion(world);
  weather_refresh_tops(world, tops);

  free(carved);
}
