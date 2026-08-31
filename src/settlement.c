#include "settlement.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mob_ai.h"
#include "voxel_shape.h"

// Jittered Voronoi lattice spacing (cells). Sites sit one per tile with hash jitter.
#define SETTLEMENT_VORONOI_CELL 7
// Minimum flatness (0..1) to found a settlement at a Voronoi site.
#define SETTLEMENT_FLAT_MIN 0.35f
// Search radius for the main trade hub near the origin.
#define SETTLEMENT_MAIN_HUB_RADIUS 48

// Wilderness cell directly under the home island (gx=gy=0, universe z=0). Always a single hut.
static inline bool settlement_is_home_drop(int gx, int gy)
{
  return gx == 0 && gy == 0;
}

static inline int settlement_iabs(int v)
{
  return v < 0 ? -v : v;
}

static inline int settlement_imax(int a, int b)
{
  return a > b ? a : b;
}

static inline int settlement_manhattan(int ax, int ay, int bx, int by)
{
  return settlement_iabs(ax - bx) + settlement_iabs(ay - by);
}

static uint32_t settlement_hash(int gx, int gy, uint32_t salt)
{
  uint32_t x = (uint32_t)gx;
  uint32_t y = (uint32_t)gy;
  uint32_t h = x * 374761393u ^ y * 668265263u ^ salt;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// Lightweight height proxy for universe cells (no World required).
// Coarse octaves only — fine hash noise would make every cell look steep.
static float settlement_proxy_height(int gx, int gy)
{
  const uint32_t continental = settlement_hash(gx / 12, gy / 12, 0xf1a70001u);
  const uint32_t regional = settlement_hash(gx / 5, gy / 5, 0x7e44a11fu);
  const float c = (float)(continental & 1023u) / 1023.0f;
  const float r = (float)(regional & 1023u) / 1023.0f;
  return c * 0.75f + r * 0.25f;
}

float universe_settlement_flatness(int gx, int gy)
{
  float min_h = 1e9f, max_h = -1e9f;
  const int R = 2;
  for (int dy = -R; dy <= R; dy++)
  {
    for (int dx = -R; dx <= R; dx++)
    {
      const float h = settlement_proxy_height(gx + dx, gy + dy);
      if (h < min_h)
        min_h = h;
      if (h > max_h)
        max_h = h;
    }
  }
  // Relief across the neighborhood; basins score near 1.
  float relief = max_h - min_h;
  float flat = 1.0f - relief * 3.5f;
  if (flat < 0.0f)
    flat = 0.0f;
  if (flat > 1.0f)
    flat = 1.0f;
  // Mild preference for mid elevations (avoid alpine / deep trench proxies).
  const float mid = settlement_proxy_height(gx, gy);
  const float elev_penalty = (mid < 0.18f || mid > 0.88f) ? 0.15f : 0.0f;
  flat -= elev_penalty;
  if (flat < 0.0f)
    flat = 0.0f;
  return flat;
}

static void settlement_div_floor(int v, int den, int *q)
{
  if (v >= 0)
    *q = v / den;
  else
    *q = -((-v + den - 1) / den);
}

static void settlement_site_of_lattice(int lx, int ly, int *sx, int *sy)
{
  const uint32_t h = settlement_hash(lx, ly, 0x50a0b01u);
  const int jx = (int)(h % (uint32_t)SETTLEMENT_VORONOI_CELL);
  const int jy = (int)((h >> 8) % (uint32_t)SETTLEMENT_VORONOI_CELL);
  if (sx)
    *sx = lx * SETTLEMENT_VORONOI_CELL + jx;
  if (sy)
    *sy = ly * SETTLEMENT_VORONOI_CELL + jy;
}

// Nearest jittered Voronoi site to (gx, gy).
static void settlement_owning_site(int gx, int gy, int *out_sx, int *out_sy)
{
  int lx0 = 0, ly0 = 0;
  settlement_div_floor(gx, SETTLEMENT_VORONOI_CELL, &lx0);
  settlement_div_floor(gy, SETTLEMENT_VORONOI_CELL, &ly0);

  int best_d = INT_MAX;
  int best_x = gx, best_y = gy;
  for (int ly = ly0 - 1; ly <= ly0 + 1; ly++)
  {
    for (int lx = lx0 - 1; lx <= lx0 + 1; lx++)
    {
      int tx = 0, ty = 0;
      settlement_site_of_lattice(lx, ly, &tx, &ty);
      const int ddx = gx - tx;
      const int ddy = gy - ty;
      const int d = ddx * ddx + ddy * ddy;
      if (d < best_d || (d == best_d && (tx < best_x || (tx == best_x && ty < best_y))))
      {
        best_d = d;
        best_x = tx;
        best_y = ty;
      }
    }
  }
  if (out_sx)
    *out_sx = best_x;
  if (out_sy)
    *out_sy = best_y;
}

static bool settlement_is_voronoi_site(int gx, int gy)
{
  int sx = 0, sy = 0;
  settlement_owning_site(gx, gy, &sx, &sy);
  return sx == gx && sy == gy;
}

// Scale at a Voronoi site before force-directed suppression. Returns 0..9.
static int settlement_raw_scale(int gx, int gy)
{
  if (settlement_is_home_drop(gx, gy))
    return 1;
  if (!settlement_is_voronoi_site(gx, gy))
    return 0;

  const float flat = universe_settlement_flatness(gx, gy);
  if (flat < SETTLEMENT_FLAT_MIN)
    return 0;

  // Larger flat basins host larger settlements.
  int from_flat = 1 + (int)((flat - SETTLEMENT_FLAT_MIN) / (1.0f - SETTLEMENT_FLAT_MIN) * 8.0f);
  if (from_flat < 1)
    from_flat = 1;
  if (from_flat > 9)
    from_flat = 9;

  const uint32_t h = settlement_hash(gx, gy, 0x5e771e11u);
  const int from_hash = 1 + (int)((h >> 10) % 9u);
  int scale = (from_flat * 2 + from_hash) / 3;
  if (scale < 1)
    scale = 1;
  if (scale > 9)
    scale = 9;

  // Fortress cities need especially flat basins.
  if (scale >= 8 && flat < 0.78f)
    scale = 6 + (int)((flat - SETTLEMENT_FLAT_MIN) * 4.0f);
  else if (scale >= 7 && flat < 0.70f)
    scale = 5;
  if (scale < 1)
    scale = 1;
  if (scale > 9)
    scale = 9;
  return scale;
}

static int settlement_exclusion_radius(int scale)
{
  if (scale < 1)
    return 1;
  // Force-directed spacing: larger settlements claim wider territories.
  return 1 + scale / 3; // 1..4
}

static uint32_t settlement_site_priority(int gx, int gy, int scale)
{
  uint32_t prio = settlement_hash(gx, gy, 0xa11acedu);
  const float flat = universe_settlement_flatness(gx, gy);
  prio += (uint32_t)(flat * 120000.0f);
  prio += (uint32_t)scale * 9000u;
  return prio;
}

int universe_settlement_scale(int gx, int gy)
{
  // Guaranteed landing pad under home: one building, never suppressed by neighbors.
  if (settlement_is_home_drop(gx, gy))
    return 1;

  const int scale = settlement_raw_scale(gx, gy);
  if (scale == 0)
    return 0;

  const int radius = settlement_exclusion_radius(scale);
  const uint32_t prio = settlement_site_priority(gx, gy, scale);
  for (int dy = -radius; dy <= radius; dy++)
  {
    for (int dx = -radius; dx <= radius; dx++)
    {
      if (dx == 0 && dy == 0)
        continue;
      const int nx = gx + dx;
      const int ny = gy + dy;
      // Home-drop always wins the neighborhood, so nothing may sit beside it.
      if (settlement_is_home_drop(nx, ny))
        return 0;
      const int ns = settlement_raw_scale(nx, ny);
      if (ns == 0)
        continue;
      const int excl = settlement_imax(radius, settlement_exclusion_radius(ns));
      const int cheb = settlement_imax(settlement_iabs(dx), settlement_iabs(dy));
      if (cheb > excl)
        continue;
      const uint32_t np = settlement_site_priority(nx, ny, ns);
      // Neighbor keeps the settlement if it has higher priority, or equal priority and
      // lexicographically smaller coordinates (stable tie-break).
      if (np > prio || (np == prio && (nx < gx || (nx == gx && ny < gy))))
        return 0;
    }
  }
  return scale;
}

bool universe_main_hub(int *out_gx, int *out_gy)
{
  int best_s = 0;
  int best_d = INT_MAX;
  int best_x = 0, best_y = 0;
  bool found = false;
  const int R = SETTLEMENT_MAIN_HUB_RADIUS;
  for (int gy = -R; gy <= R; gy++)
  {
    for (int gx = -R; gx <= R; gx++)
    {
      if (settlement_is_home_drop(gx, gy))
        continue;
      const int s = universe_settlement_scale(gx, gy);
      if (s < 5)
        continue;
      const int d = settlement_manhattan(gx, gy, 0, 0);
      if (!found || s > best_s ||
          (s == best_s && (d < best_d || (d == best_d && (gx < best_x || (gx == best_x && gy < best_y))))))
      {
        found = true;
        best_s = s;
        best_d = d;
        best_x = gx;
        best_y = gy;
      }
    }
  }
  if (!found)
    return false;
  if (out_gx)
    *out_gx = best_x;
  if (out_gy)
    *out_gy = best_y;
  return true;
}

bool universe_nearest_settlement_of_scale(int from_gx, int from_gy, int scale, int max_manhattan,
                                          int *out_gx, int *out_gy)
{
  if (scale < 1 || scale > 9 || max_manhattan < 1)
    return false;

  int best_d = max_manhattan + 1;
  int best_x = 0, best_y = 0;
  bool found = false;
  for (int dy = -max_manhattan; dy <= max_manhattan; dy++)
  {
    for (int dx = -max_manhattan; dx <= max_manhattan; dx++)
    {
      if (dx == 0 && dy == 0)
        continue;
      const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
      if (d > max_manhattan || d > best_d)
        continue;
      const int nx = from_gx + dx;
      const int ny = from_gy + dy;
      if (universe_settlement_scale(nx, ny) != scale)
        continue;
      if (!found || d < best_d ||
          (d == best_d && (nx < best_x || (nx == best_x && ny < best_y))))
      {
        found = true;
        best_d = d;
        best_x = nx;
        best_y = ny;
      }
    }
  }
  if (!found)
    return false;
  if (out_gx)
    *out_gx = best_x;
  if (out_gy)
    *out_gy = best_y;
  return true;
}

int settlement_building_count(int scale)
{
  if (scale < 1)
    return 0;
  if (scale > 9)
    scale = 9;
  return scale * scale;
}

const char *settlement_scale_label(int scale)
{
  if (scale <= 0)
    return "Wilderness";
  if (scale == 1)
    return "Hut";
  if (scale <= 3)
    return "Hamlet";
  if (scale <= 5)
    return "Village";
  if (scale <= 7)
    return "Town";
  if (scale == 8)
    return "City";
  return "Fortress";
}

const char *settlement_building_type_name(SettlementBuildingType type)
{
  switch (type)
  {
  case SETTLEMENT_BLDG_HUT: return "hut";
  case SETTLEMENT_BLDG_COTTAGE: return "cottage";
  case SETTLEMENT_BLDG_HOUSE: return "house";
  case SETTLEMENT_BLDG_SHOP: return "shop";
  case SETTLEMENT_BLDG_TOWER: return "tower";
  case SETTLEMENT_BLDG_HALL: return "hall";
  case SETTLEMENT_BLDG_MANOR: return "manor";
  case SETTLEMENT_BLDG_CASTLE: return "castle";
  case SETTLEMENT_BLDG_BASIC: return "basic";
  case SETTLEMENT_BLDG_COUNT:
  default: return "unknown";
  }
}

const char *settlement_proc_complexity_name(SettlementProcComplexity complexity)
{
  switch (complexity)
  {
  case SETTLEMENT_PROC_ONE_ROOM: return "one_room";
  case SETTLEMENT_PROC_MULTI_ROOM: return "multi_room";
  case SETTLEMENT_PROC_MULTI_LEVEL: return "multi_level";
  case SETTLEMENT_PROC_COUNT:
  default: return "unknown";
  }
}

SettlementProcComplexity settlement_proc_complexity_for_type(SettlementBuildingType type)
{
  switch (type)
  {
  case SETTLEMENT_BLDG_HUT:
  case SETTLEMENT_BLDG_BASIC:
    return SETTLEMENT_PROC_ONE_ROOM;
  case SETTLEMENT_BLDG_COTTAGE:
  case SETTLEMENT_BLDG_HOUSE:
  case SETTLEMENT_BLDG_SHOP:
    return SETTLEMENT_PROC_MULTI_ROOM;
  case SETTLEMENT_BLDG_TOWER:
  case SETTLEMENT_BLDG_HALL:
  case SETTLEMENT_BLDG_MANOR:
  case SETTLEMENT_BLDG_CASTLE:
    return SETTLEMENT_PROC_MULTI_LEVEL;
  case SETTLEMENT_BLDG_COUNT:
  default:
    return SETTLEMENT_PROC_ONE_ROOM;
  }
}

static uint32_t settlement_rng(uint32_t *state);

uint8_t settlement_occupation_for_building(SettlementBuildingType type, uint32_t *rng)
{
  uint32_t local = 0x0CC11u;
  uint32_t *r = rng ? rng : &local;
  switch (type)
  {
  case SETTLEMENT_BLDG_HUT:
  case SETTLEMENT_BLDG_COTTAGE:
    return (settlement_rng(r) & 1u) ? (uint8_t)VILLAGER_JOB_FARMER
                                    : (uint8_t)VILLAGER_JOB_SHEPHERD;
  case SETTLEMENT_BLDG_HOUSE:
    switch (settlement_rng(r) % 3u)
    {
    case 0: return (uint8_t)VILLAGER_JOB_FARMER;
    case 1: return (uint8_t)VILLAGER_JOB_MILLER;
    default: return (uint8_t)VILLAGER_JOB_BAKER;
    }
  case SETTLEMENT_BLDG_SHOP:
    return (uint8_t)VILLAGER_JOB_MERCHANT;
  case SETTLEMENT_BLDG_TOWER:
  case SETTLEMENT_BLDG_CASTLE:
    return (uint8_t)VILLAGER_JOB_GUARD;
  case SETTLEMENT_BLDG_HALL:
    return (settlement_rng(r) & 1u) ? (uint8_t)VILLAGER_JOB_HEALER
                                    : (uint8_t)VILLAGER_JOB_MERCHANT;
  case SETTLEMENT_BLDG_MANOR:
    return (settlement_rng(r) & 1u) ? (uint8_t)VILLAGER_JOB_HEALER
                                    : (uint8_t)VILLAGER_JOB_MERCHANT;
  case SETTLEMENT_BLDG_BASIC:
  case SETTLEMENT_BLDG_COUNT:
  default:
    return (uint8_t)VILLAGER_JOB_FARMER;
  }
}

const char *settlement_occupation_label(uint8_t occupation)
{
  return villager_profession_name((VillagerProfession)occupation);
}

bool settlement_has_anchor(const World *world)
{
  return world && world->settlement_has_anchor;
}

bool settlement_anchor(const World *world, int *out_x, int *out_y, int *out_z)
{
  if (!settlement_has_anchor(world))
    return false;
  if (out_x)
    *out_x = world->settlement_anchor_x;
  if (out_y)
    *out_y = world->settlement_anchor_y;
  if (out_z)
    *out_z = world->settlement_anchor_z;
  return true;
}

bool settlement_has_note(const World *world)
{
  return world && world->settlement_has_note;
}

bool settlement_note(const World *world, int *out_x, int *out_y, int *out_z)
{
  if (!settlement_has_note(world))
    return false;
  if (out_x)
    *out_x = world->settlement_note_x;
  if (out_y)
    *out_y = world->settlement_note_y;
  if (out_z)
    *out_z = world->settlement_note_z;
  return true;
}

static void settlement_record_note(World *world, int x, int y, int z)
{
  if (!world)
    return;
  world->settlement_has_note = true;
  world->settlement_note_x = (int16_t)x;
  world->settlement_note_y = (int16_t)y;
  world->settlement_note_z = (int16_t)z;
}

static void settlement_record_anchor(World *world, int ox, int oy, int bw, int bd, int ground_z)
{
  if (!world)
    return;
  world->settlement_has_anchor = true;
  world->settlement_anchor_x = (int16_t)(ox + bw / 2);
  world->settlement_anchor_y = (int16_t)(oy + bd / 2);
  world->settlement_anchor_z = (int16_t)(ground_z + 1);
}

static uint32_t settlement_rng(uint32_t *state)
{
  *state = *state * 1664525u + 1013904223u;
  return *state;
}

SettlementBuildingType settlement_pick_building_type(int scale, int index, int total, uint32_t *rng)
{
  if (scale < 1)
    return SETTLEMENT_BLDG_BASIC;
  if (scale > 9)
    scale = 9;
  if (total < 1)
    total = 1;
  if (index < 0)
    index = 0;

  // Landmark slots: first structure in larger settlements.
  if (index == 0 && scale >= 8)
    return SETTLEMENT_BLDG_CASTLE;
  if (index == 0 && scale >= 6)
    return SETTLEMENT_BLDG_MANOR;
  if (index == 0 && scale >= 5)
    return SETTLEMENT_BLDG_HALL;
  if (index == 1 && scale >= 7)
    return SETTLEMENT_BLDG_TOWER;

  uint32_t r = rng ? (settlement_rng(rng) % 100u) : (uint32_t)((index * 37) % 100);
  if (scale <= 1)
    return SETTLEMENT_BLDG_HUT;
  if (scale <= 3)
  {
    // Hamlets (scale 3) may include a shop so a shopkeeper has a workplace.
    if (scale >= 3 && r < 22)
      return SETTLEMENT_BLDG_SHOP;
    if (r < 35)
      return SETTLEMENT_BLDG_HUT;
    return SETTLEMENT_BLDG_COTTAGE;
  }
  if (scale <= 5)
  {
    if (r < 20)
      return SETTLEMENT_BLDG_COTTAGE;
    if (r < 55)
      return SETTLEMENT_BLDG_HOUSE;
    if (r < 75)
      return SETTLEMENT_BLDG_SHOP;
    if (r < 90)
      return SETTLEMENT_BLDG_TOWER;
    return SETTLEMENT_BLDG_HALL;
  }
  // Town / city / fortress fill.
  if (r < 15)
    return SETTLEMENT_BLDG_COTTAGE;
  if (r < 45)
    return SETTLEMENT_BLDG_HOUSE;
  if (r < 65)
    return SETTLEMENT_BLDG_SHOP;
  if (r < 80)
    return SETTLEMENT_BLDG_TOWER;
  if (r < 92)
    return SETTLEMENT_BLDG_HALL;
  return SETTLEMENT_BLDG_MANOR;
}

static bool settlement_is_plant(VoxelType t)
{
  if (t == VOXEL_AIR || t == VOXEL_GRASS_TALL)
    return true;
  if (t >= VOXEL_BUSH && t <= VOXEL_BUSH_STRAWBERRY)
    return true;
  if (t >= VOXEL_LEAVES && t <= VOXEL_LEAVES_REDWOOD)
    return true;
  return false;
}

static int settlement_surface_z(const World *world, int x, int y)
{
  int z = world_height_at_fast(world, x, y);
  while (z >= 0)
  {
    const Voxel *v = world_voxel_cptr_fast(world, x, y, z);
    if (!v || !settlement_is_plant(v->type))
      return z;
    z--;
  }
  return -1;
}

static void settlement_clear_column_above(World *world, int x, int y, int ground_z, int clear_h)
{
  if (!world || ground_z < 0)
    return;
  for (int dz = 1; dz <= clear_h; dz++)
  {
    const int z = ground_z + dz;
    if (z < 0 || (uint32_t)z >= world->depth)
      break;
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_AIR);
  }
}

