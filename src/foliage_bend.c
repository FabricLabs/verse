#include "foliage_bend.h"

#include <math.h>

#include "world.h"

void foliage_bend_field_clear(FoliageBendField *field)
{
  if (field)
    field->count = 0;
}

bool foliage_bend_field_add(FoliageBendField *field, float x, float y, float z, float radius)
{
  if (!field || field->count >= FOLIAGE_BEND_MAX_BODIES)
    return false;
  if (radius < 0.05f)
    radius = 0.05f;
  FoliageBendBody *b = &field->bodies[field->count++];
  b->x = x;
  b->y = y;
  b->z = z;
  b->radius = radius;
  return true;
}

bool foliage_bend_affects(VoxelType type)
{
  // Passable canopy (tall grass, leaves) parts around a body. Bushes block movement but still
  // lean when a body presses against them. Ground lawn is excluded: shearing it opens gaps that
  // read as sky shining through the surface.
  return world_voxel_type_is_foliage(type) || voxel_type_is_bush(type);
}

static bool is_passable_foliage(VoxelType type)
{
  return world_voxel_type_is_foliage(type);
}

void foliage_bend_sample(const FoliageBendField *field, VoxelType type, float vx, float vy,
                         float vz, float *out_lean_x, float *out_lean_y, float *out_squash)
{
  float lean_x = 0.0f, lean_y = 0.0f, squash = 1.0f;
  if (out_lean_x)
    *out_lean_x = 0.0f;
  if (out_lean_y)
    *out_lean_y = 0.0f;
  if (out_squash)
    *out_squash = 1.0f;

  if (!field || field->count <= 0 || !foliage_bend_affects(type))
    return;

  // Passable foliage and bushes part around a body; unused low-grass path kept for strength tuning.
  const bool canopy = is_passable_foliage(type) || voxel_type_is_bush(type);
  const float lean_strength = canopy ? 0.55f : 0.12f;
  const float squash_strength = canopy ? 0.45f : 0.28f;

  float best_w = 0.0f;
  float best_dx = 0.0f, best_dy = 0.0f;
  float best_squash_w = 0.0f;

  for (int i = 0; i < field->count; i++)
  {
    const FoliageBendBody *b = &field->bodies[i];
    const float dx = vx - b->x;
    const float dy = vy - b->y;
    const float dz = vz - b->z;
    // Vertical reach: a body only parts foliage near its own height (feet in tall grass, head
    // in a canopy), not the whole column of the world.
    if (fabsf(dz) > b->radius * 1.25f + 0.75f)
      continue;

    const float dist_sq = dx * dx + dy * dy;
    const float r = b->radius;
    if (dist_sq >= r * r)
      continue;

    const float dist = sqrtf(dist_sq);
    const float w = 1.0f - dist / r;
    if (w > best_squash_w)
      best_squash_w = w;

    // On top of the body, lean has no horizontal direction — just squash. Off to the side,
    // push away so blades open a wake behind the walker.
    if (dist > 1e-4f && w > best_w)
    {
      best_w = w;
      best_dx = dx / dist;
      best_dy = dy / dist;
    }
  }

  lean_x = best_dx * best_w * lean_strength;
  lean_y = best_dy * best_w * lean_strength;
  squash = 1.0f - best_squash_w * squash_strength;
  if (squash < 0.35f)
    squash = 0.35f;

  if (out_lean_x)
    *out_lean_x = lean_x;
  if (out_lean_y)
    *out_lean_y = lean_y;
  if (out_squash)
    *out_squash = squash;
}

void foliage_bend_apply_corner(const FoliageBendField *field, VoxelType type, int ox, int oy,
                               int oz, float *cx, float *cy, float *cz)
{
  if (!cx || !cy || !cz || !field || field->count <= 0 || !foliage_bend_affects(type))
    return;

  float lean_x, lean_y, squash;
  foliage_bend_sample(field, type, (float)ox + 0.5f, (float)oy + 0.5f, (float)oz + 0.5f,
                      &lean_x, &lean_y, &squash);
  if (lean_x == 0.0f && lean_y == 0.0f && squash >= 0.999f)
    return;

  // Planted at the base: only the upper part of the stem swings.
  float t = *cz - (float)oz;
  if (t < 0.0f)
    t = 0.0f;
  else if (t > 1.0f)
    t = 1.0f;
  // Ease so the mid-stem starts moving before the tip finishes — reads as a blade, not a hinge.
  const float swing = t * t;

  *cx += lean_x * swing;
  *cy += lean_y * swing;
  *cz = (float)oz + (*cz - (float)oz) * squash;
}
