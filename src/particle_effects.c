#include "particle_effects.h"

#include "fire_sim.h"
#include "projectile.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PARTICLE_MAX_COUNT 512
#define PARTICLE_DUST_MAX_SITES 2048
#define PARTICLE_DUST_SITE_STRIDE 12
#define PARTICLE_DUST_SPAWN_RATE 2.5f
#define PARTICLE_DUST_CLEARANCE 2
#define PARTICLE_DUST_SCAN_RADIUS 32 // voxels around camera; full-world scans were frame killers

#define PARTICLE_RAIN_SPAWN_RATE 50.0f
#define PARTICLE_RAIN_SPAWN_RADIUS 14.0f
#define PARTICLE_RAIN_SPAWN_HEIGHT_MIN 6.0f
#define PARTICLE_RAIN_SPAWN_HEIGHT_MAX 18.0f
#define PARTICLE_RAIN_FALL_SPEED_MIN 18.0f
#define PARTICLE_RAIN_FALL_SPEED_MAX 28.0f

#define PARTICLE_HEAVY_RAIN_SPAWN_RATE 140.0f
#define PARTICLE_HEAVY_RAIN_FALL_SPEED_MIN 26.0f
#define PARTICLE_HEAVY_RAIN_FALL_SPEED_MAX 40.0f

#define PARTICLE_RAIN_SITE_MAX 768
#define PARTICLE_RAIN_SITE_STRIDE 2
#define PARTICLE_RAIN_SITE_REBUILD_MOVE 3

// Expected splashes per second on each exposed water voxel in the rain area.
#define PARTICLE_RAIN_SPLASH_RATE_LIGHT 0.15f
#define PARTICLE_RAIN_SPLASH_RATE_HEAVY 1.0f

#define PARTICLE_RAIN_SPLASH_UNITS_LIGHT_MIN 4
#define PARTICLE_RAIN_SPLASH_UNITS_LIGHT_MAX 10
#define PARTICLE_RAIN_SPLASH_UNITS_HEAVY_MIN 12
#define PARTICLE_RAIN_SPLASH_UNITS_HEAVY_MAX 28

#define PARTICLE_FLAME_SITE_MAX 128
#define PARTICLE_FLAME_SITE_RADIUS 16
#define PARTICLE_FLAME_SITE_STRIDE 2
#define PARTICLE_FLAME_SITE_REBUILD_MOVE 3
#define PARTICLE_FLAME_SPAWN_RATE_CANDLE 8.0f
#define PARTICLE_FLAME_SPAWN_RATE_CAMPFIRE 28.0f
#define PARTICLE_FLAME_SPAWN_RATE_FIREBALL 40.0f

#define PARTICLE_METEOR_SPAWN_RATE 12.0f
#define PARTICLE_METEOR_SPAWN_RADIUS 22.0f
#define PARTICLE_METEOR_SPAWN_HEIGHT_MIN 14.0f
#define PARTICLE_METEOR_SPAWN_HEIGHT_MAX 36.0f
#define PARTICLE_METEOR_FALL_SPEED_MIN 22.0f
#define PARTICLE_METEOR_FALL_SPEED_MAX 42.0f
#define PARTICLE_METEOR_IMPACTS_PER_FRAME 3

static bool g_meteor_storm_universe = false;
static bool g_thunderstorm_universe = false;

static const ParticleEffectDef g_particle_effect_defs[PARTICLE_EFFECT_COUNT] = {
    {
        PARTICLE_EFFECT_DRIFTING_DUST,
        "drifting_dust",
        "Subtle dust motes drifting in deep open air",
        PARTICLE_DUST_CLEARANCE,
        true,
    },
    {
        PARTICLE_EFFECT_RAIN,
        "rain",
        "Falling raindrops around the camera",
        0,
        false,
    },
    {
        PARTICLE_EFFECT_HEAVY_RAIN,
        "heavy_rain",
        "Dense downpour with frequent ripples on water",
        0,
        false,
    },
    {
        PARTICLE_EFFECT_FLAME,
        "flame",
        "Rising sparks from candles, campfires, and fireballs",
        0,
        true,
    },
    {
        PARTICLE_EFFECT_METEOR_STORM,
        "meteor_storm",
        "Falling magma meteors (lava storm) that splash magma on impact",
        0,
        false,
    },
};

static uint32_t particle_rng_u32(uint32_t *state)
{
  *state = (*state * 1103515245u) + 12345u;
  return *state;
}

static float particle_rng_float(uint32_t *state)
{
  return (float)(particle_rng_u32(state) & 0xFFFFu) / 65535.0f;
}

static float particle_rng_range(uint32_t *state, float lo, float hi)
{
  return lo + (hi - lo) * particle_rng_float(state);
}

void particle_effects_registry_init(void)
{
  // Registry is static for now; call once at startup for future dynamic registration.
}

const ParticleEffectDef *particle_effect_def(ParticleEffectKind kind)
{
  if (kind < 0 || kind >= PARTICLE_EFFECT_COUNT)
    return NULL;
  return &g_particle_effect_defs[kind];
}

const ParticleEffectDef *particle_effect_def_by_name(const char *name)
{
  if (!name)
    return NULL;
  for (int i = 0; i < PARTICLE_EFFECT_COUNT; i++)
  {
    if (strcmp(g_particle_effect_defs[i].name, name) == 0)
      return &g_particle_effect_defs[i];
  }
  return NULL;
}

ParticleEffectKind particle_effect_kind_from_name(const char *name)
{
  const ParticleEffectDef *def = particle_effect_def_by_name(name);
  if (!def)
    return PARTICLE_EFFECT_COUNT;
  return def->kind;
}

bool particle_effects_voxel_emits_flame(VoxelType type)
{
  // Fixtures can burn; other materials emit only when their BURNING_* condition is set
  // (see particle_effects_voxel_emits_flame_voxel).
  return type == VOXEL_CANDLE || type == VOXEL_CAMPFIRE || voxel_type_is_flammable(type);
}

static bool particle_effects_site_emits(const Voxel *voxel)
{
  if (!voxel)
    return false;
  // Lit flame only while burning — water extinguishes by clearing BURNING_*.
  return fire_voxel_is_burning(voxel);
}

WorldParticleEffects *particle_effects_create(void)
{
  WorldParticleEffects *effects = (WorldParticleEffects *)calloc(1, sizeof(WorldParticleEffects));
  if (!effects)
    return NULL;

  effects->particle_capacity = PARTICLE_MAX_COUNT;
  effects->particles = (Particle *)calloc((size_t)effects->particle_capacity, sizeof(Particle));
  if (!effects->particles)
  {
    free(effects);
    return NULL;
  }

  effects->dust_site_capacity = PARTICLE_DUST_MAX_SITES;
  effects->dust_sites =
      (ParticleEmitterSite *)calloc((size_t)effects->dust_site_capacity, sizeof(ParticleEmitterSite));
  if (!effects->dust_sites)
  {
    free(effects->particles);
    free(effects);
    return NULL;
  }

  effects->rng_state = 0xA5C3E7B1u;
  effects->dust_enabled = true;
  effects->rain_enabled = false;
  effects->heavy_rain_enabled = false;
  effects->flame_enabled = true;
  effects->meteor_storm_enabled = false;
  effects->meteor_impacts_this_frame = 0;
  effects->thunderstorm_enabled = false;

  effects->rain_site_capacity = PARTICLE_RAIN_SITE_MAX;
  effects->rain_sites =
      (ParticleEmitterSite *)calloc((size_t)effects->rain_site_capacity, sizeof(ParticleEmitterSite));
  if (!effects->rain_sites)
  {
    free(effects->dust_sites);
    free(effects->particles);
    free(effects);
    return NULL;
  }

  effects->flame_site_capacity = PARTICLE_FLAME_SITE_MAX;
  effects->flame_sites =
      (ParticleEmitterSite *)calloc((size_t)effects->flame_site_capacity, sizeof(ParticleEmitterSite));
  if (!effects->flame_sites)
  {
    free(effects->rain_sites);
    free(effects->dust_sites);
    free(effects->particles);
    free(effects);
    return NULL;
  }

  return effects;
}