// Wall / roof / floor palette for procedural buildings.
typedef struct {
  VoxelType wall;
  VoxelType floor;
  VoxelType roof;
  VoxelType foundation;
  VoxelType window;
} SettlementPalette;

static SettlementPalette settlement_pick_palette(uint32_t *rng)
{
  SettlementPalette p;
  switch (settlement_rng(rng) % 4u)
  {
  case 0: p.wall = VOXEL_WOOD_OAK; break;
  case 1: p.wall = VOXEL_BRICK; break;
  case 2: p.wall = VOXEL_ADOBE; break;
  default: p.wall = VOXEL_PLASTER; break;
  }
  p.floor = (settlement_rng(rng) & 1u) ? VOXEL_PLANK : VOXEL_COBBLE;
  // Textured roof materials only — thatch or overlapping clay tiles.
  p.roof = (settlement_rng(rng) & 1u) ? VOXEL_THATCH : VOXEL_ROOF_TILE;
  p.foundation = (settlement_rng(rng) & 1u) ? VOXEL_STONE : VOXEL_COBBLE;
  p.window = VOXEL_GLASS;
  return p;
}

// Story clear height (air above floor slab to ceiling underside). Door = 2 voxels.
#define SETTLEMENT_STORY_H 4
#define SETTLEMENT_DOOR_H 2

static SettlementProcComplexity settlement_clamp_complexity(int bw, int bd,
                                                            SettlementProcComplexity want)
{
  if (want >= SETTLEMENT_PROC_MULTI_LEVEL && bw >= 6 && bd >= 6)
    return SETTLEMENT_PROC_MULTI_LEVEL;
  if (want >= SETTLEMENT_PROC_MULTI_ROOM && bw >= 7 && bd >= 7)
    return SETTLEMENT_PROC_MULTI_ROOM;
  return SETTLEMENT_PROC_ONE_ROOM;
}

static bool settlement_is_pavement(VoxelType t)
{
  return t == VOXEL_STONE || t == VOXEL_COBBLE || t == VOXEL_GRAVEL || t == VOXEL_SOIL ||
         t == VOXEL_BRICK || t == VOXEL_PLANK;
}

// Prefer the façade whose outward normal faces the town plaza / nearest pavement (roads, plaza).
static int settlement_pick_door_side(World *world, int ox, int oy, int bw, int bd, uint32_t *rng)
{
  const int mx = ox + bw / 2;
  const int my = oy + bd / 2;
  int tx = (int)world->width / 2;
  int ty = (int)world->height / 2;
  if (world->settlement_has_town_center)
  {
    tx = world->settlement_town_cx;
    ty = world->settlement_town_cy;
  }

  float best = -1e9f;
  int best_side = (int)(settlement_rng(rng) % 4u);
  for (int side = 0; side < 4; side++)
  {
    int ndx = 0, ndy = 0;
    int wall_x = mx, wall_y = my;
    if (side == 0)
    {
      ndy = -1;
      wall_y = oy;
    }
    else if (side == 1)
    {
      ndy = 1;
      wall_y = oy + bd - 1;
    }
    else if (side == 2)
    {
      ndx = -1;
      wall_x = ox;
    }
    else
    {
      ndx = 1;
      wall_x = ox + bw - 1;
    }

    float score = (float)(settlement_rng(rng) % 50u) * 0.01f; // tiny jitter
    const float tox = (float)(tx - mx);
    const float toy = (float)(ty - my);
    const float tlen = sqrtf(tox * tox + toy * toy) + 1e-3f;
    // Door wall's outward normal should point toward town (approach from the plaza).
    score += 2.0f * (((float)ndx * tox + (float)ndy * toy) / tlen);

    const int gz = settlement_surface_z(world, wall_x, wall_y);
    for (int step = 1; step <= 5; step++)
    {
      const int px = wall_x + ndx * step;
      const int py = wall_y + ndy * step;
      if (px < 0 || py < 0 || px >= (int)world->width || py >= (int)world->height)
        break;
      const int pz = (gz >= 0) ? gz : settlement_surface_z(world, px, py);
      if (pz < 0)
        continue;
      const Voxel *v = world_get_voxel(world, (uint32_t)px, (uint32_t)py, (uint32_t)pz);
      if (v && settlement_is_pavement(v->type))
      {
        score += 4.0f - 0.4f * (float)step; // nearer pavement wins
        break;
      }
    }

    if (score > best)
    {
      best = score;
      best_side = side;
    }
  }
  return best_side;
}

static bool settlement_is_exterior_door(int dx, int dy, int bw, int bd, int door_side, int z_local)
{
  if (z_local < 1 || z_local > SETTLEMENT_DOOR_H)
    return false;
  if (door_side == 0 && dy == 0 && dx == bw / 2)
    return true;
  if (door_side == 1 && dy == bd - 1 && dx == bw / 2)
    return true;
  if (door_side == 2 && dx == 0 && dy == bd / 2)
    return true;
  if (door_side == 3 && dx == bw - 1 && dy == bd / 2)
    return true;
  return false;
}

// Door panel orientation matches the wall: N/S openings use the Y-plane template.
static VoxelType settlement_door_voxel(int door_side)
{
  return (door_side == 0 || door_side == 1) ? VOXEL_DOOR_NS : VOXEL_DOOR;
}

// Window pane orientation matches the wall it sits in.
static VoxelType settlement_window_voxel(int dx, int dy, int bw, int bd)
{
  if (dy == 0 || dy == bd - 1)
    return VOXEL_GLASS_NS;
  if (dx == 0 || dx == bw - 1)
    return VOXEL_GLASS;
  return VOXEL_GLASS;
}

static bool settlement_is_window(int dx, int dy, int bw, int bd, int door_side, int z_local,
                                 int story_base_local)
{
  // Windows sit mid-story (above door height), skip corners and the door column.
  if (z_local != story_base_local + 2)
    return false;
  const bool edge = (dx == 0 || dy == 0 || dx == bw - 1 || dy == bd - 1);
  if (!edge)
    return false;
  if ((dx == 0 || dx == bw - 1) && (dy == 0 || dy == bd - 1))
    return false; // corner posts stay solid
  if (settlement_is_exterior_door(dx, dy, bw, bd, door_side, 1))
    return false;
  // Space windows every other eligible cell.
  if (dx == 0 || dx == bw - 1)
    return (dy % 3) == 1;
  return (dx % 3) == 1;
}

// Interior partition cell? split_x/split_y are wall columns (-1 = none).
static bool settlement_is_partition(int dx, int dy, int split_x, int split_y)
{
  if (split_x >= 0 && dx == split_x)
    return true;
  if (split_y >= 0 && dy == split_y)
    return true;
  return false;
}

static bool settlement_is_interior_door(int dx, int dy, int bd, int bw, int split_x, int split_y,
                                        int z_in_story)
{
  if (z_in_story < 1 || z_in_story > SETTLEMENT_DOOR_H)
    return false;
  if (split_x >= 0 && dx == split_x && dy == bd / 2)
    return true;
  if (split_y >= 0 && dy == split_y && dx == bw / 2)
    return true;
  return false;
}

// Stairwell occupies a 2×2 corner; steps rise along +X then continue as landing hole.
static bool settlement_in_stairwell(int dx, int dy, int bw, int bd, int stair_x, int stair_y)
{
  (void)bw;
  (void)bd;
  return dx >= stair_x && dx <= stair_x + 1 && dy >= stair_y && dy <= stair_y + 1;
}

