#include "debris.h"

#include <math.h>
#include <string.h>

#include "material_worlds.h"

void debris_system_reset(DebrisSystem *sys)
{
  if (!sys)
    return;
  memset(sys, 0, sizeof(*sys));
}

static DebrisPiece *debris_alloc(DebrisSystem *sys)
{
  for (int i = 0; i < DEBRIS_MAX; i++)
  {
    if (!sys->items[i].active)
      return &sys->items[i];
  }
  sys->dropped_total++;
  return NULL;
}

static void debris_color_for(VoxelType type, uint8_t *r, uint8_t *g, uint8_t *b)
{
  uint8_t tr = 140, tg = 140, tb = 140;
  if (material_worlds_sample(type, MATERIAL_FACE_TOP, 0.5f, 0.5f, &tr, &tg, &tb))
  {
    *r = tr;
    *g = tg;
    *b = tb;
    return;
  }
  // Flat fallback when the material has no template.
  switch (type)
  {
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
    *r = 120;
    *g = 78;
    *b = 42;
    break;
  case VOXEL_SOIL:
  case VOXEL_SOIL_CLAY:
  case VOXEL_SOIL_LOAM:
  case VOXEL_SOIL_SILT:
  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
    *r = 90;
    *g = 110;
    *b = 50;
    break;
  default:
    *r = 130;
    *g = 130;
    *b = 128;
    break;
  }
}

static uint32_t debris_rng(uint32_t *state)
{
  *state = (*state * 1103515245u) + 12345u;
  return *state;
}

int debris_spawn_from_voxel(DebrisSystem *sys, World *world, float x, float y, float z,
                            VoxelType type, uint32_t total_pieces, uint32_t seed)
{
  (void)world;
  if (!sys || total_pieces == 0)
    return 0;

  const ItemId item = voxel_type_to_drop_item(type);
  if (item == ITEM_NONE)
    return 0;

  uint8_t r, g, b;
  debris_color_for(type, &r, &g, &b);

  int burst = DEBRIS_BURST_COUNT;
  if ((uint32_t)burst > total_pieces)
    burst = (int)total_pieces;

  uint32_t remaining = total_pieces;
  uint32_t rng = seed ? seed : voxel_crack_seed((int)x, (int)y, (int)z);
  int spawned = 0;

  for (int i = 0; i < burst && remaining > 0; i++)
  {
    DebrisPiece *d = debris_alloc(sys);
    if (!d)
      break;

    const int left = burst - i;
    uint32_t share = remaining / (uint32_t)left;
    if (share == 0)
      share = 1;
    if (share > remaining)
      share = remaining;
    remaining -= share;

    const float angle = ((float)(debris_rng(&rng) & 0xFFFFu) / 65535.0f) * 6.2831853f;
    const float elev = 0.35f + ((float)(debris_rng(&rng) & 0xFFFFu) / 65535.0f) * 0.85f;
    const float speed = 2.5f + ((float)(debris_rng(&rng) & 0xFFFFu) / 65535.0f) * 4.5f;

    d->active = true;
    d->x = x + (((float)(debris_rng(&rng) & 0xFFu) / 255.0f) - 0.5f) * 0.4f;
    d->y = y + (((float)(debris_rng(&rng) & 0xFFu) / 255.0f) - 0.5f) * 0.4f;
    d->z = z + 0.35f + ((float)(debris_rng(&rng) & 0xFFu) / 255.0f) * 0.3f;
    d->vx = cosf(angle) * speed * 0.7f;
    d->vy = sinf(angle) * speed * 0.7f;
    d->vz = elev * speed;
    d->type = type;
    d->item = item;
    d->pieces = share;
    d->durability = 0;
    d->durability_max = 0;
    d->quality = 0;
    d->material = 0;
    d->r = r;
    d->g = g;
    d->b = b;
    d->life = 45.0f;
    d->settled = false;
    d->icon_sprite = false;
    spawned++;
    sys->spawned_total++;
  }

  // If the pool filled early, fold leftover pieces into the last spawned entity so a destroyed
  // block still grants a full fractional block when collected.
  if (remaining > 0 && spawned > 0)
  {
    for (int i = DEBRIS_MAX - 1; i >= 0; i--)
    {
      if (!sys->items[i].active)
        continue;
      sys->items[i].pieces += remaining;
      break;
    }
  }

  return spawned;
}