void particle_effects_destroy(WorldParticleEffects *effects)
{
  if (!effects)
    return;
  free(effects->particles);
  free(effects->dust_sites);
  free(effects->rain_sites);
  free(effects->flame_sites);
  free(effects);
}

bool particle_effects_air_clearance_at_least(World *world, int x, int y, int z,
                                             int clearance)
{
  if (!world || clearance <= 0)
    return false;
  if (!world_pos_in_bounds_fast(world, x, y, z))
    return false;
  if (world_is_solid_fast(world, x, y, z))
    return false;

  const int inner = clearance - 1;
  for (int dz = -inner; dz <= inner; dz++)
  {
    for (int dy = -inner; dy <= inner; dy++)
    {
      for (int dx = -inner; dx <= inner; dx++)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        const int md = (abs(dx) > abs(dy) ? abs(dx) : abs(dy));
        const int cheb = (md > abs(dz) ? md : abs(dz));
        if (cheb >= clearance)
          continue;
        if (world_is_solid_fast(world, x + dx, y + dy, z + dz))
          return false;
      }
    }
  }
  return true;
}

static void particle_effects_clear_sites(WorldParticleEffects *effects)
{
  if (!effects)
    return;
  effects->dust_site_count = 0;
}

static void particle_effects_add_dust_site(WorldParticleEffects *effects, int x, int y, int z)
{
  if (!effects || effects->dust_site_count >= effects->dust_site_capacity)
    return;
  ParticleEmitterSite *site = &effects->dust_sites[effects->dust_site_count++];
  site->x = x;
  site->y = y;
  site->z = z;
  site->type = VOXEL_AIR;
}

void particle_effects_rebuild_dust_sites(World *world)
{
  if (!world || !world->particle_effects)
    return;
  particle_effects_rebuild_dust_sites_near(
      world, (float)world->width * 0.5f, (float)world->height * 0.5f, (float)world->depth * 0.5f);
}

void particle_effects_rebuild_dust_sites_near(World *world, float camera_x, float camera_y,
                                               float camera_z)
{
  if (!world || !world->particle_effects)
    return;

  WorldParticleEffects *effects = world->particle_effects;
  const ParticleEffectDef *def = particle_effect_def(PARTICLE_EFFECT_DRIFTING_DUST);
  const int clearance = def ? def->clearance_distance : PARTICLE_DUST_CLEARANCE;

  particle_effects_clear_sites(effects);

  const int stride = PARTICLE_DUST_SITE_STRIDE;
  const int cx = (int)floorf(camera_x);
  const int cy = (int)floorf(camera_y);
  const int cz = (int)floorf(camera_z);
  const int radius = PARTICLE_DUST_SCAN_RADIUS;

  const int x0 = cx - radius;
  const int y0 = cy - radius;
  const int z0 = cz - radius;
  const int x1 = cx + radius;
  const int y1 = cy + radius;
  const int z1 = cz + radius;

  for (int z = z0; z <= z1; z += stride)
  {
    if (z < 0 || z >= (int)world->depth)
      continue;
    for (int y = y0; y <= y1; y += stride)
    {
      if (y < 0 || y >= (int)world->height)
        continue;
      for (int x = x0; x <= x1; x += stride)
      {
        if (x < 0 || x >= (int)world->width)
          continue;
        if (!particle_effects_air_clearance_at_least(world, x, y, z, clearance))
          continue;
        particle_effects_add_dust_site(effects, x, y, z);
        if (effects->dust_site_count >= effects->dust_site_capacity)
          goto done;
      }
    }
  }

done:
  effects->sites_voxel_revision = world->voxel_revision;
}

void particle_effects_init_for_world(World *world)
{
  if (!world)
    return;

  if (world->particle_effects)
    particle_effects_destroy(world->particle_effects);

  world->particle_effects = particle_effects_create();
  if (!world->particle_effects)
    return;

  const ParticleEffectDef *dust_def = particle_effect_def(PARTICLE_EFFECT_DRIFTING_DUST);
  world->particle_effects->dust_enabled = dust_def ? dust_def->enabled_by_default : true;
  const ParticleEffectDef *rain_def = particle_effect_def(PARTICLE_EFFECT_RAIN);
  world->particle_effects->rain_enabled = rain_def ? rain_def->enabled_by_default : false;
  const ParticleEffectDef *heavy_rain_def = particle_effect_def(PARTICLE_EFFECT_HEAVY_RAIN);
  world->particle_effects->heavy_rain_enabled =
      heavy_rain_def ? heavy_rain_def->enabled_by_default : false;
  const ParticleEffectDef *flame_def = particle_effect_def(PARTICLE_EFFECT_FLAME);
  world->particle_effects->flame_enabled = flame_def ? flame_def->enabled_by_default : true;
  const ParticleEffectDef *meteor_def = particle_effect_def(PARTICLE_EFFECT_METEOR_STORM);
  world->particle_effects->meteor_storm_enabled =
      g_meteor_storm_universe || (meteor_def && meteor_def->enabled_by_default);
  // Thunderstorm flag lives in weather_storm.c; inherit the sticky universe toggle here so newly
  // streamed cells start wet + ready for strikes without waiting for the next ambient tick.
  world->particle_effects->thunderstorm_enabled =
      g_thunderstorm_universe;
  world->particle_effects->rng_state ^= (uint32_t)world->width |
                                        ((uint32_t)world->height << 8) |
                                        ((uint32_t)world->depth << 16);
  particle_effects_rebuild_dust_sites(world);
}

