#include "shop.h"

#include <stdio.h>
#include <string.h>

#include "game_state.h"
#include "mob_ai.h"
#include "world.h"

void shop_session_clear(ShopSession *shop)
{
    if (!shop)
        return;
    memset(shop, 0, sizeof(*shop));
}

static bool actor_is_living_villager(const Actor *a)
{
    return a && a->is_active && a->health > 0 && mob_actor_is_villager(a);
}

Actor *shop_find_settlement_merchant(World *world)
{
    if (!world)
        return NULL;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!actor_is_living_villager(a))
            continue;
        if (mob_actor_villager_profession(a) == VILLAGER_JOB_MERCHANT)
            return a;
    }
    return NULL;
}

static void shop_add_listing(ShopSession *shop, Actor *owner, int slot)
{
    if (!shop || !owner || shop->listing_count >= SHOP_LISTING_MAX)
        return;
    if (slot < 0 || slot >= (int)owner->inventory.capacity)
        return;
    ItemStack *s = &owner->inventory.slots[slot];
    if (s->id == ITEM_NONE || s->pieces == 0)
        return;
    if (item_buy_price_copper(s) <= 0)
        return;

    ShopListing *L = &shop->listings[shop->listing_count++];
    L->stack = *s;
    L->owner_actor_id = owner->id;
    L->owner_slot = (int16_t)slot;
}

void shop_session_refresh(ShopSession *shop, World *world)
{
    if (!shop || !shop->active || !world)
        return;

    Actor *merchant = world_find_runtime_actor(world, shop->merchant_id);
    shop->listing_count = 0;
    if (!merchant || !actor_is_living_villager(merchant))
        return;

    uint32_t settlement_id = mob_actor_settlement_id(merchant);

    for (uint16_t i = 0; i < merchant->inventory.capacity; i++)
        shop_add_listing(shop, merchant, (int)i);

    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (a == merchant || !actor_is_living_villager(a))
            continue;
        if (settlement_id != 0 && mob_actor_settlement_id(a) != settlement_id)
            continue;
        if (mob_actor_villager_kind(a) == VILLAGER_CHILD)
            continue;
        for (uint16_t s = 0; s < a->inventory.capacity; s++)
            shop_add_listing(shop, a, (int)s);
    }
}

bool shop_session_open(ShopSession *shop, World *world, Actor *merchant)
{
    if (!shop || !world || !merchant || !actor_is_living_villager(merchant))
        return false;
    if (mob_actor_villager_profession(merchant) != VILLAGER_JOB_MERCHANT)
        return false;

    shop_session_clear(shop);
    shop->active = true;
    shop->merchant_id = merchant->id;
    strncpy(shop->merchant_name, merchant->name[0] ? merchant->name : "Shopkeeper",
            sizeof(shop->merchant_name) - 1);
    shop_session_refresh(shop, world);
    return true;
}

static Wallet *merchant_purse(Actor *merchant)
{
    if (!merchant || !merchant->extra_data)
        return NULL;
    MobActor *m = (MobActor *)merchant->extra_data;
    return &m->purse;
}

bool shop_buy_listing(GameState *state, int listing_index, char *status, size_t status_sz)
{
    if (!state || !state->player || !state->current_world || !state->shop.active)
        return false;
    if (listing_index < 0 || listing_index >= state->shop.listing_count)
        return false;

    ShopListing listing = state->shop.listings[listing_index];
    int64_t price = item_buy_price_copper(&listing.stack);
    if (price <= 0)
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Not for sale");
        return false;
    }
    if (!wallet_can_afford(&state->purse, price))
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Not enough coin");
        return false;
    }

    Actor *owner = world_find_runtime_actor(state->current_world, listing.owner_actor_id);
    if (!owner || !owner->is_active || owner->health == 0)
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Item gone");
        shop_session_refresh(&state->shop, state->current_world);
        return false;
    }
    if (listing.owner_slot < 0 ||
        listing.owner_slot >= (int)owner->inventory.capacity)
        return false;

    ItemStack *slot = &owner->inventory.slots[listing.owner_slot];
    if (slot->id != listing.stack.id || slot->pieces != listing.stack.pieces)
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Stock changed");
        shop_session_refresh(&state->shop, state->current_world);
        return false;
    }

    ItemStack take = *slot;
    if (!inventory_add_stack(&state->player->inventory, &take))
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Inventory full");
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    wallet_spend(&state->purse, price);

    Actor *merchant = world_find_runtime_actor(state->current_world, state->shop.merchant_id);
    Wallet *mp = merchant_purse(merchant);
    if (mp)
        wallet_add_copper(mp, price);

    if (status && status_sz)
    {
        char price_buf[32];
        Wallet tmp;
        wallet_from_copper(&tmp, price);
        wallet_format(&tmp, price_buf, sizeof(price_buf));
        snprintf(status, status_sz, "Bought %s for %s", item_name(take.id), price_buf);
    }
    shop_session_refresh(&state->shop, state->current_world);
    return true;
}

bool shop_sell_player_slot(GameState *state, int player_slot, char *status, size_t status_sz)
{
    if (!state || !state->player || !state->current_world || !state->shop.active)
        return false;
    if (player_slot < 0 || player_slot >= (int)state->player->inventory.capacity)
        return false;

    ItemStack *slot = &state->player->inventory.slots[player_slot];
    if (slot->id == ITEM_NONE || slot->pieces == 0)
        return false;

    int64_t price = item_sell_price_copper(slot);
    if (price <= 0)
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Shop won't buy that");
        return false;
    }

    Actor *merchant = world_find_runtime_actor(state->current_world, state->shop.merchant_id);
    if (!merchant || !actor_is_living_villager(merchant))
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Shopkeeper gone");
        return false;
    }

    Wallet *mp = merchant_purse(merchant);
    if (!mp || !wallet_can_afford(mp, price))
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Shopkeeper is short on coin");
        return false;
    }

    ItemStack take = *slot;
    if (!inventory_add_stack(&merchant->inventory, &take))
    {
        if (status && status_sz)
            snprintf(status, status_sz, "Shop stock is full");
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    wallet_spend(mp, price);
    wallet_add_copper(&state->purse, price);

    game_state_notify_item_sold(state, take.id);

    if (status && status_sz)
    {
        char price_buf[32];
        Wallet tmp;
        wallet_from_copper(&tmp, price);
        wallet_format(&tmp, price_buf, sizeof(price_buf));
        snprintf(status, status_sz, "Sold %s for %s", item_name(take.id), price_buf);
    }
    shop_session_refresh(&state->shop, state->current_world);
    return true;
}
