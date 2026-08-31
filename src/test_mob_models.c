// Tests for the sub-voxel mob model registry.
//
// The properties worth pinning down are the ones the renderers rely on: that a
// missing file does not take the process down, that each section is a multiple of
// 32 and actually contains the model, that the model sits on z=0 and is centred
// in x/y, and that birds and the Mud Golem resolve to the right slot.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mob_models.h"
#include "mob_ai.h"
#include "world.h"

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const char *what)
{
  g_checks++;
  if (condition)
    printf("  ok   %s\n", what);
  else
  {
    printf("  FAIL %s\n", what);
    g_failures++;
  }
}

static void solid_bounds(const World *w, int *min_x, int *min_y, int *min_z,
                         int *max_x, int *max_y, int *max_z, int *count)
{
  *min_x = (int)w->width;
  *min_y = (int)w->height;
  *min_z = (int)w->depth;
  *max_x = -1;
  *max_y = -1;
  *max_z = -1;
  *count = 0;
  for (int z = 0; z < (int)w->depth; z++)
    for (int y = 0; y < (int)w->height; y++)
      for (int x = 0; x < (int)w->width; x++)
      {
        const Voxel *v = world_voxel_cptr_fast(w, x, y, z);
        if (!v || v->type == VOXEL_AIR)
          continue;
        (*count)++;
        if (x < *min_x) *min_x = x;
        if (y < *min_y) *min_y = y;
        if (z < *min_z) *min_z = z;
        if (x > *max_x) *max_x = x;
        if (y > *max_y) *max_y = y;
        if (z > *max_z) *max_z = z;
      }
}

static void test_missing_file(void)
{
  printf("missing file\n");
  mob_models_shutdown();
  check(mob_models_init("/tmp/verse-no-such-mob-models-dir"),
        "init with a missing directory still succeeds");
  check(mob_models_get(MOB_MODEL_MUD_GOLEM) == NULL, "a missing golem slot is NULL");
  check(mob_models_get(MOB_MODEL_BIRD) == NULL, "a missing bird slot is NULL");
  mob_models_shutdown();
}

static void test_load_and_idempotent(void)
{
  printf("load\n");
  check(mob_models_init("models"), "mob_models_init succeeds against models/");
  check(mob_models_init("models"), "mob_models_init is idempotent");
  const MobModel *golem = mob_models_get(MOB_MODEL_MUD_GOLEM);
  const MobModel *bird = mob_models_get(MOB_MODEL_BIRD);
  check(golem && golem->loaded && golem->world, "the mud golem loaded");
  check(bird && bird->loaded && bird->world, "the bird loaded");
  const MobModel *sheep = mob_models_get(MOB_MODEL_SHEEP);
  const MobModel *chicken = mob_models_get(MOB_MODEL_CHICKEN);
  const MobModel *bat = mob_models_get(MOB_MODEL_BAT);
  check(sheep && sheep->loaded, "the sheep loaded");
  check(chicken && chicken->loaded, "the chicken loaded");
  check(bat && bat->loaded, "the bat loaded");
  const MobModel *deer = mob_models_get(MOB_MODEL_DEER);
  const MobModel *lizard = mob_models_get(MOB_MODEL_LIZARD);
  const MobModel *spider = mob_models_get(MOB_MODEL_SPIDER);
  const MobModel *slime = mob_models_get(MOB_MODEL_SLIME);
  check(deer && deer->loaded, "the deer loaded");
  check(lizard && lizard->loaded, "the lizard loaded");
  check(spider && spider->loaded, "the spider loaded");
  check(slime && slime->loaded, "the slime loaded");
  const MobModel *flesh = mob_models_get(MOB_MODEL_FLESH_WALKER);
  check(flesh && flesh->loaded, "the flesh walker loaded");
  if (flesh && flesh->world)
  {
    int bone = 0, flesh_n = 0;
    for (uint32_t z = 0; z < flesh->world->depth; z++)
      for (uint32_t y = 0; y < flesh->world->height; y++)
        for (uint32_t x = 0; x < flesh->world->width; x++)
        {
          const Voxel *v = world_voxel_cptr_fast(flesh->world, (int)x, (int)y, (int)z);
          if (!v || v->type == VOXEL_AIR)
            continue;
          if (v->type == VOXEL_BONE)
            bone++;
          else if (v->type == VOXEL_FLESH)
            flesh_n++;
        }
    check(bone > 50 && flesh_n > 200, "flesh walker has bone marrow and flesh shell");
  }
}

static void test_section_sizing(void)
{
  printf("section sizing\n");
  const MobModel *golem = mob_models_get(MOB_MODEL_MUD_GOLEM);
  const MobModel *bird = mob_models_get(MOB_MODEL_BIRD);
  if (!golem || !bird)
    return;

  check(golem->section_x % MOB_MODEL_SUBVOXELS == 0 &&
            golem->section_y % MOB_MODEL_SUBVOXELS == 0 &&
            golem->section_z % MOB_MODEL_SUBVOXELS == 0,
        "golem section axes are multiples of 32");
  check(bird->section_x % MOB_MODEL_SUBVOXELS == 0 &&
            bird->section_y % MOB_MODEL_SUBVOXELS == 0 &&
            bird->section_z % MOB_MODEL_SUBVOXELS == 0,
        "bird section axes are multiples of 32");

  check(golem->section_x == (int)golem->world->width &&
            golem->section_y == (int)golem->world->height &&
            golem->section_z == (int)golem->world->depth,
        "golem world dimensions match the section");
  check(bird->section_x == 32 && bird->section_y == 32 && bird->section_z == 32,
        "the bird occupies one 32^3 section");
  check(golem->section_z == 64 && golem->section_x == 64 && golem->section_y == 32,
        "the golem occupies a 64x32x64 section (two voxels tall)");
}

