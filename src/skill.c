#include "skill.h"
#include "game_state.h"
#include "player_controls.h"
#include "mob_ai.h"

#include <stdio.h>
#include <stddef.h>
#include <SDL2/SDL.h>

static bool skill_always_available(GameState *state, const PlayerControls *ctrl)
{
    (void)ctrl;
    return state && state->game_started;
}

static bool skill_known_available(GameState *state, const PlayerControls *ctrl, SkillId id)
{
    (void)ctrl;
    if (!skill_always_available(state, ctrl))
        return false;
    return skill_is_known(state->player, id);
}

static bool skill_fireball_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_FIREBALL);
}

static bool skill_fireball_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_fireball_ready(state, ctrl);
}

static bool skill_fireball_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)ctrl;
    (void)state;
    return false;
}

static bool skill_fireball_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_fireball_forward(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message), "Fireball not ready");
        return false;
    }
    return true;
}

static bool skill_dominate_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_DOMINATE);
}

static bool skill_dominate_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_dominate_ready(state, ctrl);
}

static bool skill_dominate_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)ctrl;
    return player_controls_dominate_in_range(state);
}

static bool skill_dominate_use(GameState *state, PlayerControls *ctrl)
{
    return player_controls_try_dominate(state, ctrl);
}

static bool skill_talk_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_TALK);
}

static bool skill_talk_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_talk_ready(state, ctrl);
}

static bool skill_talk_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)ctrl;
    return player_controls_talk_in_range(state);
}

static bool skill_talk_use(GameState *state, PlayerControls *ctrl)
{
    return player_controls_try_talk(state, ctrl);
}

static bool skill_fly_available(GameState *state, const PlayerControls *ctrl)
{
    (void)ctrl;
    return player_controls_inhabited_can_fly(state);
}

static bool skill_fly_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_fly_ready(state, ctrl);
}

static bool skill_fly_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    return player_controls_fly_active(ctrl);
}

static bool skill_fly_use(GameState *state, PlayerControls *ctrl)
{
    return player_controls_try_fly(state, ctrl);
}

static bool skill_town_portal_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_TOWN_PORTAL);
}

static bool skill_town_portal_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_town_portal_ready(state, ctrl);
}

static bool skill_town_portal_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    return player_controls_town_portal_charging(ctrl);
}

static bool skill_town_portal_use(GameState *state, PlayerControls *ctrl)
{
    (void)ctrl;
    if (state)
        snprintf(state->status_message, sizeof(state->status_message),
                 "Hold T to channel Town Portal");
    return false;
}

static bool skill_create_water_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_CREATE_WATER);
}

static bool skill_create_water_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_create_water_ready(state, ctrl);
}

static bool skill_create_water_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_create_water_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_create_water(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Create Water not ready");
        return false;
    }
    return true;
}

static bool skill_ice_bolt_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_ICE_BOLT);
}

static bool skill_ice_bolt_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_ice_bolt_ready(state, ctrl);
}

static bool skill_ice_bolt_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_ice_bolt_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_ice_bolt_forward(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Ice Bolt not ready");
        return false;
    }
    return true;
}

static bool skill_magic_missile_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_MAGIC_MISSILE);
}

static bool skill_magic_missile_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_magic_missile_ready(state, ctrl);
}

static bool skill_magic_missile_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_magic_missile_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_magic_missile_forward(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Magic Missile not ready");
        return false;
    }
    return true;
}

static bool skill_healing_word_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_HEALING_WORD);
}

static bool skill_healing_word_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_healing_word_ready(state, ctrl);
}

static bool skill_healing_word_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_healing_word_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_healing_word(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Healing Word not ready");
        return false;
    }
    return true;
}

static bool skill_blink_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_BLINK);
}

static bool skill_blink_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_blink_ready(state, ctrl);
}

static bool skill_blink_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_blink_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_blink(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Blink not ready");
        return false;
    }
    return true;
}

static bool skill_lightning_bolt_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_LIGHTNING_BOLT);
}

static bool skill_lightning_bolt_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_lightning_bolt_ready(state, ctrl);
}

static bool skill_lightning_bolt_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_lightning_bolt_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_lightning_bolt_forward(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Lightning Bolt not ready");
        return false;
    }
    return true;
}

