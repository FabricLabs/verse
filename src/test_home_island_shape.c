#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "world.h"
#include "world_bulk_ops.h"
#include "world_spawn.h"

// Shape and content invariants for the HOME world island, as built by world_generate_home():
// the world is cleared to air, then a stone core is laid in circular XY slices for z in
// [0, depth/2), with radius_at_z = island_radius * (z / center_z)^2 and island_radius =
// min(min(w,h,d)/2 - 1, d - 3). That leaves a flat-topped paraboloid, into which the generator
// then cuts relief, over which it lays soil and grass, and on which it scatters boulders,
// bushes and trees. A central arena is left flat, at the island's maximum height, and clear of
// decorations so the player can spawn there. D&D creature-size wireframe boxes (crystal / glass /
// ice edge cubes) stand on the south rim of that arena. A campfire (with a candle on the rim)
// sits just north of the spawn clear-zone.
//
// The assertions below are derived from that construction rather than from captured output, so
// they hold for any cubic world size.

static int failures = 0;

static void check(int condition, const char *what)
{
  if (condition) {
    printf("  ✓ %s\n", what);
  } else {
    printf("  ✗ %s\n", what);
    failures++;
  }
}

static bool is_grass(VoxelType t)
{
  return t == VOXEL_GRASS || t == VOXEL_GRASS_WIDE || t == VOXEL_GRASS_SHARP ||
         t == VOXEL_GRASS_CLOVER || t == VOXEL_GRASS_MOSS;
}

static bool is_soil(VoxelType t)
{
  return t == VOXEL_SOIL || t == VOXEL_SOIL_CLAY || t == VOXEL_SOIL_LOAM || t == VOXEL_SOIL_SILT;
}

// The island core is laid in plain VOXEL_STONE; the harder rocks below are only used for
// boulders, which stand on the surface rather than forming it.
static bool is_boulder_rock(VoxelType t)
{
  return t == VOXEL_STONE_GRANITE || t == VOXEL_STONE_BASALT || t == VOXEL_GRAVEL;
}

// Ground the island is made of, as opposed to what is standing on it.
static bool is_terrain(VoxelType t)
{
  return t == VOXEL_STONE || is_grass(t) || is_soil(t);
}

static bool is_wood(VoxelType t)
{
  return t >= VOXEL_WOOD && t <= VOXEL_WOOD_REDWOOD;
}

static bool is_leaves(VoxelType t)
{
  return t >= VOXEL_LEAVES && t <= VOXEL_LEAVES_REDWOOD;
}

static bool is_bush(VoxelType t)
{
  return t >= VOXEL_BUSH && t <= VOXEL_BUSH_STRAWBERRY;
}

// Translucent edge-cubes that mark D&D creature sizes on the home arena.
static bool is_size_wireframe(VoxelType t)
{
  return t == VOXEL_CRYSTAL || t == VOXEL_CRYSTAL_GREEN || t == VOXEL_CRYSTAL_BLUE ||
         t == VOXEL_CRYSTAL_RED || t == VOXEL_GLASS || t == VOXEL_ICE;
}

// voxel.h declares voxel_type_is_fluid() but nothing defines it, so the categories are spelled
// out here. Springs count: one would fill the island with fluid on the first physics tick even
// though the spring voxel itself is solid.
static bool is_fluid_or_source(VoxelType t)
{
  return (t >= VOXEL_WATER && t <= VOXEL_GAS) ||
         (t >= VOXEL_SPRING && t <= VOXEL_SPRING_GAS);
}

// Topmost non-air voxel in a column, or -1 for an empty column.
static int top_of_column(World *world, uint32_t x, uint32_t y)
{
  for (int z = (int)world->depth - 1; z >= 0; z--) {
    Voxel *v = world_get_voxel(world, x, y, (uint32_t)z);
    if (v && v->type != VOXEL_AIR) return z;
  }
  return -1;
}

