#ifndef MOB_AI_H
#define MOB_AI_H

#include <stdbool.h>
#include <stdint.h>
#include "actor.h"
#include "currency.h"
#include "world.h"

// Mob types
typedef enum {
    MOB_TYPE_NONE = 0,
    MOB_TYPE_SOLVER,     // Maze solver that seeks exits
    MOB_TYPE_WANDERER,   // Random movement on walkable ground
    MOB_TYPE_GUARD,      // Patrols an area
    MOB_TYPE_HUNTER,     // Seeks and follows targets
    MOB_TYPE_BUILDER,    // Places blocks
    MOB_TYPE_DIGGER,     // Removes blocks
    MOB_TYPE_BIRD,       // Perch, take off, circle, glide in to land
    MOB_TYPE_SHEEP,      // Grazing livestock; timid wanderer
    MOB_TYPE_CHICKEN,    // Small livestock; timid wanderer
    MOB_TYPE_BAT,        // Roost / short flight cycle (bird-like)
    MOB_TYPE_DEER,       // Herding browser; flees when threatened
    MOB_TYPE_LIZARD,     // Solitary ground reptile
    MOB_TYPE_SPIDER,     // Aggressive skitterer
    MOB_TYPE_SLIME,      // Slow gelatinous wanderer
    MOB_TYPE_VILLAGER    // Settlement resident; tends livestock
} MobType;

typedef enum {
    BIRD_KIND_SPARROW = 0,
    BIRD_KIND_CROW,
    BIRD_KIND_GULL,
    BIRD_KIND_COUNT
} BirdKind;

// Adult male/female or child — same mob type, different stature and dialogue.
typedef enum {
    VILLAGER_MALE = 0,
    VILLAGER_FEMALE,
    VILLAGER_CHILD,
    VILLAGER_KIND_COUNT
} VillagerKind;

// Settlement trades. Children use NONE / APPRENTICE until grown.
typedef enum {
    VILLAGER_JOB_NONE = 0,
    VILLAGER_JOB_FARMER,
    VILLAGER_JOB_SHEPHERD,
    VILLAGER_JOB_MILLER,
    VILLAGER_JOB_BAKER,
    VILLAGER_JOB_GUARD,
    VILLAGER_JOB_HEALER,
  VILLAGER_JOB_MERCHANT,
  VILLAGER_JOB_BLACKSMITH,
  VILLAGER_JOB_APPRENTICE,
  VILLAGER_JOB_COUNT
} VillagerProfession;

#define MOB_NAME_PART_MAX 20
#define MOB_STORY_MAX 6
#define MOB_STORY_TEXT_MAX 80
#define MOB_PARENT_MAX 2
// Parent/child and same-household bond on first encounter.
#define MOB_FAMILY_AFFINITY 60

// Gossip a villager remembers — about a person, a family line, or both.
typedef struct {
    uint32_t about_actor_id;  // 0 when the tale is about a family only
    uint32_t about_family_id; // 0 when the tale is about one actor only
    int8_t sentiment;         // -100..+100 bias carried into the child's view
    char text[MOB_STORY_TEXT_MAX];
} MobStoryEntry;

typedef enum {
    BIRD_PHASE_PERCH = 0,
    BIRD_PHASE_TAKEOFF,
    BIRD_PHASE_CIRCLE,
    BIRD_PHASE_APPROACH
} BirdPhase;

// Emotional overlay — modulates wander cadence and combat reactions.
typedef enum {
    MOB_MOOD_CALM = 0,
    MOB_MOOD_CURIOUS,
    MOB_MOOD_ANGRY,
    MOB_MOOD_FEARFUL
} MobMood;

// High-level intent. Wanderers chase/flee when damaged enough; BREED is reserved for farm AI.
// FOLLOW is for domesticated livestock that shadow a villager.
typedef enum {
    MOB_GOAL_IDLE = 0,
    MOB_GOAL_WANDER,
    MOB_GOAL_CHASE,
    MOB_GOAL_FLEE,
    MOB_GOAL_BREED,
    MOB_GOAL_FOLLOW
} MobGoal;

// Sentinel aggro target for the player spirit (not present in runtime_actors).
#define MOB_THREAT_PLAYER 0xFFFFFFFFu

