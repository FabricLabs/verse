#include "household.h"
#include "world.h"

#include <stdio.h>
#include <string.h>

// Household + settlement roster live on World (see world.h extension).

void household_registry_clear(World *world)
{
  if (!world)
    return;
  memset(&world->settlement_roster, 0, sizeof(world->settlement_roster));
  memset(world->households, 0, sizeof(world->households));
  world->household_count = 0;
}

SettlementRoster *world_settlement_roster(World *world)
{
  return world ? &world->settlement_roster : NULL;
}

const SettlementRoster *world_settlement_roster_const(const World *world)
{
  return world ? &world->settlement_roster : NULL;
}

void settlement_roster_bind_site(World *world, uint32_t settlement_id, int gx, int gy,
                                 uint32_t civ_id, const char *display_name)
{
  if (!world)
    return;
  SettlementRoster *r = &world->settlement_roster;
  r->active = true;
  r->settlement_id = settlement_id ? settlement_id : 1u;
  r->gx = gx;
  r->gy = gy;
  r->civ_id = civ_id;
  if (display_name && display_name[0])
  {
    strncpy(r->name, display_name, sizeof(r->name) - 1);
    r->name[sizeof(r->name) - 1] = '\0';
  }
  else if (!r->name[0])
  {
    snprintf(r->name, sizeof(r->name), "Settlement");
  }
}

bool household_register_family(World *world, uint32_t family_id, const char *family_name,
                               uint32_t settlement_id, uint32_t head_id)
{
  if (!world || !family_id)
    return false;
  Household *existing = household_find(world, family_id);
  if (existing)
  {
    if (family_name && family_name[0] && !existing->family_name[0])
    {
      strncpy(existing->family_name, family_name, HOUSEHOLD_NAME_MAX - 1);
      existing->family_name[HOUSEHOLD_NAME_MAX - 1] = '\0';
    }
    if (head_id && !existing->head_id)
      existing->head_id = head_id;
    return true;
  }
  if (world->household_count >= HOUSEHOLD_MAX_FAMILIES)
    return false;
  Household *h = &world->households[world->household_count++];
  memset(h, 0, sizeof(*h));
  h->family_id = family_id;
  h->settlement_id = settlement_id;
  h->head_id = head_id;
  if (family_name)
  {
    strncpy(h->family_name, family_name, HOUSEHOLD_NAME_MAX - 1);
    h->family_name[HOUSEHOLD_NAME_MAX - 1] = '\0';
  }
  settlement_roster_add_family(world, family_id);
  return true;
}

bool household_add_member(World *world, uint32_t family_id, uint32_t actor_id)
{
  Household *h = household_find(world, family_id);
  if (!h || !actor_id)
    return false;
  for (uint8_t i = 0; i < h->member_count; i++)
  {
    if (h->member_ids[i] == actor_id)
      return true;
  }
  if (h->member_count >= HOUSEHOLD_MAX_MEMBERS)
    return false;
  h->member_ids[h->member_count++] = actor_id;
  if (!h->head_id)
    h->head_id = actor_id;
  settlement_roster_add_member(world, actor_id);
  return true;
}

bool settlement_roster_add_member(World *world, uint32_t actor_id)
{
  if (!world || !actor_id)
    return false;
  SettlementRoster *r = &world->settlement_roster;
  r->active = true;
  for (uint8_t i = 0; i < r->member_count; i++)
  {
    if (r->member_ids[i] == actor_id)
      return true;
  }
  if (r->member_count >= SETTLEMENT_ROSTER_MAX_MEMBERS)
    return false;
  r->member_ids[r->member_count++] = actor_id;
  return true;
}

bool settlement_roster_add_family(World *world, uint32_t family_id)
{
  if (!world || !family_id)
    return false;
  SettlementRoster *r = &world->settlement_roster;
  r->active = true;
  for (uint8_t i = 0; i < r->family_count; i++)
  {
    if (r->family_ids[i] == family_id)
      return true;
  }
  if (r->family_count >= HOUSEHOLD_MAX_FAMILIES)
    return false;
  r->family_ids[r->family_count++] = family_id;
  return true;
}

Household *household_find(World *world, uint32_t family_id)
{
  if (!world || !family_id)
    return NULL;
  for (uint8_t i = 0; i < world->household_count; i++)
  {
    if (world->households[i].family_id == family_id)
      return &world->households[i];
  }
  return NULL;
}

const Household *household_find_const(const World *world, uint32_t family_id)
{
  return household_find((World *)world, family_id);
}

int household_members(const World *world, uint32_t family_id, uint32_t *out_ids, int max_out)
{
  const Household *h = household_find_const(world, family_id);
  if (!h || !out_ids || max_out <= 0)
    return 0;
  int n = 0;
  for (uint8_t i = 0; i < h->member_count && n < max_out; i++)
    out_ids[n++] = h->member_ids[i];
  return n;
}

int settlement_members(const World *world, uint32_t *out_ids, int max_out)
{
  if (!world || !out_ids || max_out <= 0)
    return 0;
  const SettlementRoster *r = &world->settlement_roster;
  int n = 0;
  for (uint8_t i = 0; i < r->member_count && n < max_out; i++)
    out_ids[n++] = r->member_ids[i];
  return n;
}

int families_in_settlement(const World *world, uint32_t *out_family_ids, int max_out)
{
  if (!world || !out_family_ids || max_out <= 0)
    return 0;
  const SettlementRoster *r = &world->settlement_roster;
  int n = 0;
  for (uint8_t i = 0; i < r->family_count && n < max_out; i++)
    out_family_ids[n++] = r->family_ids[i];
  return n;
}
