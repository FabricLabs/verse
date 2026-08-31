#include "item.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const ItemDef s_items[ITEM_COUNT] = {
    {ITEM_NONE, "Empty", "-", EQUIP_SLOT_COUNT, 0.0f, false, ITEM_KIND_MISC, ITEM_MAT_NONE, 0, 0, 0},
    {ITEM_STONE_BLOCK, "Stone Block", "St", EQUIP_SLOT_COUNT, 2.0f, true, ITEM_KIND_MATERIAL, ITEM_MAT_STONE, 0, 0, 0},
    {ITEM_WOOD_BLOCK, "Wood Block", "Wd", EQUIP_SLOT_COUNT, 1.0f, true, ITEM_KIND_MATERIAL, ITEM_MAT_WOOD, 0, 0, 0},
    {ITEM_FOOD, "Food", "Fd", EQUIP_SLOT_COUNT, 0.3f, true, ITEM_KIND_CONSUMABLE, ITEM_MAT_NONE, 0, 0, 0},
    {ITEM_POTION, "Potion", "Po", EQUIP_SLOT_COUNT, 0.4f, true, ITEM_KIND_CONSUMABLE, ITEM_MAT_NONE, 0, 0, 0},
    {ITEM_WEAPON_CLUB, "Club", "Cl", EQUIP_SLOT_MAIN_HAND, 1.5f, false, ITEM_KIND_WEAPON, ITEM_MAT_WOOD, 8, 0, 80},
    {ITEM_CORE_ARMOR_LEATHER, "Leather Core", "CA", EQUIP_SLOT_CORE_ARMOR, 3.0f, false, ITEM_KIND_ARMOR, ITEM_MAT_LEATHER, 0, 6, 100},
    {ITEM_LEGGINGS_LEATHER, "Leather Leggings", "Lg", EQUIP_SLOT_LEGGINGS, 2.0f, false, ITEM_KIND_ARMOR, ITEM_MAT_LEATHER, 0, 4, 90},
    {ITEM_HELMET_LEATHER, "Leather Helmet", "Hm", EQUIP_SLOT_HELMET, 1.0f, false, ITEM_KIND_ARMOR, ITEM_MAT_LEATHER, 0, 3, 80},
    {ITEM_GAUNTLETS_LEATHER, "Leather Gauntlets", "Ga", EQUIP_SLOT_GAUNTLETS, 0.8f, false, ITEM_KIND_ARMOR, ITEM_MAT_LEATHER, 0, 2, 70},
    {ITEM_RING_COPPER, "Copper Ring", "Rg", EQUIP_SLOT_RING, 0.1f, false, ITEM_KIND_ARMOR, ITEM_MAT_COPPER, 0, 1, 60},
    {ITEM_NECKLACE_BONE, "Bone Necklace", "Nk", EQUIP_SLOT_NECKLACE, 0.2f, false, ITEM_KIND_ARMOR, ITEM_MAT_BONE, 0, 1, 50},
    {ITEM_WOOL, "Wool", "Wl", EQUIP_SLOT_COUNT, 0.2f, true, ITEM_KIND_MATERIAL, ITEM_MAT_WOOL, 0, 0, 0},
    {ITEM_HUNTERS_NOTE, "Hunter's Note", "Nt", EQUIP_SLOT_COUNT, 0.05f, false, ITEM_KIND_MISC, ITEM_MAT_NONE, 0, 0, 0},
    {ITEM_WEAPON_DAGGER, "Dagger", "Dg", EQUIP_SLOT_MAIN_HAND, 0.8f, false, ITEM_KIND_WEAPON, ITEM_MAT_IRON, 6, 0, 70},
    {ITEM_WEAPON_SPEAR, "Spear", "Sp", EQUIP_SLOT_MAIN_HAND, 1.8f, false, ITEM_KIND_WEAPON, ITEM_MAT_WOOD, 10, 0, 90},
    {ITEM_WEAPON_SWORD, "Sword", "Sw", EQUIP_SLOT_MAIN_HAND, 2.2f, false, ITEM_KIND_WEAPON, ITEM_MAT_IRON, 14, 0, 120},
    {ITEM_WEAPON_STAFF, "Staff", "Sf", EQUIP_SLOT_MAIN_HAND, 1.4f, false, ITEM_KIND_WEAPON, ITEM_MAT_WOOD, 5, 0, 85},
    {ITEM_CORE_ARMOR_IRON, "Iron Core", "CI", EQUIP_SLOT_CORE_ARMOR, 6.0f, false, ITEM_KIND_ARMOR, ITEM_MAT_IRON, 0, 12, 160},
    {ITEM_LEGGINGS_IRON, "Iron Leggings", "Li", EQUIP_SLOT_LEGGINGS, 4.5f, false, ITEM_KIND_ARMOR, ITEM_MAT_IRON, 0, 8, 140},
    {ITEM_HELMET_IRON, "Iron Helmet", "Hi", EQUIP_SLOT_HELMET, 2.5f, false, ITEM_KIND_ARMOR, ITEM_MAT_IRON, 0, 6, 130},
    {ITEM_GAUNTLETS_IRON, "Iron Gauntlets", "Gi", EQUIP_SLOT_GAUNTLETS, 1.8f, false, ITEM_KIND_ARMOR, ITEM_MAT_IRON, 0, 4, 120},
    {ITEM_HIDE, "Hide", "Hd", EQUIP_SLOT_COUNT, 0.6f, true, ITEM_KIND_MATERIAL, ITEM_MAT_HIDE, 0, 0, 0},
    {ITEM_RAW_MEAT, "Raw Meat", "Mt", EQUIP_SLOT_COUNT, 0.4f, true, ITEM_KIND_CONSUMABLE, ITEM_MAT_NONE, 0, 0, 0},
    {ITEM_BONE, "Bone", "Bn", EQUIP_SLOT_COUNT, 0.3f, true, ITEM_KIND_MATERIAL, ITEM_MAT_BONE, 0, 0, 0},
    {ITEM_FANG, "Fang", "Fg", EQUIP_SLOT_COUNT, 0.15f, true, ITEM_KIND_MATERIAL, ITEM_MAT_BONE, 0, 0, 0},
    {ITEM_VENOM_SAC, "Venom Sac", "Vn", EQUIP_SLOT_COUNT, 0.2f, true, ITEM_KIND_MATERIAL, ITEM_MAT_VENOM, 0, 0, 0},
    {ITEM_SLIME_GEL, "Slime Gel", "Sg", EQUIP_SLOT_COUNT, 0.25f, true, ITEM_KIND_MATERIAL, ITEM_MAT_SLIME, 0, 0, 0},
    {ITEM_FEATHER, "Feather", "Ft", EQUIP_SLOT_COUNT, 0.05f, true, ITEM_KIND_MATERIAL, ITEM_MAT_FEATHER, 0, 0, 0},
    {ITEM_EGG, "Egg", "Eg", EQUIP_SLOT_COUNT, 0.15f, true, ITEM_KIND_CONSUMABLE, ITEM_MAT_NONE, 0, 0, 0},
    {ITEM_CLAY, "Clay", "Cy", EQUIP_SLOT_COUNT, 0.5f, true, ITEM_KIND_MATERIAL, ITEM_MAT_CLAY, 0, 0, 0},
    {ITEM_IRON_ORE, "Iron Ore", "Io", EQUIP_SLOT_COUNT, 1.2f, true, ITEM_KIND_MATERIAL, ITEM_MAT_IRON, 0, 0, 0},
    {ITEM_COPPER_ORE, "Copper Ore", "Co", EQUIP_SLOT_COUNT, 1.0f, true, ITEM_KIND_MATERIAL, ITEM_MAT_COPPER, 0, 0, 0},
    {ITEM_TIN_ORE, "Tin Ore", "Tn", EQUIP_SLOT_COUNT, 0.9f, true, ITEM_KIND_MATERIAL, ITEM_MAT_COPPER, 0, 0, 0},
    {ITEM_COAL, "Coal", "Cl", EQUIP_SLOT_COUNT, 0.5f, true, ITEM_KIND_MATERIAL, ITEM_MAT_NONE, 0, 0, 0},
    {ITEM_STICK, "Stick", "Sk", EQUIP_SLOT_COUNT, 0.05f, true, ITEM_KIND_MATERIAL, ITEM_MAT_WOOD, 0, 0, 0},
    {ITEM_IRON_INGOT, "Iron Ingot", "Ii", EQUIP_SLOT_COUNT, 1.0f, true, ITEM_KIND_MATERIAL, ITEM_MAT_IRON, 0, 0, 0},
    {ITEM_COPPER_INGOT, "Copper Ingot", "Ci", EQUIP_SLOT_COUNT, 0.8f, true, ITEM_KIND_MATERIAL, ITEM_MAT_COPPER, 0, 0, 0},
    {ITEM_TIN_INGOT, "Tin Ingot", "Ti", EQUIP_SLOT_COUNT, 0.7f, true, ITEM_KIND_MATERIAL, ITEM_MAT_COPPER, 0, 0, 0},
    {ITEM_BRICK, "Brick", "Bk", EQUIP_SLOT_COUNT, 0.8f, true, ITEM_KIND_MATERIAL, ITEM_MAT_CLAY, 0, 0, 0},
};

