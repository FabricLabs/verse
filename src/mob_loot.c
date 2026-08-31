#include "mob_loot.h"

#include "item.h"

#include <string.h>

typedef struct {
    ItemId id;
    uint16_t min_count;
    uint16_t max_count;
    uint8_t chance_pct; // 0..100
    ItemMaterial material; // ITEM_MAT_NONE → catalog default
    uint8_t quality_min;
    uint8_t quality_max;
} MobLootEntry;

typedef struct {
    const MobLootEntry *entries;
    int count;
} MobLootTable;

static uint32_t loot_mix(uint32_t a, uint32_t b)
{
    uint32_t x = a * 1664525u + b + 1013904223u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    return x;
}

static uint32_t loot_next(uint32_t *rng)
{
    *rng = loot_mix(*rng, 0x9E3779B9u);
    return *rng;
}

static uint8_t loot_chance(uint32_t *rng)
{
    return (uint8_t)(loot_next(rng) % 100u);
}

static uint16_t loot_range(uint32_t *rng, uint16_t lo, uint16_t hi)
{
    if (hi <= lo)
        return lo;
    return (uint16_t)(lo + (loot_next(rng) % (uint32_t)(hi - lo + 1u)));
}

static void equip_id(MobActor *mob, ItemId id)
{
    if (!mob || id == ITEM_NONE)
        return;
    equipment_equip(&mob->equipment, id);
}

static void bag_count(MobActor *mob, ItemId id, uint16_t count)
{
    if (!mob || id == ITEM_NONE || count == 0)
        return;
    inventory_add(&mob->base.inventory, id, count);
}

static void bag_gear(MobActor *mob, ItemId id, ItemMaterial mat, uint8_t quality)
{
    if (!mob || id == ITEM_NONE)
        return;
    ItemStack stack = item_stack_make_gear(id, mat, quality);
    inventory_add_stack(&mob->base.inventory, &stack);
}

static void apply_table(Actor *actor, const MobLootTable *table, uint32_t *rng)
{
    if (!actor || !table || !table->entries)
        return;
    for (int i = 0; i < table->count; i++)
    {
        const MobLootEntry *e = &table->entries[i];
        if (loot_chance(rng) >= e->chance_pct)
            continue;
        uint16_t n = loot_range(rng, e->min_count, e->max_count);
        if (n == 0)
            continue;
        const ItemDef *def = item_def(e->id);
        if (def->stackable)
        {
            inventory_add(&actor->inventory, e->id, n);
            continue;
        }
        for (uint16_t k = 0; k < n; k++)
        {
            uint8_t qmin = e->quality_min ? e->quality_min : 30;
            uint8_t qmax = e->quality_max ? e->quality_max : 70;
            uint8_t q = (uint8_t)loot_range(rng, qmin, qmax);
            ItemMaterial mat =
                e->material != ITEM_MAT_NONE ? e->material : def->default_material;
            ItemStack stack = item_stack_make_gear(e->id, mat, q);
            inventory_add_stack(&actor->inventory, &stack);
        }
    }
}

