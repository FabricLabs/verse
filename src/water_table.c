#include "water_table.h"

#include "universe_biome.h"
#include "voxel.h"

#include <stdlib.h>
#include <string.h>

// Gen diffusion budget: enough for aquifers to spread a few cells from rivulets / pockets without
// dominating wilderness generation time on a 128³ volume.
#define WATER_TABLE_DIFFUSE_ITERS 10

// Climate moisture below this leaves only local seepage around free water (arid basins).
#define WATER_TABLE_MOISTURE_FLOOR 0.12f

uint8_t water_table_permeability(VoxelType type)
{
  switch (type)
  {
  case VOXEL_AIR:
  case VOXEL_BEDROCK:
  case VOXEL_WATER:
  case VOXEL_MAGMA:
  case VOXEL_STEAM:
  case VOXEL_ICE:
  case VOXEL_OIL:
  case VOXEL_GAS:
  case VOXEL_SPRING:
  case VOXEL_SPRING_WATER:
  case VOXEL_SPRING_MAGMA:
  case VOXEL_SPRING_STEAM:
  case VOXEL_SPRING_OIL:
  case VOXEL_SPRING_GAS:
    return 0;

  // Sand: high conductivity — classic aquifer host.
  case VOXEL_SAND:
  case VOXEL_SAND_BASALT:
  case VOXEL_SAND_GRANITE:
  case VOXEL_SAND_LIMESTONE:
  case VOXEL_SAND_SANDSTONE:
    return 8;

  case VOXEL_SOIL_SILT:
    return 7;

  case VOXEL_SOIL:
  case VOXEL_SOIL_LOAM:
    return 6;

  case VOXEL_STONE_SANDSTONE:
  case VOXEL_GRAVEL:
  case VOXEL_GRAVEL_BASALT:
  case VOXEL_GRAVEL_GRANITE:
  case VOXEL_GRAVEL_LIMESTONE:
  case VOXEL_GRAVEL_SANDSTONE:
    return 5;

  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
  case VOXEL_GRASS_TALL:
    return 4;

  case VOXEL_STONE:
  case VOXEL_STONE_LIMESTONE:
    return 3;

  // Clay aquitard: slows vertical recharge.
  case VOXEL_SOIL_CLAY:
    return 2;

  // Dense igneous rock: barely permeable — wetness only at contacts.
  case VOXEL_STONE_GRANITE:
  case VOXEL_STONE_BASALT:
    return 1;

  default:
    return 0;
  }
}

bool water_table_is_permeable(VoxelType type)
{
  return water_table_permeability(type) > 0;
}

bool water_table_wet(Voxel *v, uint8_t target)
{
  if (!v || !water_table_is_permeable(v->type))
    return false;
  if (target > WATER_TABLE_WETNESS_MAX)
    target = WATER_TABLE_WETNESS_MAX;
  const uint8_t cur = voxel_get_quantity(v);
  // Only solids use quantity as wetness; never raise past the visual max used by the renderer.
  if (cur >= target || cur > WATER_TABLE_WETNESS_MAX)
    return false;
  voxel_set_quantity(v, target);
  return true;
}

static inline bool is_free_water(VoxelType type)
{
  return type == VOXEL_WATER || type == VOXEL_SPRING_WATER || type == VOXEL_SPRING;
}

// Neighbour wetness contribution: free water counts as fully saturated; solids contribute their
// stored wetness. Impermeable cells contribute nothing.
static uint8_t neighbour_source_wetness(const Voxel *v)
{
  if (!v)
    return 0;
  if (is_free_water(v->type))
    return WATER_TABLE_WETNESS_MAX;
  if (!water_table_is_permeable(v->type))
    return 0;
  const uint8_t w = voxel_get_quantity(v);
  return w <= WATER_TABLE_WETNESS_MAX ? w : 0;
}

