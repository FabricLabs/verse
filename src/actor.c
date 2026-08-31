#include "actor.h"
#include "character.h" // For PlayerId and double_sha256_hash
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// Magic bytes for actor ID (0xc0d3f33d in little endian)
#define FABRIC_MAGIC 0xc0d3f33d
static const uint8_t ACTOR_ID_MAGIC[4] = {0x3d, 0xf3, 0xd3, 0xc0};

// Actor ID preimage structure (similar to PlayerIdPreimage but for actors)
typedef struct {
    uint8_t magic[4];      // Magic bytes (0xc0d3f33d)
    uint8_t version[4];    // Version (little endian)
    uint8_t parent[32];    // Parent hash (32 bytes)
    uint8_t author[32];    // Author hash (32 bytes)
    uint8_t type[4];       // Type (little endian)
    uint8_t size[4];       // Size (little endian)
    uint8_t hash[32];      // Content hash (32 bytes)
    uint8_t signature[64]; // Signature (64 bytes)
} __attribute__((packed)) ActorIdPreimage;

// Actor ID (double SHA256 hash of preimage)
typedef struct {
    uint8_t id[32];        // Double SHA256 hash
    ActorIdPreimage preimage; // Original preimage data
} ActorId;

// Create actor ID preimage
static ActorIdPreimage* actor_id_preimage_create(uint32_t version, const char* parent_hash,
                                               const char* author_hash, uint32_t type,
                                               uint32_t size, const char* content_hash,
                                               const char* signature) {
    ActorIdPreimage* preimage = malloc(sizeof(ActorIdPreimage));
    if (!preimage) return NULL;

    // Set magic bytes
    memcpy(preimage->magic, ACTOR_ID_MAGIC, 4);

    // Set version (little endian)
    preimage->version[0] = version & 0xFF;
    preimage->version[1] = (version >> 8) & 0xFF;
    preimage->version[2] = (version >> 16) & 0xFF;
    preimage->version[3] = (version >> 24) & 0xFF;

    // Set parent hash (32 bytes)
    if (parent_hash && strlen(parent_hash) == 64) {
        for (int i = 0; i < 32; i++) {
            char hex[3] = {parent_hash[i*2], parent_hash[i*2+1], 0};
            preimage->parent[i] = (uint8_t)strtol(hex, NULL, 16);
        }
    } else {
        memset(preimage->parent, 0, 32);
    }

    // Set author hash (32 bytes)
    if (author_hash && strlen(author_hash) == 64) {
        for (int i = 0; i < 32; i++) {
            char hex[3] = {author_hash[i*2], author_hash[i*2+1], 0};
            preimage->author[i] = (uint8_t)strtol(hex, NULL, 16);
        }
    } else {
        memset(preimage->author, 0, 32);
    }

    // Set type (little endian)
    preimage->type[0] = type & 0xFF;
    preimage->type[1] = (type >> 8) & 0xFF;
    preimage->type[2] = (type >> 16) & 0xFF;
    preimage->type[3] = (type >> 24) & 0xFF;

    // Set size (little endian)
    preimage->size[0] = size & 0xFF;
    preimage->size[1] = (size >> 8) & 0xFF;
    preimage->size[2] = (size >> 16) & 0xFF;
    preimage->size[3] = (size >> 24) & 0xFF;

    // Set content hash (32 bytes)
    if (content_hash && strlen(content_hash) == 64) {
        for (int i = 0; i < 32; i++) {
            char hex[3] = {content_hash[i*2], content_hash[i*2+1], 0};
            uint8_t byte = (uint8_t)strtol(hex, NULL, 16);
            preimage->hash[i] = byte;
        }
    } else {
        memset(preimage->hash, 0, 32);
    }

    // Set signature (64 bytes)
    if (signature && strlen(signature) == 128) {
        for (int i = 0; i < 64; i++) {
            char hex[3] = {signature[i*2], signature[i*2+1], 0};
            preimage->signature[i] = (uint8_t)strtol(hex, NULL, 16);
        }
    } else {
        memset(preimage->signature, 0, 64);
    }

    return preimage;
}

