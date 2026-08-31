#include "craft.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "world.h"

void craft_session_clear(CraftSession *session)
{
    if (!session)
        return;
    memset(session, 0, sizeof(*session));
}

void craft_session_open(CraftSession *session, CraftStation station, bool book_mode)
{
    if (!session)
        return;
    craft_session_clear(session);
    session->active = true;
    session->station = station;
    session->book_mode = book_mode;
    session->selected = 0;
    session->scroll = 0;
}

const char *craft_station_name(CraftStation station)
{
    switch (station)
    {
    case CRAFT_STATION_HAND:
        return "Hand";
    case CRAFT_STATION_TABLE:
        return "Crafting Table";
    case CRAFT_STATION_ANVIL:
        return "Anvil";
    case CRAFT_STATION_FORGE:
        return "Forge";
    default:
        return "Unknown";
    }
}

CraftStation craft_station_from_voxel(VoxelType type)
{
    switch (type)
    {
    case VOXEL_CRAFTING_TABLE:
        return CRAFT_STATION_TABLE;
    case VOXEL_ANVIL:
        return CRAFT_STATION_ANVIL;
    case VOXEL_FORGE:
        return CRAFT_STATION_FORGE;
    default:
        return CRAFT_STATION_COUNT;
    }
}

bool craft_voxel_is_station(VoxelType type)
{
    return craft_station_from_voxel(type) < CRAFT_STATION_COUNT;
}

#define IN(id, pcs) \
    {               \
        (id), (pcs) \
    }

