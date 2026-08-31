#include "gpu_physics.h"
#include <math.h>

bool gpu_physics_can_move_to(World *world, int x, int y, int z)
{
  if (!world)
    return false;
  if (x < 0 || y < 0 || z < 0 || x >= (int)world->width || y >= (int)world->height || z >= (int)world->depth)
    return false;
  Voxel *vt = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
  return (vt && vt->type == VOXEL_AIR);
}

bool gpu_physics_try_move_actor(World *world, Actor *actor, int dx, int dy, int dz)
{
  if (!world || !actor)
    return false;
  int ix = (int)floor(actor->x + 0.0001) + dx;
  int iy = (int)floor(actor->y + 0.0001) + dy;
  int iz = (int)floor(actor->z + 0.0001) + dz;
  if (!gpu_physics_can_move_to(world, ix, iy, iz))
    return false;
  int old_ix = (int)floor(actor->x + 0.0001);
  int old_iy = (int)floor(actor->y + 0.0001);
  int old_iz = (int)floor(actor->z + 0.0001);
  // Clear old ACTOR marker
  if (old_ix >= 0 && old_iy >= 0 && old_iz >= 0 && old_ix < (int)world->width && old_iy < (int)world->height && old_iz < (int)world->depth)
  {
    Voxel *vold = world_get_voxel(world, (uint32_t)old_ix, (uint32_t)old_iy, (uint32_t)old_iz);
    if (vold && vold->type == VOXEL_ACTOR)
      world_set_voxel(world, (uint32_t)old_ix, (uint32_t)old_iy, (uint32_t)old_iz, VOXEL_AIR);
  }
  // Move and set new marker
  actor->x = (double)ix;
  actor->y = (double)iy;
  actor->z = (double)iz;
  world_set_voxel(world, (uint32_t)ix, (uint32_t)iy, (uint32_t)iz, VOXEL_ACTOR);
  return true;
}