int main(void)
{
  const uint32_t SIZE = 64;

  printf("HOME World Island Shape Test\n");
  printf("===========================\n\n");

  World *world = world_create(SIZE, SIZE, SIZE);
  if (!world) {
    printf("Failed to create world\n");
    return 1;
  }

  world_generate_with_type(world, "test_home_island_seed", WORLD_TYPE_HOME);

  const uint32_t center_x = world->width / 2;
  const uint32_t center_y = world->height / 2;
  const uint32_t center_z = world->depth / 2;

  uint32_t min_dim = world->width;
  if (world->height < min_dim) min_dim = world->height;
  if (world->depth < min_dim) min_dim = world->depth;
  const uint32_t max_radius = min_dim / 2 - 1;
  const uint32_t island_radius =
      (max_radius < world->depth - 3) ? max_radius : world->depth - 3;

  // The core's flat top, before relief is cut into it.
  const int surface_top = (int)center_z - 1;

  printf("Dimensions: %ux%ux%u, center_z=%u, island_radius=%u, surface_top=%d\n\n",
         world->width, world->height, world->depth, center_z, island_radius, surface_top);

  uint32_t *slice_terrain = calloc(world->depth, sizeof(uint32_t));
  uint32_t *slice_max_r2 = calloc(world->depth, sizeof(uint32_t));
  if (!slice_terrain || !slice_max_r2) {
    printf("Allocation failed\n");
    free(slice_terrain);
    free(slice_max_r2);
    world_destroy(world);
    return 1;
  }

  uint32_t terrain_total = 0;
  uint32_t border_terrain = 0;
  uint32_t terrain_above_mid = 0;
  uint32_t outside_radius = 0;
  uint32_t fluid_total = 0;
  uint32_t wood_total = 0, leaves_total = 0, bush_total = 0, hard_rock_total = 0;
  double centroid_x = 0.0, centroid_y = 0.0;

  for (uint32_t z = 0; z < world->depth; z++) {
    for (uint32_t y = 0; y < world->height; y++) {
      for (uint32_t x = 0; x < world->width; x++) {
        Voxel *voxel = world_get_voxel(world, x, y, z);
        if (!voxel || voxel->type == VOXEL_AIR) continue;

        if (is_fluid_or_source(voxel->type)) fluid_total++;
        if (is_wood(voxel->type)) wood_total++;
        if (is_leaves(voxel->type)) leaves_total++;
        if (is_bush(voxel->type)) bush_total++;
        if (is_boulder_rock(voxel->type)) hard_rock_total++;

        if (!is_terrain(voxel->type)) continue;

        terrain_total++;
        slice_terrain[z]++;
        centroid_x += x;
        centroid_y += y;

        const int dx = (int)x - (int)center_x;
        const int dy = (int)y - (int)center_y;
        const uint32_t r2 = (uint32_t)(dx * dx + dy * dy);
        if (r2 > slice_max_r2[z]) slice_max_r2[z] = r2;

        if (x == 0 || y == 0 || x == world->width - 1 || y == world->height - 1)
          border_terrain++;
        if (z > (uint32_t)surface_top) terrain_above_mid++;
        if (r2 > island_radius * island_radius) outside_radius++;
      }
    }
  }

  printf("Terrain voxels: %u  (wood %u, leaves %u, bushes %u, hard rock %u)\n\n",
         terrain_total, wood_total, leaves_total, bush_total, hard_rock_total);

  printf("Core shape:\n");
  check(terrain_total > 0, "island is not empty");

  // A 1-voxel border is reserved by max_radius, so terrain can never reach the XY perimeter;
  // if it does, the island would seam with neighbouring worlds.
  check(border_terrain == 0, "no terrain on the XY perimeter (1-voxel border intact)");

  // Terrain is only laid below the vertical midpoint. Trees and boulders stand above it, which
  // is why this counts terrain rather than every solid voxel.
  check(terrain_above_mid == 0, "no terrain above the core's flat top");

  // radius_at_z is 0 at z = 0, so the island comes to a point at the bottom.
  check(slice_terrain[0] == 0, "bottom slice (z=0) is empty; island is pointed");

  // Every terrain voxel lies within the computed island radius.
  check(outside_radius == 0, "all terrain lies within island_radius of the axis");

  // radius_at_z grows monotonically with z, so slices must not narrow going up. Relief is cut
  // into the top of the island, so only the part of the core it cannot reach is checked here;
  // the generator removes at most a handful of voxels per column.
  const int relief_reach = 8;
  const int monotonic_top = surface_top - relief_reach;
  int narrowing_at = -1;
  for (int z = 1; z <= monotonic_top; z++) {
    if (slice_max_r2[z] < slice_max_r2[z - 1]) { narrowing_at = z; break; }
  }
  if (narrowing_at >= 0)
    printf("  (narrowing first seen at z=%d)\n", narrowing_at);
  check(narrowing_at < 0, "core slice radius is non-decreasing below the relief zone");

  // The island is built around the world axis, so its centroid should be there.
  if (terrain_total > 0) {
    centroid_x /= terrain_total;
    centroid_y /= terrain_total;
    const double drift = fmax(fabs(centroid_x - center_x), fabs(centroid_y - center_y));
    printf("  (centroid drift from axis: %.2f voxels)\n", drift);
    check(drift < 1.0, "terrain centroid is on the world axis");
  }

  // --- Surface treatment -------------------------------------------------------------------
  printf("\nSurface:\n");

  uint32_t terrain_columns = 0;
  uint32_t bare_columns = 0, bare_grass_capped = 0;
  uint32_t thick_grass_columns = 0, soil_under_grass = 0, stone_under_soil = 0;
  int highest_terrain = -1, lowest_surface = (int)world->depth;
  uint32_t distinct_heights[256];
  memset(distinct_heights, 0, sizeof(distinct_heights));

  for (uint32_t y = 0; y < world->height; y++) {
    for (uint32_t x = 0; x < world->width; x++) {
      // Find the top of the *terrain* in this column, ignoring anything standing on it.
      int top = -1;
      for (int z = surface_top; z >= 0; z--) {
        Voxel *v = world_get_voxel(world, x, y, (uint32_t)z);
        if (v && is_terrain(v->type)) { top = z; break; }
      }
      if (top < 0) continue;

      terrain_columns++;
      if (top > highest_terrain) highest_terrain = top;
      if (top < lowest_surface) lowest_surface = top;
      if (top < 256) distinct_heights[top]++;

      // A boulder rests on the ground and covers the grass it sits on, so only columns with
      // nothing on them are expected to show grass. The campfire hearth also replaces the grass
      // cap with stone/coal, so those columns are treated the same way.
      Voxel *cap = world_get_voxel(world, x, y, (uint32_t)top);
      bool decorated = false;
      for (int z = top; z < (int)world->depth; z++) {
        Voxel *v = world_get_voxel(world, x, y, (uint32_t)z);
        if (!v) continue;
        if (is_boulder_rock(v->type) || v->type == VOXEL_CAMPFIRE || v->type == VOXEL_CANDLE ||
            v->type == VOXEL_ORE_COAL) {
          decorated = true;
          break;
        }
      }
      // Stone ring of the hearth: surface stone next to the campfire/candle fixtures.
      if (!decorated && cap && cap->type == VOXEL_STONE) {
        for (int dy = -1; dy <= 1 && !decorated; dy++) {
          for (int dx = -1; dx <= 1 && !decorated; dx++) {
            if (dx == 0 && dy == 0) continue;
            const int nx = (int)x + dx, ny = (int)y + dy;
            if (nx < 0 || ny < 0 || nx >= (int)world->width || ny >= (int)world->height)
              continue;
            Voxel *n = world_get_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)(top + 1));
            if (n && (n->type == VOXEL_CAMPFIRE || n->type == VOXEL_CANDLE))
              decorated = true;
          }
        }
      }

      if (!decorated) {
        bare_columns++;
        if (cap && is_grass(cap->type)) bare_grass_capped++;
      }

      // The soil band is only laid where there is stone to convert, so a one-voxel column at the
      // rim is grass with nothing under it. Only columns thick enough to hold the band are checked.
      if (cap && is_grass(cap->type) && top >= 2) {
        Voxel *under = world_get_voxel(world, x, y, (uint32_t)(top - 1));
        Voxel *under2 = world_get_voxel(world, x, y, (uint32_t)(top - 2));
        const bool thick = under && under->type != VOXEL_AIR && under2 && under2->type != VOXEL_AIR;
        if (thick) {
          thick_grass_columns++;
          if (is_soil(under->type)) {
            soil_under_grass++;
            for (int z = top - 2; z >= 0; z--) {
              Voxel *deep = world_get_voxel(world, x, y, (uint32_t)z);
              if (!deep || deep->type == VOXEL_AIR) break;
              if (is_soil(deep->type)) continue;
              if (deep->type == VOXEL_STONE) stone_under_soil++;
              break;
            }
          }
        }
      }
    }
  }

  int height_levels = 0;
  for (int i = 0; i < 256; i++) if (distinct_heights[i]) height_levels++;

  printf("  terrain columns %u (bare %u, of which grass-capped %u)\n",
         terrain_columns, bare_columns, bare_grass_capped);
  printf("  thick grass columns %u, soil beneath %u, stone under the soil %u\n",
         thick_grass_columns, soil_under_grass, stone_under_soil);
  printf("  surface heights span z=%d..%d across %d levels\n",
         lowest_surface, highest_terrain, height_levels);

  // Trees only take on grass, so a stone-topped island grows nothing at all — which is exactly
  // what used to happen here. Open ground must be grass for the surface to be plantable.
  check(bare_columns > 0 && bare_grass_capped == bare_columns,
        "every column without a boulder or hearth on it is capped with grass");
  check(thick_grass_columns > 0 && soil_under_grass == thick_grass_columns,
        "grass sits on soil wherever the column is thick enough for a soil band");
  check(stone_under_soil > 0, "stone core survives beneath the soil band");
  check(highest_terrain == surface_top, "the island's highest terrain is the core's flat top");
  check(height_levels >= 3, "the surface has relief rather than being one flat plate");

  // --- Features ----------------------------------------------------------------------------
  printf("\nFeatures:\n");
  check(wood_total > 0, "trees were planted (trunk voxels present)");
  check(leaves_total > wood_total, "trees have canopies");
  check(bush_total > 0, "bushes were placed");
  check(hard_rock_total > 0, "boulders were placed");

  // D&D size wireframes: translucent edge-boxes on the south rim of the plateau.
  uint32_t crystal = 0, crystal_green = 0, crystal_blue = 0, crystal_red = 0, glass = 0, ice = 0;
  for (uint32_t z = 0; z < world->depth; z++) {
    for (uint32_t y = 0; y < world->height; y++) {
      for (uint32_t x = 0; x < world->width; x++) {
        Voxel *voxel = world_get_voxel(world, x, y, z);
        if (!voxel) continue;
        switch (voxel->type) {
        case VOXEL_CRYSTAL: crystal++; break;
        case VOXEL_CRYSTAL_GREEN: crystal_green++; break;
        case VOXEL_CRYSTAL_BLUE: crystal_blue++; break;
        case VOXEL_CRYSTAL_RED: crystal_red++; break;
        case VOXEL_GLASS: glass++; break;
        case VOXEL_ICE: ice++; break;
        default: break;
        }
      }
    }
  }
  printf("  size wireframes: tiny %u small %u medium %u large %u huge %u gargantuan %u\n",
         crystal, crystal_green, crystal_blue, crystal_red, glass, ice);
  check(crystal == 1, "Tiny size marker (1³ crystal) is present");
  check(crystal_green == 1, "Small size marker (1³ green crystal) is present");
  check(crystal_blue > 0, "Medium wireframe (blue crystal) is present");
  check(crystal_red > 0, "Large wireframe (red crystal) is present");
  check(glass > 0, "Huge wireframe (glass) is present");
  check(ice > crystal_red, "Gargantuan wireframe (ice) is the largest");

  // Campfire just north of the spawn clear-zone, with a candle on the ring.
  uint32_t campfires = 0, candles = 0;
  for (uint32_t z = 0; z < world->depth; z++) {
    for (uint32_t y = 0; y < world->height; y++) {
      for (uint32_t x = 0; x < world->width; x++) {
        Voxel *voxel = world_get_voxel(world, x, y, z);
        if (!voxel) continue;
        if (voxel->type == VOXEL_CAMPFIRE) campfires++;
        if (voxel->type == VOXEL_CANDLE) candles++;
      }
    }
  }
  printf("  campfire %u candle %u\n", campfires, candles);
  check(campfires == 1, "a campfire sits on the home arena");
  check(candles == 1, "a candle sits on the campfire ring");

  // Lit on arrival: the fixtures carry BURNING_* so particles and spread have a source.
  {
    int campfire_lit = 0, candle_lit = 0;
    for (uint32_t z = 0; z < world->depth; z++) {
      for (uint32_t y = 0; y < world->height; y++) {
        for (uint32_t x = 0; x < world->width; x++) {
          Voxel *voxel = world_get_voxel(world, x, y, z);
          if (!voxel) continue;
          if (voxel->type == VOXEL_CAMPFIRE &&
              world_has_voxel_condition(world, x, y, z, "BURNING_HIGH"))
            campfire_lit = 1;
          if (voxel->type == VOXEL_CANDLE &&
              world_has_voxel_condition(world, x, y, z, "BURNING_MEDIUM"))
            candle_lit = 1;
        }
      }
    }
    check(campfire_lit, "the home campfire is lit (BURNING_HIGH)");
    check(candle_lit, "the home candle is lit (BURNING_MEDIUM)");
  }

  // --- No fluids ---------------------------------------------------------------------------
  printf("\nFluids:\n");
  check(fluid_total == 0, "the home island contains no fluid and no fluid source");

  // --- Spawn -------------------------------------------------------------------------------
  printf("\nSpawn:\n");
  SpawnPosition spawn = world_find_best_spawn_position(world);
  check(spawn.is_safe, "a spawn was found");
  if (spawn.is_safe) {
    Voxel *ground = world_get_voxel(world, spawn.x, spawn.y, (uint32_t)(spawn.z - 1));
    Voxel *at = world_get_voxel(world, spawn.x, spawn.y, (uint32_t)spawn.z);
    printf("  spawn (%d,%d,%d), ground below type %d\n",
           spawn.x, spawn.y, spawn.z, ground ? ground->type : -1);

    check(spawn.z - 1 == highest_terrain, "spawn stands on the island's topmost level");
    check(ground && is_terrain(ground->type), "spawn has terrain underfoot");
    check(at && at->type == VOXEL_AIR, "spawn itself is clear");
    check(top_of_column(world, spawn.x, spawn.y) == spawn.z - 1,
          "nothing is standing on the spawn column");

    // The arena has to be flat underfoot. Size-reference wireframes may stand on it; trees and
    // boulders may not, or gravity would drop the player onto cover instead of open ground.
    int arena_flat = 1, arena_clear = 1;
    for (int dy = -4; dy <= 4; dy++) {
      for (int dx = -4; dx <= 4; dx++) {
        const int ax = spawn.x + dx, ay = spawn.y + dy;
        if (ax < 0 || ay < 0 || ax >= (int)world->width || ay >= (int)world->height) continue;

        int terrain_top = -1;
        for (int z = surface_top; z >= 0; z--) {
          Voxel *v = world_get_voxel(world, (uint32_t)ax, (uint32_t)ay, (uint32_t)z);
          if (v && is_terrain(v->type)) { terrain_top = z; break; }
        }
        if (terrain_top != spawn.z - 1) arena_flat = 0;

        for (int z = terrain_top + 1; z < (int)world->depth; z++) {
          Voxel *v = world_get_voxel(world, (uint32_t)ax, (uint32_t)ay, (uint32_t)z);
          if (!v || v->type == VOXEL_AIR) continue;
          if (is_size_wireframe(v->type)) continue;
          arena_clear = 0;
          break;
        }
      }
    }
    check(arena_flat, "the 9x9 arena around the spawn is flat");
    check(arena_clear, "the 9x9 arena around the spawn is clear of trees and boulders");
  }
  free(spawn.spawn_reason);

  // Same seed, same island: the surface, decorations and spawn are all seed-derived.
  World *again = world_create(SIZE, SIZE, SIZE);
  if (again) {
    world_generate_with_type(again, "test_home_island_seed", WORLD_TYPE_HOME);
    int identical = 1;
    for (uint32_t z = 0; z < world->depth && identical; z++)
      for (uint32_t y = 0; y < world->height && identical; y++)
        for (uint32_t x = 0; x < world->width && identical; x++) {
          Voxel *a = world_get_voxel(world, x, y, z);
          Voxel *b = world_get_voxel(again, x, y, z);
          if ((a ? a->type : 0) != (b ? b->type : 0)) identical = 0;
        }
    printf("\nDeterminism:\n");
    check(identical, "the same seed rebuilds the same island");
    world_destroy(again);
  }

  free(slice_terrain);
  free(slice_max_r2);
  world_destroy(world);

  printf("\n");
  if (failures == 0) {
    printf("=== HOME island shape: all invariants hold ===\n");
    return 0;
  }
  printf("=== HOME island shape: %d invariant(s) violated ===\n", failures);
  return 1;
}