static bool skill_shadow_strike_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_SHADOW_STRIKE);
}

static bool skill_shadow_strike_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_shadow_strike_ready(state, ctrl);
}

static bool skill_shadow_strike_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_shadow_strike_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_shadow_strike_forward(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Shadow Strike not ready");
        return false;
    }
    return true;
}

static bool skill_sun_strike_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_SUN_STRIKE);
}

static bool skill_sun_strike_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_sun_strike_ready(state, ctrl);
}

static bool skill_sun_strike_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    return player_controls_sun_strike_pending(ctrl);
}

static bool skill_sun_strike_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_sun_strike(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Sun Strike not ready");
        return false;
    }
    return true;
}

static bool skill_chain_lightning_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_CHAIN_LIGHTNING);
}

static bool skill_chain_lightning_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_chain_lightning_ready(state, ctrl);
}

static bool skill_chain_lightning_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)ctrl;
    return player_controls_chain_lightning_in_range(state);
}

static bool skill_chain_lightning_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_chain_lightning(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Chain Lightning not ready");
        return false;
    }
    return true;
}

static bool skill_meteor_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_METEOR);
}

static bool skill_meteor_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_meteor_ready(state, ctrl);
}

static bool skill_meteor_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_meteor_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_meteor_forward(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Meteor not ready");
        return false;
    }
    return true;
}

static bool skill_laguna_blade_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_LAGUNA_BLADE);
}

static bool skill_laguna_blade_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_laguna_blade_ready(state, ctrl);
}

static bool skill_laguna_blade_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_laguna_blade_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_laguna_blade(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Laguna Blade not ready");
        return false;
    }
    return true;
}

static bool skill_finger_of_death_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_FINGER_OF_DEATH);
}

static bool skill_finger_of_death_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_finger_of_death_ready(state, ctrl);
}

static bool skill_finger_of_death_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)ctrl;
    return player_controls_finger_of_death_in_range(state);
}

static bool skill_finger_of_death_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_finger_of_death(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Finger of Death not ready");
        return false;
    }
    return true;
}

static bool skill_thunder_wrath_available(GameState *state, const PlayerControls *ctrl)
{
    return skill_known_available(state, ctrl, SKILL_THUNDER_WRATH);
}

static bool skill_thunder_wrath_ready(GameState *state, const PlayerControls *ctrl)
{
    return player_controls_thunder_wrath_ready(state, ctrl);
}

static bool skill_thunder_wrath_highlight(GameState *state, const PlayerControls *ctrl)
{
    (void)state;
    (void)ctrl;
    return false;
}

static bool skill_thunder_wrath_use(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cast_thunder_wrath(state, ctrl))
    {
        if (state)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Thunder Wrath not ready");
        return false;
    }
    return true;
}

#define SKILL_CB_SIMPLE(avail_id, ready_fn, use_fn, fail_msg)                                 \
    static bool avail_id##_available(GameState *state, const PlayerControls *ctrl)             \
    {                                                                                          \
        return skill_known_available(state, ctrl, avail_id);                                   \
    }                                                                                          \
    static bool avail_id##_ready(GameState *state, const PlayerControls *ctrl)                  \
    {                                                                                          \
        return ready_fn(state, ctrl);                                                          \
    }                                                                                          \
    static bool avail_id##_highlight(GameState *state, const PlayerControls *ctrl)              \
    {                                                                                          \
        (void)state;                                                                           \
        (void)ctrl;                                                                            \
        return false;                                                                          \
    }                                                                                          \
    static bool avail_id##_use(GameState *state, PlayerControls *ctrl)                          \
    {                                                                                          \
        if (!use_fn(state, ctrl))                                                              \
        {                                                                                      \
            if (state)                                                                         \
                snprintf(state->status_message, sizeof(state->status_message), fail_msg);       \
            return false;                                                                      \
        }                                                                                      \
        return true;                                                                           \
    }

SKILL_CB_SIMPLE(SKILL_COLD_SNAP, player_controls_cold_snap_ready,
                player_controls_cast_cold_snap_forward, "Cold Snap not ready")