static bool particle_effects_spawn_dust(World *world, WorldParticleEffects *effects,
                                        float camera_x, float camera_y, float camera_z,
                                        int render_distance)
{
  if (!world || !effects || effects->dust_site_count <= 0)
    return false;
  if (effects->particle_count >= effects->particle_capacity)
    return false;

  const int site_idx =
      (int)(particle_rng_u32(&effects->rng_state) % (uint32_t)effects->dust_site_count);
  const ParticleEmitterSite *site = &effects->dust_sites[site_idx];

  const float dx = (float)site->x - camera_x;
  const float dy = (float)site->y - camera_y;
  const float dz = (float)site->z - camera_z;
  const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
  if (dist > (float)render_distance)
    return false;

  Particle *p = &effects->particles[effects->particle_count++];
  p->x = (float)site->x + particle_rng_range(&effects->rng_state, -0.35f, 0.35f);
  p->y = (float)site->y + particle_rng_range(&effects->rng_state, -0.35f, 0.35f);
  p->z = (float)site->z + particle_rng_range(&effects->rng_state, -0.35f, 0.35f);
  p->vx = particle_rng_range(&effects->rng_state, -0.08f, 0.08f);
  p->vy = particle_rng_range(&effects->rng_state, -0.08f, 0.08f);
  p->vz = particle_rng_range(&effects->rng_state, -0.04f, 0.04f);
  p->max_life = particle_rng_range(&effects->rng_state, 2.5f, 5.5f);
  p->life = p->max_life;
  p->r = (uint8_t)(170 + (int)(particle_rng_float(&effects->rng_state) * 35.0f));
  p->g = (uint8_t)(160 + (int)(particle_rng_float(&effects->rng_state) * 30.0f));
  p->b = (uint8_t)(140 + (int)(particle_rng_float(&effects->rng_state) * 25.0f));
  p->a = (uint8_t)(18 + (int)(particle_rng_float(&effects->rng_state) * 28.0f));
  p->size_px = particle_rng_range(&effects->rng_state, 1.0f, 2.5f);
  p->kind = PARTICLE_KIND_AMBIENT;
  return true;
}

// Top exposed face of a water column: water with air above it.
static int particle_water_surface_z(const World *world, int x, int y)
{
  if (!world || x < 0 || y < 0 || x >= (int)world->width || y >= (int)world->height)
    return -1;
  for (int z = (int)world->depth - 2; z >= 0; z--)
  {
    const Voxel *voxel = world_voxel_cptr_fast(world, x, y, z);
    if (!voxel || voxel->type != VOXEL_WATER)
      continue;
    const Voxel *above = world_voxel_cptr_fast(world, x, y, z + 1);
    if (!above || above->type == VOXEL_AIR)
      return z;
  }
  return -1;
}

static void particle_effects_clear_rain_sites(WorldParticleEffects *effects)
{
  if (!effects)
    return;
  effects->rain_site_count = 0;
}

static void particle_effects_add_rain_site(WorldParticleEffects *effects, int x, int y, int z)
{
  if (!effects || effects->rain_site_count >= effects->rain_site_capacity)
    return;
  ParticleEmitterSite *site = &effects->rain_sites[effects->rain_site_count++];
  site->x = x;
  site->y = y;
  site->z = z;
  site->type = VOXEL_WATER;
}

static void particle_effects_rebuild_rain_sites(World *world, WorldParticleEffects *effects,
                                                float camera_x, float camera_y)
{
  if (!world || !effects)
    return;

  particle_effects_clear_rain_sites(effects);

  const int cx = (int)floorf(camera_x);
  const int cy = (int)floorf(camera_y);
  const int radius = (int)PARTICLE_RAIN_SPAWN_RADIUS;
  const int stride = PARTICLE_RAIN_SITE_STRIDE;

  for (int dy = -radius; dy <= radius; dy += stride)
  {
    for (int dx = -radius; dx <= radius; dx += stride)
    {
      if ((float)(dx * dx + dy * dy) > PARTICLE_RAIN_SPAWN_RADIUS * PARTICLE_RAIN_SPAWN_RADIUS)
        continue;
      const int x = cx + dx;
      const int y = cy + dy;
      const int z = particle_water_surface_z(world, x, y);
      if (z < 0)
        continue;
      particle_effects_add_rain_site(effects, x, y, z);
      if (effects->rain_site_count >= effects->rain_site_capacity)
        goto done;
    }
  }

done:
  effects->rain_sites_camera_x = cx;
  effects->rain_sites_camera_y = cy;
  effects->rain_sites_voxel_revision = world->voxel_revision;
  effects->rain_sites_ready = true;
}

static void particle_effects_queue_surface_splash(WorldParticleEffects *effects, int x, int y,
                                                  int z, uint8_t units)
{
  if (!effects || units == 0)
    return;

  int slot;
  if (effects->surface_splash_count < PARTICLE_SURFACE_SPLASH_CAP)
  {
    slot = (effects->surface_splash_head + effects->surface_splash_count) %
           PARTICLE_SURFACE_SPLASH_CAP;
    effects->surface_splash_count++;
  }
  else
  {
    slot = effects->surface_splash_head;
    effects->surface_splash_head = (effects->surface_splash_head + 1) % PARTICLE_SURFACE_SPLASH_CAP;
  }

  effects->surface_splashes[slot] =
      (FluidSplash){.x = (uint16_t)x,
                    .y = (uint16_t)y,
                    .z = (uint16_t)z,
                    .from_dx = 0,
                    .from_dy = 0,
                    .from_dz = -1,
                    .units = units};
}

static void particle_effects_spawn_rain_splashes(World *world, WorldParticleEffects *effects,
                                                 float dt_seconds, bool heavy)
{
  if (!world || !effects || dt_seconds <= 0.0f || effects->rain_site_count <= 0)
    return;

  const float rate =
      heavy ? PARTICLE_RAIN_SPLASH_RATE_HEAVY : PARTICLE_RAIN_SPLASH_RATE_LIGHT;
  const int units_min =
      heavy ? PARTICLE_RAIN_SPLASH_UNITS_HEAVY_MIN : PARTICLE_RAIN_SPLASH_UNITS_LIGHT_MIN;
  const int units_max =
      heavy ? PARTICLE_RAIN_SPLASH_UNITS_HEAVY_MAX : PARTICLE_RAIN_SPLASH_UNITS_LIGHT_MAX;
  const float p = rate * dt_seconds;

  for (int i = 0; i < effects->rain_site_count; i++)
  {
    if (particle_rng_float(&effects->rng_state) >= p)
      continue;

    const ParticleEmitterSite *site = &effects->rain_sites[i];
    if (particle_water_surface_z(world, site->x, site->y) != site->z)
      continue;

    const float span = (float)(units_max - units_min + 1);
    const uint8_t units =
        (uint8_t)(units_min + (int)(particle_rng_float(&effects->rng_state) * span));
    particle_effects_queue_surface_splash(effects, site->x, site->y, site->z, units);
  }
}

