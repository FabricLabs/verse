#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "fog_of_war.h"
#include "shadow_world.h"
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

static World *make_empty_world(uint32_t size)
{
  World *w = world_create(size, size, size);
  if (!w)
    return NULL;
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
        world_set_voxel(w, x, y, z, VOXEL_AIR);
  return w;
}

static void test_chunk_lazy_allocation(void)
{
  printf("chunk lazy allocation\n");
  FogAtlas *atlas = fog_atlas_create();
  World *w = make_empty_world(32);
  check(atlas != NULL && w != NULL, "fixtures created");
  check(fog_atlas_chunk_count(atlas) == 0, "starts with no chunks");
  check(!fog_is_explored(atlas, w, 1, 2, 3), "unmarked voxel is unexplored");
  fog_mark_explored(atlas, w, 1, 2, 3);
  check(fog_atlas_chunk_count(atlas) == 1, "one mark allocates one chunk");
  check(fog_is_explored(atlas, w, 1, 2, 3), "marked voxel is explored");
  check(!fog_is_explored(atlas, w, 4, 2, 3), "neighbor remains unexplored");
  world_destroy(w);
  fog_atlas_destroy(atlas);
}

static void test_revision_and_bounds(void)
{
  printf("revision and bounds\n");
  FogAtlas *atlas = fog_atlas_create();
  World *w = make_empty_world(16);
  const uint64_t r0 = fog_atlas_revision(atlas);
  fog_mark_explored(atlas, w, 0, 0, 0);
  check(fog_atlas_revision(atlas) == r0 + 1, "revision bumps on first mark");
  fog_mark_explored(atlas, w, 0, 0, 0);
  check(fog_atlas_revision(atlas) == r0 + 1, "re-marking same voxel does not bump revision");
  check(!fog_is_explored(atlas, w, 99, 0, 0), "out of bounds reads as unexplored");
  world_destroy(w);
  fog_atlas_destroy(atlas);
}

static void test_reveal_ray_occlusion(void)
{
  printf("reveal ray occlusion\n");
  FogAtlas *atlas = fog_atlas_create();
  World *w = make_empty_world(32);
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t z = 0; z < w->depth; z++)
      world_set_voxel(w, 16, y, z, VOXEL_STONE);

  ShadowWorld *sw = shadow_world_create(32, 32, 32);
  check(sw != NULL, "shadow world created");
  shadow_world_set_centre(sw, 0, 0, 0);
  shadow_world_attach(sw, 0, 0, 0, w);
  shadow_world_refresh(sw);
  check(shadow_world_slot_world(sw, SHADOW_CENTRE_SLOT) == w, "world attached at centre");

  fog_reveal_from_view(atlas, sw, 4.0f, 16.0f, 16.0f, 0.0f, 0.0f, 60.0f, 1.0f, 32.0f);

  check(fog_is_explored(atlas, w, 8, 16, 16), "air along ray is explored");
  check(fog_is_explored(atlas, w, 16, 16, 16), "blocking wall is explored");
  check(!fog_is_explored(atlas, w, 20, 16, 16), "solid behind wall stays hidden");
  check(fog_is_explored(atlas, w, 8, 18, 16), "off-axis air in the cone is filled");
  check(fog_is_explored(atlas, w, 16, 18, 16), "off-axis wall in the cone is filled");
  check(!fog_is_explored(atlas, w, 20, 18, 16), "off-axis solid behind wall stays hidden");

  shadow_world_destroy(sw);
  world_destroy(w);
  fog_atlas_destroy(atlas);
}

static void test_player_bubble(void)
{
  printf("player bubble\n");
  FogAtlas *atlas = fog_atlas_create();
  World *w = make_empty_world(32);
  ShadowWorld *sw = shadow_world_create(32, 32, 32);
  shadow_world_attach(sw, 0, 0, 0, w);
  shadow_world_refresh(sw);

  fog_reveal_from_view(atlas, sw, 16.0f, 16.0f, 16.0f, 0.0f, 0.0f, 60.0f, 1.0f, 0.0f);

  check(fog_is_explored(atlas, w, 16, 16, 16), "player cell explored");
  check(fog_is_explored(atlas, w, 18, 16, 16), "nearby cell in bubble explored");

  shadow_world_destroy(sw);
  world_destroy(w);
  fog_atlas_destroy(atlas);
}

static void test_reveal_around_for_map(void)
{
  printf("reveal around for map\n");
  FogAtlas *atlas = fog_atlas_create();
  World *w = make_empty_world(64);
  ShadowWorld *sw = shadow_world_create(64, 64, 64);
  check(atlas != NULL && w != NULL && sw != NULL, "fixtures created");
  shadow_world_attach(sw, 0, 0, 0, w);
  shadow_world_refresh(sw);

  fog_reveal_around(atlas, sw, 32.0f, 32.0f, 32.0f, 8);
  check(fog_is_explored(atlas, w, 32, 32, 32), "centre explored");
  check(fog_is_explored(atlas, w, 38, 32, 32), "cell inside radius explored");
  check(!fog_is_explored(atlas, w, 50, 32, 32), "cell outside radius stays hidden");

  const size_t chunks = fog_atlas_chunk_count(atlas);
  fog_reveal_around(atlas, sw, 32.1f, 32.0f, 32.0f, 8); // within pose epsilon
  check(fog_atlas_chunk_count(atlas) == chunks, "tiny move early-outs without more work");

  shadow_world_destroy(sw);
  world_destroy(w);
  fog_atlas_destroy(atlas);
}

static void test_reveal_fills_view_cone(void)
{
  printf("reveal fills view cone\n");
  FogAtlas *atlas = fog_atlas_create();
  World *w = make_empty_world(64);
  ShadowWorld *sw = shadow_world_create(64, 64, 64);
  check(atlas != NULL && w != NULL && sw != NULL, "fixtures created");
  shadow_world_attach(sw, 0, 0, 0, w);
  shadow_world_refresh(sw);

  fog_reveal_from_view(atlas, sw, 4.0f, 32.0f, 32.0f, 0.0f, 0.0f, 60.0f, 1.0f, 48.0f);

  check(fog_is_explored(atlas, w, 40, 32, 32), "far on-axis air is explored");
  check(fog_is_explored(atlas, w, 40, 35, 32), "far off-axis air in the cone is explored");
  check(fog_is_explored(atlas, w, 40, 32, 35), "far vertical off-axis air in the cone is explored");
  check(!fog_is_explored(atlas, w, 40, 56, 32), "air outside the cone stays hidden");

  shadow_world_destroy(sw);
  world_destroy(w);
  fog_atlas_destroy(atlas);
}

int main(void)
{
  printf("test-fog-of-war\n");
  test_chunk_lazy_allocation();
  test_revision_and_bounds();
  test_reveal_ray_occlusion();
  test_player_bubble();
  test_reveal_around_for_map();
  test_reveal_fills_view_cone();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