SKILL_CB_SIMPLE(SKILL_ARCANE_BOLT, player_controls_arcane_bolt_ready,
                player_controls_cast_arcane_bolt_forward, "Arcane Bolt not ready")
SKILL_CB_SIMPLE(SKILL_MEAT_HOOK, player_controls_meat_hook_ready,
                player_controls_cast_meat_hook_forward, "Meat Hook not ready")
SKILL_CB_SIMPLE(SKILL_TORNADO, player_controls_tornado_ready, player_controls_cast_tornado,
                "Tornado not ready")
SKILL_CB_SIMPLE(SKILL_ECHO_SLAM, player_controls_echo_slam_ready, player_controls_cast_echo_slam,
                "Echo Slam not ready")
SKILL_CB_SIMPLE(SKILL_ASSASSINATE, player_controls_assassinate_ready,
                player_controls_cast_assassinate, "Assassinate not ready")
SKILL_CB_SIMPLE(SKILL_RAVAGE, player_controls_ravage_ready, player_controls_cast_ravage,
                "Ravage not ready")
SKILL_CB_SIMPLE(SKILL_REAPERS_SCYTHE, player_controls_reapers_scythe_ready,
                player_controls_cast_reapers_scythe, "Reaper's Scythe not ready")

#undef SKILL_CB_SIMPLE

// Macro glue for the SKILL_*_available naming from SKILL_CB_SIMPLE — those expand to
// SKILL_COLD_SNAP_available etc. Wire the table to those symbols via aliases:
#define SK_AVAIL(id) id##_available
#define SK_READY(id) id##_ready
#define SK_HIGH(id) id##_highlight
#define SK_USE(id) id##_use