static bool particle_effects_spawn_rain(WorldParticleEffects *effects,
                                        float camera_x, float camera_y, float camera_z,
                                        int render_distance)
{
  if (!effects || effects->particle_count >= effects->particle_capacity)
    return false;

  const float ox = particle_rng_range(&effects->rng_state, -PARTICLE_RAIN_SPAWN_RADIUS,
                                      PARTICLE_RAIN_SPAWN_RADIUS);
  const float oy = particle_rng_range(&effects->rng_state, -PARTICLE_RAIN_SPAWN_RADIUS,
                                      PARTICLE_RAIN_SPAWN_RADIUS);
  const float spawn_z =
      camera_z + particle_rng_range(&effects->rng_state, PARTICLE_RAIN_SPAWN_HEIGHT_MIN,
                                    PARTICLE_RAIN_SPAWN_HEIGHT_MAX);

  const float dist = sqrtf(ox * ox + oy * oy + (spawn_z - camera_z) * (spawn_z - camera_z));
  if (dist > (float)render_distance)
    return false;

  Particle *p = &effects->particles[effects->particle_count++];
  p->x = camera_x + ox;
  p->y = camera_y + oy;
  p->z = spawn_z;
  const float fall =
      particle_rng_range(&effects->rng_state, PARTICLE_RAIN_FALL_SPEED_MIN, PARTICLE_RAIN_FALL_SPEED_MAX);
  p->vx = particle_rng_range(&effects->rng_state, -0.6f, 0.6f);
  p->vy = particle_rng_range(&effects->rng_state, -0.6f, 0.6f);
  p->vz = -fall;
  p->max_life = particle_rng_range(&effects->rng_state, 0.7f, 1.4f);
  p->life = p->max_life;
  p->r = (uint8_t)(140 + (int)(particle_rng_float(&effects->rng_state) * 40.0f));
  p->g = (uint8_t)(165 + (int)(particle_rng_float(&effects->rng_state) * 50.0f));
  p->b = (uint8_t)(210 + (int)(particle_rng_float(&effects->rng_state) * 45.0f));
  p->a = (uint8_t)(90 + (int)(particle_rng_float(&effects->rng_state) * 110.0f));
  p->size_px = particle_rng_range(&effects->rng_state, 1.0f, 2.0f);
  p->kind = PARTICLE_KIND_AMBIENT;
  return true;
}

static bool particle_effects_spawn_heavy_rain(WorldParticleEffects *effects,
                                              float camera_x, float camera_y, float camera_z,
                                              int render_distance)
{
  if (!effects || effects->particle_count >= effects->particle_capacity)
    return false;

  const float ox = particle_rng_range(&effects->rng_state, -PARTICLE_RAIN_SPAWN_RADIUS,
                                      PARTICLE_RAIN_SPAWN_RADIUS);
  const float oy = particle_rng_range(&effects->rng_state, -PARTICLE_RAIN_SPAWN_RADIUS,
                                      PARTICLE_RAIN_SPAWN_RADIUS);
  const float spawn_z =
      camera_z + particle_rng_range(&effects->rng_state, PARTICLE_RAIN_SPAWN_HEIGHT_MIN,
                                    PARTICLE_RAIN_SPAWN_HEIGHT_MAX);

  const float dist = sqrtf(ox * ox + oy * oy + (spawn_z - camera_z) * (spawn_z - camera_z));
  if (dist > (float)render_distance)
    return false;

  Particle *p = &effects->particles[effects->particle_count++];
  p->x = camera_x + ox;
  p->y = camera_y + oy;
  p->z = spawn_z;
  const float fall =
      particle_rng_range(&effects->rng_state, PARTICLE_HEAVY_RAIN_FALL_SPEED_MIN,
                         PARTICLE_HEAVY_RAIN_FALL_SPEED_MAX);
  p->vx = particle_rng_range(&effects->rng_state, -1.2f, 1.2f);
  p->vy = particle_rng_range(&effects->rng_state, -1.2f, 1.2f);
  p->vz = -fall;
  p->max_life = particle_rng_range(&effects->rng_state, 0.5f, 1.0f);
  p->life = p->max_life;
  p->r = (uint8_t)(110 + (int)(particle_rng_float(&effects->rng_state) * 35.0f));
  p->g = (uint8_t)(140 + (int)(particle_rng_float(&effects->rng_state) * 40.0f));
  p->b = (uint8_t)(190 + (int)(particle_rng_float(&effects->rng_state) * 50.0f));
  p->a = (uint8_t)(120 + (int)(particle_rng_float(&effects->rng_state) * 120.0f));
  p->size_px = particle_rng_range(&effects->rng_state, 1.5f, 3.0f);
  p->kind = PARTICLE_KIND_AMBIENT;
  return true;
}

static void particle_effects_spawn_rain_particles(WorldParticleEffects *effects,
                                                float camera_x, float camera_y, float camera_z,
                                                int render_distance, float dt_seconds)
{
  if (!effects)
    return;

  if (effects->rain_enabled)
  {
    effects->rain_spawn_accum += dt_seconds * PARTICLE_RAIN_SPAWN_RATE;
    while (effects->rain_spawn_accum >= 1.0f &&
           effects->particle_count < effects->particle_capacity)
    {
      if (!particle_effects_spawn_rain(effects, camera_x, camera_y, camera_z, render_distance))
        break;
      effects->rain_spawn_accum -= 1.0f;
    }
    if (effects->rain_spawn_accum > 4.0f)
      effects->rain_spawn_accum = 4.0f;
  }

  if (effects->heavy_rain_enabled)
  {
    effects->heavy_rain_spawn_accum += dt_seconds * PARTICLE_HEAVY_RAIN_SPAWN_RATE;
    while (effects->heavy_rain_spawn_accum >= 1.0f &&
           effects->particle_count < effects->particle_capacity)
    {
      if (!particle_effects_spawn_heavy_rain(effects, camera_x, camera_y, camera_z, render_distance))
        break;
      effects->heavy_rain_spawn_accum -= 1.0f;
    }
    if (effects->heavy_rain_spawn_accum > 4.0f)
      effects->heavy_rain_spawn_accum = 4.0f;
  }
}

static bool particle_effects_spawn_meteor(WorldParticleEffects *effects, float camera_x,
                                          float camera_y, float camera_z, int render_distance)
{
  if (!effects || effects->particle_count >= effects->particle_capacity)
    return false;

  const float ox = particle_rng_range(&effects->rng_state, -PARTICLE_METEOR_SPAWN_RADIUS,
                                      PARTICLE_METEOR_SPAWN_RADIUS);
  const float oy = particle_rng_range(&effects->rng_state, -PARTICLE_METEOR_SPAWN_RADIUS,
                                      PARTICLE_METEOR_SPAWN_RADIUS);
  const float spawn_z =
      camera_z + particle_rng_range(&effects->rng_state, PARTICLE_METEOR_SPAWN_HEIGHT_MIN,
                                    PARTICLE_METEOR_SPAWN_HEIGHT_MAX);
  const float dist = sqrtf(ox * ox + oy * oy + (spawn_z - camera_z) * (spawn_z - camera_z));
  if (dist > (float)render_distance)
    return false;

  Particle *p = &effects->particles[effects->particle_count++];
  p->x = camera_x + ox;
  p->y = camera_y + oy;
  p->z = spawn_z;
  const float fall = particle_rng_range(&effects->rng_state, PARTICLE_METEOR_FALL_SPEED_MIN,
                                        PARTICLE_METEOR_FALL_SPEED_MAX);
  // Slight lateral streak so trails read as slanted fireballs rather than vertical drops.
  p->vx = particle_rng_range(&effects->rng_state, -4.0f, 4.0f);
  p->vy = particle_rng_range(&effects->rng_state, -4.0f, 4.0f);
  p->vz = -fall;
  p->max_life = particle_rng_range(&effects->rng_state, 1.2f, 2.4f);
  p->life = p->max_life;
  p->r = (uint8_t)(220 + (int)(particle_rng_float(&effects->rng_state) * 35.0f));
  p->g = (uint8_t)(80 + (int)(particle_rng_float(&effects->rng_state) * 90.0f));
  p->b = (uint8_t)(20 + (int)(particle_rng_float(&effects->rng_state) * 40.0f));
  p->a = (uint8_t)(200 + (int)(particle_rng_float(&effects->rng_state) * 55.0f));
  p->size_px = particle_rng_range(&effects->rng_state, 3.0f, 6.5f);
  p->kind = PARTICLE_KIND_METEOR;
  return true;
}

