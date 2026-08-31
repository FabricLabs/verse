#ifndef VERSE_PROJECTILE_H
#define VERSE_PROJECTILE_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel.h"
#include "world.h"

struct Universe; // for universe-aware stepping across world seams

// Things in flight: a fireball the spirit throws, an ice bolt, and whatever else later wants to
// arc under gravity and hit something.
//
// Deliberately free of SDL and of GameState, so the whole of it can be stepped and asserted on in a
// headless test. The caller owns the system, feeds it a world and a time step, and reads back the
// impacts; nothing in here draws, and nothing in here knows what a player is.
//
// Travel is substepped for the same reason the player's movement is: a projectile moves far enough
// per frame that testing only its destination would let it pass through a wall. Each substep is
// short enough that no voxel it crosses goes unexamined.

#define PROJECTILE_MAX 64

// How far a projectile may advance between collision checks, in voxels. Under half a voxel, so a
// one-voxel wall is always sampled at least twice however the path is aligned.
#define PROJECTILE_MAX_STEP 0.35f

// A bound on substeps per frame, so a pathological frame delta cannot spin. A projectile that runs
// out of substeps simply arrives late, which is invisible next to the hitch that caused it.
#define PROJECTILE_MAX_SUBSTEPS 64

typedef enum
{
  PROJECTILE_FIREBALL = 0,
  PROJECTILE_ICE_BOLT = 1,
  PROJECTILE_MAGIC_MISSILE = 2,
  PROJECTILE_LIGHTNING = 3,
  PROJECTILE_SHADOW_STRIKE = 4,
  PROJECTILE_METEOR = 5,
  PROJECTILE_KIND_COUNT
} ProjectileKind;

// The chance foliage has of stopping a projectile, per foliage voxel crossed.
//
// Per voxel, not per frame or per step. What guarantees that is the roll being a hash of the
// projectile and the voxel rather than draws from a running generator: however many times a step
// lands inside a given leaf, the answer for that leaf is the same one. A generator would instead be
// sampled more often at a fine time step than a coarse one, and the same shot through the same canopy
// would land at 120fps and be swallowed at 30.
//
// Leaves are the denser obstacle; a tuft of grass rarely stops anything.
#define PROJECTILE_LEAVES_STOP_CHANCE 0.25f
#define PROJECTILE_GRASS_STOP_CHANCE  0.08f

// Actor pose is the feet. Shots travel at spirit / chest height, so a sphere on the feet alone
// misses every standing target. Hit test is a vertical capsule: XY uses this body radius plus the
// projectile radius; Z covers roughly ankles to head.
#define PROJECTILE_ACTOR_BODY_RADIUS 0.55f
#define PROJECTILE_ACTOR_HIT_BELOW   0.35f
#define PROJECTILE_ACTOR_HIT_ABOVE   1.75f

typedef struct
{
  bool active;

  float x, y, z;    // world position in voxels
  float vx, vy, vz; // voxels per second
  float life;       // seconds remaining before it burns out

  float radius;         // how close to an actor counts as a hit, in voxels
  float gravity_scale;  // 0 flies flat; 1 falls at the world's gravity
  float gravity_delay;  // seconds of flat flight remaining before gravity applies
  uint32_t damage;
  uint32_t owner_id;    // caster actor id, or MOB_THREAT_PLAYER for the spirit
  ProjectileKind kind;
  // Skill potency baked at cast (100 = INT 10 baseline). Scales ice chill / cool / freeze.
  uint16_t potency;

  // Seed for this projectile's foliage rolls. Keeping it per projectile, and hashing it with the
  // voxel coordinate, makes the outcome reproducible: the same shot through the same canopy is
  // stopped by the same leaf every time, which is what lets a test assert on it at all.
  uint32_t seed;

  // The voxel the projectile was in when it was last examined, so entering a new foliage voxel can
  // be told apart from continuing to sit inside one.
  int last_vx, last_vy, last_vz;

  // Universe cell the local (x,y,z) are relative to. When bind_universe is set, leaving the world
  // box rebases into a neighbour instead of expiring — shots can cross seams the way the player can.
  bool bind_universe;
  uint64_t universe_x, universe_y, universe_z;
} Projectile;