static void settlement_place_stairs(World *world, int ox, int oy, int min_ground, int story,
                                    int stair_x, int stair_y, VoxelType tread)
{
  // Rise one stair voxel per step along +X inside the stairwell for one story height.
  // Prefer the sparse stair wedge when the caller passes plank/cobble treads.
  const VoxelType step_type =
      (tread == VOXEL_PLANK || tread == VOXEL_COBBLE || tread == VOXEL_WOOD ||
       tread == VOXEL_WOOD_OAK)
          ? VOXEL_STAIR
          : tread;
  for (int step = 0; step < SETTLEMENT_STORY_H; step++)
  {
    const int dx = stair_x + (step < 2 ? 0 : 1);
    const int dy = stair_y;
    const int z = min_ground + 1 + story * SETTLEMENT_STORY_H + step;
    if (z < 0 || (uint32_t)z >= world->depth)
      break;
    world_set_voxel(world, (uint32_t)(ox + dx), (uint32_t)(oy + dy), (uint32_t)z, step_type);
  }
}

static void settlement_place_roof(World *world, int ox, int oy, int bw, int bd, int plate_z,
                                  VoxelType roof, VoxelType wall)
{
  // Pitched roof peaking along the building X centerline. Eaves are the ±X walls
  // (rise == 0 on the plate). ±Y walls are gables: fill them up to the roof so the
  // slope never floats above empty air on the silhouette.
  //
  // One-cell eave overhang past each ±X wall so the shell reads as a real roof, not a
  // flush lid. Mirror materials cover the right half so the two slopes lean toward the ridge.
  const VoxelType roof_r =
      (roof == VOXEL_THATCH) ? VOXEL_THATCH_MIRROR
      : (roof == VOXEL_ROOF_TILE) ? VOXEL_ROOF_TILE_MIRROR
                                 : roof;
  const int eave = 1;
  for (int dy = 0; dy < bd; dy++)
  {
    for (int dx = -eave; dx < bw + eave; dx++)
    {
      const int from_left = dx + eave;
      const int span = bw + 2 * eave;
      const int from_right = span - 1 - from_left;
      const int rise = (from_left < from_right) ? from_left : from_right;
      const int roof_z = plate_z + rise;
      if (roof_z < 0 || (uint32_t)roof_z >= world->depth)
        continue;

      const int wx = ox + dx;
      const int wy = oy + dy;
      if (wx < 0 || wy < 0 || wx >= (int)world->width || wy >= (int)world->height)
        continue;

      const bool on_footprint = (dx >= 0 && dx < bw && dy >= 0 && dy < bd);
      const bool gable_end = on_footprint && (dy == 0 || dy == bd - 1);
      if (gable_end && rise > 0)
      {
        for (int gz = plate_z + 1; gz < roof_z; gz++)
        {
          if (gz >= 0 && (uint32_t)gz < world->depth)
            world_set_voxel(world, (uint32_t)wx, (uint32_t)wy, (uint32_t)gz, wall);
        }
      }

      const bool right_half = (from_left >= span / 2);
      VoxelType mat = right_half ? roof_r : roof;
      world_set_voxel(world, (uint32_t)wx, (uint32_t)wy, (uint32_t)roof_z, mat);
    }
  }
}

// Try to place a prop on an interior floor cell (not door midpoints / edges).
static bool settlement_try_prop(World *world, int ox, int oy, int bw, int bd, int floor_z,
                                int dx, int dy, VoxelType t)
{
  if (dx <= 0 || dy <= 0 || dx >= bw - 1 || dy >= bd - 1)
    return false;
  const int x = ox + dx;
  const int y = oy + dy;
  const int z = floor_z + 1;
  if (z < 0 || (uint32_t)z >= world->depth)
    return false;
  // Don't overwrite doors or walls.
  const Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  if (v && v->type != VOXEL_AIR)
    return false;
  world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
  return true;
}

static void settlement_furnish(World *world, int ox, int oy, int bw, int bd, int floor_z,
                               SettlementBuildingType type, uint8_t occupation, uint32_t *rng)
{
  if (!world || bw < 5 || bd < 5)
    return;

  const int cx = bw / 2;
  const int cy = bd / 2;
  // Offset from geometric center — interior doors sit on mid-span partitions.
  const int hx = (cx > 1) ? cx - 1 : 1;
  const int hy = (cy > 1) ? cy - 1 : 1;

  // Shared hearth: table with a candle, kept off partition doorways.
  if (settlement_try_prop(world, ox, oy, bw, bd, floor_z, hx, hy, VOXEL_TABLE))
  {
    if ((uint32_t)(floor_z + 2) < world->depth)
      world_set_voxel(world, (uint32_t)(ox + hx), (uint32_t)(oy + hy), (uint32_t)(floor_z + 2),
                      VOXEL_CANDLE);
  }
  settlement_try_prop(world, ox, oy, bw, bd, floor_z, hx + 1, hy, VOXEL_CHAIR);
  settlement_try_prop(world, ox, oy, bw, bd, floor_z, hx - 1, hy, VOXEL_CHAIR);

  switch ((VillagerProfession)occupation)
  {
  case VILLAGER_JOB_FARMER:
  case VILLAGER_JOB_SHEPHERD:
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, 1, VOXEL_BED);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 2, 1, VOXEL_BED);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, bd - 2, VOXEL_CHEST);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 3, bd - 2, VOXEL_BARREL);
    if (occupation == (uint8_t)VILLAGER_JOB_SHEPHERD)
      settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, bd - 2, VOXEL_WOOL_WHITE);
    else
      settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, bd - 2, VOXEL_WOOL_YELLOW);
    break;

  case VILLAGER_JOB_BAKER:
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, cx - 1, 1, VOXEL_BRICK); // oven base
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, cx, 1, VOXEL_BRICK);
    if ((uint32_t)(floor_z + 2) < world->depth)
      world_set_voxel(world, (uint32_t)(ox + cx), (uint32_t)(oy + 1), (uint32_t)(floor_z + 2),
                      VOXEL_CAMPFIRE);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, cy, VOXEL_TABLE); // counter
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, cy + 1, VOXEL_CERAMIC);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, bd - 2, VOXEL_BARREL);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 2, bd - 2, VOXEL_CRATE);
    break;

  case VILLAGER_JOB_MILLER:
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, cx, cy - 1, VOXEL_COBBLE); // millstone
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, cx + 1, cy - 1, VOXEL_COBBLE);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, 1, VOXEL_BARREL);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 2, 1, VOXEL_BARREL);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, bd - 2, VOXEL_CHEST);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 3, bd - 2, VOXEL_WOOL_BROWN);
    break;

  case VILLAGER_JOB_MERCHANT:
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, cy, VOXEL_TABLE); // counter run
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, cy + 1, VOXEL_TABLE);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 2, cy, VOXEL_CERAMIC);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 2, cy + 1, VOXEL_WOOL_RED);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, 1, VOXEL_CHEST);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, 2, VOXEL_CRATE);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, bd - 2, VOXEL_BARREL);
        settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 3, cy, VOXEL_CRAFTING_TABLE);
  break;

  case VILLAGER_JOB_BLACKSMITH:
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, cx, 1, VOXEL_ANVIL);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, cx + 1, 1, VOXEL_STONE_BASALT);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, cx - 1, 1, VOXEL_FORGE);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, bd - 2, VOXEL_CHEST);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, bd - 2, VOXEL_ASH);
    break;

  case VILLAGER_JOB_GUARD:
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, 1, VOXEL_BED);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, 1, VOXEL_IRON);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, 2, VOXEL_STEEL);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, hx, hy + 1, VOXEL_COBBLE); // weapon rack
    break;

  case VILLAGER_JOB_HEALER:
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, cy, VOXEL_TABLE); // table
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 2, cy, VOXEL_PAPER);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 2, cy + 1, VOXEL_CERAMIC);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, 1, VOXEL_BED);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, bd - 2, VOXEL_CHEST);
    break;

  default:
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, 1, 1, VOXEL_BED);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, bw - 2, bd - 2, VOXEL_CHEST);
    settlement_try_prop(world, ox, oy, bw, bd, floor_z, cx, cy, VOXEL_CRAFTING_TABLE);
    break;
  }

  (void)type;
  (void)rng;
}

static void settlement_record_placed(World *world, int ox, int oy, int bw, int bd, int floor_z,
                                     SettlementBuildingType type, uint8_t occupation)
{
  if (!world || world->settlement_placed_count >= WORLD_SETTLEMENT_PLACED_MAX)
    return;
  int i = world->settlement_placed_count++;
  world->settlement_placed[i].ox = (int16_t)ox;
  world->settlement_placed[i].oy = (int16_t)oy;
  world->settlement_placed[i].bw = (int16_t)bw;
  world->settlement_placed[i].bd = (int16_t)bd;
  world->settlement_placed[i].floor_z = (int16_t)floor_z;
  world->settlement_placed[i].building_type = (uint8_t)type;
  world->settlement_placed[i].occupation = occupation;
}

// True when this building role normally sits on a raised stone plinth.
static bool settlement_wants_stone_foundation(SettlementBuildingType type, uint32_t *rng)
{
  if (type == SETTLEMENT_BLDG_HALL || type == SETTLEMENT_BLDG_MANOR ||
      type == SETTLEMENT_BLDG_CASTLE || type == SETTLEMENT_BLDG_TOWER)
    return true;
  if (type == SETTLEMENT_BLDG_HOUSE || type == SETTLEMENT_BLDG_SHOP)
    return (settlement_rng(rng) % 100u) < 55u;
  if (type == SETTLEMENT_BLDG_COTTAGE)
    return (settlement_rng(rng) % 100u) < 30u;
  return false;
}