static const SkillDef s_skills[] = {
    {SKILL_NONE, "Empty", "", "No skill equipped", SKILL_FLAG_NONE, {0, 0, 0},
     30, 30, 40, 90, 90, 110, NULL, NULL, NULL, NULL},
    {SKILL_FIREBALL, "Fireball", "Fb", "Launch a flaming bolt that explodes on impact",
     SKILL_FLAG_BASE | SKILL_FLAG_RANKABLE, {1, 6, 12},
     70, 30, 16, 220, 120, 40,
     skill_fireball_available, skill_fireball_ready, skill_fireball_highlight, skill_fireball_use},
    {SKILL_DOMINATE, "Dominate", "Dm", "Possess a nearby living body; use again to leave",
     SKILL_FLAG_BASE | SKILL_FLAG_TOGGLE, {0, 0, 0},
     50, 36, 22, 160, 110, 60,
     skill_dominate_available, skill_dominate_ready, skill_dominate_highlight, skill_dominate_use},
    {SKILL_TALK, "Talk", "Tk", "Start a conversation with a nearby creature",
     SKILL_FLAG_BASE, {0, 0, 0},
     28, 44, 40, 110, 170, 140,
     skill_talk_available, skill_talk_ready, skill_talk_highlight, skill_talk_use},
    {SKILL_FLY, "Fly", "Fl", "Take wing while inhabiting a bird; fold wings to dive",
     SKILL_FLAG_TOGGLE | SKILL_FLAG_CONTEXT, {0, 0, 0},
     36, 52, 78, 140, 190, 230,
     skill_fly_available, skill_fly_ready, skill_fly_highlight, skill_fly_use},
    {SKILL_TOWN_PORTAL, "Town Portal", "Tp", "Hold T to channel a return to your home world",
     SKILL_FLAG_BASE, {0, 0, 0},
     40, 48, 72, 150, 180, 220,
     skill_town_portal_available, skill_town_portal_ready, skill_town_portal_highlight,
     skill_town_portal_use},
    {SKILL_CREATE_WATER, "Create Water", "Cw", "Conjure water at your feet; higher ranks flood more",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {1, 4, 8},
     24, 48, 90, 80, 160, 220,
     skill_create_water_available, skill_create_water_ready, skill_create_water_highlight,
     skill_create_water_use},
    {SKILL_ICE_BOLT, "Ice Bolt", "Ib", "Hurl frost that chills foes and freezes water",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {3, 7, 13},
     40, 70, 110, 140, 210, 255,
     skill_ice_bolt_available, skill_ice_bolt_ready, skill_ice_bolt_highlight, skill_ice_bolt_use},
    {SKILL_MAGIC_MISSILE, "Magic Missile", "Mm", "Fire a volley of seeking arcane darts",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {1, 5, 10},
     90, 50, 140, 200, 140, 255,
     skill_magic_missile_available, skill_magic_missile_ready, skill_magic_missile_highlight,
     skill_magic_missile_use},
    {SKILL_HEALING_WORD, "Healing Word", "Hw", "Speak a word that restores your body's health",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {2, 6, 12},
     40, 90, 50, 120, 220, 140,
     skill_healing_word_available, skill_healing_word_ready, skill_healing_word_highlight,
     skill_healing_word_use},
    {SKILL_BLINK, "Blink", "Bl", "Teleport a short distance along your facing",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {3, 8, 14},
     60, 40, 100, 180, 120, 240,
     skill_blink_available, skill_blink_ready, skill_blink_highlight, skill_blink_use},
    {SKILL_LIGHTNING_BOLT, "Lightning Bolt", "Lb", "Loose a crackling spear of lightning",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {4, 9, 15},
     50, 70, 120, 200, 230, 255,
     skill_lightning_bolt_available, skill_lightning_bolt_ready, skill_lightning_bolt_highlight,
     skill_lightning_bolt_use},
    {SKILL_SHADOW_STRIKE, "Shadow Strike", "Ss", "Strike with a venom dart that poisons over time",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {4, 10, 16},
     70, 20, 90, 180, 60, 200,
     skill_shadow_strike_available, skill_shadow_strike_ready, skill_shadow_strike_highlight,
     skill_shadow_strike_use},
    {SKILL_SUN_STRIKE, "Sun Strike", "Su", "Mark a point; after a delay, the sun blasts it",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {6, 13, 19},
     120, 90, 20, 255, 200, 60,
     skill_sun_strike_available, skill_sun_strike_ready, skill_sun_strike_highlight,
     skill_sun_strike_use},
    {SKILL_CHAIN_LIGHTNING, "Chain Lightning", "Cl", "Arc lightning that jumps between nearby foes",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {6, 14, 20},
     40, 80, 140, 160, 210, 255,
     skill_chain_lightning_available, skill_chain_lightning_ready, skill_chain_lightning_highlight,
     skill_chain_lightning_use},
    {SKILL_METEOR, "Meteor", "Mt", "Call a meteor from the sky onto your aim point",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {8, 17, 23},
     90, 35, 10, 255, 140, 40,
     skill_meteor_available, skill_meteor_ready, skill_meteor_highlight, skill_meteor_use},
    {SKILL_LAGUNA_BLADE, "Laguna Blade", "Lg", "Unleash a devastating lightning nuke on one foe",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {9, 19, 25},
     100, 40, 30, 255, 120, 80,
     skill_laguna_blade_available, skill_laguna_blade_ready, skill_laguna_blade_highlight,
     skill_laguna_blade_use},
    {SKILL_FINGER_OF_DEATH, "Finger of Death", "Fd", "Point at the nearest foe and deal massive damage",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {12, 22, 28},
     80, 20, 40, 220, 80, 120,
     skill_finger_of_death_available, skill_finger_of_death_ready, skill_finger_of_death_highlight,
     skill_finger_of_death_use},
    {SKILL_THUNDER_WRATH, "Thunder Wrath", "Tw", "Call down thunder on every enemy around you",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {13, 23, 29},
     50, 60, 130, 180, 200, 255,
     skill_thunder_wrath_available, skill_thunder_wrath_ready, skill_thunder_wrath_highlight,
     skill_thunder_wrath_use},
    {SKILL_COLD_SNAP, "Cold Snap", "Cs", "Snap frost onto a foe, freezing them in place",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {5, 11, 17},
     100, 180, 230, 180, 230, 255,
     SK_AVAIL(SKILL_COLD_SNAP), SK_READY(SKILL_COLD_SNAP), SK_HIGH(SKILL_COLD_SNAP),
     SK_USE(SKILL_COLD_SNAP)},
    {SKILL_ARCANE_BOLT, "Arcane Bolt", "Ab", "Fire a heavy magic dart that hits hard",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {5, 12, 18},
     120, 80, 180, 220, 160, 255,
     SK_AVAIL(SKILL_ARCANE_BOLT), SK_READY(SKILL_ARCANE_BOLT), SK_HIGH(SKILL_ARCANE_BOLT),
     SK_USE(SKILL_ARCANE_BOLT)},
    {SKILL_MEAT_HOOK, "Meat Hook", "Mh", "Throw a long hook that grabs and pins a target",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {7, 15, 21},
     90, 30, 30, 200, 80, 70,
     SK_AVAIL(SKILL_MEAT_HOOK), SK_READY(SKILL_MEAT_HOOK), SK_HIGH(SKILL_MEAT_HOOK),
     SK_USE(SKILL_MEAT_HOOK)},
    {SKILL_TORNADO, "Tornado", "Tn", "Send a swirling chill that knocks foes ahead",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {7, 16, 22},
     70, 100, 130, 160, 200, 230,
     SK_AVAIL(SKILL_TORNADO), SK_READY(SKILL_TORNADO), SK_HIGH(SKILL_TORNADO),
     SK_USE(SKILL_TORNADO)},
    {SKILL_ECHO_SLAM, "Echo Slam", "Es", "Slam the ground; the blast echoes per nearby foe",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {8, 18, 24},
     80, 60, 30, 200, 160, 80,
     SK_AVAIL(SKILL_ECHO_SLAM), SK_READY(SKILL_ECHO_SLAM), SK_HIGH(SKILL_ECHO_SLAM),
     SK_USE(SKILL_ECHO_SLAM)},
    {SKILL_ASSASSINATE, "Assassinate", "As", "Take a long-range shot that executes at low HP",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {10, 20, 26},
     60, 50, 40, 180, 140, 100,
     SK_AVAIL(SKILL_ASSASSINATE), SK_READY(SKILL_ASSASSINATE), SK_HIGH(SKILL_ASSASSINATE),
     SK_USE(SKILL_ASSASSINATE)},
    {SKILL_RAVAGE, "Ravage", "Rv", "Slam the ground to damage and freeze nearby foes",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {11, 21, 27},
     40, 90, 80, 100, 200, 180,
     SK_AVAIL(SKILL_RAVAGE), SK_READY(SKILL_RAVAGE), SK_HIGH(SKILL_RAVAGE), SK_USE(SKILL_RAVAGE)},
    {SKILL_REAPERS_SCYTHE, "Reaper's Scythe", "Rs", "Reap a wounded foe; deals more when they are low",
     SKILL_FLAG_UNLOCKABLE | SKILL_FLAG_RANKABLE, {14, 24, 30},
     50, 20, 60, 160, 80, 180,
     SK_AVAIL(SKILL_REAPERS_SCYTHE), SK_READY(SKILL_REAPERS_SCYTHE), SK_HIGH(SKILL_REAPERS_SCYTHE),
     SK_USE(SKILL_REAPERS_SCYTHE)},
};