static const MobLootEntry s_sheep_loot[] = {
    {ITEM_WOOL, 1, 3, 100, ITEM_MAT_WOOL, 0, 0},
    {ITEM_RAW_MEAT, 1, 2, 85, ITEM_MAT_NONE, 0, 0},
    {ITEM_HIDE, 0, 1, 40, ITEM_MAT_HIDE, 0, 0},
    {ITEM_BONE, 0, 2, 35, ITEM_MAT_BONE, 0, 0},
};
static const MobLootEntry s_chicken_loot[] = {
    {ITEM_FEATHER, 1, 4, 100, ITEM_MAT_FEATHER, 0, 0},
    {ITEM_RAW_MEAT, 1, 1, 80, ITEM_MAT_NONE, 0, 0},
    {ITEM_EGG, 0, 2, 45, ITEM_MAT_NONE, 0, 0},
    {ITEM_BONE, 0, 1, 25, ITEM_MAT_BONE, 0, 0},
};
static const MobLootEntry s_deer_loot[] = {
    {ITEM_HIDE, 1, 2, 95, ITEM_MAT_HIDE, 0, 0},
    {ITEM_RAW_MEAT, 2, 4, 100, ITEM_MAT_NONE, 0, 0},
    {ITEM_BONE, 1, 3, 70, ITEM_MAT_BONE, 0, 0},
};
static const MobLootEntry s_lizard_loot[] = {
    {ITEM_HIDE, 1, 1, 80, ITEM_MAT_HIDE, 0, 0},
    {ITEM_RAW_MEAT, 1, 2, 75, ITEM_MAT_NONE, 0, 0},
    {ITEM_BONE, 0, 2, 40, ITEM_MAT_BONE, 0, 0},
};
static const MobLootEntry s_spider_loot[] = {
    {ITEM_FANG, 1, 2, 90, ITEM_MAT_BONE, 0, 0},
    {ITEM_VENOM_SAC, 1, 1, 70, ITEM_MAT_VENOM, 0, 0},
    {ITEM_RAW_MEAT, 0, 1, 30, ITEM_MAT_NONE, 0, 0},
};
static const MobLootEntry s_slime_loot[] = {
    {ITEM_SLIME_GEL, 1, 3, 100, ITEM_MAT_SLIME, 0, 0},
    {ITEM_POTION, 0, 1, 15, ITEM_MAT_NONE, 0, 0},
};
static const MobLootEntry s_bird_loot[] = {
    {ITEM_FEATHER, 1, 3, 100, ITEM_MAT_FEATHER, 0, 0},
    {ITEM_RAW_MEAT, 0, 1, 55, ITEM_MAT_NONE, 0, 0},
    {ITEM_BONE, 0, 1, 30, ITEM_MAT_BONE, 0, 0},
};
static const MobLootEntry s_bat_loot[] = {
    {ITEM_FANG, 0, 1, 50, ITEM_MAT_BONE, 0, 0},
    {ITEM_RAW_MEAT, 0, 1, 40, ITEM_MAT_NONE, 0, 0},
};
static const MobLootEntry s_pet_loot[] = {
    {ITEM_HIDE, 0, 1, 50, ITEM_MAT_HIDE, 0, 0},
    {ITEM_RAW_MEAT, 1, 2, 70, ITEM_MAT_NONE, 0, 0},
    {ITEM_BONE, 1, 2, 60, ITEM_MAT_BONE, 0, 0},
};
static const MobLootEntry s_golem_loot[] = {
    {ITEM_CLAY, 2, 5, 100, ITEM_MAT_CLAY, 0, 0},
    {ITEM_STONE_BLOCK, 0, 2, 40, ITEM_MAT_STONE, 0, 0},
    {ITEM_IRON_ORE, 0, 1, 12, ITEM_MAT_IRON, 0, 0},
};
static const MobLootEntry s_guard_loot[] = {
    {ITEM_FOOD, 0, 2, 40, ITEM_MAT_NONE, 0, 0},
    {ITEM_POTION, 0, 1, 20, ITEM_MAT_NONE, 0, 0},
};
static const MobLootEntry s_farmer_loot[] = {
    {ITEM_FOOD, 1, 3, 90, ITEM_MAT_NONE, 0, 0},
    {ITEM_WOOD_BLOCK, 0, 2, 35, ITEM_MAT_WOOD, 0, 0},
    {ITEM_EGG, 0, 2, 25, ITEM_MAT_NONE, 0, 0},
};
static const MobLootEntry s_shepherd_loot[] = {
    {ITEM_WOOL, 1, 4, 90, ITEM_MAT_WOOL, 0, 0},
    {ITEM_FOOD, 0, 2, 50, ITEM_MAT_NONE, 0, 0},
};
static const MobLootEntry s_baker_loot[] = {
    {ITEM_FOOD, 2, 4, 100, ITEM_MAT_NONE, 0, 0},
};
static const MobLootEntry s_healer_loot[] = {
    {ITEM_POTION, 1, 3, 100, ITEM_MAT_NONE, 0, 0},
    {ITEM_FOOD, 0, 2, 40, ITEM_MAT_NONE, 0, 0},
};
static const MobLootEntry s_blacksmith_loot[] = {
    {ITEM_IRON_ORE, 1, 3, 90, ITEM_MAT_IRON, 0, 0},
    {ITEM_STONE_BLOCK, 0, 2, 40, ITEM_MAT_STONE, 0, 0},
    {ITEM_WEAPON_CLUB, 0, 1, 15, ITEM_MAT_IRON, 40, 75},
};
static const MobLootEntry s_merchant_loot[] = {
    {ITEM_FOOD, 1, 2, 70, ITEM_MAT_NONE, 0, 0},
    {ITEM_POTION, 0, 2, 35, ITEM_MAT_NONE, 0, 0},
    {ITEM_RING_COPPER, 0, 1, 12, ITEM_MAT_COPPER, 40, 70},
    {ITEM_WOOL, 0, 2, 30, ITEM_MAT_WOOL, 0, 0},
};
static const MobLootEntry s_villager_loot[] = {
    {ITEM_FOOD, 0, 2, 60, ITEM_MAT_NONE, 0, 0},
    {ITEM_POTION, 0, 1, 10, ITEM_MAT_NONE, 0, 0},
};

