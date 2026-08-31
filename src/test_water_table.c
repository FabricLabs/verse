// Headless checks for groundwater: permeability ranking, diffusion through sand/soil/stone,
// bedrock as aquiclude, and runtime seepage from standing water into the bed.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluid_sim.h"
#include "water_table.h"
#include "world.h"

static int g_failures = 0;
static int g_checks = 0;

static void check(bool ok, const char *what)
{
  g_checks++;
  if (ok)
    printf("  ok   %s\n", what);
  else
  {
    printf("  FAIL %s\n", what);
    g_failures++;
  }
}

static World *make_world(uint32_t w, uint32_t h, uint32_t d)
{
  World *world = world_create(w, h, d);
  if (!world)
    return NULL;
  for (uint32_t z = 0; z < d; z++)
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++)
        world_set_voxel(world, x, y, z, VOXEL_AIR);
  return world;
}

static uint8_t wet_at(World *world, uint32_t x, uint32_t y, uint32_t z)
{
  Voxel *v = world_get_voxel(world, x, y, z);
  if (!v || !water_table_is_permeable(v->type))
    return 0;
  uint8_t q = voxel_get_quantity(v);
  return q <= WATER_TABLE_WETNESS_MAX ? q : 0;
}

static void test_permeability_ranking(void)
{
  printf("permeability ranking\n");
  check(water_table_permeability(VOXEL_BEDROCK) == 0, "bedrock is impermeable");
  check(water_table_permeability(VOXEL_WATER) == 0, "free water is not a porous solid");
  check(water_table_permeability(VOXEL_SAND) > water_table_permeability(VOXEL_SOIL),
        "sand conducts better than soil");
  check(water_table_permeability(VOXEL_SOIL) > water_table_permeability(VOXEL_SOIL_CLAY),
        "loamy soil conducts better than clay");
  check(water_table_permeability(VOXEL_STONE_SANDSTONE) >
            water_table_permeability(VOXEL_STONE_GRANITE),
        "sandstone conducts better than granite");
  check(water_table_permeability(VOXEL_STONE_GRANITE) > 0, "granite is barely permeable");
  check(water_table_is_permeable(VOXEL_SAND), "sand is permeable");
  check(!water_table_is_permeable(VOXEL_BEDROCK), "bedrock is not permeable");
}

static void test_seep_down_through_sand(void)
{
  printf("water seeps down through sand\n");
  World *world = make_world(8, 8, 8);
  check(world != NULL, "world created");
  if (!world)
    return;

  for (uint32_t z = 0; z < 8; z++)
    for (uint32_t y = 0; y < 8; y++)
      for (uint32_t x = 0; x < 8; x++)
        world_set_voxel(world, x, y, z, z == 0 ? VOXEL_BEDROCK : VOXEL_SAND);

  world_set_voxel(world, 4, 4, 5, VOXEL_WATER);
  voxel_set_quantity(world_get_voxel(world, 4, 4, 5), FLUID_LEVEL_FULL);

  int *tops = (int *)calloc((size_t)8 * 8, sizeof(int));
  check(tops != NULL, "tops allocated");
  if (!tops)
  {
    world_destroy(world);
    return;
  }
  for (uint32_t i = 0; i < 64; i++)
    tops[i] = 4;
  tops[4 * 8 + 4] = 5;

  water_table_build(world, tops, 0xA11CE001u);

  check(wet_at(world, 4, 4, 4) > 0, "sand under water is wet");
  check(wet_at(world, 4, 4, 3) > 0, "sand deeper in the column is wet");
  check(wet_at(world, 4, 4, 1) > 0, "water table reaches near bedrock");
  check(voxel_get_type(world_get_voxel(world, 4, 4, 0)) == VOXEL_BEDROCK, "bedrock stays bedrock");
  check(wet_at(world, 4, 4, 0) == 0, "bedrock never takes wetness");

  free(tops);
  world_destroy(world);
}

static void test_clay_slows_diffusion(void)
{
  printf("clay attenuates more than sand\n");
  World *sand_w = make_world(6, 6, 6);
  World *clay_w = make_world(6, 6, 6);
  check(sand_w && clay_w, "pair of worlds created");
  if (!sand_w || !clay_w)
    return;

  for (uint32_t z = 0; z < 6; z++)
    for (uint32_t y = 0; y < 6; y++)
      for (uint32_t x = 0; x < 6; x++)
      {
        world_set_voxel(sand_w, x, y, z, z == 0 ? VOXEL_BEDROCK : VOXEL_SAND);
        world_set_voxel(clay_w, x, y, z, z == 0 ? VOXEL_BEDROCK : VOXEL_SOIL_CLAY);
      }

  world_set_voxel(sand_w, 3, 3, 3, VOXEL_WATER);
  world_set_voxel(clay_w, 3, 3, 3, VOXEL_WATER);
  voxel_set_quantity(world_get_voxel(sand_w, 3, 3, 3), FLUID_LEVEL_FULL);
  voxel_set_quantity(world_get_voxel(clay_w, 3, 3, 3), FLUID_LEVEL_FULL);

  int *tops = (int *)calloc(36, sizeof(int));
  check(tops != NULL, "tops allocated");
  if (!tops)
  {
    world_destroy(sand_w);
    world_destroy(clay_w);
    return;
  }
  for (int i = 0; i < 36; i++)
    tops[i] = 4;

  water_table_build(sand_w, tops, 1);
  water_table_build(clay_w, tops, 1);

  uint8_t sand_side = wet_at(sand_w, 4, 3, 2);
  uint8_t clay_side = wet_at(clay_w, 4, 3, 2);
  check(sand_side > clay_side, "sand neighbour wetter than clay neighbour after diffusion");

  free(tops);
  world_destroy(sand_w);
  world_destroy(clay_w);
}