// Destroy actor ID preimage
static void actor_id_preimage_destroy(ActorIdPreimage* preimage) {
    if (preimage) {
        free(preimage);
    }
}

// Create actor ID
static ActorId* actor_id_create(const char* actor_name, const char* world_id, uint32_t version) {
    if (!actor_name || !world_id) return NULL;

    // Create preimage
    ActorIdPreimage* preimage = actor_id_preimage_create(
        version,                    // version
        NULL,                       // parent_hash (no parent for actors)
        NULL,                       // author_hash (no author for actors)
        0x00000001,                 // type (actor type)
        strlen(actor_name),         // size
        NULL,                       // content_hash (will be computed)
        NULL                        // signature (no signature for actors)
    );

    if (!preimage) return NULL;

    // Create content hash from actor name and world ID
    char content_data[256];
    snprintf(content_data, sizeof(content_data), "%s:%s", actor_name, world_id);

    // Calculate content hash
    uint8_t content_hash[32];
    double_sha256_hash((const uint8_t*)content_data, strlen(content_data), content_hash);

    // Update preimage with content hash
    memcpy(preimage->hash, content_hash, 32);

    // Create actor ID
    ActorId* actor_id = malloc(sizeof(ActorId));
    if (!actor_id) {
        actor_id_preimage_destroy(preimage);
        return NULL;
    }

    // Calculate double SHA256 hash of preimage
    double_sha256_hash((const uint8_t*)preimage, sizeof(ActorIdPreimage), actor_id->id);

    // Store preimage
    memcpy(&actor_id->preimage, preimage, sizeof(ActorIdPreimage));

    // Clean up
    actor_id_preimage_destroy(preimage);

    return actor_id;
}

// Destroy actor ID
static void actor_id_destroy(ActorId* actor_id) {
    if (actor_id) {
        free(actor_id);
    }
}

// Convert actor ID to hex string
static char* actor_id_to_hex(const ActorId* actor_id) {
    if (!actor_id) return NULL;

    char* hex_string = malloc(65); // 32 bytes * 2 + null terminator
    if (!hex_string) return NULL;

    for (int i = 0; i < 32; i++) {
        sprintf(hex_string + (i * 2), "%02x", actor_id->id[i]);
    }
    hex_string[64] = '\0';

    return hex_string;
}

// Actor creation and management functions
Actor* actor_create(const char* name, const char* description, const char* world_id) {
    Actor* actor = malloc(sizeof(Actor));
    if (!actor) return NULL;

    // Initialize with zeros
    memset(actor, 0, sizeof(Actor));

    // Set basic properties
    if (name) {
        strncpy(actor->name, name, sizeof(actor->name) - 1);
        actor->name[sizeof(actor->name) - 1] = '\0';
    }

    if (description) {
        strncpy(actor->description, description, sizeof(actor->description) - 1);
        actor->description[sizeof(actor->description) - 1] = '\0';
    }

    if (world_id) {
        strncpy(actor->world_id, world_id, sizeof(actor->world_id) - 1);
        actor->world_id[sizeof(actor->world_id) - 1] = '\0';
    }

    // Set default stats (DOTA-style: resources derived from level + attributes).
    actor->strength = 10;
    actor->dexterity = 10;
    actor->intelligence = 10;
    actor->wisdom = 10;
    actor->constitution = 10;
    actor->charisma = 10;
    actor->luck = 10;
    actor->experience = 0;
    actor->level = 1;
    actor->attribute_points = 0;
    actor->skill_points = 0;
    actor->unlocked_skills = 0;
    memset(actor->skill_ranks, 0, sizeof(actor->skill_ranks));
    actor->chill = 0;
    actor->freeze_ttl = 0.0f;
    actor->poison_ttl = 0.0f;
    actor->poison_dps = 0.0f;
    actor->poison_accum = 0.0f;
    actor->inventory_size = INVENTORY_DEFAULT_SLOTS;
    inventory_init(&actor->inventory, INVENTORY_DEFAULT_SLOTS);
    actor->is_active = true;
    actor->is_flying = true; // default: flying enabled
    actor_recalculate_stats(actor);
    actor->health = actor_max_health(actor);
    actor->stamina = actor_max_stamina(actor);
    actor->mana = actor_max_mana(actor);
    // Generate actor ID
    ActorId* actor_id = actor_id_create(name, world_id, 1);
    if (actor_id) {
        // Convert ID to uint32_t for storage (using first 4 bytes)
        actor->id = (actor_id->id[0] << 24) | (actor_id->id[1] << 16) |
                   (actor_id->id[2] << 8) | actor_id->id[3];
        actor_id_destroy(actor_id);
    } else {
        // Fallback: use hash of name
        actor->id = (uint32_t)strlen(name);
    }

    return actor;
}

