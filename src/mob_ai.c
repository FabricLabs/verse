#include "mob_ai.h"
#include "mob_loot.h"
#include "voxel.h"
#include "player_controls_types.h"
#include "poly_mesh.h"
#include "universe.h"
#include "universe_biome.h"
#include "settlement.h"
#include "household.h"
#include "chronicle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MOB_MELEE_RANGE 1.75f
#define MOB_MELEE_COOLDOWN 2.4f
#define MOB_AGGRO_DAMAGE_THRESHOLD 10u
#define MOB_DAMAGE_ACCUM_WINDOW 4.0f
#define MOB_AGGRO_TTL 12.0f
#define MOB_BREED_RANGE 2.5f
#define MOB_BREED_COOLDOWN 30.0f
#define MOB_MOOD_DRIFT_PERIOD 3.5f
#define MOB_REPUTATION_SCAN_PERIOD 1.25f
#define MOB_REPUTATION_FRIEND_MIN 10
#define MOB_REPUTATION_FOE_MAX (-10)
// Predators close on disliked prey; timid fauna bolt from disliked threats.
#define MOB_HUNT_RANGE 7.5f
#define MOB_THREAT_SIGHT 8.5f
#define MOB_DOMESTICATE_PERIOD 2.2f
#define MOB_DOMESTICATE_GAIN 5

static uint32_t s_next_mob_id = 1;

// Refreshed each frame from GameState so spirit-aggro'd mobs can chase the player.
static float s_player_x, s_player_y, s_player_z;
static uint32_t s_player_body_id;
static bool s_player_presence_valid;

void mob_ai_set_player_presence(float x, float y, float z, uint32_t body_id)
{
    s_player_x = x;
    s_player_y = y;
    s_player_z = z;
    s_player_body_id = body_id;
    s_player_presence_valid = true;
}

static const BirdStats s_bird_stats[BIRD_KIND_COUNT] = {
    {"Sparrow", "A fidgety brown bird",
     0.78f, 3.2f, 4.2f, 5.0f, 3.5f, 2.1f, 2.6f, 1.8f, PLAYER_SPARROW_WEIGHT,
     168, 132, 88},
    {"Crow", "A watchful black bird",
     0.88f, 4.4f, 3.6f, 7.0f, 6.0f, 1.7f, 1.9f, 1.15f, PLAYER_CROW_WEIGHT,
     28, 28, 32},
    {"Gull", "A long-winged white bird",
     0.94f, 5.6f, 3.1f, 9.0f, 9.0f, 1.35f, 1.4f, 0.72f, PLAYER_GULL_WEIGHT,
     220, 220, 210},
};

static uint32_t mob_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// Defined later; create_villager needs them at compile time.
void mob_actor_set_villager_identity(MobActor *mob, const char *given, const char *family,
                                     VillagerProfession job);
void mob_actor_reputation_adjust(MobActor *mob, uint32_t other_id, int delta);
int mob_actor_inherit_stories(MobActor *child, const MobActor *parent);

