#include "projectile.h"

#include <math.h>
#include <string.h>

// world.h only forward-declares struct Actor, and hit detection reads its position and health.
#include "actor.h"
#include "constants.h"
#include "universe.h"

// A projectile's foliage rolls have to be reproducible, so this is a plain integer hash of the
// projectile's seed and the voxel coordinate rather than a running RNG. Two projectiles crossing the
// same leaf get different answers; the same projectile crossing it twice gets the same one.
//
// That last property is load-bearing, not incidental: it is what makes a shot's fate independent of
// the time step, since sampling the same leaf more often cannot change the answer. Replacing this
// with draws from a generator would reintroduce a frame-rate dependency even though the call site
// looks unchanged.
static float foliage_roll(uint32_t seed, int x, int y, int z)
{
  uint32_t h = seed * 2654435761u;
  h ^= (uint32_t)(x * 73856093);
  h ^= (uint32_t)(y * 19349663);
  h ^= (uint32_t)(z * 83492791);
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return (float)(h & 0xFFFFFFu) / (float)0x1000000u;
}

float projectile_foliage_stop_chance(VoxelType type)
{
  if (type == VOXEL_GRASS_TALL)
    return PROJECTILE_GRASS_STOP_CHANCE;
  if (world_voxel_type_is_foliage(type))
    return PROJECTILE_LEAVES_STOP_CHANCE;
  return 0.0f;
}

void projectile_system_reset(ProjectileSystem *sys, uint32_t seed)
{
  if (!sys)
    return;
  memset(sys, 0, sizeof(*sys));
  // Never zero: the seed is multiplied in the hash, and a zero seed would make every projectile's
  // rolls identical regardless of coordinate.
  sys->next_seed = seed ? seed : 0x9E3779B9u;
}

bool projectile_spawn(ProjectileSystem *sys, const ProjectileSpawn *spawn)
{
  if (!sys || !spawn)
    return false;

  const float len = sqrtf(spawn->dir_x * spawn->dir_x + spawn->dir_y * spawn->dir_y +
                          spawn->dir_z * spawn->dir_z);
  if (!(len > 1e-6f) || !(spawn->speed > 0.0f) || !(spawn->life > 0.0f))
    return false;

  Projectile *p = NULL;
  for (int i = 0; i < PROJECTILE_MAX; i++)
    if (!sys->items[i].active)
    {
      p = &sys->items[i];
      break;
    }

  if (!p)
  {
    sys->dropped_total++;
    return false;
  }

  const float inv = 1.0f / len;
  memset(p, 0, sizeof(*p));
  p->active = true;
  p->x = spawn->x;
  p->y = spawn->y;
  p->z = spawn->z;
  p->vx = spawn->dir_x * inv * spawn->speed;
  p->vy = spawn->dir_y * inv * spawn->speed;
  p->vz = spawn->dir_z * inv * spawn->speed;
  p->life = spawn->life;
  p->radius = spawn->radius > 0.0f ? spawn->radius : 0.4f;
  p->gravity_scale = spawn->gravity_scale;
  p->gravity_delay = spawn->gravity_delay > 0.0f ? spawn->gravity_delay : 0.0f;
  p->damage = spawn->damage;
  p->owner_id = spawn->owner_id;
  p->kind = spawn->kind;
  p->potency = spawn->potency > 0 ? spawn->potency : 100u;
  p->seed = sys->next_seed;
  p->last_vx = (int)floorf(p->x);
  p->last_vy = (int)floorf(p->y);
  p->last_vz = (int)floorf(p->z);
  p->bind_universe = spawn->bind_universe;
  p->universe_x = spawn->universe_x;
  p->universe_y = spawn->universe_y;
  p->universe_z = spawn->universe_z;

  sys->next_seed = sys->next_seed * 1664525u + 1013904223u;
  sys->spawned_total++;
  return true;
}

int projectile_active_count(const ProjectileSystem *sys)
{
  if (!sys)
    return 0;
  int n = 0;
  for (int i = 0; i < PROJECTILE_MAX; i++)
    if (sys->items[i].active)
      n++;
  return n;
}

static void record_impact(ProjectileImpact *impacts, int max_impacts, int *count,
                          const ProjectileImpact *impact)
{
  if (!impacts || !count || *count >= max_impacts)
    return;
  impacts[*count] = *impact;
  (*count)++;
}

// The first actor whose body capsule the projectile is inside, or NULL. A linear scan, matching the
// way the melee swing finds its target: there is no spatial index over actors, and a handful of
// projectiles against a handful of actors does not warrant building one yet.
//
// Pose is feet; the capsule covers roughly ankles to head so a flat fireball at spirit height still
// connects. `ignore_id` skips the caster (dominated body) so a muzzle spawn cannot self-hit.
static Actor *actor_hit_at(World *world, float x, float y, float z, float radius,
                           uint32_t ignore_id)
{
  if (!world || !world->runtime_actors)
    return NULL;

  const float hit_r = radius + PROJECTILE_ACTOR_BODY_RADIUS;
  const float hit_r2 = hit_r * hit_r;
  for (int i = 0; i < world->runtime_actor_count; i++)
  {
    Actor *a = &world->runtime_actors[i];
    if (!a->is_active || a->health == 0)
      continue;
    if (ignore_id != 0 && a->id == ignore_id)
      continue;

    const float dx = (float)a->x - x;
    const float dy = (float)a->y - y;
    if (dx * dx + dy * dy > hit_r2)
      continue;

    const float az = (float)a->z;
    if (z < az - PROJECTILE_ACTOR_HIT_BELOW || z > az + PROJECTILE_ACTOR_HIT_ABOVE)
      continue;

    return a;
  }
  return NULL;
}