// Procedural building: one-room shell → multi-room partitions → multi-level stories.
// Returns floor z (top of foundation / walkable slab), or -1 on failure.
int settlement_place_procedural_typed(World *world, int ox, int oy, int bw, int bd,
                                      SettlementBuildingType type, uint32_t *rng)
{
  if (!world || bw < 4 || bd < 4)
    return -1;
  if (ox < 0 || oy < 0 || ox + bw >= (int)world->width || oy + bd >= (int)world->height)
    return -1;

  uint32_t local = rng ? *rng : 0xA5A5A5A5u;
  uint32_t *r = rng ? rng : &local;

  SettlementProcComplexity complexity = settlement_proc_complexity_for_type(type);
  complexity = settlement_clamp_complexity(bw, bd, complexity);
  const SettlementPalette pal = settlement_pick_palette(r);
  const int door_side = settlement_pick_door_side(world, ox, oy, bw, bd, r);
  const uint8_t occupation = settlement_occupation_for_building(type, r);

  int stories = 1;
  if (complexity == SETTLEMENT_PROC_MULTI_LEVEL)
  {
    stories = 2 + (int)(settlement_rng(r) % 2u);
    if (bw < 6 || bd < 6)
      stories = 2;
  }
  const int wall_h = stories * SETTLEMENT_STORY_H;

  int split_x = -1, split_y = -1;
  if (complexity >= SETTLEMENT_PROC_MULTI_ROOM)
  {
    const uint32_t layout = settlement_rng(r) % 3u;
    if (layout == 0 || layout == 2)
      split_x = bw / 2;
    if (layout == 1 || layout == 2)
      split_y = bd / 2;
    if (split_x < 0 && split_y < 0)
      split_x = bw / 2;
    // Keep the exterior door column as a clear passage — don't run a partition through it.
    if (door_side == 0 || door_side == 1)
    {
      if (split_x == bw / 2)
        split_x = (bw / 2 > 2) ? bw / 2 - 1 : bw / 2 + 1;
    }
    else
    {
      if (split_y == bd / 2)
        split_y = (bd / 2 > 2) ? bd / 2 - 1 : bd / 2 + 1;
    }
  }

  int stair_x = 1, stair_y = 1;
  if (complexity == SETTLEMENT_PROC_MULTI_LEVEL)
  {
    if (door_side == 0)
    {
      stair_x = bw - 3;
      stair_y = bd - 3;
    }
    else if (door_side == 1)
    {
      stair_x = 1;
      stair_y = 1;
    }
    else if (door_side == 2)
    {
      stair_x = bw - 3;
      stair_y = 1;
    }
    else
    {
      stair_x = 1;
      stair_y = bd - 3;
    }
    if (stair_x < 1)
      stair_x = 1;
    if (stair_y < 1)
      stair_y = 1;
    if (stair_x + 1 >= bw - 1)
      stair_x = bw - 3;
    if (stair_y + 1 >= bd - 1)
      stair_y = bd - 3;
  }

  int min_ground = (int)world->depth;
  for (int dy = 0; dy < bd; dy++)
  {
    for (int dx = 0; dx < bw; dx++)
    {
      const int gz = settlement_surface_z(world, ox + dx, oy + dy);
      if (gz < 0)
        return -1;
      if (gz < min_ground)
        min_ground = gz;
    }
  }

  // Raised stone/cobble plinth under halls, towers, many houses/shops.
  const bool stone_found = settlement_wants_stone_foundation(type, r);
  const int plinth_h = stone_found ? (1 + (int)(settlement_rng(r) % 2u)) : 0; // 0, 1, or 2
  const VoxelType plinth_mat = VOXEL_STONE;
  const int floor_z = min_ground + (plinth_h > 0 ? plinth_h - 1 : 0);

  const int clear_h = plinth_h + wall_h + bw + 2;
  for (int dy = 0; dy < bd; dy++)
  {
    for (int dx = 0; dx < bw; dx++)
    {
      const int x = ox + dx;
      const int y = oy + dy;
      settlement_clear_column_above(world, x, y, min_ground, clear_h);

      // Stone foundation courses.
      for (int pz = 0; pz < plinth_h; pz++)
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(min_ground + pz), plinth_mat);

      // Floor slab on the top of the plinth (or at grade when no plinth).
      const bool edge = (dx == 0 || dy == 0 || dx == bw - 1 || dy == bd - 1);
      if (plinth_h == 0)
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)floor_z,
                        edge ? pal.foundation : pal.floor);
      else if (!edge)
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)floor_z, pal.floor);

      for (int story = 0; story < stories; story++)
      {
        const int story_base = story * SETTLEMENT_STORY_H;
        if (story > 0)
        {
          const int fz = floor_z + story_base;
          if (!edge && !settlement_in_stairwell(dx, dy, bw, bd, stair_x, stair_y))
          {
            if (fz >= 0 && (uint32_t)fz < world->depth)
              world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)fz, pal.floor);
          }
        }

        for (int z_off = 1; z_off <= SETTLEMENT_STORY_H; z_off++)
        {
          const int z_local = story_base + z_off;
          const int z = floor_z + z_local;
          if (z < 0 || (uint32_t)z >= world->depth)
            continue;

          // Keep the plate course (top of last story) so the roof can sit on it.
          const int z_in_story = z_off;

          if (story == 0 && settlement_is_exterior_door(dx, dy, bw, bd, door_side, z_in_story))
          {
            world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z,
                            settlement_door_voxel(door_side));
            continue;
          }

          if (settlement_is_window(dx, dy, bw, bd, door_side, z_local, story_base))
          {
            world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z,
                            settlement_window_voxel(dx, dy, bw, bd));
            continue;
          }

          if (edge)
          {
            world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, pal.wall);
            continue;
          }

          if (complexity >= SETTLEMENT_PROC_MULTI_ROOM &&
              settlement_is_partition(dx, dy, split_x, split_y) &&
              !settlement_in_stairwell(dx, dy, bw, bd, stair_x, stair_y))
          {
            if (settlement_is_interior_door(dx, dy, bd, bw, split_x, split_y, z_in_story))
            {
              VoxelType idoor = (split_x >= 0 && dx == split_x) ? VOXEL_DOOR : VOXEL_DOOR_NS;
              world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, idoor);
              continue;
            }
            world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, pal.wall);
          }
        }
      }
    }
  }

  if (complexity == SETTLEMENT_PROC_MULTI_LEVEL)
  {
    for (int story = 0; story < stories - 1; story++)
      settlement_place_stairs(world, ox, oy, floor_z, story, stair_x, stair_y, VOXEL_PLANK);
  }

  // Eaves sit on the wall plate (floor_z + wall_h); gables rise to meet the pitch.
  const int plate_z = floor_z + wall_h;
  settlement_place_roof(world, ox, oy, bw, bd, plate_z, pal.roof, pal.wall);

  // Workplace chimneys pierce the roof above the hearth wall (baker / blacksmith).
  if (occupation == (uint8_t)VILLAGER_JOB_BAKER ||
      occupation == (uint8_t)VILLAGER_JOB_BLACKSMITH)
  {
    const int cx = bw / 2;
    const int chim_x = ox + cx;
    const int chim_y = oy + 1;
    const int rise = (cx < bw - 1 - cx) ? cx : (bw - 1 - cx);
    for (int hz = plate_z + rise; hz <= plate_z + rise + 3; hz++)
    {
      if (hz < 0 || (uint32_t)hz >= world->depth)
        break;
      if (chim_x >= 0 && chim_y >= 0 && chim_x < (int)world->width && chim_y < (int)world->height)
        world_set_voxel(world, (uint32_t)chim_x, (uint32_t)chim_y, (uint32_t)hz, VOXEL_BRICK);
    }
  }

  settlement_furnish(world, ox, oy, bw, bd, floor_z, type, occupation, r);

  // Soften a raised plinth into grade with 45° stone wedges (shape×orient, same VoxelType).
  if (plinth_h > 0)
  {
    const VoxelType skirt = pal.foundation;
    // Outside each façade: wedge rises toward the building.
    for (int dx = 0; dx < bw; dx++)
    {
      const int x = ox + dx;
      if (oy - 1 >= 0)
        world_set_voxel_shaped(world, (uint32_t)x, (uint32_t)(oy - 1), (uint32_t)min_ground, skirt,
                               VOXEL_SHAPE_WEDGE, 1); // rise +Y
      if (oy + bd < (int)world->height)
        world_set_voxel_shaped(world, (uint32_t)x, (uint32_t)(oy + bd), (uint32_t)min_ground, skirt,
                               VOXEL_SHAPE_WEDGE, 3); // rise -Y
    }
    for (int dy = 0; dy < bd; dy++)
    {
      const int y = oy + dy;
      if (ox - 1 >= 0)
        world_set_voxel_shaped(world, (uint32_t)(ox - 1), (uint32_t)y, (uint32_t)min_ground, skirt,
                               VOXEL_SHAPE_WEDGE, 0); // rise +X
      if (ox + bw < (int)world->width)
        world_set_voxel_shaped(world, (uint32_t)(ox + bw), (uint32_t)y, (uint32_t)min_ground, skirt,
                               VOXEL_SHAPE_WEDGE, 2); // rise -X
    }
    // Outer corners get tetrahedral pieces so two skirts meet cleanly.
    if (ox - 1 >= 0 && oy - 1 >= 0)
      world_set_voxel_shaped(world, (uint32_t)(ox - 1), (uint32_t)(oy - 1), (uint32_t)min_ground,
                             skirt, VOXEL_SHAPE_CORNER, 0);
    if (ox + bw < (int)world->width && oy - 1 >= 0)
      world_set_voxel_shaped(world, (uint32_t)(ox + bw), (uint32_t)(oy - 1), (uint32_t)min_ground,
                             skirt, VOXEL_SHAPE_CORNER, 3);
    if (ox + bw < (int)world->width && oy + bd < (int)world->height)
      world_set_voxel_shaped(world, (uint32_t)(ox + bw), (uint32_t)(oy + bd), (uint32_t)min_ground,
                             skirt, VOXEL_SHAPE_CORNER, 2);
    if (ox - 1 >= 0 && oy + bd < (int)world->height)
      world_set_voxel_shaped(world, (uint32_t)(ox - 1), (uint32_t)(oy + bd), (uint32_t)min_ground,
                             skirt, VOXEL_SHAPE_CORNER, 1);
  }

  settlement_record_placed(world, ox, oy, bw, bd, floor_z, type, occupation);
  return floor_z;
}

int settlement_place_procedural(World *world, int ox, int oy, int bw, int bd,
                                SettlementProcComplexity complexity, uint32_t *rng)
{
  SettlementBuildingType type = SETTLEMENT_BLDG_BASIC;
  if (complexity == SETTLEMENT_PROC_MULTI_LEVEL)
    type = SETTLEMENT_BLDG_TOWER;
  else if (complexity == SETTLEMENT_PROC_MULTI_ROOM)
    type = SETTLEMENT_BLDG_HOUSE;
  else
    type = SETTLEMENT_BLDG_HUT;
  (void)complexity;
  return settlement_place_procedural_typed(world, ox, oy, bw, bd, type, rng);
}

// Backward-compatible wrapper used by the hunter's shack path.
static int settlement_place_building(World *world, int ox, int oy, int bw, int bd, int bh,
                                     uint32_t *rng)
{
  (void)bh;
  return settlement_place_procedural_typed(world, ox, oy, bw, bd, SETTLEMENT_BLDG_HUT, rng);
}

// Place a typed building with procedural generation sized by role. Fills *out_bw/*out_bd.
static int settlement_place_typed_building(World *world, int ox, int oy, SettlementBuildingType type,
                                           uint32_t *rng, int margin, int *out_bw, int *out_bd)
{
  // Procedural footprint sized by role (large enough for the requested complexity).
  int bw = 5 + (int)(settlement_rng(rng) % 3u);
  int bd = 5 + (int)(settlement_rng(rng) % 3u);
  if (type == SETTLEMENT_BLDG_MANOR || type == SETTLEMENT_BLDG_CASTLE || type == SETTLEMENT_BLDG_HALL)
  {
    bw = 10 + (int)(settlement_rng(rng) % 4u);
    bd = 10 + (int)(settlement_rng(rng) % 4u);
  }
  else if (type == SETTLEMENT_BLDG_TOWER)
  {
    bw = 6 + (int)(settlement_rng(rng) % 2u);
    bd = bw;
  }
  else if (type == SETTLEMENT_BLDG_HOUSE || type == SETTLEMENT_BLDG_SHOP)
  {
    bw = 8 + (int)(settlement_rng(rng) % 3u);
    bd = 8 + (int)(settlement_rng(rng) % 3u);
  }
  else if (type == SETTLEMENT_BLDG_COTTAGE)
  {
    bw = 7 + (int)(settlement_rng(rng) % 2u);
    bd = 7 + (int)(settlement_rng(rng) % 2u);
  }
  else if (type == SETTLEMENT_BLDG_HUT)
  {
    bw = 5 + (int)(settlement_rng(rng) % 2u);
    bd = 5 + (int)(settlement_rng(rng) % 2u);
  }
  if (ox + bw >= (int)world->width - margin || oy + bd >= (int)world->height - margin)
    return -1;

  const int ground = settlement_place_procedural_typed(world, ox, oy, bw, bd, type, rng);
  if (ground >= 0)
  {
    if (out_bw)
      *out_bw = bw;
    if (out_bd)
      *out_bd = bd;
  }
  return ground;
}

static void settlement_place_plaza(World *world, int cx, int cy, int radius)
{
  if (!world || radius < 1)
    return;
  for (int dy = -radius; dy <= radius; dy++)
  {
    for (int dx = -radius; dx <= radius; dx++)
    {
      if (dx * dx + dy * dy > radius * radius)
        continue;
      const int x = cx + dx;
      const int y = cy + dy;
      const int gz = settlement_surface_z(world, x, y);
      if (gz < 0)
        continue;
      settlement_clear_column_above(world, x, y, gz, 6);
      world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)gz, VOXEL_STONE);
    }
  }
}

// Perimeter tier by settlement scale:
//   4–5  wooden post-and-rail fence
//   6    wattle / hurdle fence
//   7–8  stone curtain wall (+ iron gate posts at 8)
//   9    thick stone walls with rampart walk and parapet merlons (fortress)
typedef enum {
  SETTLEMENT_PERIMETER_NONE = 0,
  SETTLEMENT_PERIMETER_WOOD,
  SETTLEMENT_PERIMETER_WATTLE,
  SETTLEMENT_PERIMETER_STONE,
  SETTLEMENT_PERIMETER_FORTRESS
} SettlementPerimeterKind;

static SettlementPerimeterKind settlement_perimeter_kind(int scale)
{
  if (scale >= 9)
    return SETTLEMENT_PERIMETER_FORTRESS;
  if (scale >= 7)
    return SETTLEMENT_PERIMETER_STONE;
  if (scale >= 6)
    return SETTLEMENT_PERIMETER_WATTLE;
  if (scale >= 4)
    return SETTLEMENT_PERIMETER_WOOD;
  return SETTLEMENT_PERIMETER_NONE;
}

static int settlement_perimeter_radius(int scale)
{
  // Keep inside typical stamp worlds (≈96+) while leaving room for buildings.
  if (scale >= 9)
    return 20 + scale; // 29
  if (scale >= 7)
    return 16 + scale * 2;
  if (scale >= 6)
    return 14 + scale * 2;
  return 12 + scale * 2;
}

static bool settlement_perimeter_is_gate(int x, int y, int x0, int y0, int x1, int y1, int half_gap)
{
  const int mx = (x0 + x1) / 2;
  const int my = (y0 + y1) / 2;
  if ((y == y0 || y == y1) && x >= mx - half_gap && x <= mx + half_gap)
    return true;
  if ((x == x0 || x == x1) && y >= my - half_gap && y <= my + half_gap)
    return true;
  return false;
}

static int settlement_edge_dist(int x, int y, int x0, int y0, int x1, int y1)
{
  const int dx = (x - x0 < x1 - x) ? (x - x0) : (x1 - x);
  const int dy = (y - y0 < y1 - y) ? (y - y0) : (y1 - y);
  return (dx < dy) ? dx : dy;
}

// East/west curtain cells: the fence runs along +Y. North/south curtain cells run along +X.
// VOXEL_FENCE / wattle / iron are authored thin in X with detail along Y (yaw 0).
// North/south runs use VOXEL_FENCE_NS, or the same template with yaw = π/2.
static bool settlement_fence_runs_ns(int x, int y, int x0, int y0, int x1, int y1, int thick)
{
  (void)y;
  (void)y0;
  (void)y1;
  return (x - x0 < thick) || (x1 - x < thick);
}

static void settlement_place_column_material(World *world, int x, int y, int gz, int z0, int z1,
                                             VoxelType t)
{
  if (!world || gz < 0)
    return;
  for (int z = z0; z <= z1; z++)
  {
    const int zz = gz + z;
    if (zz < 0 || (uint32_t)zz >= world->depth)
      break;
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)zz, t);
  }
}

// Place a fence column and stamp a shared edge yaw so adjacent segments align in the FP mesh.
static void settlement_place_fence_column(World *world, int x, int y, int gz, int z0, int z1,
                                          VoxelType t, float yaw_rad)
{
  if (!world || gz < 0)
    return;
  for (int z = z0; z <= z1; z++)
  {
    const int zz = gz + z;
    if (zz < 0 || (uint32_t)zz >= world->depth)
      break;
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)zz, t);
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)zz);
    if (v)
      voxel_set_yaw_radians(v, yaw_rad);
  }
}

static void settlement_place_corner_tower(World *world, int cx, int cy, int height)
{
  if (!world)
    return;
  for (int dy = -1; dy <= 1; dy++)
  {
    for (int dx = -1; dx <= 1; dx++)
    {
      const int x = cx + dx;
      const int y = cy + dy;
      const int gz = settlement_surface_z(world, x, y);
      if (gz < 0)
        continue;
      settlement_clear_column_above(world, x, y, gz, height + 4);
      settlement_place_column_material(world, x, y, gz, 1, height + 2, VOXEL_STONE);
      const int top = gz + height + 2;
      if (top + 1 < (int)world->depth)
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(top + 1), VOXEL_RAMPART);
      // Outer corners get merlons.
      if ((dx == -1 || dx == 1) && (dy == -1 || dy == 1) && top + 2 < (int)world->depth)
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(top + 2), VOXEL_PARAPET);
    }
  }
}