typedef struct {
    const char *name;
    const char *description;
    float lift;          // fraction of gravity cancelled while gliding (0-1)
    float glide_speed;   // voxels/second of forward drift with no WASD
    float takeoff_vz;    // launch speed, voxels/second
    float cruise_alt;    // how high AI climbs above perch, voxels
    float circle_radius; // AI orbit radius
    float circle_omega;  // AI orbit rate, rad/s
    float bank_rate;     // Q/E roll rate toward max bank, rad/s
    float sink_max;      // max downward speed while gliding
    float weight;        // extra kg while inhabited
    uint8_t r, g, b;
} BirdStats;

#define MOB_WANDER_SPEED 1.25  // voxels/second; slower than a walking spirit
#define MOB_CHASE_SPEED  1.85  // voxels/second while aggro'd

// Soft radius for actor-actor separation (voxels).
#define MOB_ACTOR_RADIUS 0.55f
// Soft vertical band for actor-actor push (same floor / short steps). Stacked floors ignore each other.
#define MOB_ACTOR_SEPARATION_Z 1.35f
// Prefer same-type neighbors within this range when wandering.
#define MOB_HERD_RANGE 10.0f
#define MOB_HERD_COMFORT 3.5f
// First contact / memory slots for affections and dislikes.
#define MOB_REPUTATION_MAX 16
#define MOB_REPUTATION_ENCOUNTER_RANGE 8.0f
#define MOB_RELATION_MAX 8
// Villager tending raises affinity; at this score the animal follows them.
#define MOB_DOMESTICATE_AFFINITY 40
#define MOB_DOMESTICATE_RANGE 6.0f
#define MOB_FOLLOW_COMFORT 2.4f

// Score toward another actor (-100 dislike .. +100 affection). 0 = unknown / neutral.
typedef struct {
    uint32_t other_id;
    int8_t score;
} MobReputationEntry;

typedef enum {
    MOB_REL_NONE = 0,
    MOB_REL_KIN,
    MOB_REL_RIVAL,
    MOB_REL_OWED,
    MOB_REL_FEAR
} MobRelationKind;

typedef struct {
    uint32_t other_id;
    MobRelationKind kind;
} MobRelationEntry;

// AI state for pathfinding
typedef struct {
    int target_x;
    int target_y;
    int target_z;
    bool has_target;

    // For SOLVER: track visited positions to avoid loops
    uint8_t* visited_map;  // Bit array of visited positions
    int visited_map_size;

    // Simple pathfinding state
    int last_x;
    int last_y;
    int last_z;
    int stuck_counter;
    // Seconds until a wanderer picks a new destination. <= 0 means pick now.
    float retarget_in;
    // Seconds without cell progress while walking toward a target.
    float path_stuck_in;

    // Bird flight cycle. Unused for other mob types.
    uint8_t bird_phase;
    float bird_timer;
    float circle_cx;
    float circle_cy;
    float circle_angle;
    float perch_z;
} AIState;

// Extended actor structure with mob AI
typedef struct {
    Actor base;          // Base actor properties
    MobType mob_type;    // Type of mob
    AIState ai_state;    // AI-specific state
    BirdKind bird_kind;  // Meaningful when mob_type is MOB_TYPE_BIRD
    VillagerKind villager_kind; // Meaningful when mob_type is MOB_TYPE_VILLAGER
    VillagerProfession profession;
    char given_name[MOB_NAME_PART_MAX];
    char family_name[MOB_NAME_PART_MAX];
    uint32_t family_id; // Shared surname / household line (0 = none)
    uint32_t settlement_id; // Site / settlement membership (0 = none)
    uint32_t parent_ids[MOB_PARENT_MAX];
    uint8_t parent_count;
    MobStoryEntry stories[MOB_STORY_MAX];
    uint8_t story_count;
    // Personality facets (-100..+100). Bias dialogue and reactions.
    int8_t trait_bravery;
    int8_t trait_greed;
    int8_t trait_piety;
    int8_t trait_curiosity;
    int8_t trait_loyalty;
    int8_t trait_wrath;
    MobRelationEntry relations[MOB_RELATION_MAX];
    uint8_t relation_count;
    // Unique chronicle beast (forgotten-beast analogue).
    uint32_t unique_id;
    Equipment equipment; // Body gear — spirits have no equipment slots
    bool loot_generated; // Corpse loot table has been rolled into inventory
    Wallet purse;        // Copper / silver / gold held by merchants (and future traders)

    // Polygon mesh. Empty mesh_name keeps the voxel/procedural drawing.
    char mesh_name[32];
    char anim_clip[32];
    float anim_time;
    float anim_lock;     // one-shot remaining; locomotion will not interrupt
    float melee_cooldown;
    float facing_yaw;
    float facing_roll;   // bank angle; set while player-controlled in flight
    uint32_t last_health;

    // Mood / goal / threat memory
    MobMood mood;
    MobGoal goal;
    uint32_t aggro_target_id; // runtime actor id, or MOB_THREAT_PLAYER
    float aggro_ttl;
    uint32_t damage_accum;
    float damage_accum_ttl;
    float mood_timer;
    float breed_cooldown; // set by try_breed; no automatic mate AI yet

    // Domestication / following. Animals set follow_target_id to a villager when bonded.
    uint32_t follow_target_id;
    float domesticate_timer; // villagers: seconds until next tend tick

    // Per-actor affections/dislikes, filled as others are encountered.
    MobReputationEntry reputation[MOB_REPUTATION_MAX];
    uint8_t reputation_count;
    float reputation_scan_in; // seconds until next nearby encounter scan
} MobActor;

