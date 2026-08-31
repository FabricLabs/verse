#include "voxel_combat.h"

#include <math.h>
#include <string.h>

uint8_t voxel_durability(VoxelType type)
{
  switch (type)
  {
  case VOXEL_AIR:
  case VOXEL_BEDROCK:
  case VOXEL_WATER:
  case VOXEL_MAGMA:
  case VOXEL_STEAM:
  case VOXEL_SPRING:
    return 0;

  // Turf coat: one light hit scrapes to bare soil. Tall grass is a soft prop in air.
  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
  case VOXEL_GRASS_TALL:
    return 12;

  case VOXEL_SOIL:
  case VOXEL_SOIL_LOAM:
  case VOXEL_SOIL_SILT:
  case VOXEL_SAND:
  case VOXEL_SAND_BASALT:
  case VOXEL_SAND_GRANITE:
  case VOXEL_SAND_LIMESTONE:
  case VOXEL_SAND_SANDSTONE:
    return 72; // ~4 fireballs

  case VOXEL_STONE_SANDSTONE:
  case VOXEL_GRAVEL:
  case VOXEL_GRAVEL_BASALT:
  case VOXEL_GRAVEL_GRANITE:
  case VOXEL_GRAVEL_LIMESTONE:
  case VOXEL_GRAVEL_SANDSTONE:
    return 108; // 6 fireballs

  case VOXEL_WOOD:
  case VOXEL_WOOD_OAK:
  case VOXEL_WOOD_BEECH:
  case VOXEL_WOOD_BIRCH:
  case VOXEL_WOOD_PINE:
  case VOXEL_WOOD_PECAN:
  case VOXEL_WOOD_LOCUST:
  case VOXEL_WOOD_MAPLE:
  case VOXEL_WOOD_ELM:
  case VOXEL_WOOD_HAZELNUT:
  case VOXEL_WOOD_CHESTNUT:
  case VOXEL_WOOD_WILLOW:
  case VOXEL_WOOD_WALNUT:
  case VOXEL_WOOD_ACACIA:
  case VOXEL_WOOD_COTTONWOOD:
  case VOXEL_WOOD_CYPRESS:
  case VOXEL_WOOD_SPRUCE:
  case VOXEL_WOOD_JUNIPER:
  case VOXEL_WOOD_REDWOOD:
  case VOXEL_BUSH:
  case VOXEL_BUSH_FERN:
  case VOXEL_BUSH_VINES:
  case VOXEL_BUSH_THORNS:
  case VOXEL_BUSH_BLUEBERRY:
  case VOXEL_BUSH_BLACKBERRY:
  case VOXEL_BUSH_RASPBERRY:
  case VOXEL_BUSH_STRAWBERRY:
  case VOXEL_CANDLE:
  case VOXEL_CAMPFIRE:
    return 144; // 8 fireballs

  case VOXEL_STONE:
  case VOXEL_STONE_LIMESTONE:
    return 180; // 10 fireballs

  case VOXEL_STONE_GRANITE:
  case VOXEL_STONE_BASALT:
    return 252; // 14 fireballs

  default:
    return 144;
  }
}

bool voxel_is_destructible(VoxelType type)
{
  return voxel_durability(type) > 0;
}

uint8_t voxel_impulse_damage(VoxelType type, float speed_vox_s)
{
  if (speed_vox_s <= 0.0f || !voxel_is_destructible(type))
    return 0;

  const float mass = voxel_type_mass_kg(type);
  const float ref_mass = voxel_type_mass_kg(VOXEL_STONE);
  if (mass <= 1e-8f || ref_mass <= 1e-8f)
    return 0;

  // Stone at 14 vox/s → 18 HP, matching PLAYER_FIREBALL_DAMAGE. Mass in the denominator so a
  // cottonwood voxel yields more than granite from the same leftover speed.
  const float k = 18.0f * ref_mass / 14.0f;
  float dmg = k * speed_vox_s / mass;
  if (dmg < 1.0f)
    dmg = 1.0f;
  if (dmg > 255.0f)
    dmg = 255.0f;
  return (uint8_t)(dmg + 0.5f);
}

bool voxel_apply_impulse(Voxel *voxel, float speed_vox_s)
{
  if (!voxel)
    return false;
  return voxel_apply_damage(voxel, voxel_impulse_damage(voxel->type, speed_vox_s));
}