MobActor* mob_actor_create(const char* name, MobType type, double x, double y, double z) {
    MobActor* mob = (MobActor*)calloc(1, sizeof(MobActor));
    if (!mob) return NULL;

    mob->base.id = s_next_mob_id++;
    if (mob->base.id == 0)
        mob->base.id = s_next_mob_id++;
    if (name)
        strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
    mob->base.x = x;
    mob->base.y = y;
    mob->base.z = z;
    mob->base.strength = 10;
    mob->base.dexterity = 3;
    mob->base.intelligence = 4;
    mob->base.wisdom = 4;
    mob->base.constitution = 12;
    mob->base.charisma = 5;
    mob->base.luck = 5;
    mob->base.experience = 0;
    mob->base.level = 1;
    mob->base.is_active = true;
    mob->base.is_flying = false;
    mob->base.is_controlled = false;
    mob->base.inventory_size = INVENTORY_DEFAULT_SLOTS;
    inventory_init(&mob->base.inventory, INVENTORY_DEFAULT_SLOTS);
    strncpy(mob->base.world_id, "home", sizeof(mob->base.world_id) - 1);

    mob->mob_type = type;
    equipment_init(&mob->equipment);
    mob->ai_state.has_target = false;
    mob->ai_state.last_x = (int)x;
    mob->ai_state.last_y = (int)y;
    mob->ai_state.last_z = (int)z;
    mob->ai_state.retarget_in = 0.0f;
    mob->last_health = mob->base.health;
    mob->mood = MOB_MOOD_CALM;
    mob->goal = MOB_GOAL_WANDER;
    mob->aggro_target_id = 0;
    mob->aggro_ttl = 0.0f;
    mob->damage_accum = 0;
    mob->damage_accum_ttl = 0.0f;
    mob->mood_timer = MOB_MOOD_DRIFT_PERIOD * 0.5f +
                      (float)(mob->base.id % 7) * 0.2f;
    mob->breed_cooldown = 0.0f;
    mob->reputation_count = 0;
    mob->reputation_scan_in = 0.3f + (float)(mob->base.id % 5) * 0.15f;

    switch (type) {
        case MOB_TYPE_SOLVER:
            strncpy(mob->base.description, "A maze-solving entity seeking the exit",
                    sizeof(mob->base.description) - 1);
            break;
        case MOB_TYPE_WANDERER:
            strncpy(mob->base.description, "A wandering entity",
                    sizeof(mob->base.description) - 1);
            break;
        case MOB_TYPE_BIRD:
            strncpy(mob->base.description, "A bird",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 2;
            mob->base.dexterity = 8;
            mob->base.wisdom = 6;
            break;
        case MOB_TYPE_SHEEP:
            strncpy(mob->base.description, "A woolly grazing animal",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 3;
            mob->base.dexterity = 2;
            mob->base.constitution = 14;
            mob->base.wisdom = 3;
            mob->mood = MOB_MOOD_CALM;
            break;
        case MOB_TYPE_CHICKEN:
            strncpy(mob->base.description, "A skittish farm bird",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 1;
            mob->base.dexterity = 6;
            mob->base.constitution = 6;
            mob->base.wisdom = 4;
            mob->mood = MOB_MOOD_CURIOUS;
            break;
        case MOB_TYPE_BAT:
            strncpy(mob->base.description, "A fluttering cave flier",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 1;
            mob->base.dexterity = 7;
            mob->base.wisdom = 5;
            mob->bird_kind = BIRD_KIND_SPARROW; // flight numbers via sparrow-like profile
            break;
        case MOB_TYPE_DEER:
            strncpy(mob->base.description, "A wary browsing deer",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 4;
            mob->base.dexterity = 7;
            mob->base.constitution = 11;
            mob->base.wisdom = 8;
            break;
        case MOB_TYPE_LIZARD:
            strncpy(mob->base.description, "A sun-warmed monitor lizard",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 5;
            mob->base.dexterity = 5;
            mob->base.constitution = 10;
            mob->base.wisdom = 5;
            mob->mood = MOB_MOOD_CURIOUS;
            break;
        case MOB_TYPE_SPIDER:
            strncpy(mob->base.description, "A skittering hunter",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 6;
            mob->base.dexterity = 9;
            mob->base.constitution = 8;
            mob->base.wisdom = 4;
            mob->mood = MOB_MOOD_CURIOUS;
            break;
        case MOB_TYPE_SLIME:
            strncpy(mob->base.description, "A slow gelatinous blob",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 4;
            mob->base.dexterity = 1;
            mob->base.constitution = 16;
            mob->base.wisdom = 2;
            break;
        case MOB_TYPE_VILLAGER:
            strncpy(mob->base.description, "A settlement resident",
                    sizeof(mob->base.description) - 1);
            mob->base.strength = 6;
            mob->base.dexterity = 4;
            mob->base.intelligence = 8;
            mob->base.constitution = 12;
            mob->base.wisdom = 7;
            mob->villager_kind = VILLAGER_MALE;
            mob->domesticate_timer = 0.8f + (float)(mob->base.id % 5) * 0.2f;
            break;
        default:
            strncpy(mob->base.description, "A mysterious entity",
                    sizeof(mob->base.description) - 1);
            break;
    }

    actor_recalculate_stats(&mob->base);
    mob->base.health = actor_max_health(&mob->base);
    mob->base.stamina = actor_max_stamina(&mob->base);
    mob->base.mana = actor_max_mana(&mob->base);
    mob->last_health = mob->base.health;

    mob_actor_apply_loadout(mob);
    return mob;
}

MobActor *mob_actor_create_sheep(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Sheep", MOB_TYPE_SHEEP, x, y, z);
    if (mob)
        mob_actor_bind_mesh(mob, "sheep");
    return mob;
}

MobActor *mob_actor_create_chicken(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Chicken", MOB_TYPE_CHICKEN, x, y, z);
    if (mob)
        mob_actor_bind_mesh(mob, "chick");
    return mob;
}

// Cube Pets livestock / critters reuse sheep/deer AI so tend-livestock and flee work.
MobActor *mob_actor_create_cow(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Cow", MOB_TYPE_SHEEP, x, y, z);
    if (mob)
    {
        mob->base.strength = 9;
        mob->base.constitution = 16;
        actor_recalculate_stats(&mob->base);
        mob->base.health = actor_max_health(&mob->base);
        mob->base.stamina = actor_max_stamina(&mob->base);
        mob->last_health = mob->base.health;
        mob_actor_bind_mesh(mob, "cow");
    }
    return mob;
}

MobActor *mob_actor_create_pig(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Pig", MOB_TYPE_SHEEP, x, y, z);
    if (mob)
    {
        mob->base.strength = 4;
        mob->base.constitution = 12;
        actor_recalculate_stats(&mob->base);
        mob->base.health = actor_max_health(&mob->base);
        mob->base.stamina = actor_max_stamina(&mob->base);
        mob->last_health = mob->base.health;
        // Hog mesh for a slightly tougher-looking pig; pig.vmesh stays available.
        mob_actor_bind_mesh(mob, (mob->base.id & 1u) ? "hog" : "pig");
    }
    return mob;
}

MobActor *mob_actor_create_rabbit(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Rabbit", MOB_TYPE_DEER, x, y, z);
    if (mob)
    {
        mob->base.strength = 1;
        mob->base.dexterity = 9;
        mob->base.constitution = 6;
        actor_recalculate_stats(&mob->base);
        mob->base.health = actor_max_health(&mob->base);
        mob->base.stamina = actor_max_stamina(&mob->base);
        mob->last_health = mob->base.health;
        mob_actor_bind_mesh(mob, "bunny");
    }
    return mob;
}

MobActor *mob_actor_create_dog(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Dog", MOB_TYPE_WANDERER, x, y, z);
    if (mob)
    {
        mob->base.strength = 4;
        mob->base.dexterity = 6;
        mob->base.constitution = 10;
        actor_recalculate_stats(&mob->base);
        mob->base.health = actor_max_health(&mob->base);
        mob->base.stamina = actor_max_stamina(&mob->base);
        mob->last_health = mob->base.health;
        mob_actor_bind_mesh(mob, "dog");
    }
    return mob;
}

MobActor *mob_actor_create_cat(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Cat", MOB_TYPE_WANDERER, x, y, z);
    if (mob)
    {
        mob->base.strength = 2;
        mob->base.dexterity = 8;
        mob->base.constitution = 7;
        actor_recalculate_stats(&mob->base);
        mob->base.health = actor_max_health(&mob->base);
        mob->base.stamina = actor_max_stamina(&mob->base);
        mob->last_health = mob->base.health;
        mob_actor_bind_mesh(mob, "cat");
    }
    return mob;
}

MobActor *mob_actor_create_bat(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Bat", MOB_TYPE_BAT, x, y, z);
    if (mob)
    {
        mob->ai_state.bird_phase = BIRD_PHASE_PERCH;
        mob->ai_state.perch_z = (float)z;
        mob->ai_state.bird_timer = 1.0f + (float)(mob->base.id % 5) * 0.4f;
        mob_actor_bind_mesh(mob, "bat");
    }
    return mob;
}

MobActor *mob_actor_create_deer(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Deer", MOB_TYPE_DEER, x, y, z);
    if (mob)
        // Tall browser — Cube Pet giraffe stands in until a dedicated deer mesh lands.
        mob_actor_bind_mesh(mob, "giraffe");
    return mob;
}

MobActor *mob_actor_create_elephant(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Elephant", MOB_TYPE_DEER, x, y, z);
    if (mob)
    {
        mob->base.strength = 15;
        mob->base.dexterity = 2;
        mob->base.constitution = 22;
        actor_recalculate_stats(&mob->base);
        mob->base.health = actor_max_health(&mob->base);
        mob->base.stamina = actor_max_stamina(&mob->base);
        mob->last_health = mob->base.health;
        mob_actor_bind_mesh(mob, "elephant");
    }
    return mob;
}

MobActor *mob_actor_create_lizard(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Lizard", MOB_TYPE_LIZARD, x, y, z);
    if (mob)
        mob_actor_bind_mesh(mob, "monkey");
    return mob;
}

MobActor *mob_actor_create_spider(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Spider", MOB_TYPE_SPIDER, x, y, z);
    if (mob)
        // Aggressive ground predator stand-in from Cube Pets.
        mob_actor_bind_mesh(mob, (mob->base.id & 1u) ? "lion" : "tiger");
    return mob;
}

MobActor *mob_actor_create_slime(double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Slime", MOB_TYPE_SLIME, x, y, z);
    if (mob)
        mob_actor_bind_mesh(mob, "caterpillar");
    return mob;
}

MobActor *mob_actor_create_villager(VillagerKind kind, double x, double y, double z)
{
    MobActor *mob = mob_actor_create("Villager", MOB_TYPE_VILLAGER, x, y, z);
    if (!mob)
        return NULL;
    mob->villager_kind = kind;
    mob->profession = (kind == VILLAGER_CHILD) ? VILLAGER_JOB_APPRENTICE : VILLAGER_JOB_NONE;
    // Gobkit minions: a/b adults, c children, d burly guards / elders.
    {
        static const char *male_meshes[] = {"minion-a01", "minion-a02", "minion-d01", "minion-d02"};
        static const char *female_meshes[] = {"minion-b01", "minion-b02", "minion-a01", "minion-a02"};
        static const char *child_meshes[] = {"minion-c01", "minion-c02"};
        const char *mesh = male_meshes[mob->base.id % 4];
        if (kind == VILLAGER_FEMALE)
            mesh = female_meshes[(mob->base.id >> 2) % 4];
        else if (kind == VILLAGER_CHILD)
            mesh = child_meshes[mob->base.id % 2];
        mob_actor_bind_mesh(mob, mesh);
    }
    mob->family_id = 0;
    mob->settlement_id = 0;
    mob->parent_count = 0;
    mob->story_count = 0;
    mob->relation_count = 0;
    mob->unique_id = 0;
    mob->trait_bravery = 0;
    mob->trait_greed = 0;
    mob->trait_piety = 0;
    mob->trait_curiosity = 0;
    mob->trait_loyalty = 0;
    mob->trait_wrath = 0;
    mob->given_name[0] = '\0';
    mob->family_name[0] = '\0';
    mob_actor_roll_traits(mob, mob->base.id);
    if (kind == VILLAGER_CHILD)
    {
        mob->base.strength = 2;
        mob->base.dexterity = 5;
        mob->base.constitution = 8;
        mob->base.wisdom = 4;
    }
    else if (kind == VILLAGER_FEMALE)
    {
        mob->base.strength = 5;
        mob->base.dexterity = 5;
        mob->base.constitution = 11;
        mob->base.wisdom = 8;
    }
    else
    {
        mob->base.strength = 7;
        mob->base.dexterity = 4;
        mob->base.constitution = 13;
        mob->base.wisdom = 7;
    }
    actor_recalculate_stats(&mob->base);
    mob->base.health = actor_max_health(&mob->base);
    mob->base.stamina = actor_max_stamina(&mob->base);
    mob->base.mana = actor_max_mana(&mob->base);
    mob->last_health = mob->base.health;
    mob->domesticate_timer = 0.5f + (float)(mob->base.id % 7) * 0.15f;
    // Placeholder identity until spawn / caller assigns a household name.
    {
        static const char *m_names[] = {"Alden", "Bram", "Cedric", "Doran", "Edric"};
        static const char *f_names[] = {"Astrid", "Brynn", "Cora", "Dana", "Elsa"};
        static const char *c_names[] = {"Pip", "Tess", "Wren", "Ned", "Lia"};
        static const char *surnames[] = {"Reed", "Ashford", "Thorn", "Vale", "Marsh"};
        uint32_t h = mob_hash_u32(mob->base.id * 2654435761u);
        const char *given = m_names[h % 5];
        if (kind == VILLAGER_FEMALE)
            given = f_names[(h >> 3) % 5];
        else if (kind == VILLAGER_CHILD)
            given = c_names[(h >> 5) % 5];
        const char *family = surnames[(h >> 8) % 5];
        VillagerProfession job = VILLAGER_JOB_FARMER;
        if (kind == VILLAGER_CHILD)
            job = VILLAGER_JOB_APPRENTICE;
        else
            job = (VillagerProfession)(VILLAGER_JOB_FARMER + (h % 7));
        if (job >= VILLAGER_JOB_APPRENTICE)
            job = VILLAGER_JOB_FARMER;
        mob_actor_set_villager_identity(mob, given, family, job);
    }
    return mob;
}

const BirdStats *mob_bird_stats(BirdKind kind)
{
    if (kind < 0 || kind >= BIRD_KIND_COUNT)
        kind = BIRD_KIND_CROW;
    return &s_bird_stats[kind];
}

MobActor *mob_actor_create_bird(BirdKind kind, double x, double y, double z)
{
    const BirdStats *stats = mob_bird_stats(kind);
    MobActor *mob = mob_actor_create(stats->name, MOB_TYPE_BIRD, x, y, z);
    if (!mob)
        return NULL;
    mob->bird_kind = kind;
    strncpy(mob->base.description, stats->description, sizeof(mob->base.description) - 1);
    mob->base.strength = 1u + (uint32_t)kind * 2u;
    mob->base.dexterity = 6u + (uint32_t)kind;
    mob->base.constitution = 6 + (uint32_t)kind * 2;
    mob->base.wisdom = 5u + (uint32_t)kind;
    actor_recalculate_stats(&mob->base);
    mob->base.health = actor_max_health(&mob->base);
    mob->base.stamina = actor_max_stamina(&mob->base);
    mob->base.mana = actor_max_mana(&mob->base);
    mob->last_health = mob->base.health;
    mob->ai_state.bird_phase = BIRD_PHASE_PERCH;
    mob->ai_state.bird_timer = 0.8f + (float)(mob->base.id % 5) * 0.35f;
    mob->ai_state.perch_z = (float)z;
    // Crow keeps the Quaternius pigeon; sparrows/gulls use Cube Pet parrot.
    if (kind == BIRD_KIND_CROW)
        mob_actor_bind_mesh(mob, "pigeon");
    else
        mob_actor_bind_mesh(mob, "parrot");
    return mob;
}

static const MobActor *mob_from_actor(const Actor *actor)
{
    if (!actor || !actor->extra_data)
        return NULL;
    return (const MobActor *)actor->extra_data;
}

static MobActor *mob_from_actor_mut(Actor *actor)
{
    if (!actor || !actor->extra_data)
        return NULL;
    return (MobActor *)actor->extra_data;
}

Equipment *mob_actor_equipment(Actor *actor)
{
    MobActor *mob = mob_from_actor_mut(actor);
    return mob ? &mob->equipment : NULL;
}

const Equipment *mob_actor_equipment_const(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob ? &mob->equipment : NULL;
}

bool mob_actor_is_bird(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && mob->mob_type == MOB_TYPE_BIRD;
}

bool mob_actor_is_bat(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && mob->mob_type == MOB_TYPE_BAT;
}

bool mob_actor_is_livestock(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && (mob->mob_type == MOB_TYPE_SHEEP || mob->mob_type == MOB_TYPE_CHICKEN ||
                   mob->mob_type == MOB_TYPE_DEER);
}

bool mob_actor_is_deer(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && mob->mob_type == MOB_TYPE_DEER;
}

bool mob_actor_is_lizard(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && mob->mob_type == MOB_TYPE_LIZARD;
}

bool mob_actor_is_spider(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && mob->mob_type == MOB_TYPE_SPIDER;
}

bool mob_actor_is_slime(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && mob->mob_type == MOB_TYPE_SLIME;
}

bool mob_actor_is_villager(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && mob->mob_type == MOB_TYPE_VILLAGER;
}

VillagerKind mob_actor_villager_kind(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    if (!mob || mob->mob_type != MOB_TYPE_VILLAGER)
        return VILLAGER_MALE;
    return mob->villager_kind;
}

const char *villager_profession_name(VillagerProfession job)
{
    switch (job)
    {
    case VILLAGER_JOB_FARMER: return "farmer";
    case VILLAGER_JOB_SHEPHERD: return "shepherd";
    case VILLAGER_JOB_MILLER: return "miller";
    case VILLAGER_JOB_BAKER: return "baker";
    case VILLAGER_JOB_GUARD: return "guard";
    case VILLAGER_JOB_HEALER: return "healer";
    case VILLAGER_JOB_MERCHANT: return "shopkeeper";
    case VILLAGER_JOB_BLACKSMITH: return "blacksmith";
    case VILLAGER_JOB_APPRENTICE: return "apprentice";
    case VILLAGER_JOB_NONE:
    default: return "villager";
    }
}

VillagerProfession mob_actor_villager_profession(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    if (!mob || mob->mob_type != MOB_TYPE_VILLAGER)
        return VILLAGER_JOB_NONE;
    return mob->profession;
}

const char *mob_actor_given_name(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return (mob && mob->given_name[0]) ? mob->given_name : "";
}

const char *mob_actor_family_name(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return (mob && mob->family_name[0]) ? mob->family_name : "";
}

uint32_t mob_actor_family_id(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob ? mob->family_id : 0;
}

uint32_t mob_actor_settlement_id(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob ? mob->settlement_id : 0;
}

void mob_actor_set_settlement_id(MobActor *mob, uint32_t settlement_id)
{
    if (mob)
        mob->settlement_id = settlement_id;
}

void mob_actor_roll_traits(MobActor *mob, uint32_t salt)
{
    if (!mob)
        return;
    uint32_t h = mob_hash_u32(salt ^ 0x71A17u);
    mob->trait_bravery = (int8_t)((int)(h % 101u) - 50);
    h = mob_hash_u32(h + 1);
    mob->trait_greed = (int8_t)((int)(h % 101u) - 50);
    h = mob_hash_u32(h + 1);
    mob->trait_piety = (int8_t)((int)(h % 101u) - 50);
    h = mob_hash_u32(h + 1);
    mob->trait_curiosity = (int8_t)((int)(h % 101u) - 50);
    h = mob_hash_u32(h + 1);
    mob->trait_loyalty = (int8_t)((int)(h % 101u) - 50);
    h = mob_hash_u32(h + 1);
    mob->trait_wrath = (int8_t)((int)(h % 101u) - 50);
}

void mob_actor_set_relation(MobActor *mob, uint32_t other_id, MobRelationKind kind)
{
    if (!mob || !other_id)
        return;
    for (uint8_t i = 0; i < mob->relation_count; i++)
    {
        if (mob->relations[i].other_id == other_id)
        {
            mob->relations[i].kind = kind;
            return;
        }
    }
    if (mob->relation_count >= MOB_RELATION_MAX)
        return;
    mob->relations[mob->relation_count].other_id = other_id;
    mob->relations[mob->relation_count].kind = kind;
    mob->relation_count++;
}

MobRelationKind mob_actor_relation_get(const MobActor *mob, uint32_t other_id)
{
    if (!mob || !other_id)
        return MOB_REL_NONE;
    for (uint8_t i = 0; i < mob->relation_count; i++)
    {
        if (mob->relations[i].other_id == other_id)
            return mob->relations[i].kind;
    }
    return MOB_REL_NONE;
}

bool mob_actor_are_kin(const MobActor *a, const MobActor *b)
{
    if (!a || !b || a == b)
        return false;
    if (a->mob_type != MOB_TYPE_VILLAGER || b->mob_type != MOB_TYPE_VILLAGER)
        return false;
    if (a->family_id != 0 && a->family_id == b->family_id)
        return true;
    for (uint8_t i = 0; i < a->parent_count; i++)
    {
        if (a->parent_ids[i] == b->base.id)
            return true;
    }
    for (uint8_t i = 0; i < b->parent_count; i++)
    {
        if (b->parent_ids[i] == a->base.id)
            return true;
    }
    return false;
}

void mob_actor_set_villager_identity(MobActor *mob, const char *given, const char *family,
                                     VillagerProfession job)
{
    if (!mob || mob->mob_type != MOB_TYPE_VILLAGER)
        return;
    if (given && given[0])
        strncpy(mob->given_name, given, sizeof(mob->given_name) - 1);
    if (family && family[0])
        strncpy(mob->family_name, family, sizeof(mob->family_name) - 1);
    mob->profession = job;
    if (mob->given_name[0] && mob->family_name[0])
        snprintf(mob->base.name, sizeof(mob->base.name), "%s %s",
                 mob->given_name, mob->family_name);
    else if (mob->given_name[0])
        strncpy(mob->base.name, mob->given_name, sizeof(mob->base.name) - 1);

    const char *job_name = villager_profession_name(job);
    if (mob->villager_kind == VILLAGER_CHILD)
        snprintf(mob->base.description, sizeof(mob->base.description),
                 "%s %s, a young %s of the settlement",
                 mob->given_name[0] ? mob->given_name : "A child",
                 mob->family_name[0] ? mob->family_name : "",
                 job_name);
    else
        snprintf(mob->base.description, sizeof(mob->base.description),
                 "%s %s, a %s of the settlement",
                 mob->given_name[0] ? mob->given_name : "A villager",
                 mob->family_name[0] ? mob->family_name : "",
                 job_name);
    mob_actor_apply_loadout(mob);
}

void mob_actor_link_parent(MobActor *child, MobActor *parent)
{
    if (!child || !parent || child == parent)
        return;
    if (child->mob_type != MOB_TYPE_VILLAGER || parent->mob_type != MOB_TYPE_VILLAGER)
        return;
    for (uint8_t i = 0; i < child->parent_count; i++)
    {
        if (child->parent_ids[i] == parent->base.id)
            return;
    }
    if (child->parent_count >= MOB_PARENT_MAX)
        return;
    child->parent_ids[child->parent_count++] = parent->base.id;
    if (parent->family_id != 0)
        child->family_id = parent->family_id;
    if (parent->settlement_id != 0)
        child->settlement_id = parent->settlement_id;
    if (parent->family_name[0] && !child->family_name[0])
        strncpy(child->family_name, parent->family_name, sizeof(child->family_name) - 1);
    // Dilute personality toward the parent.
    child->trait_bravery = (int8_t)((child->trait_bravery + parent->trait_bravery) / 2);
    child->trait_greed = (int8_t)((child->trait_greed + parent->trait_greed) / 2);
    child->trait_piety = (int8_t)((child->trait_piety + parent->trait_piety) / 2);
    child->trait_curiosity = (int8_t)((child->trait_curiosity + parent->trait_curiosity) / 2);
    child->trait_loyalty = (int8_t)((child->trait_loyalty + parent->trait_loyalty) / 2);
    child->trait_wrath = (int8_t)((child->trait_wrath + parent->trait_wrath) / 2);
    // Kin start with strong mutual affection.
    mob_actor_reputation_adjust(child, parent->base.id, MOB_FAMILY_AFFINITY);
    mob_actor_reputation_adjust(parent, child->base.id, MOB_FAMILY_AFFINITY);
    mob_actor_set_relation(child, parent->base.id, MOB_REL_KIN);
    mob_actor_set_relation(parent, child->base.id, MOB_REL_KIN);
    mob_actor_inherit_stories(child, parent);
    mob_actor_set_villager_identity(child, child->given_name, child->family_name,
                                    child->profession);
}

int mob_actor_story_count(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob ? (int)mob->story_count : 0;
}

const MobStoryEntry *mob_actor_story_get(const Actor *actor, int index)
{
    const MobActor *mob = mob_from_actor(actor);
    if (!mob || index < 0 || index >= (int)mob->story_count)
        return NULL;
    return &mob->stories[index];
}

bool mob_actor_add_story(MobActor *mob, uint32_t about_actor_id, uint32_t about_family_id,
                         int8_t sentiment, const char *text)
{
    if (!mob || !text || !text[0])
        return false;
    for (uint8_t i = 0; i < mob->story_count; i++)
    {
        if (strcmp(mob->stories[i].text, text) == 0)
            return false;
    }
    if (mob->story_count >= MOB_STORY_MAX)
        return false;
    MobStoryEntry *s = &mob->stories[mob->story_count++];
    s->about_actor_id = about_actor_id;
    s->about_family_id = about_family_id;
    s->sentiment = sentiment;
    strncpy(s->text, text, sizeof(s->text) - 1);
    s->text[sizeof(s->text) - 1] = '\0';
    return true;
}

int mob_actor_inherit_stories(MobActor *child, const MobActor *parent)
{
    if (!child || !parent)
        return 0;
    int added = 0;
    for (uint8_t i = 0; i < parent->story_count; i++)
    {
        const MobStoryEntry *src = &parent->stories[i];
        int8_t diluted = (int8_t)(src->sentiment / 2);
        if (diluted == 0 && src->sentiment != 0)
            diluted = (src->sentiment > 0) ? 1 : -1;
        if (mob_actor_add_story(child, src->about_actor_id, src->about_family_id,
                                diluted, src->text))
        {
            added++;
            // Soften the child's first opinion of the story's subject.
            if (src->about_actor_id != 0 && diluted != 0)
                mob_actor_reputation_adjust(child, src->about_actor_id, diluted);
        }
    }
    return added;
}

bool mob_actor_is_domesticable(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob && (mob->mob_type == MOB_TYPE_SHEEP || mob->mob_type == MOB_TYPE_CHICKEN ||
                   mob->mob_type == MOB_TYPE_DEER);
}

BirdKind mob_actor_bird_kind(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    if (!mob)
        return BIRD_KIND_CROW;
    if (mob->mob_type == MOB_TYPE_BIRD || mob->mob_type == MOB_TYPE_BAT)
        return mob->bird_kind;
    return BIRD_KIND_CROW;
}

float mob_actor_bird_weight(const Actor *actor)
{
    return mob_bird_stats(mob_actor_bird_kind(actor))->weight;
}

float mob_actor_bird_lift(const Actor *actor)
{
    return mob_bird_stats(mob_actor_bird_kind(actor))->lift;
}

float mob_actor_bird_glide_speed(const Actor *actor)
{
    return mob_bird_stats(mob_actor_bird_kind(actor))->glide_speed;
}

float mob_actor_bird_bank_rate(const Actor *actor)
{
    return mob_bird_stats(mob_actor_bird_kind(actor))->bank_rate;
}

void mob_actor_bird_color(const Actor *actor, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const BirdStats *stats = mob_bird_stats(mob_actor_bird_kind(actor));
    if (r) *r = stats->r;
    if (g) *g = stats->g;
    if (b) *b = stats->b;
}

void mob_actor_bind_mesh(MobActor *mob, const char *mesh_name)
{
    if (!mob)
        return;
    mob->mesh_name[0] = '\0';
    mob->anim_clip[0] = '\0';
    mob->anim_time = 0.0f;
    if (!mesh_name || !mesh_name[0])
        return;
    strncpy(mob->mesh_name, mesh_name, sizeof(mob->mesh_name) - 1);
    {
        // Prefer real idle clips; Walk is the fallback when a pack only ships a
        // static "Idle" placeholder that the baker drops (e.g. Kenney chick).
        const char *idle = "Idle";
        if (strcmp(mesh_name, "goleling") == 0 || strcmp(mesh_name, "pigeon") == 0)
            idle = "Flying_Idle";
        else if (strcmp(mesh_name, "chick") == 0)
            idle = "Walk";
        strncpy(mob->anim_clip, idle, sizeof(mob->anim_clip) - 1);
    }
    mob->anim_lock = 0.0f;
    mob->melee_cooldown = 0.0f;
    mob->last_health = mob->base.health;
}

const char *mob_actor_mesh_name(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    if (!mob || !mob->mesh_name[0])
        return NULL;
    return mob->mesh_name;
}

const char *mob_actor_anim_clip(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    if (!mob || !mob->anim_clip[0])
        return NULL;
    return mob->anim_clip;
}

float mob_actor_anim_time(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob ? mob->anim_time : 0.0f;
}

float mob_actor_facing_yaw(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob ? mob->facing_yaw : 0.0f;
}

float mob_actor_facing_roll(const Actor *actor)
{
    const MobActor *mob = mob_from_actor(actor);
    return mob ? mob->facing_roll : 0.0f;
}

static bool clip_is_locomotion(const char *clip)
{
    return clip && (strcmp(clip, "Flying_Idle") == 0 || strcmp(clip, "Fast_Flying") == 0 ||
                    strcmp(clip, "Idle") == 0 || strcmp(clip, "Walk") == 0 ||
                    strcmp(clip, "Run") == 0 || strcmp(clip, "Flying") == 0);
}

static const char *mesh_first_clip(const MobActor *mob, const char *const *names)
{
    if (!mob || !mob->mesh_name[0] || !names)
        return NULL;
    const PolyMesh *mesh = poly_mesh_get(mob->mesh_name);
    if (!mesh)
        return NULL;
    for (int i = 0; names[i]; i++)
    {
        if (poly_mesh_has_clip(mesh, names[i]))
            return names[i];
    }
    return NULL;
}

static float mesh_clip_duration(const MobActor *mob, const char *clip)
{
    if (!mob || !mob->mesh_name[0] || !clip)
        return 0.8f;
    const PolyMesh *mesh = poly_mesh_get(mob->mesh_name);
    const PolyMeshClip *c = poly_mesh_find_clip(mesh, clip);
    if (c && c->duration > 0.05f)
        return c->duration;
    return 0.8f;
}

static void mob_actor_play_clip(MobActor *mob, const char *clip, bool lock)
{
    if (!mob || !clip || !clip[0])
        return;
    const bool same = strcmp(mob->anim_clip, clip) == 0;
    strncpy(mob->anim_clip, clip, sizeof(mob->anim_clip) - 1);
    mob->anim_clip[sizeof(mob->anim_clip) - 1] = '\0';
    if (!same || lock)
        mob->anim_time = 0.0f;
    mob->anim_lock = lock ? mesh_clip_duration(mob, clip) : 0.0f;
}

void mob_actor_notify_talk(Actor *actor, int line_index)
{
    if (!actor || !actor->extra_data)
        return;
    MobActor *mob = (MobActor *)actor->extra_data;
    if (!mob->mesh_name[0])
        return;
    static const char *yes_names[] = {"Yes", "Idle", "Flying_Idle", NULL};
    static const char *no_names[] = {"No", "HitReact", "Idle", "Flying_Idle", NULL};
    const char *clip = mesh_first_clip(mob, line_index <= 0 ? yes_names : no_names);
    if (clip)
        mob_actor_play_clip(mob, clip, true);
}

static void mob_clear_aggro(MobActor *mob)
{
    if (!mob)
        return;
    mob->aggro_target_id = 0;
    mob->aggro_ttl = 0.0f;
    if (mob->mood == MOB_MOOD_ANGRY || mob->mood == MOB_MOOD_FEARFUL)
        mob->mood = MOB_MOOD_CALM;
    if (mob->goal == MOB_GOAL_CHASE || mob->goal == MOB_GOAL_FLEE)
        mob->goal = MOB_GOAL_WANDER;
}

static void mob_enter_aggro(MobActor *mob, uint32_t attacker_id, bool flee)
{
    if (!mob || attacker_id == 0 || attacker_id == mob->base.id)
        return;
    mob->aggro_target_id = attacker_id;
    mob->aggro_ttl = MOB_AGGRO_TTL;
    if (flee)
    {
        mob->mood = MOB_MOOD_FEARFUL;
        mob->goal = MOB_GOAL_FLEE;
    }
    else
    {
        mob->mood = MOB_MOOD_ANGRY;
        mob->goal = MOB_GOAL_CHASE;
    }
    mob->ai_state.has_target = false;
    mob->ai_state.retarget_in = 0.0f;
}

void mob_actor_after_damage(Actor *actor, uint32_t attacker_id)
{
    if (!actor)
        return;
    actor->hurt_display_ttl = ACTOR_HURT_DISPLAY_SECONDS;
    if (actor->extra_data)
    {
        MobActor *mob = (MobActor *)actor->extra_data;
        uint32_t before = mob->base.health;
        if (mob->last_health > before)
            before = mob->last_health;
        uint32_t dealt = 0;
        if (before > actor->health)
            dealt = before - actor->health;

        mob->base.health = actor->health;

        if (dealt > 0 && attacker_id != 0 && attacker_id != mob->base.id &&
            actor->health > 0)
        {
            // Being hurt by someone is a lasting dislike.
            int delta = -(int)(dealt * 3u);
            if (delta > -8)
                delta = -8;
            if (delta < -40)
                delta = -40;
            mob_actor_reputation_adjust(mob, attacker_id, delta);

            mob->damage_accum += dealt;
            mob->damage_accum_ttl = MOB_DAMAGE_ACCUM_WINDOW;
            uint32_t threshold = MOB_AGGRO_DAMAGE_THRESHOLD;
            if (mob->mob_type == MOB_TYPE_SPIDER)
                threshold = 6u; // skitterers snap faster
            if (mob->damage_accum >= threshold)
            {
                const bool low_hp = actor->health * 10u <
                                    (before > 0 ? before : 100u) * 3u;
                const bool livestock = mob->mob_type == MOB_TYPE_SHEEP ||
                                       mob->mob_type == MOB_TYPE_CHICKEN ||
                                       mob->mob_type == MOB_TYPE_DEER ||
                                       mob->mob_type == MOB_TYPE_BAT;
                const bool flee = livestock ||
                                  (low_hp &&
                                   (mob->mob_type == MOB_TYPE_BIRD ||
                                    mob->base.constitution < 8u));
                mob_enter_aggro(mob, attacker_id, flee);
                mob->damage_accum = 0;
            }
            else if (mob->mood == MOB_MOOD_CALM)
            {
                mob->mood = MOB_MOOD_CURIOUS;
                mob->mood_timer = 1.2f;
            }
        }
        mob->last_health = actor->health;
    }
    if (actor->health == 0)
        mob_actor_generate_corpse_loot(actor);
    // Dead actors stay in the world as corpses (no deactivate).
}

void mob_actor_tick_animation(MobActor *mob, float dt_seconds)
{
    if (!mob || !mob->mesh_name[0])
        return;
    if (dt_seconds < 0.0f)
        dt_seconds = 0.0f;
    if (mob->melee_cooldown > 0.0f)
    {
        mob->melee_cooldown -= dt_seconds;
        if (mob->melee_cooldown < 0.0f)
            mob->melee_cooldown = 0.0f;
    }

    const float vx = (float)mob->base.velocity_x;
    const float vy = (float)mob->base.velocity_y;
    const float speed2 = vx * vx + vy * vy;
    // Controlled bodies get facing from player_controls (including Q/E bank). AI still faces the
    // way they are moving so a circling crow looks into the turn.
    if (!mob->base.is_controlled && speed2 > 0.04f)
        mob->facing_yaw = atan2f(vy, vx);

    if (mob->base.health == 0)
    {
        static const char *death_names[] = {"Death", "Die", NULL};
        const char *death = mesh_first_clip(mob, death_names);
        if (!death)
            death = "Death";
        if (strcmp(mob->anim_clip, death) != 0)
            mob_actor_play_clip(mob, death, true);
        mob->base.velocity_x = 0.0;
        mob->base.velocity_y = 0.0;
        mob->base.velocity_z = 0.0;
        const float death_dur = mesh_clip_duration(mob, death);
        if (mob->anim_time < death_dur)
        {
            mob->anim_time += dt_seconds;
            if (mob->anim_time > death_dur)
                mob->anim_time = death_dur;
        }
        mob->anim_lock = 0.0f;
        // Remain active: corpse stays as an entity frozen on the final Death pose.
        mob->last_health = 0;
        return;
    }

    if (mob->last_health > mob->base.health)
    {
        static const char *hit_names[] = {"HitReact", "Jump", "Attack", "Idle", "Flying_Idle", NULL};
        const char *hit = mesh_first_clip(mob, hit_names);
        if (hit)
            mob_actor_play_clip(mob, hit, true);
    }
    mob->last_health = mob->base.health;

    if (mob->anim_lock > 0.0f)
    {
        mob->anim_lock -= dt_seconds;
        if (mob->anim_lock < 0.0f)
            mob->anim_lock = 0.0f;
        if (!clip_is_locomotion(mob->anim_clip))
        {
            mob->base.velocity_x = 0.0;
            mob->base.velocity_y = 0.0;
        }
        mob->anim_time += dt_seconds;
        return;
    }

    const bool flying = mob->base.is_flying ||
                        mob->ai_state.bird_phase == BIRD_PHASE_TAKEOFF ||
                        mob->ai_state.bird_phase == BIRD_PHASE_CIRCLE ||
                        mob->ai_state.bird_phase == BIRD_PHASE_APPROACH;
    const bool moving = speed2 > 0.04f || (mob->base.is_controlled && mob->base.is_flying);
    const char *clip = NULL;
    if ((mob->mob_type == MOB_TYPE_BIRD || mob->mob_type == MOB_TYPE_BAT) && flying)
    {
        static const char *fly_names[] = {"Fast_Flying", "Flying", "Run", "Walk", NULL};
        clip = mesh_first_clip(mob, fly_names);
    }
    else if (moving)
    {
        static const char *move_names[] = {"Fast_Flying", "Run", "Walk", "Flying", NULL};
        clip = mesh_first_clip(mob, move_names);
    }
    if (!clip)
    {
        static const char *idle_names[] = {"Flying_Idle", "Idle", "Walk", NULL};
        clip = mesh_first_clip(mob, idle_names);
    }
    if (!clip)
        clip = "Idle";

    mob_actor_play_clip(mob, clip, false);
    mob->anim_time += dt_seconds;
}

void mob_actor_destroy(MobActor* mob) {
    if (!mob) return;

    if (mob->ai_state.visited_map) {
        free(mob->ai_state.visited_map);
    }

    free(mob);
}

int8_t mob_actor_reputation_get(const MobActor *mob, uint32_t other_id)
{
    if (!mob || other_id == 0)
        return 0;
    for (uint8_t i = 0; i < mob->reputation_count; i++)
    {
        if (mob->reputation[i].other_id == other_id)
            return mob->reputation[i].score;
    }
    return 0;
}

void mob_actor_reputation_adjust(MobActor *mob, uint32_t other_id, int delta)
{
    if (!mob || other_id == 0 || other_id == mob->base.id || delta == 0)
        return;
    for (uint8_t i = 0; i < mob->reputation_count; i++)
    {
        if (mob->reputation[i].other_id == other_id)
        {
            int v = (int)mob->reputation[i].score + delta;
            if (v > 100)
                v = 100;
            if (v < -100)
                v = -100;
            mob->reputation[i].score = (int8_t)v;
            return;
        }
    }
    if (mob->reputation_count >= MOB_REPUTATION_MAX)
    {
        // Drop the oldest slot to make room for a fresh encounter.
        memmove(&mob->reputation[0], &mob->reputation[1],
                (MOB_REPUTATION_MAX - 1) * sizeof(MobReputationEntry));
        mob->reputation_count = MOB_REPUTATION_MAX - 1;
    }
    int v = delta;
    if (v > 100)
        v = 100;
    if (v < -100)
        v = -100;
    mob->reputation[mob->reputation_count].other_id = other_id;
    mob->reputation[mob->reputation_count].score = (int8_t)v;
    mob->reputation_count++;
}

static int8_t mob_type_affinity(const MobActor *a, const MobActor *b)
{
    if (!a || !b)
        return 0;
    if (a->mob_type == b->mob_type)
    {
        if (a->mob_type == MOB_TYPE_BIRD && a->bird_kind != b->bird_kind)
            return 8; // same class, different kind — mild flock affinity
        return 18;
    }
    const bool a_live = a->mob_type == MOB_TYPE_SHEEP || a->mob_type == MOB_TYPE_CHICKEN ||
                        a->mob_type == MOB_TYPE_DEER;
    const bool b_live = b->mob_type == MOB_TYPE_SHEEP || b->mob_type == MOB_TYPE_CHICKEN ||
                        b->mob_type == MOB_TYPE_DEER;
    if (a_live && b_live)
        return 12;
    if ((a->mob_type == MOB_TYPE_WANDERER && b_live) ||
        (b->mob_type == MOB_TYPE_WANDERER && a_live))
        return -12;
    if ((a->mob_type == MOB_TYPE_SPIDER && b_live) || (b->mob_type == MOB_TYPE_SPIDER && a_live))
        return -20;
    if ((a->mob_type == MOB_TYPE_SPIDER && b->mob_type == MOB_TYPE_BIRD) ||
        (b->mob_type == MOB_TYPE_SPIDER && a->mob_type == MOB_TYPE_BIRD))
        return -10;
    if ((a->mob_type == MOB_TYPE_LIZARD && b->mob_type == MOB_TYPE_CHICKEN) ||
        (b->mob_type == MOB_TYPE_LIZARD && a->mob_type == MOB_TYPE_CHICKEN))
        return -14;
    if ((a->mob_type == MOB_TYPE_LIZARD && b->mob_type == MOB_TYPE_SPIDER) ||
        (b->mob_type == MOB_TYPE_LIZARD && a->mob_type == MOB_TYPE_SPIDER))
        return -8;
    if ((a->mob_type == MOB_TYPE_SLIME && b->mob_type == MOB_TYPE_SLIME))
        return 14;
    if ((a->mob_type == MOB_TYPE_BAT && b_live) || (b->mob_type == MOB_TYPE_BAT && a_live))
        return -6;
    if ((a->mob_type == MOB_TYPE_SPIDER && b->mob_type == MOB_TYPE_BAT) ||
        (b->mob_type == MOB_TYPE_SPIDER && a->mob_type == MOB_TYPE_BAT))
        return -12;
    if ((a->mob_type == MOB_TYPE_BIRD && b->mob_type == MOB_TYPE_BAT) ||
        (a->mob_type == MOB_TYPE_BAT && b->mob_type == MOB_TYPE_BIRD))
        return -4;
    // Villagers favour livestock; animals warm to their keepers.
    if ((a->mob_type == MOB_TYPE_VILLAGER && b_live) ||
        (b->mob_type == MOB_TYPE_VILLAGER && a_live))
        return 14;
    if (a->mob_type == MOB_TYPE_VILLAGER && b->mob_type == MOB_TYPE_VILLAGER)
    {
        if (mob_actor_are_kin(a, b))
            return MOB_FAMILY_AFFINITY;
        return 16;
    }
    if ((a->mob_type == MOB_TYPE_VILLAGER && b->mob_type == MOB_TYPE_SPIDER) ||
        (b->mob_type == MOB_TYPE_VILLAGER && a->mob_type == MOB_TYPE_SPIDER))
        return -18;
    return 0;
}

void mob_actor_reputation_encounter(MobActor *self, const MobActor *other)
{
    if (!self || !other || self == other || other->base.id == 0)
        return;
    if (mob_actor_reputation_get(self, other->base.id) != 0)
        return; // already have a memory; do not reseeds
    // Check whether an entry exists at all (score may be 0 if adjusted to neutral).
    for (uint8_t i = 0; i < self->reputation_count; i++)
    {
        if (self->reputation[i].other_id == other->base.id)
            return;
    }
    int8_t seed = mob_type_affinity(self, other);
    if (seed == 0)
    {
        // Still record a neutral encounter so we do not re-seed every scan.
        mob_actor_reputation_adjust(self, other->base.id, 0);
        // adjust with 0 is a no-op — force insert:
        if (self->reputation_count < MOB_REPUTATION_MAX)
        {
            self->reputation[self->reputation_count].other_id = other->base.id;
            self->reputation[self->reputation_count].score = 0;
            self->reputation_count++;
        }
        return;
    }
    mob_actor_reputation_adjust(self, other->base.id, seed);
}

static void mob_scan_encounters(MobActor *mob, World *world, float dt_seconds)
{
    if (!mob || !world)
        return;
    mob->reputation_scan_in -= dt_seconds;
    if (mob->reputation_scan_in > 0.0f)
        return;
    mob->reputation_scan_in = MOB_REPUTATION_SCAN_PERIOD;
    if (!world->runtime_actors)
        return;
    const float encounter_r = actor_detection_range(&mob->base, MOB_REPUTATION_ENCOUNTER_RANGE);
    const float r2 = encounter_r * encounter_r;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->id == mob->base.id || !a->extra_data)
            continue;
        float dx = (float)(a->x - mob->base.x);
        float dy = (float)(a->y - mob->base.y);
        float dz = (float)(a->z - mob->base.z);
        if (dx * dx + dy * dy + dz * dz > r2)
            continue;
        MobActor *other = (MobActor *)a->extra_data;
        mob_actor_reputation_encounter(mob, other);
        // Peaceful same-type proximity slowly builds affection.
        if (other->mob_type == mob->mob_type &&
            mob->mood != MOB_MOOD_ANGRY && other->mood != MOB_MOOD_ANGRY)
            mob_actor_reputation_adjust(mob, other->base.id, 1);
    }
}

static bool mob_is_predator_type(MobType t)
{
    // Active hunters that close on disliked prey. Wanderers still punch foes in reach,
    // but they do not stalk flocks across the cell.
    return t == MOB_TYPE_SPIDER || t == MOB_TYPE_LIZARD;
}

static bool mob_is_timid_prey_type(MobType t)
{
    return t == MOB_TYPE_SHEEP || t == MOB_TYPE_CHICKEN || t == MOB_TYPE_DEER ||
           t == MOB_TYPE_BAT;
}

// Simple pressure rule: predators close on disliked neighbours; timid fauna bolt from foes.
// Reputation seeds from type affinity, so spider↔sheep and lizard↔chicken light up without scripts.
static void mob_consider_social_pressure(MobActor *mob, World *world)
{
    if (!mob || !world || !world->runtime_actors)
        return;
    if (mob->goal == MOB_GOAL_CHASE || mob->goal == MOB_GOAL_FLEE)
        return;
    if (mob->base.health == 0 || mob->base.is_controlled)
        return;

    const bool predator = mob_is_predator_type(mob->mob_type);
    const bool timid = mob_is_timid_prey_type(mob->mob_type) ||
                       (mob->mob_type == MOB_TYPE_VILLAGER &&
                        mob->villager_kind == VILLAGER_CHILD);
    const bool keeper = mob->mob_type == MOB_TYPE_VILLAGER &&
                        mob->villager_kind != VILLAGER_CHILD;
    if (!predator && !timid && !keeper)
        return;

    Actor *quarry = NULL;
    Actor *threat = NULL;
    const float hunt_r = actor_detection_range(&mob->base, MOB_HUNT_RANGE);
    const float threat_r = actor_detection_range(&mob->base, MOB_THREAT_SIGHT);
    float best_quarry = hunt_r * hunt_r;
    float best_threat = threat_r * threat_r;

    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->id == mob->base.id)
            continue;
        const float dx = (float)(a->x - mob->base.x);
        const float dy = (float)(a->y - mob->base.y);
        const float dz = (float)(a->z - mob->base.z);
        const float d2 = dx * dx + dy * dy + dz * dz;
        const int8_t rep = mob_actor_reputation_get(mob, a->id);
        if (rep > MOB_REPUTATION_FOE_MAX)
            continue;

        if (predator && d2 <= best_quarry)
        {
            // Predators do not hunt their own kind; wanderers leave fellow wanderers alone.
            if (a->extra_data)
            {
                MobActor *om = (MobActor *)a->extra_data;
                if (om->mob_type == mob->mob_type)
                    continue;
            }
            best_quarry = d2;
            quarry = a;
        }
        if (keeper && d2 <= best_quarry && a->extra_data)
        {
            MobActor *om = (MobActor *)a->extra_data;
            if (om->mob_type == MOB_TYPE_SPIDER)
            {
                best_quarry = d2;
                quarry = a;
            }
        }
        if (timid && d2 <= best_threat)
        {
            best_threat = d2;
            threat = a;
        }
    }

    // Fleeing takes priority for timid fauna — a sheep near a spider bolts rather than staring.
    if (timid && threat)
    {
        mob_enter_aggro(mob, threat->id, true);
        return;
    }
    if ((predator || keeper) && quarry)
        mob_enter_aggro(mob, quarry->id, false);
}

bool mob_can_move_to(World* world, int x, int y, int z) {
    if (!world) return false;

    // A cell past this world's rim is walkable when the neighbour is loaded and that mapped cell
    // would hold a walker — otherwise fauna treat every world edge as an invisible wall.
    if (world->universe_context &&
        (x < 0 || y < 0 || x >= (int)world->width || y >= (int)world->height) &&
        z >= 0 && z < (int)world->depth)
    {
        int cdx = 0, cdy = 0;
        int lx = x, ly = y;
        if (x < 0)
        {
            cdx = -1;
            lx = x + (int)world->width;
        }
        else if (x >= (int)world->width)
        {
            cdx = 1;
            lx = x - (int)world->width;
        }
        if (y < 0)
        {
            cdy = -1;
            ly = y + (int)world->height;
        }
        else if (y >= (int)world->height)
        {
            cdy = 1;
            ly = y - (int)world->height;
        }
        // Only a single-cell step into the neighbour; farther OOB is not a path pick we honour.
        if (lx < 0 || ly < 0 || lx >= (int)world->width || ly >= (int)world->height)
            return false;
        const int64_t nux = (int64_t)world->universe_x + cdx;
        const int64_t nuy = (int64_t)world->universe_y + cdy;
        if (nux < 0 || nuy < 0)
            return false;
        World *next = universe_get(world->universe_context, (uint64_t)nux, (uint64_t)nuy,
                                   world->universe_z);
        if (!next)
            return false;
        return mob_can_move_to(next, lx, ly, z);
    }

    if (x < 0 || y < 0 || z < 0 ||
        x >= (int)world->width || y >= (int)world->height || z >= (int)world->depth) {
        return false;
    }

    // Passable, by the one definition the whole engine uses. Testing for VOXEL_AIR instead made a
    // cell of tall grass, water, steam or leaves unwalkable to the AI even though a body passes
    // straight through all of them, so a band of any of them across a world was a fence no actor
    // would path around because there was nothing to path around.
    Voxel* v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (!v || world_voxel_type_blocks_movement(v->type)) {
        return false;
    }

    // Something has to hold a walker up here, and it has to be something that would also have
    // stopped it walking in — water is not a floor.
    if (z > 0) {
        Voxel* below = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(z - 1));
        if (!below || !world_voxel_type_blocks_movement(below->type)) {
            return false;
        }
    }

    return true;
}

bool mob_cell_is_open(World *world, int x, int y, int z)
{
    if (!world)
        return false;
    if (x < 0 || y < 0 || z < 0 ||
        x >= (int)world->width || y >= (int)world->height || z >= (int)world->depth)
        return false;
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (!v)
        return false;
    return !world_voxel_type_blocks_movement(v->type);
}

bool mob_actor_can_occupy(World *world, double x, double y, double z)
{
    if (!world)
        return false;

    const float r = MOB_ACTOR_RADIUS;
    const int vz = (int)floor(z);
    const int vx_min = (int)floor(x - (double)r);
    const int vx_max = (int)floor(x + (double)r);
    const int vy_min = (int)floor(y - (double)r);
    const int vy_max = (int)floor(y + (double)r);

    for (int vx = vx_min; vx <= vx_max; vx++)
    {
        for (int vy = vy_min; vy <= vy_max; vy++)
        {
            // Distance from actor axis to nearest point of this voxel's XY footprint.
            const float nx = fminf(fmaxf((float)x, (float)vx), (float)vx + 1.0f);
            const float ny = fminf(fmaxf((float)y, (float)vy), (float)vy + 1.0f);
            const float dx = (float)x - nx;
            const float dy = (float)y - ny;
            if (dx * dx + dy * dy > r * r)
                continue;

            if (vx < 0 || vy < 0 || vx >= (int)world->width || vy >= (int)world->height)
            {
                // Crossing into a loaded neighbour is allowed when this world has a universe.
                if (world->universe_context == NULL)
                    return false;
                continue;
            }
            if (!mob_cell_is_open(world, vx, vy, vz))
                return false;
        }
    }
    return true;
}

bool mob_actor_clear_of_actors(World *world, double x, double y, double z, uint32_t ignore_id)
{
    if (!world || !world->runtime_actors)
        return true;
    const float min_d = MOB_ACTOR_RADIUS * 2.0f;
    const float min_d2 = min_d * min_d;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        const Actor *o = &world->runtime_actors[i];
        if (!o->is_active || o->health == 0 || o->id == ignore_id)
            continue;
        const float dz = (float)(z - o->z);
        if (fabsf(dz) > MOB_ACTOR_SEPARATION_Z)
            continue;
        const float dx = (float)(x - o->x);
        const float dy = (float)(y - o->y);
        if (dx * dx + dy * dy < min_d2)
            return false;
    }
    return true;
}

bool mob_is_at_world_border(World* world, int x, int y, int z) {
    if (!world) return false;
    (void)z;

    return (x == 0 || x == (int)world->width - 1 ||
            y == 0 || y == (int)world->height - 1);
}

bool mob_solver_is_at_exit(MobActor* solver, World* world) {
    if (!solver || !world || solver->mob_type != MOB_TYPE_SOLVER) {
        return false;
    }

    int x = (int)solver->base.x;
    int y = (int)solver->base.y;
    int z = (int)solver->base.z;

    return mob_is_at_world_border(world, x, y, z);
}

void mob_solver_find_next_move(MobActor* solver, World* world) {
    if (!solver || !world || solver->mob_type != MOB_TYPE_SOLVER) {
        return;
    }

    int current_x = (int)solver->base.x;
    int current_y = (int)solver->base.y;
    int current_z = (int)solver->base.z;

    int dx[] = {1, 0, -1, 0};
    int dy[] = {0, 1, 0, -1};

    int face_dir = 0;
    if (solver->ai_state.last_x != current_x || solver->ai_state.last_y != current_y) {
        int move_dx = current_x - solver->ai_state.last_x;
        int move_dy = current_y - solver->ai_state.last_y;

        for (int i = 0; i < 4; i++) {
            if (dx[i] == move_dx && dy[i] == move_dy) {
                face_dir = i;
                break;
            }
        }
    }

    int move_order[] = {(face_dir + 1) % 4, face_dir, (face_dir + 3) % 4, (face_dir + 2) % 4};

    for (int i = 0; i < 4; i++) {
        int dir = move_order[i];
        int new_x = current_x + dx[dir];
        int new_y = current_y + dy[dir];

        if (mob_can_move_to(world, new_x, new_y, current_z)) {
            solver->base.velocity_x = (double)dx[dir] * 2.0;
            solver->base.velocity_y = (double)dy[dir] * 2.0;

            solver->ai_state.last_x = current_x;
            solver->ai_state.last_y = current_y;
            solver->ai_state.last_z = current_z;
            solver->ai_state.stuck_counter = 0;
            return;
        }
    }

    solver->base.velocity_x = 0.0;
    solver->base.velocity_y = 0.0;
    solver->ai_state.stuck_counter++;
}

static float mob_idle_pause(const MobActor *mob)
{
    float base = 0.45f + (float)(mob->base.id % 5) * 0.08f;
    if (mob->mood == MOB_MOOD_CURIOUS)
        base = 0.22f + (float)(mob->base.id % 4) * 0.06f;
    else if (mob->mood == MOB_MOOD_CALM)
        base = 0.35f + (float)(mob->base.id % 5) * 0.08f;
    return base;
}

static Actor *nearest_same_type(MobActor *mob, World *world, float max_range, float *out_dist)
{
    if (!mob || !world || !world->runtime_actors)
        return NULL;
    Actor *best = NULL;
    float best_d2 = max_range * max_range;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->id == mob->base.id || !a->extra_data)
            continue;
        MobActor *om = (MobActor *)a->extra_data;
        if (om->mob_type != mob->mob_type)
            continue;
        if (mob->mob_type == MOB_TYPE_BIRD && om->bird_kind != mob->bird_kind)
            continue;
        const float dx = (float)(a->x - mob->base.x);
        const float dy = (float)(a->y - mob->base.y);
        const float dz = (float)(a->z - mob->base.z);
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= best_d2)
        {
            best_d2 = d2;
            best = a;
        }
    }
    if (best && out_dist)
        *out_dist = sqrtf(best_d2);
    return best;
}

