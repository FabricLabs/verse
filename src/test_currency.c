#include "currency.h"
#include "item.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void report(const char *name, bool ok)
{
  printf("  %s %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok)
    failures++;
}

static void test_wallet(void)
{
  printf("\n-- wallet --\n");
  Wallet w;
  wallet_set(&w, 1, 50, 150);
  report("normalize carries copper into silver", w.copper == 50 && w.silver == 51 && w.gold == 1);
  report("total copper matches", wallet_total_copper(&w) ==
                                     (int64_t)1 * COPPER_PER_GOLD + 51 * COPPER_PER_SILVER + 50);

  report("can afford exact total", wallet_can_afford(&w, wallet_total_copper(&w)));
  report("cannot afford +1", !wallet_can_afford(&w, wallet_total_copper(&w) + 1));

  int64_t before = wallet_total_copper(&w);
  report("spend 125c", wallet_spend(&w, 125));
  report("balance after spend", wallet_total_copper(&w) == before - 125);

  wallet_add_copper(&w, 100);
  report("add one silver of copper", wallet_total_copper(&w) == before - 25);

  char buf[48];
  wallet_set(&w, 2, 0, 0);
  wallet_format(&w, buf, sizeof(buf));
  report("format gold only", strcmp(buf, "2g") == 0);

  wallet_clear(&w);
  wallet_format(&w, buf, sizeof(buf));
  report("format empty", strcmp(buf, "0c") == 0);
}

static void test_prices(void)
{
  printf("\n-- item prices --\n");
  ItemStack food = item_stack_make(ITEM_FOOD, 1);
  ItemStack potion = item_stack_make(ITEM_POTION, 1);
  ItemStack note = item_stack_make(ITEM_HUNTERS_NOTE, 1);
  ItemStack dagger = item_stack_make_gear(ITEM_WEAPON_DAGGER, ITEM_MAT_COPPER, 50);

  report("food has buy price", item_buy_price_copper(&food) == 5);
  report("potion costs more than food", item_buy_price_copper(&potion) > item_buy_price_copper(&food));
  report("quest note not for sale", item_buy_price_copper(&note) == 0);
  report("sell is half buy", item_sell_price_copper(&food) == 2);
  report("gear has a price", item_buy_price_copper(&dagger) > 0);
}

int main(void)
{
  printf("=== Currency / price tests ===\n");
  test_wallet();
  test_prices();
  printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures,
         failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
