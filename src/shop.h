#ifndef VERSE_SHOP_H
#define VERSE_SHOP_H

#include <stdbool.h>
#include <stdint.h>

#include "actor.h"
#include "currency.h"
#include "item.h"

struct World;
struct GameState;

#define SHOP_LISTING_MAX 64

// One sellable stack currently offered by a settlement villager (or the shopkeeper).
typedef struct {
    ItemStack stack;
    uint32_t owner_actor_id;
    int16_t owner_slot;
} ShopListing;

typedef struct {
    bool active;
    uint32_t merchant_id;
    char merchant_name[64];
    ShopListing listings[SHOP_LISTING_MAX];
    int listing_count;
} ShopSession;

void shop_session_clear(ShopSession *shop);
// Open a shop session against a living merchant; pools bag stock from the merchant and
// other living villagers in the same settlement.
bool shop_session_open(ShopSession *shop, struct World *world, Actor *merchant);
void shop_session_refresh(ShopSession *shop, struct World *world);

bool shop_buy_listing(struct GameState *state, int listing_index, char *status, size_t status_sz);
bool shop_sell_player_slot(struct GameState *state, int player_slot, char *status, size_t status_sz);

// Living merchant/shopkeeper in this world, or NULL.
Actor *shop_find_settlement_merchant(struct World *world);

#endif // VERSE_SHOP_H