static bool mob_is_herding_type(MobType t)
{
    return t == MOB_TYPE_SHEEP || t == MOB_TYPE_CHICKEN || t == MOB_TYPE_DEER ||
           t == MOB_TYPE_BIRD || t == MOB_TYPE_BAT || t == MOB_TYPE_WANDERER ||
           t == MOB_TYPE_SLIME;
}

static void mob_wanderer_pick_target(MobActor* mob, World* world)
{
    int cx = (int)floor(mob->base.x);
    int cy = (int)floor(mob->base.y);
    int cz = (int)floor(mob->base.z);

    uint32_t h = mob_hash_u32(mob->base.id ^ (uint32_t)(cx * 374761393u + cy * 668265263u +
                                                        (uint32_t)mob->ai_state.stuck_counter *
                                                            2246822519u));

    // Herd: when a same-type neighbor is farther than comfort, walk toward them.
    if (mob_is_herding_type(mob->mob_type))
    {
        float herd_dist = 0.0f;
        Actor *kin = nearest_same_type(mob, world, MOB_HERD_RANGE, &herd_dist);
        if (kin && herd_dist > MOB_HERD_COMFORT)
        {
            int tx = (int)floor(kin->x);
            int ty = (int)floor(kin->y);
            int steps = 3 + (int)((h >> 5) % 5u);
            int px = cx, py = cy;
            for (int s = 0; s < steps; s++)
            {
                int nx = px + (tx > px ? 1 : (tx < px ? -1 : 0));
                int ny = py + (ty > py ? 1 : (ty < py ? -1 : 0));
                if (nx != px && ny != py && mob_can_move_to(world, nx, ny, cz))
                {
                    px = nx;
                    py = ny;
                    continue;
                }
                if (nx != px && mob_can_move_to(world, nx, py, cz))
                {
                    px = nx;
                    continue;
                }
                if (ny != py && mob_can_move_to(world, px, ny, cz))
                {
                    py = ny;
                    continue;
                }
                break;
            }
            if (px != cx || py != cy)
            {
                mob->ai_state.target_x = px;
                mob->ai_state.target_y = py;
                mob->ai_state.target_z = cz;
                mob->ai_state.has_target = true;
                return;
            }
        }
    }

    static const int dx4[] = {1, 0, -1, 0};
    static const int dy4[] = {0, 1, 0, -1};
    static const int dx8[] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int dy8[] = {0, 1, 1, 1, 0, -1, -1, -1};
    const bool use8 = ((h >> 8) % 10u) < 3u;
    const int *dx = use8 ? dx8 : dx4;
    const int *dy = use8 ? dy8 : dy4;
    const int ndir = use8 ? 8 : 4;
    const int attempts = use8 ? 8 : 4;

    for (int attempt = 0; attempt < attempts; attempt++) {
        int dir = (int)((h + (uint32_t)attempt) % (uint32_t)ndir);
        int steps = 2 + (int)((h >> (attempt * 3)) % 4u);
        if (((h >> 16) + (uint32_t)attempt) % 5u == 0)
            steps = 4 + (int)((h >> (attempt + 4)) % 5u);
        int tx = cx;
        int ty = cy;
        int reached = 0;
        for (int s = 0; s < steps; s++) {
            int nx = tx + dx[dir];
            int ny = ty + dy[dir];
            if (!mob_can_move_to(world, nx, ny, cz))
                break;
            tx = nx;
            ty = ny;
            reached++;
        }
        if (reached > 0 && (tx != cx || ty != cy)) {
            mob->ai_state.target_x = tx;
            mob->ai_state.target_y = ty;
            mob->ai_state.target_z = cz;
            mob->ai_state.has_target = true;
            return;
        }
    }

    mob->ai_state.has_target = false;
    mob->ai_state.retarget_in = mob_idle_pause(mob);
}