// How much wetness is lost when crossing into a cell of this permeability. High-perm sand barely
// attenuates; clay and granite soak up the gradient quickly.
static uint8_t attenuation_for_perm(uint8_t perm)
{
  if (perm >= 7)
    return 0;
  if (perm >= 5)
    return 1;
  if (perm >= 3)
    return 2;
  if (perm >= 2)
    return 3;
  return 4; // granite / basalt
}

static void wet_cell_from_sources(World *world, int x, int y, int z)
{
  if (x < 0 || y < 0 || z < 0)
    return;
  if (!world_is_position_valid(world, (uint32_t)x, (uint32_t)y, (uint32_t)z))
    return;
  Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  if (!v || !water_table_is_permeable(v->type))
    return;

  const uint8_t perm = water_table_permeability(v->type);
  const uint8_t atten = attenuation_for_perm(perm);
  uint8_t best = neighbour_source_wetness(v);

  static const int dx[6] = {1, -1, 0, 0, 0, 0};
  static const int dy[6] = {0, 0, 1, -1, 0, 0};
  static const int dz[6] = {0, 0, 0, 0, 1, -1};
  for (int i = 0; i < 6; i++)
  {
    const int nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
    if (nx < 0 || ny < 0 || nz < 0)
      continue;
    if (!world_is_position_valid(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz))
      continue;
    Voxel *n = world_get_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz);
    uint8_t src = neighbour_source_wetness(n);
    if (src == 0)
      continue;
    // Prefer downward recharge: water above a cell charges it harder than lateral neighbours.
    uint8_t loss = atten;
    if (dz[i] == 1)
      loss = atten > 0 ? (uint8_t)(atten - 1) : 0; // neighbour is above → gravity feed
    else if (dz[i] == -1)
      loss = (uint8_t)(atten + 1); // climbing against the table
    uint8_t offered = src > loss ? (uint8_t)(src - loss) : 0;
    if (offered > best)
      best = offered;
  }

  if (best > 0)
    water_table_wet(v, best);
}

// Hydrostatic fill: saturate permeable solids from the floor up to a climate- and water-driven
// table height. Lateral diffusion then connects columns under ridges.
static void fill_column_table(World *world, const int *tops, uint32_t x, uint32_t y)
{
  const uint32_t W = world->width;
  const uint32_t D = world->depth;
  const int top = tops ? tops[(size_t)y * W + x] : -1;
  if (top < 1)
    return;

  int water_z = -1;
  for (uint32_t z = 1; z < D; z++)
  {
    Voxel *v = world_get_voxel(world, x, y, z);
    if (v && is_free_water(v->type))
      water_z = (int)z;
  }

  float elev = (D > 1) ? (float)top / (float)(D - 1) : 0.5f;
  UniverseClimate climate = universe_climate_sample(world, x, y, elev);
  UniverseBiomeSample biome = universe_biome_classify(&climate);

  float moisture = climate.moisture;
  moisture += 0.35f * biome.weights[UNIVERSE_BIOME_WETLAND];
  moisture += 0.15f * biome.weights[UNIVERSE_BIOME_TROPICAL];
  moisture -= 0.45f * biome.weights[UNIVERSE_BIOME_DESERT];
  moisture -= 0.20f * biome.weights[UNIVERSE_BIOME_ALPINE];
  if (moisture < 0.0f)
    moisture = 0.0f;
  if (moisture > 1.0f)
    moisture = 1.0f;

  // Table sits a moisture-driven fraction of the way up the column. Wetlands push close to the
  // surface; deserts keep groundwater deep or absent unless free water is already present.
  int table_z = 0;
  if (moisture >= WATER_TABLE_MOISTURE_FLOOR)
    table_z = 1 + (int)((float)(top - 1) * (0.25f + 0.55f * moisture));
  if (water_z >= 0 && water_z > table_z)
    table_z = water_z;
  if (table_z >= top)
    table_z = top; // allow wetting the surface coat
  if (table_z < 1)
    return;

  for (int z = 1; z <= table_z; z++)
  {
    Voxel *v = world_get_voxel(world, x, y, (uint32_t)z);
    if (!v || !water_table_is_permeable(v->type))
      continue;
    // Climate-driven tables live in aquifers (sand, soil, sandstone). Dense igneous rock is
    // only dampened by direct contact diffusion later — not by a regional moisture fill.
    const uint8_t perm = water_table_permeability(v->type);
    if (perm <= 1)
      continue;
    // Full saturation deep below the table; taper in the last couple of cells so the capillary
    // fringe reads as damp rather than flooded.
    int below = table_z - z;
    uint8_t wet = WATER_TABLE_WETNESS_MAX;
    if (below == 0)
      wet = 3;
    else if (below == 1)
      wet = 5;
    // Clay under a high table still saturates, but stay a notch drier so aquitards read denser.
    if (perm <= 2 && wet > 4)
      wet = 4;
    water_table_wet(v, wet);
  }
}