static const char *s_slot_names[EQUIP_SLOT_COUNT] = {
    "Core Armor",
    "Leggings",
    "Helmet",
    "Gauntlets",
    "Ring",
    "Necklace",
    "Main Hand",
};

static const char *s_slot_abbrevs[EQUIP_SLOT_COUNT] = {
    "Core",
    "Legs",
    "Helm",
    "Gaunt",
    "Ring",
    "Neck",
    "Hand",
};

static const char *s_mat_names[ITEM_MAT_COUNT] = {
    "None", "Wood", "Stone", "Leather", "Bone", "Copper", "Iron",
    "Wool", "Hide", "Slime", "Venom", "Feather", "Clay",
};

const ItemDef *item_def(ItemId id)
{
    if (id < 0 || id >= ITEM_COUNT)
        return &s_items[ITEM_NONE];
    return &s_items[id];
}

const char *item_name(ItemId id)
{
    return item_def(id)->name;
}

const char *item_abbrev(ItemId id)
{
    return item_def(id)->abbrev;
}

bool item_is_equippable(ItemId id)
{
    return item_equip_slot(id) < EQUIP_SLOT_COUNT;
}

EquipmentSlot item_equip_slot(ItemId id)
{
    return item_def(id)->equip_slot;
}

const char *equipment_slot_name(EquipmentSlot slot)
{
    if (slot < 0 || slot >= EQUIP_SLOT_COUNT)
        return "None";
    return s_slot_names[slot];
}