static Actor *find_living_actor_by_id(World *world, uint32_t id)
{
    if (!world || !world->runtime_actors || id == 0 || id == MOB_THREAT_PLAYER)
        return NULL;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (a->id == id && a->is_active && a->health > 0)
            return a;
    }
    return NULL;
}

static bool mob_resolve_threat_pos(MobActor *mob, World *world,
                                   float *out_x, float *out_y, float *out_z)
{
    if (!mob || !out_x || !out_y || !out_z || mob->aggro_target_id == 0)
        return false;

    if (mob->aggro_target_id == MOB_THREAT_PLAYER ||
        (s_player_presence_valid && mob->aggro_target_id == s_player_body_id &&
         s_player_body_id != 0))
    {
        if (!s_player_presence_valid)
            return false;
        *out_x = s_player_x;
        *out_y = s_player_y;
        *out_z = s_player_z;
        return true;
    }

    Actor *t = find_living_actor_by_id(world, mob->aggro_target_id);
    if (!t)
        return false;
    *out_x = (float)t->x;
    *out_y = (float)t->y;
    *out_z = (float)t->z;
    return true;
}

static Actor *mob_melee_target(MobActor *mob, World *world)
{
    if (!mob || !world)
        return NULL;
    if (mob->aggro_target_id != 0 && mob->aggro_target_id != MOB_THREAT_PLAYER)
    {
        Actor *pref = find_living_actor_by_id(world, mob->aggro_target_id);
        if (pref)
        {
            const float dx = (float)(pref->x - mob->base.x);
            const float dy = (float)(pref->y - mob->base.y);
            const float dz = (float)(pref->z - mob->base.z);
            if (dx * dx + dy * dy + dz * dz <= MOB_MELEE_RANGE * MOB_MELEE_RANGE)
                return pref;
        }
    }

    // Livestock only fight when actively chasing (handled above via aggro).
    if (mob->mob_type == MOB_TYPE_SHEEP || mob->mob_type == MOB_TYPE_CHICKEN ||
        mob->mob_type == MOB_TYPE_DEER || mob->mob_type == MOB_TYPE_SLIME)
        return NULL;

    // Villagers only strike disliked foes (spiders), never passers-by.
    const bool villager = mob->mob_type == MOB_TYPE_VILLAGER;

    Actor *best_foe = NULL;
    Actor *best_any = NULL;
    float best_foe_d2 = MOB_MELEE_RANGE * MOB_MELEE_RANGE;
    float best_any_d2 = MOB_MELEE_RANGE * MOB_MELEE_RANGE;
    if (!world->runtime_actors)
        return NULL;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->id == mob->base.id)
            continue;
        const float dx = (float)(a->x - mob->base.x);
        const float dy = (float)(a->y - mob->base.y);
        const float dz = (float)(a->z - mob->base.z);
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > MOB_MELEE_RANGE * MOB_MELEE_RANGE)
            continue;
        int8_t rep = mob_actor_reputation_get(mob, a->id);
        if (rep >= MOB_REPUTATION_FRIEND_MIN)
            continue; // affection — will not strike
        if (a->extra_data)
        {
            MobActor *om = (MobActor *)a->extra_data;
            if (om->mob_type == mob->mob_type)
                continue; // never casually punch the herd
            if (villager && (om->mob_type == MOB_TYPE_SHEEP || om->mob_type == MOB_TYPE_CHICKEN ||
                             om->mob_type == MOB_TYPE_DEER))
                continue;
        }
        if (rep <= MOB_REPUTATION_FOE_MAX && d2 <= best_foe_d2)
        {
            best_foe_d2 = d2;
            best_foe = a;
        }
        if (!villager && d2 <= best_any_d2)
        {
            best_any_d2 = d2;
            best_any = a;
        }
    }
    return best_foe ? best_foe : best_any;
}

static bool mob_try_melee(MobActor *mob, World *world)
{
    if (!mob || mob->anim_lock > 0.0f ||
        mob->melee_cooldown > 0.0f || mob->base.is_controlled)
        return false;
    Actor *t = mob_melee_target(mob, world);
    if (!t)
        return false;
    const float dx = (float)(t->x - mob->base.x);
    const float dy = (float)(t->y - mob->base.y);
    if (dx * dx + dy * dy > 1e-8f)
        mob->facing_yaw = atan2f(dy, dx);
    mob->base.velocity_x = 0.0;
    mob->base.velocity_y = 0.0;
    if (mob->mesh_name[0])
    {
        static const char *atk_names[] = {"Punch", "Headbutt", "Attack", "Jump", NULL};
        const char *atk = mesh_first_clip(mob, atk_names);
        if (atk)
            mob_actor_play_clip(mob, atk, true);
    }
    mob->melee_cooldown = MOB_MELEE_COOLDOWN;

    uint32_t dmg = 1u + actor_attack_bonus(&mob->base);
    if (dmg < 1u)
        dmg = 1u;
    actor_apply_damage(t, dmg);
    // Attacking someone deepens dislike both ways when they notice.
    mob_actor_reputation_adjust(mob, t->id, -6);
    if (t->extra_data)
        mob_actor_reputation_adjust((MobActor *)t->extra_data, mob->base.id, -10);
    mob_actor_after_damage(t, mob->base.id);
    if (mob->goal == MOB_GOAL_CHASE)
        mob->aggro_ttl = MOB_AGGRO_TTL;
    return true;
}

static void mob_tick_mood_and_aggro(MobActor *mob, float dt_seconds)
{
    if (!mob)
        return;
    if (mob->breed_cooldown > 0.0f)
    {
        mob->breed_cooldown -= dt_seconds;
        if (mob->breed_cooldown < 0.0f)
            mob->breed_cooldown = 0.0f;
    }
    if (mob->damage_accum_ttl > 0.0f)
    {
        mob->damage_accum_ttl -= dt_seconds;
        if (mob->damage_accum_ttl <= 0.0f)
        {
            mob->damage_accum_ttl = 0.0f;
            mob->damage_accum = 0;
        }
    }
    if (mob->aggro_ttl > 0.0f)
    {
        mob->aggro_ttl -= dt_seconds;
        if (mob->aggro_ttl <= 0.0f)
            mob_clear_aggro(mob);
    }
    if (mob->mood == MOB_MOOD_ANGRY || mob->mood == MOB_MOOD_FEARFUL)
        return;
    mob->mood_timer -= dt_seconds;
    if (mob->mood_timer > 0.0f)
        return;
    mob->mood_timer = MOB_MOOD_DRIFT_PERIOD + (float)(mob->base.id % 5) * 0.4f;
    if (mob->mood == MOB_MOOD_CALM)
    {
        uint32_t h = mob_hash_u32(mob->base.id ^ (uint32_t)(mob->base.x * 13.0) ^
                                  (uint32_t)(mob->mood_timer * 100.0f));
        if ((h % 5u) == 0u)
        {
            mob->mood = MOB_MOOD_CURIOUS;
            mob->mood_timer = 1.5f + (float)(h % 3u) * 0.4f;
        }
    }
    else if (mob->mood == MOB_MOOD_CURIOUS)
        mob->mood = MOB_MOOD_CALM;
}

static void mob_pick_flee_target(MobActor *mob, World *world, float tx, float ty)
{
    int cx = (int)floor(mob->base.x);
    int cy = (int)floor(mob->base.y);
    int cz = (int)floor(mob->base.z);
    float away_x = (float)mob->base.x - tx;
    float away_y = (float)mob->base.y - ty;
    float len = sqrtf(away_x * away_x + away_y * away_y);
    if (len < 1e-3f)
    {
        uint32_t h = mob_hash_u32(mob->base.id);
        away_x = (float)((int)(h % 3u) - 1);
        away_y = (float)((int)((h >> 4) % 3u) - 1);
        len = sqrtf(away_x * away_x + away_y * away_y);
        if (len < 1e-3f)
        {
            away_x = 1.0f;
            len = 1.0f;
        }
    }
    away_x /= len;
    away_y /= len;
    int best_x = cx, best_y = cy, best_score = -1;
    static const int dx8[] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int dy8[] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int d = 0; d < 8; d++)
    {
        int nx = cx + dx8[d] * 3;
        int ny = cy + dy8[d] * 3;
        if (!mob_can_move_to(world, nx, ny, cz))
        {
            nx = cx + dx8[d];
            ny = cy + dy8[d];
            if (!mob_can_move_to(world, nx, ny, cz))
                continue;
        }
        float sx = (float)(nx - cx);
        float sy = (float)(ny - cy);
        int score = (int)(sx * away_x * 10.0f + sy * away_y * 10.0f);
        if (score > best_score)
        {
            best_score = score;
            best_x = nx;
            best_y = ny;
        }
    }
    if (best_score >= 0)
    {
        mob->ai_state.target_x = best_x;
        mob->ai_state.target_y = best_y;
        mob->ai_state.target_z = cz;
        mob->ai_state.has_target = true;
    }
}

static void mob_steer_toward(MobActor *mob, float tx, float ty, float speed)
{
    double dx = (double)tx - mob->base.x;
    double dy = (double)ty - mob->base.y;
    double dist = sqrt(dx * dx + dy * dy);
    if (dist < 0.15)
    {
        mob->base.velocity_x = 0.0;
        mob->base.velocity_y = 0.0;
        return;
    }
    mob->base.velocity_x = (dx / dist) * (double)speed;
    mob->base.velocity_y = (dy / dist) * (double)speed;
    mob->facing_yaw = atan2f((float)dy, (float)dx);
}

static void mob_wanderer_update(MobActor* mob, World* world, float dt_seconds)
{
    if (mob->base.is_controlled) {
        mob->base.velocity_x = 0.0;
        mob->base.velocity_y = 0.0;
        return;
    }

    mob_tick_mood_and_aggro(mob, dt_seconds);
    mob_scan_encounters(mob, world, dt_seconds);
    if (mob->goal != MOB_GOAL_CHASE && mob->goal != MOB_GOAL_FLEE)
        mob_consider_social_pressure(mob, world);

    if (mob->goal == MOB_GOAL_CHASE || mob->goal == MOB_GOAL_FLEE)
    {
        float tx, ty, tz;
        if (!mob_resolve_threat_pos(mob, world, &tx, &ty, &tz))
        {
            mob_clear_aggro(mob);
        }
        else if (mob->goal == MOB_GOAL_CHASE)
        {
            if (mob_try_melee(mob, world))
                return;
            float dx = tx - (float)mob->base.x;
            float dy = ty - (float)mob->base.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > MOB_MELEE_RANGE * 0.85f)
                mob_steer_toward(mob, tx, ty, MOB_CHASE_SPEED);
            else
            {
                mob->base.velocity_x = 0.0;
                mob->base.velocity_y = 0.0;
            }
            return;
        }
        else
        {
            if (!mob->ai_state.has_target)
                mob_pick_flee_target(mob, world, tx, ty);
            if (mob->ai_state.has_target)
            {
                double dx = ((double)mob->ai_state.target_x + 0.5) - mob->base.x;
                double dy = ((double)mob->ai_state.target_y + 0.5) - mob->base.y;
                double dist = sqrt(dx * dx + dy * dy);
                if (dist < 0.3)
                {
                    mob->ai_state.has_target = false;
                    mob_pick_flee_target(mob, world, tx, ty);
                }
                else
                    mob_steer_toward(mob, (float)mob->ai_state.target_x + 0.5f,
                                     (float)mob->ai_state.target_y + 0.5f, MOB_CHASE_SPEED);
            }
            return;
        }
    }

    // Domesticated livestock shadow their villager when not fleeing.
    if (mob->follow_target_id != 0)
    {
        Actor *leader = find_living_actor_by_id(world, mob->follow_target_id);
        if (!leader)
        {
            mob->follow_target_id = 0;
            if (mob->goal == MOB_GOAL_FOLLOW)
                mob->goal = MOB_GOAL_WANDER;
        }
        else
        {
            mob->goal = MOB_GOAL_FOLLOW;
            float dx = (float)(leader->x - mob->base.x);
            float dy = (float)(leader->y - mob->base.y);
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > MOB_FOLLOW_COMFORT)
                mob_steer_toward(mob, (float)leader->x, (float)leader->y, MOB_WANDER_SPEED * 1.1f);
            else
            {
                mob->base.velocity_x = 0.0;
                mob->base.velocity_y = 0.0;
                mob->facing_yaw += 0.35f * dt_seconds;
            }
            return;
        }
    }

    // Only opportunistic melee when angry or when a known foe is in reach.
    if (mob->mood == MOB_MOOD_ANGRY || mob->goal == MOB_GOAL_CHASE)
    {
        if (mob_try_melee(mob, world))
            return;
    }
    else
    {
        // Still allow striking a disliked neighbor without full aggro.
        Actor *foe = NULL;
        if (world->runtime_actors)
        {
            for (int i = 0; i < world->runtime_actor_count; i++)
            {
                Actor *a = &world->runtime_actors[i];
                if (!a->is_active || a->health == 0 || a->id == mob->base.id)
                    continue;
                if (mob_actor_reputation_get(mob, a->id) > MOB_REPUTATION_FOE_MAX)
                    continue;
                float dx = (float)(a->x - mob->base.x);
                float dy = (float)(a->y - mob->base.y);
                float dz = (float)(a->z - mob->base.z);
                if (dx * dx + dy * dy + dz * dz <= MOB_MELEE_RANGE * MOB_MELEE_RANGE)
                {
                    foe = a;
                    break;
                }
            }
        }
        if (foe && mob_try_melee(mob, world))
            return;
    }

    if (mob->ai_state.has_target) {
        double dx = ((double)mob->ai_state.target_x + 0.5) - mob->base.x;
        double dy = ((double)mob->ai_state.target_y + 0.5) - mob->base.y;
        double dist = sqrt(dx * dx + dy * dy);
        if (dist < 0.25) {
            mob->ai_state.has_target = false;
            mob->ai_state.path_stuck_in = 0.0f;
            mob->base.velocity_x = 0.0;
            mob->base.velocity_y = 0.0;
            mob->ai_state.retarget_in = mob_idle_pause(mob);
            return;
        }
        // If we are not making cell progress, abandon the target and pick again.
        {
            int cx = (int)floor(mob->base.x);
            int cy = (int)floor(mob->base.y);
            if (cx == mob->ai_state.last_x && cy == mob->ai_state.last_y)
                mob->ai_state.path_stuck_in += dt_seconds;
            else
            {
                mob->ai_state.last_x = cx;
                mob->ai_state.last_y = cy;
                mob->ai_state.path_stuck_in = 0.0f;
            }
            if (mob->ai_state.path_stuck_in > 1.4f)
            {
                mob->ai_state.has_target = false;
                mob->ai_state.path_stuck_in = 0.0f;
                mob->ai_state.stuck_counter++;
                mob->ai_state.retarget_in = 0.12f;
                mob->base.velocity_x = 0.0;
                mob->base.velocity_y = 0.0;
                return;
            }
        }
        float speed = MOB_WANDER_SPEED;
        if (mob->mood == MOB_MOOD_CURIOUS)
            speed = MOB_WANDER_SPEED * 1.15f;
        if (mob->mob_type == MOB_TYPE_SLIME)
            speed = MOB_WANDER_SPEED * 0.45f;
        else if (mob->mob_type == MOB_TYPE_SPIDER)
            speed = MOB_WANDER_SPEED * 1.35f;
        else if (mob->mob_type == MOB_TYPE_DEER)
            speed = MOB_WANDER_SPEED * 1.2f;
        else if (mob->mob_type == MOB_TYPE_LIZARD)
            speed = MOB_WANDER_SPEED * 1.05f;
        // Soft flock cohesion: blend a little toward nearby kin.
        float herd_dist = 0.0f;
        Actor *kin = nearest_same_type(mob, world, MOB_HERD_RANGE, &herd_dist);
        if (kin && herd_dist > MOB_HERD_COMFORT)
        {
            double kx = kin->x - mob->base.x;
            double ky = kin->y - mob->base.y;
            double klen = sqrt(kx * kx + ky * ky);
            if (klen > 1e-3)
            {
                dx = dx * 0.7 + (kx / klen) * dist * 0.3;
                dy = dy * 0.7 + (ky / klen) * dist * 0.3;
                dist = sqrt(dx * dx + dy * dy);
            }
        }
        if (dist > 1e-4)
        {
            mob->base.velocity_x = (dx / dist) * (double)speed;
            mob->base.velocity_y = (dy / dist) * (double)speed;
        }
        return;
    }

    mob->ai_state.retarget_in -= dt_seconds;
    mob->base.velocity_x = 0.0;
    mob->base.velocity_y = 0.0;
    // Look around while paused.
    mob->facing_yaw += 0.55f * dt_seconds *
                       (((mob->base.id + (uint32_t)mob->ai_state.stuck_counter) & 1u) ? 1.0f : -1.0f);
    if (mob->ai_state.retarget_in <= 0.0f) {
        mob->ai_state.stuck_counter++;
        mob_wanderer_pick_target(mob, world);
        if (!mob->ai_state.has_target)
            mob->ai_state.retarget_in = mob_idle_pause(mob);
    }
}

static int stand_z_at(World *world, int x, int y);

static bool bird_pick_landing(MobActor *mob, World *world)
{
    int cx = (int)floor(mob->base.x);
    int cy = (int)floor(mob->base.y);
    uint32_t h = mob_hash_u32(mob->base.id ^ (uint32_t)(cx * 2654435761u + cy * 40503u +
                                                        (uint32_t)mob->ai_state.stuck_counter));
    for (int attempt = 0; attempt < 10; attempt++) {
        int dist = 4 + (int)((h >> (attempt * 2)) % 9u);
        int dir = (int)((h + (uint32_t)attempt * 3u) % 8u);
        static const int dx8[] = {1, 1, 0, -1, -1, -1, 0, 1};
        static const int dy8[] = {0, 1, 1, 1, 0, -1, -1, -1};
        int tx = cx + dx8[dir] * dist;
        int ty = cy + dy8[dir] * dist;
        int tz = stand_z_at(world, tx, ty);
        if (tz < 0 || !mob_can_move_to(world, tx, ty, tz))
            continue;
        mob->ai_state.target_x = tx;
        mob->ai_state.target_y = ty;
        mob->ai_state.target_z = tz;
        mob->ai_state.has_target = true;
        return true;
    }
    int tz = stand_z_at(world, cx, cy);
    if (tz >= 0) {
        mob->ai_state.target_x = cx;
        mob->ai_state.target_y = cy;
        mob->ai_state.target_z = tz;
        mob->ai_state.has_target = true;
        return true;
    }
    return false;
}

