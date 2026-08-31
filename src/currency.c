#include "currency.h"

#include <stdio.h>
#include <string.h>

void wallet_clear(Wallet *w)
{
    if (!w)
        return;
    w->copper = 0;
    w->silver = 0;
    w->gold = 0;
}

void wallet_set(Wallet *w, int gold, int silver, int copper)
{
    if (!w)
        return;
    w->gold = gold;
    w->silver = silver;
    w->copper = copper;
    wallet_normalize(w);
}

void wallet_normalize(Wallet *w)
{
    if (!w)
        return;

    int64_t total = wallet_total_copper(w);
    if (total < 0)
        total = 0;
    wallet_from_copper(w, total);
}

int64_t wallet_total_copper(const Wallet *w)
{
    if (!w)
        return 0;
    return (int64_t)w->gold * (int64_t)COPPER_PER_GOLD +
           (int64_t)w->silver * (int64_t)COPPER_PER_SILVER +
           (int64_t)w->copper;
}

void wallet_from_copper(Wallet *w, int64_t copper)
{
    if (!w)
        return;
    if (copper < 0)
        copper = 0;
    w->gold = (int)(copper / COPPER_PER_GOLD);
    copper %= COPPER_PER_GOLD;
    w->silver = (int)(copper / COPPER_PER_SILVER);
    w->copper = (int)(copper % COPPER_PER_SILVER);
}

bool wallet_can_afford(const Wallet *w, int64_t copper_cost)
{
    if (copper_cost <= 0)
        return true;
    return wallet_total_copper(w) >= copper_cost;
}

bool wallet_spend(Wallet *w, int64_t copper_cost)
{
    if (!w)
        return false;
    if (copper_cost <= 0)
        return true;
    int64_t total = wallet_total_copper(w);
    if (total < copper_cost)
        return false;
    wallet_from_copper(w, total - copper_cost);
    return true;
}

void wallet_add_copper(Wallet *w, int64_t copper)
{
    if (!w || copper == 0)
        return;
    int64_t total = wallet_total_copper(w) + copper;
    if (total < 0)
        total = 0;
    wallet_from_copper(w, total);
}

void wallet_format(const Wallet *w, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0)
        return;
    buf[0] = '\0';
    if (!w)
        return;

    Wallet n = *w;
    wallet_normalize(&n);

    if (n.gold == 0 && n.silver == 0 && n.copper == 0)
    {
        snprintf(buf, buf_size, "0c");
        return;
    }

    char tmp[64];
    tmp[0] = '\0';
    size_t used = 0;
    if (n.gold > 0)
    {
        int wrote = snprintf(tmp + used, sizeof(tmp) - used, "%dg", n.gold);
        if (wrote > 0)
            used += (size_t)wrote;
    }
    if (n.silver > 0)
    {
        int wrote = snprintf(tmp + used, sizeof(tmp) - used, "%s%ds",
                             used > 0 ? " " : "", n.silver);
        if (wrote > 0)
            used += (size_t)wrote;
    }
    if (n.copper > 0 || used == 0)
    {
        int wrote = snprintf(tmp + used, sizeof(tmp) - used, "%s%dc",
                             used > 0 ? " " : "", n.copper);
        if (wrote > 0)
            used += (size_t)wrote;
    }
    (void)used;
    snprintf(buf, buf_size, "%s", tmp);
}
