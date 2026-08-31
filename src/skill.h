#ifndef VERSE_SKILL_H
#define VERSE_SKILL_H

#include "player_controls_types.h"

struct GameState;
struct PlayerControls;
struct Actor;

// Table-driven skills. Add a SkillId, a SkillDef row, and the use/ready
// callbacks — the hotbar, HUD, and key dispatch all read this table.

#define SKILL_FLAG_NONE     0u
#define SKILL_FLAG_TOGGLE   (1u << 0)
#define SKILL_FLAG_CONTEXT  (1u << 1)
// Granted at character creation; does not spend a skill point.
#define SKILL_FLAG_BASE     (1u << 2)
// Offered on the skill tree; requires an unlock spend.
#define SKILL_FLAG_UNLOCKABLE (1u << 3)
// Can spend skill points to raise rank (I → II → III).
#define SKILL_FLAG_RANKABLE (1u << 4)

// Skill power ranks: basic / expanded / powerful.
#define SKILL_RANK_BASIC    1u
#define SKILL_RANK_EXPANDED 2u
#define SKILL_RANK_POWERFUL 3u
#define SKILL_RANK_MAX      3u

typedef bool (*SkillUseFn)(struct GameState *state, struct PlayerControls *ctrl);
typedef bool (*SkillQueryFn)(struct GameState *state, const struct PlayerControls *ctrl);

typedef struct SkillDef
{
    SkillId id;
    const char *name;
    const char *abbrev;
    const char *blurb; // short skill-tree description
    uint32_t flags;
    // Spirit level required for ranks I, II, III (index 0..2).
    uint8_t min_level[SKILL_RANK_MAX];
    uint8_t fill_r, fill_g, fill_b;
    uint8_t border_r, border_g, border_b;
    SkillQueryFn available;
    SkillQueryFn ready;
    SkillQueryFn highlighted;
    SkillUseFn use;
} SkillDef;

const SkillDef *skill_def(SkillId id);
const char *skill_name(SkillId id);
const char *skill_abbrev(SkillId id);
const char *skill_blurb(SkillId id); // short hover / skill-tree explanation
bool skill_available(struct GameState *state, const struct PlayerControls *ctrl, SkillId id);
bool skill_ready(struct GameState *state, const struct PlayerControls *ctrl, SkillId id);
bool skill_highlighted(struct GameState *state, SkillId id);
bool skill_use(struct GameState *state, struct PlayerControls *ctrl, SkillId id);

// Spirit defaults in slots 1-3; slot 4 is granted by the inhabited body (Fly on birds)
// or by an unlocked combat spell when no fly skill is active.
void skill_refresh_hotbar(struct GameState *state, struct PlayerControls *ctrl);

// --- Skill tree ---------------------------------------------------------------

bool skill_is_base(SkillId id);
bool skill_is_unlockable(SkillId id);
bool skill_is_rankable(SkillId id);
bool skill_appears_in_tree(SkillId id);
bool skill_is_known(const struct Actor *spirit, SkillId id);
bool skill_is_unlocked(const struct Actor *spirit, SkillId id);

// Current rank 0..3 (0 = unknown). Base skills default to rank I when unset.
uint8_t skill_rank(const struct Actor *spirit, SkillId id);
// Next purchasable rank, or 0 if maxed / not available.
uint8_t skill_next_rank(const struct Actor *spirit, SkillId id);
uint8_t skill_min_level(SkillId id); // rank I gate (compat)
uint8_t skill_min_level_for_rank(SkillId id, uint8_t rank); // rank 1..3
bool skill_meets_level(const struct Actor *spirit, SkillId id);
bool skill_meets_level_for_rank(const struct Actor *spirit, SkillId id, uint8_t rank);
// Damage/heal/range multiplier: I=1.0, II=1.5, III=2.2.
float skill_rank_mult(uint8_t rank);
const char *skill_rank_roman(uint8_t rank); // "", "I", "II", "III"

// Spend one skill point to unlock (rank I) or upgrade (II/III). Equips on first unlock.
bool skill_try_unlock(struct GameState *state, SkillId id);

// Cheat: raise spirit XP to the highest rank-III gate and unlock every skill at rank III.
bool skill_godmode(struct GameState *state);

// Equip a known skill onto a hotbar slot (0-based). Returns false if unknown / invalid.
bool skill_equip_hotbar(struct GameState *state, struct PlayerControls *ctrl,
                        int slot, SkillId id);

// Mouse-wheel: cycle hotbar `slot` through known skills. direction >0 = next, <0 = previous.
// Returns the skill now in the slot, or SKILL_NONE if nothing changed.
SkillId skill_cycle_hotbar(struct GameState *state, struct PlayerControls *ctrl,
                           int slot, int direction);

// Bit for unlocked_skills masks. SkillId values must stay below 32.
static inline uint32_t skill_bit(SkillId id)
{
    if (id <= SKILL_NONE || id >= SKILL_COUNT || id >= 32)
        return 0u;
    return 1u << (uint32_t)id;
}

#endif