static void mob_bird_update(MobActor *mob, World *world, float dt_seconds)
{
    if (mob->base.is_controlled) {
        mob->base.velocity_x = 0.0;
        mob->base.velocity_y = 0.0;
        mob->base.velocity_z = 0.0;
        return;
    }

    mob_tick_mood_and_aggro(mob, dt_seconds);
    mob_scan_encounters(mob, world, dt_seconds);
    if (mob->goal != MOB_GOAL_CHASE && mob->goal != MOB_GOAL_FLEE)
        mob_consider_social_pressure(mob, world);

    const BirdStats *stats = mob_bird_stats(mob->bird_kind);
    mob->ai_state.bird_timer -= dt_seconds;

    // On the ground, angry/fearful birds chase or flee like wanderers.
    if ((BirdPhase)mob->ai_state.bird_phase == BIRD_PHASE_PERCH &&
        (mob->goal == MOB_GOAL_CHASE || mob->goal == MOB_GOAL_FLEE))
    {
        float tx, ty, tz;
        if (!mob_resolve_threat_pos(mob, world, &tx, &ty, &tz))
            mob_clear_aggro(mob);
        else if (mob->goal == MOB_GOAL_CHASE)
        {
            mob->base.is_flying = false;
            if (mob_try_melee(mob, world))
                return;
            mob_steer_toward(mob, tx, ty, MOB_CHASE_SPEED);
            mob->base.velocity_z = 0.0;
            return;
        }
        else
        {
            mob->base.is_flying = false;
            if (!mob->ai_state.has_target)
                mob_pick_flee_target(mob, world, tx, ty);
            if (mob->ai_state.has_target)
                mob_steer_toward(mob, (float)mob->ai_state.target_x + 0.5f,
                                 (float)mob->ai_state.target_y + 0.5f, MOB_CHASE_SPEED);
            mob->base.velocity_z = 0.0;
            return;
        }
    }

    switch ((BirdPhase)mob->ai_state.bird_phase) {
    case BIRD_PHASE_PERCH:
        mob->base.is_flying = false;
        mob->base.velocity_x = 0.0;
        mob->base.velocity_y = 0.0;
        mob->base.velocity_z = 0.0;
        if (mob_try_melee(mob, world))
            break;
        if (mob->ai_state.bird_timer <= 0.0f) {
            uint32_t h = mob_hash_u32(mob->base.id ^ (uint32_t)(mob->base.x * 17.0 + mob->base.y * 31.0));
            float heading = (float)(h % 360u) * ((float)M_PI / 180.0f);
            mob->ai_state.circle_cx = (float)mob->base.x;
            mob->ai_state.circle_cy = (float)mob->base.y;
            mob->ai_state.circle_angle = heading;
            mob->ai_state.perch_z = (float)mob->base.z;
            mob->base.is_flying = true;
            mob->base.velocity_x = cos(heading) * stats->glide_speed * 0.55;
            mob->base.velocity_y = sin(heading) * stats->glide_speed * 0.55;
            mob->base.velocity_z = stats->takeoff_vz;
            mob->ai_state.bird_phase = BIRD_PHASE_TAKEOFF;
            mob->ai_state.bird_timer = 1.1f + (float)(mob->base.id % 4) * 0.15f;
        }
        break;

    case BIRD_PHASE_TAKEOFF: {
        mob->base.is_flying = true;
        float cruise_z = mob->ai_state.perch_z + stats->cruise_alt;
        if (mob->base.z < cruise_z)
            mob->base.velocity_z = stats->takeoff_vz * 0.55;
        else
            mob->base.velocity_z = 0.0;
        if (mob->ai_state.bird_timer <= 0.0f || mob->base.z >= cruise_z - 0.4) {
            float revs = 1.15f + (float)(mob->base.id % 3) * 0.4f;
            mob->ai_state.bird_phase = BIRD_PHASE_CIRCLE;
            mob->ai_state.bird_timer = (float)((2.0 * M_PI) / stats->circle_omega) * revs;
        }
        break;
    }

    case BIRD_PHASE_CIRCLE: {
        mob->base.is_flying = true;
        mob->ai_state.circle_angle += stats->circle_omega * dt_seconds;
        float tx = mob->ai_state.circle_cx + stats->circle_radius * cosf(mob->ai_state.circle_angle);
        float ty = mob->ai_state.circle_cy + stats->circle_radius * sinf(mob->ai_state.circle_angle);
        // Angry birds bias the orbit center toward the threat.
        if (mob->mood == MOB_MOOD_ANGRY)
        {
            float hx, hy, hz;
            if (mob_resolve_threat_pos(mob, world, &hx, &hy, &hz))
            {
                tx = tx * 0.65f + hx * 0.35f;
                ty = ty * 0.65f + hy * 0.35f;
            }
        }
        double dx = (double)tx - mob->base.x;
        double dy = (double)ty - mob->base.y;
        double dist = sqrt(dx * dx + dy * dy);
        if (dist > 0.05) {
            mob->base.velocity_x = (dx / dist) * stats->glide_speed;
            mob->base.velocity_y = (dy / dist) * stats->glide_speed;
        }
        float cruise_z = mob->ai_state.perch_z + stats->cruise_alt;
        float zerr = cruise_z - (float)mob->base.z;
        mob->base.velocity_z = zerr * 0.7f;
        if (mob->base.velocity_z > 1.6)
            mob->base.velocity_z = 1.6;
        if (mob->base.velocity_z < -1.2)
            mob->base.velocity_z = -1.2;
        if (mob->ai_state.bird_timer <= 0.0f) {
            mob->ai_state.stuck_counter++;
            if (mob->mood == MOB_MOOD_ANGRY)
            {
                float hx, hy, hz;
                if (mob_resolve_threat_pos(mob, world, &hx, &hy, &hz))
                {
                    int lx = (int)floorf(hx);
                    int ly = (int)floorf(hy);
                    int lz = stand_z_at(world, lx, ly);
                    if (lz >= 0 && mob_can_move_to(world, lx, ly, lz))
                    {
                        mob->ai_state.target_x = lx;
                        mob->ai_state.target_y = ly;
                        mob->ai_state.target_z = lz;
                        mob->ai_state.has_target = true;
                        mob->ai_state.bird_phase = BIRD_PHASE_APPROACH;
                        mob->ai_state.bird_timer = 8.0f;
                        break;
                    }
                }
            }
            if (bird_pick_landing(mob, world)) {
                mob->ai_state.bird_phase = BIRD_PHASE_APPROACH;
                mob->ai_state.bird_timer = 8.0f;
            } else {
                mob->ai_state.bird_timer = 1.5f;
            }
        }
        break;
    }

    case BIRD_PHASE_APPROACH: {
        mob->base.is_flying = true;
        if (!mob->ai_state.has_target && !bird_pick_landing(mob, world)) {
            mob->ai_state.bird_phase = BIRD_PHASE_PERCH;
            mob->ai_state.bird_timer = 1.2f;
            mob->base.is_flying = false;
            break;
        }
        double lx = (double)mob->ai_state.target_x + 0.5;
        double ly = (double)mob->ai_state.target_y + 0.5;
        double lz = (double)mob->ai_state.target_z;
        double dx = lx - mob->base.x;
        double dy = ly - mob->base.y;
        double dist = sqrt(dx * dx + dy * dy);
        float approach = stats->glide_speed * 0.8f;
        if (dist > 0.08) {
            mob->base.velocity_x = (dx / dist) * approach;
            mob->base.velocity_y = (dy / dist) * approach;
        } else {
            mob->base.velocity_x = 0.0;
            mob->base.velocity_y = 0.0;
        }
        double zerr = lz - mob->base.z;
        mob->base.velocity_z = zerr * 0.85;
        if (mob->base.velocity_z > 0.4)
            mob->base.velocity_z = 0.4;
        if (mob->base.velocity_z < -stats->sink_max)
            mob->base.velocity_z = -stats->sink_max;
        if ((dist < 0.7 && mob->base.z <= lz + 1.15) || mob->ai_state.bird_timer <= 0.0f) {
            mob->base.x = lx;
            mob->base.y = ly;
            mob->base.z = lz;
            mob->base.velocity_x = 0.0;
            mob->base.velocity_y = 0.0;
            mob->base.velocity_z = 0.0;
            mob->base.is_flying = false;
            mob->ai_state.perch_z = (float)lz;
            mob->ai_state.has_target = false;
            mob->ai_state.bird_phase = BIRD_PHASE_PERCH;
            mob->ai_state.bird_timer = 2.0f + (float)(mob->base.id % 6) * 0.4f;
            if (mob->mesh_name[0])
            {
                mob_actor_play_clip(mob, "Headbutt", true);
                mob->melee_cooldown = MOB_MELEE_COOLDOWN;
            }
        }
        break;
    }
    }
}

static void mob_villager_tend_livestock(MobActor *mob, World *world)
{
    if (!mob || !world || !world->runtime_actors)
        return;
    const float r2 = MOB_DOMESTICATE_RANGE * MOB_DOMESTICATE_RANGE;
    Actor *best = NULL;
    float best_d2 = r2;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->id == mob->base.id || !a->extra_data)
            continue;
        if (!mob_actor_is_domesticable(a))
            continue;
        MobActor *animal = (MobActor *)a->extra_data;
        // Already bonded to someone else — leave them be.
        if (animal->follow_target_id != 0 && animal->follow_target_id != mob->base.id)
            continue;
        float dx = (float)(a->x - mob->base.x);
        float dy = (float)(a->y - mob->base.y);
        float dz = (float)(a->z - mob->base.z);
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > best_d2)
            continue;
        best_d2 = d2;
        best = a;
    }
    if (!best || !best->extra_data)
        return;

    MobActor *animal = (MobActor *)best->extra_data;
    mob_actor_reputation_encounter(mob, animal);
    mob_actor_reputation_encounter(animal, mob);
    int gain = MOB_DOMESTICATE_GAIN;
    if (mob->villager_kind == VILLAGER_CHILD)
        gain = 3; // children are gentler / slower keepers
    mob_actor_reputation_adjust(mob, best->id, gain);
    mob_actor_reputation_adjust(animal, mob->base.id, gain);

    if (mob_actor_reputation_get(animal, mob->base.id) >= MOB_DOMESTICATE_AFFINITY)
    {
        animal->follow_target_id = mob->base.id;
        if (animal->goal != MOB_GOAL_CHASE && animal->goal != MOB_GOAL_FLEE)
            animal->goal = MOB_GOAL_FOLLOW;
    }

    // Walk toward the animal while tending so the village gathers a flock.
    if (best_d2 > MOB_FOLLOW_COMFORT * MOB_FOLLOW_COMFORT)
        mob_steer_toward(mob, (float)best->x, (float)best->y, MOB_WANDER_SPEED * 0.9f);
}

static void mob_villager_update(MobActor *mob, World *world, float dt_seconds)
{
    if (!mob || !world)
        return;
    if (mob->base.is_controlled)
    {
        mob->base.velocity_x = 0.0;
        mob->base.velocity_y = 0.0;
        return;
    }

    mob_tick_mood_and_aggro(mob, dt_seconds);
    mob_scan_encounters(mob, world, dt_seconds);
    if (mob->goal != MOB_GOAL_CHASE && mob->goal != MOB_GOAL_FLEE)
        mob_consider_social_pressure(mob, world);

    // Adults will punch spiders that get too close; children flee via social pressure.
    if (mob->goal == MOB_GOAL_CHASE || mob->goal == MOB_GOAL_FLEE)
    {
        float tx, ty, tz;
        if (!mob_resolve_threat_pos(mob, world, &tx, &ty, &tz))
            mob_clear_aggro(mob);
        else if (mob->goal == MOB_GOAL_CHASE)
        {
            if (mob_try_melee(mob, world))
                return;
            mob_steer_toward(mob, tx, ty, MOB_CHASE_SPEED * 0.85f);
            return;
        }
        else
        {
            if (!mob->ai_state.has_target)
                mob_pick_flee_target(mob, world, tx, ty);
            if (mob->ai_state.has_target)
                mob_steer_toward(mob, (float)mob->ai_state.target_x + 0.5f,
                                 (float)mob->ai_state.target_y + 0.5f, MOB_CHASE_SPEED);
            return;
        }
    }

    mob->domesticate_timer -= dt_seconds;
    if (mob->domesticate_timer <= 0.0f)
    {
        mob->domesticate_timer = MOB_DOMESTICATE_PERIOD;
        mob_villager_tend_livestock(mob, world);
        if (fabs(mob->base.velocity_x) > 0.01 || fabs(mob->base.velocity_y) > 0.01)
            return; // already walking to a beast this tick
    }

    // Prefer lingering near the settlement anchor when one exists.
    if (!mob->ai_state.has_target)
    {
        int ax = 0, ay = 0, az = 0;
        if (settlement_has_anchor(world) && settlement_anchor(world, &ax, &ay, &az))
        {
            float dx = (float)ax + 0.5f - (float)mob->base.x;
            float dy = (float)ay + 0.5f - (float)mob->base.y;
            float dist = sqrtf(dx * dx + dy * dy);
            const float leash = mob->villager_kind == VILLAGER_CHILD ? 6.0f : 10.0f;
            if (dist > leash)
            {
                mob->ai_state.target_x = ax + ((int)mob->base.id % 5) - 2;
                mob->ai_state.target_y = ay + ((int)(mob->base.id / 3) % 5) - 2;
                mob->ai_state.target_z = az;
                mob->ai_state.has_target = true;
            }
        }
    }

    // Local wander — do not call mob_wanderer_update (would re-scan and double-tick mood).
    if (mob->ai_state.has_target)
    {
        double dx = ((double)mob->ai_state.target_x + 0.5) - mob->base.x;
        double dy = ((double)mob->ai_state.target_y + 0.5) - mob->base.y;
        double dist = sqrt(dx * dx + dy * dy);
        if (dist < 0.3)
        {
            mob->ai_state.has_target = false;
            mob->ai_state.retarget_in = 0.8f + (float)(mob->base.id % 5) * 0.2f;
            mob->base.velocity_x = 0.0;
            mob->base.velocity_y = 0.0;
            return;
        }
        mob_steer_toward(mob, (float)mob->ai_state.target_x + 0.5f,
                         (float)mob->ai_state.target_y + 0.5f, MOB_WANDER_SPEED);
        return;
    }

    mob->ai_state.retarget_in -= dt_seconds;
    mob->base.velocity_x = 0.0;
    mob->base.velocity_y = 0.0;
    if (mob->ai_state.retarget_in <= 0.0f)
    {
        mob_wanderer_pick_target(mob, world);
        if (!mob->ai_state.has_target)
            mob->ai_state.retarget_in = 1.0f + (float)(mob->base.id % 4) * 0.3f;
    }
}

void mob_actor_update(MobActor* mob, World* world, float dt_seconds) {
    if (!mob || !world || !mob->base.is_active) {
        return;
    }

    if (mob->base.health == 0) {
        mob_actor_tick_animation(mob, dt_seconds);
        return;
    }

    // Ice Bolt and similar effects pin a living body until the chill wears off.
    if (mob->base.freeze_ttl > 0.0f) {
        mob->base.freeze_ttl -= dt_seconds;
        if (mob->base.freeze_ttl < 0.0f)
            mob->base.freeze_ttl = 0.0f;
        mob->base.velocity_x = 0.0;
        mob->base.velocity_y = 0.0;
        mob->base.velocity_z = 0.0;
        mob_actor_tick_animation(mob, dt_seconds);
        return;
    }

    // Residual chill bleeds off once the body is free to move again.
    if (mob->base.chill > 0) {
        const float decay = 55.0f * dt_seconds;
        if (decay >= (float)mob->base.chill)
            mob->base.chill = 0;
        else
            mob->base.chill = (uint8_t)((float)mob->base.chill - decay);
    }

    // Shadow Strike poison ticks while the toxin lasts.
    if (mob->base.poison_ttl > 0.0f && mob->base.poison_dps > 0.0f) {
        mob->base.poison_accum += mob->base.poison_dps * dt_seconds;
        if (mob->base.poison_accum >= 1.0f) {
            const uint32_t dmg = (uint32_t)mob->base.poison_accum;
            mob->base.poison_accum -= (float)dmg;
            actor_apply_damage(&mob->base, dmg);
        }
        mob->base.poison_ttl -= dt_seconds;
        if (mob->base.poison_ttl < 0.0f) {
            mob->base.poison_ttl = 0.0f;
            mob->base.poison_dps = 0.0f;
            mob->base.poison_accum = 0.0f;
        }
        if (mob->base.health == 0) {
            mob_actor_tick_animation(mob, dt_seconds);
            return;
        }
    }

    if (!mob->base.is_controlled) {
        switch (mob->mob_type) {
            case MOB_TYPE_SOLVER:
                if (mob_solver_is_at_exit(mob, world)) {
                    mob->base.is_active = false;
                    mob->base.velocity_x = 0.0;
                    mob->base.velocity_y = 0.0;
                    return;
                }
                mob_solver_find_next_move(mob, world);
                break;

            case MOB_TYPE_WANDERER:
            case MOB_TYPE_SHEEP:
            case MOB_TYPE_CHICKEN:
            case MOB_TYPE_DEER:
            case MOB_TYPE_LIZARD:
            case MOB_TYPE_SPIDER:
            case MOB_TYPE_SLIME:
                mob_wanderer_update(mob, world, dt_seconds);
                break;

            case MOB_TYPE_VILLAGER:
                mob_villager_update(mob, world, dt_seconds);
                break;

            case MOB_TYPE_BIRD:
            case MOB_TYPE_BAT:
                mob_bird_update(mob, world, dt_seconds);
                break;

            default:
                break;
        }
    }

    mob_actor_tick_animation(mob, dt_seconds);
}

static int stand_z_at(World *world, int x, int y)
{
    if (!world || x < 0 || y < 0 || x >= (int)world->width || y >= (int)world->height)
        return -1;
    for (int z = (int)world->depth - 1; z >= 0; z--) {
        Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        // Tall grass and other non-blocking foliage are not ground — keep scanning down.
        if (!v || v->type == VOXEL_AIR || !world_voxel_type_blocks_movement(v->type))
            continue;
        if (z + 1 >= (int)world->depth)
            return -1;
        Voxel *above = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(z + 1));
        if (above && !world_voxel_type_blocks_movement(above->type))
            return z + 1;
        return -1;
    }
    return -1;
}

static bool world_has_named_actor(const World *world, const char *name)
{
    if (!world || !world->runtime_actors || !name)
        return false;
    for (int i = 0; i < world->runtime_actor_count; i++) {
        if (strcmp(world->runtime_actors[i].name, name) == 0)
            return true;
    }
    return false;
}

static bool collect_stand_spots(World *world, int origin_x, int origin_y,
                                int spots[][3], int max_spots, int *out_count)
{
    static const int offsets[][2] = {
        {8, 0}, {0, 8}, {-8, 0}, {0, -8},
        {6, 6}, {-6, 6}, {6, -6}, {-6, -6},
        {10, 2}, {-10, 4}, {7, -3}, {-4, 9},
        {4, 0}, {0, 4}, {-4, 0}, {0, -4},
        {12, 0}, {0, 12}, {2, 1}, {-2, 1}
    };

    int n = 0;
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]) && n < max_spots; i++) {
        int x = origin_x + offsets[i][0];
        int y = origin_y + offsets[i][1];
        if (x == origin_x && y == origin_y)
            continue;
        int z = stand_z_at(world, x, y);
        if (z < 0 || !mob_can_move_to(world, x, y, z))
            continue;
        spots[n][0] = x;
        spots[n][1] = y;
        spots[n][2] = z;
        n++;
    }

    if (n < max_spots) {
        for (int r = 2; r <= 14 && n < max_spots; r++) {
            for (int dy = -r; dy <= r && n < max_spots; dy++) {
                for (int dx = -r; dx <= r && n < max_spots; dx++) {
                    if (abs(dx) != r && abs(dy) != r)
                        continue;
                    int x = origin_x + dx;
                    int y = origin_y + dy;
                    int z = stand_z_at(world, x, y);
                    if (z < 0 || !mob_can_move_to(world, x, y, z))
                        continue;
                    bool dup = false;
                    for (int k = 0; k < n; k++) {
                        if (spots[k][0] == x && spots[k][1] == y) {
                            dup = true;
                            break;
                        }
                    }
                    if (dup)
                        continue;
                    spots[n][0] = x;
                    spots[n][1] = y;
                    spots[n][2] = z;
                    n++;
                }
            }
        }
    }

    *out_count = n;
    return n > 0;
}

// Prefer unused stand spots so settlement spawns do not stack on the same cell.
static bool take_stand_spot(int spots[][3], int spot_count, bool *used, int *cursor, int out[3])
{
    if (!spots || spot_count <= 0 || !used || !cursor || !out)
        return false;
    for (int t = 0; t < spot_count; t++)
    {
        int i = (*cursor) % spot_count;
        (*cursor)++;
        if (used[i])
            continue;
        used[i] = true;
        out[0] = spots[i][0];
        out[1] = spots[i][1];
        out[2] = spots[i][2];
        return true;
    }
    // All claimed: jitter from the next cyclic spot so bodies are not identical.
    int i = (*cursor) % spot_count;
    int ring = (*cursor) / spot_count;
    (*cursor)++;
    out[0] = spots[i][0] + (ring % 3) - 1;
    out[1] = spots[i][1] + ((ring / 3) % 3) - 1;
    out[2] = spots[i][2];
    return true;
}