static void particle_effects_spawn_meteor_particles(WorldParticleEffects *effects, float camera_x,
                                                    float camera_y, float camera_z,
                                                    int render_distance, float dt_seconds)
{
  if (!effects || !effects->meteor_storm_enabled)
    return;

  effects->meteor_storm_spawn_accum += dt_seconds * PARTICLE_METEOR_SPAWN_RATE;
  while (effects->meteor_storm_spawn_accum >= 1.0f &&
         effects->particle_count < effects->particle_capacity)
  {
    if (!particle_effects_spawn_meteor(effects, camera_x, camera_y, camera_z, render_distance))
      break;
    effects->meteor_storm_spawn_accum -= 1.0f;
  }
  if (effects->meteor_storm_spawn_accum > 4.0f)
    effects->meteor_storm_spawn_accum = 4.0f;
}

// Splash magma where a meteor hits terrain, and light nearby fuel. Caps impacts per frame so a
// dense burst cannot rewrite hundreds of voxels in one tick.
static bool particle_effects_meteor_impact(World *world, WorldParticleEffects *effects, Particle *p)
{
  if (!world || !effects || !p || effects->meteor_impacts_this_frame >= PARTICLE_METEOR_IMPACTS_PER_FRAME)
    return false;

  int x = (int)floorf(p->x);
  int y = (int)floorf(p->y);
  int z = (int)floorf(p->z);
  if (x < 0 || y < 0 || z < 0 || x >= (int)world->width || y >= (int)world->height)
    return false;

  // Walk down from the meteor until we find a solid, then place magma in the air cell above it
  // (or on the surface if the strike is inside a soft block).
  int solid_z = -1;
  const int z_lo = z - 8 < 0 ? 0 : z - 8;
  for (int zz = z; zz >= z_lo; zz--)
  {
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)zz);
    if (v && world_voxel_type_blocks_movement(v->type))
    {
      solid_z = zz;
      break;
    }
  }
  if (solid_z < 0)
    return false;

  int place_z = solid_z + 1;
  if (place_z >= (int)world->depth)
    place_z = solid_z;

  Voxel *dest = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)place_z);
  if (!dest)
    return false;

  // Do not overwrite bedrock or existing deep magma columns needlessly.
  if (dest->type == VOXEL_BEDROCK)
    return false;

  world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)place_z, VOXEL_MAGMA);
  fluid_sim_touch(world, x, y, place_z);

  static const int nbs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int i = 0; i < 6; i++)
    fire_ignite_at(world, x + nbs[i][0], y + nbs[i][1], place_z + nbs[i][2], "BURNING_MEDIUM");

  effects->meteor_impacts_this_frame++;
  return true;
}

static void particle_effects_clear_flame_sites(WorldParticleEffects *effects)
{
  if (!effects)
    return;
  effects->flame_site_count = 0;
}

static void particle_effects_add_flame_site(WorldParticleEffects *effects, int x, int y, int z,
                                            VoxelType type)
{
  if (!effects || effects->flame_site_count >= effects->flame_site_capacity)
    return;
  ParticleEmitterSite *site = &effects->flame_sites[effects->flame_site_count++];
  site->x = x;
  site->y = y;
  site->z = z;
  site->type = type;
}

static void particle_effects_rebuild_flame_sites(World *world, WorldParticleEffects *effects,
                                                 float camera_x, float camera_y)
{
  if (!world || !effects)
    return;

  particle_effects_clear_flame_sites(effects);

  const int cx = (int)floorf(camera_x);
  const int cy = (int)floorf(camera_y);
  const int radius = PARTICLE_FLAME_SITE_RADIUS;
  const int r2 = radius * radius;
  const int stride = PARTICLE_FLAME_SITE_STRIDE;

  for (int dy = -radius; dy <= radius; dy += stride)
  {
    for (int dx = -radius; dx <= radius; dx += stride)
    {
      if (dx * dx + dy * dy > r2)
        continue;
      const int x = cx + dx;
      const int y = cy + dy;
      if (x < 0 || y < 0 || x >= (int)world->width || y >= (int)world->height)
        continue;

      for (int z = 0; z < (int)world->depth; z++)
      {
        const Voxel *voxel = world_voxel_cptr_fast(world, x, y, z);
        if (!voxel || !particle_effects_site_emits(voxel))
          continue;
        particle_effects_add_flame_site(effects, x, y, z, voxel->type);
        if (effects->flame_site_count >= effects->flame_site_capacity)
          goto done;
      }
    }
  }

done:
  effects->flame_sites_camera_x = cx;
  effects->flame_sites_camera_y = cy;
  effects->flame_sites_voxel_revision = world->voxel_revision;
  effects->flame_sites_condition_revision = world->condition_revision;
  effects->flame_sites_ready = true;
}

static bool particle_effects_spawn_flame_at(WorldParticleEffects *effects, float x, float y,
                                            float z, VoxelType source, float spread)
{
  if (!effects || effects->particle_count >= effects->particle_capacity)
    return false;

  const bool candle = (source == VOXEL_CANDLE);
  const bool campfire = (source == VOXEL_CAMPFIRE);

  Particle *p = &effects->particles[effects->particle_count++];
  p->x = x + particle_rng_range(&effects->rng_state, -spread, spread);
  p->y = y + particle_rng_range(&effects->rng_state, -spread, spread);
  p->z = z + particle_rng_range(&effects->rng_state, 0.05f, candle ? 0.35f : 0.55f);
  p->vx = particle_rng_range(&effects->rng_state, -0.35f, 0.35f);
  p->vy = particle_rng_range(&effects->rng_state, -0.35f, 0.35f);
  p->vz = particle_rng_range(&effects->rng_state, candle ? 1.2f : 2.0f, candle ? 2.8f : 5.5f);
  p->max_life = particle_rng_range(&effects->rng_state, candle ? 0.25f : 0.35f,
                                   candle ? 0.55f : 0.9f);
  p->life = p->max_life;

  // Hot core → cooler tip: more yellow/white early, more red/orange overall.
  const float heat = particle_rng_float(&effects->rng_state);
  if (heat > 0.72f)
  {
    p->r = 255;
    p->g = (uint8_t)(220 + (int)(particle_rng_float(&effects->rng_state) * 35.0f));
    p->b = (uint8_t)(120 + (int)(particle_rng_float(&effects->rng_state) * 80.0f));
  }
  else if (heat > 0.35f)
  {
    p->r = 255;
    p->g = (uint8_t)(140 + (int)(particle_rng_float(&effects->rng_state) * 70.0f));
    p->b = (uint8_t)(20 + (int)(particle_rng_float(&effects->rng_state) * 40.0f));
  }
  else
  {
    p->r = (uint8_t)(200 + (int)(particle_rng_float(&effects->rng_state) * 55.0f));
    p->g = (uint8_t)(40 + (int)(particle_rng_float(&effects->rng_state) * 50.0f));
    p->b = (uint8_t)(8 + (int)(particle_rng_float(&effects->rng_state) * 20.0f));
  }

  p->a = (uint8_t)(campfire ? (160 + (int)(particle_rng_float(&effects->rng_state) * 90.0f))
                            : (120 + (int)(particle_rng_float(&effects->rng_state) * 100.0f)));
  p->size_px = particle_rng_range(&effects->rng_state, candle ? 1.0f : 1.5f,
                                  candle ? 2.2f : 3.8f);
  p->kind = PARTICLE_KIND_FLAME;
  return true;
}

