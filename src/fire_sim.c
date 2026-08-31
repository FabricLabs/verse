#include "fire_sim.h"

#include "constants.h"

#include <stdlib.h>
#include <string.h>

#define FIRE_SPREAD_CHANCE_LOW 0.04f
#define FIRE_SPREAD_CHANCE_MEDIUM 0.10f
#define FIRE_SPREAD_CHANCE_HIGH 0.18f
#define FIRE_CONSUME_CHANCE_FOLIAGE 0.35f
#define FIRE_CONSUME_CHANCE_GRASS 0.20f
#define FIRE_CONSUME_CHANCE_WOOD 0.008f

static const int FIRE_NEIGHBOURS[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

static uint64_t fire_bit_low(void)
{
  return condition_bit_from_name("BURNING_LOW");
}
static uint64_t fire_bit_medium(void)
{
  return condition_bit_from_name("BURNING_MEDIUM");
}
static uint64_t fire_bit_high(void)
{
  return condition_bit_from_name("BURNING_HIGH");
}
static uint64_t fire_bit_any(void)
{
  return fire_bit_low() | fire_bit_medium() | fire_bit_high();
}
static uint64_t fire_bit_wet(void)
{
  return condition_bit_from_name("WET");
}

bool voxel_type_is_flammable(VoxelType type)
{
  if (type >= VOXEL_WOOD && type <= VOXEL_WOOD_REDWOOD)
    return true;
  if (type >= VOXEL_LEAVES && type <= VOXEL_LEAVES_REDWOOD)
    return true;
  if (type >= VOXEL_BUSH && type <= VOXEL_BUSH_STRAWBERRY)
    return true;
  switch (type)
  {
  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
  case VOXEL_GRASS_TALL:
  case VOXEL_ORE_COAL:
  case VOXEL_WOOL:
  case VOXEL_WOOL_WHITE:
  case VOXEL_WOOL_BLACK:
  case VOXEL_WOOL_BROWN:
  case VOXEL_WOOL_GRAY:
  case VOXEL_WOOL_RED:
  case VOXEL_WOOL_BLUE:
  case VOXEL_WOOL_GREEN:
  case VOXEL_WOOL_YELLOW:
  case VOXEL_PLANK:
  case VOXEL_THATCH:
  case VOXEL_THATCH_MIRROR:
  case VOXEL_DOOR:
  case VOXEL_DOOR_NS:
  case VOXEL_CRATE:
  case VOXEL_BARREL:
  case VOXEL_BED:
  case VOXEL_STRAW:
  case VOXEL_PAPER:
  case VOXEL_ROPE:
  case VOXEL_FEATHER:
  case VOXEL_FUR:
  case VOXEL_LEATHER:
  case VOXEL_WAX:
  case VOXEL_CLOTH:
  case VOXEL_CANDLE:
  case VOXEL_CAMPFIRE:
    return true;
  default:
    return false;
  }
}

bool fire_voxel_is_burning(const Voxel *voxel)
{
  if (!voxel)
    return false;
  return (voxel->condition_mask & fire_bit_any()) != 0ULL;
}

bool fire_voxel_is_wet(const Voxel *voxel)
{
  if (!voxel)
    return false;
  if (voxel->type == VOXEL_WATER || voxel->type == VOXEL_ICE)
    return true;
  return (voxel->condition_mask & fire_bit_wet()) != 0ULL;
}

bool fire_can_ignite_at(const World *world, int x, int y, int z)
{
  if (!world || !world_pos_in_bounds_fast(world, x, y, z))
    return false;
  const Voxel *v = world_voxel_cptr_fast(world, x, y, z);
  if (!v || !voxel_type_is_flammable(v->type))
    return false;
  if (fire_voxel_is_wet(v) || fire_voxel_is_burning(v))
    return false;
  return true;
}

bool fire_ignite_at(World *world, int x, int y, int z, const char *intensity)
{
  if (!world || !intensity)
    return false;
  if (!world_is_position_valid(world, (uint32_t)x, (uint32_t)y, (uint32_t)z))
    return false;

  Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  if (!v)
    return false;

  // Candles and campfires may be re-lit even when already listed as fixtures; other materials
  // need to be dry and flammable.
  if (v->type != VOXEL_CANDLE && v->type != VOXEL_CAMPFIRE)
  {
    if (!voxel_type_is_flammable(v->type) || fire_voxel_is_wet(v))
      return false;
  }
  else if (fire_voxel_is_wet(v))
  {
    return false; // soaked wick will not take
  }

  // Replace any existing intensity with the requested one so a fireball upgrades a smoulder.
  world_remove_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, "BURNING_LOW");
  world_remove_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, "BURNING_MEDIUM");
  world_remove_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, "BURNING_HIGH");
  world_remove_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, "WET");
  return world_add_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, intensity);
}