static void settlement_place_perimeter(World *world, int x0, int y0, int x1, int y1, int scale)
{
  if (!world || x1 - x0 < 6 || y1 - y0 < 6)
    return;

  const SettlementPerimeterKind kind = settlement_perimeter_kind(scale);
  if (kind == SETTLEMENT_PERIMETER_NONE)
    return;

  const int thick = (kind == SETTLEMENT_PERIMETER_FORTRESS) ? 2 : 1;
  const int half_gap = (kind == SETTLEMENT_PERIMETER_FORTRESS) ? 2 : (kind >= SETTLEMENT_PERIMETER_STONE ? 1 : 0);
  int height = 2;
  if (kind == SETTLEMENT_PERIMETER_WOOD)
    height = 2;
  else if (kind == SETTLEMENT_PERIMETER_WATTLE)
    height = 3;
  else if (kind == SETTLEMENT_PERIMETER_STONE)
    height = 4 + (scale - 7);
  else
    height = 7; // fortress curtain

  for (int y = y0; y <= y1; y++)
  {
    for (int x = x0; x <= x1; x++)
    {
      const int ed = settlement_edge_dist(x, y, x0, y0, x1, y1);
      if (ed >= thick)
        continue;

      const int mx = (x0 + x1) / 2;
      const int my = (y0 + y1) / 2;
      const bool gate_post =
          kind >= SETTLEMENT_PERIMETER_STONE && scale >= 8 &&
          (((y == y0 || y == y1) && (x == mx - half_gap - 1 || x == mx + half_gap + 1)) ||
           ((x == x0 || x == x1) && (y == my - half_gap - 1 || y == my + half_gap + 1)));

      // Gate openings on mid-sides (outer ring and matching thick cells).
      if (half_gap >= 0 && !gate_post)
      {
        if (settlement_perimeter_is_gate(x, y, x0, y0, x1, y1, half_gap) && ed == 0)
          continue;
        if (half_gap > 0)
        {
          if ((y <= y0 + thick - 1 || y >= y1 - thick + 1) && x >= mx - half_gap &&
              x <= mx + half_gap)
            continue;
          if ((x <= x0 + thick - 1 || x >= x1 - thick + 1) && y >= my - half_gap &&
              y <= my + half_gap)
            continue;
        }
      }

      const int gz = settlement_surface_z(world, x, y);
      if (gz < 0)
        continue;
      settlement_clear_column_above(world, x, y, gz, height + 4);

      // Shared orientation for this edge so neighbouring fence cells face the same way.
      const bool runs_ns = settlement_fence_runs_ns(x, y, x0, y0, x1, y1, thick);
      const float fence_yaw = runs_ns ? 0.0f : ((float)M_PI * 0.5f);

      if (gate_post)
      {
        settlement_place_fence_column(world, x, y, gz, 1, height, VOXEL_FENCE_IRON, fence_yaw);
        continue;
      }

      if (kind == SETTLEMENT_PERIMETER_WOOD)
      {
        // FENCE is authored for NS runs; FENCE_NS is the pre-rotated EW mesh (yaw stays 0).
        const VoxelType fence = runs_ns ? VOXEL_FENCE : VOXEL_FENCE_NS;
        settlement_place_fence_column(world, x, y, gz, 1, height, fence, 0.0f);
      }
      else if (kind == SETTLEMENT_PERIMETER_WATTLE)
      {
        settlement_place_fence_column(world, x, y, gz, 1, height, VOXEL_FENCE_WATTLE, fence_yaw);
      }
      else
      {
        VoxelType body = VOXEL_STONE;
        if (kind == SETTLEMENT_PERIMETER_STONE && ((x + y) & 1) != 0)
          body = VOXEL_COBBLE;
        settlement_place_column_material(world, x, y, gz, 1, height, body);

        if (kind == SETTLEMENT_PERIMETER_FORTRESS)
        {
          const int walk_z = gz + height + 1;
          const int parapet_z = gz + height + 2;
          // Walkway on the inner ring of the thick wall.
          if (ed == thick - 1 && walk_z < (int)world->depth)
            world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)walk_z, VOXEL_RAMPART);
          // Alternating merlons on the outer face.
          if (ed == 0 && parapet_z < (int)world->depth && ((x + y) & 1) == 0)
            world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)parapet_z, VOXEL_PARAPET);
        }
      }
    }
  }

  if (kind == SETTLEMENT_PERIMETER_FORTRESS)
  {
    settlement_place_corner_tower(world, x0, y0, height);
    settlement_place_corner_tower(world, x0, y1, height);
    settlement_place_corner_tower(world, x1, y0, height);
    settlement_place_corner_tower(world, x1, y1, height);
  }
}

// Prefer the flattest local basin so settlements sit on larger flat areas.
static void settlement_pick_flat_center(const World *world, int margin, int *out_cx, int *out_cy)
{
  const int W = (int)world->width;
  const int H = (int)world->height;
  int best_x = W / 2;
  int best_y = H / 2;
  int best_relief = INT_MAX;
  int best_dist = INT_MAX;
  const int step = 3;
  const int sample_r = 3;
  for (int y = margin; y < H - margin; y += step)
  {
    for (int x = margin; x < W - margin; x += step)
    {
      int min_z = INT_MAX, max_z = INT_MIN;
      int samples = 0;
      for (int dy = -sample_r; dy <= sample_r; dy++)
      {
        for (int dx = -sample_r; dx <= sample_r; dx++)
        {
          const int gz = settlement_surface_z(world, x + dx, y + dy);
          if (gz < 0)
            continue;
          if (gz < min_z)
            min_z = gz;
          if (gz > max_z)
            max_z = gz;
          samples++;
        }
      }
      if (samples < 8)
        continue;
      const int relief = max_z - min_z;
      const int dist = settlement_iabs(x - W / 2) + settlement_iabs(y - H / 2);
      if (relief < best_relief || (relief == best_relief && dist < best_dist))
      {
        best_relief = relief;
        best_dist = dist;
        best_x = x;
        best_y = y;
      }
    }
  }
  if (out_cx)
    *out_cx = best_x;
  if (out_cy)
    *out_cy = best_y;
}

static void settlement_plant_yard_decorations(World *world, int cx, int cy, int scale, uint32_t *rng)
{
  if (!world || scale < 1)
    return;
  const int n = 6 + scale * 3;
  for (int i = 0; i < n; i++)
  {
    const int x = cx + (int)(settlement_rng(rng) % 41u) - 20;
    const int y = cy + (int)(settlement_rng(rng) % 41u) - 20;
    if (x < 2 || y < 2 || x >= (int)world->width - 2 || y >= (int)world->height - 2)
      continue;
    // Keep the plaza clear.
    const int dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy < 16)
      continue;
    const int gz = settlement_surface_z(world, x, y);
    if (gz < 0 || gz + 1 >= (int)world->depth)
      continue;
    const Voxel *ground = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)gz);
    if (!ground || settlement_is_pavement(ground->type) || settlement_is_plant(ground->type))
      continue;
    const Voxel *above = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(gz + 1));
    if (above && above->type != VOXEL_AIR)
      continue;

    VoxelType bush = VOXEL_BUSH;
    const uint32_t pick = settlement_rng(rng) % 5u;
    if (pick == 1)
      bush = VOXEL_BUSH_FERN;
    else if (pick == 2)
      bush = VOXEL_GRASS_TALL;
    else if (pick == 3)
      bush = VOXEL_BUSH_BLUEBERRY;

    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(gz + 1), bush);
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(gz + 1));
    if (!v)
      continue;
    const float yaw =
        decoration_yaw_blend(settlement_rng(rng), x, y, cx, cy, 0.65f);
    voxel_set_yaw_radians(v, yaw);
  }
}

bool settlement_stamp(World *world, int scale, const char *seed)
{
  if (!world || !world->voxels || scale < 1)
    return false;
  if (scale > 9)
    scale = 9;

  uint32_t rng = 0xC0FFEEu;
  if (seed)
  {
    for (const char *p = seed; *p; p++)
      rng = rng * 31u + (uint8_t)*p;
  }
  rng ^= (uint32_t)world->universe_x * 0x9e3779b1u;
  rng ^= (uint32_t)world->universe_y * 0x85ebca6bu;

  const int count = settlement_building_count(scale);
  const int margin = 8;
  if ((int)world->width < margin * 2 + 10 || (int)world->height < margin * 2 + 10)
    return false;

  int cx = (int)world->width / 2;
  int cy = (int)world->height / 2;
  settlement_pick_flat_center(world, margin + 10, &cx, &cy);
  world->settlement_scale = scale;
  world->settlement_has_anchor = false;
  world->settlement_has_note = false;
  world->settlement_placed_count = 0;
  world->settlement_has_town_center = true;
  world->settlement_town_cx = (int16_t)cx;
  world->settlement_town_cy = (int16_t)cy;

  // Home-drop: one hunter's shack (quest objective) with the note inside.
  if (settlement_is_home_drop((int)world->universe_x, (int)world->universe_y) && scale == 1)
  {
    const int ox = cx - 3;
    const int oy = cy + 5;
    const int bw = 6, bd = 6, bh = 4;
    const int ground = settlement_place_building(world, ox, oy, bw, bd, bh, &rng);
    if (ground < 0)
      return false;

    settlement_record_anchor(world, ox, oy, bw, bd, ground);

    // Desk + candle inside the shack: the hunter's note the player must find.
    const int nx = ox + bw / 2;
    const int ny = oy + bd / 2;
    const int nz = ground + 1;
    if (nx > 0 && ny > 0 && nz > 0 && (uint32_t)nx < world->width &&
        (uint32_t)ny < world->height && (uint32_t)(nz + 1) < world->depth)
    {
      world_set_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz, VOXEL_PLANK);
      world_set_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)(nz + 1), VOXEL_CANDLE);
      // Window pane beside the desk so glass reads as a settlement material.
      if (nx + 1 < (int)world->width)
        world_set_voxel(world, (uint32_t)(nx + 1), (uint32_t)ny, (uint32_t)(nz + 1), VOXEL_GLASS);
      // Cobble stoop outside the door line (south of desk).
      if (ny + 2 < (int)world->height)
        world_set_voxel(world, (uint32_t)nx, (uint32_t)(ny + 2), (uint32_t)(nz - 1), VOXEL_COBBLE);
      settlement_record_note(world, nx, ny, nz + 1);
    }

    printf("[settlement] hunter's shack at (%d,%d,%d) universe (0,0)\n",
           world->settlement_anchor_x, world->settlement_anchor_y, world->settlement_anchor_z);
    return true;
  }

  if (scale >= 5)
    settlement_place_plaza(world, cx, cy, 3 + scale / 2);

  // Place buildings on a jittered ring / grid that densifies with scale.
  const int span = 18 + scale * 6;
  int placed = 0;
  for (int attempt = 0; attempt < count * 10 && placed < count; attempt++)
  {
    const SettlementBuildingType btype =
        settlement_pick_building_type(scale, placed, count, &rng);

    // Landmarks sit near the plaza; later buildings fill outward.
    int ox, oy;
    if (placed == 0 && scale >= 5)
    {
      ox = cx - 8;
      oy = cy - 8;
    }
    else
    {
      ox = margin + (int)(settlement_rng(&rng) % (uint32_t)(world->width - 2 * margin - 16));
      oy = margin + (int)(settlement_rng(&rng) % (uint32_t)(world->height - 2 * margin - 16));
    }

    if (ox < margin || oy < margin)
      continue;

    const int dist = abs(ox + 4 - cx) + abs(oy + 4 - cy);
    const int max_dist = span + placed * 2;
    if (placed > 0 && dist > max_dist)
      continue;

    int bw = 0, bd = 0;
    const int ground =
        settlement_place_typed_building(world, ox, oy, btype, &rng, margin, &bw, &bd);
    if (ground < 0 || bw < 1 || bd < 1)
      continue;

    if (!world->settlement_has_anchor)
      settlement_record_anchor(world, ox, oy, bw, bd, ground);
    placed++;
  }

  if (settlement_perimeter_kind(scale) != SETTLEMENT_PERIMETER_NONE)
  {
    const int wall_r = settlement_perimeter_radius(scale);
    const int x0 = cx - wall_r;
    const int y0 = cy - wall_r;
    const int x1 = cx + wall_r;
    const int y1 = cy + wall_r;
    if (x0 > margin && y0 > margin && x1 < (int)world->width - margin &&
        y1 < (int)world->height - margin)
      settlement_place_perimeter(world, x0, y0, x1, y1, scale);
  }

  settlement_plant_yard_decorations(world, cx, cy, scale, &rng);

  printf("[settlement] stamped scale=%d buildings=%d/%d at universe (%llu,%llu) center=(%d,%d)\n",
         scale, placed, count, (unsigned long long)world->universe_x,
         (unsigned long long)world->universe_y, cx, cy);
  return placed > 0;
}

// ---- Inter-settlement roads (hub tree + hub mesh) ----

#define SETTLEMENT_ROAD_LINK_MIN 3 // hamlets+ join the hub tree
#define SETTLEMENT_ROAD_HUB_MIN 6  // peer mesh among large hubs
#define SETTLEMENT_ROAD_MAX_DIST 28
// How far a routed corridor may leave the A↔B bounding box while contouring.
#define SETTLEMENT_ROAD_ROUTE_PAD 12
#define SETTLEMENT_ROAD_MAX_PATH 192
#define SETTLEMENT_ROAD_COST_INF 1000000000
#define SETTLEMENT_ROAD_ROUTE_CACHE 64
// Extra terrain cost on a straight spoke that triggers A* contouring.
#define SETTLEMENT_ROAD_REROUTE_PENALTY 900

const char *settlement_road_type_name(SettlementRoadType type)
{
  switch (type)
  {
  case SETTLEMENT_ROAD_PATH: return "path";
  case SETTLEMENT_ROAD_DIRT: return "dirt";
  case SETTLEMENT_ROAD_GRAVEL: return "gravel";
  case SETTLEMENT_ROAD_COBBLE: return "cobble";
  case SETTLEMENT_ROAD_HIGHWAY: return "highway";
  case SETTLEMENT_ROAD_NONE:
  default: return "none";
  }
}