static void particle_effects_spawn_flame_from_sites(World *world, WorldParticleEffects *effects,
                                                    float camera_x, float camera_y, float camera_z,
                                                    int render_distance, float dt_seconds)
{
  if (!world || !effects || !effects->flame_enabled || effects->flame_site_count <= 0)
    return;

  for (int i = 0; i < effects->flame_site_count; i++)
  {
    const ParticleEmitterSite *site = &effects->flame_sites[i];
    const Voxel *voxel = world_voxel_cptr_fast(world, site->x, site->y, site->z);
    if (!voxel || voxel->type != site->type || !fire_voxel_is_burning(voxel))
      continue;

    const float dx = (float)site->x + 0.5f - camera_x;
    const float dy = (float)site->y + 0.5f - camera_y;
    const float dz = (float)site->z + 0.5f - camera_z;
    if (sqrtf(dx * dx + dy * dy + dz * dz) > (float)render_distance)
      continue;

    const float rate = (site->type == VOXEL_CANDLE) ? PARTICLE_FLAME_SPAWN_RATE_CANDLE
                                                    : PARTICLE_FLAME_SPAWN_RATE_CAMPFIRE;
    const float spread = (site->type == VOXEL_CANDLE) ? 0.12f : 0.35f;
    // Per-site accumulator would need storage; approximate with expected count this frame.
    float expected = rate * dt_seconds;
    while (expected >= 1.0f && effects->particle_count < effects->particle_capacity)
    {
      particle_effects_spawn_flame_at(effects, (float)site->x + 0.5f, (float)site->y + 0.5f,
                                      (float)site->z + 0.85f, site->type, spread);
      expected -= 1.0f;
    }
    if (expected > 0.0f && particle_rng_float(&effects->rng_state) < expected &&
        effects->particle_count < effects->particle_capacity)
    {
      particle_effects_spawn_flame_at(effects, (float)site->x + 0.5f, (float)site->y + 0.5f,
                                      (float)site->z + 0.85f, site->type, spread);
    }
  }
}

static void particle_effects_spawn_frost_from_projectiles(WorldParticleEffects *effects,
                                                          const ProjectileSystem *projectiles,
                                                          float camera_x, float camera_y,
                                                          float camera_z, int render_distance,
                                                          float dt_seconds)
{
  if (!effects || !projectiles)
    return;

  for (int i = 0; i < PROJECTILE_MAX; i++)
  {
    const Projectile *proj = &projectiles->items[i];
    if (!proj->active ||
        (proj->kind != PROJECTILE_ICE_BOLT && proj->kind != PROJECTILE_LIGHTNING &&
         proj->kind != PROJECTILE_MAGIC_MISSILE && proj->kind != PROJECTILE_SHADOW_STRIKE))
      continue;

    const float dx = proj->x - camera_x;
    const float dy = proj->y - camera_y;
    const float dz = proj->z - camera_z;
    if (sqrtf(dx * dx + dy * dy + dz * dz) > (float)render_distance)
      continue;

    float expected = 28.0f * dt_seconds;
    while (expected >= 1.0f && effects->particle_count < effects->particle_capacity)
    {
      Particle *p = &effects->particles[effects->particle_count++];
      p->x = proj->x + particle_rng_range(&effects->rng_state, -0.2f, 0.2f);
      p->y = proj->y + particle_rng_range(&effects->rng_state, -0.2f, 0.2f);
      p->z = proj->z + particle_rng_range(&effects->rng_state, -0.2f, 0.2f);
      p->vx = particle_rng_range(&effects->rng_state, -0.4f, 0.4f);
      p->vy = particle_rng_range(&effects->rng_state, -0.4f, 0.4f);
      p->vz = particle_rng_range(&effects->rng_state, -0.8f, -0.1f);
      p->max_life = particle_rng_range(&effects->rng_state, 0.25f, 0.55f);
      p->life = p->max_life;
      if (proj->kind == PROJECTILE_SHADOW_STRIKE)
      {
        p->r = (uint8_t)(140 + (int)(particle_rng_float(&effects->rng_state) * 80.0f));
        p->g = (uint8_t)(30 + (int)(particle_rng_float(&effects->rng_state) * 50.0f));
        p->b = (uint8_t)(160 + (int)(particle_rng_float(&effects->rng_state) * 80.0f));
      }
      else if (proj->kind == PROJECTILE_MAGIC_MISSILE)
      {
        p->r = (uint8_t)(160 + (int)(particle_rng_float(&effects->rng_state) * 70.0f));
        p->g = (uint8_t)(90 + (int)(particle_rng_float(&effects->rng_state) * 60.0f));
        p->b = 255;
      }
      else if (proj->kind == PROJECTILE_LIGHTNING)
      {
        p->r = (uint8_t)(200 + (int)(particle_rng_float(&effects->rng_state) * 55.0f));
        p->g = (uint8_t)(220 + (int)(particle_rng_float(&effects->rng_state) * 35.0f));
        p->b = 255;
      }
      else
      {
        p->r = (uint8_t)(180 + (int)(particle_rng_float(&effects->rng_state) * 75.0f));
        p->g = (uint8_t)(210 + (int)(particle_rng_float(&effects->rng_state) * 45.0f));
        p->b = 255;
      }
      p->a = (uint8_t)(140 + (int)(particle_rng_float(&effects->rng_state) * 80.0f));
      p->size_px = particle_rng_range(&effects->rng_state, 1.0f, 2.4f);
      p->kind = PARTICLE_KIND_AMBIENT;
      expected -= 1.0f;
    }
  }
}

static void particle_effects_spawn_flame_from_projectiles(WorldParticleEffects *effects,
                                                          const ProjectileSystem *projectiles,
                                                          float camera_x, float camera_y,
                                                          float camera_z, int render_distance,
                                                          float dt_seconds)
{
  if (!effects || !effects->flame_enabled || !projectiles)
    return;

  for (int i = 0; i < PROJECTILE_MAX; i++)
  {
    const Projectile *proj = &projectiles->items[i];
    if (!proj->active ||
        (proj->kind != PROJECTILE_FIREBALL && proj->kind != PROJECTILE_METEOR))
      continue;

    const float dx = proj->x - camera_x;
    const float dy = proj->y - camera_y;
    const float dz = proj->z - camera_z;
    if (sqrtf(dx * dx + dy * dy + dz * dz) > (float)render_distance)
      continue;

    float expected = PARTICLE_FLAME_SPAWN_RATE_FIREBALL * dt_seconds;
    while (expected >= 1.0f && effects->particle_count < effects->particle_capacity)
    {
      // Slightly behind the ball so the trail reads as wake rather than a second core.
      const float speed = sqrtf(proj->vx * proj->vx + proj->vy * proj->vy + proj->vz * proj->vz);
      const float bx = speed > 0.01f ? proj->vx / speed : 0.0f;
      const float by = speed > 0.01f ? proj->vy / speed : 0.0f;
      const float bz = speed > 0.01f ? proj->vz / speed : 0.0f;
      particle_effects_spawn_flame_at(effects, proj->x - bx * 0.25f, proj->y - by * 0.25f,
                                      proj->z - bz * 0.25f, VOXEL_CAMPFIRE, 0.2f);
      expected -= 1.0f;
    }
    if (expected > 0.0f && particle_rng_float(&effects->rng_state) < expected &&
        effects->particle_count < effects->particle_capacity)
    {
      particle_effects_spawn_flame_at(effects, proj->x, proj->y, proj->z, VOXEL_CAMPFIRE, 0.2f);
    }
  }
}

