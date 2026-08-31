#include "voxel_shape.h"

#include "world.h"

#include <math.h>
#include <stdlib.h>

#define VS_S 32

float decoration_yaw_blend(uint32_t hash, int x, int y, int tx, int ty, float town_weight)
{
  const float rnd = ((float)(hash & 0xFFFFu) / 65535.0f) * VOXEL_YAW_TAU;
  if (town_weight <= 0.0f)
    return rnd;
  if (town_weight > 1.0f)
    town_weight = 1.0f;
  const float dx = (float)(tx - x);
  const float dy = (float)(ty - y);
  if (dx * dx + dy * dy < 1e-4f)
    return rnd;
  const float toward = atan2f(dy, dx);
  // Shortest-arc blend between random and town-facing.
  float d = toward - rnd;
  while (d > 3.14159265f)
    d -= VOXEL_YAW_TAU;
  while (d < -3.14159265f)
    d += VOXEL_YAW_TAU;
  return rnd + d * town_weight;
}

void world_paint_decoration_yaw(World *world, int x0, int y0, int z0, int x1, int y1, int z1,
                                uint8_t yaw_u8)
{
  if (!world)
    return;
  if (x0 > x1)
  {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (y0 > y1)
  {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (z0 > z1)
  {
    int t = z0;
    z0 = z1;
    z1 = t;
  }
  for (int z = z0; z <= z1; z++)
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++)
      {
        if (x < 0 || y < 0 || z < 0 || x >= (int)world->width || y >= (int)world->height ||
            z >= (int)world->depth)
          continue;
        Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (!v || v->type == VOXEL_AIR)
          continue;
        if (!world_voxel_type_has_material_gaps(v->type) && !voxel_has_nontrivial_shape(v))
          continue;
        voxel_set_yaw_u8(v, yaw_u8);
      }
}

const char *voxel_shape_name(VoxelShape shape)
{
  switch (shape)
  {
  case VOXEL_SHAPE_FULL: return "full";
  case VOXEL_SHAPE_SLAB: return "slab";
  case VOXEL_SHAPE_WEDGE: return "wedge";
  case VOXEL_SHAPE_CORNER: return "corner";
  case VOXEL_SHAPE_COUNT:
  default: return "unknown";
  }
}

// Map (sx,sy,sz) into a canonical frame for floor-ramp wedges: u runs along the slope, v across,
// w is up. Cardinal yaw 0..3; ceiling flips w.
static void vs_wedge_coords(uint8_t orient, int sx, int sy, int sz, int *u, int *v, int *w,
                            bool *ceiling, bool *vertical)
{
  const uint8_t o = (uint8_t)(orient % 12u);
  *ceiling = (o >= 4 && o < 8);
  *vertical = (o >= 8);
  const int yaw = (int)(o & 3u);
  int x = sx, y = sy, z = sz;
  switch (yaw)
  {
  case 1: // rise +Y: rotate so +Y → +X
  {
    const int t = x;
    x = y;
    y = VS_S - 1 - t;
    break;
  }
  case 2: // rise -X
    x = VS_S - 1 - x;
    break;
  case 3: // rise -Y
  {
    const int t = x;
    x = VS_S - 1 - y;
    y = t;
    break;
  }
  default:
    break;
  }
  if (*vertical)
  {
    *u = x;
    *v = z;
    *w = y;
  }
  else
  {
    *u = x;
    *v = y;
    *w = *ceiling ? (VS_S - 1 - z) : z;
  }
}

bool voxel_shape_occupies(VoxelShape shape, uint8_t orient, int sx, int sy, int sz)
{
  if (sx < 0 || sy < 0 || sz < 0 || sx >= VS_S || sy >= VS_S || sz >= VS_S)
    return false;

  switch (shape)
  {
  case VOXEL_SHAPE_FULL:
    return true;

  case VOXEL_SHAPE_SLAB:
  {
    const uint8_t o = (uint8_t)(orient % 6u);
    if (o == 0)
      return sz < VS_S / 2;
    if (o == 1)
      return sz >= VS_S / 2;
    if (o == 2)
      return sx < VS_S / 2;
    if (o == 3)
      return sx >= VS_S / 2;
    if (o == 4)
      return sy < VS_S / 2;
    return sy >= VS_S / 2;
  }

  case VOXEL_SHAPE_WEDGE:
  {
    int u, v, w;
    bool ceiling, vertical;
    vs_wedge_coords(orient, sx, sy, sz, &u, &v, &w, &ceiling, &vertical);
    (void)v;
    (void)ceiling;
    // 45°: solid when height ≤ along-slope (inclusive band so the diagonal seals).
    return w * (VS_S - 1) <= u * (VS_S - 1);
  }

  case VOXEL_SHAPE_CORNER:
  {
    int u, v, w;
    bool ceiling, vertical;
    vs_wedge_coords(orient, sx, sy, sz, &u, &v, &w, &ceiling, &vertical);
    (void)vertical;
    (void)ceiling;
    return w * (VS_S - 1) <= u * (VS_S - 1) && w * (VS_S - 1) <= v * (VS_S - 1);
  }

  case VOXEL_SHAPE_COUNT:
  default:
    return true;
  }
}

float voxel_shape_fill_ratio(VoxelShape shape, uint8_t orient)
{
  (void)orient;
  switch (shape)
  {
  case VOXEL_SHAPE_FULL: return 1.0f;
  case VOXEL_SHAPE_SLAB: return 0.5f;
  case VOXEL_SHAPE_WEDGE: return 0.5f;
  case VOXEL_SHAPE_CORNER: return 1.0f / 6.0f; // tetra ≈ 1/6 of cube
  default: return 1.0f;
  }
}

float voxel_shape_floor_height(VoxelShape shape, uint8_t orient, float u, float v)
{
  if (u < 0.0f)
    u = 0.0f;
  else if (u > 1.0f)
    u = 1.0f;
  if (v < 0.0f)
    v = 0.0f;
  else if (v > 1.0f)
    v = 1.0f;

  switch (shape)
  {
  case VOXEL_SHAPE_FULL:
    return 1.0f;
  case VOXEL_SHAPE_SLAB:
  {
    const uint8_t o = (uint8_t)(orient % 6u);
    if (o == 0)
      return 0.5f;
    if (o == 1)
      return 1.0f;
    return 1.0f; // side slabs still block full height for walking
  }
  case VOXEL_SHAPE_WEDGE:
  {
    const uint8_t o = (uint8_t)(orient % 12u);
    if (o >= 4)
      return 1.0f; // ceiling / vertical: treat as full for floor queries
    float t = u;
    if ((o & 3u) == 1)
      t = v;
    else if ((o & 3u) == 2)
      t = 1.0f - u;
    else if ((o & 3u) == 3)
      t = 1.0f - v;
    return t;
  }
  case VOXEL_SHAPE_CORNER:
  {
    const uint8_t o = (uint8_t)(orient % 4u);
    float a = u, b = v;
    if (o == 1)
    {
      a = v;
      b = 1.0f - u;
    }
    else if (o == 2)
    {
      a = 1.0f - u;
      b = 1.0f - v;
    }
    else if (o == 3)
    {
      a = 1.0f - v;
      b = u;
    }
    return (a < b) ? a : b;
  }
  default:
    return 1.0f;
  }
}

void voxel_shape_carve_world(World *w, VoxelShape shape, uint8_t orient)
{
  if (!w || shape == VOXEL_SHAPE_FULL)
    return;
  if ((int)w->width != VS_S || (int)w->height != VS_S || (int)w->depth != VS_S)
    return;
  for (int z = 0; z < VS_S; z++)
    for (int y = 0; y < VS_S; y++)
      for (int x = 0; x < VS_S; x++)
        if (!voxel_shape_occupies(shape, orient, x, y, z))
          world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_AIR);
}

void voxel_shape_fill_world(World *w, VoxelShape shape, uint8_t orient, VoxelType fill)
{
  if (!w)
    return;
  if ((int)w->width != VS_S || (int)w->height != VS_S || (int)w->depth != VS_S)
    return;
  for (int z = 0; z < VS_S; z++)
    for (int y = 0; y < VS_S; y++)
      for (int x = 0; x < VS_S; x++)
      {
        if (voxel_shape_occupies(shape, orient, x, y, z))
          world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, fill);
        else
          world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_AIR);
      }
}

bool world_set_voxel_shaped(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType type,
                            VoxelShape shape, uint8_t orient)
{
  if (!world_set_voxel(world, x, y, z, type))
    return false;
  Voxel *v = world_get_voxel(world, x, y, z);
  if (!v)
    return false;
  voxel_set_shape_orient(v, shape, orient);
  return true;
}

#undef VS_S