#undef SK_AVAIL
#undef SK_READY
#undef SK_HIGH
#undef SK_USE

_Static_assert(SKILL_COUNT <= 32, "unlocked_skills is a uint32_t bitmask");
_Static_assert(sizeof(s_skills) / sizeof(s_skills[0]) == (size_t)SKILL_COUNT,
               "s_skills must cover every SkillId");

const SkillDef *skill_def(SkillId id)
{
    if (id <= SKILL_NONE || id >= SKILL_COUNT)
        return NULL;
    if ((size_t)id >= sizeof(s_skills) / sizeof(s_skills[0]))
        return NULL;
    return &s_skills[id];
}

const char *skill_name(SkillId id)
{
    if (id == SKILL_NONE)
        return "Empty";
    const SkillDef *def = skill_def(id);
    return def && def->name ? def->name : "Empty";
}

const char *skill_abbrev(SkillId id)
{
    const SkillDef *def = skill_def(id);
    return def && def->abbrev ? def->abbrev : "";
}

const char *skill_blurb(SkillId id)
{
    const SkillDef *def = skill_def(id);
    return def && def->blurb ? def->blurb : "";
}

bool skill_available(GameState *state, const PlayerControls *ctrl, SkillId id)
{
    const SkillDef *def = skill_def(id);
    if (!def || !def->available)
        return false;
    return def->available(state, ctrl);
}