bool fire_extinguish_at(World *world, int x, int y, int z)
{
  if (!world || !world_is_position_valid(world, (uint32_t)x, (uint32_t)y, (uint32_t)z))
    return false;
  Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  if (!v || !fire_voxel_is_burning(v))
    return false;

  const bool a = world_remove_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, "BURNING_LOW");
  const bool b = world_remove_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, "BURNING_MEDIUM");
  const bool c = world_remove_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, "BURNING_HIGH");
  return a || b || c;
}

static bool fire_neighbour_is_water(const World *world, int x, int y, int z)
{
  for (int i = 0; i < 6; i++)
  {
    const int nx = x + FIRE_NEIGHBOURS[i][0];
    const int ny = y + FIRE_NEIGHBOURS[i][1];
    const int nz = z + FIRE_NEIGHBOURS[i][2];
    if (!world_pos_in_bounds_fast(world, nx, ny, nz))
      continue;
    const Voxel *n = world_voxel_cptr_fast(world, nx, ny, nz);
    if (n && n->type == VOXEL_WATER)
      return true;
  }
  return false;
}

bool fire_try_extinguish_with_water(World *world, int x, int y, int z)
{
  if (!world || !world_is_position_valid(world, (uint32_t)x, (uint32_t)y, (uint32_t)z))
    return false;
  Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  if (!v || !fire_voxel_is_burning(v))
    return false;

  if (v->type == VOXEL_WATER || fire_voxel_is_wet(v) || fire_neighbour_is_water(world, x, y, z))
  {
    // Mark soaked so it cannot re-ignite on the same tick from a still-burning neighbour.
    world_add_voxel_condition(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, "WET");
    return fire_extinguish_at(world, x, y, z);
  }
  return false;
}

static float fire_spread_chance(const Voxel *v)
{
  if (!v)
    return 0.0f;
  if (v->condition_mask & fire_bit_high())
    return FIRE_SPREAD_CHANCE_HIGH;
  if (v->condition_mask & fire_bit_medium())
    return FIRE_SPREAD_CHANCE_MEDIUM;
  if (v->condition_mask & fire_bit_low())
    return FIRE_SPREAD_CHANCE_LOW;
  return 0.0f;
}

static float fire_rng01(uint32_t *state)
{
  *state = (*state * 1103515245u) + 12345u;
  return (float)(*state & 0xFFFFu) / 65535.0f;
}

static void fire_index_to_xyz(const World *world, uint32_t index, int *x, int *y, int *z)
{
  const uint32_t w = world->width;
  const uint32_t h = world->height;
  const uint32_t layer = w * h;
  *z = (int)(index / layer);
  const uint32_t rem = index % layer;
  *y = (int)(rem / w);
  *x = (int)(rem % w);
}