VoxelType settlement_road_voxel(SettlementRoadType type)
{
  switch (type)
  {
  case SETTLEMENT_ROAD_PATH: return VOXEL_SOIL;
  case SETTLEMENT_ROAD_DIRT: return VOXEL_SOIL_LOAM;
  case SETTLEMENT_ROAD_GRAVEL: return VOXEL_GRAVEL;
  case SETTLEMENT_ROAD_COBBLE: return VOXEL_COBBLE;
  case SETTLEMENT_ROAD_HIGHWAY: return VOXEL_BRICK;
  case SETTLEMENT_ROAD_NONE:
  default: return VOXEL_AIR;
  }
}

int settlement_road_width(SettlementRoadType type)
{
  switch (type)
  {
  case SETTLEMENT_ROAD_PATH: return 1;
  case SETTLEMENT_ROAD_DIRT: return 2;
  case SETTLEMENT_ROAD_GRAVEL: return 3;
  case SETTLEMENT_ROAD_COBBLE: return 4;
  case SETTLEMENT_ROAD_HIGHWAY: return 5;
  case SETTLEMENT_ROAD_NONE:
  default: return 0;
  }
}

int settlement_road_thickness(SettlementRoadType type)
{
  // 1..5 layers of pavement downward from the surface (subvoxel-depth grade).
  return settlement_road_width(type);
}

int settlement_road_half_width(SettlementRoadType type)
{
  const int w = settlement_road_width(type);
  return w > 0 ? (w / 2) : 0;
}

static SettlementRoadType settlement_road_type_from_scales(int sa, int sb)
{
  const int weak = (sa < sb) ? sa : sb;
  if (weak >= 8)
    return SETTLEMENT_ROAD_HIGHWAY;
  if (weak >= 7)
    return SETTLEMENT_ROAD_COBBLE;
  if (weak >= 5)
    return SETTLEMENT_ROAD_GRAVEL;
  if (weak >= 4)
    return SETTLEMENT_ROAD_DIRT;
  if (weak >= SETTLEMENT_ROAD_LINK_MIN)
    return SETTLEMENT_ROAD_PATH;
  return SETTLEMENT_ROAD_NONE;
}

static bool settlement_is_road_hub(int gx, int gy)
{
  return universe_settlement_scale(gx, gy) >= SETTLEMENT_ROAD_HUB_MIN;
}

static bool settlement_is_road_node(int gx, int gy)
{
  return universe_settlement_scale(gx, gy) >= SETTLEMENT_ROAD_LINK_MIN;
}

// Parent toward the main hub: nearest linked settlement that is strictly closer to the hub.
static bool settlement_hub_parent(int gx, int gy, int *out_x, int *out_y)
{
  int hx = 0, hy = 0;
  if (!universe_main_hub(&hx, &hy))
    return false;
  if (gx == hx && gy == hy)
    return false;
  if (!settlement_is_road_node(gx, gy) && !settlement_is_road_hub(gx, gy))
    return false;

  const int my_dh = settlement_manhattan(gx, gy, hx, hy);
  int best_score = INT_MAX;
  int best_x = 0, best_y = 0;
  bool found = false;

  // Prefer linking directly to the hub when in range.
  const int d_hub = settlement_manhattan(gx, gy, hx, hy);
  if (d_hub > 0 && d_hub <= SETTLEMENT_ROAD_MAX_DIST && universe_settlement_scale(hx, hy) >= 5)
  {
    found = true;
    best_score = d_hub;
    best_x = hx;
    best_y = hy;
  }

  for (int dy = -SETTLEMENT_ROAD_MAX_DIST; dy <= SETTLEMENT_ROAD_MAX_DIST; dy++)
  {
    for (int dx = -SETTLEMENT_ROAD_MAX_DIST; dx <= SETTLEMENT_ROAD_MAX_DIST; dx++)
    {
      if (dx == 0 && dy == 0)
        continue;
      const int d = settlement_manhattan(0, 0, dx, dy);
      if (d > SETTLEMENT_ROAD_MAX_DIST)
        continue;
      const int nx = gx + dx;
      const int ny = gy + dy;
      if (nx == hx && ny == hy)
        continue;
      const int ns = universe_settlement_scale(nx, ny);
      if (ns < SETTLEMENT_ROAD_LINK_MIN)
        continue;
      const int their_dh = settlement_manhattan(nx, ny, hx, hy);
      if (their_dh >= my_dh)
        continue;
      // Prefer nodes closer to the hub, then shorter spokes, then larger peers.
      const int score = their_dh * 1000 + d * 10 - ns;
      if (!found || score < best_score ||
          (score == best_score && (nx < best_x || (nx == best_x && ny < best_y))))
      {
        found = true;
        best_score = score;
        best_x = nx;
        best_y = ny;
      }
    }
  }
  if (!found)
    return false;
  if (out_x)
    *out_x = best_x;
  if (out_y)
    *out_y = best_y;
  return true;
}

// Nearest other hub within ROAD_MAX_DIST (Manhattan). Tie-break: closer, then higher scale,
// then lexicographically smaller (gx,gy).
static bool settlement_nearest_hub(int gx, int gy, int *out_x, int *out_y, int *out_scale)
{
  int best_d = SETTLEMENT_ROAD_MAX_DIST + 1;
  int best_x = 0, best_y = 0, best_s = 0;
  bool found = false;
  for (int dy = -SETTLEMENT_ROAD_MAX_DIST; dy <= SETTLEMENT_ROAD_MAX_DIST; dy++)
  {
    for (int dx = -SETTLEMENT_ROAD_MAX_DIST; dx <= SETTLEMENT_ROAD_MAX_DIST; dx++)
    {
      if (dx == 0 && dy == 0)
        continue;
      const int d = settlement_manhattan(0, 0, dx, dy);
      if (d > SETTLEMENT_ROAD_MAX_DIST || d >= best_d)
        continue;
      const int nx = gx + dx;
      const int ny = gy + dy;
      const int s = universe_settlement_scale(nx, ny);
      if (s < SETTLEMENT_ROAD_HUB_MIN)
        continue;
      if (!found || d < best_d ||
          (d == best_d && (s > best_s ||
                           (s == best_s && (nx < best_x || (nx == best_x && ny < best_y))))))
      {
        found = true;
        best_d = d;
        best_x = nx;
        best_y = ny;
        best_s = s;
      }
    }
  }
  if (!found)
    return false;
  if (out_x)
    *out_x = best_x;
  if (out_y)
    *out_y = best_y;
  if (out_scale)
    *out_scale = best_s;
  return true;
}

// Directed edge: hub-tree parent link or peer-hub mesh spoke.
static bool settlement_road_edge(int ax, int ay, int bx, int by, SettlementRoadType *out_type)
{
  if (ax == bx && ay == by)
    return false;
  const int sa = universe_settlement_scale(ax, ay);
  const int sb = universe_settlement_scale(bx, by);
  if (sa < SETTLEMENT_ROAD_LINK_MIN && sb < SETTLEMENT_ROAD_LINK_MIN)
    return false;
  const int d = settlement_manhattan(ax, ay, bx, by);
  if (d > SETTLEMENT_ROAD_MAX_DIST || d <= 0)
    return false;

  // Hub-tree: B is A's parent toward the main hub.
  int px = 0, py = 0;
  if (settlement_hub_parent(ax, ay, &px, &py) && px == bx && py == by)
  {
    if (out_type)
      *out_type = settlement_road_type_from_scales(sa, sb);
    return true;
  }

  // Peer mesh among hubs (scale >= 6): nearest-hub spoke.
  if (sa >= SETTLEMENT_ROAD_HUB_MIN && sb >= SETTLEMENT_ROAD_HUB_MIN)
  {
    int nx = 0, ny = 0;
    if (settlement_nearest_hub(ax, ay, &nx, &ny, NULL) && nx == bx && ny == by)
    {
      if (out_type)
        *out_type = settlement_road_type_from_scales(sa, sb);
      return true;
    }
  }
  return false;
}

bool universe_settlements_linked(int ax, int ay, int bx, int by, SettlementRoadType *out_type)
{
  SettlementRoadType t = SETTLEMENT_ROAD_NONE;
  if (settlement_road_edge(ax, ay, bx, by, &t) || settlement_road_edge(bx, by, ax, ay, &t))
  {
    if (out_type)
      *out_type = t;
    return true;
  }
  return false;
}

// Coarse climate proxies (no World required) for corridor costing. Patchy hashes keep
// wetlands / volcanic belts / copses from being uniform sheets across the map.
static float settlement_proxy_moisture(int gx, int gy)
{
  const uint32_t continental = settlement_hash(gx / 14, gy / 14, 0xa3b17e55u);
  const uint32_t regional = settlement_hash(gx / 6, gy / 6, 0x51eed01eu);
  const float c = (float)(continental & 1023u) / 1023.0f;
  const float r = (float)(regional & 1023u) / 1023.0f;
  return c * 0.7f + r * 0.3f;
}

static float settlement_proxy_volcanic(int gx, int gy)
{
  const uint32_t continental = settlement_hash(gx / 16, gy / 16, 0x5eedf00du);
  const uint32_t regional = settlement_hash(gx / 7, gy / 7, 0x701c4a07u);
  const float c = (float)(continental & 1023u) / 1023.0f;
  const float r = (float)(regional & 1023u) / 1023.0f;
  return c * 0.75f + r * 0.25f;
}

static float settlement_proxy_forest(int gx, int gy)
{
  const float m = settlement_proxy_moisture(gx, gy);
  const float h = settlement_proxy_height(gx, gy);
  float dens = m * (1.0f - fabsf(h - 0.45f) * 2.0f);
  if (dens < 0.0f)
    dens = 0.0f;
  if (dens > 1.0f)
    dens = 1.0f;
  const float patch = (float)(settlement_hash(gx, gy, 0x7ee001u) & 255u) / 255.0f;
  return dens * (0.35f + 0.65f * patch);
}

// Cost to step from (fx,fy) onto (tx,ty). Favours gentle grades; soft-avoids water /
// lava / forest provinces so roads contour instead of cutting straight through.
static int settlement_road_step_cost(int fx, int fy, int tx, int ty, bool diagonal)
{
  const float hf = settlement_proxy_height(fx, fy);
  const float ht = settlement_proxy_height(tx, ty);
  const float dh = fabsf(ht - hf);
  int cost = diagonal ? 14 : 10;

  // Slope dominates: contour around ridges rather than climb them.
  cost += (int)(dh * 450.0f);
  if (dh > 0.10f)
    cost += 350;
  if (dh > 0.16f)
    cost += 1800;
  if (dh > 0.24f)
    cost += 8000;

  const float moist = settlement_proxy_moisture(tx, ty);
  if (moist > 0.62f && ht < 0.38f)
    cost += 700;
  if (moist > 0.78f && ht < 0.30f)
    cost += 4500; // standing water / river bed

  const float volc = settlement_proxy_volcanic(tx, ty);
  if (volc > 0.52f)
    cost += 650;
  if (volc > 0.72f)
    cost += 4000; // magma province

  cost += (int)(settlement_proxy_forest(tx, ty) * 95.0f);
  return cost;
}

static bool settlement_road_in_route_bounds(int cx, int cy, int ax, int ay, int bx, int by)
{
  const int minx = (ax < bx ? ax : bx) - SETTLEMENT_ROAD_ROUTE_PAD;
  const int maxx = (ax > bx ? ax : bx) + SETTLEMENT_ROAD_ROUTE_PAD;
  const int miny = (ay < by ? ay : by) - SETTLEMENT_ROAD_ROUTE_PAD;
  const int maxy = (ay > by ? ay : by) + SETTLEMENT_ROAD_ROUTE_PAD;
  return cx >= minx && cx <= maxx && cy >= miny && cy <= maxy;
}

// Supercover Bresenham fallback when A* cannot reach within the search window.
static int settlement_road_bresenham_fill(int ax, int ay, int bx, int by,
                                          int16_t *out_xy, int max_len)
{
  if (!out_xy || max_len < 1)
    return 0;
  int x = ax, y = ay;
  const int dx = (bx > ax) ? (bx - ax) : (ax - bx);
  const int dy = (by > ay) ? (by - ay) : (ay - by);
  const int sx = (ax < bx) ? 1 : (ax > bx ? -1 : 0);
  const int sy = (ay < by) ? 1 : (ay > by ? -1 : 0);
  int err = dx - dy;
  int n = 0;
  for (;;)
  {
    if (n + 1 >= max_len)
      break;
    out_xy[n * 2] = (int16_t)x;
    out_xy[n * 2 + 1] = (int16_t)y;
    n++;
    if (x == bx && y == by)
      break;
    const int e2 = err * 2;
    if (e2 > -dy)
    {
      err -= dy;
      x += sx;
    }
    if (e2 < dx)
    {
      err += dx;
      y += sy;
    }
  }
  return n;
}

typedef struct
{
  int ax, ay, bx, by;
  int len;
  int16_t xy[SETTLEMENT_ROAD_MAX_PATH * 2];
  bool valid;
} SettlementRoadRoute;

static SettlementRoadRoute s_road_route_cache[SETTLEMENT_ROAD_ROUTE_CACHE];
static int s_road_route_cache_clock;

