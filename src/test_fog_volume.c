// Headless checks for volumetric fog inside steam voxels: body wakes carve a cavity, diffusion
// fills it back in, and calm patches retire so a still cloud costs nothing.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fog_volume.h"
#include "world.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                                          \
  do                                                                                              \
  {                                                                                               \
    g_checks++;                                                                                   \
    if (cond)                                                                                     \
      printf("  ok   " __VA_ARGS__);                                                              \
    else                                                                                          \
    {                                                                                             \
      printf("  FAIL " __VA_ARGS__);                                                              \
      g_failures++;                                                                               \
    }                                                                                             \
    printf("\n");                                                                                 \
  } while (0)

static World *make_steam_block(uint32_t size)
{
  World *w = world_create(size, size, size);
  if (!w)
    return NULL;
  for (uint32_t z = 0; z < size; z++)
    for (uint32_t y = 0; y < size; y++)
      for (uint32_t x = 0; x < size; x++)
        world_set_voxel(w, x, y, z, VOXEL_STEAM);
  return w;
}

static long long density_energy(const int16_t *d)
{
  long long e = 0;
  for (int i = 0; i < FOG_VOLUME_CELLS; i++)
    e += (long long)d[i] * d[i];
  return e;
}

static int density_min(const int16_t *d)
{
  int m = d[0];
  for (int i = 1; i < FOG_VOLUME_CELLS; i++)
    if (d[i] < m)
      m = d[i];
  return m;
}

static void test_wake_carves_cavity(void)
{
  printf("body wake carves a cavity\n");

  World *world = make_steam_block(8);
  CHECK(world != NULL, "could not create a steam world");
  if (!world)
    return;

  FogVolume *fog = fog_volume_create(16);
  CHECK(fog != NULL, "could not create the fog cache");
  if (!fog)
  {
    world_destroy(world);
    return;
  }

  CHECK(fog_volume_live_count(fog) == 0, "a fresh cache is empty");

  // Stand still in the middle of a steam voxel: should carve without needing velocity.
  fog_volume_body_wake(fog, world, 4.5f, 4.5f, 4.5f, 0.9f, 0.0f, 0.0f, 0.0f);
  CHECK(fog_volume_live_count(fog) > 0, "standing in steam did not create a patch");

  const int16_t *d = fog_volume_densities(fog, world, 4, 4, 4);
  CHECK(d != NULL, "no density field after a wake");
  if (d)
  {
    CHECK(density_min(d) < -(FOG_VOLUME_UNIT / 4),
          "wake did not carve a cavity (min=%d)", density_min(d));
    CHECK(density_energy(d) > 0, "wake left the volume flat");
  }

  uint32_t texels[FOG_VOLUME_DIM * FOG_VOLUME_DIM];
  CHECK(fog_volume_bake_face(fog, world, 4, 4, 4, FOG_FACE_TOP, 245, 245, 235, texels),
        "disturbed steam would not bake");

  // Air has no fog field.
  world_set_voxel(world, 0, 0, 0, VOXEL_AIR);
  fog_volume_body_wake(fog, world, 0.5f, 0.5f, 0.5f, 0.9f, 1.0f, 0.0f, 0.0f);
  CHECK(fog_volume_densities(fog, world, 0, 0, 0) == NULL, "air grew a fog patch");

  fog_volume_destroy(fog);
  world_destroy(world);
}

static void test_wake_dies_out(void)
{
  printf("wake dies out and retires\n");

  World *world = make_steam_block(4);
  FogVolume *fog = fog_volume_create(8);
  CHECK(world && fog, "setup failed");
  if (!world || !fog)
  {
    fog_volume_destroy(fog);
    world_destroy(world);
    return;
  }

  fog_volume_body_wake(fog, world, 2.0f, 2.0f, 2.0f, 1.0f, 2.0f, 0.0f, 0.0f);
  CHECK(fog_volume_live_count(fog) > 0, "moving wake created no patch");

  for (int i = 0; i < 4000; i++)
    fog_volume_step(fog, 1.0f / (float)FOG_VOLUME_HZ);

  CHECK(fog_volume_live_count(fog) == 0, "%d patches are still ringing after a single wake",
        fog_volume_live_count(fog));
  CHECK(!fog_volume_bake_face(fog, world, 2, 2, 2, FOG_FACE_TOP, 245, 245, 235,
                              (uint32_t[FOG_VOLUME_DIM * FOG_VOLUME_DIM]){0}),
        "a calm volume still baked");

  fog_volume_destroy(fog);
  world_destroy(world);
}

static void test_bow_wave_ahead(void)
{
  printf("moving body piles fog ahead\n");

  World *world = make_steam_block(8);
  FogVolume *fog = fog_volume_create(16);
  CHECK(world && fog, "setup failed");
  if (!world || !fog)
  {
    fog_volume_destroy(fog);
    world_destroy(world);
    return;
  }

  // Sprint +X through the centre of voxel (4,4,4).
  fog_volume_body_wake(fog, world, 4.5f, 4.5f, 4.5f, 0.8f, 4.0f, 0.0f, 0.0f);
  const int16_t *d = fog_volume_densities(fog, world, 4, 4, 4);
  CHECK(d != NULL, "no field after a sprint wake");
  if (d)
  {
    // Ahead of the body (+X half) should hold more positive density than behind.
    long long ahead = 0, behind = 0;
    for (int k = 0; k < FOG_VOLUME_DIM; k++)
      for (int j = 0; j < FOG_VOLUME_DIM; j++)
        for (int i = 0; i < FOG_VOLUME_DIM; i++)
        {
          const int v = d[(k * FOG_VOLUME_DIM + j) * FOG_VOLUME_DIM + i];
          if (v <= 0)
            continue;
          if (i > FOG_VOLUME_DIM / 2)
            ahead += v;
          else
            behind += v;
        }
    CHECK(ahead > behind, "bow wave was not ahead of the body (ahead=%lld behind=%lld)", ahead,
          behind);
  }

  fog_volume_destroy(fog);
  world_destroy(world);
}

int main(void)
{
  printf("Fog volume\n----------\n");
  test_wake_carves_cavity();
  test_wake_dies_out();
  test_bow_wave_ahead();
  printf("\n=== %s (%d checks, %d failures) ===\n", g_failures ? "FAILED" : "ALL PASSED",
         g_checks, g_failures);
  return g_failures ? 1 : 0;
}
