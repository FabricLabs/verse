#ifndef VERSE_ACTOR_H
#define VERSE_ACTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // For size_t
#include "item.h"

// Highly unoptimized actor structure
typedef struct Actor {
  uint32_t id;               // Unique identifier
  char name[64];             // Actor name
  char description[256];     // Actor description
  double x;                  // Position X
  double y;                  // Position Y
  double z;                  // Position Z
  double velocity_x;         // Velocity X
  double velocity_y;         // Velocity Y
  double velocity_z;         // Velocity Z
  char world_id[64];         // Current world ID
  uint32_t health;           // Current health (max from level + strength)
  // Seconds left to show the hurt health bar after taking damage. Fades in the last second.
  float hurt_display_ttl;
  float stamina;             // Current stamina (max from level + dexterity)
  float mana;                // Current mana (max from level + intelligence)
  uint32_t strength;         // Strength — HP, damage, and jump launch impulse
  uint32_t dexterity;        // Dexterity — armor, damage reduction, turn speed, stamina
  uint32_t intelligence;     // Intelligence — mana pool
  uint32_t wisdom;           // Wisdom — detection range and luck boosts
  uint32_t constitution;     // Constitution — carry weight / toughness checks
  uint32_t charisma;         // Charisma
  uint32_t luck;             // Luck (wisdom adds to effective luck)
  uint32_t turn_speed;       // Facing turn rate (deg/s); derived from dexterity
  uint32_t experience;       // Experience
  uint32_t level;            // Level
  uint32_t attribute_points; // Unspent points from leveling (1 per level gained)
  uint32_t skill_points;     // Unspent skill-tree points (1 every SKILL_POINT_LEVELS levels)
  uint32_t unlocked_skills;  // Bitmask of SkillId values unlocked from the tree
  // Rank per SkillId: 0 = unknown (or default for base), 1 = basic, 2 = expanded, 3 = powerful.
  // Indexed by SkillId; only slots below 32 are used (matches unlocked_skills bits).
  uint8_t skill_ranks[32];
  uint32_t inventory_size;   // Inventory capacity (mirrors inventory.capacity)
  Inventory inventory;       // Bag — available to spirits and mobs
  bool is_active;            // Whether actor is active
  // Status flags
  bool is_flying;            // Ignores gravity and can move vertically
  bool is_controlled;        // Player soul currently inhabits this actor
  // Cold buildup from ice magic (0..255). Reaching 255 applies freeze_ttl.
  uint8_t chill;
  // Seconds remaining while iced; AI zeros velocity until it expires.
  float freeze_ttl;
  // Poison from Shadow Strike: remaining duration and damage-per-second while active.
  float poison_ttl;
  float poison_dps;
  float poison_accum; // fractional HP waiting to apply
  void* extra_data;          // Additional data pointer (MobActor* for mobs)
} Actor;

// How long the hurt health bar stays fully visible, then fades.
#define ACTOR_HURT_DISPLAY_SECONDS 3.5f
#define ACTOR_HURT_FADE_SECONDS 1.0f
// Hide hurt bars beyond this distance from the viewer (camera / player). Soft-fade starts here.
#define ACTOR_HURT_DISPLAY_RANGE 14.0f
#define ACTOR_HURT_DISPLAY_FADE_START 10.0f

// Spendable combat/character attributes (not turn_speed).
typedef enum {
  ACTOR_ATTR_STRENGTH = 0,
  ACTOR_ATTR_DEXTERITY,
  ACTOR_ATTR_INTELLIGENCE,
  ACTOR_ATTR_WISDOM,
  ACTOR_ATTR_CONSTITUTION,
  ACTOR_ATTR_CHARISMA,
  ACTOR_ATTR_LUCK,
  ACTOR_ATTR_COUNT
} ActorAttribute;

#define ACTOR_ATTR_MAX 100u

// A skill-tree point is granted each time the spirit reaches a level divisible by this.
#define ACTOR_SKILL_POINT_LEVELS 4u

