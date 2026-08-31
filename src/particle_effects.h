#ifndef PARTICLE_EFFECTS_H
#define PARTICLE_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "fluid_sim.h"
#include "world.h"

// Forward declaration for client render path (SDL lives in the renderer).
struct IsometricRenderer;
struct SDL_Renderer;
struct ProjectileSystem;

#define PARTICLE_SURFACE_SPLASH_CAP 128

// Extensible effect kinds. Add new entries before PARTICLE_EFFECT_COUNT.
typedef enum
{
  PARTICLE_EFFECT_DRIFTING_DUST = 0,
  PARTICLE_EFFECT_RAIN,
  PARTICLE_EFFECT_HEAVY_RAIN,
  PARTICLE_EFFECT_FLAME,
  PARTICLE_EFFECT_METEOR_STORM,
  PARTICLE_EFFECT_COUNT
} ParticleEffectKind;

// Visual / motion class for a live particle. Fade is applied at draw time from life/max_life.
typedef enum
{
  PARTICLE_KIND_AMBIENT = 0, // dust, rain
  PARTICLE_KIND_FLAME,
  PARTICLE_KIND_METEOR // falling magma rock; impacts place magma
} ParticleKind;

// Static definition for tooling / editor integration later.
typedef struct ParticleEffectDef
{
  ParticleEffectKind kind;
  const char *name;
  const char *description;
  int clearance_distance; // minimum open-air clearance for spatial emitters
  bool enabled_by_default;
} ParticleEffectDef;

typedef struct Particle
{
  float x, y, z;
  float vx, vy, vz;
  float life;
  float max_life;
  uint8_t r, g, b, a;
  float size_px;
  ParticleKind kind;
} Particle;

typedef struct ParticleEmitterSite
{
  int x, y, z;
  VoxelType type; // candle vs campfire for spawn intensity
} ParticleEmitterSite;

typedef struct WorldParticleEffects
{
  Particle *particles;
  int particle_count;
  int particle_capacity;

  ParticleEmitterSite *dust_sites;
  int dust_site_count;
  int dust_site_capacity;

  uint32_t rng_state;
  bool dust_enabled;
  float spawn_accum;
  bool rain_enabled;
  float rain_spawn_accum;
  bool heavy_rain_enabled;
  float heavy_rain_spawn_accum;
  bool flame_enabled;
  float flame_spawn_accum;
  bool meteor_storm_enabled;
  float meteor_storm_spawn_accum;
  int meteor_impacts_this_frame;
  bool thunderstorm_enabled;

  // Exposed water surfaces near the camera, for rain ripples. Rebuilt when the view moves or the
  // world changes; only maintained while rain is active.
  ParticleEmitterSite *rain_sites;
  int rain_site_count;
  int rain_site_capacity;
  int rain_sites_camera_x;
  int rain_sites_camera_y;
  uint64_t rain_sites_voxel_revision;
  bool rain_sites_ready; // true after first rebuild; count==0 must not force a rescan

  // Lit fixtures near the camera (candles, campfires, burning voxels). Same rebuild cadence as
  // rain sites; also rebuild when condition bits change so extinguish/ignite update emitters.
  ParticleEmitterSite *flame_sites;
  int flame_site_count;
  int flame_site_capacity;
  int flame_sites_camera_x;
  int flame_sites_camera_y;
  uint64_t flame_sites_voxel_revision;
  uint64_t flame_sites_condition_revision;
  bool flame_sites_ready;

  // Ripples waiting for the renderer's fluid_surface cache. Same shape as a fluid arrival, but
  // recorded here because rain does not move coarse fluid.
  FluidSplash surface_splashes[PARTICLE_SURFACE_SPLASH_CAP];
  int surface_splash_head;
  int surface_splash_count;

  uint64_t sites_voxel_revision;
  float dust_rebuild_cooldown_s; // coalesce full-world dust scans after fluid ticks
} WorldParticleEffects;

void particle_effects_registry_init(void);
const ParticleEffectDef *particle_effect_def(ParticleEffectKind kind);
const ParticleEffectDef *particle_effect_def_by_name(const char *name);
ParticleEffectKind particle_effect_kind_from_name(const char *name);

WorldParticleEffects *particle_effects_create(void);
void particle_effects_destroy(WorldParticleEffects *effects);

// Attach default effects to a world (drifting dust). Safe to call after generate/load.
void particle_effects_init_for_world(World *world);

// Rebuild dust emitter sites when voxel layout changes (full world — prefer _near during play).
void particle_effects_rebuild_dust_sites(World *world);
void particle_effects_rebuild_dust_sites_near(World *world, float camera_x, float camera_y,
                                               float camera_z);

bool particle_effects_air_clearance_at_least(World *world, int x, int y, int z,
                                             int clearance);

// True for fixtures that should emit rising flame particles.
bool particle_effects_voxel_emits_flame(VoxelType type);

void particle_effects_update(World *world, float dt_seconds,
                             float camera_x, float camera_y, float camera_z,
                             int render_distance,
                             const struct ProjectileSystem *projectiles);

void particle_effects_set_dust_enabled(World *world, bool enabled);
bool particle_effects_dust_enabled(const World *world);

void particle_effects_set_rain_enabled(World *world, bool enabled);
bool particle_effects_rain_enabled(const World *world);

void particle_effects_set_heavy_rain_enabled(World *world, bool enabled);
bool particle_effects_heavy_rain_enabled(const World *world);

void particle_effects_set_flame_enabled(World *world, bool enabled);
bool particle_effects_flame_enabled(const World *world);

// Meteor / lava storm: falling magma meteors that splash VOXEL_MAGMA on impact.
// The universe flag is sticky for newly generated worlds; per-world toggles still apply.
void particle_effects_set_meteor_storm_enabled(World *world, bool enabled);
bool particle_effects_meteor_storm_enabled(const World *world);
void particle_effects_set_meteor_storm_universe_enabled(bool enabled);
bool particle_effects_meteor_storm_universe_enabled(void);

// Thunderstorm: heavy rain plus jagged lightning that can ignite dry fuel. Implemented in
// weather_storm.c; flags live on WorldParticleEffects so the console/UI can share them.
void particle_effects_set_thunderstorm_enabled(World *world, bool enabled);
bool particle_effects_thunderstorm_enabled(const World *world);
void particle_effects_set_thunderstorm_universe_enabled(bool enabled);
bool particle_effects_thunderstorm_universe_enabled(void);

// Take up to max rain ripples queued for the surface cache. Drains the buffer.
int particle_effects_drain_surface_splashes(World *world, FluidSplash *out, int max);

// One-shot burst at a spell impact: fireball sprays flame outward, ice bolt sprays frost.
// `ice` selects the palette; `radius` scales how far the spray reaches.
void particle_effects_spawn_blast(World *world, float x, float y, float z, bool ice,
                                  float radius);

#endif // PARTICLE_EFFECTS_H