// Mob AI functions
MobActor* mob_actor_create(const char* name, MobType type, double x, double y, double z);
MobActor* mob_actor_create_bird(BirdKind kind, double x, double y, double z);
MobActor* mob_actor_create_sheep(double x, double y, double z);
MobActor* mob_actor_create_chicken(double x, double y, double z);
MobActor* mob_actor_create_cow(double x, double y, double z);
MobActor* mob_actor_create_pig(double x, double y, double z);
MobActor* mob_actor_create_rabbit(double x, double y, double z);
MobActor* mob_actor_create_dog(double x, double y, double z);
MobActor* mob_actor_create_cat(double x, double y, double z);
MobActor* mob_actor_create_bat(double x, double y, double z);
MobActor* mob_actor_create_deer(double x, double y, double z);
MobActor* mob_actor_create_elephant(double x, double y, double z);
MobActor* mob_actor_create_lizard(double x, double y, double z);
MobActor* mob_actor_create_spider(double x, double y, double z);
MobActor* mob_actor_create_slime(double x, double y, double z);
MobActor* mob_actor_create_villager(VillagerKind kind, double x, double y, double z);
void mob_actor_destroy(MobActor* mob);
// Choose the next velocity. Does not move the actor; world_step_actors integrates it.
void mob_actor_update(MobActor* mob, World* world, float dt_seconds);

const BirdStats *mob_bird_stats(BirdKind kind);
bool mob_actor_is_bird(const Actor *actor);
bool mob_actor_is_bat(const Actor *actor);
bool mob_actor_is_livestock(const Actor *actor);
bool mob_actor_is_deer(const Actor *actor);
bool mob_actor_is_lizard(const Actor *actor);
bool mob_actor_is_spider(const Actor *actor);
bool mob_actor_is_slime(const Actor *actor);
bool mob_actor_is_villager(const Actor *actor);
VillagerKind mob_actor_villager_kind(const Actor *actor);
VillagerProfession mob_actor_villager_profession(const Actor *actor);
const char *villager_profession_name(VillagerProfession job);
const char *mob_actor_given_name(const Actor *actor);
const char *mob_actor_family_name(const Actor *actor);
uint32_t mob_actor_family_id(const Actor *actor);
uint32_t mob_actor_settlement_id(const Actor *actor);
void mob_actor_set_settlement_id(MobActor *mob, uint32_t settlement_id);
void mob_actor_roll_traits(MobActor *mob, uint32_t salt);
void mob_actor_set_relation(MobActor *mob, uint32_t other_id, MobRelationKind kind);
MobRelationKind mob_actor_relation_get(const MobActor *mob, uint32_t other_id);
bool mob_actor_are_kin(const MobActor *a, const MobActor *b);
int mob_actor_story_count(const Actor *actor);
const MobStoryEntry *mob_actor_story_get(const Actor *actor, int index);
// Append a remembered tale (dedupes identical text). False if full / invalid.
bool mob_actor_add_story(MobActor *mob, uint32_t about_actor_id, uint32_t about_family_id,
                         int8_t sentiment, const char *text);
// Copy parent tales into the child (diluted sentiment). Returns how many were new.
int mob_actor_inherit_stories(MobActor *child, const MobActor *parent);
// Given + family names, optional job; refreshes base.name / description.
void mob_actor_set_villager_identity(MobActor *mob, const char *given, const char *family,
                                     VillagerProfession job);