// A* on the universe cell grid. 8-neighborhood so corridors can contour hills.
// Writes interleaved (x,y) into out_xy; returns cell count (0 on failure).
// Flat / clear spokes keep the Bresenham line (fast); rough terrain triggers A*.
static int settlement_road_route_compute(int ax, int ay, int bx, int by,
                                         int16_t *out_xy, int max_len)
{
  if (!out_xy || max_len < 2)
    return 0;
  if (ax == bx && ay == by)
  {
    out_xy[0] = (int16_t)ax;
    out_xy[1] = (int16_t)ay;
    return 1;
  }

  const int straight_len = settlement_road_bresenham_fill(ax, ay, bx, by, out_xy, max_len);
  if (straight_len < 2)
    return straight_len;

  // Score the straight spoke; skip A* when the grade / obstacles are mild.
  int penalty = 0;
  for (int i = 1; i < straight_len; i++)
  {
    const int x0 = out_xy[(i - 1) * 2], y0 = out_xy[(i - 1) * 2 + 1];
    const int x1 = out_xy[i * 2], y1 = out_xy[i * 2 + 1];
    const bool diag = (x0 != x1 && y0 != y1);
    const int base = diag ? 14 : 10;
    const int step = settlement_road_step_cost(x0, y0, x1, y1, diag);
    if (step > base)
      penalty += step - base;
  }
  if (penalty < SETTLEMENT_ROAD_REROUTE_PENALTY)
    return straight_len;

  const int minx = (ax < bx ? ax : bx) - SETTLEMENT_ROAD_ROUTE_PAD;
  const int maxx = (ax > bx ? ax : bx) + SETTLEMENT_ROAD_ROUTE_PAD;
  const int miny = (ay < by ? ay : by) - SETTLEMENT_ROAD_ROUTE_PAD;
  const int maxy = (ay > by ? ay : by) + SETTLEMENT_ROAD_ROUTE_PAD;
  const int W = maxx - minx + 1;
  const int H = maxy - miny + 1;
  if (W < 1 || H < 1 || W > 96 || H > 96)
    return straight_len;

  const size_t ncells = (size_t)W * (size_t)H;
  int *g = (int *)malloc(ncells * sizeof(int));
  int *parent = (int *)malloc(ncells * sizeof(int));
  uint8_t *closed = (uint8_t *)calloc(ncells, 1);
  uint8_t *in_open = (uint8_t *)calloc(ncells, 1);
  int *open_i = (int *)malloc(ncells * sizeof(int));
  if (!g || !parent || !closed || !in_open || !open_i)
  {
    free(g);
    free(parent);
    free(closed);
    free(in_open);
    free(open_i);
    return straight_len;
  }

  for (size_t i = 0; i < ncells; i++)
  {
    g[i] = SETTLEMENT_ROAD_COST_INF;
    parent[i] = -1;
  }

  const int sx = ax - minx, sy = ay - miny;
  const int gx = bx - minx, gy = by - miny;
  const int sidx = sy * W + sx;
  const int gidx = gy * W + gx;
  g[sidx] = 0;
  int open_n = 0;
  open_i[open_n++] = sidx;
  in_open[sidx] = 1;

  static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                 {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

  bool found = false;
  while (open_n > 0)
  {
    int best_oi = 0;
    int best_f = SETTLEMENT_ROAD_COST_INF;
    for (int oi = 0; oi < open_n; oi++)
    {
      const int idx = open_i[oi];
      const int cx = idx % W, cy = idx / W;
      const int h = (settlement_iabs(cx - gx) + settlement_iabs(cy - gy)) * 10;
      const int f = g[idx] + h;
      if (f < best_f || (f == best_f && idx < open_i[best_oi]))
      {
        best_f = f;
        best_oi = oi;
      }
    }
    const int cur = open_i[best_oi];
    open_i[best_oi] = open_i[--open_n];
    in_open[cur] = 0;
    if (closed[cur])
      continue;
    closed[cur] = 1;
    if (cur == gidx)
    {
      found = true;
      break;
    }

    const int cx = cur % W, cy = cur / W;
    const int wx = cx + minx, wy = cy + miny;
    for (int d = 0; d < 8; d++)
    {
      const int nx = cx + dirs[d][0];
      const int ny = cy + dirs[d][1];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H)
        continue;
      const int nidx = ny * W + nx;
      if (closed[nidx])
        continue;
      const int nwx = nx + minx, nwy = ny + miny;
      const bool diag = (dirs[d][0] != 0 && dirs[d][1] != 0);
      const int step = settlement_road_step_cost(wx, wy, nwx, nwy, diag);
      const int ng = g[cur] + step;
      if (ng >= g[nidx])
        continue;
      g[nidx] = ng;
      parent[nidx] = cur;
      if (!in_open[nidx])
      {
        open_i[open_n++] = nidx;
        in_open[nidx] = 1;
      }
    }
  }

  int path_len = 0;
  if (found)
  {
    int stack[SETTLEMENT_ROAD_MAX_PATH];
    int sn = 0;
    for (int idx = gidx; idx >= 0 && sn < SETTLEMENT_ROAD_MAX_PATH; idx = parent[idx])
      stack[sn++] = idx;
    path_len = sn;
    if (path_len > max_len)
      path_len = max_len;
    for (int i = 0; i < path_len; i++)
    {
      const int idx = stack[sn - 1 - i];
      out_xy[i * 2] = (int16_t)(minx + (idx % W));
      out_xy[i * 2 + 1] = (int16_t)(miny + (idx / W));
    }
  }

  free(g);
  free(parent);
  free(closed);
  free(in_open);
  free(open_i);

  if (!found || path_len < 2)
    return straight_len;
  return path_len;
}

static const SettlementRoadRoute *settlement_road_route_cached(int ax, int ay, int bx, int by)
{
  // Canonicalize endpoint order so A→B and B→A share one cache entry.
  int eax = ax, eay = ay, ebx = bx, eby = by;
  if (ebx < eax || (ebx == eax && eby < eay))
  {
    eax = bx;
    eay = by;
    ebx = ax;
    eby = ay;
  }
  for (int i = 0; i < SETTLEMENT_ROAD_ROUTE_CACHE; i++)
  {
    SettlementRoadRoute *c = &s_road_route_cache[i];
    if (c->valid && c->ax == eax && c->ay == eay && c->bx == ebx && c->by == eby)
      return c;
  }
  const int slot = s_road_route_cache_clock++ % SETTLEMENT_ROAD_ROUTE_CACHE;
  SettlementRoadRoute *c = &s_road_route_cache[slot];
  c->ax = eax;
  c->ay = eay;
  c->bx = ebx;
  c->by = eby;
  c->len = settlement_road_route_compute(eax, eay, ebx, eby, c->xy, SETTLEMENT_ROAD_MAX_PATH);
  c->valid = c->len > 0;
  return c->valid ? c : NULL;
}

// True if (cx,cy) lies on the terrain-aware corridor from A to B.
static bool settlement_cell_on_road_segment(int cx, int cy, int ax, int ay, int bx, int by)
{
  if ((cx == ax && cy == ay) || (cx == bx && cy == by))
    return true;
  if (!settlement_road_in_route_bounds(cx, cy, ax, ay, bx, by))
    return false;
  const SettlementRoadRoute *route = settlement_road_route_cached(ax, ay, bx, by);
  if (!route)
    return false;
  for (int i = 0; i < route->len; i++)
  {
    if ((int)route->xy[i * 2] == cx && (int)route->xy[i * 2 + 1] == cy)
      return true;
  }
  return false;
}

// Previous / next cells along the routed corridor through (gx,gy).
static bool settlement_road_route_neighbors(int gx, int gy, int ax, int ay, int bx, int by,
                                           int *out_prev_x, int *out_prev_y,
                                           int *out_next_x, int *out_next_y)
{
  const SettlementRoadRoute *route = settlement_road_route_cached(ax, ay, bx, by);
  if (!route || route->len < 1)
    return false;

  int idx = -1;
  for (int i = 0; i < route->len; i++)
  {
    if ((int)route->xy[i * 2] == gx && (int)route->xy[i * 2 + 1] == gy)
    {
      idx = i;
      break;
    }
  }
  if (idx < 0)
    return false;

  // Cache stores canonical low→high; flip traversal if the spoke runs the other way.
  const bool forward = (route->ax == ax && route->ay == ay);
  int prev_i, next_i;
  if (forward)
  {
    prev_i = idx - 1;
    next_i = idx + 1;
  }
  else
  {
    prev_i = idx + 1;
    next_i = idx - 1;
  }

  int px = ax, py = ay, nx = bx, ny = by;
  if (prev_i >= 0 && prev_i < route->len)
  {
    px = route->xy[prev_i * 2];
    py = route->xy[prev_i * 2 + 1];
  }
  if (next_i >= 0 && next_i < route->len)
  {
    nx = route->xy[next_i * 2];
    ny = route->xy[next_i * 2 + 1];
  }
  if (out_prev_x)
    *out_prev_x = px;
  if (out_prev_y)
    *out_prev_y = py;
  if (out_next_x)
    *out_next_x = nx;
  if (out_next_y)
    *out_next_y = ny;
  return true;
}

bool universe_settlement_road(int gx, int gy, SettlementRoadType *out_type,
                              int *out_from_gx, int *out_from_gy,
                              int *out_to_gx, int *out_to_gy)
{
  SettlementRoadType best = SETTLEMENT_ROAD_NONE;
  int best_ax = 0, best_ay = 0, best_bx = 0, best_by = 0;

  // Candidate nodes whose spoke (hub-tree parent or peer-hub) might cross this cell.
  for (int dy = -SETTLEMENT_ROAD_MAX_DIST; dy <= SETTLEMENT_ROAD_MAX_DIST; dy++)
  {
    for (int dx = -SETTLEMENT_ROAD_MAX_DIST; dx <= SETTLEMENT_ROAD_MAX_DIST; dx++)
    {
      const int ax = gx + dx;
      const int ay = gy + dy;
      if (!settlement_is_road_node(ax, ay) && !settlement_is_road_hub(ax, ay))
        continue;

      // Hub-tree parent spoke.
      {
        int bx = 0, by = 0;
        if (settlement_hub_parent(ax, ay, &bx, &by) &&
            settlement_road_in_route_bounds(gx, gy, ax, ay, bx, by))
        {
          if (settlement_cell_on_road_segment(gx, gy, ax, ay, bx, by))
          {
            const SettlementRoadType type =
                settlement_road_type_from_scales(universe_settlement_scale(ax, ay),
                                                 universe_settlement_scale(bx, by));
            if ((int)type > (int)best ||
                (type == best && (ax < best_ax || (ax == best_ax && ay < best_ay))))
            {
              best = type;
              best_ax = ax;
              best_ay = ay;
              best_bx = bx;
              best_by = by;
            }
          }
        }
      }

      // Peer hub mesh spoke.
      if (settlement_is_road_hub(ax, ay))
      {
        int bx = 0, by = 0;
        if (!settlement_nearest_hub(ax, ay, &bx, &by, NULL))
          continue;
        if (!settlement_road_in_route_bounds(gx, gy, ax, ay, bx, by))
          continue;
        // Nearest-hub is directed; only count the canonical edge once.
        if (!settlement_road_edge(ax, ay, bx, by, NULL))
          continue;
        if (!settlement_cell_on_road_segment(gx, gy, ax, ay, bx, by))
          continue;
        const SettlementRoadType type =
            settlement_road_type_from_scales(universe_settlement_scale(ax, ay),
                                             universe_settlement_scale(bx, by));
        if ((int)type > (int)best ||
            (type == best && (ax < best_ax || (ax == best_ax && ay < best_ay))))
        {
          best = type;
          best_ax = ax;
          best_ay = ay;
          best_bx = bx;
          best_by = by;
        }
      }
    }
  }

  if (best == SETTLEMENT_ROAD_NONE)
    return false;
  if (out_type)
    *out_type = best;
  if (out_from_gx)
    *out_from_gx = best_ax;
  if (out_from_gy)
    *out_from_gy = best_ay;
  if (out_to_gx)
    *out_to_gx = best_bx;
  if (out_to_gy)
    *out_to_gy = best_by;
  return true;
}

bool universe_settlement_trade_route(int gx, int gy, SettlementRoadType *out_type,
                                     int *out_from_gx, int *out_from_gy,
                                     int *out_to_gx, int *out_to_gy)
{
  return universe_settlement_road(gx, gy, out_type, out_from_gx, out_from_gy, out_to_gx, out_to_gy);
}

static bool settlement_road_is_structure(VoxelType t)
{
  if (t == VOXEL_BRICK || t == VOXEL_PLANK || t == VOXEL_THATCH || t == VOXEL_PLASTER ||
      t == VOXEL_TERRACOTTA || t == VOXEL_GLASS || t == VOXEL_BEDROCK)
    return true;
  if (t >= VOXEL_WOOD && t <= VOXEL_WOOD_REDWOOD)
    return true;
  return false;
}

static bool settlement_road_is_tree_wood(VoxelType t)
{
  return t >= VOXEL_WOOD && t <= VOXEL_WOOD_REDWOOD;
}

static bool settlement_road_is_building(VoxelType t)
{
  return settlement_road_is_structure(t) && !settlement_road_is_tree_wood(t);
}

static void settlement_road_pave_column(World *world, int x, int y, VoxelType mat, int thickness)
{
  if (!world || x < 0 || y < 0 || (uint32_t)x >= world->width || (uint32_t)y >= world->height)
    return;
  if (thickness < 1)
    thickness = 1;
  if (thickness > 5)
    thickness = 5;
  const int gz = settlement_surface_z(world, x, y);
  if (gz < 0)
    return;
  // Leave standing walls / roofs alone (buildings only — trees may be cleared).
  for (int dz = 1; dz <= 4; dz++)
  {
    const int zz = gz + dz;
    if (zz < 0 || (uint32_t)zz >= world->depth)
      break;
    const Voxel *v = world_voxel_cptr_fast(world, x, y, zz);
    if (v && settlement_road_is_building(v->type))
      return;
  }
  const Voxel *ground = world_voxel_cptr_fast(world, x, y, gz);
  if (ground && settlement_road_is_building(ground->type))
    return;
  // Do not pave through open water or magma; the local router should have avoided these.
  if (ground && (ground->type == VOXEL_WATER || ground->type == VOXEL_MAGMA ||
                 ground->type == VOXEL_ICE))
    return;

  settlement_clear_column_above(world, x, y, gz, 6);
  for (int d = 0; d < thickness; d++)
  {
    const int zz = gz - d;
    if (zz < 1)
      break; // preserve bedrock floor
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)zz, mat);
  }
}