const char *equipment_slot_abbrev(EquipmentSlot slot)
{
    if (slot < 0 || slot >= EQUIP_SLOT_COUNT)
        return "-";
    return s_slot_abbrevs[slot];
}

const char *item_material_name(ItemMaterial mat)
{
    if (mat < 0 || mat >= ITEM_MAT_COUNT)
        return "None";
    return s_mat_names[mat];
}

float item_material_damage_mul(ItemMaterial mat)
{
    switch (mat)
    {
    case ITEM_MAT_WOOD: return 0.85f;
    case ITEM_MAT_STONE: return 1.05f;
    case ITEM_MAT_BONE: return 0.95f;
    case ITEM_MAT_COPPER: return 1.10f;
    case ITEM_MAT_IRON: return 1.35f;
    case ITEM_MAT_VENOM: return 1.20f;
    default: return 1.0f;
    }
}

float item_material_armor_mul(ItemMaterial mat)
{
    switch (mat)
    {
    case ITEM_MAT_LEATHER: return 0.90f;
    case ITEM_MAT_HIDE: return 0.85f;
    case ITEM_MAT_WOOL: return 0.55f;
    case ITEM_MAT_COPPER: return 1.05f;
    case ITEM_MAT_IRON: return 1.40f;
    case ITEM_MAT_BONE: return 0.75f;
    case ITEM_MAT_CLAY: return 0.70f;
    default: return 1.0f;
    }
}