void actor_destroy(Actor* actor) {
    if (actor) {
        // Free any extra data
        if (actor->extra_data) {
            free(actor->extra_data);
        }
        free(actor);
    }
}

// Actor movement functions
void actor_set_position(Actor* actor, double x, double y, double z) {
    if (actor) {
        actor->x = x;
        actor->y = y;
        actor->z = z;
    }
}

void actor_set_velocity(Actor* actor, double vx, double vy, double vz) {
    if (actor) {
        actor->velocity_x = vx;
        actor->velocity_y = vy;
        actor->velocity_z = vz;
    }
}

void actor_move(Actor* actor, double dx, double dy, double dz) {
    if (actor) {
        actor->x += dx;
        actor->y += dy;
        actor->z += dz;
    }
}

// Actor stats functions
void actor_set_health(Actor* actor, uint32_t health) {
    if (actor) {
        actor->health = health;
    }
}

void actor_set_stamina(Actor* actor, float stamina) {
    if (actor) {
        float max = actor_max_stamina(actor);
        if (stamina < 0.0f)
            stamina = 0.0f;
        if (stamina > max)
            stamina = max;
        actor->stamina = stamina;
    }
}

void actor_set_mana(Actor* actor, float mana) {
    if (actor) {
        float max = actor_max_mana(actor);
        if (mana < 0.0f)
            mana = 0.0f;
        if (mana > max)
            mana = max;
        actor->mana = mana;
    }
}

static uint32_t actor_effective_level(const Actor *actor)
{
    if (!actor || actor->level == 0)
        return 1u;
    return actor->level;
}

uint32_t actor_max_health(const Actor *actor)
{
    if (!actor)
        return ACTOR_HP_PER_LEVEL + ACTOR_HP_PER_STRENGTH * 10u;
    return ACTOR_HP_PER_LEVEL * actor_effective_level(actor) +
           ACTOR_HP_PER_STRENGTH * actor->strength;
}

float actor_max_stamina(const Actor *actor)
{
    if (!actor)
        return ACTOR_STAMINA_BASE + ACTOR_STAMINA_PER_DEX * 10.0f;
    const uint32_t lvl = actor_effective_level(actor);
    return ACTOR_STAMINA_BASE +
           ACTOR_STAMINA_PER_LEVEL * (float)(lvl > 0 ? lvl - 1u : 0u) +
           ACTOR_STAMINA_PER_DEX * (float)actor->dexterity;
}

float actor_max_mana(const Actor *actor)
{
    if (!actor)
        return (float)(ACTOR_MANA_PER_LEVEL + ACTOR_MANA_PER_INTELLIGENCE * 10u);
    return (float)(ACTOR_MANA_PER_LEVEL * actor_effective_level(actor) +
                   ACTOR_MANA_PER_INTELLIGENCE * actor->intelligence);
}

uint32_t actor_armor(const Actor *actor)
{
    if (!actor)
        return 0;
    return actor->dexterity / ACTOR_ARMOR_PER_DEX_DIV;
}

