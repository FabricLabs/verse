#include <stdio.h>
#include <string.h>

#include "fire_sim.h"
#include "world.h"
#include "world_bulk_ops.h"
#include "particle_effects.h"

static int failures;

static void check(int ok, const char *msg)
{
  if (ok)
    printf("  ok %s\n", msg);
  else
  {
    printf("  FAIL %s\n", msg);
    failures++;
  }
}

static World *make_room(void)
{
  World *w = world_create(12, 12, 6);
  world_fill_region(w, 0, 0, 0, 12, 12, 1, VOXEL_STONE, BULK_OP_REPLACE, NULL, NULL);
  return w;
}

static void test_ignite_dry_wood(void)
{
  printf("ignite dry wood\n");
  World *w = make_room();
  world_set_voxel(w, 5, 5, 1, VOXEL_WOOD_OAK);
  check(fire_can_ignite_at(w, 5, 5, 1), "dry oak can ignite");
  check(fire_ignite_at(w, 5, 5, 1, "BURNING_MEDIUM"), "ignite succeeds");
  check(world_has_voxel_condition(w, 5, 5, 1, "BURNING_MEDIUM"), "burning medium set");
  check(!fire_can_ignite_at(w, 5, 5, 1), "already burning cannot re-qualify as dry");
  world_destroy(w);
}

static void test_wet_will_not_ignite(void)
{
  printf("wet surfaces refuse a spark\n");
  World *w = make_room();
  world_set_voxel(w, 5, 5, 1, VOXEL_WOOD_PINE);
  world_add_voxel_condition(w, 5, 5, 1, "WET");
  check(!fire_can_ignite_at(w, 5, 5, 1), "wet pine cannot ignite");
  check(!fire_ignite_at(w, 5, 5, 1, "BURNING_LOW"), "ignite refuses wet fuel");
  world_destroy(w);
}

static void test_water_extinguishes(void)
{
  printf("water extinguishes fire\n");
  World *w = make_room();
  world_set_voxel(w, 5, 5, 1, VOXEL_CAMPFIRE);
  check(fire_ignite_at(w, 5, 5, 1, "BURNING_HIGH"), "campfire lit");
  check(fire_voxel_is_burning(world_get_voxel(w, 5, 5, 1)), "campfire burning");

  // Pour water beside it.
  world_set_voxel(w, 6, 5, 1, VOXEL_WATER);
  check(fire_try_extinguish_with_water(w, 5, 5, 1), "adjacent water puts it out");
  check(!fire_voxel_is_burning(world_get_voxel(w, 5, 5, 1)), "no longer burning");
  check(world_has_voxel_condition(w, 5, 5, 1, "WET"), "left wet after dousing");

  // Soaked campfire will not re-light until dried.
  check(!fire_ignite_at(w, 5, 5, 1, "BURNING_HIGH"), "wet campfire will not re-light");
  world_destroy(w);
}

static void test_spread_to_neighbour(void)
{
  printf("fire spreads to dry neighbour\n");
  World *w = make_room();
  // Campfire does not consume itself; it is a stable ignition source for the neighbour.
  world_set_voxel(w, 5, 5, 1, VOXEL_CAMPFIRE);
  world_set_voxel(w, 6, 5, 1, VOXEL_WOOD_OAK);
  fire_ignite_at(w, 5, 5, 1, "BURNING_HIGH");

  int caught = 0;
  for (int i = 0; i < 80 && !caught; i++)
  {
    fire_sim_step(w, 1.0f / 20.0f);
    if (world_has_voxel_condition(w, 6, 5, 1, "BURNING_LOW") ||
        world_has_voxel_condition(w, 6, 5, 1, "BURNING_MEDIUM") ||
        world_has_voxel_condition(w, 6, 5, 1, "BURNING_HIGH"))
      caught = 1;
  }
  check(caught, "neighbouring dry wood eventually catches");
  world_destroy(w);
}

static void test_flame_particles_follow_burning(void)
{
  printf("particles track burning state\n");
  World *w = make_room();
  world_set_voxel(w, 5, 5, 1, VOXEL_CAMPFIRE);
  fire_ignite_at(w, 5, 5, 1, "BURNING_HIGH");
  particle_effects_init_for_world(w);

  int flames = 0;
  for (int i = 0; i < 20; i++)
  {
    particle_effects_update(w, 1.0f / 30.0f, 5.5f, 5.5f, 2.0f, 64, NULL);
    WorldParticleEffects *e = w->particle_effects;
    for (int p = 0; p < e->particle_count; p++)
      if (e->particles[p].kind == PARTICLE_KIND_FLAME)
        flames++;
  }
  check(flames > 5, "lit campfire emits flame particles");

  world_set_voxel(w, 5, 6, 1, VOXEL_WATER);
  fire_try_extinguish_with_water(w, 5, 5, 1);

  // Drain existing particles, then confirm no new flames spawn.
  for (int i = 0; i < 60; i++)
    particle_effects_update(w, 1.0f / 30.0f, 5.5f, 5.5f, 2.0f, 64, NULL);

  int after = 0;
  WorldParticleEffects *e = w->particle_effects;
  for (int p = 0; p < e->particle_count; p++)
    if (e->particles[p].kind == PARTICLE_KIND_FLAME)
      after++;
  check(after == 0, "extinguished campfire stops emitting");
  world_destroy(w);
}

static void test_stone_not_flammable(void)
{
  printf("stone is not fuel\n");
  check(!voxel_type_is_flammable(VOXEL_STONE), "stone not flammable");
  check(voxel_type_is_flammable(VOXEL_WOOD), "wood is flammable");
  check(voxel_type_is_flammable(VOXEL_GRASS_TALL), "tall grass is flammable");
  check(voxel_type_is_flammable(VOXEL_CANDLE), "candle is flammable");
}

static void test_meteor_storm_impacts(void)
{
  printf("meteor / lava storm\n");
  World *w = make_room();
  particle_effects_init_for_world(w);
  particle_effects_set_meteor_storm_universe_enabled(true);
  particle_effects_set_meteor_storm_enabled(w, true);
  check(particle_effects_meteor_storm_enabled(w), "storm enabled on world");
  check(particle_effects_meteor_storm_universe_enabled(), "storm sticky for new worlds");

  int meteors = 0;
  int magma = 0;
  for (int i = 0; i < 90; i++)
  {
    particle_effects_update(w, 1.0f / 30.0f, 6.0f, 6.0f, 4.0f, 64, NULL);
    WorldParticleEffects *e = w->particle_effects;
    for (int p = 0; p < e->particle_count; p++)
      if (e->particles[p].kind == PARTICLE_KIND_METEOR)
        meteors++;
  }
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
      {
        Voxel *v = world_get_voxel(w, x, y, z);
        if (v && v->type == VOXEL_MAGMA)
          magma++;
      }

  check(meteors > 0, "storm spawns falling meteors");
  check(magma > 0, "meteors splash magma on impact");

  particle_effects_set_meteor_storm_enabled(w, false);
  particle_effects_set_meteor_storm_universe_enabled(false);
  check(!particle_effects_meteor_storm_enabled(w), "storm can be cleared");
  world_destroy(w);
}

int main(void)
{
  printf("=== fire simulation ===\n");
  test_stone_not_flammable();
  test_ignite_dry_wood();
  test_wet_will_not_ignite();
  test_water_extinguishes();
  test_spread_to_neighbour();
  test_flame_particles_follow_burning();
  test_meteor_storm_impacts();
  printf(failures ? "\n%d failures\n" : "\nall passed\n", failures);
  return failures ? 1 : 0;
}