static const CraftRecipe s_recipes[] = {
    {
        "Stick",
        "Split wood into sticks",
        CRAFT_STATION_HAND,
        {IN(ITEM_WOOD_BLOCK, 64)},
        1,
        ITEM_STICK,
        4,
        false,
        ITEM_MAT_WOOD,
        0,
    },
    {
        "Club",
        "A rough wooden club",
        CRAFT_STATION_HAND,
        {IN(ITEM_WOOD_BLOCK, 256), IN(ITEM_STICK, 1)},
        2,
        ITEM_WEAPON_CLUB,
        1,
        true,
        ITEM_MAT_WOOD,
        55,
    },
    {
        "Bone Necklace",
        "Bones strung on wool",
        CRAFT_STATION_HAND,
        {IN(ITEM_BONE, 3), IN(ITEM_WOOL, 1)},
        2,
        ITEM_NECKLACE_BONE,
        1,
        true,
        ITEM_MAT_BONE,
        50,
    },
    {
        "Staff",
        "A walking staff",
        CRAFT_STATION_TABLE,
        {IN(ITEM_WOOD_BLOCK, 384), IN(ITEM_STICK, 2)},
        2,
        ITEM_WEAPON_STAFF,
        1,
        true,
        ITEM_MAT_WOOD,
        60,
    },
    {
        "Spear",
        "Shaft with a bone tip",
        CRAFT_STATION_TABLE,
        {IN(ITEM_STICK, 2), IN(ITEM_BONE, 1)},
        2,
        ITEM_WEAPON_SPEAR,
        1,
        true,
        ITEM_MAT_BONE,
        60,
    },
    {
        "Fang Spear",
        "Shaft tipped with a fang",
        CRAFT_STATION_TABLE,
        {IN(ITEM_STICK, 2), IN(ITEM_FANG, 1)},
        2,
        ITEM_WEAPON_SPEAR,
        1,
        true,
        ITEM_MAT_BONE,
        70,
    },
    {
        "Leather Core",
        "Hide shaped into core armor",
        CRAFT_STATION_TABLE,
        {IN(ITEM_HIDE, 4)},
        1,
        ITEM_CORE_ARMOR_LEATHER,
        1,
        true,
        ITEM_MAT_LEATHER,
        60,
    },
    {
        "Leather Leggings",
        "Hide shaped into leggings",
        CRAFT_STATION_TABLE,
        {IN(ITEM_HIDE, 3)},
        1,
        ITEM_LEGGINGS_LEATHER,
        1,
        true,
        ITEM_MAT_LEATHER,
        60,
    },
    {
        "Leather Helmet",
        "Hide shaped into a helm",
        CRAFT_STATION_TABLE,
        {IN(ITEM_HIDE, 2)},
        1,
        ITEM_HELMET_LEATHER,
        1,
        true,
        ITEM_MAT_LEATHER,
        60,
    },
    {
        "Leather Gauntlets",
        "Hide shaped into gloves",
        CRAFT_STATION_TABLE,
        {IN(ITEM_HIDE, 2)},
        1,
        ITEM_GAUNTLETS_LEATHER,
        1,
        true,
        ITEM_MAT_LEATHER,
        60,
    },
    {
        "Potion",
        "Brew slime, venom, and feather",
        CRAFT_STATION_TABLE,
        {IN(ITEM_SLIME_GEL, 1), IN(ITEM_VENOM_SAC, 1), IN(ITEM_FEATHER, 1)},
        3,
        ITEM_POTION,
        1,
        false,
        ITEM_MAT_NONE,
        0,
    },
    {
        "Iron Ingot",
        "Smelt iron ore with coal",
        CRAFT_STATION_FORGE,
        {IN(ITEM_IRON_ORE, 1), IN(ITEM_COAL, 1)},
        2,
        ITEM_IRON_INGOT,
        1,
        false,
        ITEM_MAT_IRON,
        0,
    },
    {
        "Copper Ingot",
        "Smelt copper ore with coal",
        CRAFT_STATION_FORGE,
        {IN(ITEM_COPPER_ORE, 1), IN(ITEM_COAL, 1)},
        2,
        ITEM_COPPER_INGOT,
        1,
        false,
        ITEM_MAT_COPPER,
        0,
    },
    {
        "Tin Ingot",
        "Smelt tin ore with coal",
        CRAFT_STATION_FORGE,
        {IN(ITEM_TIN_ORE, 1), IN(ITEM_COAL, 1)},
        2,
        ITEM_TIN_INGOT,
        1,
        false,
        ITEM_MAT_COPPER,
        0,
    },
    {
        "Brick",
        "Fire clay into brick",
        CRAFT_STATION_FORGE,
        {IN(ITEM_CLAY, 2), IN(ITEM_COAL, 1)},
        2,
        ITEM_BRICK,
        1,
        false,
        ITEM_MAT_CLAY,
        0,
    },
    {
        "Cooked Meal",
        "Cook raw meat over the forge",
        CRAFT_STATION_FORGE,
        {IN(ITEM_RAW_MEAT, 1), IN(ITEM_COAL, 1)},
        2,
        ITEM_FOOD,
        1,
        false,
        ITEM_MAT_NONE,
        0,
    },
    {
        "Iron Sword",
        "Forge a blade on the anvil",
        CRAFT_STATION_ANVIL,
        {IN(ITEM_IRON_INGOT, 2), IN(ITEM_STICK, 1)},
        2,
        ITEM_WEAPON_SWORD,
        1,
        true,
        ITEM_MAT_IRON,
        65,
    },
    {
        "Iron Dagger",
        "A short forged blade",
        CRAFT_STATION_ANVIL,
        {IN(ITEM_IRON_INGOT, 1), IN(ITEM_STICK, 1)},
        2,
        ITEM_WEAPON_DAGGER,
        1,
        true,
        ITEM_MAT_IRON,
        65,
    },
    {
        "Iron Spear",
        "Forged head on a shaft",
        CRAFT_STATION_ANVIL,
        {IN(ITEM_IRON_INGOT, 1), IN(ITEM_STICK, 2)},
        2,
        ITEM_WEAPON_SPEAR,
        1,
        true,
        ITEM_MAT_IRON,
        65,
    },
    {
        "Iron Core",
        "Heavy iron core armor",
        CRAFT_STATION_ANVIL,
        {IN(ITEM_IRON_INGOT, 5)},
        1,
        ITEM_CORE_ARMOR_IRON,
        1,
        true,
        ITEM_MAT_IRON,
        65,
    },
    {
        "Iron Leggings",
        "Plate leggings",
        CRAFT_STATION_ANVIL,
        {IN(ITEM_IRON_INGOT, 4)},
        1,
        ITEM_LEGGINGS_IRON,
        1,
        true,
        ITEM_MAT_IRON,
        65,
    },
    {
        "Iron Helmet",
        "Forged iron helm",
        CRAFT_STATION_ANVIL,
        {IN(ITEM_IRON_INGOT, 3)},
        1,
        ITEM_HELMET_IRON,
        1,
        true,
        ITEM_MAT_IRON,
        65,
    },
    {
        "Iron Gauntlets",
        "Forged iron gloves",
        CRAFT_STATION_ANVIL,
        {IN(ITEM_IRON_INGOT, 2)},
        1,
        ITEM_GAUNTLETS_IRON,
        1,
        true,
        ITEM_MAT_IRON,
        65,
    },
    {
        "Copper Ring",
        "A simple copper band",
        CRAFT_STATION_ANVIL,
        {IN(ITEM_COPPER_INGOT, 1)},
        1,
        ITEM_RING_COPPER,
        1,
        true,
        ITEM_MAT_COPPER,
        60,
    },
};

#undef IN

int craft_recipe_count(void)
{
    return (int)(sizeof(s_recipes) / sizeof(s_recipes[0]));
}

