#include "adventure_hooks.h"

#include <stdio.h>
#include <string.h>

const char *adventure_hook_type_name(AdventureHookType t)
{
  static const char *names[] = {
      "none", "clear_ruin", "recover_artifact", "escort_migration", "avenge_raid", "slay_beast"};
  if (t < 0 || t >= ADVENTURE_HOOK_COUNT)
    return "unknown";
  return names[t];
}

static int manhattan(int ax, int ay, int bx, int by)
{
  int dx = ax - bx;
  int dy = ay - by;
  if (dx < 0)
    dx = -dx;
  if (dy < 0)
    dy = -dy;
  return dx + dy;
}

int adventure_hooks_generate(const Chronicle *c, int gx, int gy, int radius,
                             AdventureHook *out_hooks, int max_hooks)
{
  if (!c || !out_hooks || max_hooks <= 0)
    return 0;
  if (radius < 1)
    radius = 1;

  int n = 0;

  for (uint32_t i = 0; i < c->site_count && n < max_hooks; i++)
  {
    const ChronicleSite *s = &c->sites[i];
    if (manhattan(gx, gy, s->gx, s->gy) > radius)
      continue;

    if (s->ruin || s->kind == CHRONICLE_SITE_RUIN)
    {
      AdventureHook *h = &out_hooks[n++];
      memset(h, 0, sizeof(*h));
      h->type = ADVENTURE_HOOK_CLEAR_RUIN;
      h->site_id = s->id;
      h->civ_id = s->civ_id;
      h->target_gx = s->gx;
      h->target_gy = s->gy;
      snprintf(h->title, sizeof(h->title), "Clear %s", s->name);
      snprintf(h->description, sizeof(h->description),
               "The ruins of %s still harbor danger. Explore and clear them.", s->name);
      continue;
    }

    if (s->has_lair || s->kind == CHRONICLE_SITE_LAIR)
    {
      AdventureHook *h = &out_hooks[n++];
      memset(h, 0, sizeof(*h));
      h->type = ADVENTURE_HOOK_SLAY_BEAST;
      h->site_id = s->id;
      h->civ_id = s->civ_id;
      h->target_gx = s->gx;
      h->target_gy = s->gy;
      snprintf(h->title, sizeof(h->title), "Hunt near %s", s->name);
      snprintf(h->description, sizeof(h->description),
               "A unique beast is said to lurk near %s.", s->name);
    }
  }

  // Artifact recovery from chronicle artifacts whose sites are nearby.
  for (uint32_t i = 0; i < c->artifact_count && n < max_hooks; i++)
  {
    const ChronicleArtifact *a = &c->artifacts[i];
    const ChronicleSite *s = chronicle_site(c, a->site_id);
    if (!s || manhattan(gx, gy, s->gx, s->gy) > radius)
      continue;
    AdventureHook *h = &out_hooks[n++];
    memset(h, 0, sizeof(*h));
    h->type = ADVENTURE_HOOK_RECOVER_ARTIFACT;
    h->site_id = a->site_id;
    h->artifact_id = a->id;
    h->civ_id = a->civ_id;
    h->target_gx = s->gx;
    h->target_gy = s->gy;
    snprintf(h->title, sizeof(h->title), "Recover %s", a->name);
    snprintf(h->description, sizeof(h->description),
             "Seek the %s last known at %s.", a->name, s->name);
  }

  // Recent raid / migration events near player.
  for (uint32_t i = 0; i < c->event_count && n < max_hooks; i++)
  {
    const ChronicleEvent *ev = &c->events[i];
    const ChronicleSite *s = chronicle_site(c, ev->site_id);
    if (!s || manhattan(gx, gy, s->gx, s->gy) > radius)
      continue;
    if (ev->type == CHRONICLE_EV_WAR_RAID)
    {
      AdventureHook *h = &out_hooks[n++];
      memset(h, 0, sizeof(*h));
      h->type = ADVENTURE_HOOK_AVENGE_RAID;
      h->site_id = s->id;
      h->civ_id = ev->civ_a;
      h->target_gx = s->gx;
      h->target_gy = s->gy;
      snprintf(h->title, sizeof(h->title), "Avenge the raid on %s", s->name);
      snprintf(h->description, sizeof(h->description),
               "Year %u: a raid struck %s. Seek justice.", (unsigned)ev->year, s->name);
    }
    else if (ev->type == CHRONICLE_EV_MIGRATION && n < max_hooks)
    {
      AdventureHook *h = &out_hooks[n++];
      memset(h, 0, sizeof(*h));
      h->type = ADVENTURE_HOOK_ESCORT_MIGRATION;
      h->site_id = s->id;
      h->civ_id = ev->civ_a;
      h->target_gx = s->gx;
      h->target_gy = s->gy;
      snprintf(h->title, sizeof(h->title), "Aid travelers near %s", s->name);
      snprintf(h->description, sizeof(h->description),
               "Displaced folk near %s need an escort.", s->name);
    }
  }

  (void)gx;
  return n;
}