float item_material_durability_mul(ItemMaterial mat)
{
    switch (mat)
    {
    case ITEM_MAT_WOOD: return 0.80f;
    case ITEM_MAT_STONE: return 1.10f;
    case ITEM_MAT_LEATHER: return 0.85f;
    case ITEM_MAT_HIDE: return 0.80f;
    case ITEM_MAT_BONE: return 0.70f;
    case ITEM_MAT_COPPER: return 1.00f;
    case ITEM_MAT_IRON: return 1.45f;
    case ITEM_MAT_CLAY: return 0.60f;
    case ITEM_MAT_SLIME: return 0.50f;
    default: return 1.0f;
    }
}

uint32_t item_pieces_per_unit(ItemId id)
{
    switch (id)
    {
    case ITEM_STONE_BLOCK:
    case ITEM_WOOD_BLOCK:
        return ITEM_BLOCK_PIECES;
    default:
        return 1u;
    }
}

bool item_is_fractional(ItemId id)
{
    return item_pieces_per_unit(id) > 1u;
}

static uint16_t clamp_u16(float v, uint16_t lo, uint16_t hi)
{
    if (v < (float)lo)
        return lo;
    if (v > (float)hi)
        return hi;
    return (uint16_t)(v + 0.5f);
}

ItemStack item_stack_make(ItemId id, uint32_t pieces)
{
    ItemStack s;
    memset(&s, 0, sizeof(s));
    if (id == ITEM_NONE || pieces == 0)
        return s;
    s.id = id;
    s.pieces = pieces;
    const ItemDef *def = item_def(id);
    if (!def->stackable && def->base_durability > 0)
        item_stack_ensure_stats(&s);
    return s;
}

ItemStack item_stack_make_gear(ItemId id, ItemMaterial material, uint8_t quality)
{
    ItemStack s;
    memset(&s, 0, sizeof(s));
    const ItemDef *def = item_def(id);
    if (id == ITEM_NONE || def->stackable)
        return item_stack_make(id, 1);

    if (quality < 1)
        quality = 1;
    if (quality > 100)
        quality = 100;
    if (material == ITEM_MAT_NONE)
        material = def->default_material;

    s.id = id;
    s.pieces = 1;
    s.quality = quality;
    s.material = (uint8_t)material;

    const float q = 0.55f + 0.45f * ((float)quality / 100.0f);
    float dur = (float)def->base_durability * item_material_durability_mul(material) * q;
    s.durability_max = clamp_u16(dur, 1, 60000);
    s.durability = s.durability_max;
    return s;
}

void item_stack_ensure_stats(ItemStack *stack)
{
    if (!stack || stack->id == ITEM_NONE)
        return;
    const ItemDef *def = item_def(stack->id);
    if (def->stackable || def->base_durability == 0)
        return;
    if (stack->durability_max > 0)
        return;
    ItemMaterial mat = (ItemMaterial)stack->material;
    if (mat == ITEM_MAT_NONE)
        mat = def->default_material;
    uint8_t q = stack->quality ? stack->quality : 50;
    *stack = item_stack_make_gear(stack->id, mat, q);
    if (stack->pieces == 0)
        stack->pieces = 1;
}

uint16_t item_stack_damage(const ItemStack *stack)
{
    if (!stack || stack->id == ITEM_NONE)
        return 0;
    const ItemDef *def = item_def(stack->id);
    if (def->base_damage == 0)
        return 0;
    ItemStack tmp = *stack;
    item_stack_ensure_stats(&tmp);
    const float q = 0.55f + 0.45f * ((float)tmp.quality / 100.0f);
    float dmg = (float)def->base_damage * item_material_damage_mul((ItemMaterial)tmp.material) * q;
    // Worn gear hits softer once past half durability.
    if (tmp.durability_max > 0 && tmp.durability * 2 < tmp.durability_max)
        dmg *= 0.75f;
    return clamp_u16(dmg, 1, 60000);
}

uint16_t item_stack_armor(const ItemStack *stack)
{
    if (!stack || stack->id == ITEM_NONE)
        return 0;
    const ItemDef *def = item_def(stack->id);
    if (def->base_armor == 0)
        return 0;
    ItemStack tmp = *stack;
    item_stack_ensure_stats(&tmp);
    const float q = 0.55f + 0.45f * ((float)tmp.quality / 100.0f);
    float arm = (float)def->base_armor * item_material_armor_mul((ItemMaterial)tmp.material) * q;
    if (tmp.durability_max > 0 && tmp.durability * 2 < tmp.durability_max)
        arm *= 0.7f;
    return clamp_u16(arm, 0, 60000);
}