void particle_effects_update(World *world, float dt_seconds,
                             float camera_x, float camera_y, float camera_z,
                             int render_distance,
                             const ProjectileSystem *projectiles)
{
  if (!world || dt_seconds <= 0.0f)
    return;

  if (!world->particle_effects)
    particle_effects_init_for_world(world);
  if (!world->particle_effects)
    return;

  WorldParticleEffects *effects = world->particle_effects;
  if (effects->dust_rebuild_cooldown_s > 0.0f)
    effects->dust_rebuild_cooldown_s -= dt_seconds;

  // A fluid/fire tick bumps voxel_revision every 50 ms. Dust site rebuilds used to scan the whole
  // 128³; now they are camera-local, but still coalesce to ≤2 Hz so a wet tick cannot own the frame.
  if (effects->sites_voxel_revision != world->voxel_revision &&
      effects->dust_rebuild_cooldown_s <= 0.0f)
  {
    particle_effects_rebuild_dust_sites_near(world, camera_x, camera_y, camera_z);
    effects->dust_rebuild_cooldown_s = 1.0f;
  }

  effects->meteor_impacts_this_frame = 0;

  int write = 0;
  for (int i = 0; i < effects->particle_count; i++)
  {
    Particle *p = &effects->particles[i];
    p->life -= dt_seconds;
    if (p->life <= 0.0f)
      continue;

    p->x += p->vx * dt_seconds;
    p->y += p->vy * dt_seconds;
    p->z += p->vz * dt_seconds;

    // Flames cool and slow as they rise; ambient particles keep their launch velocity.
    if (p->kind == PARTICLE_KIND_FLAME)
    {
      p->vz *= (1.0f - 0.8f * dt_seconds);
      p->vx *= (1.0f - 1.5f * dt_seconds);
      p->vy *= (1.0f - 1.5f * dt_seconds);
    }

    // Meteors die on terrain contact and leave a magma splash.
    if (p->kind == PARTICLE_KIND_METEOR)
    {
      const int ix = (int)floorf(p->x);
      const int iy = (int)floorf(p->y);
      const int iz = (int)floorf(p->z);
      if (ix >= 0 && iy >= 0 && iz >= 0 && ix < (int)world->width && iy < (int)world->height &&
          iz < (int)world->depth)
      {
        Voxel *cell = world_get_voxel(world, (uint32_t)ix, (uint32_t)iy, (uint32_t)iz);
        if (cell && world_voxel_type_blocks_movement(cell->type))
        {
          (void)particle_effects_meteor_impact(world, effects, p);
          continue; // consume the meteor
        }
      }
      if (p->z < 0.0f)
      {
        (void)particle_effects_meteor_impact(world, effects, p);
        continue;
      }
    }

    // Alpha is faded at draw time from life/max_life so we do not compound multiply each frame.
    effects->particles[write++] = *p;
  }
  effects->particle_count = write;

  if (effects->dust_enabled && effects->dust_site_count > 0)
  {
    effects->spawn_accum += dt_seconds * PARTICLE_DUST_SPAWN_RATE;
    while (effects->spawn_accum >= 1.0f &&
           effects->particle_count < effects->particle_capacity)
    {
      if (!particle_effects_spawn_dust(world, effects, camera_x, camera_y, camera_z, render_distance))
        break;
      effects->spawn_accum -= 1.0f;
    }
    if (effects->spawn_accum > 4.0f)
      effects->spawn_accum = 4.0f;
  }

  const bool rain_active = effects->rain_enabled || effects->heavy_rain_enabled;
  if (rain_active)
  {
    const int cx = (int)floorf(camera_x);
    const int cy = (int)floorf(camera_y);
    const bool camera_moved =
        abs(cx - effects->rain_sites_camera_x) >= PARTICLE_RAIN_SITE_REBUILD_MOVE ||
        abs(cy - effects->rain_sites_camera_y) >= PARTICLE_RAIN_SITE_REBUILD_MOVE;
    // Never treat an empty site list as "not yet built" — a dry biome would rescan every frame.
    if (!effects->rain_sites_ready || camera_moved ||
        (effects->rain_sites_voxel_revision != world->voxel_revision &&
         effects->dust_rebuild_cooldown_s <= 0.0f))
    {
      particle_effects_rebuild_rain_sites(world, effects, camera_x, camera_y);
    }

    particle_effects_spawn_rain_particles(effects, camera_x, camera_y, camera_z, render_distance,
                                          dt_seconds);

    if (effects->heavy_rain_enabled)
      particle_effects_spawn_rain_splashes(world, effects, dt_seconds, true);
    else if (effects->rain_enabled)
      particle_effects_spawn_rain_splashes(world, effects, dt_seconds, false);
  }
  else
  {
    particle_effects_clear_rain_sites(effects);
    effects->rain_sites_ready = false;
  }

  if (effects->flame_enabled)
  {
    const int cx = (int)floorf(camera_x);
    const int cy = (int)floorf(camera_y);
    const bool camera_moved =
        abs(cx - effects->flame_sites_camera_x) >= PARTICLE_FLAME_SITE_REBUILD_MOVE ||
        abs(cy - effects->flame_sites_camera_y) >= PARTICLE_FLAME_SITE_REBUILD_MOVE;
    const bool lit_changed =
        effects->flame_sites_condition_revision != world->condition_revision;
    // Same empty-list trap as rain: no campfires nearby must not mean "rebuild forever".
    if (!effects->flame_sites_ready || camera_moved || lit_changed ||
        (effects->flame_sites_voxel_revision != world->voxel_revision &&
         effects->dust_rebuild_cooldown_s <= 0.0f))
    {
      particle_effects_rebuild_flame_sites(world, effects, camera_x, camera_y);
    }

    particle_effects_spawn_flame_from_sites(world, effects, camera_x, camera_y, camera_z,
                                            render_distance, dt_seconds);
    particle_effects_spawn_flame_from_projectiles(effects, projectiles, camera_x, camera_y,
                                                  camera_z, render_distance, dt_seconds);
    particle_effects_spawn_frost_from_projectiles(effects, projectiles, camera_x, camera_y,
                                                  camera_z, render_distance, dt_seconds);
  }
  else
  {
    particle_effects_clear_flame_sites(effects);
    effects->flame_sites_ready = false;
  }

  if (effects->meteor_storm_enabled)
  {
    particle_effects_spawn_meteor_particles(effects, camera_x, camera_y, camera_z, render_distance,
                                            dt_seconds);
  }
}

void particle_effects_set_dust_enabled(World *world, bool enabled)
{
  if (!world || !world->particle_effects)
    return;
  world->particle_effects->dust_enabled = enabled;
}

