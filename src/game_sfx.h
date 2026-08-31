#ifndef GAME_SFX_H
#define GAME_SFX_H

#include "synthesizer/synthesizer.h"
#include "player_controls_types.h"
#include "projectile.h"

#include <stdbool.h>

// Game Boy–style SFX built on the shared synthesizer.
//
// Two layers:
//   1. Primitives — short pulse/noise building blocks (blip, chirp, thud, crash, …)
//      that map roughly to what a DMG pulse or noise channel would play alone.
//   2. Events — sequences of primitives composed the way Pokemon Blue/Red stacks
//      short notes into a fireball whoosh, a punch, or a rock cracking.
//
// Pulse SFX use channel `pulse_channel` (square/pulse/triangle); noise SFX use
// `noise_channel`. Both should sit above the music pad / UI channels (default 11/12).

typedef enum {
    SFX_PRIM_PULSE_BLIP = 0,   // short mid square beep
    SFX_PRIM_PULSE_BLIP_HI,    // higher, sharper beep
    SFX_PRIM_PULSE_BLIP_LO,    // lower confirmation tick
    SFX_PRIM_CHIRP_UP,         // rising pulse sweep (whoosh / cast)
    SFX_PRIM_CHIRP_DOWN,       // falling pulse sweep (swing / miss)
    SFX_PRIM_ARP_UP,           // two-note ascending arpeggio
    SFX_PRIM_ARP_DOWN,         // two-note descending arpeggio
    SFX_PRIM_BUZZ,             // short harsh square buzz
    SFX_PRIM_THUD,             // low square body hit
    SFX_PRIM_NOISE_TICK,       // very short noise (leaf rustle / soft hit)
    SFX_PRIM_NOISE_HIT,        // percussive noise crack
    SFX_PRIM_NOISE_CRASH,      // longer noise burst (break / impact)
    SFX_PRIM_NOISE_RUMBLE,     // low rumbling noise
    SFX_PRIM_COUNT
} GameSfxPrimitive;

typedef enum {
    // Combat / world
    SFX_EVENT_FIREBALL_CAST = 0,
    SFX_EVENT_FIREBALL_IMPACT,
    SFX_EVENT_UNARMED_SWING,
    SFX_EVENT_UNARMED_HIT,
    SFX_EVENT_BLOCK_HIT,
    SFX_EVENT_BLOCK_BREAK,

    // Skill casts — each skill has a distinct Game Boy cue
    SFX_EVENT_CREATE_WATER_CAST,
    SFX_EVENT_ICE_BOLT_CAST,
    SFX_EVENT_ICE_BOLT_IMPACT,
    SFX_EVENT_MAGIC_MISSILE_CAST,
    SFX_EVENT_HEALING_WORD_CAST,
    SFX_EVENT_BLINK_CAST,
    SFX_EVENT_LIGHTNING_BOLT_CAST,
    SFX_EVENT_LIGHTNING_IMPACT,
    SFX_EVENT_SHADOW_STRIKE_CAST,
    SFX_EVENT_SHADOW_STRIKE_IMPACT,
    SFX_EVENT_SUN_STRIKE_CAST,
    SFX_EVENT_SUN_STRIKE_IMPACT,
    SFX_EVENT_CHAIN_LIGHTNING_CAST,
    SFX_EVENT_METEOR_CAST,
    SFX_EVENT_METEOR_IMPACT,
    SFX_EVENT_LAGUNA_BLADE_CAST,
    SFX_EVENT_FINGER_OF_DEATH_CAST,
    SFX_EVENT_THUNDER_WRATH_CAST,
    SFX_EVENT_COLD_SNAP_CAST,
    SFX_EVENT_ARCANE_BOLT_CAST,
    SFX_EVENT_MEAT_HOOK_CAST,
    SFX_EVENT_TORNADO_CAST,
    SFX_EVENT_ECHO_SLAM_CAST,
    SFX_EVENT_ASSASSINATE_CAST,
    SFX_EVENT_RAVAGE_CAST,
    SFX_EVENT_REAPERS_SCYTHE_CAST,

    // Spirit / utility skills
    SFX_EVENT_DOMINATE_CAST,
    SFX_EVENT_TALK_CAST,
    SFX_EVENT_FLY_CAST,
    SFX_EVENT_TOWN_PORTAL_CAST,

    SFX_EVENT_COUNT
} GameSfxEvent;

typedef struct GameSfxSystem GameSfxSystem;

GameSfxSystem *game_sfx_create(Synthesizer *synth, int pulse_channel, int noise_channel);
void game_sfx_destroy(GameSfxSystem *sfx);

void game_sfx_set_enabled(GameSfxSystem *sfx, bool enabled);
void game_sfx_set_volume(GameSfxSystem *sfx, float volume); // 0..1

// Play one primitive (composable building block).
void game_sfx_play_primitive(GameSfxSystem *sfx, GameSfxPrimitive prim);

// Play a composed event (cancels any in-flight sequence on this system).
void game_sfx_play_event(GameSfxSystem *sfx, GameSfxEvent event);

// Map SkillId / ProjectileKind onto the right cue (no-op if unknown / inactive).
void play_skill_cast_sound(SkillId id);
void play_projectile_impact_sound(ProjectileKind kind);

// Convenience wrappers that target the active system (set by create / cleared by destroy).
void play_fireball_cast_sound(void);
void play_fireball_impact_sound(void);
void play_unarmed_swing_sound(void);
void play_unarmed_hit_sound(void);
void play_block_hit_sound(void);
void play_block_break_sound(void);

GameSfxSystem *game_sfx_get_active(void);

#endif // GAME_SFX_H
