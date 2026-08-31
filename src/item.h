#ifndef VERSE_ITEM_H
#define VERSE_ITEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Catalog ids. Zero is empty; keep the order stable for saves — append only.
typedef enum {
    ITEM_NONE = 0,
    ITEM_STONE_BLOCK,
    ITEM_WOOD_BLOCK,
    ITEM_FOOD,
    ITEM_POTION,
    ITEM_WEAPON_CLUB,
    ITEM_CORE_ARMOR_LEATHER,
    ITEM_LEGGINGS_LEATHER,
    ITEM_HELMET_LEATHER,
    ITEM_GAUNTLETS_LEATHER,
    ITEM_RING_COPPER,
    ITEM_NECKLACE_BONE,
    ITEM_WOOL,
    ITEM_HUNTERS_NOTE,
    // --- procedural / loot catalog (append-only) ---
    ITEM_WEAPON_DAGGER,
    ITEM_WEAPON_SPEAR,
    ITEM_WEAPON_SWORD,
    ITEM_WEAPON_STAFF,
    ITEM_CORE_ARMOR_IRON,
    ITEM_LEGGINGS_IRON,
    ITEM_HELMET_IRON,
    ITEM_GAUNTLETS_IRON,
    ITEM_HIDE,
    ITEM_RAW_MEAT,
    ITEM_BONE,
    ITEM_FANG,
    ITEM_VENOM_SAC,
    ITEM_SLIME_GEL,
    ITEM_FEATHER,
    ITEM_EGG,
    ITEM_CLAY,
    ITEM_IRON_ORE,
    // --- crafting materials (append-only) ---
    ITEM_COPPER_ORE,
    ITEM_TIN_ORE,
    ITEM_COAL,
    ITEM_STICK,
    ITEM_IRON_INGOT,
    ITEM_COPPER_INGOT,
    ITEM_TIN_INGOT,
    ITEM_BRICK,
    ITEM_COUNT
} ItemId;

// Body-only equipment slots. The spirit has none. MAIN_HAND holds weapons.
typedef enum {
    EQUIP_SLOT_CORE_ARMOR = 0,
    EQUIP_SLOT_LEGGINGS,
    EQUIP_SLOT_HELMET,
    EQUIP_SLOT_GAUNTLETS,
    EQUIP_SLOT_RING,
    EQUIP_SLOT_NECKLACE,
    EQUIP_SLOT_MAIN_HAND,
    EQUIP_SLOT_COUNT
} EquipmentSlot;

// What an item is mostly made from. Crafting (later) picks materials; loot rolls them now.
typedef enum {
    ITEM_MAT_NONE = 0,
    ITEM_MAT_WOOD,
    ITEM_MAT_STONE,
    ITEM_MAT_LEATHER,
    ITEM_MAT_BONE,
    ITEM_MAT_COPPER,
    ITEM_MAT_IRON,
    ITEM_MAT_WOOL,
    ITEM_MAT_HIDE,
    ITEM_MAT_SLIME,
    ITEM_MAT_VENOM,
    ITEM_MAT_FEATHER,
    ITEM_MAT_CLAY,
    ITEM_MAT_COUNT
} ItemMaterial;

typedef enum {
    ITEM_KIND_MISC = 0,
    ITEM_KIND_WEAPON,
    ITEM_KIND_ARMOR,
    ITEM_KIND_CONSUMABLE,
    ITEM_KIND_MATERIAL
} ItemKind;

typedef struct {
    ItemId id;
    // Quantity in the item's native unit: whole items for food/gear, and 4x4x4 subcomponent
    // pieces for placeable blocks (see item_pieces_per_unit). A full stone block is 512 pieces.
    uint32_t pieces;
    // Instance stats for gear (ignored for stackables). durability_max 0 means "not rolled yet".
    uint16_t durability;
    uint16_t durability_max;
    uint8_t quality;   // 1..100 — scales damage / armor / durability
    uint8_t material;  // ItemMaterial that formed this piece
} ItemStack;

#define INVENTORY_MAX_SLOTS 32
#define INVENTORY_DEFAULT_SLOTS 16
#define INVENTORY_STACK_PIECES_MAX 999999u