bool skill_ready(GameState *state, const PlayerControls *ctrl, SkillId id)
{
    const SkillDef *def = skill_def(id);
    if (!def || !def->ready)
        return false;
    return def->ready(state, ctrl);
}

bool skill_highlighted(GameState *state, SkillId id)
{
    const SkillDef *def = skill_def(id);
    if (!def || !def->highlighted)
        return false;
    return def->highlighted(state, state ? &state->controls : NULL);
}

bool skill_use(GameState *state, PlayerControls *ctrl, SkillId id)
{
    const SkillDef *def = skill_def(id);
    if (!def || !def->use)
        return false;
    return def->use(state, ctrl);
}

bool skill_is_base(SkillId id)
{
    const SkillDef *def = skill_def(id);
    return def && (def->flags & SKILL_FLAG_BASE) != 0;
}

bool skill_is_unlockable(SkillId id)
{
    const SkillDef *def = skill_def(id);
    return def && (def->flags & SKILL_FLAG_UNLOCKABLE) != 0;
}

bool skill_is_rankable(SkillId id)
{
    const SkillDef *def = skill_def(id);
    return def && (def->flags & SKILL_FLAG_RANKABLE) != 0;
}

bool skill_appears_in_tree(SkillId id)
{
    return skill_is_unlockable(id) || (skill_is_base(id) && skill_is_rankable(id));
}

bool skill_is_unlocked(const Actor *spirit, SkillId id)
{
    if (!spirit || !skill_is_unlockable(id))
        return false;
    return (spirit->unlocked_skills & skill_bit(id)) != 0;
}

uint8_t skill_rank(const Actor *spirit, SkillId id)
{
    if (!spirit || id <= SKILL_NONE || id >= SKILL_COUNT || (int)id >= 32)
        return 0;
    uint8_t stored = spirit->skill_ranks[id];
    if (skill_is_base(id))
        return stored > 0 ? stored : SKILL_RANK_BASIC;
    if (!skill_is_unlocked(spirit, id))
        return 0;
    return stored > 0 ? stored : SKILL_RANK_BASIC;
}

uint8_t skill_next_rank(const Actor *spirit, SkillId id)
{
    if (!skill_is_rankable(id))
        return 0;
    const uint8_t cur = skill_rank(spirit, id);
    if (skill_is_unlockable(id) && cur == 0)
        return SKILL_RANK_BASIC;
    if (cur > 0 && cur < SKILL_RANK_MAX)
        return (uint8_t)(cur + 1u);
    return 0;
}

uint8_t skill_min_level_for_rank(SkillId id, uint8_t rank)
{
    const SkillDef *def = skill_def(id);
    if (!def || rank < SKILL_RANK_BASIC || rank > SKILL_RANK_MAX)
        return 0;
    return def->min_level[rank - 1u];
}

uint8_t skill_min_level(SkillId id)
{
    return skill_min_level_for_rank(id, SKILL_RANK_BASIC);
}

bool skill_meets_level_for_rank(const Actor *spirit, SkillId id, uint8_t rank)
{
    const uint8_t need = skill_min_level_for_rank(id, rank);
    if (need == 0)
        return true;
    return spirit && spirit->level >= (uint32_t)need;
}

bool skill_meets_level(const Actor *spirit, SkillId id)
{
    const uint8_t next = skill_next_rank(spirit, id);
    if (next == 0)
        return skill_meets_level_for_rank(spirit, id, SKILL_RANK_BASIC);
    return skill_meets_level_for_rank(spirit, id, next);
}

float skill_rank_mult(uint8_t rank)
{
    if (rank >= SKILL_RANK_POWERFUL)
        return 2.2f;
    if (rank == SKILL_RANK_EXPANDED)
        return 1.5f;
    return 1.0f;
}

const char *skill_rank_roman(uint8_t rank)
{
    switch (rank)
    {
    case 1:
        return "I";
    case 2:
        return "II";
    case 3:
        return "III";
    default:
        return "";
    }
}