typedef struct ProjectileSystem
{
  Projectile items[PROJECTILE_MAX];
  uint32_t next_seed;
  uint32_t spawned_total;  // lifetime counter, useful for tests and diagnostics
  uint32_t dropped_total;  // spawns refused because the pool was full
} ProjectileSystem;

typedef enum
{
  PROJECTILE_IMPACT_VOXEL = 0, // stopped by something solid, or by foliage that caught it
  PROJECTILE_IMPACT_ACTOR,
  PROJECTILE_IMPACT_EXPIRED    // burned out or left the world without hitting anything
} ProjectileImpactKind;

typedef struct
{
  ProjectileImpactKind kind;
  float x, y, z;      // where it stopped
  int vx, vy, vz;     // the voxel involved, for a voxel impact
  VoxelType type;     // what it hit, for a voxel impact
  uint32_t actor_id;  // which actor, for an actor impact
  uint32_t damage;
  uint32_t owner_id;  // who launched the projectile
  ProjectileKind projectile;
  uint16_t potency;   // caster skill potency at launch (100 = baseline)
  float speed;        // voxels/s at the moment of impact, for impulse conversion
  World *world;       // borrowed: the world the hit was resolved in (may differ after a seam cross)
} ProjectileImpact;

typedef struct
{
  float x, y, z;
  float dir_x, dir_y, dir_z; // need not be normalised
  float speed;               // voxels per second
  float life;                // seconds
  float radius;
  float gravity_scale;
  float gravity_delay; // seconds before gravity; 0 starts falling immediately
  uint32_t damage;
  uint32_t owner_id;
  ProjectileKind kind;
  uint16_t potency; // 0 defaults to 100 (baseline) at spawn

  // When true, the projectile is anchored to a universe cell and may cross into loaded neighbours.
  // Headless tests leave this false so out-of-bounds still means "left the arena".
  bool bind_universe;
  uint64_t universe_x, universe_y, universe_z;
} ProjectileSpawn;

// Empty the pool. `seed` seeds the per-projectile seeds, so a run is reproducible.
void projectile_system_reset(ProjectileSystem *sys, uint32_t seed);

// Launch one. Returns false when the pool is full or the direction is degenerate; a refused spawn
// is counted in dropped_total rather than reported, since a caster holding down a key is expected
// to hit the cap.
bool projectile_spawn(ProjectileSystem *sys, const ProjectileSpawn *spawn);

int projectile_active_count(const ProjectileSystem *sys);

// Advance every live projectile by dt seconds against `world`, resolving voxel and actor hits.
//
// Impacts are appended to `impacts` up to `max_impacts`; pass NULL to discard them. `out_impacts`
// receives how many were written. Damage is *not* applied to actors here — the impact reports which
// actor was hit and for how much, and the caller decides what that means, so this stays free of the
// rules of combat.
//
// When `universe` is non-NULL, projectiles spawned with bind_universe may leave `world` and continue
// in a loaded neighbour (coordinates rebased). Unloaded neighbours still expire the shot.
void projectile_system_step(ProjectileSystem *sys, World *world, float dt,
                            ProjectileImpact *impacts, int max_impacts, int *out_impacts);

void projectile_system_step_universe(ProjectileSystem *sys, struct Universe *universe, World *world,
                                     float dt, ProjectileImpact *impacts, int max_impacts,
                                     int *out_impacts);

// The chance a single voxel of this material has of stopping a projectile. Zero for anything that
// is not foliage: solid materials stop it outright and air does not touch it.
float projectile_foliage_stop_chance(VoxelType type);

#endif // VERSE_PROJECTILE_H
