#include "fluid_navier_stokes.h"
#include "world.h"
#include <stdlib.h>
#include <string.h>

static size_t idx3(size_t x, size_t y, size_t z, size_t w, size_t h) {
  return (z * h + y) * w + x;
}

static void *xcalloc(size_t n, size_t sz) {
  void *p = calloc(n, sz);
  return p;
}

NSField *ns_create_for_world(const World *world) {
  if (!world) return NULL;
  NSField *f = (NSField *)calloc(1, sizeof(NSField));
  if (!f) return NULL;
  f->width = world->width;
  f->height = world->height;
  f->depth = world->depth;
  size_t n = (size_t)f->width * (size_t)f->height * (size_t)f->depth;
  f->velocity_x = (float *)xcalloc(n, sizeof(float));
  f->velocity_y = (float *)xcalloc(n, sizeof(float));
  f->velocity_z = (float *)xcalloc(n, sizeof(float));
  f->pressure   = (float *)xcalloc(n, sizeof(float));
  f->solid_mask = (uint8_t *)xcalloc(n, sizeof(uint8_t));
  f->water_density = (float *)xcalloc(n, sizeof(float));
  if (!f->velocity_x || !f->velocity_y || !f->velocity_z || !f->pressure || !f->solid_mask || !f->water_density) {
    ns_destroy(f);
    return NULL;
  }
  return f;
}

void ns_destroy(NSField *f) {
  if (!f) return;
  free(f->velocity_x);
  free(f->velocity_y);
  free(f->velocity_z);
  free(f->pressure);
  free(f->solid_mask);
  free(f->water_density);
  free(f);
}

void ns_sync_from_world(NSField *f, const World *world) {
  if (!f || !world) return;
  if (f->width != world->width || f->height != world->height || f->depth != world->depth) {
    // Dimensions mismatch; skip for now.
    return;
  }
  size_t w = f->width, h = f->height, d = f->depth;
  for (size_t z = 0; z < d; z++)
    for (size_t y = 0; y < h; y++)
      for (size_t x = 0; x < w; x++) {
        Voxel *v = world_get_voxel((World *)world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        size_t i = idx3(x, y, z, w, h);
        uint8_t solid = 0;
        float density = 0.0f;
        if (v) {
          if (v->type == VOXEL_WATER) density = 1.0f;
          if (v->type != VOXEL_AIR && v->type != VOXEL_WATER && v->type != VOXEL_LEAVES) solid = 1;
        }
        f->solid_mask[i] = solid;
        f->water_density[i] = density;
      }
}

void ns_step(NSField *f, double dt_seconds) {
  (void)dt_seconds;
  if (!f) return;
  // Stub: Intentionally no-op for now. Real implementation would:
  // 1) Advect velocities, 2) Add forces (gravity), 3) Enforce incompressibility via pressure projection,
  // 4) Apply boundary conditions from solid_mask, 5) Update water_density from velocities.
}


