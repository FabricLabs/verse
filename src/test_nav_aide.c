#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "nav_aide.h"
#include "storyline.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;

static void report(const char *name, bool ok)
{
  printf("  %s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok)
    failures++;
}

static void test_nav_aide_bearing(void)
{
  printf("\n-- nav aide bearing --\n");
  NavAide nav;
  nav_aide_clear(&nav);
  report("cleared aide is inactive", !nav_aide_active(&nav));

  nav_aide_set_target(&nav, 0, 0, 0, 70.5f, 64.5f, 5.0f, "Hunter's Shack");
  report("set target activates aide", nav_aide_active(&nav));
  report("label stored", strcmp(nav.label, "Hunter's Shack") == 0);

  float bearing = 0.0f;
  // Player at (64.5, 64.5), target at (70.5, 64.5) → east → bearing ~ 0
  report("bearing resolves",
         nav_aide_world_bearing(&nav, 0, 0, 0, 64.5f, 64.5f, 128, 128, &bearing));
  report("bearing points roughly east", fabsf(bearing) < 0.05f);

  // Facing east while target is east → relative 0 (arrow points screen-up / forward).
  report("relative yaw is ~0 when facing the target",
         fabsf(nav_aide_relative_yaw(bearing, 0.0f)) < 0.05f);
  // Facing north (π/2) with target east (0) → relative −π/2 (arrow points screen-left).
  {
    const float rel = nav_aide_relative_yaw(0.0f, (float)(M_PI * 0.5));
    report("relative yaw is ~-π/2 when target is to the right of facing",
           fabsf(rel + (float)(M_PI * 0.5)) < 0.05f);
  }
  // Wrap across ±π.
  {
    const float rel = nav_aide_relative_yaw(-(float)M_PI * 0.9f, (float)M_PI * 0.9f);
    report("relative yaw wraps across ±π", fabsf(rel + (float)(M_PI * 0.2)) < 0.05f ||
                                               fabsf(rel - (float)(M_PI * 0.2)) < 0.05f);
  }

  // Neighbor cell west of target world: player in (-1,0) at local x=120 → abs ~ -8, target abs 70.5
  report("cross-cell bearing resolves",
         nav_aide_world_bearing(&nav, (uint64_t)(int64_t)-1, 0, 0, 120.5f, 64.5f, 128, 128, &bearing));
  report("cross-cell bearing still points east", bearing > -0.2f && bearing < 0.2f);

  nav_aide_clear(&nav);
  report("clear deactivates", !nav_aide_active(&nav));
}

static void test_nav_aide_distance(void)
{
  printf("\n-- nav aide distance --\n");
  NavAide nav;
  nav_aide_clear(&nav);
  nav_aide_set_target(&nav, 0, 0, 0, 70.5f, 64.5f, 5.0f, "Hunter's Shack");

  float dist = 0.0f;
  report("distance resolves",
         nav_aide_planar_distance(&nav, 0, 0, 0, 64.5f, 64.5f, 128, 128, &dist));
  report("same-cell distance is ~6m", fabsf(dist - 6.0f) < 0.05f);

  char buf[16];
  report("hides distance at or under 50m", !nav_aide_format_distance(50.0f, buf, sizeof(buf)));
  report("hides nearby objective distance", !nav_aide_format_distance(6.0f, buf, sizeof(buf)));
  report("shows distance above 50m", nav_aide_format_distance(51.0f, buf, sizeof(buf)) &&
                                         strcmp(buf, "51m") == 0);
  report("formats hundreds of metres", nav_aide_format_distance(512.0f, buf, sizeof(buf)) &&
                                           strcmp(buf, "512m") == 0);
  report("formats kilometres", nav_aide_format_distance(1664.0f, buf, sizeof(buf)) &&
                                   strcmp(buf, "1.7km") == 0);

  // One cell west: player in cell -1 near east edge → target in cell 0.
  report("cross-cell distance resolves",
         nav_aide_planar_distance(&nav, (uint64_t)(int64_t)-1, 0, 0, 120.5f, 64.5f, 128, 128, &dist));
  // From x=-1*128+120.5=-7.5 to 70.5 → 78m
  report("cross-cell distance is ~78m", fabsf(dist - 78.0f) < 0.05f);
  report("cross-cell shows distance label", nav_aide_format_distance(dist, buf, sizeof(buf)) &&
                                                strcmp(buf, "78m") == 0);
}

static void test_activate_hunters_quest(void)
{
  printf("\n-- activate hunter's shack quest --\n");
  StorylineState story;
  storyline_init(&story);
  report("quest file activates", storyline_activate_quest(&story, "00001-locate-hunters-shack"));
  report("quest is active", storyline_has_active_quest(&story));
  const StorylineQuest *q = storyline_active_quest(&story);
  report("quest name matches", q && q->name && strstr(q->name, "Hunter") != NULL);
  storyline_update_quest_progress(&story, "locate", "hunters_shack", 1);
  report("quest completes on progress", !storyline_has_active_quest(&story) &&
                                            story.active_quest.is_completed);
  storyline_destroy(&story);
}

static void test_quest_chain_and_objectives(void)
{
  printf("\n-- quest chain note → wool → town --\n");
  StorylineState story;
  storyline_init(&story);

  report("note quest activates", storyline_activate_quest(&story, "00002-find-hunters-note"));
  report("note quest active", storyline_has_active_quest(&story));
  const StorylineQuest *q = storyline_active_quest(&story);
  report("note quest has one objective", q && q->objectives_count == 1);
  storyline_update_quest_progress(&story, "locate", "hunters_note", 1);
  report("note quest completes", !storyline_has_active_quest(&story) && story.active_quest.is_completed);

  report("wool quest activates", storyline_activate_quest(&story, "00003-harvest-sheep-wool"));
  q = storyline_active_quest(&story);
  report("wool quest has two objectives", q && q->objectives_count == 2);
  storyline_update_quest_progress(&story, "kill", "sheep", 1);
  report("wool quest still active after kill", storyline_has_active_quest(&story));
  q = storyline_active_quest(&story);
  report("one wool objective completed", q && q->completed_objectives == 1);
  // Wrong target must not advance.
  storyline_update_quest_progress(&story, "harvest", "stone", 1);
  q = storyline_active_quest(&story);
  report("mismatched harvest target ignored", q && q->completed_objectives == 1);
  storyline_update_quest_progress(&story, "harvest", "wool", 1);
  report("wool quest completes after harvest", !storyline_has_active_quest(&story) &&
                                                 story.active_quest.is_completed);

  report("town quest activates", storyline_activate_quest(&story, "00004-reach-nearest-town"));
  q = storyline_active_quest(&story);
  report("town quest name matches", q && q->name && strstr(q->name, "Town") != NULL);
  report("town quest has locate objective",
         q && q->objectives_count == 1 && q->objectives[0].target &&
             strcmp(q->objectives[0].target, "nearest_town") == 0);
  storyline_update_quest_progress(&story, "locate", "nearest_town", 1);
  report("town quest completes on arrive", !storyline_has_active_quest(&story) &&
                                              story.active_quest.is_completed);

  report("shopkeeper quest activates",
         storyline_activate_quest(&story, "00005-find-shopkeeper"));
  q = storyline_active_quest(&story);
  report("shopkeeper quest name matches",
         q && q->name && strstr(q->name, "Shopkeeper") != NULL);
  report("shopkeeper quest has locate objective",
         q && q->objectives_count == 1 && q->objectives[0].target &&
             strcmp(q->objectives[0].target, "shopkeeper") == 0);
  storyline_update_quest_progress(&story, "locate", "shopkeeper", 1);
  report("shopkeeper quest completes on talk", !storyline_has_active_quest(&story) &&
                                                   story.active_quest.is_completed);

  report("sell wool quest activates", storyline_activate_quest(&story, "00006-sell-wool"));
  q = storyline_active_quest(&story);
  report("sell wool quest name matches", q && q->name && strstr(q->name, "Wool") != NULL);
  report("sell wool quest has sell objective",
         q && q->objectives_count == 1 && q->objectives[0].type &&
             strcmp(q->objectives[0].type, "sell") == 0 && q->objectives[0].target &&
             strcmp(q->objectives[0].target, "wool") == 0);
  storyline_update_quest_progress(&story, "sell", "wool", 1);
  report("sell wool quest completes", !storyline_has_active_quest(&story) &&
                                          story.active_quest.is_completed);

  storyline_destroy(&story);
}

int main(void)
{
  printf("=== Nav Aide / Quest Tests ===\n");
  test_nav_aide_bearing();
  test_nav_aide_distance();
  test_activate_hunters_quest();
  test_quest_chain_and_objectives();
  printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
