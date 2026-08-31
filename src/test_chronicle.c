// Headless coverage for chronicle, households, dialogue choices, and adventure hooks.
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "chronicle.h"
#include "household.h"
#include "dialogue.h"
#include "adventure_hooks.h"
#include "mob_ai.h"
#include "settlement.h"
#include "world.h"
#include "universe.h"
#include "constants.h"

static int failures = 0;

static void report(const char *name, bool ok)
{
  printf("  %s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok)
    failures++;
}

static void test_chronicle_determinism(void)
{
  printf("\n-- chronicle determinism --\n");
  Chronicle a, b;
  report("generate A", chronicle_generate(&a, "deadbeefcafebabe0123456789abcdef0123456789abcdef0123456789abcdef", 200));
  report("generate B", chronicle_generate(&b, "deadbeefcafebabe0123456789abcdef0123456789abcdef0123456789abcdef", 200));
  report("same civ count", a.civ_count == b.civ_count);
  report("same site count", a.site_count == b.site_count);
  report("same event count", a.event_count == b.event_count);
  report("has civs", a.civ_count > 0);
  report("has sites", a.site_count > 0);
  report("has events", a.event_count > 10);
  report("has culture deity", a.civs[0].deity[0] != '\0');
  report("has myth", a.civs[0].myth[0] != '\0');

  if (a.event_count > 0 && a.event_count == b.event_count)
  {
    bool match = true;
    for (uint32_t i = 0; i < a.event_count; i++)
    {
      if (a.events[i].type != b.events[i].type || a.events[i].year != b.events[i].year ||
          a.events[i].site_id != b.events[i].site_id)
      {
        match = false;
        break;
      }
    }
    report("events match", match);
  }

  const ChronicleEvent *evs[8];
  int n = chronicle_query_recent(&a, 8, evs);
  report("query recent", n > 0);
  char line[CHRONICLE_LINE_MAX];
  if (n > 0)
  {
    chronicle_format_line(&a, evs[0], line, sizeof(line));
    report("format line non-empty", line[0] != '\0');
    // Newest-first: first result year should be >= last result year.
    if (n >= 2)
      report("recent is newest-first", evs[0]->year >= evs[n - 1]->year);
    else
      report("recent is newest-first", true);
  }

  AdventureHook hooks[ADVENTURE_HOOK_MAX];
  int hn = adventure_hooks_generate(&a, 0, 0, 48, hooks, ADVENTURE_HOOK_MAX);
  report("adventure hooks nearby", hn >= 0);
  printf("       hooks=%d artifacts=%u\n", hn, (unsigned)a.artifact_count);
}

static void test_household_and_dialogue(void)
{
  printf("\n-- household + dialogue --\n");
  World *w = world_create(32, 32, 16);
  report("world created", w != NULL);
  if (!w)
    return;

  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      world_set_voxel(w, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
    }
  world_refresh_occupancy_bitfield(w);
  w->settlement_scale = 5;
  w->settlement_has_anchor = true;
  w->settlement_anchor_x = 16;
  w->settlement_anchor_y = 16;
  w->settlement_anchor_z = 2;
  w->universe_x = 3;
  w->universe_y = 4;
  w->universe_z = 0;

  Universe u;
  report("universe init", universe_init(&u, "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899", 0, 1));
  w->universe_context = &u;

  // Prefer a cell that has a settlement so chronicle site binds; scale override still spawns.
  report("spawn villagers", world_spawn_settlement_villagers(w));
  report("roster active", w->settlement_roster.active);
  report("has families", w->household_count > 0);
  report("has members", w->settlement_roster.member_count > 0);

  uint32_t fids[16];
  int nf = families_in_settlement(w, fids, 16);
  report("families_in_settlement", nf > 0);

  Actor *villager = NULL;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    if (mob_actor_is_villager(&w->runtime_actors[i]))
    {
      villager = &w->runtime_actors[i];
      break;
    }
  }
  report("found villager", villager != NULL);
  if (villager)
  {
    const MobActor *vm = (const MobActor *)villager->extra_data;
    report("settlement_id set", vm && vm->settlement_id != 0);
    report("family_id set", vm && vm->family_id != 0);
    report("traits rolled", vm && (vm->trait_curiosity != 0 || vm->trait_bravery != 0 ||
                                   vm->trait_loyalty != 0 || vm->trait_wrath != 0 ||
                                   vm->trait_greed != 0 || vm->trait_piety != 0));

    DialogueState d;
    report("begin conversation",
           dialogue_begin_conversation(&d, villager, w, &u.chronicle));
    report("has graph", d.has_graph);
    report("root choices", dialogue_choice_count(&d) >= 4 ||
                               (dialogue_advance(&d), dialogue_advance(&d),
                                dialogue_advance(&d), dialogue_awaiting_choice(&d)));

    // Finish lines into choice mode
    int guard = 0;
    while (dialogue_active(&d) && !dialogue_awaiting_choice(&d) && guard++ < 20)
      dialogue_advance(&d);
    report("awaiting choice", dialogue_awaiting_choice(&d));
    if (dialogue_awaiting_choice(&d))
    {
      report("choice labels", dialogue_choice_label(&d, 0)[0] != '\0');
      dialogue_choice_move(&d, 1);
      report("choice move", dialogue_selected_choice(&d) == 1);
      // Pick goodbye (last) to close
      d.selected_choice = dialogue_choice_count(&d) - 1;
      dialogue_choice_confirm(&d);
      report("goodbye closes", !dialogue_active(&d));
    }
  }

  universe_free(&u);
  world_destroy(w);
}

int main(void)
{
  printf("=== test_chronicle ===\n");
  test_chronicle_determinism();
  test_household_and_dialogue();
  printf("\n%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
