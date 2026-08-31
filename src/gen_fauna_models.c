/* One-shot helper: write minimal fauna .world models into models/. */
#include "world.h"
#include <stdio.h>
#include <string.h>

static void fill_box(World *w, int x0, int y0, int z0, int x1, int y1, int z1, VoxelType t)
{
  for (int z = z0; z <= z1; z++)
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++)
        if (world_is_position_valid(w, x, y, z))
          world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, t);
}

static bool write_sheep(const char *path)
{
  World *w = world_create(16, 16, 16);
  if (!w)
    return false;
  /* body */
  fill_box(w, 4, 5, 3, 11, 10, 8, VOXEL_WOOL);
  /* head */
  fill_box(w, 11, 6, 5, 13, 9, 8, VOXEL_WOOL);
  fill_box(w, 13, 7, 6, 14, 8, 7, VOXEL_SOIL);
  /* legs */
  fill_box(w, 5, 5, 0, 6, 6, 2, VOXEL_WOOD);
  fill_box(w, 5, 9, 0, 6, 10, 2, VOXEL_WOOD);
  fill_box(w, 9, 5, 0, 10, 6, 2, VOXEL_WOOD);
  fill_box(w, 9, 9, 0, 10, 10, 2, VOXEL_WOOD);
  bool ok = world_save(w, path);
  world_destroy(w);
  return ok;
}

static bool write_chicken(const char *path)
{
  World *w = world_create(12, 12, 12);
  if (!w)
    return false;
  fill_box(w, 4, 4, 2, 7, 7, 5, VOXEL_SAND); /* body tan */
  fill_box(w, 7, 5, 3, 9, 6, 5, VOXEL_SAND); /* neck */
  fill_box(w, 9, 5, 4, 10, 6, 5, VOXEL_ORE_COPPER); /* comb-ish */
  fill_box(w, 4, 4, 0, 5, 5, 1, VOXEL_WOOD);
  fill_box(w, 4, 6, 0, 5, 7, 1, VOXEL_WOOD);
  fill_box(w, 3, 4, 3, 3, 7, 5, VOXEL_LEAVES_OAK); /* tail fluff */
  bool ok = world_save(w, path);
  world_destroy(w);
  return ok;
}

static bool write_bat(const char *path)
{
  World *w = world_create(16, 12, 10);
  if (!w)
    return false;
  fill_box(w, 6, 4, 3, 9, 7, 6, VOXEL_STONE_BASALT); /* body */
  fill_box(w, 9, 5, 4, 11, 6, 6, VOXEL_STONE_BASALT); /* head */
  /* wings */
  fill_box(w, 2, 2, 4, 5, 3, 5, VOXEL_LEAVES_MAPLE);
  fill_box(w, 2, 8, 4, 5, 9, 5, VOXEL_LEAVES_MAPLE);
  fill_box(w, 10, 2, 4, 13, 3, 5, VOXEL_LEAVES_MAPLE);
  fill_box(w, 10, 8, 4, 13, 9, 5, VOXEL_LEAVES_MAPLE);
  bool ok = world_save(w, path);
  world_destroy(w);
  return ok;
}

int main(void)
{
  int rc = 0;
  if (!write_sheep("models/sheep.world"))
  {
    fprintf(stderr, "sheep failed\n");
    rc = 1;
  }
  if (!write_chicken("models/chicken.world"))
  {
    fprintf(stderr, "chicken failed\n");
    rc = 1;
  }
  if (!write_bat("models/bat.world"))
  {
    fprintf(stderr, "bat failed\n");
    rc = 1;
  }
  return rc;
}