ItemId voxel_type_to_drop_item(VoxelType type)
{
  switch (type)
  {
  case VOXEL_STONE:
  case VOXEL_STONE_BASALT:
  case VOXEL_STONE_GRANITE:
  case VOXEL_STONE_LIMESTONE:
  case VOXEL_STONE_SANDSTONE:
  case VOXEL_GRAVEL:
  case VOXEL_GRAVEL_BASALT:
  case VOXEL_GRAVEL_GRANITE:
  case VOXEL_GRAVEL_LIMESTONE:
  case VOXEL_GRAVEL_SANDSTONE:
  case VOXEL_SOIL:
  case VOXEL_SOIL_LOAM:
  case VOXEL_SOIL_SILT:
  case VOXEL_SAND:
  case VOXEL_SAND_BASALT:
  case VOXEL_SAND_GRANITE:
  case VOXEL_SAND_LIMESTONE:
  case VOXEL_SAND_SANDSTONE:
    return ITEM_STONE_BLOCK;

  // Low-grass scrape leaves the soil host in place; no block drop from the coat itself.
  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
    return ITEM_NONE;

  case VOXEL_WOOD:
  case VOXEL_WOOD_OAK:
  case VOXEL_WOOD_BEECH:
  case VOXEL_WOOD_BIRCH:
  case VOXEL_WOOD_PINE:
  case VOXEL_WOOD_PECAN:
  case VOXEL_WOOD_LOCUST:
  case VOXEL_WOOD_MAPLE:
  case VOXEL_WOOD_ELM:
  case VOXEL_WOOD_HAZELNUT:
  case VOXEL_WOOD_CHESTNUT:
  case VOXEL_WOOD_WILLOW:
  case VOXEL_WOOD_WALNUT:
  case VOXEL_WOOD_ACACIA:
  case VOXEL_WOOD_COTTONWOOD:
  case VOXEL_WOOD_CYPRESS:
  case VOXEL_WOOD_SPRUCE:
  case VOXEL_WOOD_JUNIPER:
  case VOXEL_WOOD_REDWOOD:
  case VOXEL_BUSH:
  case VOXEL_BUSH_FERN:
  case VOXEL_BUSH_VINES:
  case VOXEL_BUSH_THORNS:
  case VOXEL_BUSH_BLUEBERRY:
  case VOXEL_BUSH_BLACKBERRY:
  case VOXEL_BUSH_RASPBERRY:
  case VOXEL_BUSH_STRAWBERRY:
    return ITEM_WOOD_BLOCK;

  case VOXEL_WOOL:
  case VOXEL_WOOL_WHITE:
  case VOXEL_WOOL_BLACK:
  case VOXEL_WOOL_BROWN:
  case VOXEL_WOOL_GRAY:
  case VOXEL_WOOL_RED:
  case VOXEL_WOOL_BLUE:
  case VOXEL_WOOL_GREEN:
  case VOXEL_WOOL_YELLOW:
    return ITEM_WOOL;

  case VOXEL_ORE_IRON:
    return ITEM_IRON_ORE;
  case VOXEL_ORE_COPPER:
    return ITEM_COPPER_ORE;
  case VOXEL_ORE_TIN:
    return ITEM_TIN_ORE;
  case VOXEL_ORE_COAL:
    return ITEM_COAL;
  case VOXEL_CLAY:
    return ITEM_CLAY;
  case VOXEL_SOIL_CLAY:
    return ITEM_CLAY;

  default:
    return ITEM_NONE;
  }
}

bool voxel_apply_damage(Voxel *voxel, uint8_t damage_amount)
{
  if (!voxel || damage_amount == 0)
    return false;

  const uint8_t dur = voxel_durability(voxel->type);
  if (dur == 0)
    return false;

  // Scale the hit into the 0..255 damage field so one byte can represent progress against any
  // durability. A hit that would finish the voxel clamps to 255 and reports destruction.
  const uint32_t cur = voxel_get_damage(voxel);
  const uint32_t add = ((uint32_t)damage_amount * 255u + (dur / 2u)) / dur;
  const uint32_t next = cur + (add > 0 ? add : 1u);
  if (next >= 255u)
  {
    voxel_set_damage(voxel, 255);
    return true;
  }
  voxel_set_damage(voxel, (uint8_t)next);
  return false;
}

void voxel_heat(Voxel *voxel, uint8_t heat_amount)
{
  if (!voxel || heat_amount == 0)
    return;
  uint32_t t = (uint32_t)voxel_get_temperature(voxel) + heat_amount;
  voxel_set_temperature(voxel, t > 255u ? 255u : (uint8_t)t);
}

bool voxel_cool(Voxel *voxel, uint8_t cool_amount)
{
  if (!voxel || cool_amount == 0)
    return false;

  // Drop residual heat-field energy as well (editor / magma-adjacent cells).
  uint8_t heat = voxel_get_heat(voxel);
  if (heat > 0)
  {
    uint8_t drop = cool_amount / 32u;
    if (drop < 1u)
      drop = 1u;
    voxel_set_heat(voxel, heat > drop ? (uint8_t)(heat - drop) : 0u);
  }

  uint8_t t = voxel_get_temperature(voxel);
  // Fresh voxels store temperature 0; treat that as ambient so ice magic can cool them.
  if (t == 0u && voxel->type != VOXEL_ICE)
    t = VOXEL_TEMP_AMBIENT;

  if (t <= cool_amount)
  {
    voxel_set_temperature(voxel, 0u);
    if (voxel->type == VOXEL_WATER)
    {
      voxel->type = VOXEL_ICE;
      return true;
    }
    if (voxel->type == VOXEL_MAGMA)
    {
      // Off-vent magma that has lost its heat sets to basalt — same product the fluid sim uses.
      voxel->type = VOXEL_STONE_BASALT;
      voxel_set_quantity(voxel, 0);
      return true;
    }
    return false;
  }

  voxel_set_temperature(voxel, (uint8_t)(t - cool_amount));
  return false;
}