uint32_t actor_mitigate_damage(const Actor *actor, uint32_t raw_damage)
{
    if (raw_damage == 0)
        return 0;
    const uint32_t armor = actor_armor(actor);
    // Diminishing returns: raw * 100 / (100 + armor), at least 1.
    const uint32_t mitigated = (raw_damage * 100u) / (100u + armor);
    return mitigated > 0 ? mitigated : 1u;
}

uint32_t actor_attack_bonus(const Actor *actor)
{
    if (!actor)
        return 0;
    return actor->strength / 4u;
}

uint32_t actor_effective_luck(const Actor *actor)
{
    if (!actor)
        return 0;
    return actor->luck + actor->wisdom / 2u;
}

float actor_detection_range(const Actor *actor, float base_range)
{
    if (!actor || base_range <= 0.0f)
        return base_range;
    // Wisdom stretches awareness: +1% range per wisdom point (dex-10 baseline ignored).
    return base_range * (1.0f + (float)actor->wisdom * 0.01f);
}

uint32_t actor_derived_turn_speed(const Actor *actor)
{
    if (!actor)
        return ACTOR_TURN_BASE_DEG + ACTOR_TURN_PER_DEX * 10u;
    return ACTOR_TURN_BASE_DEG + ACTOR_TURN_PER_DEX * actor->dexterity;
}

float actor_skill_power(const Actor *actor)
{
    const uint32_t intel = actor ? actor->intelligence : ACTOR_SKILL_POWER_BASE_INT;
    float power = (float)intel / (float)ACTOR_SKILL_POWER_BASE_INT;
    if (power < ACTOR_SKILL_POWER_MIN)
        power = ACTOR_SKILL_POWER_MIN;
    if (power > ACTOR_SKILL_POWER_MAX)
        power = ACTOR_SKILL_POWER_MAX;
    return power;
}

uint32_t actor_skill_scale_u32(const Actor *actor, uint32_t base)
{
    if (base == 0)
        return 0;
    const float scaled = (float)base * actor_skill_power(actor) + 0.5f;
    if (scaled < 1.0f)
        return 1u;
    if (scaled > (float)UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)scaled;
}

float actor_skill_scale_f(const Actor *actor, float base)
{
    return base * actor_skill_power(actor);
}

uint16_t actor_skill_potency(const Actor *actor)
{
    const float p = actor_skill_power(actor) * 100.0f + 0.5f;
    if (p < 1.0f)
        return 1u;
    if (p > 65535.0f)
        return 65535u;
    return (uint16_t)p;
}

void actor_recalculate_stats(Actor *actor)
{
    if (!actor)
        return;

    const uint32_t max_hp = actor_max_health(actor);
    const float max_sta = actor_max_stamina(actor);
    const float max_mp = actor_max_mana(actor);

    actor->turn_speed = actor_derived_turn_speed(actor);

    // Clamp if over the new ceiling (e.g. after an attr reset).
    if (actor->health > max_hp)
        actor->health = max_hp;
    if (actor->stamina > max_sta)
        actor->stamina = max_sta;
    if (actor->mana > max_mp)
        actor->mana = max_mp;
}

uint32_t actor_apply_damage(Actor *actor, uint32_t raw_damage)
{
    if (!actor || raw_damage == 0 || actor->health == 0)
        return 0;
    const uint32_t dealt = actor_mitigate_damage(actor, raw_damage);
    if (dealt == 0)
        return 0;
    if (actor->health > dealt)
        actor->health -= dealt;
    else
        actor->health = 0;
    // Arm the hurt chrome whenever HP actually drops (FP + iso health bars).
    actor->hurt_display_ttl = ACTOR_HURT_DISPLAY_SECONDS;
    return dealt;
}

