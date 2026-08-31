// Headless checks for the foliage-bend field: bodies part nearby canopy and press ground grass.
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "foliage_bend.h"
#include "voxel.h"
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

int main(void)
{
  printf("Foliage bend\n------------\n");

  check(!foliage_bend_affects(VOXEL_STONE), "stone does not bend");
  check(!foliage_bend_affects(VOXEL_GRASS), "ground grass does not bend (shearing it opens sky holes)");
  check(foliage_bend_affects(VOXEL_GRASS_TALL), "tall grass bends");
  check(foliage_bend_affects(VOXEL_LEAVES), "leaves bend");
  check(foliage_bend_affects(VOXEL_BUSH), "bushes bend when a body presses them");
  check(foliage_bend_affects(VOXEL_BUSH_FERN), "fern bushes bend");
  check(world_voxel_type_has_material_gaps(VOXEL_BUSH),
        "bushes keep material gaps without being passable foliage");
  check(!world_voxel_type_is_foliage(VOXEL_BUSH), "bushes are not passable foliage");

  FoliageBendField field;
  foliage_bend_field_clear(&field);
  check(field.count == 0, "cleared field is empty");
  check(foliage_bend_field_add(&field, 10.0f, 10.0f, 5.0f, 2.0f), "body added");

  float lx, ly, sq;
  foliage_bend_sample(&field, VOXEL_GRASS_TALL, 10.0f, 10.0f, 5.0f, &lx, &ly, &sq);
  check(fabsf(lx) < 1e-4f && fabsf(ly) < 1e-4f, "on top of a body there is no horizontal lean");
  check(sq < 0.95f, "on top of a body the stem squashes");

  foliage_bend_sample(&field, VOXEL_GRASS_TALL, 11.5f, 10.0f, 5.0f, &lx, &ly, &sq);
  check(lx > 0.05f, "tall grass beside a body leans away (+x)");
  check(fabsf(ly) < 0.05f, "lean is along the push axis");

  float cx = 11.5f, cy = 10.5f, cz = 6.0f; // top of voxel at (11,10,5)
  foliage_bend_apply_corner(&field, VOXEL_GRASS_TALL, 11, 10, 5, &cx, &cy, &cz);
  check(cx > 11.5f, "top corner swings away from the body");
  check(cz < 6.0f, "top corner drops as the stem squashes");

  float gx = 10.5f, gy = 10.5f, gz = 6.0f;
  foliage_bend_apply_corner(&field, VOXEL_GRASS, 10, 10, 5, &gx, &gy, &gz);
  check(gz == 6.0f && gx == 10.5f, "ground grass corners stay planted");

  foliage_bend_sample(&field, VOXEL_GRASS_TALL, 10.0f, 10.0f, 20.0f, &lx, &ly, &sq);
  check(sq > 0.99f && fabsf(lx) < 1e-4f, "foliage far above a body is undisturbed");

  printf("\n=== %s (%d checks, %d failures) ===\n", g_failures ? "FAILED" : "ALL PASSED",
         g_checks, g_failures);
  return g_failures ? 1 : 0;
}