// DOTA-style growth: flat per level, plus secondary bonuses from attributes.
// max_hp      = HP_PER_LEVEL * level + HP_PER_STR * strength
// max_stamina = STA_BASE + STA_PER_LEVEL*(level-1) + STA_PER_DEX * dexterity
// max_mana    = MANA_PER_LEVEL * level + MANA_PER_INT * intelligence
#define ACTOR_HP_PER_LEVEL        20u
#define ACTOR_HP_PER_STRENGTH     8u
#define ACTOR_STAMINA_BASE        80.0f
#define ACTOR_STAMINA_PER_LEVEL   5.0f
#define ACTOR_STAMINA_PER_DEX     2.0f
#define ACTOR_MANA_PER_LEVEL      10u
#define ACTOR_MANA_PER_INTELLIGENCE 5u
#define ACTOR_ARMOR_PER_DEX_DIV   1u   /* armor = dexterity (diminishing DR in mitigate) */
#define ACTOR_TURN_BASE_DEG       60u
#define ACTOR_TURN_PER_DEX        18u  /* L1 dex 10 => 240 deg/s */
#define ACTOR_ATTR_POINTS_PER_LEVEL 1u
// Intelligence skill power: INT 10 => 1.0x, INT 20 => 2.0x (clamped).
#define ACTOR_SKILL_POWER_BASE_INT  10u
#define ACTOR_SKILL_POWER_MIN       0.25f
#define ACTOR_SKILL_POWER_MAX       4.0f

// Actor creation and management
Actor* actor_create(const char* name, const char* description, const char* world_id);
void actor_destroy(Actor* actor);

// Actor movement
void actor_set_position(Actor* actor, double x, double y, double z);
void actor_set_velocity(Actor* actor, double vx, double vy, double vz);
void actor_move(Actor* actor, double dx, double dy, double dz);

// Actor stats
void actor_set_health(Actor* actor, uint32_t health);
void actor_set_stamina(Actor* actor, float stamina);
void actor_set_mana(Actor* actor, float mana);
void actor_add_experience(Actor* actor, uint32_t exp);
// Spend one unspent attribute point on attr. Returns false if none left or attr capped.
bool actor_spend_attribute_point(Actor* actor, ActorAttribute attr);
uint32_t *actor_attribute_ptr(Actor* actor, ActorAttribute attr);

// Derived combat/resource stats (DOTA-style: level flat + attribute secondaries).
uint32_t actor_max_health(const Actor *actor);
float actor_max_stamina(const Actor *actor);
float actor_max_mana(const Actor *actor);
uint32_t actor_armor(const Actor *actor);
uint32_t actor_mitigate_damage(const Actor *actor, uint32_t raw_damage);
uint32_t actor_attack_bonus(const Actor *actor);
uint32_t actor_effective_luck(const Actor *actor);
float actor_detection_range(const Actor *actor, float base_range);
uint32_t actor_derived_turn_speed(const Actor *actor);
// Intelligence scales spell/skill magnitude (fireball damage, ice chill, water volume, …).
float actor_skill_power(const Actor *actor);
uint32_t actor_skill_scale_u32(const Actor *actor, uint32_t base);
float actor_skill_scale_f(const Actor *actor, float base);
// Integer potency for projectiles: 100 = baseline (INT 10).
uint16_t actor_skill_potency(const Actor *actor);
// Recompute turn_speed and top up current HP/stamina/mana when maxima rise.
void actor_recalculate_stats(Actor *actor);
// Apply damage after armor mitigation. Returns damage actually dealt.
uint32_t actor_apply_damage(Actor *actor, uint32_t raw_damage);

// Actor validation
bool actor_is_valid(const Actor* actor);

// Actor serialization
bool actor_serialize(const Actor* actor, char* buffer, size_t buffer_size);
bool actor_deserialize(Actor* actor, const char* buffer, size_t buffer_size);

#endif // VERSE_ACTOR_H