static void test_granite_barely_wets(void)
{
  printf("granite barely accepts wetness from contact water\n");
  World *world = make_world(4, 4, 4);
  check(world != NULL, "world created");
  if (!world)
    return;

  for (uint32_t z = 0; z < 4; z++)
    for (uint32_t y = 0; y < 4; y++)
      for (uint32_t x = 0; x < 4; x++)
        world_set_voxel(world, x, y, z, z == 0 ? VOXEL_BEDROCK : VOXEL_STONE_GRANITE);

  world_set_voxel(world, 2, 2, 2, VOXEL_WATER);
  voxel_set_quantity(world_get_voxel(world, 2, 2, 2), FLUID_LEVEL_FULL);

  int tops[16];
  for (int i = 0; i < 16; i++)
    tops[i] = 2;

  water_table_build(world, tops, 2);

  check(wet_at(world, 0, 0, 1) == 0, "distant granite stays dry");
  check(wet_at(world, 2, 2, 1) <= 3, "granite under water stays lightly damp at most");

  world_destroy(world);
}

static void test_runtime_seepage(void)
{
  printf("runtime transfer seeps into sand bed\n");
  World *world = make_world(6, 6, 6);
  check(world != NULL, "world created");
  if (!world)
    return;

  for (uint32_t y = 0; y < 6; y++)
    for (uint32_t x = 0; x < 6; x++)
    {
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(world, x, y, 1, VOXEL_SAND);
    }

  world_set_voxel(world, 2, 2, 2, VOXEL_WATER);
  voxel_set_quantity(world_get_voxel(world, 2, 2, 2), FLUID_LEVEL_FULL);
  world_set_voxel(world, 3, 2, 2, VOXEL_WATER);
  voxel_set_quantity(world_get_voxel(world, 3, 2, 2), 40);
  fluid_sim_touch(world, 2, 2, 2);
  fluid_sim_touch(world, 3, 2, 2);

  for (int i = 0; i < 40; i++)
    fluid_sim_step(world, 256);

  check(wet_at(world, 2, 2, 1) > 0 || wet_at(world, 3, 2, 1) > 0,
        "sand under flowing water becomes damp");

  world_destroy(world);
}

static void test_determinism(void)
{
  printf("same seed yields same wetness field\n");
  World *a = make_world(10, 10, 8);
  World *b = make_world(10, 10, 8);
  check(a && b, "pair of worlds created");
  if (!a || !b)
    return;

  int *tops = (int *)calloc(100, sizeof(int));
  check(tops != NULL, "tops allocated");
  if (!tops)
  {
    world_destroy(a);
    world_destroy(b);
    return;
  }

  for (uint32_t y = 0; y < 10; y++)
    for (uint32_t x = 0; x < 10; x++)
    {
      tops[y * 10 + x] = 4;
      for (uint32_t z = 0; z < 8; z++)
      {
        VoxelType t = z == 0 ? VOXEL_BEDROCK : (z <= 3 ? VOXEL_STONE_SANDSTONE : VOXEL_SOIL);
        world_set_voxel(a, x, y, z, t);
        world_set_voxel(b, x, y, z, t);
      }
    }
  world_set_voxel(a, 5, 5, 4, VOXEL_WATER);
  world_set_voxel(b, 5, 5, 4, VOXEL_WATER);
  voxel_set_quantity(world_get_voxel(a, 5, 5, 4), FLUID_LEVEL_FULL);
  voxel_set_quantity(world_get_voxel(b, 5, 5, 4), FLUID_LEVEL_FULL);

  a->universe_x = b->universe_x = 2;
  a->universe_y = b->universe_y = 4;
  a->universe_z = b->universe_z = 0;

  water_table_build(a, tops, 0xDEC0DE01u);
  water_table_build(b, tops, 0xDEC0DE01u);

  bool match = true;
  for (uint32_t z = 0; z < 8 && match; z++)
    for (uint32_t y = 0; y < 10 && match; y++)
      for (uint32_t x = 0; x < 10 && match; x++)
        if (wet_at(a, x, y, z) != wet_at(b, x, y, z))
          match = false;
  check(match, "identical builds match cell-for-cell");

  free(tops);
  world_destroy(a);
  world_destroy(b);
}

int main(void)
{
  printf("Water table\n-----------\n");
  test_permeability_ranking();
  test_seep_down_through_sand();
  test_clay_slows_diffusion();
  test_granite_barely_wets();
  test_runtime_seepage();
  test_determinism();
  printf("\n=== %s (%d checks, %d failures) ===\n", g_failures ? "FAILED" : "ALL PASSED",
         g_checks, g_failures);
  return g_failures ? 1 : 0;
}