static bool spawn_actor_at(World *world, MobActor *mob)
{
    if (!world || !mob)
        return false;
    // Fauna must stand inside the cell on open air — reject void / buried placements.
    if (mob->base.x < 0.0 || mob->base.y < 0.0 || mob->base.z < 0.0 ||
        mob->base.x >= (double)world->width || mob->base.y >= (double)world->height ||
        mob->base.z >= (double)world->depth)
    {
        printf("[wildlife] refusing out-of-bounds spawn '%s' at (%.1f,%.1f,%.1f)\n",
               mob->base.name, mob->base.x, mob->base.y, mob->base.z);
        mob_actor_destroy(mob);
        return false;
    }

    // Resolve terrain / actor intersection before inserting: try the requested feet, then a
    // small ring of nearby cells so breed midpoints and reused stand spots do not stack.
    double sx = mob->base.x;
    double sy = mob->base.y;
    double sz = mob->base.z;
    bool placed = false;
    static const int ring[][2] = {
        {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
        {2, 0}, {-2, 0}, {0, 2}, {0, -2},
        {2, 1}, {-2, 1}, {1, 2}, {1, -2},
        {-2, -1}, {2, -1}, {-1, 2}, {-1, -2},
        {3, 0}, {-3, 0}, {0, 3}, {0, -3},
        {2, 2}, {-2, 2}, {2, -2}, {-2, -2}};
    for (size_t t = 0; t < sizeof(ring) / sizeof(ring[0]); t++)
    {
        double tx = sx + (double)ring[t][0];
        double ty = sy + (double)ring[t][1];
        double tz = sz;
        if (tx < 0.5 || ty < 0.5 ||
            tx >= (double)world->width - 0.5 || ty >= (double)world->height - 0.5)
            continue;
        if (!mob->base.is_flying)
        {
            int stand = stand_z_at(world, (int)floor(tx), (int)floor(ty));
            if (stand < 0)
                continue;
            tz = (double)stand;
        }
        if (!mob_actor_can_occupy(world, tx, ty, tz))
            continue;
        if (!mob_actor_clear_of_actors(world, tx, ty, tz, 0))
            continue;
        mob->base.x = tx;
        mob->base.y = ty;
        mob->base.z = tz;
        placed = true;
        break;
    }
    if (!placed)
    {
        printf("[wildlife] refusing intersecting spawn '%s' at (%.1f,%.1f,%.1f)\n",
               mob->base.name, sx, sy, sz);
        mob_actor_destroy(mob);
        return false;
    }

    Actor copy = mob->base;
    copy.extra_data = mob;
    if (!world_add_runtime_actor(world, &copy)) {
        mob_actor_destroy(mob);
        return false;
    }
    return true;
}

bool mob_actor_spawn_in_world(World *world, MobActor *mob)
{
    return spawn_actor_at(world, mob);
}


static uint32_t blend_stat_u32(uint32_t a, uint32_t b, uint32_t salt, uint32_t min_v)
{
    uint32_t avg = (a + b) / 2u;
    uint32_t h = mob_hash_u32(salt ^ (avg * 2654435761u));
    int delta = (int)(h % 5u) - 2; // -2..+2
    int v = (int)avg + delta;
    if (v < (int)min_v)
        v = (int)min_v;
    if (v > 255)
        v = 255;
    return (uint32_t)v;
}

MobActor *mob_actor_create_progeny(const MobActor *parent_a, const MobActor *parent_b,
                                   double x, double y, double z)
{
    if (!parent_a || !parent_b)
        return NULL;
    if (parent_a->mob_type != parent_b->mob_type)
        return NULL;
    if (parent_a->mob_type == MOB_TYPE_NONE || parent_a->mob_type == MOB_TYPE_SOLVER)
        return NULL;

    uint32_t salt = parent_a->base.id * 31u + parent_b->base.id * 17u;
    uint32_t h = mob_hash_u32(salt);

    MobActor *child = NULL;
    if (parent_a->mob_type == MOB_TYPE_BIRD)
    {
        BirdKind kind = (h & 1u) ? parent_a->bird_kind : parent_b->bird_kind;
        if ((h % 20u) == 0u)
            kind = (BirdKind)((kind + 1) % BIRD_KIND_COUNT);
        child = mob_actor_create_bird(kind, x, y, z);
        if (!child)
            return NULL;
        snprintf(child->base.name, sizeof(child->base.name), "Fledgling");
        strncpy(child->base.description, "A young bird",
                sizeof(child->base.description) - 1);
    }
    else if (parent_a->mob_type == MOB_TYPE_SHEEP)
    {
        child = mob_actor_create_sheep(x, y, z);
        if (!child)
            return NULL;
        snprintf(child->base.name, sizeof(child->base.name), "Lamb");
    }
    else if (parent_a->mob_type == MOB_TYPE_CHICKEN)
    {
        child = mob_actor_create_chicken(x, y, z);
        if (!child)
            return NULL;
        snprintf(child->base.name, sizeof(child->base.name), "Chick");
    }
    else if (parent_a->mob_type == MOB_TYPE_BAT)
    {
        child = mob_actor_create_bat(x, y, z);
        if (!child)
            return NULL;
        snprintf(child->base.name, sizeof(child->base.name), "Pup");
    }
    else if (parent_a->mob_type == MOB_TYPE_DEER)
    {
        child = mob_actor_create_deer(x, y, z);
        if (!child)
            return NULL;
        snprintf(child->base.name, sizeof(child->base.name), "Fawn");
    }
    else if (parent_a->mob_type == MOB_TYPE_LIZARD)
    {
        child = mob_actor_create_lizard(x, y, z);
        if (!child)
            return NULL;
        snprintf(child->base.name, sizeof(child->base.name), "Hatchling");
    }
    else if (parent_a->mob_type == MOB_TYPE_SPIDER)
    {
        child = mob_actor_create_spider(x, y, z);
        if (!child)
            return NULL;
        snprintf(child->base.name, sizeof(child->base.name), "Spiderling");
    }
    else if (parent_a->mob_type == MOB_TYPE_SLIME)
    {
        child = mob_actor_create_slime(x, y, z);
        if (!child)
            return NULL;
        snprintf(child->base.name, sizeof(child->base.name), "Bloblet");
    }
    else
    {
        char name[64];
        const char *src = (h & 1u) ? parent_a->base.name : parent_b->base.name;
        snprintf(name, sizeof(name), "Cub of %.48s", src[0] ? src : "Beast");
        child = mob_actor_create(name, parent_a->mob_type, x, y, z);
        if (!child)
            return NULL;
        strncpy(child->base.description, "Offspring of two wanderers",
                sizeof(child->base.description) - 1);
    }

    child->base.strength =
        blend_stat_u32(parent_a->base.strength, parent_b->base.strength, salt ^ 1u, 1);
    child->base.dexterity =
        blend_stat_u32(parent_a->base.dexterity, parent_b->base.dexterity, salt ^ 2u, 1);
    child->base.intelligence =
        blend_stat_u32(parent_a->base.intelligence, parent_b->base.intelligence, salt ^ 3u, 1);
    child->base.wisdom =
        blend_stat_u32(parent_a->base.wisdom, parent_b->base.wisdom, salt ^ 4u, 1);
    child->base.constitution =
        blend_stat_u32(parent_a->base.constitution, parent_b->base.constitution, salt ^ 5u, 1);
    child->base.charisma =
        blend_stat_u32(parent_a->base.charisma, parent_b->base.charisma, salt ^ 6u, 1);
    child->base.luck =
        blend_stat_u32(parent_a->base.luck, parent_b->base.luck, salt ^ 7u, 1);
    // Offspring start at full derived pools from blended attributes (DOTA-style).
    actor_recalculate_stats(&child->base);
    child->base.health = actor_max_health(&child->base);
    child->base.stamina = actor_max_stamina(&child->base);
    child->base.mana = actor_max_mana(&child->base);
    child->last_health = child->base.health;
    child->mood = MOB_MOOD_CALM;
    child->goal = MOB_GOAL_WANDER;
    child->breed_cooldown = MOB_BREED_COOLDOWN;
    return child;
}

bool mob_actor_try_breed(MobActor *a, MobActor *b, World *world)
{
    if (!a || !b || !world || a == b)
        return false;
    if (a->breed_cooldown > 0.0f || b->breed_cooldown > 0.0f)
        return false;
    if (a->mood == MOB_MOOD_ANGRY || a->mood == MOB_MOOD_FEARFUL ||
        b->mood == MOB_MOOD_ANGRY || b->mood == MOB_MOOD_FEARFUL)
        return false;
    if (a->mob_type != b->mob_type)
        return false;

    const float dx = (float)(a->base.x - b->base.x);
    const float dy = (float)(a->base.y - b->base.y);
    const float dz = (float)(a->base.z - b->base.z);
    if (dx * dx + dy * dy + dz * dz > MOB_BREED_RANGE * MOB_BREED_RANGE)
        return false;

    double mx = (a->base.x + b->base.x) * 0.5;
    double my = (a->base.y + b->base.y) * 0.5;
    double mz = (a->base.z + b->base.z) * 0.5;
    MobActor *child = mob_actor_create_progeny(a, b, mx, my, mz);
    if (!child)
        return false;
    if (!spawn_actor_at(world, child))
        return false;
    a->breed_cooldown = MOB_BREED_COOLDOWN;
    b->breed_cooldown = MOB_BREED_COOLDOWN;
    return true;
}

bool world_spawn_home_mobs(World *world)
{
    if (!world)
        return false;

    const bool have_golem = world_has_named_actor(world, "Mud Golem");
    const bool have_goleling = world_has_named_actor(world, "Goleling");
    const bool have_crow = world_has_named_actor(world, "Crow");
    const bool have_pigeon = world_has_named_actor(world, "Pigeon");
    const bool have_flesh = world_has_named_actor(world, "Flesh Walker");
    if (have_golem && have_goleling && have_crow && have_pigeon && have_flesh)
        return true;

    int origin_x = (int)world->width / 2;
    int origin_y = (int)world->height / 2;
    int spots[8][3];
    int spot_count = 0;
    if (!collect_stand_spots(world, origin_x, origin_y, spots, 8, &spot_count))
        return have_golem && have_goleling && have_crow && have_pigeon && have_flesh;

    if (!have_golem) {
        int *s = spots[0];
        MobActor *golem = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER,
                                           (double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!golem)
            return false;
        strncpy(golem->base.description, "A lumbering construct of wet clay",
                sizeof(golem->base.description) - 1);
        golem->base.strength = 14;
        golem->base.dexterity = 2;
        golem->base.constitution = 16;
        actor_recalculate_stats(&golem->base);
        golem->base.health = actor_max_health(&golem->base);
        golem->base.stamina = actor_max_stamina(&golem->base);
        golem->ai_state.retarget_in = 0.4f;
        equipment_equip(&golem->equipment, ITEM_CORE_ARMOR_LEATHER);
        if (!spawn_actor_at(world, golem))
            return false;
    }

    if (!have_goleling) {
        int idx = (spot_count > 2) ? 2 : 0;
        int *s = spots[idx];
        MobActor *golem = mob_actor_create("Goleling", MOB_TYPE_WANDERER,
                                           (double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!golem)
            return false;
        strncpy(golem->base.description, "A winged construct of packed earth",
                sizeof(golem->base.description) - 1);
        golem->base.strength = 14;
        golem->base.dexterity = 4;
        golem->base.constitution = 16;
        actor_recalculate_stats(&golem->base);
        golem->base.health = actor_max_health(&golem->base);
        golem->base.stamina = actor_max_stamina(&golem->base);
        golem->ai_state.retarget_in = 0.55f;
        mob_actor_bind_mesh(golem, "goleling");
        if (!spawn_actor_at(world, golem))
            return false;
    }

    if (!have_crow) {
        int idx = (spot_count > 1) ? 1 : 0;
        int *s = spots[idx];
        MobActor *bird = mob_actor_create_bird(BIRD_KIND_CROW,
                                               (double)s[0] + 0.5, (double)s[1] + 0.5,
                                               (double)s[2]);
        if (!bird)
            return false;
        if (!spawn_actor_at(world, bird))
            return false;
    }

    if (!have_pigeon) {
        int idx = (spot_count > 3) ? 3 : 0;
        int *s = spots[idx];
        MobActor *bird = mob_actor_create_bird(BIRD_KIND_CROW,
                                               (double)s[0] + 0.5, (double)s[1] + 0.5,
                                               (double)s[2]);
        if (!bird)
            return false;
        strncpy(bird->base.name, "Pigeon", sizeof(bird->base.name) - 1);
        strncpy(bird->base.description, "A stout bird with a low, beating flight",
                sizeof(bird->base.description) - 1);
        mob_actor_bind_mesh(bird, "pigeon");
        if (!spawn_actor_at(world, bird))
            return false;
    }

    if (!have_flesh) {
        int idx = (spot_count > 4) ? 4 : 0;
        int *s = spots[idx];
        MobActor *flesh = mob_actor_create("Flesh Walker", MOB_TYPE_WANDERER,
                                           (double)s[0] + 0.5, (double)s[1] + 0.5,
                                           (double)s[2]);
        if (!flesh)
            return false;
        strncpy(flesh->base.description,
                "A humanoid scaffold of bone wrapped in procedural flesh",
                sizeof(flesh->base.description) - 1);
        flesh->base.strength = 12;
        flesh->base.dexterity = 3;
        flesh->base.constitution = 12;
        actor_recalculate_stats(&flesh->base);
        flesh->base.health = actor_max_health(&flesh->base);
        flesh->base.stamina = actor_max_stamina(&flesh->base);
        flesh->ai_state.retarget_in = 0.5f;
        mob_actor_bind_mesh(flesh, "flesh_walker");
        if (!spawn_actor_at(world, flesh))
            return false;
    }

    return world_has_named_actor(world, "Mud Golem") &&
           world_has_named_actor(world, "Goleling") &&
           world_has_named_actor(world, "Crow") &&
           world_has_named_actor(world, "Pigeon") &&
           world_has_named_actor(world, "Flesh Walker");
}

static int count_wild_prefix_actors(const World *world)
{
    if (!world || !world->runtime_actors)
        return 0;
    int n = 0;
    for (int i = 0; i < world->runtime_actor_count; i++) {
        if (strncmp(world->runtime_actors[i].name, "Wild ", 5) == 0)
            n++;
    }
    return n;
}

// Gather stand spots across the cell rather than only near the centre, so fauna reads as
// scattered wilderness life instead of a camp around spawn.
static bool collect_wilderness_stand_spots(World *world, int spots[][3], int max_spots, int *out_count)
{
    if (!world || !spots || max_spots <= 0 || !out_count)
        return false;

    static const int offsets[][2] = {
        {8, 0}, {0, 8}, {-8, 0}, {0, -8},
        {6, 6}, {-6, 6}, {6, -6}, {-6, -6},
        {14, 4}, {-14, 6}, {10, -10}, {-4, 14},
        {18, 0}, {0, 18}, {-18, 2}, {2, -18},
        {12, 12}, {-12, 12}, {12, -12}, {-12, -12},
        {20, 8}, {-8, 20}, {16, -6}, {-16, -8},
        {4, 10}, {-10, 4}, {22, -4}, {-22, 10},
        {3, 3}, {-3, 5}, {5, -3}, {-5, -5},
        {9, 2}, {-9, -2}, {2, 12}, {-12, -3},
        {15, 10}, {-15, -10}, {7, -14}, {-7, 16},
        {11, -5}, {-11, 7}, {19, -12}, {-19, 9},
        {1, 15}, {-15, 1}, {13, 7}, {-13, -9},
        {25, 3}, {-25, 5}, {3, 25}, {-5, -25},
        {28, -8}, {-28, 10}, {17, 17}, {-17, -17},
        {30, 0}, {0, 30}, {-30, 0}, {0, -30},
        {8, 16}, {-16, 8}, {24, 12}, {-12, -24}
    };

    const int origin_x = (int)world->width / 2;
    const int origin_y = (int)world->height / 2;
    int n = 0;

    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]) && n < max_spots; i++) {
        int x = origin_x + offsets[i][0];
        int y = origin_y + offsets[i][1];
        if (x < 1 || y < 1 || x >= (int)world->width - 1 || y >= (int)world->height - 1)
            continue;
        int z = stand_z_at(world, x, y);
        if (z < 0 || !mob_can_move_to(world, x, y, z))
            continue;
        bool dup = false;
        for (int k = 0; k < n; k++) {
            if (spots[k][0] == x && spots[k][1] == y) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        spots[n][0] = x;
        spots[n][1] = y;
        spots[n][2] = z;
        n++;
    }

    // Fill remaining slots with a denser grid sweep so packs have room to place.
    if (n < max_spots) {
        const int step = (world->width > 48 && world->height > 48) ? 3 : 2;
        for (int y = 2; y < (int)world->height - 2 && n < max_spots; y += step) {
            for (int x = 2; x < (int)world->width - 2 && n < max_spots; x += step) {
                int z = stand_z_at(world, x, y);
                if (z < 0 || !mob_can_move_to(world, x, y, z))
                    continue;
                bool dup = false;
                for (int k = 0; k < n; k++) {
                    if (spots[k][0] == x && spots[k][1] == y) {
                        dup = true;
                        break;
                    }
                }
                if (dup)
                    continue;
                spots[n][0] = x;
                spots[n][1] = y;
                spots[n][2] = z;
                n++;
            }
        }
    }

    *out_count = n;
    return n > 0;
}