bool skill_is_known(const Actor *spirit, SkillId id)
{
    if (id == SKILL_NONE || id >= SKILL_COUNT)
        return false;
    if (skill_is_base(id))
        return true;
    const SkillDef *def = skill_def(id);
    if (def && (def->flags & SKILL_FLAG_CONTEXT) != 0)
        return true;
    return skill_is_unlocked(spirit, id);
}

static void skill_equip_unlocked(PlayerControls *ctrl, SkillId id)
{
    if (!ctrl || id == SKILL_NONE)
        return;
    for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
    {
        if (ctrl->hotbar[i] == id)
            return;
    }
    for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
    {
        if (ctrl->hotbar[i] == SKILL_NONE)
        {
            ctrl->hotbar[i] = id;
            return;
        }
    }
    if (ctrl->hotbar[3] != SKILL_FLY)
        ctrl->hotbar[3] = id;
}

bool skill_try_unlock(GameState *state, SkillId id)
{
    if (!state || !state->player || !skill_appears_in_tree(id))
        return false;

    const uint8_t next = skill_next_rank(state->player, id);
    if (next == 0)
    {
        snprintf(state->status_message, sizeof(state->status_message),
                 "%s already at rank III", skill_name(id));
        return false;
    }
    if (state->player->skill_points == 0)
    {
        snprintf(state->status_message, sizeof(state->status_message),
                 "No skill points (earn 1 every %u levels)", ACTOR_SKILL_POINT_LEVELS);
        return false;
    }
    if (!skill_meets_level_for_rank(state->player, id, next))
    {
        snprintf(state->status_message, sizeof(state->status_message),
                 "%s %s requires level %u", skill_name(id), skill_rank_roman(next),
                 (unsigned)skill_min_level_for_rank(id, next));
        return false;
    }

    const bool first = (skill_rank(state->player, id) == 0) ||
                       (skill_is_unlockable(id) && !skill_is_unlocked(state->player, id));
    state->player->skill_points--;
    if (skill_is_unlockable(id))
        state->player->unlocked_skills |= skill_bit(id);
    if ((int)id < 32)
        state->player->skill_ranks[id] = next;
    if (first)
        skill_equip_unlocked(&state->controls, id);
    skill_refresh_hotbar(state, &state->controls);

    snprintf(state->status_message, sizeof(state->status_message),
             "Learned %s %s!", skill_name(id), skill_rank_roman(next));
    snprintf(state->toast_text, sizeof(state->toast_text), "%s %s!", skill_name(id),
             skill_rank_roman(next));
    state->toast_text[sizeof(state->toast_text) - 1] = '\0';
    state->toast_start_ms = SDL_GetTicks();
    state->toast_duration_ms = 2800;
    state->toast_y_offset = 0.0f;
    state->toast_active = true;
    return true;
}

bool skill_godmode(GameState *state)
{
    if (!state || !state->player)
        return false;

    uint8_t max_lv = 1;
    for (SkillId id = (SkillId)1; id < SKILL_COUNT; id++)
    {
        if (!skill_appears_in_tree(id))
            continue;
        const uint8_t need = skill_min_level_for_rank(id, SKILL_RANK_POWERFUL);
        if (need > max_lv)
            max_lv = need;
    }

    const uint32_t need_xp = (max_lv > 0u ? (uint32_t)(max_lv - 1u) : 0u) * 100u;
    if (state->player->experience < need_xp)
        actor_add_experience(state->player, need_xp - state->player->experience);

    int unlocked = 0;
    for (SkillId id = (SkillId)1; id < SKILL_COUNT; id++)
    {
        if (!skill_appears_in_tree(id))
            continue;
        if (skill_is_unlockable(id))
            state->player->unlocked_skills |= skill_bit(id);
        if ((int)id < 32 && state->player->skill_ranks[id] < SKILL_RANK_POWERFUL)
        {
            state->player->skill_ranks[id] = SKILL_RANK_POWERFUL;
            unlocked++;
        }
        skill_equip_unlocked(&state->controls, id);
    }
    skill_refresh_hotbar(state, &state->controls);

    snprintf(state->status_message, sizeof(state->status_message),
             "Godmode: Lv %u, %d skills at III", state->player->level, unlocked);
    snprintf(state->toast_text, sizeof(state->toast_text), "Godmode!");
    state->toast_text[sizeof(state->toast_text) - 1] = '\0';
    state->toast_start_ms = SDL_GetTicks();
    state->toast_duration_ms = 2800;
    state->toast_y_offset = 0.0f;
    state->toast_active = true;
    return true;
}

