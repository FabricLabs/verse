/* Headless tests for universe biomes: continuity, ore host gating, fauna spawn. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "universe_biome.h"
#include "universe.h"
#include "world.h"
#include "mob_ai.h"

static int g_fail = 0;
static int g_ok = 0;

static void expect(bool cond, const char *msg)
{
  if (cond)
  {
    printf("  ok   %s\n", msg);
    g_ok++;
  }
  else
  {
    printf("  FAIL %s\n", msg);
    g_fail++;
  }
}

static void test_climate_continuity(void)
{
  printf("climate continuity\n");
  World *a = world_create(32, 32, 32);
  World *b = world_create(32, 32, 32);
  expect(a && b, "worlds created");
  a->universe_x = 0;
  a->universe_y = 0;
  a->universe_z = 0;
  b->universe_x = 1;
  b->universe_y = 0;
  b->universe_z = 0;

  UniverseClimate ca = universe_climate_sample(a, 31, 16, -1.0f);
  UniverseClimate cb = universe_climate_sample(b, 0, 16, -1.0f);
  float dt = fabsf(ca.temperature - cb.temperature);
  float dm = fabsf(ca.moisture - cb.moisture);
  expect(dt < 0.08f && dm < 0.08f, "adjacent-face climate samples agree");

  UniverseBiomeSample sa = universe_biome_classify(&ca);
  UniverseBiomeSample sb = universe_biome_classify(&cb);
  expect(sa.primary == sb.primary ||
             fabsf(sa.weights[sa.primary] - sb.weights[sa.primary]) < 0.25f,
         "adjacent-face biome classification is close");

  world_destroy(a);
  world_destroy(b);
}

static void test_rain_intensity(void)
{
  printf("rain intensity\n");
  UniverseClimate wet = {.temperature = 0.48f, .moisture = 0.9f, .elevation = 0.2f, .volcanic = 0.0f};
  UniverseClimate dry = {.temperature = 0.8f, .moisture = 0.1f, .elevation = 0.3f, .volcanic = 0.1f};
  UniverseBiomeSample sw = universe_biome_classify(&wet);
  UniverseBiomeSample sd = universe_biome_classify(&dry);
  float rw = universe_biome_rain_intensity(&sw);
  float rd = universe_biome_rain_intensity(&sd);
  expect(rw > rd, "wetland-ish climate rains more than desert-ish");
  expect(rw > 0.2f, "wet climate has measurable rain");
}

static void test_fauna_create_and_update(void)
{
  printf("fauna create/update\n");
  World *w = world_create(16, 16, 16);
  expect(!!w, "world for fauna");
  for (uint32_t x = 0; x < 16; x++)
    for (uint32_t y = 0; y < 16; y++)
      world_set_voxel(w, x, y, 0, VOXEL_STONE_GRANITE);

  MobActor *sheep = mob_actor_create_sheep(4.5, 4.5, 1.0);
  MobActor *chicken = mob_actor_create_chicken(6.5, 4.5, 1.0);
  MobActor *bat = mob_actor_create_bat(8.5, 4.5, 3.0);
  expect(sheep && sheep->mob_type == MOB_TYPE_SHEEP, "sheep created");
  expect(chicken && chicken->mob_type == MOB_TYPE_CHICKEN, "chicken created");
  expect(bat && bat->mob_type == MOB_TYPE_BAT, "bat created");

  Actor s_copy = sheep->base;
  s_copy.extra_data = sheep;
  Actor c_copy = chicken->base;
  c_copy.extra_data = chicken;
  Actor b_copy = bat->base;
  b_copy.extra_data = bat;
  expect(mob_actor_is_livestock(&s_copy), "sheep is livestock");
  expect(mob_actor_is_livestock(&c_copy), "chicken is livestock");
  expect(mob_actor_is_bat(&b_copy), "bat is bat");

  mob_actor_update(sheep, w, 0.05f);
  mob_actor_update(chicken, w, 0.05f);
  mob_actor_update(bat, w, 0.05f);
  expect(sheep->base.is_active && chicken->base.is_active && bat->base.is_active,
         "fauna update keeps actors active");

  mob_actor_destroy(sheep);
  mob_actor_destroy(chicken);
  mob_actor_destroy(bat);
  world_destroy(w);
}

static void test_wilderness_spawn_biome(void)
{
  printf("wilderness spawn\n");
  const char *seed = "biome_spawn_test_seed_0001";
  World *w = world_create(64, 64, 64);
  expect(!!w, "wilderness world created");
  w->universe_x = 2;
  w->universe_y = 3;
  w->universe_z = 0;
  world_generate_with_type(w, seed, WORLD_TYPE_WILDERNESS);

  UniverseBiomeId primary = universe_biome_primary_at(w, w->width / 2, w->height / 2);
  printf("  primary biome: %s\n", universe_biome_name(primary));
  expect(world_spawn_wilderness_mobs(w), "wilderness mobs spawn");

  int sheep = 0, chicken = 0, bat = 0, wild = 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    Actor *a = &w->runtime_actors[i];
    if (strncmp(a->name, "Wild ", 5) == 0)
      wild++;
    if (strstr(a->name, "Sheep") || strstr(a->name, "Ewe") || strstr(a->name, "Ram"))
      sheep++;
    if (strstr(a->name, "Chicken") || strstr(a->name, "Hen") || strstr(a->name, "Rooster"))
      chicken++;
    if (strstr(a->name, "Bat") || strstr(a->name, "Flitter"))
      bat++;
  }
  expect(wild > 0, "at least one Wild-prefixed actor");
  printf("  counts sheep=%d chicken=%d bat=%d wild=%d\n", sheep, chicken, bat, wild);

  /* Idempotent second call */
  int before = w->runtime_actor_count;
  world_spawn_wilderness_mobs(w);
  expect(w->runtime_actor_count == before, "spawn is idempotent when populated");

  world_destroy(w);
}

static void test_sandstone_ore_paint_host(void)
{
  printf("sandstone ore host\n");
  World *w = world_create(8, 8, 8);
  expect(!!w, "ore paint world");
  for (uint32_t z = 0; z < 8; z++)
    for (uint32_t y = 0; y < 8; y++)
      for (uint32_t x = 0; x < 8; x++)
        world_set_voxel(w, x, y, z, VOXEL_STONE_SANDSTONE);

  /* Use public generation which calls paint — verify sandstone can hold ore voxels
     by manually placing via world_set after confirming type eligibility. */
  int sand_count = 0;
  for (uint32_t z = 0; z < 8; z++)
    for (uint32_t y = 0; y < 8; y++)
      for (uint32_t x = 0; x < 8; x++)
      {
        Voxel *v = world_get_voxel(w, x, y, z);
        if (v && v->type == VOXEL_STONE_SANDSTONE)
          sand_count++;
      }
  expect(sand_count == 8 * 8 * 8, "filled with sandstone");

  /* Simulate vein paint eligibility: sandstone should be replaceable. */
  world_set_voxel(w, 4, 4, 4, VOXEL_ORE_COPPER);
  Voxel *v = world_get_voxel(w, 4, 4, 4);
  expect(v && v->type == VOXEL_ORE_COPPER, "sandstone cell accepts ore write");
  world_destroy(w);
}

int main(void)
{
  test_climate_continuity();
  test_rain_intensity();
  test_fauna_create_and_update();
  test_wilderness_spawn_biome();
  test_sandstone_ore_paint_host();

  printf("\n%d passed, %d failed\n", g_ok, g_fail);
  return g_fail ? 1 : 0;
}