// One full placeable block is 512 of the 4x4x4 drops carved from a 32^3 material lattice.
#define ITEM_BLOCK_PIECES 512u

typedef struct {
    ItemStack slots[INVENTORY_MAX_SLOTS];
    uint16_t capacity; // active bag size (<= INVENTORY_MAX_SLOTS)
} Inventory;

typedef struct {
    ItemId slots[EQUIP_SLOT_COUNT];
} Equipment;

typedef struct {
    ItemId id;
    const char *name;
    const char *abbrev;       // short label for 16x16 UI slots
    EquipmentSlot equip_slot; // EQUIP_SLOT_COUNT if not equippable
    float weight;             // weight of one whole unit (one block / one food / ...)
    bool stackable;
    ItemKind kind;
    ItemMaterial default_material;
    uint16_t base_damage;     // weapons (0 for non-weapons)
    uint16_t base_armor;      // armor pieces (0 for non-armor)
    uint16_t base_durability; // gear wear budget (0 for stackables)
} ItemDef;

const ItemDef *item_def(ItemId id);
const char *item_name(ItemId id);
const char *item_abbrev(ItemId id);
bool item_is_equippable(ItemId id);
EquipmentSlot item_equip_slot(ItemId id);
const char *equipment_slot_name(EquipmentSlot slot);
const char *equipment_slot_abbrev(EquipmentSlot slot);

const char *item_material_name(ItemMaterial mat);
// Multipliers used when rolling instance stats from the crafting material.
float item_material_damage_mul(ItemMaterial mat);
float item_material_armor_mul(ItemMaterial mat);
float item_material_durability_mul(ItemMaterial mat);

// How many inventory pieces make one whole unit of this item. Block materials use 512
// (one 32^3 voxel as 4^3 drops); everything else is 1.
uint32_t item_pieces_per_unit(ItemId id);
bool item_is_fractional(ItemId id);

// Build a stack. Stackables ignore material/quality. Gear rolls durability from base * material * quality.
ItemStack item_stack_make(ItemId id, uint32_t pieces);
ItemStack item_stack_make_gear(ItemId id, ItemMaterial material, uint8_t quality);
// Ensure gear stacks have rolled durability (no-op for stackables / already rolled).
void item_stack_ensure_stats(ItemStack *stack);
uint16_t item_stack_damage(const ItemStack *stack);
uint16_t item_stack_armor(const ItemStack *stack);

void inventory_init(Inventory *inv, uint16_t capacity);
void inventory_clear(Inventory *inv);
int inventory_used_slots(const Inventory *inv);
// Whole units currently held (floored). Prefer inventory_count_pieces for fractional stacks.
int inventory_count_item(const Inventory *inv, ItemId id);
uint32_t inventory_count_pieces(const Inventory *inv, ItemId id);
bool inventory_add(Inventory *inv, ItemId id, uint16_t count);
bool inventory_add_pieces(Inventory *inv, ItemId id, uint32_t pieces);
// Adds a full instance (gear keeps durability/quality/material).
bool inventory_add_stack(Inventory *inv, const ItemStack *stack);
bool inventory_remove(Inventory *inv, ItemId id, uint16_t count);
bool inventory_remove_pieces(Inventory *inv, ItemId id, uint32_t pieces);
float inventory_total_weight(const Inventory *inv);

// Format a stack for UI: "3", "1.25", or "0.06" for fractional blocks.
void item_format_quantity(ItemId id, uint32_t pieces, char *buf, size_t buf_size);

void equipment_init(Equipment *eq);
void equipment_clear(Equipment *eq);
bool equipment_equip(Equipment *eq, ItemId id);
ItemId equipment_unequip(Equipment *eq, EquipmentSlot slot);
float equipment_total_weight(const Equipment *eq);
// Instantiated gear for a worn catalog id (used when looting equipment off a corpse).
ItemStack equipment_slot_to_stack(ItemId id, uint32_t seed);

// Starter bag for a newly created spirit.
void inventory_seed_spirit_starter(Inventory *inv);

// Shop pricing in copper pieces (100c = 1s, 100s = 1g). Quest items return 0.
int64_t item_buy_price_copper(const ItemStack *stack);
int64_t item_sell_price_copper(const ItemStack *stack);

#endif // VERSE_ITEM_H