void item_format_quantity(ItemId id, uint32_t pieces, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0)
        return;
    const uint32_t per = item_pieces_per_unit(id);
    if (per <= 1u)
    {
        snprintf(buf, buf_size, "%u", pieces);
        return;
    }
    const uint32_t whole = pieces / per;
    const uint32_t frac = pieces % per;
    if (frac == 0)
    {
        snprintf(buf, buf_size, "%u", whole);
        return;
    }
    const unsigned hundredths = (unsigned)((frac * 100u + per / 2u) / per);
    if (whole == 0)
        snprintf(buf, buf_size, "0.%02u", hundredths);
    else
        snprintf(buf, buf_size, "%u.%02u", whole, hundredths);
}

void inventory_init(Inventory *inv, uint16_t capacity)
{
    if (!inv)
        return;
    memset(inv, 0, sizeof(*inv));
    if (capacity == 0)
        capacity = INVENTORY_DEFAULT_SLOTS;
    if (capacity > INVENTORY_MAX_SLOTS)
        capacity = INVENTORY_MAX_SLOTS;
    inv->capacity = capacity;
}

void inventory_clear(Inventory *inv)
{
    if (!inv)
        return;
    uint16_t cap = inv->capacity;
    memset(inv->slots, 0, sizeof(inv->slots));
    inv->capacity = cap ? cap : INVENTORY_DEFAULT_SLOTS;
}

int inventory_used_slots(const Inventory *inv)
{
    if (!inv)
        return 0;
    int used = 0;
    for (uint16_t i = 0; i < inv->capacity; i++)
    {
        if (inv->slots[i].id != ITEM_NONE && inv->slots[i].pieces > 0)
            used++;
    }
    return used;
}

uint32_t inventory_count_pieces(const Inventory *inv, ItemId id)
{
    if (!inv || id == ITEM_NONE)
        return 0;
    uint32_t total = 0;
    for (uint16_t i = 0; i < inv->capacity; i++)
    {
        if (inv->slots[i].id == id)
            total += inv->slots[i].pieces;
    }
    return total;
}

int inventory_count_item(const Inventory *inv, ItemId id)
{
    const uint32_t per = item_pieces_per_unit(id);
    if (per == 0)
        return 0;
    return (int)(inventory_count_pieces(inv, id) / per);
}

bool inventory_add_stack(Inventory *inv, const ItemStack *stack)
{
    if (!inv || !stack || stack->id == ITEM_NONE || stack->pieces == 0)
        return false;

    ItemStack src = *stack;
    item_stack_ensure_stats(&src);
    const ItemDef *def = item_def(src.id);

    if (def->stackable)
        return inventory_add_pieces(inv, src.id, src.pieces);

    // Non-stackables occupy one slot per piece, preserving instance stats.
    for (uint32_t n = 0; n < src.pieces; n++)
    {
        int free_i = -1;
        for (uint16_t i = 0; i < inv->capacity; i++)
        {
            if (inv->slots[i].id == ITEM_NONE || inv->slots[i].pieces == 0)
            {
                free_i = (int)i;
                break;
            }
        }
        if (free_i < 0)
            return false;
        inv->slots[free_i] = src;
        inv->slots[free_i].pieces = 1;
    }
    return true;
}