static void test_placement(void)
{
  printf("placement\n");
  const MobModel *models[5] = {
      mob_models_get(MOB_MODEL_MUD_GOLEM),
      mob_models_get(MOB_MODEL_BIRD),
      mob_models_get(MOB_MODEL_SHEEP),
      mob_models_get(MOB_MODEL_CHICKEN),
      mob_models_get(MOB_MODEL_BAT),
  };
  const char *names[5] = {"golem", "bird", "sheep", "chicken", "bat"};
  for (int i = 0; i < 5; i++)
  {
    const MobModel *m = models[i];
    if (!m)
      continue;
    int min_x, min_y, min_z, max_x, max_y, max_z, count;
    solid_bounds(m->world, &min_x, &min_y, &min_z, &max_x, &max_y, &max_z, &count);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s has solid voxels", names[i]);
    check(count > 0, buf);

    snprintf(buf, sizeof(buf), "%s rests on z=0", names[i]);
    check(min_z == 0, buf);

    const int pad_x0 = min_x;
    const int pad_x1 = m->section_x - 1 - max_x;
    const int pad_y0 = min_y;
    const int pad_y1 = m->section_y - 1 - max_y;
    snprintf(buf, sizeof(buf), "%s is centred in x", names[i]);
    check(abs(pad_x0 - pad_x1) <= 1, buf);
    snprintf(buf, sizeof(buf), "%s is centred in y", names[i]);
    check(abs(pad_y0 - pad_y1) <= 1, buf);
  }
}

static void test_actor_mapping(void)
{
  printf("actor mapping\n");
  MobActor *golem = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER, 1, 1, 1);
  MobActor *bird = mob_actor_create_bird(BIRD_KIND_CROW, 2, 2, 2);
  MobActor *solver = mob_actor_create("Solver", MOB_TYPE_SOLVER, 3, 3, 3);
  check(golem && bird && solver, "test actors can be created");
  if (!golem || !bird || !solver)
    return;

  golem->base.extra_data = golem;
  bird->base.extra_data = bird;
  solver->base.extra_data = solver;

  check(mob_models_for_actor(&golem->base) == mob_models_get(MOB_MODEL_MUD_GOLEM),
        "a Mud Golem maps to the golem model");
  check(mob_models_for_actor(&bird->base) == mob_models_get(MOB_MODEL_BIRD),
        "a bird maps to the shrike model");
  check(mob_models_for_actor(&solver->base) == NULL,
        "an unmapped mob returns NULL");
  check(mob_models_for_actor(NULL) == NULL, "NULL actor returns NULL");

  MobActor *sheep = mob_actor_create("Bare Sheep", MOB_TYPE_SHEEP, 4, 4, 4);
  MobActor *chicken = mob_actor_create("Bare Chicken", MOB_TYPE_CHICKEN, 5, 5, 5);
  MobActor *bat = mob_actor_create("Bare Bat", MOB_TYPE_BAT, 6, 6, 6);
  check(sheep && chicken && bat, "livestock test actors can be created");
  if (sheep && chicken && bat)
  {
    sheep->base.extra_data = sheep;
    chicken->base.extra_data = chicken;
    bat->base.extra_data = bat;
    check(mob_models_get(MOB_MODEL_SHEEP) != NULL, "the sheep voxel model loaded");
    check(mob_models_get(MOB_MODEL_CHICKEN) != NULL, "the chicken voxel model loaded");
    check(mob_models_get(MOB_MODEL_BAT) != NULL, "the bat voxel model loaded");
    check(mob_models_for_actor(&sheep->base) == mob_models_get(MOB_MODEL_SHEEP),
          "a mesh-free sheep maps to the voxel sheep");
    check(mob_models_for_actor(&chicken->base) == mob_models_get(MOB_MODEL_CHICKEN),
          "a mesh-free chicken maps to the voxel chicken");
    check(mob_models_for_actor(&bat->base) == mob_models_get(MOB_MODEL_BAT),
          "a mesh-free bat maps to the voxel bat");
  }

  mob_actor_bind_mesh(golem, "goleling");
  check(mob_models_for_actor(&golem->base) == NULL,
        "a polygon-mesh golem does not use the voxel model");
  mob_actor_bind_mesh(bird, "pigeon");
  check(mob_models_for_actor(&bird->base) == NULL,
        "a polygon-mesh bird does not use the voxel model");

  mob_actor_destroy(golem);
  mob_actor_destroy(bird);
  mob_actor_destroy(solver);
  if (sheep)
    mob_actor_destroy(sheep);
  if (chicken)
    mob_actor_destroy(chicken);
  if (bat)
    mob_actor_destroy(bat);
}

int main(void)
{
  test_missing_file();
  test_load_and_idempotent();
  test_section_sizing();
  test_placement();
  test_actor_mapping();
  mob_models_shutdown();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