bool debris_spawn_item_stack(DebrisSystem *sys, World *world, float x, float y, float z,
                             const ItemStack *stack, uint32_t seed)
{
  (void)world;
  if (!sys || !stack || stack->id == ITEM_NONE || stack->pieces == 0)
    return false;

  DebrisPiece *d = debris_alloc(sys);
  if (!d)
    return false;

  uint32_t rng = seed ? seed : 1u;
  const float angle = ((float)(debris_rng(&rng) & 0xFFFFu) / 65535.0f) * 6.2831853f;
  const float speed = 1.2f + ((float)(debris_rng(&rng) & 0xFFFFu) / 65535.0f) * 1.8f;

  d->active = true;
  d->x = x + (((float)(debris_rng(&rng) & 0xFFu) / 255.0f) - 0.5f) * 0.35f;
  d->y = y + (((float)(debris_rng(&rng) & 0xFFu) / 255.0f) - 0.5f) * 0.35f;
  d->z = z + 0.55f;
  d->vx = cosf(angle) * speed;
  d->vy = sinf(angle) * speed;
  d->vz = 2.2f + ((float)(debris_rng(&rng) & 0xFFu) / 255.0f) * 1.5f;
  d->type = VOXEL_AIR;
  d->item = stack->id;
  d->pieces = stack->pieces;
  d->durability = stack->durability;
  d->durability_max = stack->durability_max;
  d->quality = stack->quality;
  d->material = stack->material;
  d->r = 220;
  d->g = 220;
  d->b = 200;
  d->life = 120.0f;
  d->settled = false;
  d->icon_sprite = true;
  sys->spawned_total++;
  return true;
}

static bool debris_solid_at(World *world, int x, int y, int z)
{
  if (!world || !world_is_position_valid(world, (uint32_t)x, (uint32_t)y, (uint32_t)z))
    return true; // treat out of bounds as solid so pieces settle at the edge
  const Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  if (!v || v->type == VOXEL_AIR)
    return false;
  if (v->type == VOXEL_WATER || v->type == VOXEL_STEAM)
    return false;
  return world_voxel_type_blocks_movement(v->type);
}

void debris_system_step(DebrisSystem *sys, World *world, float dt, float gravity)
{
  if (!sys || !(dt > 0.0f))
    return;
  if (gravity < 0.0f)
    gravity = 0.0f;

  for (int i = 0; i < DEBRIS_MAX; i++)
  {
    DebrisPiece *d = &sys->items[i];
    if (!d->active)
      continue;

    d->life -= dt;
    if (d->life <= 0.0f)
    {
      d->active = false;
      continue;
    }

    if (!d->settled)
    {
      d->vz -= gravity * dt;
      d->x += d->vx * dt;
      d->y += d->vy * dt;
      d->z += d->vz * dt;

      // Dampen horizontal drift so pieces pile near the blast rather than skating forever.
      d->vx *= 0.98f;
      d->vy *= 0.98f;

      const float half = DEBRIS_SIZE * 0.5f;
      const int gx = (int)floorf(d->x);
      const int gy = (int)floorf(d->y);
      const int gz = (int)floorf(d->z - half);

      if (debris_solid_at(world, gx, gy, gz))
      {
        d->z = (float)gz + 1.0f + half;
        d->vx *= 0.4f;
        d->vy *= 0.4f;
        d->vz = 0.0f;
        if (fabsf(d->vx) < 0.15f && fabsf(d->vy) < 0.15f)
        {
          d->vx = d->vy = 0.0f;
          d->settled = true;
        }
      }
    }
  }
}

uint32_t debris_try_pickup(DebrisSystem *sys, Inventory *inv, float px, float py, float pz,
                           float radius)
{
  if (!sys || !inv || !(radius > 0.0f))
    return 0;

  const float r2 = radius * radius;
  uint32_t collected = 0;

  for (int i = 0; i < DEBRIS_MAX; i++)
  {
    DebrisPiece *d = &sys->items[i];
    if (!d->active || d->pieces == 0 || d->item == ITEM_NONE)
      continue;

    const float dx = d->x - px;
    const float dy = d->y - py;
    const float dz = d->z - pz;
    if (dx * dx + dy * dy + dz * dz > r2)
      continue;

    ItemStack stack = {0};
    stack.id = d->item;
    stack.pieces = d->pieces;
    stack.durability = d->durability;
    stack.durability_max = d->durability_max;
    stack.quality = d->quality;
    stack.material = d->material;
    if (d->icon_sprite)
    {
      if (!inventory_add_stack(inv, &stack))
        continue; // bag full — leave it on the ground
    }
    else if (!inventory_add_pieces(inv, d->item, d->pieces))
    {
      continue;
    }

    collected += d->pieces;
    d->active = false;
  }

  return collected;
}

int debris_active_count(const DebrisSystem *sys)
{
  if (!sys)
    return 0;
  int n = 0;
  for (int i = 0; i < DEBRIS_MAX; i++)
    if (sys->items[i].active)
      n++;
  return n;
}
