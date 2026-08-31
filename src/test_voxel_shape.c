#include "voxel_shape.h"
#include "world.h"
#include "voxel_mesh.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void check(bool ok, const char *msg)
{
  if (ok)
    printf("  ok   %s\n", msg);
  else
  {
    printf("  FAIL %s\n", msg);
    g_failed++;
  }
}

static void test_packing(void)
{
  printf("\nPacking\n-------\n");
  check(voxel_pack_shape_orient(VOXEL_SHAPE_FULL, 0) == 0, "full+0 packs to zero (save-compatible)");
  check(voxel_unpack_shape(voxel_pack_shape_orient(VOXEL_SHAPE_WEDGE, 3)) == VOXEL_SHAPE_WEDGE,
        "wedge survives pack/unpack");
  check(voxel_unpack_orient(voxel_pack_shape_orient(VOXEL_SHAPE_WEDGE, 3)) == 3,
        "orient 3 survives pack/unpack");

  Voxel v;
  memset(&v, 0, sizeof(v));
  v.type = VOXEL_BRICK;
  check(!voxel_has_nontrivial_shape(&v), "fresh voxel is full");
  voxel_set_shape_orient(&v, VOXEL_SHAPE_WEDGE, 0);
  check(voxel_get_shape(&v) == VOXEL_SHAPE_WEDGE, "setter stores wedge");
  check(voxel_get_orient(&v) == 0, "setter stores orient 0");
  check(voxel_has_nontrivial_shape(&v), "wedge is nontrivial");
  check(world_voxel_needs_nested_silhouette(&v), "shaped brick needs nested silhouette");
  check(world_voxel_omits_parent_aabb(&v), "shaped brick omits parent AABB");
  v.type = VOXEL_LEAVES_OAK;
  voxel_set_shape_orient(&v, VOXEL_SHAPE_FULL, 0);
  check(world_voxel_needs_nested_silhouette(&v), "leaves still have material gaps");
  check(!world_voxel_omits_parent_aabb(&v), "leaves keep parent AABB under skip_sparse");
}

static void test_occupancy(void)
{
  printf("\nOccupancy\n---------\n");
  check(voxel_shape_occupies(VOXEL_SHAPE_FULL, 0, 0, 0, 0), "full occupies origin");
  check(voxel_shape_occupies(VOXEL_SHAPE_FULL, 0, 31, 31, 31), "full occupies far corner");

  // Floor wedge rise +X: solid when z <= x
  check(voxel_shape_occupies(VOXEL_SHAPE_WEDGE, 0, 16, 8, 8), "wedge +X solid below diagonal");
  check(!voxel_shape_occupies(VOXEL_SHAPE_WEDGE, 0, 8, 8, 24), "wedge +X empty above diagonal");

  // Slab bottom
  check(voxel_shape_occupies(VOXEL_SHAPE_SLAB, 0, 10, 10, 8), "bottom slab solid low");
  check(!voxel_shape_occupies(VOXEL_SHAPE_SLAB, 0, 10, 10, 24), "bottom slab empty high");

  const float fill_w = voxel_shape_fill_ratio(VOXEL_SHAPE_WEDGE, 0);
  check(fill_w > 0.4f && fill_w < 0.6f, "wedge fill ratio ≈ ½");

  const float h0 = voxel_shape_floor_height(VOXEL_SHAPE_WEDGE, 0, 0.0f, 0.5f);
  const float h1 = voxel_shape_floor_height(VOXEL_SHAPE_WEDGE, 0, 1.0f, 0.5f);
  check(h0 < 0.05f && h1 > 0.95f, "floor height rises along +X for orient 0");
}

static void test_mesh_skip(void)
{
  printf("\nMesh skip\n---------\n");
  World *w = world_create(8, 8, 8);
  check(w != NULL, "world created");
  if (!w)
    return;

  world_set_voxel(w, 2, 2, 2, VOXEL_STONE);
  world_set_voxel_shaped(w, 3, 2, 2, VOXEL_BRICK, VOXEL_SHAPE_WEDGE, 0);
  world_set_voxel(w, 4, 2, 2, VOXEL_STONE);

  VoxelMesh mesh;
  voxel_mesh_init(&mesh);
  voxel_mesh_build_all_faces_greedy_ex(w, &mesh, true);

  int brick_quads = 0, stone_quads = 0;
  for (int i = 0; i < mesh.count; i++)
  {
    if (mesh.quads[i].type == VOXEL_BRICK)
      brick_quads++;
    if (mesh.quads[i].type == VOXEL_STONE)
      stone_quads++;
  }
  printf("       stone_quads=%d brick_wedge_quads=%d\n", stone_quads, brick_quads);
  check(stone_quads > 0, "full stone still emits cube faces");
  check(brick_quads == 0, "shaped brick omitted from greedy mesh (nested path)");

  VoxelMesh carved;
  voxel_mesh_init(&carved);
  World *sw = world_create(32, 32, 32);
  voxel_shape_fill_world(sw, VOXEL_SHAPE_WEDGE, 0, VOXEL_BRICK);
  voxel_mesh_build_all_faces_greedy_ex(sw, &carved, false);
  printf("       nested wedge quads=%d\n", carved.count);
  check(carved.count > 6, "filled wedge mesh is not a six-face cube");
  check(carved.count < 6 * 32 * 32, "wedge mesh is far smaller than a solid cube");

  voxel_mesh_free(&mesh);
  voxel_mesh_free(&carved);
  world_destroy(sw);
  world_destroy(w);
}

static void test_names(void)
{
  printf("\nNames\n-----\n");
  check(strcmp(voxel_shape_name(VOXEL_SHAPE_WEDGE), "wedge") == 0, "wedge name");
  check(strcmp(voxel_shape_name(VOXEL_SHAPE_CORNER), "corner") == 0, "corner name");
}

static void test_yaw(void)
{
  printf("\nDecoration yaw\n---------------\n");
  const uint8_t u = voxel_yaw_u8_from_radians(VOXEL_YAW_TAU * 0.25f);
  const float back = voxel_yaw_radians_from_u8(u);
  check(back > 1.4f && back < 1.8f, "yaw byte round-trips ~π/2");

  Voxel v;
  memset(&v, 0, sizeof(v));
  v.type = VOXEL_BUSH;
  voxel_set_yaw_radians(&v, 1.0f);
  check(fabsf(voxel_get_yaw_radians(&v) - 1.0f) < 0.05f, "voxel stores yaw");

  const float rnd = decoration_yaw_blend(0x1234u, 0, 0, 10, 0, 0.0f);
  const float face = decoration_yaw_blend(0x1234u, 0, 0, 10, 0, 1.0f);
  check(fabsf(face) < 0.25f || fabsf(face - VOXEL_YAW_TAU) < 0.25f,
        "town_weight=1 faces +X toward target");
  (void)rnd;
}

int main(void)
{
  printf("=== Voxel Shape Modifier Tests ===\n");
  test_packing();
  test_occupancy();
  test_mesh_skip();
  test_names();
  test_yaw();
  printf("\n%s (%d failures)\n", g_failed ? "FAIL" : "PASS", g_failed);
  return g_failed ? 1 : 0;
}