// Advance one projectile. Returns false when it should be retired.
// `world_io` may be retargeted when the shot crosses into a loaded neighbour.
static bool step_one(Projectile *p, World **world_io, struct Universe *universe, float dt,
                     ProjectileImpact *impacts, int max_impacts, int *count)
{
  World *world = world_io ? *world_io : NULL;
  if (!p || !world)
    return false;

  p->life -= dt;
  if (p->life <= 0.0f)
  {
    ProjectileImpact im = {.kind = PROJECTILE_IMPACT_EXPIRED,
                           .x = p->x, .y = p->y, .z = p->z,
                           .damage = p->damage,
                           .owner_id = p->owner_id,
                           .projectile = p->kind,
                           .potency = p->potency,
                           .speed = sqrtf(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz),
                           .world = world};
    record_impact(impacts, max_impacts, count, &im);
    return false;
  }

  // Flat for gravity_delay, then ease into the world's gravity so spells run far before they sink.
  float grav_dt = dt;
  if (p->gravity_delay > 0.0f)
  {
    if (p->gravity_delay >= dt)
    {
      p->gravity_delay -= dt;
      grav_dt = 0.0f;
    }
    else
    {
      grav_dt = dt - p->gravity_delay;
      p->gravity_delay = 0.0f;
    }
  }
  if (grav_dt > 0.0f && p->gravity_scale != 0.0f)
  {
    // Same voxels/s² the player and world_step_actors use, so a lobbed shot falls with the world.
    float gravity = world_get_gravity(world);
    if (gravity <= 0.0f)
      gravity = GRAVITY_DEFAULT;
    p->vz -= gravity * p->gravity_scale * grav_dt;
  }

  const float dx = p->vx * dt;
  const float dy = p->vy * dt;
  const float dz = p->vz * dt;
  const float dist = sqrtf(dx * dx + dy * dy + dz * dz);

  int substeps = (int)ceilf(dist / PROJECTILE_MAX_STEP);
  if (substeps < 1)
    substeps = 1;
  if (substeps > PROJECTILE_MAX_SUBSTEPS)
    substeps = PROJECTILE_MAX_SUBSTEPS;

  const float sx = dx / (float)substeps;
  const float sy = dy / (float)substeps;
  const float sz = dz / (float)substeps;

  for (int i = 0; i < substeps; i++)
  {
    p->x += sx;
    p->y += sy;
    p->z += sz;
    world = *world_io;

    // An actor is checked before the voxel, so a target standing in a doorway is hit rather than
    // the wall behind it.
    Actor *a = actor_hit_at(world, p->x, p->y, p->z, p->radius, p->owner_id);
    if (a)
    {
      ProjectileImpact im = {.kind = PROJECTILE_IMPACT_ACTOR,
                             .x = p->x, .y = p->y, .z = p->z,
                             .actor_id = a->id,
                             .damage = p->damage,
                             .owner_id = p->owner_id,
                             .projectile = p->kind,
                             .potency = p->potency,
                             .world = world};
      record_impact(impacts, max_impacts, count, &im);
      return false;
    }

    const int vx = (int)floorf(p->x);
    const int vy = (int)floorf(p->y);
    const int vz = (int)floorf(p->z);

    // Checked with signed comparisons rather than through world_is_position_valid, which takes
    // unsigned coordinates: a projectile that flies out through x = -1 would arrive there as a very
    // large positive number and be judged in bounds.
    if (vx < 0 || vy < 0 || vz < 0 || vx >= (int)world->width || vy >= (int)world->height ||
        vz >= (int)world->depth)
    {
      // Try to rebase into a loaded neighbour. Without a universe anchor (tests, single arenas)
      // out-of-bounds still means the shot is gone.
      if (!universe || !p->bind_universe)
      {
        ProjectileImpact im = {.kind = PROJECTILE_IMPACT_EXPIRED,
                               .x = p->x, .y = p->y, .z = p->z,
                               .projectile = p->kind,
                               .world = world};
        record_impact(impacts, max_impacts, count, &im);
        return false;
      }

      int cdx = 0, cdy = 0, cdz = 0;
      if (p->x < 0.0f)
      {
        cdx = -1;
        p->x += (float)world->width;
      }
      else if (p->x >= (float)world->width)
      {
        cdx = 1;
        p->x -= (float)world->width;
      }
      if (p->y < 0.0f)
      {
        cdy = -1;
        p->y += (float)world->height;
      }
      else if (p->y >= (float)world->height)
      {
        cdy = 1;
        p->y -= (float)world->height;
      }
      if (p->z < 0.0f)
      {
        cdz = -1;
        p->z += (float)world->depth;
      }
      else if (p->z >= (float)world->depth)
      {
        cdz = 1;
        p->z -= (float)world->depth;
      }

      const int64_t nux = (int64_t)p->universe_x + cdx;
      const int64_t nuy = (int64_t)p->universe_y + cdy;
      const int64_t nuz = (int64_t)p->universe_z + cdz;
      if (nuz < 0)
      {
        ProjectileImpact im = {.kind = PROJECTILE_IMPACT_EXPIRED,
                               .x = p->x, .y = p->y, .z = p->z,
                               .projectile = p->kind,
                               .world = world};
        record_impact(impacts, max_impacts, count, &im);
        return false;
      }

      World *next = universe_get(universe, (uint64_t)nux, (uint64_t)nuy, (uint64_t)nuz);
      if (!next)
      {
        ProjectileImpact im = {.kind = PROJECTILE_IMPACT_EXPIRED,
                               .x = p->x, .y = p->y, .z = p->z,
                               .projectile = p->kind,
                               .world = world};
        record_impact(impacts, max_impacts, count, &im);
        return false;
      }

      p->universe_x = (uint64_t)nux;
      p->universe_y = (uint64_t)nuy;
      p->universe_z = (uint64_t)nuz;
      *world_io = next;
      world = next;
      p->last_vx = (int)floorf(p->x);
      p->last_vy = (int)floorf(p->y);
      p->last_vz = (int)floorf(p->z);
      continue;
    }

    const bool entered_new_voxel = (vx != p->last_vx || vy != p->last_vy || vz != p->last_vz);
    p->last_vx = vx;
    p->last_vy = vy;
    p->last_vz = vz;

    const Voxel *v = world_voxel_cptr_fast(world, vx, vy, vz);
    if (!v || v->type == VOXEL_AIR)
      continue;

    const float foliage_chance = projectile_foliage_stop_chance(v->type);
    if (foliage_chance > 0.0f)
    {
      // Only a voxel just entered gets a roll. This is an optimisation rather than a correctness
      // measure — the hash would return the same answer for the same leaf anyway — and it saves
      // re-rolling on every substep a slow projectile spends inside one canopy voxel.
      if (!entered_new_voxel)
        continue;
      if (foliage_roll(p->seed, vx, vy, vz) >= foliage_chance)
        continue; // through the gaps

      ProjectileImpact im = {.kind = PROJECTILE_IMPACT_VOXEL,
                             .x = p->x, .y = p->y, .z = p->z,
                             .vx = vx, .vy = vy, .vz = vz,
                             .type = v->type,
                             .damage = p->damage,
                             .owner_id = p->owner_id,
                             .projectile = p->kind,
                             .potency = p->potency,
                             .speed = sqrtf(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz),
                             .world = world};
      record_impact(impacts, max_impacts, count, &im);
      return false;
    }

    if (world_voxel_type_blocks_movement(v->type))
    {
      // Solids stop the shot outright. Only foliage above may let a projectile continue; leftover
      // momentum through stone was punching fireballs through multi-block walls.
      if (!entered_new_voxel)
        continue;

      const float speed = sqrtf(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz);
      ProjectileImpact im = {.kind = PROJECTILE_IMPACT_VOXEL,
                             .x = p->x, .y = p->y, .z = p->z,
                             .vx = vx, .vy = vy, .vz = vz,
                             .type = v->type,
                             .damage = p->damage,
                             .owner_id = p->owner_id,
                             .projectile = p->kind,
                             .potency = p->potency,
                             .speed = speed,
                             .world = world};
      record_impact(impacts, max_impacts, count, &im);
      return false;
    }

    // Water and the other non-blocking fluids: a fireball flies on through. Quenching it here would
    // need a rule about steam and damage that does not exist yet.
  }

  return true;
}

void projectile_system_step(ProjectileSystem *sys, World *world, float dt,
                            ProjectileImpact *impacts, int max_impacts, int *out_impacts)
{
  projectile_system_step_universe(sys, NULL, world, dt, impacts, max_impacts, out_impacts);
}

void projectile_system_step_universe(ProjectileSystem *sys, struct Universe *universe, World *world,
                                     float dt, ProjectileImpact *impacts, int max_impacts,
                                     int *out_impacts)
{
  int count = 0;
  if (out_impacts)
    *out_impacts = 0;
  if (!sys || !world || !(dt > 0.0f))
    return;

  for (int i = 0; i < PROJECTILE_MAX; i++)
  {
    Projectile *p = &sys->items[i];
    if (!p->active)
      continue;
    World *shot_world = world;
    if (p->bind_universe && universe)
    {
      World *anchored = universe_get(universe, p->universe_x, p->universe_y, p->universe_z);
      if (anchored)
        shot_world = anchored;
    }
    if (!step_one(p, &shot_world, universe, dt, impacts, max_impacts, &count))
      p->active = false;
  }

  if (out_impacts)
    *out_impacts = count;
}