bool particle_effects_dust_enabled(const World *world)
{
  if (!world || !world->particle_effects)
    return false;
  return world->particle_effects->dust_enabled;
}

void particle_effects_set_rain_enabled(World *world, bool enabled)
{
  if (!world)
    return;
  if (!world->particle_effects)
    particle_effects_init_for_world(world);
  if (!world->particle_effects)
    return;
  world->particle_effects->rain_enabled = enabled;
}

bool particle_effects_rain_enabled(const World *world)
{
  if (!world || !world->particle_effects)
    return false;
  return world->particle_effects->rain_enabled;
}

void particle_effects_set_heavy_rain_enabled(World *world, bool enabled)
{
  if (!world)
    return;
  if (!world->particle_effects)
    particle_effects_init_for_world(world);
  if (!world->particle_effects)
    return;
  world->particle_effects->heavy_rain_enabled = enabled;
}

bool particle_effects_heavy_rain_enabled(const World *world)
{
  if (!world || !world->particle_effects)
    return false;
  return world->particle_effects->heavy_rain_enabled;
}

void particle_effects_set_flame_enabled(World *world, bool enabled)
{
  if (!world)
    return;
  if (!world->particle_effects)
    particle_effects_init_for_world(world);
  if (!world->particle_effects)
    return;
  world->particle_effects->flame_enabled = enabled;
}

bool particle_effects_flame_enabled(const World *world)
{
  if (!world || !world->particle_effects)
    return false;
  return world->particle_effects->flame_enabled;
}

void particle_effects_set_meteor_storm_enabled(World *world, bool enabled)
{
  if (!world)
    return;
  if (!world->particle_effects)
    particle_effects_init_for_world(world);
  if (!world->particle_effects)
    return;
  world->particle_effects->meteor_storm_enabled = enabled;
  if (enabled)
  {
    // Rain under a lava storm looks wrong; clear local rain while the storm runs.
    world->particle_effects->rain_enabled = false;
    world->particle_effects->heavy_rain_enabled = false;
  }
}

bool particle_effects_meteor_storm_enabled(const World *world)
{
  if (!world || !world->particle_effects)
    return false;
  return world->particle_effects->meteor_storm_enabled;
}

void particle_effects_set_meteor_storm_universe_enabled(bool enabled)
{
  g_meteor_storm_universe = enabled;
}

bool particle_effects_meteor_storm_universe_enabled(void)
{
  return g_meteor_storm_universe;
}

void particle_effects_set_thunderstorm_enabled(World *world, bool enabled)
{
  if (!world)
    return;
  if (!world->particle_effects)
    particle_effects_init_for_world(world);
  if (!world->particle_effects)
    return;
  world->particle_effects->thunderstorm_enabled = enabled;
  if (enabled)
  {
    // Thunderstorms bring heavy rain; meteor storms still own the sky when both fight.
    if (!world->particle_effects->meteor_storm_enabled)
    {
      world->particle_effects->heavy_rain_enabled = true;
      world->particle_effects->rain_enabled = false;
    }
  }
}

bool particle_effects_thunderstorm_enabled(const World *world)
{
  if (!world || !world->particle_effects)
    return false;
  return world->particle_effects->thunderstorm_enabled;
}

void particle_effects_set_thunderstorm_universe_enabled(bool enabled)
{
  g_thunderstorm_universe = enabled;
}

bool particle_effects_thunderstorm_universe_enabled(void)
{
  return g_thunderstorm_universe;
}

int particle_effects_drain_surface_splashes(World *world, FluidSplash *out, int max)
{
  if (!world || !world->particle_effects || !out || max <= 0)
    return 0;

  WorldParticleEffects *effects = world->particle_effects;
  int written = 0;
  while (written < max && effects->surface_splash_count > 0)
  {
    out[written++] = effects->surface_splashes[effects->surface_splash_head];
    effects->surface_splash_head =
        (effects->surface_splash_head + 1) % PARTICLE_SURFACE_SPLASH_CAP;
    effects->surface_splash_count--;
  }
  return written;
}

void particle_effects_spawn_blast(World *world, float x, float y, float z, bool ice,
                                  float radius)
{
  if (!world)
    return;
  if (!world->particle_effects)
    particle_effects_init_for_world(world);
  if (!world->particle_effects)
    return;

  WorldParticleEffects *effects = world->particle_effects;
  if (!ice && !effects->flame_enabled)
    return;

  const float r = radius > 0.5f ? radius : 1.5f;
  const int count = ice ? 18 : 22;
  for (int i = 0; i < count && effects->particle_count < effects->particle_capacity; i++)
  {
    // Uniform-ish direction on the sphere so the burst reads as an explosion, not a column.
    const float u = particle_rng_float(&effects->rng_state);
    const float v = particle_rng_float(&effects->rng_state);
    const float theta = u * 6.2831853f;
    const float phi = acosf(2.0f * v - 1.0f);
    const float sp = sinf(phi);
    const float dx = cosf(theta) * sp;
    const float dy = sinf(theta) * sp;
    const float dz = cosf(phi);
    const float speed = particle_rng_range(&effects->rng_state, r * 1.2f, r * 3.4f);

    Particle *p = &effects->particles[effects->particle_count++];
    p->x = x + dx * particle_rng_range(&effects->rng_state, 0.0f, 0.35f);
    p->y = y + dy * particle_rng_range(&effects->rng_state, 0.0f, 0.35f);
    p->z = z + dz * particle_rng_range(&effects->rng_state, 0.0f, 0.35f);
    p->vx = dx * speed;
    p->vy = dy * speed;
    p->vz = dz * speed + (ice ? 0.0f : particle_rng_range(&effects->rng_state, 0.5f, 2.0f));
    p->max_life = particle_rng_range(&effects->rng_state, 0.28f, 0.65f);
    p->life = p->max_life;
    if (ice)
    {
      p->r = (uint8_t)(170 + (int)(particle_rng_float(&effects->rng_state) * 85.0f));
      p->g = (uint8_t)(200 + (int)(particle_rng_float(&effects->rng_state) * 55.0f));
      p->b = 255;
      p->a = (uint8_t)(150 + (int)(particle_rng_float(&effects->rng_state) * 90.0f));
      p->size_px = particle_rng_range(&effects->rng_state, 1.4f, 3.2f);
      p->kind = PARTICLE_KIND_AMBIENT;
    }
    else
    {
      const float heat = particle_rng_float(&effects->rng_state);
      if (heat > 0.55f)
      {
        p->r = 255;
        p->g = (uint8_t)(180 + (int)(particle_rng_float(&effects->rng_state) * 75.0f));
        p->b = (uint8_t)(40 + (int)(particle_rng_float(&effects->rng_state) * 80.0f));
      }
      else
      {
        p->r = 255;
        p->g = (uint8_t)(90 + (int)(particle_rng_float(&effects->rng_state) * 80.0f));
        p->b = (uint8_t)(10 + (int)(particle_rng_float(&effects->rng_state) * 40.0f));
      }
      p->a = (uint8_t)(160 + (int)(particle_rng_float(&effects->rng_state) * 90.0f));
      p->size_px = particle_rng_range(&effects->rng_state, 1.8f, 4.2f);
      p->kind = PARTICLE_KIND_FLAME;
    }
  }
}
