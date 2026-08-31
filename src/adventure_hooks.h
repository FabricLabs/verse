#ifndef ADVENTURE_HOOKS_H
#define ADVENTURE_HOOKS_H

#include <stdbool.h>
#include <stdint.h>

#include "chronicle.h"

#define ADVENTURE_HOOK_TITLE_MAX 64
#define ADVENTURE_HOOK_DESC_MAX 160
#define ADVENTURE_HOOK_MAX 8

typedef enum {
  ADVENTURE_HOOK_NONE = 0,
  ADVENTURE_HOOK_CLEAR_RUIN,
  ADVENTURE_HOOK_RECOVER_ARTIFACT,
  ADVENTURE_HOOK_ESCORT_MIGRATION,
  ADVENTURE_HOOK_AVENGE_RAID,
  ADVENTURE_HOOK_SLAY_BEAST,
  ADVENTURE_HOOK_COUNT
} AdventureHookType;

typedef struct {
  AdventureHookType type;
  uint32_t site_id;
  uint32_t artifact_id;
  uint32_t civ_id;
  int16_t target_gx, target_gy;
  char title[ADVENTURE_HOOK_TITLE_MAX];
  char description[ADVENTURE_HOOK_DESC_MAX];
} AdventureHook;

// Fill out_hooks with candidates near (gx,gy). Returns count.
int adventure_hooks_generate(const Chronicle *c, int gx, int gy, int radius,
                             AdventureHook *out_hooks, int max_hooks);

const char *adventure_hook_type_name(AdventureHookType t);

#endif // ADVENTURE_HOOKS_H