bool inventory_add_pieces(Inventory *inv, ItemId id, uint32_t pieces)
{
    if (!inv || id == ITEM_NONE || pieces == 0)
        return false;

    const ItemDef *def = item_def(id);
    if (!def->stackable)
    {
        for (uint32_t n = 0; n < pieces; n++)
        {
            ItemStack gear = item_stack_make_gear(id, def->default_material, 50);
            if (!inventory_add_stack(inv, &gear))
                return false;
        }
        return true;
    }

    uint32_t remaining = pieces;
    for (uint16_t i = 0; i < inv->capacity && remaining > 0; i++)
    {
        if (inv->slots[i].id != id)
            continue;
        uint32_t room = INVENTORY_STACK_PIECES_MAX - inv->slots[i].pieces;
        if (room == 0)
            continue;
        if (remaining <= room)
        {
            inv->slots[i].pieces += remaining;
            remaining = 0;
        }
        else
        {
            inv->slots[i].pieces += room;
            remaining -= room;
        }
    }

    while (remaining > 0)
    {
        int free_i = -1;
        for (uint16_t i = 0; i < inv->capacity; i++)
        {
            if (inv->slots[i].id == ITEM_NONE || inv->slots[i].pieces == 0)
            {
                free_i = (int)i;
                break;
            }
        }
        if (free_i < 0)
            return false;
        uint32_t put = remaining;
        if (put > INVENTORY_STACK_PIECES_MAX)
            put = INVENTORY_STACK_PIECES_MAX;
        inv->slots[free_i].id = id;
        inv->slots[free_i].pieces = put;
        inv->slots[free_i].durability = 0;
        inv->slots[free_i].durability_max = 0;
        inv->slots[free_i].quality = 0;
        inv->slots[free_i].material = (uint8_t)def->default_material;
        remaining -= put;
    }
    return true;
}

bool inventory_add(Inventory *inv, ItemId id, uint16_t count)
{
    if (count == 0)
        return true;
    const uint32_t per = item_pieces_per_unit(id);
    return inventory_add_pieces(inv, id, (uint32_t)count * per);
}

bool inventory_remove_pieces(Inventory *inv, ItemId id, uint32_t pieces)
{
    if (!inv || id == ITEM_NONE || pieces == 0)
        return false;
    if (inventory_count_pieces(inv, id) < pieces)
        return false;

    uint32_t remaining = pieces;
    for (uint16_t i = 0; i < inv->capacity && remaining > 0; i++)
    {
        if (inv->slots[i].id != id)
            continue;
        if (inv->slots[i].pieces <= remaining)
        {
            remaining -= inv->slots[i].pieces;
            memset(&inv->slots[i], 0, sizeof(inv->slots[i]));
        }
        else
        {
            inv->slots[i].pieces -= remaining;
            remaining = 0;
        }
    }
    return remaining == 0;
}

bool inventory_remove(Inventory *inv, ItemId id, uint16_t count)
{
    if (count == 0)
        return true;
    const uint32_t per = item_pieces_per_unit(id);
    return inventory_remove_pieces(inv, id, (uint32_t)count * per);
}

float inventory_total_weight(const Inventory *inv)
{
    if (!inv)
        return 0.0f;
    float total = 0.0f;
    for (uint16_t i = 0; i < inv->capacity; i++)
    {
        if (inv->slots[i].id == ITEM_NONE || inv->slots[i].pieces == 0)
            continue;
        const ItemId id = inv->slots[i].id;
        const uint32_t per = item_pieces_per_unit(id);
        total += item_def(id)->weight * ((float)inv->slots[i].pieces / (float)per);
    }
    return total;
}

void equipment_init(Equipment *eq)
{
    equipment_clear(eq);
}

void equipment_clear(Equipment *eq)
{
    if (!eq)
        return;
    memset(eq, 0, sizeof(*eq));
}

bool equipment_equip(Equipment *eq, ItemId id)
{
    if (!eq || !item_is_equippable(id))
        return false;
    EquipmentSlot slot = item_equip_slot(id);
    eq->slots[slot] = id;
    return true;
}

ItemId equipment_unequip(Equipment *eq, EquipmentSlot slot)
{
    if (!eq || slot < 0 || slot >= EQUIP_SLOT_COUNT)
        return ITEM_NONE;
    ItemId id = eq->slots[slot];
    eq->slots[slot] = ITEM_NONE;
    return id;
}

float equipment_total_weight(const Equipment *eq)
{
    if (!eq)
        return 0.0f;
    float total = 0.0f;
    for (int i = 0; i < EQUIP_SLOT_COUNT; i++)
    {
        if (eq->slots[i] != ITEM_NONE)
            total += item_def(eq->slots[i])->weight;
    }
    return total;
}