uint32_t voxel_crack_seed(int x, int y, int z)
{
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h ? h : 1u;
}

static uint32_t crack_hash(uint32_t seed, int x, int y)
{
  uint32_t h = seed ^ (uint32_t)(x * 374761393 + y * 668265263);
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return h;
}

void voxel_crack_modulate(uint8_t damage, uint32_t seed, float u, float v,
                          uint8_t *r, uint8_t *g, uint8_t *b)
{
  if (damage == 0 || !r || !g || !b)
    return;

  const float fu = u - floorf(u);
  const float fv = v - floorf(v);
  const int tu = (int)(fu * (float)MATERIAL_FACE_SIZE) & (MATERIAL_FACE_SIZE - 1);
  const int tv = (int)(fv * (float)MATERIAL_FACE_SIZE) & (MATERIAL_FACE_SIZE - 1);

  // Overall darkening grows with damage so a lightly hit surface looks stressed before cracks open.
  const float stress = (float)damage / 255.0f;
  float shade = 1.0f - 0.22f * stress;

  // A few diagonal fault lines, seeded per voxel so neighbouring blocks crack differently.
  const int faults = 2 + (int)(stress * 4.0f);
  for (int f = 0; f < faults; f++)
  {
    const uint32_t h = crack_hash(seed + (uint32_t)f * 97u, tu, tv);
    const int ox = (int)(h % MATERIAL_FACE_SIZE);
    const int oy = (int)((h >> 8) % MATERIAL_FACE_SIZE);
    const int slope = (int)((h >> 16) & 3u) - 1; // -1,0,1,2 → mostly diagonals
    const int thickness = 1 + (stress > 0.6f ? 1 : 0);

    int dx = tu - ox;
    int dy = tv - oy;
    int dist = abs(dy - slope * dx);
    if (slope == 2)
      dist = abs(dx); // vertical seam
    if (dist <= thickness)
    {
      // Crack interior is nearly black; edges feather so it reads as a fracture rather than a stroke.
      const float edge = 1.0f - (float)dist / (float)(thickness + 1);
      shade *= 1.0f - (0.55f + 0.35f * stress) * edge;
    }
  }

  // Sparse chip pits near heavy damage.
  if (stress > 0.45f)
  {
    const uint32_t pit = crack_hash(seed ^ 0xA5A5A5A5u, tu / 2, tv / 2);
    if ((pit & 0xFFu) < (uint32_t)(18.0f * stress) &&
        abs(tu - (int)(pit % MATERIAL_FACE_SIZE)) < 2 &&
        abs(tv - (int)((pit >> 8) % MATERIAL_FACE_SIZE)) < 2)
      shade *= 0.35f;
  }

  if (shade < 0.12f)
    shade = 0.12f;
  *r = (uint8_t)((float)*r * shade);
  *g = (uint8_t)((float)*g * shade);
  *b = (uint8_t)((float)*b * shade);
}

bool voxel_bake_cracked_face(VoxelType type, MaterialFace face, uint8_t damage, uint32_t seed,
                             uint8_t base_r, uint8_t base_g, uint8_t base_b, uint32_t *out)
{
  if (!out || face < 0 || face >= MATERIAL_FACE_COUNT)
    return false;

  const MaterialTemplate *t = material_worlds_for_voxel(type);
  for (int tv = 0; tv < MATERIAL_FACE_SIZE; tv++)
  {
    for (int tu = 0; tu < MATERIAL_FACE_SIZE; tu++)
    {
      uint8_t r = base_r, g = base_g, b = base_b;
      uint8_t a = 255;
      if (t && t->baked)
      {
        const uint32_t texel = t->bake.texels[face][tv * MATERIAL_FACE_SIZE + tu];
        a = (uint8_t)((texel >> 24) & 0xFFu);
        if (a == 0)
        {
          out[tv * MATERIAL_FACE_SIZE + tu] = 0;
          continue;
        }
        r = (uint8_t)((texel >> 16) & 0xFFu);
        g = (uint8_t)((texel >> 8) & 0xFFu);
        b = (uint8_t)(texel & 0xFFu);
      }

      const float u = ((float)tu + 0.5f) / (float)MATERIAL_FACE_SIZE;
      const float v = ((float)tv + 0.5f) / (float)MATERIAL_FACE_SIZE;
      voxel_crack_modulate(damage, seed, u, v, &r, &g, &b);
      out[tv * MATERIAL_FACE_SIZE + tu] =
          ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
  }
  return true;
}
