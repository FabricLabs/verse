#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fire_sim.h"
#include "lightning_path.h"
#include "particle_effects.h"
#include "volcano.h"
#include "weather_storm.h"
#include "world.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                                           \
  do                                                                                               \
  {                                                                                                \
    g_checks++;                                                                                    \
    if (!(cond))                                                                                   \
    {                                                                                              \
      g_failures++;                                                                                \
      printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                                \
      printf(__VA_ARGS__);                                                                         \
      printf("\n");                                                                                \
    }                                                                                              \
  } while (0)

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

static void test_lightning_path_deterministic(void)
{
  printf("lightning path determinism\n");
  LightningPathPoint a[LIGHTNING_PATH_MAX_POINTS];
  LightningPathPoint b[LIGHTNING_PATH_MAX_POINTS];
  const int na =
      lightning_path_generate(0xabcdu, 4, 4, 1, 4, 4, 20, a, LIGHTNING_PATH_MAX_POINTS);
  const int nb =
      lightning_path_generate(0xabcdu, 4, 4, 1, 4, 4, 20, b, LIGHTNING_PATH_MAX_POINTS);
  CHECK(na >= 2, "path A length");
  CHECK(na == nb, "same length");
  int match = 1;
  for (int i = 0; i < na; i++)
  {
    if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z)
      match = 0;
  }
  CHECK(match, "identical points");
  CHECK(a[0].z == 1 && a[na - 1].z == 20, "endpoints");
  int jagged = 0;
  for (int i = 0; i < na; i++)
  {
    if (a[i].x != 4 || a[i].y != 4)
      jagged = 1;
  }
  CHECK(jagged, "path has lateral jags");
}

static void test_volcano_vent_hotspot(void)
{
  printf("volcano vent hotspot sustain\n");
  World *world = make_world(24, 24, 16);
  CHECK(world != NULL, "create world");
  world->generation_type = WORLD_TYPE_WILDERNESS;
  world->settlement_scale = 0;
  world->universe_x = 3;
  world->universe_y = 7;

  int *tops = calloc((size_t)24 * 24, sizeof(int));
  CHECK(tops != NULL, "tops");
  for (uint32_t y = 0; y < 24; y++)
    for (uint32_t x = 0; x < 24; x++)
    {
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
      for (uint32_t z = 1; z <= 8; z++)
        world_set_voxel(world, x, y, z, VOXEL_STONE);
      tops[(size_t)y * 24 + x] = 8;
    }

  bool stamped = false;
  for (uint32_t salt = 1; salt < 2000 && !stamped; salt++)
    stamped = wilderness_stamp_volcano(world, tops, salt);
  CHECK(stamped, "volcano stamped");
  CHECK(world->volcano_present, "volcano_present");
  CHECK(world->magma_vent_column_count > 0, "vent columns registered");
  CHECK(world_has_magma_vent_column(world, world->volcano_x, world->volcano_y),
        "crater column registered");
  CHECK(world_magma_hotspot_at(world, world->volcano_x, world->volcano_y),
        "crater is magma hotspot");

  int magma_cells = 0;
  for (uint32_t z = 1; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
      {
        Voxel *v = world_get_voxel(world, x, y, z);
        if (v && v->type == VOXEL_MAGMA)
          magma_cells++;
      }
  CHECK(magma_cells >= 5, "magma conduit painted (%d cells)", magma_cells);

  free(tops);
  world_destroy(world);
}

static void test_lightning_ignites_wood(void)
{
  printf("lightning ignites wood\n");
  World *world = make_world(16, 16, 12);
  CHECK(world != NULL, "create world");
  for (uint32_t y = 0; y < 16; y++)
    for (uint32_t x = 0; x < 16; x++)
    {
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(world, x, y, 1, VOXEL_STONE);
    }
  world_set_voxel(world, 8, 8, 1, VOXEL_WOOD_OAK);
  CHECK(fire_can_ignite_at(world, 8, 8, 1), "oak dry");
  CHECK(weather_storm_strike_at(world, 8, 8, 0x51eed01eu), "strike ignited");
  CHECK(world_has_voxel_condition(world, 8, 8, 1, "BURNING_HIGH") ||
            world_has_voxel_condition(world, 8, 8, 1, "BURNING_MEDIUM") ||
            world_has_voxel_condition(world, 8, 8, 1, "BURNING_LOW"),
        "oak burning after strike");
  world_destroy(world);
}

static void test_volcano_pump(void)
{
  printf("volcano pump spills magma\n");
  World *world = make_world(16, 16, 10);
  world->volcano_present = true;
  world->volcano_x = 8;
  world->volcano_y = 8;
  world->volcano_surface_z = 4;
  world->volcano_path_seed = 1;
  world->volcano_pump_cooldown = 0;
  for (uint32_t y = 0; y < 16; y++)
    for (uint32_t x = 0; x < 16; x++)
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
  world_set_voxel(world, 8, 8, 4, VOXEL_STONE_BASALT);

  bool spilled = false;
  for (uint32_t salt = 0; salt < 512 && !spilled; salt++)
    spilled = volcano_try_pump(world, salt);
  CHECK(spilled, "pump spilled");
  int magma = 0;
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
      {
        Voxel *v = world_get_voxel(world, x, y, z);
        if (v && v->type == VOXEL_MAGMA)
          magma++;
      }
  CHECK(magma > 0, "magma present after pump");
  world_destroy(world);
}

int main(void)
{
  test_lightning_path_deterministic();
  test_volcano_vent_hotspot();
  test_lightning_ignites_wood();
  test_volcano_pump();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