static void fire_consume_fuel(World *world, int x, int y, int z, Voxel *v, uint32_t *rng)
{
  if (!v)
    return;

  // Fixtures smoulder without being eaten by the fire step.
  if (v->type == VOXEL_CANDLE || v->type == VOXEL_CAMPFIRE)
    return;

  float chance = 0.0f;
  if (world_voxel_type_is_foliage(v->type) ||
      (v->type >= VOXEL_BUSH && v->type <= VOXEL_BUSH_STRAWBERRY))
    chance = FIRE_CONSUME_CHANCE_FOLIAGE;
  else if (v->type == VOXEL_GRASS || v->type == VOXEL_GRASS_WIDE || v->type == VOXEL_GRASS_SHARP ||
           v->type == VOXEL_GRASS_CLOVER || v->type == VOXEL_GRASS_MOSS ||
           v->type == VOXEL_GRASS_TALL)
    chance = FIRE_CONSUME_CHANCE_GRASS;
  else if (v->type >= VOXEL_WOOD && v->type <= VOXEL_WOOD_REDWOOD)
    chance = FIRE_CONSUME_CHANCE_WOOD;
  else if (v->type == VOXEL_ORE_COAL)
    chance = FIRE_CONSUME_CHANCE_WOOD * 0.5f;

  if (chance <= 0.0f || fire_rng01(rng) >= chance)
    return;

  // Low-grass burns off as a coat and leaves the soil host; tall grass and other fuels go to air.
  const VoxelType left = voxel_type_after_break(v->type);
  world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, left);
  // Conditions die with the voxel content for gameplay; clear explicitly for the index.
  world_clear_voxel_conditions(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
}

void fire_sim_step(World *world, float dt_seconds)
{
  if (!world || !world->voxels || dt_seconds <= 0.0f)
    return;

  if (!world_condition_index_is_fresh(world))
    world_refresh_condition_index(world);
  if (!world_condition_index_is_fresh(world))
    return;

  // Snapshot burning cells first: ignition writes bump the condition index mid-step.
  const uint64_t bit_vals[3] = {fire_bit_low(), fire_bit_medium(), fire_bit_high()};
  int bits[3];
  for (int b = 0; b < 3; b++)
    bits[b] = bit_vals[b] ? (int)__builtin_ctzll(bit_vals[b]) : -1;

  uint32_t *burning = NULL;
  size_t burning_count = 0;
  size_t burning_cap = 0;

  for (int b = 0; b < 3; b++)
  {
    const int bit = bits[b];
    if (bit < 0 || bit >= 64)
      continue;
    const struct VoxelIndexList *list = &world->condition_voxel_indices[bit];
    for (size_t i = 0; i < list->size; i++)
    {
      if (burning_count >= burning_cap)
      {
        const size_t next = burning_cap ? burning_cap * 2u : 64u;
        uint32_t *grown = (uint32_t *)realloc(burning, next * sizeof(uint32_t));
        if (!grown)
        {
          free(burning);
          return;
        }
        burning = grown;
        burning_cap = next;
      }
      burning[burning_count++] = list->indices[i];
    }
  }

  uint32_t rng = world->rng_state ^ 0xF1A7E11Eu ^ (uint32_t)burning_count;

  // Deduplicate indices that carry more than one burning bit (should not happen, but cheap).
  for (size_t i = 0; i < burning_count; i++)
  {
    for (size_t j = i + 1; j < burning_count; j++)
    {
      if (burning[j] == burning[i])
      {
        burning[j] = burning[burning_count - 1];
        burning_count--;
        j--;
      }
    }
  }

  for (size_t i = 0; i < burning_count; i++)
  {
    int x = 0, y = 0, z = 0;
    fire_index_to_xyz(world, burning[i], &x, &y, &z);
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (!v || !fire_voxel_is_burning(v))
      continue;

    if (fire_try_extinguish_with_water(world, x, y, z))
      continue;

    const float chance = fire_spread_chance(v);
    if (chance > 0.0f)
    {
      for (int n = 0; n < 6; n++)
      {
        const int nx = x + FIRE_NEIGHBOURS[n][0];
        const int ny = y + FIRE_NEIGHBOURS[n][1];
        const int nz = z + FIRE_NEIGHBOURS[n][2];
        if (!fire_can_ignite_at(world, nx, ny, nz))
          continue;
        if (fire_rng01(&rng) >= chance)
          continue;
        // Spread as a lower intensity than the source, so a campfire does not instantly torch
        // an entire meadow in one tick.
        const char *level = "BURNING_LOW";
        if (v->condition_mask & fire_bit_high())
          level = "BURNING_MEDIUM";
        else if (v->condition_mask & fire_bit_medium())
          level = "BURNING_LOW";
        fire_ignite_at(world, nx, ny, nz, level);
      }
    }

    fire_consume_fuel(world, x, y, z, v, &rng);
  }

  world->rng_state = rng;
  free(burning);
}