ItemStack equipment_slot_to_stack(ItemId id, uint32_t seed)
{
    if (id == ITEM_NONE)
        return item_stack_make(ITEM_NONE, 0);
    const ItemDef *def = item_def(id);
    uint8_t quality = (uint8_t)(35u + (seed * 17u + 11u) % 51u); // 35..85
    return item_stack_make_gear(id, def->default_material, quality);
}

void inventory_seed_spirit_starter(Inventory *inv)
{
    if (!inv)
        return;
    inventory_add(inv, ITEM_FOOD, 3);
    inventory_add(inv, ITEM_POTION, 1);
    inventory_add(inv, ITEM_STONE_BLOCK, 1);
}

static int64_t base_unit_price_copper(ItemId id)
{
    switch (id)
    {
    case ITEM_FOOD:
        return 5;
    case ITEM_POTION:
        return 25;
    case ITEM_WOOL:
        return 8;
    case ITEM_HIDE:
        return 12;
    case ITEM_RAW_MEAT:
        return 6;
    case ITEM_BONE:
        return 3;
    case ITEM_FANG:
        return 15;
    case ITEM_VENOM_SAC:
        return 30;
    case ITEM_SLIME_GEL:
        return 10;
    case ITEM_FEATHER:
        return 2;
    case ITEM_EGG:
        return 4;
    case ITEM_CLAY:
        return 3;
    case ITEM_IRON_ORE:
        return 18;
    case ITEM_COPPER_ORE:
        return 14;
    case ITEM_TIN_ORE:
        return 12;
    case ITEM_COAL:
        return 6;
    case ITEM_STICK:
        return 1;
    case ITEM_IRON_INGOT:
        return 40;
    case ITEM_COPPER_INGOT:
        return 28;
    case ITEM_TIN_INGOT:
        return 24;
    case ITEM_BRICK:
        return 8;
    case ITEM_STONE_BLOCK:
        return 4;
    case ITEM_WOOD_BLOCK:
        return 3;
    case ITEM_WEAPON_CLUB:
        return 40;
    case ITEM_WEAPON_DAGGER:
        return 80;
    case ITEM_WEAPON_SPEAR:
        return 100;
    case ITEM_WEAPON_SWORD:
        return 200;
    case ITEM_WEAPON_STAFF:
        return 70;
    case ITEM_CORE_ARMOR_LEATHER:
        return 90;
    case ITEM_LEGGINGS_LEATHER:
        return 70;
    case ITEM_HELMET_LEATHER:
        return 50;
    case ITEM_GAUNTLETS_LEATHER:
        return 40;
    case ITEM_CORE_ARMOR_IRON:
        return 220;
    case ITEM_LEGGINGS_IRON:
        return 160;
    case ITEM_HELMET_IRON:
        return 120;
    case ITEM_GAUNTLETS_IRON:
        return 90;
    case ITEM_RING_COPPER:
        return 50;
    case ITEM_NECKLACE_BONE:
        return 45;
    case ITEM_HUNTERS_NOTE:
        return 0;
    case ITEM_NONE:
    default:
        return 0;
    }
}

int64_t item_buy_price_copper(const ItemStack *stack)
{
    if (!stack || stack->id == ITEM_NONE || stack->pieces == 0)
        return 0;

    int64_t unit = base_unit_price_copper(stack->id);
    if (unit <= 0)
        return 0;

    const ItemDef *def = item_def(stack->id);
    if (def && !def->stackable)
    {
        int q = stack->quality > 0 ? (int)stack->quality : 50;
        int64_t price = (unit * (int64_t)q) / 50;
        if (price < 1)
            price = 1;
        return price;
    }

    uint32_t ppu = item_pieces_per_unit(stack->id);
    if (ppu <= 1)
        return unit * (int64_t)stack->pieces;

    int64_t price = (unit * (int64_t)stack->pieces) / (int64_t)ppu;
    if (price < 1 && stack->pieces > 0 && unit > 0)
        price = 1;
    return price;
}

int64_t item_sell_price_copper(const ItemStack *stack)
{
    int64_t buy = item_buy_price_copper(stack);
    if (buy <= 0)
        return 0;
    int64_t sell = buy / 2;
    if (sell < 1)
        sell = 1;
    return sell;
}