// Record parentage and raise mutual affinity.
void mob_actor_link_parent(MobActor *child, MobActor *parent);
bool mob_actor_is_domesticable(const Actor *actor);
BirdKind mob_actor_bird_kind(const Actor *actor);
float mob_actor_bird_weight(const Actor *actor);
float mob_actor_bird_lift(const Actor *actor);
float mob_actor_bird_glide_speed(const Actor *actor);
float mob_actor_bird_bank_rate(const Actor *actor);
void mob_actor_bird_color(const Actor *actor, uint8_t *r, uint8_t *g, uint8_t *b);

// Equipment lives on the MobActor pointed by actor->extra_data. NULL for spirits.
Equipment *mob_actor_equipment(Actor *actor);
const Equipment *mob_actor_equipment_const(const Actor *actor);

// Bind a baked polygon mesh ("goleling", "pigeon"). Clip starts at Flying_Idle.
void mob_actor_bind_mesh(MobActor *mob, const char *mesh_name);
const char *mob_actor_mesh_name(const Actor *actor);
const char *mob_actor_anim_clip(const Actor *actor);
float mob_actor_anim_time(const Actor *actor);
float mob_actor_facing_yaw(const Actor *actor);
float mob_actor_facing_roll(const Actor *actor);
// Drive clips from AI/combat. Safe to call on a dominated body the world step skips.
void mob_actor_tick_animation(MobActor *mob, float dt_seconds);
// Yes on the first line, No on later lines. No-op without a polygon mesh.
void mob_actor_notify_talk(Actor *actor, int line_index);
// Aggro / hurt UI after HP changes. Corpses stay active entities (Death freezes on last frame).
// attacker_id is the damager's actor id, or MOB_THREAT_PLAYER for the spirit.
void mob_actor_after_damage(Actor *actor, uint32_t attacker_id);

// How long the hurt health bar stays fully visible, then fades.
// (Defines live in actor.h next to Actor.hurt_display_ttl.)

// Player spirit pose for mobs that aggro on the spirit (not in runtime_actors).
// body_id is dominated_actor_id when inhabited, else 0. Call each frame before world_step_actors.
void mob_ai_set_player_presence(float x, float y, float z, uint32_t body_id);

// Breeding stubs: blend parent stats with small variation. Compatible types only (same mob_type).
MobActor *mob_actor_create_progeny(const MobActor *parent_a, const MobActor *parent_b,
                                   double x, double y, double z);
// Validates mood/range/cooldown, creates progeny, inserts into the world. No automatic mate AI.
bool mob_actor_try_breed(MobActor *a, MobActor *b, World *world);
// Insert an already-built MobActor into the world's runtime list.
bool mob_actor_spawn_in_world(World *world, MobActor *mob);

// Per-actor reputation (-100..+100). Unknown others are 0 until encountered.
int8_t mob_actor_reputation_get(const MobActor *mob, uint32_t other_id);
void mob_actor_reputation_adjust(MobActor *mob, uint32_t other_id, int delta);
// Seed a first-contact score from type affinity if no entry exists yet.
void mob_actor_reputation_encounter(MobActor *self, const MobActor *other);

// SOLVER specific functions
bool mob_solver_is_at_exit(MobActor* solver, World* world);
void mob_solver_find_next_move(MobActor* solver, World* world);

// Utility functions
bool mob_can_move_to(World* world, int x, int y, int z);
bool mob_cell_is_open(World* world, int x, int y, int z);
// Cylinder occupancy (MOB_ACTOR_RADIUS) — feet-level terrain intersection test for physics/spawn.
bool mob_actor_can_occupy(World *world, double x, double y, double z);
// True when no living actor is within 2*MOB_ACTOR_RADIUS (XY) and MOB_ACTOR_SEPARATION_Z (Z).
bool mob_actor_clear_of_actors(World *world, double x, double y, double z, uint32_t ignore_id);
bool mob_is_at_world_border(World* world, int x, int y, int z);

// Place the home-world golem and bird, plus a polygon-mesh second spawn of each.
// Idempotent: missing mobs are added, already-present ones are left alone.
bool world_spawn_home_mobs(World *world);

// Sparse generic wanderers and birds for wilderness cells.
// Idempotent: skips work when "Wild " fauna are already present.
bool world_spawn_wilderness_mobs(World *world);

// Villagers for stamped settlements with scale >= 3 (male, female, children near the anchor).
// Idempotent: skips when villagers are already present.
bool world_spawn_settlement_villagers(World *world);

#endif // MOB_AI_H
