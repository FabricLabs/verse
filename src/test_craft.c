#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "craft.h"
#include "item.h"

#include "world.h"

/* Headless stub: nearby-station scan is covered by voxel mapping checks. */
Voxel *world_get_voxel(World *world, uint32_t x, uint32_t y, uint32_t z)
{
    (void)world;
    (void)x;
    (void)y;
    (void)z;
    return NULL;
}

static int failures = 0;

static void check(const char *name, bool ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok)
        failures++;
}

int main(void)
{
    printf("craft tests\n");

    check("recipe catalog non-empty", craft_recipe_count() > 0);
    check("crafting table voxel maps",
          craft_station_from_voxel(VOXEL_CRAFTING_TABLE) == CRAFT_STATION_TABLE);
    check("anvil voxel maps", craft_station_from_voxel(VOXEL_ANVIL) == CRAFT_STATION_ANVIL);
    check("forge voxel maps", craft_station_from_voxel(VOXEL_FORGE) == CRAFT_STATION_FORGE);
    check("stone is not a station", !craft_voxel_is_station(VOXEL_STONE));

    Inventory inv;
    inventory_init(&inv, 16);
    inventory_add_pieces(&inv, ITEM_WOOD_BLOCK, 256);
    inventory_add(&inv, ITEM_STICK, 1);

    CraftSession session;
    craft_session_open(&session, CRAFT_STATION_HAND, false);
    int indices[64];
    int n = craft_session_list_recipes(&session, indices, 64);
    check("hand recipes listed", n > 0);

    const CraftRecipe *club = NULL;
    for (int i = 0; i < n; i++)
    {
        const CraftRecipe *r = craft_recipe_at(indices[i]);
        if (r && strcmp(r->name, "Club") == 0)
            club = r;
    }
    check("club recipe found", club != NULL);
    check("club craftable with wood+stick", club && craft_can_craft(&inv, club));

    char status[64];
    bool made = club && craft_apply(&inv, club, status, sizeof(status));
    check("club craft succeeds", made);
    check("club in inventory", inventory_count_item(&inv, ITEM_WEAPON_CLUB) >= 1);
    check("wood consumed", inventory_count_pieces(&inv, ITEM_WOOD_BLOCK) < 256);

    inventory_clear(&inv);
    inventory_init(&inv, 16);
    inventory_add(&inv, ITEM_IRON_ORE, 1);
    inventory_add(&inv, ITEM_COAL, 1);
    craft_session_open(&session, CRAFT_STATION_FORGE, false);
    n = craft_session_list_recipes(&session, indices, 64);
    const CraftRecipe *ingot = NULL;
    for (int i = 0; i < n; i++)
    {
        const CraftRecipe *r = craft_recipe_at(indices[i]);
        if (r && r->output_id == ITEM_IRON_INGOT)
            ingot = r;
    }
    check("iron ingot recipe found", ingot != NULL);
    check("smelt works", ingot && craft_apply(&inv, ingot, status, sizeof(status)));
    check("ingot produced", inventory_count_item(&inv, ITEM_IRON_INGOT) >= 1);

    craft_session_open(&session, CRAFT_STATION_HAND, true);
    n = craft_session_list_recipes(&session, indices, 64);
    check("recipe book lists all", n == craft_recipe_count());

    if (failures)
    {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all craft tests passed\n");
    return 0;
}