#define TABLE(arr)                           \
    {                                        \
        (arr), (int)(sizeof(arr) / sizeof((arr)[0])) \
    }

static const MobLootTable *table_for_mob(const MobActor *mob)
{
    static const MobLootTable sheep = TABLE(s_sheep_loot);
    static const MobLootTable chicken = TABLE(s_chicken_loot);
    static const MobLootTable deer = TABLE(s_deer_loot);
    static const MobLootTable lizard = TABLE(s_lizard_loot);
    static const MobLootTable spider = TABLE(s_spider_loot);
    static const MobLootTable slime = TABLE(s_slime_loot);
    static const MobLootTable bird = TABLE(s_bird_loot);
    static const MobLootTable bat = TABLE(s_bat_loot);
    static const MobLootTable pet = TABLE(s_pet_loot);
    static const MobLootTable golem = TABLE(s_golem_loot);
    static const MobLootTable guard = TABLE(s_guard_loot);
    static const MobLootTable farmer = TABLE(s_farmer_loot);
    static const MobLootTable shepherd = TABLE(s_shepherd_loot);
    static const MobLootTable baker = TABLE(s_baker_loot);
    static const MobLootTable healer = TABLE(s_healer_loot);
    static const MobLootTable blacksmith = TABLE(s_blacksmith_loot);
    static const MobLootTable merchant = TABLE(s_merchant_loot);
    static const MobLootTable villager = TABLE(s_villager_loot);

    if (!mob)
        return NULL;

    switch (mob->mob_type)
    {
    case MOB_TYPE_SHEEP:
        return &sheep;
    case MOB_TYPE_CHICKEN:
        return &chicken;
    case MOB_TYPE_DEER:
        return &deer;
    case MOB_TYPE_LIZARD:
        return &lizard;
    case MOB_TYPE_SPIDER:
        return &spider;
    case MOB_TYPE_SLIME:
        return &slime;
    case MOB_TYPE_BIRD:
        return &bird;
    case MOB_TYPE_BAT:
        return &bat;
    case MOB_TYPE_WANDERER:
        if (mob->mesh_name[0] &&
            (strcmp(mob->mesh_name, "dog") == 0 || strcmp(mob->mesh_name, "cat") == 0))
            return &pet;
        return &golem;
    case MOB_TYPE_SOLVER:
    case MOB_TYPE_GUARD:
    case MOB_TYPE_HUNTER:
    case MOB_TYPE_BUILDER:
    case MOB_TYPE_DIGGER:
        return &golem;
    case MOB_TYPE_VILLAGER:
        switch (mob->profession)
        {
        case VILLAGER_JOB_GUARD:
            return &guard;
        case VILLAGER_JOB_FARMER:
        case VILLAGER_JOB_MILLER:
            return &farmer;
        case VILLAGER_JOB_SHEPHERD:
            return &shepherd;
        case VILLAGER_JOB_BAKER:
            return &baker;
        case VILLAGER_JOB_HEALER:
            return &healer;
        case VILLAGER_JOB_BLACKSMITH:
            return &blacksmith;
        case VILLAGER_JOB_MERCHANT:
            return &merchant;
        default:
            return &villager;
        }
    default:
        return NULL;
    }
}

