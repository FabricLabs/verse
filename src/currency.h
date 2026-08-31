#ifndef VERSE_CURRENCY_H
#define VERSE_CURRENCY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// General trading currencies. All amounts normalize to copper as the base unit:
//   100 copper = 1 silver
//   100 silver = 1 gold
#define COPPER_PER_SILVER 100
#define SILVER_PER_GOLD 100
#define COPPER_PER_GOLD (COPPER_PER_SILVER * SILVER_PER_GOLD)

typedef struct {
    int copper;
    int silver;
    int gold;
} Wallet;

void wallet_clear(Wallet *w);
void wallet_set(Wallet *w, int gold, int silver, int copper);
void wallet_normalize(Wallet *w);
int64_t wallet_total_copper(const Wallet *w);
void wallet_from_copper(Wallet *w, int64_t copper);
bool wallet_can_afford(const Wallet *w, int64_t copper_cost);
bool wallet_spend(Wallet *w, int64_t copper_cost);
void wallet_add_copper(Wallet *w, int64_t copper);
// Compact purse string, e.g. "2g 15s 8c" (omits zero denominations when possible).
void wallet_format(const Wallet *w, char *buf, size_t buf_size);

#endif // VERSE_CURRENCY_H
