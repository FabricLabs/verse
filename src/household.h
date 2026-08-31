#ifndef HOUSEHOLD_H
#define HOUSEHOLD_H

#include <stdbool.h>
#include <stdint.h>

struct World;

// Runtime settlement roster and family/household registry for live villager worlds.

#define HOUSEHOLD_NAME_MAX 20
#define HOUSEHOLD_MAX_MEMBERS 16
#define HOUSEHOLD_MAX_FAMILIES 16
#define SETTLEMENT_ROSTER_MAX_MEMBERS 64

typedef struct {
  uint32_t family_id;
  char family_name[HOUSEHOLD_NAME_MAX];
  uint32_t settlement_id;
  uint32_t member_ids[HOUSEHOLD_MAX_MEMBERS];
  uint8_t member_count;
  uint32_t head_id;
} Household;

typedef struct {
  uint32_t settlement_id;
  int gx, gy;
  char name[48];
  uint32_t civ_id;
  uint32_t member_ids[SETTLEMENT_ROSTER_MAX_MEMBERS];
  uint8_t member_count;
  uint32_t family_ids[HOUSEHOLD_MAX_FAMILIES];
  uint8_t family_count;
  bool active;
} SettlementRoster;

void household_registry_clear(struct World *world);
SettlementRoster *world_settlement_roster(struct World *world);
const SettlementRoster *world_settlement_roster_const(const struct World *world);

bool household_register_family(struct World *world, uint32_t family_id, const char *family_name,
                               uint32_t settlement_id, uint32_t head_id);
bool household_add_member(struct World *world, uint32_t family_id, uint32_t actor_id);
bool settlement_roster_add_member(struct World *world, uint32_t actor_id);
bool settlement_roster_add_family(struct World *world, uint32_t family_id);

Household *household_find(struct World *world, uint32_t family_id);
const Household *household_find_const(const struct World *world, uint32_t family_id);

int household_members(const struct World *world, uint32_t family_id, uint32_t *out_ids, int max_out);
int settlement_members(const struct World *world, uint32_t *out_ids, int max_out);
int families_in_settlement(const struct World *world, uint32_t *out_family_ids, int max_out);

void settlement_roster_bind_site(struct World *world, uint32_t settlement_id, int gx, int gy,
                                 uint32_t civ_id, const char *display_name);

#endif // HOUSEHOLD_H