void actor_add_experience(Actor* actor, uint32_t exp) {
    if (!actor || exp == 0)
        return;

    // Derive level from XP so uninitialized level-0 mobs do not get a free point.
    const uint32_t old_level = (actor->experience / 100u) + 1u;
    const uint32_t old_max_hp = actor_max_health(actor);
    const float old_max_sta = actor_max_stamina(actor);
    const float old_max_mp = actor_max_mana(actor);

    actor->experience += exp;
    actor->level = (actor->experience / 100u) + 1u;
    if (actor->level > old_level)
    {
        const uint32_t gained = actor->level - old_level;
        actor->attribute_points += gained * ACTOR_ATTR_POINTS_PER_LEVEL;
        // Skill points land on every multiple of ACTOR_SKILL_POINT_LEVELS (4, 8, 12, …).
        for (uint32_t L = old_level + 1u; L <= actor->level; L++)
        {
            if ((L % ACTOR_SKILL_POINT_LEVELS) == 0u)
                actor->skill_points++;
        }

        // Flat per-level resource gains (also reflected in the derived maxima).
        actor_recalculate_stats(actor);
        const uint32_t new_max_hp = actor_max_health(actor);
        const float new_max_sta = actor_max_stamina(actor);
        const float new_max_mp = actor_max_mana(actor);
        actor->health += new_max_hp - old_max_hp;
        actor->stamina += new_max_sta - old_max_sta;
        actor->mana += new_max_mp - old_max_mp;
        if (actor->health > new_max_hp)
            actor->health = new_max_hp;
        if (actor->stamina > new_max_sta)
            actor->stamina = new_max_sta;
        if (actor->mana > new_max_mp)
            actor->mana = new_max_mp;
    }
}

uint32_t *actor_attribute_ptr(Actor* actor, ActorAttribute attr) {
    if (!actor)
        return NULL;
    switch (attr) {
    case ACTOR_ATTR_STRENGTH:     return &actor->strength;
    case ACTOR_ATTR_DEXTERITY:    return &actor->dexterity;
    case ACTOR_ATTR_INTELLIGENCE: return &actor->intelligence;
    case ACTOR_ATTR_WISDOM:       return &actor->wisdom;
    case ACTOR_ATTR_CONSTITUTION: return &actor->constitution;
    case ACTOR_ATTR_CHARISMA:     return &actor->charisma;
    case ACTOR_ATTR_LUCK:         return &actor->luck;
    default:                      return NULL;
    }
}

bool actor_spend_attribute_point(Actor* actor, ActorAttribute attr) {
    if (!actor || actor->attribute_points == 0)
        return false;

    uint32_t *stat = actor_attribute_ptr(actor, attr);
    if (!stat || *stat >= ACTOR_ATTR_MAX)
        return false;

    const uint32_t old_max_hp = actor_max_health(actor);
    const float old_max_sta = actor_max_stamina(actor);
    const float old_max_mp = actor_max_mana(actor);

    (*stat)++;
    actor->attribute_points--;

    actor_recalculate_stats(actor);

    // Grant the newly unlocked pool from STR/DEX/INT like DOTA attribute growth.
    const uint32_t new_max_hp = actor_max_health(actor);
    const float new_max_sta = actor_max_stamina(actor);
    const float new_max_mp = actor_max_mana(actor);
    if (new_max_hp > old_max_hp)
        actor->health += new_max_hp - old_max_hp;
    if (new_max_sta > old_max_sta)
        actor->stamina += new_max_sta - old_max_sta;
    if (new_max_mp > old_max_mp)
        actor->mana += new_max_mp - old_max_mp;

    return true;
}

// Actor validation
bool actor_is_valid(const Actor* actor) {
    return actor != NULL &&
           actor->name[0] != '\0' &&
           actor->world_id[0] != '\0' &&
           actor->health > 0;
}

// Actor serialization (basic)
bool actor_serialize(const Actor* actor, char* buffer, size_t buffer_size) {
    if (!actor || !buffer || buffer_size < sizeof(Actor)) {
        return false;
    }

    memcpy(buffer, actor, sizeof(Actor));
    return true;
}

bool actor_deserialize(Actor* actor, const char* buffer, size_t buffer_size) {
    if (!actor || !buffer || buffer_size < sizeof(Actor)) {
        return false;
    }

    memcpy(actor, buffer, sizeof(Actor));
    return true;
}