void mob_actor_apply_loadout(MobActor *mob)
{
    if (!mob)
        return;

    switch (mob->mob_type)
    {
    case MOB_TYPE_SHEEP:
    case MOB_TYPE_CHICKEN:
    case MOB_TYPE_DEER:
    case MOB_TYPE_LIZARD:
    case MOB_TYPE_SPIDER:
    case MOB_TYPE_SLIME:
    case MOB_TYPE_BIRD:
    case MOB_TYPE_BAT:
        return;
    case MOB_TYPE_WANDERER:
        if (mob->mesh_name[0] &&
            (strcmp(mob->mesh_name, "dog") == 0 || strcmp(mob->mesh_name, "cat") == 0))
            return;
        equip_id(mob, ITEM_CORE_ARMOR_LEATHER);
        equip_id(mob, ITEM_WEAPON_CLUB);
        bag_count(mob, ITEM_CLAY, 1);
        return;
    case MOB_TYPE_SOLVER:
    case MOB_TYPE_GUARD:
        equip_id(mob, ITEM_CORE_ARMOR_LEATHER);
        equip_id(mob, ITEM_HELMET_LEATHER);
        equip_id(mob, ITEM_WEAPON_SPEAR);
        return;
    case MOB_TYPE_HUNTER:
        equip_id(mob, ITEM_CORE_ARMOR_LEATHER);
        equip_id(mob, ITEM_WEAPON_DAGGER);
        bag_count(mob, ITEM_FOOD, 1);
        return;
    case MOB_TYPE_BUILDER:
        equip_id(mob, ITEM_WEAPON_CLUB);
        bag_count(mob, ITEM_STONE_BLOCK, 2);
        bag_count(mob, ITEM_WOOD_BLOCK, 2);
        return;
    case MOB_TYPE_DIGGER:
        equip_id(mob, ITEM_WEAPON_CLUB);
        bag_count(mob, ITEM_IRON_ORE, 1);
        bag_count(mob, ITEM_STONE_BLOCK, 1);
        return;
    case MOB_TYPE_VILLAGER:
        break;
    default:
        return;
    }

    if (mob->villager_kind == VILLAGER_CHILD || mob->profession == VILLAGER_JOB_NONE ||
        mob->profession == VILLAGER_JOB_APPRENTICE)
    {
        bag_count(mob, ITEM_FOOD, 1);
        return;
    }

    switch (mob->profession)
    {
    case VILLAGER_JOB_GUARD:
        equip_id(mob, ITEM_CORE_ARMOR_LEATHER);
        equip_id(mob, ITEM_HELMET_LEATHER);
        equip_id(mob, ITEM_LEGGINGS_LEATHER);
        equip_id(mob, ITEM_WEAPON_SPEAR);
        bag_count(mob, ITEM_FOOD, 1);
        break;
    case VILLAGER_JOB_FARMER:
    case VILLAGER_JOB_MILLER:
        equip_id(mob, ITEM_WEAPON_STAFF);
        bag_count(mob, ITEM_FOOD, 2);
        bag_count(mob, ITEM_WOOD_BLOCK, 1);
        break;
    case VILLAGER_JOB_SHEPHERD:
        equip_id(mob, ITEM_WEAPON_STAFF);
        bag_count(mob, ITEM_WOOL, 2);
        break;
    case VILLAGER_JOB_BAKER:
        equip_id(mob, ITEM_WEAPON_CLUB);
        bag_count(mob, ITEM_FOOD, 3);
        break;
    case VILLAGER_JOB_HEALER:
        equip_id(mob, ITEM_WEAPON_STAFF);
        equip_id(mob, ITEM_NECKLACE_BONE);
        bag_count(mob, ITEM_POTION, 2);
        break;
    case VILLAGER_JOB_BLACKSMITH:
        equip_id(mob, ITEM_CORE_ARMOR_IRON);
        equip_id(mob, ITEM_GAUNTLETS_IRON);
        equip_id(mob, ITEM_WEAPON_SWORD);
        bag_count(mob, ITEM_IRON_ORE, 2);
        break;
    case VILLAGER_JOB_MERCHANT:
        equip_id(mob, ITEM_RING_COPPER);
        bag_count(mob, ITEM_FOOD, 2);
        bag_count(mob, ITEM_POTION, 1);
        bag_gear(mob, ITEM_WEAPON_DAGGER, ITEM_MAT_COPPER, 45);
        // Shop float: enough to buy common goods from travelers.
        wallet_set(&mob->purse, 3, 25, 40);
        break;
    default:
        bag_count(mob, ITEM_FOOD, 1);
        break;
    }
}

void mob_actor_generate_corpse_loot(Actor *actor)
{
    if (!actor || !actor->extra_data)
        return;
    MobActor *mob = (MobActor *)actor->extra_data;
    if (mob->loot_generated)
        return;
    mob->loot_generated = true;

    uint32_t rng = loot_mix(actor->id * 2654435761u,
                            (uint32_t)actor->x ^ ((uint32_t)actor->y << 8) ^
                                ((uint32_t)actor->z << 16));
    const MobLootTable *table = table_for_mob(mob);
    if (table)
        apply_table(actor, table, &rng);

    if (mob->mob_type == MOB_TYPE_SHEEP && mob->mesh_name[0])
    {
        if (strcmp(mob->mesh_name, "cow") == 0)
            inventory_add(&actor->inventory, ITEM_RAW_MEAT, 2);
        else if (strcmp(mob->mesh_name, "pig") == 0)
            inventory_add(&actor->inventory, ITEM_RAW_MEAT, 1);
    }
    if (mob->mob_type == MOB_TYPE_DEER && mob->mesh_name[0] &&
        strcmp(mob->mesh_name, "elephant") == 0)
    {
        inventory_add(&actor->inventory, ITEM_HIDE, 2);
        inventory_add(&actor->inventory, ITEM_RAW_MEAT, 3);
        inventory_add(&actor->inventory, ITEM_BONE, 2);
    }
}

bool mob_actor_corpse_loot_ready(const Actor *actor)
{
    if (!actor || !actor->extra_data)
        return false;
    return ((const MobActor *)actor->extra_data)->loot_generated;
}