void water_table_build(World *world, const int *tops, uint32_t salt)
{
  (void)salt;
  if (!world || !world->voxels)
    return;
  if (world->width == 0 || world->height == 0 || world->depth < 2)
    return;

  const uint32_t W = world->width;
  const uint32_t H = world->height;
  const uint32_t D = world->depth;

  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
      fill_column_table(world, tops, x, y);

  // Diffuse from free water and already-saturated cells so aquifers connect laterally through
  // sand / sandstone under impermeable caps.
  for (int iter = 0; iter < WATER_TABLE_DIFFUSE_ITERS; iter++)
  {
    for (uint32_t z = 1; z < D; z++)
      for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
          wet_cell_from_sources(world, (int)x, (int)y, (int)z);
  }
}

void water_table_seep_from(World *world, int x, int y, int z)
{
  if (!world || x < 0 || y < 0 || z < 0)
    return;
  if (!world_is_position_valid(world, (uint32_t)x, (uint32_t)y, (uint32_t)z))
    return;
  Voxel *src = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  if (!src || !is_free_water(src->type))
    return;

  static const int dx[6] = {0, 1, -1, 0, 0, 0};
  static const int dy[6] = {0, 0, 0, 1, -1, 0};
  static const int dz[6] = {-1, 0, 0, 0, 0, 1}; // bed first, then lateral, then above
  for (int i = 0; i < 6; i++)
  {
    const int nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
    if (nx < 0 || ny < 0 || nz < 0)
      continue;
    if (!world_is_position_valid(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz))
      continue;
    Voxel *n = world_get_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz);
    if (!n || !water_table_is_permeable(n->type))
      continue;
    const uint8_t perm = water_table_permeability(n->type);
    uint8_t target = WATER_TABLE_WETNESS_MAX;
    if (perm <= 2)
      target = 3;
    else if (perm <= 4)
      target = 5;
    // Single-step soak toward target so standing pools charge the bed over many ticks.
    const uint8_t cur = voxel_get_quantity(n);
    if (cur < target && cur <= WATER_TABLE_WETNESS_MAX)
      water_table_wet(n, (uint8_t)(cur + 1));
  }
}

void water_table_on_water_transfer(World *world, int ax, int ay, int az, int bx, int by, int bz,
                                   int flow_amount)
{
  if (!world || flow_amount <= 0)
    return;
  // Charge both ends of the path so a trickle across sand leaves a damp trail.
  water_table_seep_from(world, ax, ay, az);
  water_table_seep_from(world, bx, by, bz);

  // Also wet the solid directly under the receiving cell (classic bed soak).
  if (bz > 0)
  {
    Voxel *bed = world_get_voxel(world, (uint32_t)bx, (uint32_t)by, (uint32_t)(bz - 1));
    if (bed && water_table_is_permeable(bed->type))
    {
      const uint8_t cur = voxel_get_quantity(bed);
      if (cur < WATER_TABLE_WETNESS_MAX)
        water_table_wet(bed, (uint8_t)(cur + 1));
    }
  }
}