bool skill_equip_hotbar(GameState *state, PlayerControls *ctrl, int slot, SkillId id)
{
    if (!ctrl || slot < 0 || slot >= PLAYER_HOTBAR_SLOTS)
        return false;
    if (id != SKILL_NONE && !skill_is_known(state ? state->player : NULL, id))
        return false;
    if (id == SKILL_FLY && !skill_available(state, ctrl, SKILL_FLY))
        return false;
    ctrl->hotbar[slot] = id;
    return true;
}

static int skill_collect_cycleable(GameState *state, PlayerControls *ctrl,
                                   SkillId *out, int max)
{
    int n = 0;
    if (!out || max <= 0)
        return 0;
    for (SkillId id = (SkillId)1; id < SKILL_COUNT; id++)
    {
        if (!skill_is_known(state ? state->player : NULL, id))
            continue;
        const SkillDef *def = skill_def(id);
        if (def && (def->flags & SKILL_FLAG_CONTEXT) != 0 &&
            !skill_available(state, ctrl, id))
            continue;
        if (n < max)
            out[n++] = id;
    }
    return n;
}

SkillId skill_cycle_hotbar(GameState *state, PlayerControls *ctrl, int slot, int direction)
{
    if (!ctrl || slot < 0 || slot >= PLAYER_HOTBAR_SLOTS || direction == 0)
        return SKILL_NONE;

    SkillId list[SKILL_COUNT];
    const int n = skill_collect_cycleable(state, ctrl, list, SKILL_COUNT);
    if (n <= 0)
        return SKILL_NONE;

    const SkillId current = ctrl->hotbar[slot];
    int idx = -1;
    for (int i = 0; i < n; i++)
    {
        if (list[i] == current)
        {
            idx = i;
            break;
        }
    }

    // Empty / unknown current → start at first (wheel down) or last (wheel up).
    int next;
    if (idx < 0)
        next = direction > 0 ? 0 : (n - 1);
    else
        next = (idx + (direction > 0 ? 1 : -1) + n * 8) % n;

    const SkillId chosen = list[next];
    if (chosen == current)
        return current;

    // If another slot already holds this skill, swap so the loadout stays unique.
    for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
    {
        if (i != slot && ctrl->hotbar[i] == chosen)
        {
            ctrl->hotbar[i] = current;
            break;
        }
    }
    ctrl->hotbar[slot] = chosen;

    if (state)
        snprintf(state->status_message, sizeof(state->status_message),
                 "Slot %d: %s", slot + 1, skill_name(chosen));
    return chosen;
}

void skill_refresh_hotbar(GameState *state, PlayerControls *ctrl)
{
    if (!ctrl)
        return;

    const bool can_fly = skill_available(state, ctrl, SKILL_FLY);
    if (can_fly)
    {
        bool has_fly = false;
        for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
        {
            if (ctrl->hotbar[i] == SKILL_FLY)
            {
                has_fly = true;
                break;
            }
        }
        if (!has_fly)
        {
            if (ctrl->hotbar[3] == SKILL_NONE)
                ctrl->hotbar[3] = SKILL_FLY;
            else
            {
                for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
                {
                    if (ctrl->hotbar[i] == SKILL_NONE)
                    {
                        ctrl->hotbar[i] = SKILL_FLY;
                        break;
                    }
                }
            }
        }
    }
    else
    {
        for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
        {
            if (ctrl->hotbar[i] == SKILL_FLY)
                ctrl->hotbar[i] = SKILL_NONE;
        }
    }

    bool any = false;
    for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
    {
        if (ctrl->hotbar[i] != SKILL_NONE)
        {
            any = true;
            break;
        }
    }
    if (!any)
    {
        ctrl->hotbar[0] = SKILL_FIREBALL;
        ctrl->hotbar[1] = SKILL_DOMINATE;
        ctrl->hotbar[2] = SKILL_TALK;
        ctrl->hotbar[3] = can_fly ? SKILL_FLY : SKILL_NONE;
    }
}