bool world_spawn_wilderness_mobs(World *world)
{
    if (!world)
        return false;

    const uint32_t cx = world->width / 2;
    const uint32_t cy = world->height / 2;
    UniverseBiomeSample biome = universe_biome_at(world, cx, cy, -1.0f);
    UniverseBiomeId primary = biome.primary;

    // Sparse wildlife: leave room for villagers and village livestock to read clearly.
    int target_sheep = 0, target_chicken = 0, target_bat = 0;
    int target_deer = 0, target_lizard = 0, target_spider = 0, target_slime = 0;
    int target_wanderers = 1, target_birds = 3;
    BirdKind bird_a = BIRD_KIND_SPARROW, bird_b = BIRD_KIND_CROW;
    const char *bird_mesh_a = "pigeon";
    const char *bird_mesh_b = "pigeon";

    switch (primary)
    {
    case UNIVERSE_BIOME_GRASSLAND:
        target_sheep = 3;
        target_chicken = 2;
        target_deer = 2;
        target_spider = 1;
        target_wanderers = 1;
        target_birds = 3;
        bird_a = BIRD_KIND_SPARROW;
        bird_b = BIRD_KIND_SPARROW;
        break;
    case UNIVERSE_BIOME_TEMPERATE:
        target_sheep = 2;
        target_chicken = 2;
        target_deer = 2;
        target_spider = 1;
        target_wanderers = 1;
        target_birds = 3;
        bird_a = BIRD_KIND_CROW;
        bird_b = BIRD_KIND_SPARROW;
        break;
    case UNIVERSE_BIOME_BOREAL:
        target_sheep = 2;
        target_deer = 2;
        target_spider = 1;
        target_wanderers = 1;
        target_birds = 3;
        bird_a = BIRD_KIND_CROW;
        bird_b = BIRD_KIND_CROW;
        break;
    case UNIVERSE_BIOME_DESERT:
        target_lizard = 2;
        target_chicken = 1;
        target_wanderers = 1;
        target_bat = 1;
        target_birds = 2;
        bird_a = BIRD_KIND_CROW;
        bird_b = BIRD_KIND_CROW;
        break;
    case UNIVERSE_BIOME_WETLAND:
        target_chicken = 2;
        target_bat = 1;
        target_slime = 1;
        target_spider = 1;
        target_wanderers = 0;
        target_birds = 4;
        bird_a = BIRD_KIND_GULL;
        bird_b = BIRD_KIND_GULL;
        break;
    case UNIVERSE_BIOME_ALPINE:
        target_sheep = 2;
        target_deer = 1;
        target_spider = 1;
        target_bat = 1;
        target_wanderers = 0;
        target_birds = 2;
        bird_a = BIRD_KIND_CROW;
        bird_b = BIRD_KIND_CROW;
        break;
    case UNIVERSE_BIOME_TROPICAL:
        target_chicken = 2;
        target_lizard = 2;
        target_spider = 1;
        target_wanderers = 0;
        target_birds = 4;
        bird_a = BIRD_KIND_SPARROW;
        bird_b = BIRD_KIND_SPARROW;
        break;
    case UNIVERSE_BIOME_VOLCANIC:
        target_lizard = 2;
        target_spider = 1;
        target_slime = 1;
        target_wanderers = 1;
        target_bat = 1;
        target_birds = 2;
        bird_a = BIRD_KIND_CROW;
        bird_b = BIRD_KIND_CROW;
        break;
    default:
        target_sheep = 2;
        target_chicken = 1;
        target_deer = 1;
        target_spider = 1;
        break;
    }

    // Soft-weight nudges from neighboring biomes (never inflate past sparse caps).
    if (biome.weights[UNIVERSE_BIOME_GRASSLAND] > 0.25f && target_deer < 1)
        target_deer = 1;
    if (biome.weights[UNIVERSE_BIOME_WETLAND] > 0.25f && target_slime < 1)
        target_slime = 1;
    if (biome.weights[UNIVERSE_BIOME_DESERT] > 0.25f && target_lizard < 1)
        target_lizard = 1;
    if (biome.weights[UNIVERSE_BIOME_TROPICAL] > 0.25f && target_birds < 3)
        target_birds = 3;

    // Settlement keep-out: wild fauna stays clear of towns so villagers read as the focus.
    // Local scale clears wild herds (village livestock is spawned with villagers).
    // Neighboring towns still push monsters and herds away from the shared border.
    int keep_x = (int)(world->width / 2);
    int keep_y = (int)(world->height / 2);
    int keep_r = 0;
    int pressure = world->settlement_scale;
    int neighbor_dx = 0, neighbor_dy = 0, neighbor_n = 0;
    {
        const int gx = (int)(int64_t)world->universe_x;
        const int gy = (int)(int64_t)world->universe_y;
        if (pressure <= 0)
        {
            for (int dy = -1; dy <= 1; dy++)
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dy == 0)
                        continue;
                    const int s = universe_settlement_scale(gx + dx, gy + dy);
                    if (s <= 0)
                        continue;
                    neighbor_dx += dx;
                    neighbor_dy += dy;
                    neighbor_n++;
                    // Soft bleed from a neighbouring town (~half its scale).
                    const int soft = (s + 1) / 2;
                    if (soft > pressure)
                        pressure = soft;
                }
            }
        }
        if (world->settlement_scale > 0)
        {
            if (settlement_has_anchor(world))
            {
                int ax = keep_x, ay = keep_y, az = 0;
                if (settlement_anchor(world, &ax, &ay, &az))
                {
                    keep_x = ax;
                    keep_y = ay;
                }
            }
            // Clear wild fauna near the built area; radius grows with town size.
            keep_r = 10 + world->settlement_scale * 3;
            // Villagers already keep paddock animals — do not double up with Wild herds.
            target_sheep = 0;
            target_chicken = 0;
            target_deer = 0;
            target_bat = 0;
            target_lizard = 0;
            target_spider = 0;
            target_slime = 0;
            target_wanderers = 0;
            // A few distant birds only.
            if (target_birds > 2)
                target_birds = 2;
        }
        else if (pressure > 0)
        {
            // Neighbouring towns push monsters out; keep-out below handles placement.
            #define SETTLEMENT_KEEP_MONSTERS(n) \
                ((pressure >= 8) ? 0 : (((n) * (8 - pressure) + 3) / 8))
            target_spider = SETTLEMENT_KEEP_MONSTERS(target_spider);
            target_lizard = SETTLEMENT_KEEP_MONSTERS(target_lizard);
            target_slime = SETTLEMENT_KEEP_MONSTERS(target_slime);
            target_wanderers = SETTLEMENT_KEEP_MONSTERS(target_wanderers);
            target_bat = SETTLEMENT_KEEP_MONSTERS(target_bat);
            #undef SETTLEMENT_KEEP_MONSTERS
            // Cap herds near towns; spatial keep-out keeps them off the shared border.
            if (target_sheep > 2)
                target_sheep = 2;
            if (target_chicken > 2)
                target_chicken = 2;
            if (target_deer > 2)
                target_deer = 2;
            if (target_birds > 3)
                target_birds = 3;
            if (neighbor_n > 0)
            {
                // Keep-out on the border facing neighbouring settlements.
                keep_x = (int)(world->width / 2) +
                         (neighbor_dx * (int)world->width) / (3 * neighbor_n);
                keep_y = (int)(world->height / 2) +
                         (neighbor_dy * (int)world->height) / (3 * neighbor_n);
                if (keep_x < 4)
                    keep_x = 4;
                if (keep_y < 4)
                    keep_y = 4;
                if (keep_x >= (int)world->width - 4)
                    keep_x = (int)world->width - 5;
                if (keep_y >= (int)world->height - 4)
                    keep_y = (int)world->height - 5;
                keep_r = 10 + pressure * 2;
            }
        }
    }

    const int target_total =
        target_sheep + target_chicken + target_bat + target_deer + target_lizard +
        target_spider + target_slime + target_wanderers + target_birds;
    if (target_total <= 0)
        return true;
    if (count_wild_prefix_actors(world) >= target_total)
        return true;

    int spots[192][3];
    int spot_count = 0;
    if (!collect_wilderness_stand_spots(world, spots, 192, &spot_count))
        return count_wild_prefix_actors(world) > 0;

    // Consume stand spots inside the settlement / border keep-out so fauna spawn elsewhere.
    if (keep_r > 0)
    {
        const int keep_r2 = keep_r * keep_r;
        int kept = 0;
        for (int i = 0; i < spot_count; i++)
        {
            int ddx = spots[i][0] - keep_x;
            int ddy = spots[i][1] - keep_y;
            if (ddx * ddx + ddy * ddy < keep_r2)
            {
                spots[i][0] = -1000001; /* consumed */
                continue;
            }
            kept++;
        }
        (void)kept;
    }

    if (spot_count < 8)
        printf("[wildlife] warning: only %d stand spots in %ux%u cell (want %d fauna)\n",
               spot_count, world->width, world->height, target_total);

    // Unique "Wild Foo" / "Wild Foo 2" … names so flocks can exceed the label list.
    #define MAKE_WILD_NAME(buf, base, index)                                                   \
        do {                                                                                   \
            if ((index) <= 0)                                                                  \
                snprintf((buf), sizeof(buf), "%s", (base));                                    \
            else                                                                               \
                snprintf((buf), sizeof(buf), "%s %d", (base), (index) + 1);                    \
            int guard = 0;                                                                     \
            while (world_has_named_actor(world, (buf)) && guard++ < 32) {                      \
                snprintf((buf), sizeof(buf), "%s %d", (base), (index) + 1 + guard);            \
            }                                                                                  \
        } while (0)

    // Prefer a stand spot near (near_x, near_y) so same species clump; falls back to next free.
    // Copy into taken_spot before marking the slot consumed — returning &spots[i] and then
    // zeroing spots[i][0] used to spawn every animal at x≈-1e6 (inside "the void").
    int taken_spot[3];
    #define TAKE_CLUSTERED_SPOT(near_x, near_y, out_s)                                         \
        do {                                                                                   \
            int best = -1;                                                                     \
            int best_d2 = 0x7fffffff;                                                          \
            for (int si = 0; si < spot_count; si++) {                                          \
                if (spots[si][0] < -100000)                                                    \
                    continue; /* consumed */                                                   \
                double cx = (double)spots[si][0] + 0.5;                                        \
                double cy = (double)spots[si][1] + 0.5;                                        \
                double cz = (double)spots[si][2];                                              \
                if (!mob_actor_can_occupy(world, cx, cy, cz) ||                                \
                    !mob_actor_clear_of_actors(world, cx, cy, cz, 0))                          \
                    continue;                                                                  \
                int ddx = spots[si][0] - (near_x);                                             \
                int ddy = spots[si][1] - (near_y);                                             \
                int d2 = ddx * ddx + ddy * ddy;                                                \
                if (best < 0 || d2 < best_d2) {                                                \
                    best = si;                                                                 \
                    best_d2 = d2;                                                              \
                }                                                                              \
            }                                                                                  \
            if (best < 0) {                                                                    \
                (out_s) = NULL;                                                                \
            } else {                                                                           \
                taken_spot[0] = spots[best][0];                                                \
                taken_spot[1] = spots[best][1];                                                \
                taken_spot[2] = spots[best][2];                                                \
                spots[best][0] = -1000001;                                                     \
                (out_s) = taken_spot;                                                          \
            }                                                                                  \
        } while (0)

    // Prefer a stand spot that stays clear of already-placed wild fauna (spread packs/predators).
    #define TAKE_SPREAD_SPOT(near_x, near_y, min_sep, out_s)                                   \
        do {                                                                                   \
            int best = -1;                                                                     \
            int best_score = -0x7fffffff;                                                      \
            const int sep2 = (min_sep) * (min_sep);                                            \
            for (int si = 0; si < spot_count; si++) {                                           \
                if (spots[si][0] < -100000)                                                    \
                    continue;                                                                  \
                int ddx = spots[si][0] - (near_x);                                             \
                int ddy = spots[si][1] - (near_y);                                             \
                int toward = -(ddx * ddx + ddy * ddy);                                         \
                int nearest = 0x7fffffff;                                                      \
                for (int ai = 0; ai < world->runtime_actor_count; ai++) {                       \
                    Actor *a = &world->runtime_actors[ai];                                      \
                    if (strncmp(a->name, "Wild ", 5) != 0)                                     \
                        continue;                                                              \
                    int adx = spots[si][0] - (int)a->x;                                        \
                    int ady = spots[si][1] - (int)a->y;                                        \
                    int ad2 = adx * adx + ady * ady;                                           \
                    if (ad2 < nearest)                                                         \
                        nearest = ad2;                                                         \
                }                                                                              \
                if (nearest < sep2)                                                            \
                    continue;                                                                  \
                int score = nearest + toward / 8;                                              \
                if (best < 0 || score > best_score) {                                          \
                    best = si;                                                                 \
                    best_score = score;                                                        \
                }                                                                              \
            }                                                                                  \
            if (best < 0) {                                                                    \
                TAKE_CLUSTERED_SPOT(near_x, near_y, out_s);                                    \
            } else {                                                                           \
                taken_spot[0] = spots[best][0];                                                \
                taken_spot[1] = spots[best][1];                                                \
                taken_spot[2] = spots[best][2];                                                \
                spots[best][0] = -1000001;                                                     \
                (out_s) = taken_spot;                                                          \
            }                                                                                  \
        } while (0)

    // Anchor flocks toward map corners / edges so open wilderness feels spread out.
    const int qx = (int)world->width / 4;
    const int qy = (int)world->height / 4;
    const int tx = (int)world->width - qx;
    const int ty = (int)world->height - qy;

    // Sheep — one small flock away from the cell centre
    static const char *sheep_bases[] = {"Wild Sheep", "Wild Ewe", "Wild Ram"};
    int sheep_anchor_x = qx, sheep_anchor_y = qy;
    for (int i = 0; i < target_sheep; i++)
    {
        char name[64];
        MAKE_WILD_NAME(name, sheep_bases[i % 3], i);
        if (world_has_named_actor(world, name))
            continue;
        int *s = NULL;
        TAKE_CLUSTERED_SPOT(sheep_anchor_x, sheep_anchor_y, s);
        if (!s)
            break;
        if (i == 0)
        {
            sheep_anchor_x = s[0];
            sheep_anchor_y = s[1];
        }
        MobActor *mob = mob_actor_create_sheep((double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!mob)
            continue;
        strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
        strncpy(mob->base.description, "A wild sheep of the open country",
                sizeof(mob->base.description) - 1);
        mob->ai_state.retarget_in = 0.6f + (float)i * 0.25f;
        spawn_actor_at(world, mob);
    }

    // Chickens — separate flock in another corner
    static const char *chicken_bases[] = {"Wild Chicken", "Wild Hen", "Wild Rooster"};
    int chicken_anchor_x = tx, chicken_anchor_y = qy;
    for (int i = 0; i < target_chicken; i++)
    {
        char name[64];
        MAKE_WILD_NAME(name, chicken_bases[i % 3], i);
        if (world_has_named_actor(world, name))
            continue;
        int *s = NULL;
        TAKE_CLUSTERED_SPOT(chicken_anchor_x, chicken_anchor_y, s);
        if (!s)
            break;
        if (i == 0)
        {
            chicken_anchor_x = s[0];
            chicken_anchor_y = s[1];
        }
        MobActor *mob =
            mob_actor_create_chicken((double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!mob)
            continue;
        strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
        strncpy(mob->base.description, "A wild chicken scratching the soil",
                sizeof(mob->base.description) - 1);
        mob->ai_state.retarget_in = 0.4f + (float)i * 0.2f;
        spawn_actor_at(world, mob);
    }

    // Rabbits — only when a deer herd is present; small clump
    {
        const int target_rabbit = target_deer > 1 ? 1 : 0;
        static const char *rabbit_bases[] = {"Wild Rabbit", "Wild Hare"};
        int rabbit_anchor_x = qx + 2;
        int rabbit_anchor_y = ty - 2;
        for (int i = 0; i < target_rabbit; i++)
        {
            char name[64];
            MAKE_WILD_NAME(name, rabbit_bases[i % 2], i);
            if (world_has_named_actor(world, name))
                continue;
            int *s = NULL;
            TAKE_CLUSTERED_SPOT(rabbit_anchor_x, rabbit_anchor_y, s);
            if (!s)
                break;
            if (i == 0)
            {
                rabbit_anchor_x = s[0];
                rabbit_anchor_y = s[1];
            }
            MobActor *mob =
                mob_actor_create_rabbit((double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
            if (!mob)
                continue;
            strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
            strncpy(mob->base.description, "A wild rabbit of the meadows",
                    sizeof(mob->base.description) - 1);
            mob->ai_state.retarget_in = 0.3f + (float)i * 0.15f;
            spawn_actor_at(world, mob);
        }
    }

    // Bats — roost away from town keep-out
    static const char *bat_bases[] = {"Wild Bat", "Wild Flitter"};
    int bat_anchor_x = tx, bat_anchor_y = ty;
    for (int i = 0; i < target_bat; i++)
    {
        char name[64];
        MAKE_WILD_NAME(name, bat_bases[i % 2], i);
        if (world_has_named_actor(world, name))
            continue;
        int *s = NULL;
        TAKE_SPREAD_SPOT(bat_anchor_x, bat_anchor_y, 6, s);
        if (!s)
            break;
        if (i == 0)
        {
            bat_anchor_x = s[0];
            bat_anchor_y = s[1];
        }
        double bz = (double)s[2] + 2.0;
        if (bz >= (double)world->depth - 1.0)
            bz = (double)s[2];
        MobActor *mob = mob_actor_create_bat((double)s[0] + 0.5, (double)s[1] + 0.5, bz);
        if (!mob)
            continue;
        strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
        strncpy(mob->base.description, "A wild bat of shadowed ledges",
                sizeof(mob->base.description) - 1);
        mob->ai_state.retarget_in = 0.5f + (float)i * 0.3f;
        spawn_actor_at(world, mob);
    }

    // Deer — browsing herd in a far corner
    int deer_anchor_x = qx, deer_anchor_y = ty;
    for (int i = 0; i < target_deer; i++)
    {
        char name[64];
        MAKE_WILD_NAME(name, "Wild Deer", i);
        if (world_has_named_actor(world, name))
            continue;
        int *s = NULL;
        TAKE_CLUSTERED_SPOT(deer_anchor_x, deer_anchor_y, s);
        if (!s)
            break;
        if (i == 0)
        {
            deer_anchor_x = s[0];
            deer_anchor_y = s[1];
        }
        MobActor *mob = mob_actor_create_deer((double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!mob)
            continue;
        strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
        strncpy(mob->base.description, "A wild deer of the wooded country",
                sizeof(mob->base.description) - 1);
        mob->ai_state.retarget_in = 0.5f + (float)i * 0.2f;
        spawn_actor_at(world, mob);
    }

    // Elephant — rare; only when the herd is present and the cell is unsettled
    if (target_deer >= 2 && world->settlement_scale <= 0 && pressure < 3)
    {
        char name[64];
        MAKE_WILD_NAME(name, "Wild Elephant", 0);
        if (!world_has_named_actor(world, name))
        {
            int *s = NULL;
            TAKE_SPREAD_SPOT(deer_anchor_x, deer_anchor_y, 10, s);
            if (s)
            {
                MobActor *mob =
                    mob_actor_create_elephant((double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
                if (mob)
                {
                    strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
                    strncpy(mob->base.description, "A great wild elephant of the open plains",
                            sizeof(mob->base.description) - 1);
                    mob->ai_state.retarget_in = 1.2f;
                    spawn_actor_at(world, mob);
                }
            }
        }
    }

    // Lizards — solitary, well spaced
    for (int i = 0; i < target_lizard; i++)
    {
        char name[64];
        MAKE_WILD_NAME(name, "Wild Lizard", i);
        if (world_has_named_actor(world, name))
            continue;
        int *s = NULL;
        int lx = tx - (i * 5);
        int ly = qy + (i * 7);
        TAKE_SPREAD_SPOT(lx, ly, 10, s);
        if (!s)
            break;
        MobActor *mob =
            mob_actor_create_lizard((double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!mob)
            continue;
        strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
        strncpy(mob->base.description, "A wild monitor lizard",
                sizeof(mob->base.description) - 1);
        mob->ai_state.retarget_in = 0.7f + (float)i * 0.35f;
        spawn_actor_at(world, mob);
    }

    // Spiders — solitary hunters, kept apart
    for (int i = 0; i < target_spider; i++)
    {
        char name[64];
        MAKE_WILD_NAME(name, "Wild Spider", i);
        if (world_has_named_actor(world, name))
            continue;
        int *s = NULL;
        int sx = qx + (i * 11);
        int sy = ty - (i * 5);
        TAKE_SPREAD_SPOT(sx, sy, 12, s);
        if (!s)
            break;
        MobActor *mob =
            mob_actor_create_spider((double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!mob)
            continue;
        strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
        strncpy(mob->base.description, "A wild hunting spider",
                sizeof(mob->base.description) - 1);
        mob->ai_state.retarget_in = 0.35f + (float)i * 0.2f;
        spawn_actor_at(world, mob);
    }

    // Slimes — solitary
    for (int i = 0; i < target_slime; i++)
    {
        char name[64];
        MAKE_WILD_NAME(name, "Wild Slime", i);
        if (world_has_named_actor(world, name))
            continue;
        int *s = NULL;
        TAKE_SPREAD_SPOT(tx - 2, ty - 2, 10, s);
        if (!s)
            break;
        MobActor *mob =
            mob_actor_create_slime((double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!mob)
            continue;
        strncpy(mob->base.name, name, sizeof(mob->base.name) - 1);
        strncpy(mob->base.description, "A wild slime oozing across the ground",
                sizeof(mob->base.description) - 1);
        mob->ai_state.retarget_in = 1.0f + (float)i * 0.4f;
        spawn_actor_at(world, mob);
    }

    // Golem wanderers — dispersed
    static const char *wanderer_bases[] = {
        "Wild Stray", "Wild Stalker", "Wild Rover", "Wild Drifter", "Wild Nomad"};
    static const char *wanderer_descs[] = {
        "A restless creature of the open wilds",
        "A lean figure pacing the grassland",
        "A wandering beast with no home",
        "A drifting presence on the plains",
        "A nomad of the wilderness"};
    for (int i = 0; i < target_wanderers; i++)
    {
        char name[64];
        MAKE_WILD_NAME(name, wanderer_bases[i % 5], i);
        if (world_has_named_actor(world, name))
            continue;
        int *s = NULL;
        int wx = qx + (i * 13) % (tx - qx + 1);
        int wy = qy + (i * 9) % (ty - qy + 1);
        TAKE_SPREAD_SPOT(wx, wy, 14, s);
        if (!s)
            break;
        MobActor *mob = mob_actor_create(name, MOB_TYPE_WANDERER,
                                         (double)s[0] + 0.5, (double)s[1] + 0.5, (double)s[2]);
        if (!mob)
            continue;
        strncpy(mob->base.description, wanderer_descs[i % 5],
                sizeof(mob->base.description) - 1);
        mob->base.strength = 8;
        mob->base.dexterity = 4;
        mob->base.constitution = 10;
        actor_recalculate_stats(&mob->base);
        mob->base.health = actor_max_health(&mob->base);
        mob->base.stamina = actor_max_stamina(&mob->base);
        mob->ai_state.retarget_in = 0.8f + (float)i * 0.35f;
        mob_actor_bind_mesh(mob, "goleling");
        spawn_actor_at(world, mob);
    }

    // Birds — two surface flocks near opposite edges, perched on open ground.
    static const char *bird_bases[] = {"Wild Crow", "Wild Pigeon", "Wild Sparrow", "Wild Gull"};
    int bird_anchor_x = tx, bird_anchor_y = ty;
    const int bird_flock2 = target_birds / 2;
    for (int i = 0; i < target_birds; i++)
    {
        if (i == bird_flock2)
        {
            bird_anchor_x = qx;
            bird_anchor_y = qy;
        }
        BirdKind kind = (i < bird_flock2) ? bird_a : bird_b;
        const char *base = bird_bases[0];
        const char *mesh = bird_mesh_a;
        if (kind == BIRD_KIND_SPARROW)
        {
            base = bird_bases[2];
            mesh = "pigeon";
        }
        else if (kind == BIRD_KIND_GULL)
        {
            base = bird_bases[3];
            mesh = "pigeon";
        }
        else if (kind == BIRD_KIND_CROW)
        {
            base = (i % 2 == 1) ? bird_bases[1] : bird_bases[0];
            mesh = bird_mesh_b;
        }

        char unique[64];
        MAKE_WILD_NAME(unique, base, i);
        if (world_has_named_actor(world, unique))
            continue;
        int *s = NULL;
        TAKE_CLUSTERED_SPOT(bird_anchor_x, bird_anchor_y, s);
        if (!s)
            break;
        // Reject any spot that somehow left the world (defense in depth after the consume bug).
        if (s[0] < 1 || s[1] < 1 || s[0] >= (int)world->width - 1 ||
            s[1] >= (int)world->height - 1 || s[2] < 1 || s[2] >= (int)world->depth)
            continue;
        if (i == 0 || i == bird_flock2)
        {
            bird_anchor_x = s[0];
            bird_anchor_y = s[1];
        }
        MobActor *bird = mob_actor_create_bird(kind,
                                               (double)s[0] + 0.5, (double)s[1] + 0.5,
                                               (double)s[2]);
        if (!bird)
            continue;
        strncpy(bird->base.name, unique, sizeof(bird->base.name) - 1);
        strncpy(bird->base.description, "A wild bird of the open country",
                sizeof(bird->base.description) - 1);
        mob_actor_bind_mesh(bird, mesh);
        bird->ai_state.retarget_in = 1.0f + (float)i * 0.35f;
        bird->ai_state.perch_z = (float)s[2];
        bird->base.is_flying = false;
        bird->ai_state.bird_phase = BIRD_PHASE_PERCH;
        spawn_actor_at(world, bird);
    }

    // Trade-route caravans: a traveling merchant on road/trade corridor cells.
    if (world->universe_z == 0 && world->settlement_scale <= 0)
    {
        const int gx = (int)(int64_t)world->universe_x;
        const int gy = (int)(int64_t)world->universe_y;
        SettlementRoadType rt = SETTLEMENT_ROAD_NONE;
        if (universe_settlement_trade_route(gx, gy, &rt, NULL, NULL, NULL, NULL))
        {
            char cname[64];
            snprintf(cname, sizeof(cname), "Caravan Merchant");
            if (!world_has_named_actor(world, cname))
            {
                int *s = NULL;
                TAKE_CLUSTERED_SPOT((int)world->width / 2, (int)world->height / 2, s);
                if (s)
                {
                    MobActor *trader = mob_actor_create_villager(VILLAGER_MALE, (double)s[0] + 0.5,
                                                                 (double)s[1] + 0.5, (double)s[2]);
                    if (trader)
                    {
                        strncpy(trader->base.name, cname, sizeof(trader->base.name) - 1);
                        snprintf(trader->base.description, sizeof(trader->base.description),
                                 "A merchant traveling the %s road between settlements",
                                 settlement_road_type_name(rt));
                        trader->profession = VILLAGER_JOB_MERCHANT;
                        trader->trait_greed = 40;
                        trader->ai_state.retarget_in = 1.5f;
                        spawn_actor_at(world, trader);
                    }
                }
            }
        }
    }

    #undef TAKE_SPREAD_SPOT
    #undef TAKE_CLUSTERED_SPOT
    #undef MAKE_WILD_NAME

    {
        const int spawned = count_wild_prefix_actors(world);
        printf("[wildlife] %d Wild fauna present (target %d, spots %d) at (%lld,%lld) biome=%d\n",
               spawned, target_total, spot_count,
               (long long)world->universe_x, (long long)world->universe_y, (int)primary);
        return spawned > 0;
    }
}

static int count_villager_actors(const World *world)
{
    if (!world || !world->runtime_actors)
        return 0;
    int n = 0;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        if (mob_actor_is_villager(&world->runtime_actors[i]))
            n++;
    }
    return n;
}

bool world_spawn_settlement_villagers(World *world)
{
    if (!world)
        return false;

    const int scale = world->settlement_scale;
    if (scale < 3)
        return false;

    if (count_villager_actors(world) > 0)
        return true;

    household_registry_clear(world);

    int ax = (int)world->width / 2;
    int ay = (int)world->height / 2;
    int az = 0;
    if (settlement_has_anchor(world) && settlement_anchor(world, &ax, &ay, &az))
    {
        (void)az;
    }

    int spots[48][3];
    int spot_count = 0;
    if (!collect_stand_spots(world, ax, ay, spots, 48, &spot_count) || spot_count == 0)
        return false;
    bool spot_used[48];
    memset(spot_used, 0, sizeof(spot_used));
    int spot_i = 0;
    int claimed[3];

    static const char *male_names[] = {
        "Alden", "Bram", "Cedric", "Doran", "Edric", "Finn", "Gareth", "Hugo"};
    static const char *female_names[] = {
        "Astrid", "Brynn", "Cora", "Dana", "Elsa", "Freya", "Greta", "Hilda"};
    static const char *child_names[] = {
        "Pip", "Tess", "Wren", "Ned", "Lia", "Otto", "Mia", "Kit"};
    static const char *surnames[] = {
        "Reed", "Ashford", "Thorn", "Vale", "Marsh", "Flint", "Brook", "Hawke",
        "Stone", "Willow"};
    static const VillagerProfession adult_jobs[] = {
        VILLAGER_JOB_FARMER, VILLAGER_JOB_SHEPHERD, VILLAGER_JOB_MILLER,
        VILLAGER_JOB_BAKER, VILLAGER_JOB_GUARD, VILLAGER_JOB_HEALER,
        VILLAGER_JOB_MERCHANT, VILLAGER_JOB_BLACKSMITH};

    const int n_male = 1 + scale / 3;
    const int n_female = 1 + scale / 3;
    const int n_child = scale > 5 ? (scale / 3) : 1;
    const int households = (n_male < n_female) ? n_male : n_female;

    uint32_t salt = (uint32_t)world->universe_x * 0x9e3779b1u ^
                    (uint32_t)world->universe_y * 0x85ebca6bu ^
                    (uint32_t)(scale * 0xC2B2AE3Du);

    const Chronicle *chron = NULL;
    if (world->universe_context && world->universe_context->chronicle_ready)
        chron = &world->universe_context->chronicle;

    uint32_t settlement_id = chronicle_site_id_for_cell((int)world->universe_x,
                                                       (int)world->universe_y);
    if (!settlement_id)
        settlement_id = mob_hash_u32(salt ^ 0x51Eu);
    uint32_t civ_id = 0;
    char site_label[64];
    site_label[0] = '\0';
    if (chron)
    {
        const ChronicleSite *site = chronicle_site_at(chron, (int)world->universe_x,
                                                     (int)world->universe_y);
        if (site)
        {
            settlement_id = site->id;
            civ_id = site->civ_id;
            chronicle_site_label(chron, (int)world->universe_x, (int)world->universe_y,
                                 site_label, sizeof(site_label));
            // Ruined sites: skip villagers (fauna/unique beasts only later).
            if (site->ruin || site->kind == CHRONICLE_SITE_RUIN)
                return false;
        }
    }
    if (!site_label[0])
        snprintf(site_label, sizeof(site_label), "%s", settlement_scale_label(scale));
    settlement_roster_bind_site(world, settlement_id, (int)world->universe_x,
                                (int)world->universe_y, civ_id, site_label);

    // Keep pointers so we can link parents and seed inter-family stories after spawn.
    MobActor *males[16] = {0};
    MobActor *females[16] = {0};
    MobActor *children[16] = {0};
    uint32_t family_ids[16] = {0};
    char family_names[16][MOB_NAME_PART_MAX];
    memset(family_names, 0, sizeof(family_names));

    int spawned = 0;
    int male_n = 0, female_n = 0, child_n = 0;

    for (int h = 0; h < households && h < 16; h++)
    {
        uint32_t hs = mob_hash_u32(salt ^ (uint32_t)(h + 1) * 747796405u);
        const char *surname = surnames[hs % (sizeof(surnames) / sizeof(surnames[0]))];
        for (int tries = 0; tries < 8; tries++)
        {
            bool taken = false;
            for (int p = 0; p < h; p++)
            {
                if (strcmp(family_names[p], surname) == 0)
                {
                    taken = true;
                    break;
                }
            }
            if (!taken)
                break;
            hs = mob_hash_u32(hs + 1u);
            surname = surnames[hs % (sizeof(surnames) / sizeof(surnames[0]))];
        }
        strncpy(family_names[h], surname, MOB_NAME_PART_MAX - 1);
        family_ids[h] = mob_hash_u32(hs ^ 0xA5A5A5A5u);
        if (family_ids[h] == 0)
            family_ids[h] = 1u + (uint32_t)h;

        if (!take_stand_spot(spots, spot_count, spot_used, &spot_i, claimed))
            break;
        MobActor *dad = mob_actor_create_villager(VILLAGER_MALE,
                                                  (double)claimed[0] + 0.5,
                                                  (double)claimed[1] + 0.5,
                                                  (double)claimed[2]);
        if (dad)
        {
            const char *given = male_names[(hs >> 4) % (sizeof(male_names) / sizeof(male_names[0]))];
            VillagerProfession job = adult_jobs[(hs >> 8) % (sizeof(adult_jobs) / sizeof(adult_jobs[0]))];
            // Prefer an occupation that matches a placed building (workplace / residence).
            if (world->settlement_placed_count > 0)
            {
                int bi = (int)((hs >> 8) % (uint32_t)world->settlement_placed_count);
                job = (VillagerProfession)world->settlement_placed[bi].occupation;
                if (job == VILLAGER_JOB_NONE || job == VILLAGER_JOB_APPRENTICE)
                    job = adult_jobs[(hs >> 8) % (sizeof(adult_jobs) / sizeof(adult_jobs[0]))];
            }
            dad->family_id = family_ids[h];
            mob_actor_set_settlement_id(dad, settlement_id);
            // Settlements of size 3+ always include a shopkeeper (first household head).
            if (h == 0 && scale >= 3)
                job = VILLAGER_JOB_MERCHANT;
            mob_actor_set_villager_identity(dad, given, surname, job);
            dad->ai_state.retarget_in = 0.4f + (float)h * 0.15f;
            if (spawn_actor_at(world, dad))
            {
                males[male_n++] = dad;
                spawned++;
                household_register_family(world, family_ids[h], surname, settlement_id,
                                          dad->base.id);
                household_add_member(world, family_ids[h], dad->base.id);
            }
            else
                dad = NULL;
        }

        if (!take_stand_spot(spots, spot_count, spot_used, &spot_i, claimed))
            break;
        MobActor *mom = mob_actor_create_villager(VILLAGER_FEMALE,
                                                  (double)claimed[0] + 0.5,
                                                  (double)claimed[1] + 0.5,
                                                  (double)claimed[2]);
        if (mom)
        {
            const char *given = female_names[(hs >> 6) % (sizeof(female_names) / sizeof(female_names[0]))];
            VillagerProfession job = adult_jobs[(hs >> 10) % (sizeof(adult_jobs) / sizeof(adult_jobs[0]))];
            if (world->settlement_placed_count > 0)
            {
                int bi = (int)((hs >> 12) % (uint32_t)world->settlement_placed_count);
                job = (VillagerProfession)world->settlement_placed[bi].occupation;
                if (job == VILLAGER_JOB_NONE || job == VILLAGER_JOB_APPRENTICE)
                    job = adult_jobs[(hs >> 10) % (sizeof(adult_jobs) / sizeof(adult_jobs[0]))];
            }
            if (dad && job == dad->profession)
                job = adult_jobs[(job + 1) % (sizeof(adult_jobs) / sizeof(adult_jobs[0]))];
            mom->family_id = family_ids[h];
            mob_actor_set_settlement_id(mom, settlement_id);
            mob_actor_set_villager_identity(mom, given, surname, job);
            mom->ai_state.retarget_in = 0.5f + (float)h * 0.15f;
            if (spawn_actor_at(world, mom))
            {
                females[female_n++] = mom;
                spawned++;
                household_register_family(world, family_ids[h], surname, settlement_id,
                                          dad ? dad->base.id : mom->base.id);
                household_add_member(world, family_ids[h], mom->base.id);
            }
            else
                mom = NULL;
        }

        if (dad && mom)
        {
            mob_actor_reputation_adjust(dad, mom->base.id, MOB_FAMILY_AFFINITY - 10);
            mob_actor_reputation_adjust(mom, dad->base.id, MOB_FAMILY_AFFINITY - 10);
            mob_actor_set_relation(dad, mom->base.id, MOB_REL_KIN);
            mob_actor_set_relation(mom, dad->base.id, MOB_REL_KIN);
        }
    }

    for (int i = male_n; i < n_male && male_n < 16; i++)
    {
        uint32_t hs = mob_hash_u32(salt ^ 0x1111u ^ (uint32_t)(i + 3) * 2246822519u);
        if (!take_stand_spot(spots, spot_count, spot_used, &spot_i, claimed))
            break;
        MobActor *v = mob_actor_create_villager(VILLAGER_MALE,
                                                (double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                                (double)claimed[2]);
        if (!v)
            continue;
        // Join an existing family when possible; otherwise form a new household.
        int join = (households > 0) ? (i % households) : -1;
        const char *surname;
        uint32_t fid;
        if (join >= 0 && family_ids[join] != 0)
        {
            surname = family_names[join];
            fid = family_ids[join];
        }
        else
        {
            surname = surnames[hs % (sizeof(surnames) / sizeof(surnames[0]))];
            fid = mob_hash_u32(hs ^ 0xBEEFu);
            if (fid == 0)
                fid = 100u + (uint32_t)i;
        }
        v->family_id = fid;
        mob_actor_set_settlement_id(v, settlement_id);
        mob_actor_set_villager_identity(
            v, male_names[(hs >> 3) % (sizeof(male_names) / sizeof(male_names[0]))],
            surname, adult_jobs[hs % (sizeof(adult_jobs) / sizeof(adult_jobs[0]))]);
        v->ai_state.retarget_in = 0.6f + (float)i * 0.1f;
        if (spawn_actor_at(world, v))
        {
            males[male_n++] = v;
            spawned++;
            household_register_family(world, fid, surname, settlement_id, v->base.id);
            household_add_member(world, fid, v->base.id);
        }
    }
    for (int i = female_n; i < n_female && female_n < 16; i++)
    {
        uint32_t hs = mob_hash_u32(salt ^ 0x2222u ^ (uint32_t)(i + 5) * 3266489917u);
        if (!take_stand_spot(spots, spot_count, spot_used, &spot_i, claimed))
            break;
        MobActor *v = mob_actor_create_villager(VILLAGER_FEMALE,
                                                (double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                                (double)claimed[2]);
        if (!v)
            continue;
        int join = (households > 0) ? (i % households) : -1;
        const char *surname;
        uint32_t fid;
        if (join >= 0 && family_ids[join] != 0)
        {
            surname = family_names[join];
            fid = family_ids[join];
        }
        else
        {
            surname = surnames[hs % (sizeof(surnames) / sizeof(surnames[0]))];
            fid = mob_hash_u32(hs ^ 0xCAFEu);
            if (fid == 0)
                fid = 200u + (uint32_t)i;
        }
        v->family_id = fid;
        mob_actor_set_settlement_id(v, settlement_id);
        mob_actor_set_villager_identity(
            v, female_names[(hs >> 3) % (sizeof(female_names) / sizeof(female_names[0]))],
            surname, adult_jobs[hs % (sizeof(adult_jobs) / sizeof(adult_jobs[0]))]);
        v->ai_state.retarget_in = 0.7f + (float)i * 0.1f;
        if (spawn_actor_at(world, v))
        {
            females[female_n++] = v;
            spawned++;
            household_register_family(world, fid, surname, settlement_id, v->base.id);
            household_add_member(world, fid, v->base.id);
        }
    }

    // Seed adult gossip about other families / chronicle events.
    for (int i = 0; i < male_n; i++)
    {
        MobActor *self = males[i];
        if (!self)
            continue;
        for (int j = 0; j < households && j < 16; j++)
        {
            if (family_ids[j] == 0 || family_ids[j] == self->family_id)
                continue;
            char tale[MOB_STORY_TEXT_MAX];
            int8_t sent = (int8_t)(((int)(mob_hash_u32(self->base.id ^ family_ids[j]) % 41u)) - 20);
            if (sent >= 0)
                snprintf(tale, sizeof(tale), "The %s family keeps a fair hearth.",
                         family_names[j]);
            else
                snprintf(tale, sizeof(tale), "The %s lot drive a hard bargain.",
                         family_names[j]);
            mob_actor_add_story(self, 0, family_ids[j], sent, tale);
            break;
        }
        if (chron)
        {
            const ChronicleEvent *evs[6];
            int ne = chronicle_query_by_site(chron, settlement_id, evs, 6);
            char buf[CHRONICLE_LINE_MAX];
            for (int ei = ne - 1; ei >= 0; ei--)
            {
                if (evs[ei]->type == CHRONICLE_EV_SITE_FOUND ||
                    evs[ei]->type == CHRONICLE_EV_CIV_FOUND)
                    continue;
                chronicle_format_line(chron, evs[ei], buf, sizeof(buf));
                if (buf[0])
                {
                    char tale[MOB_STORY_TEXT_MAX];
                    snprintf(tale, sizeof(tale), "%s", buf);
                    int8_t sent = 0;
                    if (evs[ei]->type == CHRONICLE_EV_WAR_RAID ||
                        evs[ei]->type == CHRONICLE_EV_BEAST_ATTACK)
                        sent = -20;
                    else if (evs[ei]->type == CHRONICLE_EV_PEACE)
                        sent = 15;
                    mob_actor_add_story(self, 0, 0, sent, tale);
                    break;
                }
            }
        }
        if (female_n > 0)
        {
            MobActor *other = females[(i + 1) % female_n];
            if (other && other->family_id != self->family_id)
            {
                char tale[MOB_STORY_TEXT_MAX];
                snprintf(tale, sizeof(tale), "%s %s is a steady %s.",
                         other->given_name, other->family_name,
                         villager_profession_name(other->profession));
                mob_actor_add_story(self, other->base.id, other->family_id, 12, tale);
                if (self->trait_wrath > 30)
                    mob_actor_set_relation(self, other->base.id, MOB_REL_RIVAL);
            }
        }
    }
    for (int i = 0; i < female_n; i++)
    {
        MobActor *self = females[i];
        if (!self)
            continue;
        for (int j = 0; j < households && j < 16; j++)
        {
            if (family_ids[j] == 0 || family_ids[j] == self->family_id)
                continue;
            char tale[MOB_STORY_TEXT_MAX];
            snprintf(tale, sizeof(tale), "I heard the %ss helped raise the mill.",
                     family_names[j]);
            mob_actor_add_story(self, 0, family_ids[j], 8, tale);
            break;
        }
        if (chron && self->profession == VILLAGER_JOB_HEALER)
        {
            const ChronicleEvent *evs[4];
            int ne = chronicle_query_by_site(chron, settlement_id, evs, 4);
            for (int ei = 0; ei < ne; ei++)
            {
                if (evs[ei]->type == CHRONICLE_EV_PLAGUE)
                {
                    char buf[CHRONICLE_LINE_MAX];
                    chronicle_format_line(chron, evs[ei], buf, sizeof(buf));
                    mob_actor_add_story(self, 0, 0, -10, buf);
                    break;
                }
            }
        }
        if (male_n > 0)
        {
            MobActor *other = males[(i + 2) % male_n];
            if (other && !mob_actor_are_kin(self, other))
            {
                char tale[MOB_STORY_TEXT_MAX];
                snprintf(tale, sizeof(tale), "%s %s watches the road at dusk.",
                         other->given_name, other->family_name);
                mob_actor_add_story(self, other->base.id, other->family_id, 6, tale);
            }
        }
    }

    for (int i = 0; i < n_child && child_n < 16; i++)
    {
        int house = (households > 0) ? (i % households) : 0;
        uint32_t hs = mob_hash_u32(salt ^ 0x3333u ^ (uint32_t)(i + 7) * 668265263u);
        if (!take_stand_spot(spots, spot_count, spot_used, &spot_i, claimed))
            break;
        MobActor *kid = mob_actor_create_villager(VILLAGER_CHILD,
                                                  (double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                                  (double)claimed[2]);
        if (!kid)
            continue;
        const char *surname = (house < households && family_names[house][0])
                                  ? family_names[house]
                                  : surnames[hs % (sizeof(surnames) / sizeof(surnames[0]))];
        if (house < households)
            kid->family_id = family_ids[house];
        else
        {
            kid->family_id = mob_hash_u32(hs);
            if (kid->family_id == 0)
                kid->family_id = 300u + (uint32_t)i;
        }
        mob_actor_set_settlement_id(kid, settlement_id);
        mob_actor_set_villager_identity(
            kid, child_names[hs % (sizeof(child_names) / sizeof(child_names[0]))],
            surname, VILLAGER_JOB_APPRENTICE);
        kid->ai_state.retarget_in = 0.3f + (float)i * 0.2f;
        if (!spawn_actor_at(world, kid))
            continue;
        children[child_n++] = kid;
        spawned++;
        household_register_family(world, kid->family_id, surname, settlement_id, 0);
        household_add_member(world, kid->family_id, kid->base.id);

        MobActor *dad = (house < male_n) ? males[house] : NULL;
        MobActor *mom = (house < female_n) ? females[house] : NULL;
        if (dad)
        {
            mob_actor_link_parent(kid, dad);
            mob_actor_set_relation(kid, dad->base.id, MOB_REL_KIN);
        }
        if (mom)
        {
            mob_actor_link_parent(kid, mom);
            mob_actor_set_relation(kid, mom->base.id, MOB_REL_KIN);
        }
    }

    const int livestock = scale >= 6 ? 8 : (scale >= 4 ? 5 : 3);
    for (int i = 0; i < livestock; i++)
    {
        if (!take_stand_spot(spots, spot_count, spot_used, &spot_i, claimed))
            break;
        MobActor *animal = NULL;
        char name[64];
        const int kind = i % 6;
        if (kind == 0)
        {
            animal = mob_actor_create_sheep((double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                            (double)claimed[2]);
            snprintf(name, sizeof(name), "Village Sheep %d", i / 6 + 1);
        }
        else if (kind == 1)
        {
            animal = mob_actor_create_chicken((double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                              (double)claimed[2]);
            snprintf(name, sizeof(name), "Village Hen %d", i / 6 + 1);
        }
        else if (kind == 2)
        {
            animal = mob_actor_create_cow((double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                          (double)claimed[2]);
            snprintf(name, sizeof(name), "Village Cow %d", i / 6 + 1);
        }
        else if (kind == 3)
        {
            animal = mob_actor_create_pig((double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                          (double)claimed[2]);
            snprintf(name, sizeof(name), "Village Pig %d", i / 6 + 1);
        }
        else if (kind == 4)
        {
            animal = mob_actor_create_dog((double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                          (double)claimed[2]);
            snprintf(name, sizeof(name), "Village Dog %d", i / 6 + 1);
        }
        else
        {
            animal = mob_actor_create_cat((double)claimed[0] + 0.5, (double)claimed[1] + 0.5,
                                          (double)claimed[2]);
            snprintf(name, sizeof(name), "Village Cat %d", i / 6 + 1);
        }
        if (!animal)
            continue;
        if (world_has_named_actor(world, name))
        {
            mob_actor_destroy(animal);
            continue;
        }
        strncpy(animal->base.name, name, sizeof(animal->base.name) - 1);
        strncpy(animal->base.description, "A settlement animal, half-tame already",
                sizeof(animal->base.description) - 1);
        animal->ai_state.retarget_in = 0.5f + (float)i * 0.3f;
        spawn_actor_at(world, animal);
    }

    // Unique lair beast from chronicle (Phase 6).
    if (chron)
    {
        const ChronicleSite *site = chronicle_site_at(chron, (int)world->universe_x,
                                                     (int)world->universe_y);
        if (site && (site->has_lair || site->kind == CHRONICLE_SITE_LAIR))
        {
            if (take_stand_spot(spots, spot_count, spot_used, &spot_i, claimed))
            {
                MobActor *beast = mob_actor_create_spider((double)claimed[0] + 0.5,
                                                          (double)claimed[1] + 0.5,
                                                          (double)claimed[2]);
                if (beast)
                {
                    beast->unique_id = site->id ^ 0xBEA57u;
                    snprintf(beast->base.name, sizeof(beast->base.name), "Forgotten Maw");
                    snprintf(beast->base.description, sizeof(beast->base.description),
                             "A unique beast bound to this lair's legends");
                    beast->base.strength += 8;
                    beast->base.constitution += 10;
                    actor_recalculate_stats(&beast->base);
                    beast->base.health = actor_max_health(&beast->base);
                    spawn_actor_at(world, beast);
                }
            }
        }
    }

    (void)children;

    // Guarantee a shopkeeper even if building occupations overrode the first head.
    if (scale >= 3 && spawned > 0)
    {
        bool has_merchant = false;
        MobActor *promote = NULL;
        for (int i = 0; i < world->runtime_actor_count; i++)
        {
            Actor *a = &world->runtime_actors[i];
            if (!mob_actor_is_villager(a) || a->health == 0)
                continue;
            MobActor *m = (MobActor *)a->extra_data;
            if (!m || m->villager_kind == VILLAGER_CHILD)
                continue;
            if (m->profession == VILLAGER_JOB_MERCHANT)
            {
                has_merchant = true;
                break;
            }
            if (!promote)
                promote = m;
        }
        if (!has_merchant && promote)
        {
            mob_actor_set_villager_identity(promote, promote->given_name, promote->family_name,
                                           VILLAGER_JOB_MERCHANT);
        }
    }

    return spawned > 0;
}