const CraftRecipe *craft_recipe_at(int index)
{
    if (index < 0 || index >= craft_recipe_count())
        return NULL;
    return &s_recipes[index];
}

int craft_session_list_recipes(const CraftSession *session, int *out_indices, int max_out)
{
    if (!session || !out_indices || max_out <= 0)
        return 0;
    int n = 0;
    const int total = craft_recipe_count();
    for (int i = 0; i < total && n < max_out; i++)
    {
        const CraftRecipe *r = &s_recipes[i];
        if (session->book_mode || r->station == session->station)
            out_indices[n++] = i;
    }
    return n;
}

bool craft_can_craft(const Inventory *inv, const CraftRecipe *recipe)
{
    if (!inv || !recipe)
        return false;
    if (recipe->input_count == 0 || recipe->output_id == ITEM_NONE || recipe->output_pieces == 0)
        return false;
    for (uint8_t i = 0; i < recipe->input_count; i++)
    {
        const CraftIngredient *in = &recipe->inputs[i];
        if (in->id == ITEM_NONE || in->pieces == 0)
            continue;
        if (inventory_count_pieces(inv, in->id) < in->pieces)
            return false;
    }
    return true;
}

bool craft_apply(Inventory *inv, const CraftRecipe *recipe, char *status, size_t status_sz)
{
    if (!inv || !recipe)
        return false;
    if (!craft_can_craft(inv, recipe))
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Missing materials");
        return false;
    }

    for (uint8_t i = 0; i < recipe->input_count; i++)
    {
        const CraftIngredient *in = &recipe->inputs[i];
        if (in->id == ITEM_NONE || in->pieces == 0)
            continue;
        if (!inventory_remove_pieces(inv, in->id, in->pieces))
        {
            if (status && status_sz)
                snprintf(status, status_sz, "Craft failed");
            return false;
        }
    }

    ItemStack out;
    if (recipe->output_gear)
    {
        ItemMaterial mat = recipe->output_material;
        if (mat == ITEM_MAT_NONE)
            mat = item_def(recipe->output_id)->default_material;
        uint8_t q = recipe->output_quality ? recipe->output_quality : 60;
        out = item_stack_make_gear(recipe->output_id, mat, q);
    }
    else
    {
        out = item_stack_make(recipe->output_id, recipe->output_pieces);
    }

    if (out.id == ITEM_NONE || out.pieces == 0 || !inventory_add_stack(inv, &out))
    {
        for (uint8_t i = 0; i < recipe->input_count; i++)
        {
            const CraftIngredient *in = &recipe->inputs[i];
            if (in->id != ITEM_NONE && in->pieces > 0)
                inventory_add_pieces(inv, in->id, in->pieces);
        }
        if (status && status_sz)
            snprintf(status, status_sz, "Bag full");
        return false;
    }

    if (status && status_sz)
        snprintf(status, status_sz, "Crafted %s", recipe->name);
    return true;
}

bool craft_find_nearby_station(World *world, float x, float y, float z, float range,
                               CraftStation *out_station, int *out_vx, int *out_vy, int *out_vz)
{
    if (!world || range <= 0.0f)
        return false;

    const int cx = (int)floorf(x);
    const int cy = (int)floorf(y);
    const int cz = (int)floorf(z);
    const int r = (int)ceilf(range);
    float best_d2 = range * range + 1.0f;
    bool found = false;
    CraftStation best_station = CRAFT_STATION_COUNT;
    int best_x = 0, best_y = 0, best_z = 0;

    for (int dz = -r; dz <= r; dz++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                const int vx = cx + dx;
                const int vy = cy + dy;
                const int vz = cz + dz;
                if (vx < 0 || vy < 0 || vz < 0)
                    continue;
                if ((uint32_t)vx >= world->width || (uint32_t)vy >= world->height ||
                    (uint32_t)vz >= world->depth)
                    continue;
                const Voxel *v = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)vz);
                if (!v)
                    continue;
                CraftStation st = craft_station_from_voxel(v->type);
                if (st >= CRAFT_STATION_COUNT)
                    continue;
                const float fx = ((float)vx + 0.5f) - x;
                const float fy = ((float)vy + 0.5f) - y;
                const float fz = ((float)vz + 0.5f) - z;
                const float d2 = fx * fx + fy * fy + fz * fz;
                if (d2 > best_d2)
                    continue;
                best_d2 = d2;
                best_station = st;
                best_x = vx;
                best_y = vy;
                best_z = vz;
                found = true;
            }

    if (!found)
        return false;
    if (out_station)
        *out_station = best_station;
    if (out_vx)
        *out_vx = best_x;
    if (out_vy)
        *out_vy = best_y;
    if (out_vz)
        *out_vz = best_z;
    return true;
}