static void settlement_road_paint_thick(World *world, int x0, int y0, int x1, int y1,
                                        int width, VoxelType mat, int thickness)
{
  if (width < 1)
    return;
  const int half_lo = width / 2;
  const int half_hi = width - half_lo - 1;
  int x = x0, y = y0;
  const int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  const int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
  const int sx = (x0 < x1) ? 1 : (x0 > x1 ? -1 : 0);
  const int sy = (y0 < y1) ? 1 : (y0 > y1 ? -1 : 0);
  int err = dx - dy;

  for (;;)
  {
    for (int oy = -half_lo; oy <= half_hi; oy++)
      for (int ox = -half_lo; ox <= half_hi; ox++)
        settlement_road_pave_column(world, x + ox, y + oy, mat, thickness);

    if (x == x1 && y == y1)
      break;
    const int e2 = err * 2;
    if (e2 > -dy)
    {
      err -= dy;
      x += sx;
    }
    if (e2 < dx)
    {
      err += dx;
      y += sy;
    }
  }
}

// Cost to step onto local column (x,y) from (px,py). Negative = blocked.
static int settlement_road_local_step_cost(const World *world, int px, int py, int x, int y,
                                          bool diagonal)
{
  if (!world || x < 1 || y < 1 || x >= (int)world->width - 1 || y >= (int)world->height - 1)
    return -1;
  const int gz = settlement_surface_z(world, x, y);
  if (gz < 0)
    return -1;

  const Voxel *ground = world_voxel_cptr_fast(world, x, y, gz);
  if (!ground)
    return -1;
  if (ground->type == VOXEL_MAGMA)
    return -1;
  if (ground->type == VOXEL_WATER || ground->type == VOXEL_ICE)
    return -1;
  if (settlement_road_is_building(ground->type))
    return -1;

  bool has_tree = settlement_road_is_tree_wood(ground->type);
  for (int dz = 1; dz <= 5; dz++)
  {
    const int zz = gz + dz;
    if (zz < 0 || (uint32_t)zz >= world->depth)
      break;
    const Voxel *v = world_voxel_cptr_fast(world, x, y, zz);
    if (!v || v->type == VOXEL_AIR)
      continue;
    if (settlement_road_is_building(v->type))
      return -1;
    if (settlement_road_is_tree_wood(v->type) ||
        (v->type >= VOXEL_LEAVES && v->type <= VOXEL_LEAVES_REDWOOD))
      has_tree = true;
    if (v->type == VOXEL_WATER || v->type == VOXEL_MAGMA)
      return -1;
  }

  const int pz = settlement_surface_z(world, px, py);
  int dh = 0;
  if (pz >= 0)
    dh = settlement_iabs(gz - pz);

  int cost = diagonal ? 14 : 10;
  cost += dh * dh * 18;
  if (dh > 2)
    cost += 250;
  if (dh > 4)
    cost += 1200;
  if (dh > 6)
    return -1; // cliff face
  if (has_tree)
    cost += 220;
  return cost;
}

// A* on the local voxel XY plane between entry and exit, hugging the surface.
static int settlement_road_local_route(const World *world, int x0, int y0, int x1, int y1,
                                       int16_t *out_xy, int max_len)
{
  if (!world || !out_xy || max_len < 2)
    return 0;
  if (x0 == x1 && y0 == y1)
  {
    out_xy[0] = (int16_t)x0;
    out_xy[1] = (int16_t)y0;
    return 1;
  }

  const int W = (int)world->width;
  const int H = (int)world->height;
  if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 || x0 >= W || y0 >= H || x1 >= W || y1 >= H)
    return 0;

  // Bound the search to a padded box around the straight span so stamping stays cheap.
  int minx = x0 < x1 ? x0 : x1;
  int maxx = x0 > x1 ? x0 : x1;
  int miny = y0 < y1 ? y0 : y1;
  int maxy = y0 > y1 ? y0 : y1;
  const int pad = 14;
  minx = (minx - pad < 1) ? 1 : minx - pad;
  miny = (miny - pad < 1) ? 1 : miny - pad;
  maxx = (maxx + pad > W - 2) ? W - 2 : maxx + pad;
  maxy = (maxy + pad > H - 2) ? H - 2 : maxy + pad;
  const int BW = maxx - minx + 1;
  const int BH = maxy - miny + 1;
  if (BW < 1 || BH < 1 || BW * BH > 96 * 96)
    return 0;

  const size_t ncells = (size_t)BW * (size_t)BH;
  int *g = (int *)malloc(ncells * sizeof(int));
  int *parent = (int *)malloc(ncells * sizeof(int));
  uint8_t *closed = (uint8_t *)calloc(ncells, 1);
  uint8_t *in_open = (uint8_t *)calloc(ncells, 1);
  int *open_i = (int *)malloc(ncells * sizeof(int));
  if (!g || !parent || !closed || !in_open || !open_i)
  {
    free(g);
    free(parent);
    free(closed);
    free(in_open);
    free(open_i);
    return 0;
  }
  for (size_t i = 0; i < ncells; i++)
  {
    g[i] = SETTLEMENT_ROAD_COST_INF;
    parent[i] = -1;
  }

  const int sx = x0 - minx, sy = y0 - miny;
  const int gx = x1 - minx, gy = y1 - miny;
  if (sx < 0 || sy < 0 || sx >= BW || sy >= BH || gx < 0 || gy < 0 || gx >= BW || gy >= BH)
  {
    free(g);
    free(parent);
    free(closed);
    free(in_open);
    free(open_i);
    return 0;
  }
  const int sidx = sy * BW + sx;
  const int gidx = gy * BW + gx;
  g[sidx] = 0;
  int open_n = 0;
  open_i[open_n++] = sidx;
  in_open[sidx] = 1;

  static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                 {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  bool found = false;
  while (open_n > 0)
  {
    int best_oi = 0;
    int best_f = SETTLEMENT_ROAD_COST_INF;
    for (int oi = 0; oi < open_n; oi++)
    {
      const int idx = open_i[oi];
      const int cx = idx % BW, cy = idx / BW;
      const int h = (settlement_iabs(cx - gx) + settlement_iabs(cy - gy)) * 10;
      const int f = g[idx] + h;
      if (f < best_f)
      {
        best_f = f;
        best_oi = oi;
      }
    }
    const int cur = open_i[best_oi];
    open_i[best_oi] = open_i[--open_n];
    in_open[cur] = 0;
    if (closed[cur])
      continue;
    closed[cur] = 1;
    if (cur == gidx)
    {
      found = true;
      break;
    }
    const int cx = cur % BW, cy = cur / BW;
    const int wx = cx + minx, wy = cy + miny;
    for (int d = 0; d < 8; d++)
    {
      const int nx = cx + dirs[d][0];
      const int ny = cy + dirs[d][1];
      if (nx < 0 || ny < 0 || nx >= BW || ny >= BH)
        continue;
      const int nidx = ny * BW + nx;
      if (closed[nidx])
        continue;
      const bool diag = (dirs[d][0] != 0 && dirs[d][1] != 0);
      const int step = settlement_road_local_step_cost(world, wx, wy, nx + minx, ny + miny, diag);
      if (step < 0)
        continue;
      const int ng = g[cur] + step;
      if (ng >= g[nidx])
        continue;
      g[nidx] = ng;
      parent[nidx] = cur;
      if (!in_open[nidx])
      {
        open_i[open_n++] = nidx;
        in_open[nidx] = 1;
      }
    }
  }

  int path_len = 0;
  if (found)
  {
    int stack[512];
    int sn = 0;
    const int stack_max = (int)(sizeof(stack) / sizeof(stack[0]));
    for (int idx = gidx; idx >= 0 && sn < stack_max; idx = parent[idx])
      stack[sn++] = idx;
    path_len = sn < max_len ? sn : max_len;
    for (int i = 0; i < path_len; i++)
    {
      const int idx = stack[sn - 1 - i];
      out_xy[i * 2] = (int16_t)(minx + (idx % BW));
      out_xy[i * 2 + 1] = (int16_t)(miny + (idx / BW));
    }
  }

  free(g);
  free(parent);
  free(closed);
  free(in_open);
  free(open_i);
  return found ? path_len : 0;
}

static void settlement_road_paint_path(World *world, const int16_t *xy, int len, int width,
                                       VoxelType mat, int thickness)
{
  if (!world || !xy || len < 1 || width < 1)
    return;
  if (len == 1)
  {
    settlement_road_paint_thick(world, xy[0], xy[1], xy[0], xy[1], width, mat, thickness);
    return;
  }
  for (int i = 0; i < len - 1; i++)
    settlement_road_paint_thick(world, xy[i * 2], xy[i * 2 + 1], xy[(i + 1) * 2],
                                xy[(i + 1) * 2 + 1], width, mat, thickness);
}

// Border / plaza endpoints for the local span through this universe cell.
static void settlement_road_local_span(const World *world, int gx, int gy,
                                       int ax, int ay, int bx, int by,
                                       int *out_x0, int *out_y0, int *out_x1, int *out_y1)
{
  const int W = (int)world->width;
  const int H = (int)world->height;
  int prev_x = ax, prev_y = ay, next_x = bx, next_y = by;
  if (!settlement_road_route_neighbors(gx, gy, ax, ay, bx, by, &prev_x, &prev_y, &next_x,
                                       &next_y))
  {
    prev_x = ax;
    prev_y = ay;
    next_x = bx;
    next_y = by;
  }

  int cx = W / 2, cy = H / 2;
  if (settlement_has_anchor(world))
  {
    int az = 0;
    settlement_anchor(world, &cx, &cy, &az);
    (void)az;
  }

  // Point on this cell's border facing a neighbor universe cell.
  int x0 = cx, y0 = cy, x1 = cx, y1 = cy;
  {
    const int dx0 = prev_x - gx, dy0 = prev_y - gy;
    if (dx0 < 0)
      x0 = 1;
    else if (dx0 > 0)
      x0 = W - 2;
    else
      x0 = cx;
    if (dy0 < 0)
      y0 = 1;
    else if (dy0 > 0)
      y0 = H - 2;
    else
      y0 = cy;
    if (dx0 != 0 && dy0 != 0)
    {
      x0 = (dx0 < 0) ? 1 : W - 2;
      y0 = (dy0 < 0) ? 1 : H - 2;
    }

    const int dx1 = next_x - gx, dy1 = next_y - gy;
    if (dx1 < 0)
      x1 = 1;
    else if (dx1 > 0)
      x1 = W - 2;
    else
      x1 = cx;
    if (dy1 < 0)
      y1 = 1;
    else if (dy1 > 0)
      y1 = H - 2;
    else
      y1 = cy;
    if (dx1 != 0 && dy1 != 0)
    {
      x1 = (dx1 < 0) ? 1 : W - 2;
      y1 = (dy1 < 0) ? 1 : H - 2;
    }
  }

  // Hub / settlement cells bend through the plaza anchor.
  if (gx == ax && gy == ay)
  {
    x0 = cx;
    y0 = cy;
  }
  if (gx == bx && gy == by)
  {
    x1 = cx;
    y1 = cy;
  }

  if (x0 < 1) x0 = 1;
  if (y0 < 1) y0 = 1;
  if (x1 < 1) x1 = 1;
  if (y1 < 1) y1 = 1;
  if (x0 > W - 2) x0 = W - 2;
  if (y0 > H - 2) y0 = H - 2;
  if (x1 > W - 2) x1 = W - 2;
  if (y1 > H - 2) y1 = H - 2;

  if (out_x0) *out_x0 = x0;
  if (out_y0) *out_y0 = y0;
  if (out_x1) *out_x1 = x1;
  if (out_y1) *out_y1 = y1;
}

bool settlement_stamp_roads(World *world)
{
  if (!world || !world->voxels)
    return false;
  if (world->universe_z != 0)
    return false;

  const int gx = (int)world->universe_x;
  const int gy = (int)world->universe_y;
  SettlementRoadType type = SETTLEMENT_ROAD_NONE;
  int ax = 0, ay = 0, bx = 0, by = 0;
  if (!universe_settlement_road(gx, gy, &type, &ax, &ay, &bx, &by))
    return false;

  const VoxelType mat = settlement_road_voxel(type);
  const int thick = settlement_road_thickness(type);
  const int width = settlement_road_width(type);
  if (mat == VOXEL_AIR || width < 1)
    return false;

  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  settlement_road_local_span(world, gx, gy, ax, ay, bx, by, &x0, &y0, &x1, &y1);

  int16_t local_xy[512];
  int local_len = settlement_road_local_route(world, x0, y0, x1, y1, local_xy, 256);
  if (local_len < 1)
  {
    // Fallback: straight local span if the surface is fully blocked.
    local_xy[0] = (int16_t)x0;
    local_xy[1] = (int16_t)y0;
    local_xy[2] = (int16_t)x1;
    local_xy[3] = (int16_t)y1;
    local_len = (x0 == x1 && y0 == y1) ? 1 : 2;
  }

  // Shoulders first (softer material, one grade down), then the crowned surface.
  if (type == SETTLEMENT_ROAD_HIGHWAY)
    settlement_road_paint_path(world, local_xy, local_len, width + 2, VOXEL_COBBLE,
                               thick > 1 ? thick - 1 : 1);
  else if (type == SETTLEMENT_ROAD_COBBLE)
    settlement_road_paint_path(world, local_xy, local_len, width + 2, VOXEL_GRAVEL,
                               thick > 1 ? thick - 1 : 1);
  else if (type == SETTLEMENT_ROAD_GRAVEL)
    settlement_road_paint_path(world, local_xy, local_len, width + 2, VOXEL_SOIL,
                               thick > 1 ? thick - 1 : 1);
  else if (type == SETTLEMENT_ROAD_DIRT)
    settlement_road_paint_path(world, local_xy, local_len, width + 2, VOXEL_SOIL, 1);
  settlement_road_paint_path(world, local_xy, local_len, width, mat, thick);

  printf("[settlement] road %s w=%d t=%d across (%d,%d) linking (%d,%d)->(%d,%d)\n",
         settlement_road_type_name(type), width, thick, gx, gy, ax, ay, bx, by);
  return true;
}
