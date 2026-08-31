#include "game_sfx.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

// One note in a composed sequence. delay_ms waits before the note; hold_ms is how
// long it stays on before note-off (ADSR release still rings after that).
typedef struct {
    bool use_noise;
    WaveType wave;
    float freq_hz;
    float freq_end_hz; // 0 → no pitch slide
    float amp;
    float attack;
    float decay;
    float sustain;
    float release;
    uint16_t delay_ms;
    uint16_t hold_ms;
} SfxNote;

#define SFX_MAX_SEQ 8
#define SFX_SEQ_LEN(arr) ((int)(sizeof(arr) / sizeof((arr)[0])))

struct GameSfxSystem {
    Synthesizer *synth;
    int pulse_channel;
    int noise_channel;
    bool enabled;
    float volume;
    SfxNote sequence[SFX_MAX_SEQ];
    int sequence_len;
    int sequence_index;
    bool awaiting_release; // true → next timer tick is note-off, not a new note
    bool last_used_noise;  // which lane the current hold belongs to
    SDL_TimerID timer_id;
};

static GameSfxSystem *g_active_sfx = NULL;

// --- Primitive single notes -------------------------------------------------

static const SfxNote PRIM_PULSE_BLIP = {
    false, WAVE_SQUARE, 880.0f, 0.0f, 0.45f, 0.002f, 0.02f, 0.0f, 0.04f, 0, 55};
static const SfxNote PRIM_PULSE_BLIP_HI = {
    false, WAVE_SQUARE, 1320.0f, 0.0f, 0.40f, 0.001f, 0.015f, 0.0f, 0.03f, 0, 40};
static const SfxNote PRIM_PULSE_BLIP_LO = {
    false, WAVE_SQUARE, 440.0f, 0.0f, 0.40f, 0.002f, 0.02f, 0.0f, 0.05f, 0, 70};
static const SfxNote PRIM_CHIRP_UP = {
    false, WAVE_PULSE, 220.0f, 980.0f, 0.50f, 0.002f, 0.05f, 0.2f, 0.06f, 0, 120};
static const SfxNote PRIM_CHIRP_DOWN = {
    false, WAVE_PULSE, 720.0f, 180.0f, 0.48f, 0.002f, 0.04f, 0.15f, 0.05f, 0, 90};
static const SfxNote PRIM_ARP_UP_A = {
    false, WAVE_SQUARE, 523.25f, 0.0f, 0.42f, 0.001f, 0.01f, 0.0f, 0.03f, 0, 40};
static const SfxNote PRIM_ARP_UP_B = {
    false, WAVE_SQUARE, 783.99f, 0.0f, 0.42f, 0.001f, 0.01f, 0.0f, 0.04f, 0, 50};
static const SfxNote PRIM_ARP_DOWN_A = {
    false, WAVE_SQUARE, 659.25f, 0.0f, 0.42f, 0.001f, 0.01f, 0.0f, 0.03f, 0, 40};
static const SfxNote PRIM_ARP_DOWN_B = {
    false, WAVE_SQUARE, 392.00f, 0.0f, 0.42f, 0.001f, 0.01f, 0.0f, 0.05f, 0, 55};
static const SfxNote PRIM_BUZZ = {
    false, WAVE_SAW, 160.0f, 0.0f, 0.40f, 0.001f, 0.03f, 0.0f, 0.04f, 0, 80};
static const SfxNote PRIM_THUD = {
    false, WAVE_SQUARE, 90.0f, 55.0f, 0.55f, 0.001f, 0.04f, 0.0f, 0.06f, 0, 70};
static const SfxNote PRIM_NOISE_TICK = {
    true, WAVE_NOISE, 0.0f, 0.0f, 0.30f, 0.001f, 0.01f, 0.0f, 0.02f, 0, 30};
static const SfxNote PRIM_NOISE_HIT = {
    true, WAVE_NOISE, 0.0f, 0.0f, 0.55f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 55};
static const SfxNote PRIM_NOISE_CRASH = {
    true, WAVE_NOISE, 0.0f, 0.0f, 0.65f, 0.001f, 0.05f, 0.1f, 0.10f, 0, 140};
static const SfxNote PRIM_NOISE_RUMBLE = {
    true, WAVE_NOISE, 0.0f, 0.0f, 0.45f, 0.005f, 0.08f, 0.2f, 0.12f, 0, 160};

// --- Composed events (Pokemon-style stacked short notes) --------------------

static const SfxNote EVT_FIREBALL_CAST[] = {
    {false, WAVE_PULSE, 180.0f, 1100.0f, 0.52f, 0.002f, 0.04f, 0.25f, 0.05f, 0, 110},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.28f, 0.001f, 0.03f, 0.0f, 0.06f, 0, 90},
};

static const SfxNote EVT_FIREBALL_IMPACT[] = {
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.70f, 0.001f, 0.04f, 0.05f, 0.08f, 0, 100},
    {false, WAVE_SQUARE, 120.0f, 60.0f, 0.50f, 0.001f, 0.05f, 0.0f, 0.08f, 0, 90},
};

static const SfxNote EVT_UNARMED_SWING[] = {
    {false, WAVE_PULSE, 640.0f, 140.0f, 0.42f, 0.001f, 0.03f, 0.1f, 0.04f, 0, 85},
};

static const SfxNote EVT_UNARMED_HIT[] = {
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.55f, 0.001f, 0.015f, 0.0f, 0.03f, 0, 45},
    {false, WAVE_SQUARE, 520.0f, 260.0f, 0.48f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 60},
};

static const SfxNote EVT_BLOCK_HIT[] = {
    {false, WAVE_SQUARE, 110.0f, 70.0f, 0.50f, 0.001f, 0.03f, 0.0f, 0.05f, 0, 55},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.40f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 50},
};

static const SfxNote EVT_BLOCK_BREAK[] = {
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.70f, 0.001f, 0.05f, 0.1f, 0.12f, 0, 130},
    {false, WAVE_SQUARE, 660.0f, 0.0f, 0.40f, 0.001f, 0.01f, 0.0f, 0.03f, 0, 40},
    {false, WAVE_SQUARE, 330.0f, 0.0f, 0.38f, 0.001f, 0.015f, 0.0f, 0.05f, 0, 70},
};

// Water: soft rising blips + splash tick
static const SfxNote EVT_CREATE_WATER_CAST[] = {
    {false, WAVE_TRIANGLE, 300.0f, 520.0f, 0.38f, 0.004f, 0.04f, 0.2f, 0.08f, 0, 100},
    {false, WAVE_TRIANGLE, 420.0f, 680.0f, 0.34f, 0.003f, 0.03f, 0.15f, 0.07f, 40, 90},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.22f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 55},
};

// Ice: glassy descending squares + crystal tick
static const SfxNote EVT_ICE_BOLT_CAST[] = {
    {false, WAVE_SQUARE, 980.0f, 420.0f, 0.44f, 0.001f, 0.03f, 0.1f, 0.06f, 0, 100},
    {false, WAVE_TRIANGLE, 1320.0f, 0.0f, 0.30f, 0.001f, 0.01f, 0.0f, 0.04f, 20, 45},
};

static const SfxNote EVT_ICE_BOLT_IMPACT[] = {
    {false, WAVE_SQUARE, 880.0f, 220.0f, 0.48f, 0.001f, 0.04f, 0.0f, 0.08f, 0, 90},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.35f, 0.001f, 0.02f, 0.0f, 0.05f, 0, 60},
};

// Magic Missile: three quick ascending beeps
static const SfxNote EVT_MAGIC_MISSILE_CAST[] = {
    {false, WAVE_SQUARE, 660.0f, 0.0f, 0.40f, 0.001f, 0.01f, 0.0f, 0.03f, 0, 35},
    {false, WAVE_SQUARE, 880.0f, 0.0f, 0.40f, 0.001f, 0.01f, 0.0f, 0.03f, 25, 35},
    {false, WAVE_SQUARE, 1175.0f, 0.0f, 0.42f, 0.001f, 0.01f, 0.0f, 0.04f, 25, 45},
};

// Heal: warm major arpeggio
static const SfxNote EVT_HEALING_WORD_CAST[] = {
    {false, WAVE_TRIANGLE, 392.0f, 0.0f, 0.36f, 0.004f, 0.03f, 0.2f, 0.10f, 0, 70},
    {false, WAVE_TRIANGLE, 523.25f, 0.0f, 0.34f, 0.003f, 0.02f, 0.2f, 0.10f, 40, 70},
    {false, WAVE_TRIANGLE, 659.25f, 0.0f, 0.32f, 0.003f, 0.02f, 0.15f, 0.12f, 40, 90},
};

// Blink: digital teleport zip
static const SfxNote EVT_BLINK_CAST[] = {
    {false, WAVE_PULSE, 1400.0f, 200.0f, 0.46f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 55},
    {false, WAVE_PULSE, 200.0f, 1600.0f, 0.40f, 0.001f, 0.02f, 0.0f, 0.05f, 10, 70},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.20f, 0.001f, 0.01f, 0.0f, 0.03f, 0, 35},
};

// Lightning: sharp zap + crackle
static const SfxNote EVT_LIGHTNING_BOLT_CAST[] = {
    {false, WAVE_SAW, 90.0f, 1800.0f, 0.50f, 0.001f, 0.02f, 0.05f, 0.04f, 0, 70},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.55f, 0.001f, 0.015f, 0.0f, 0.04f, 0, 50},
    {false, WAVE_SQUARE, 1500.0f, 400.0f, 0.35f, 0.001f, 0.01f, 0.0f, 0.03f, 15, 40},
};

static const SfxNote EVT_LIGHTNING_IMPACT[] = {
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.65f, 0.001f, 0.02f, 0.0f, 0.05f, 0, 70},
    {false, WAVE_SAW, 600.0f, 90.0f, 0.42f, 0.001f, 0.03f, 0.0f, 0.06f, 0, 80},
};

// Shadow / poison: low ominous buzz + whisper noise
static const SfxNote EVT_SHADOW_STRIKE_CAST[] = {
    {false, WAVE_SAW, 140.0f, 90.0f, 0.42f, 0.005f, 0.06f, 0.2f, 0.10f, 0, 120},
    {false, WAVE_PULSE, 280.0f, 160.0f, 0.30f, 0.003f, 0.04f, 0.1f, 0.08f, 30, 90},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.25f, 0.002f, 0.04f, 0.1f, 0.08f, 0, 80},
};

static const SfxNote EVT_SHADOW_STRIKE_IMPACT[] = {
    {false, WAVE_SAW, 220.0f, 80.0f, 0.40f, 0.002f, 0.05f, 0.0f, 0.08f, 0, 90},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.40f, 0.002f, 0.04f, 0.1f, 0.08f, 0, 100},
};

// Sun Strike: charge whistle then delayed thump on impact
static const SfxNote EVT_SUN_STRIKE_CAST[] = {
    {false, WAVE_TRIANGLE, 600.0f, 1400.0f, 0.36f, 0.01f, 0.08f, 0.4f, 0.12f, 0, 160},
    {false, WAVE_SQUARE, 900.0f, 0.0f, 0.28f, 0.002f, 0.02f, 0.0f, 0.05f, 50, 50},
};

static const SfxNote EVT_SUN_STRIKE_IMPACT[] = {
    {false, WAVE_SQUARE, 180.0f, 70.0f, 0.55f, 0.001f, 0.05f, 0.0f, 0.10f, 0, 110},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.60f, 0.001f, 0.05f, 0.1f, 0.12f, 0, 140},
    {false, WAVE_PULSE, 880.0f, 220.0f, 0.30f, 0.001f, 0.03f, 0.0f, 0.06f, 20, 70},
};

// Chain Lightning: rapid multi-zap arpeggio
static const SfxNote EVT_CHAIN_LIGHTNING_CAST[] = {
    {false, WAVE_SQUARE, 700.0f, 0.0f, 0.40f, 0.001f, 0.01f, 0.0f, 0.03f, 0, 30},
    {false, WAVE_SQUARE, 1050.0f, 0.0f, 0.40f, 0.001f, 0.01f, 0.0f, 0.03f, 20, 30},
    {false, WAVE_SQUARE, 1400.0f, 0.0f, 0.42f, 0.001f, 0.01f, 0.0f, 0.03f, 20, 30},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.40f, 0.001f, 0.015f, 0.0f, 0.04f, 10, 45},
};

// Meteor: long falling whistle + rumble
static const SfxNote EVT_METEOR_CAST[] = {
    {false, WAVE_SAW, 900.0f, 120.0f, 0.48f, 0.005f, 0.08f, 0.3f, 0.10f, 0, 180},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.35f, 0.01f, 0.08f, 0.2f, 0.12f, 40, 160},
};

static const SfxNote EVT_METEOR_IMPACT[] = {
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.75f, 0.001f, 0.06f, 0.15f, 0.14f, 0, 160},
    {false, WAVE_SQUARE, 80.0f, 40.0f, 0.60f, 0.001f, 0.06f, 0.0f, 0.12f, 0, 130},
    {false, WAVE_PULSE, 220.0f, 80.0f, 0.35f, 0.002f, 0.04f, 0.0f, 0.08f, 30, 90},
};

// Laguna Blade: bright laser pierce
static const SfxNote EVT_LAGUNA_BLADE_CAST[] = {
    {false, WAVE_PULSE, 200.0f, 2000.0f, 0.50f, 0.001f, 0.02f, 0.1f, 0.05f, 0, 90},
    {false, WAVE_SQUARE, 1600.0f, 400.0f, 0.38f, 0.001f, 0.02f, 0.0f, 0.05f, 15, 70},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.30f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 50},
};

// Finger of Death: dark chord stab
static const SfxNote EVT_FINGER_OF_DEATH_CAST[] = {
    {false, WAVE_SAW, 110.0f, 0.0f, 0.48f, 0.002f, 0.05f, 0.15f, 0.12f, 0, 100},
    {false, WAVE_SQUARE, 165.0f, 0.0f, 0.40f, 0.002f, 0.04f, 0.1f, 0.10f, 0, 100},
    {false, WAVE_PULSE, 880.0f, 110.0f, 0.35f, 0.001f, 0.03f, 0.0f, 0.08f, 40, 90},
};

// Thunder Wrath: wide multi-strike peal
static const SfxNote EVT_THUNDER_WRATH_CAST[] = {
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.55f, 0.001f, 0.02f, 0.0f, 0.05f, 0, 50},
    {false, WAVE_SQUARE, 500.0f, 0.0f, 0.42f, 0.001f, 0.02f, 0.0f, 0.04f, 20, 40},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.50f, 0.001f, 0.02f, 0.0f, 0.05f, 30, 50},
    {false, WAVE_SQUARE, 750.0f, 0.0f, 0.40f, 0.001f, 0.02f, 0.0f, 0.04f, 20, 40},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.55f, 0.001f, 0.03f, 0.05f, 0.08f, 30, 80},
};

// Cold Snap: freeze shutter
static const SfxNote EVT_COLD_SNAP_CAST[] = {
    {false, WAVE_TRIANGLE, 1100.0f, 280.0f, 0.42f, 0.001f, 0.03f, 0.05f, 0.06f, 0, 80},
    {false, WAVE_SQUARE, 1480.0f, 0.0f, 0.32f, 0.001f, 0.01f, 0.0f, 0.03f, 15, 35},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.28f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 50},
};

// Arcane Bolt: single purple-toned chirp
static const SfxNote EVT_ARCANE_BOLT_CAST[] = {
    {false, WAVE_PULSE, 440.0f, 990.0f, 0.44f, 0.002f, 0.03f, 0.15f, 0.06f, 0, 95},
    {false, WAVE_TRIANGLE, 1320.0f, 0.0f, 0.28f, 0.001f, 0.015f, 0.0f, 0.04f, 20, 40},
};

// Meat Hook: chain rattle + thunk
static const SfxNote EVT_MEAT_HOOK_CAST[] = {
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.40f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 40},
    {false, WAVE_SQUARE, 320.0f, 0.0f, 0.38f, 0.001f, 0.015f, 0.0f, 0.03f, 20, 35},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.35f, 0.001f, 0.015f, 0.0f, 0.03f, 15, 35},
    {false, WAVE_SQUARE, 90.0f, 55.0f, 0.50f, 0.001f, 0.04f, 0.0f, 0.08f, 25, 80},
};

// Tornado: swirling rising noise+pulse
static const SfxNote EVT_TORNADO_CAST[] = {
    {false, WAVE_PULSE, 180.0f, 720.0f, 0.40f, 0.01f, 0.08f, 0.3f, 0.10f, 0, 140},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.38f, 0.01f, 0.08f, 0.25f, 0.12f, 0, 150},
    {false, WAVE_TRIANGLE, 360.0f, 900.0f, 0.28f, 0.005f, 0.05f, 0.2f, 0.08f, 40, 110},
};

// Echo Slam: heavy thud then echoing blips
static const SfxNote EVT_ECHO_SLAM_CAST[] = {
    {false, WAVE_SQUARE, 70.0f, 40.0f, 0.60f, 0.001f, 0.05f, 0.0f, 0.10f, 0, 100},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.50f, 0.001f, 0.04f, 0.05f, 0.08f, 0, 90},
    {false, WAVE_SQUARE, 220.0f, 0.0f, 0.28f, 0.001f, 0.02f, 0.0f, 0.05f, 60, 45},
    {false, WAVE_SQUARE, 180.0f, 0.0f, 0.22f, 0.001f, 0.02f, 0.0f, 0.05f, 50, 40},
};

// Assassinate: long charging whistle then crack
static const SfxNote EVT_ASSASSINATE_CAST[] = {
    {false, WAVE_TRIANGLE, 700.0f, 1400.0f, 0.34f, 0.02f, 0.10f, 0.5f, 0.08f, 0, 160},
    {false, WAVE_SQUARE, 1800.0f, 200.0f, 0.45f, 0.001f, 0.02f, 0.0f, 0.05f, 30, 70},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.35f, 0.001f, 0.02f, 0.0f, 0.04f, 0, 50},
};

// Ravage: ground-quake boom
static const SfxNote EVT_RAVAGE_CAST[] = {
    {false, WAVE_SQUARE, 60.0f, 35.0f, 0.62f, 0.001f, 0.06f, 0.0f, 0.12f, 0, 120},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.55f, 0.002f, 0.06f, 0.15f, 0.12f, 0, 140},
    {false, WAVE_TRIANGLE, 400.0f, 120.0f, 0.28f, 0.002f, 0.04f, 0.0f, 0.08f, 30, 80},
};

// Reaper's Scythe: grim reaper chord + scythe swipe
static const SfxNote EVT_REAPERS_SCYTHE_CAST[] = {
    {false, WAVE_SAW, 98.0f, 0.0f, 0.45f, 0.005f, 0.08f, 0.25f, 0.15f, 0, 130},
    {false, WAVE_SAW, 123.47f, 0.0f, 0.38f, 0.005f, 0.06f, 0.2f, 0.12f, 0, 130},
    {false, WAVE_PULSE, 900.0f, 180.0f, 0.40f, 0.001f, 0.03f, 0.05f, 0.08f, 50, 100},
    {true, WAVE_NOISE, 0.0f, 0.0f, 0.30f, 0.002f, 0.04f, 0.1f, 0.10f, 0, 90},
};

// Dominate: possession warble
static const SfxNote EVT_DOMINATE_CAST[] = {
    {false, WAVE_PULSE, 220.0f, 440.0f, 0.40f, 0.01f, 0.08f, 0.3f, 0.10f, 0, 120},
    {false, WAVE_PULSE, 440.0f, 220.0f, 0.36f, 0.01f, 0.08f, 0.3f, 0.10f, 40, 120},
    {false, WAVE_TRIANGLE, 660.0f, 0.0f, 0.28f, 0.005f, 0.04f, 0.1f, 0.08f, 30, 80},
};

// Talk: soft chat blips
static const SfxNote EVT_TALK_CAST[] = {
    {false, WAVE_TRIANGLE, 520.0f, 0.0f, 0.30f, 0.003f, 0.02f, 0.0f, 0.05f, 0, 50},
    {false, WAVE_TRIANGLE, 620.0f, 0.0f, 0.28f, 0.003f, 0.02f, 0.0f, 0.05f, 35, 55},
};

// Fly: wing flutter chirps
static const SfxNote EVT_FLY_CAST[] = {
    {false, WAVE_PULSE, 480.0f, 720.0f, 0.32f, 0.002f, 0.02f, 0.1f, 0.04f, 0, 50},
    {false, WAVE_PULSE, 720.0f, 480.0f, 0.30f, 0.002f, 0.02f, 0.1f, 0.04f, 30, 50},
    {false, WAVE_PULSE, 560.0f, 840.0f, 0.28f, 0.002f, 0.02f, 0.1f, 0.04f, 30, 55},
};

// Town Portal: mystical rising gate
static const SfxNote EVT_TOWN_PORTAL_CAST[] = {
    {false, WAVE_TRIANGLE, 260.0f, 0.0f, 0.34f, 0.01f, 0.06f, 0.4f, 0.15f, 0, 100},
    {false, WAVE_TRIANGLE, 330.0f, 0.0f, 0.32f, 0.01f, 0.05f, 0.4f, 0.15f, 50, 100},
    {false, WAVE_TRIANGLE, 390.0f, 520.0f, 0.30f, 0.01f, 0.05f, 0.3f, 0.18f, 50, 140},
    {false, WAVE_SQUARE, 780.0f, 0.0f, 0.22f, 0.002f, 0.02f, 0.0f, 0.08f, 40, 60},
};

static void sfx_cancel_timer(GameSfxSystem *sfx)
{
    if (sfx && sfx->timer_id != 0)
    {
        SDL_RemoveTimer(sfx->timer_id);
        sfx->timer_id = 0;
    }
}

static void sfx_note_off_all(GameSfxSystem *sfx)
{
    if (!sfx || !sfx->synth)
        return;
    synthesizer_note_off(sfx->synth, sfx->pulse_channel);
    synthesizer_note_off(sfx->synth, sfx->noise_channel);
}

static void sfx_note_off_last(GameSfxSystem *sfx)
{
    if (!sfx || !sfx->synth)
        return;
    const int ch = sfx->last_used_noise ? sfx->noise_channel : sfx->pulse_channel;
    synthesizer_note_off(sfx->synth, ch);
}

static void sfx_play_note(GameSfxSystem *sfx, const SfxNote *note)
{
    if (!sfx || !sfx->synth || !note)
        return;

    const int ch = note->use_noise ? sfx->noise_channel : sfx->pulse_channel;
    const float amp = note->amp * sfx->volume;

    synthesizer_set_channel_wave_type(sfx->synth, ch, note->wave);
    synthesizer_set_channel_adsr(sfx->synth, ch, note->attack, note->decay,
                                 note->sustain, note->release);
    synthesizer_set_channel_amplitude(sfx->synth, ch, amp);
    synthesizer_set_channel_portamento(sfx->synth, ch, 0.0f);

    if (!note->use_noise)
    {
        const float start = note->freq_hz > 1.0f ? note->freq_hz : 440.0f;
        synthesizer_set_channel_frequency(sfx->synth, ch, start);
        if (note->freq_end_hz > 1.0f && note->hold_ms > 0)
        {
            synthesizer_set_channel_portamento(sfx->synth, ch, (float)note->hold_ms / 1000.0f);
            synthesizer_set_channel_frequency(sfx->synth, ch, note->freq_end_hz);
        }
    }
    else
    {
        synthesizer_set_channel_frequency(sfx->synth, ch, 1.0f);
    }

    synthesizer_note_on(sfx->synth, ch);
}

static Uint32 sfx_timer_callback(Uint32 interval, void *param);

static void sfx_arm_timer(GameSfxSystem *sfx, Uint32 delay_ms)
{
    sfx_cancel_timer(sfx);
    if (delay_ms == 0)
        delay_ms = 1;
    sfx->timer_id = SDL_AddTimer(delay_ms, sfx_timer_callback, sfx);
}

static void sfx_start_note_at_index(GameSfxSystem *sfx)
{
    if (!sfx || sfx->sequence_index >= sfx->sequence_len)
    {
        sfx_note_off_all(sfx);
        sfx_cancel_timer(sfx);
        sfx->awaiting_release = false;
        return;
    }

    const SfxNote *note = &sfx->sequence[sfx->sequence_index];
    sfx->last_used_noise = note->use_noise;
    sfx_play_note(sfx, note);
    sfx->awaiting_release = true;
    sfx_arm_timer(sfx, note->hold_ms > 0 ? note->hold_ms : 1);
}

static Uint32 sfx_timer_callback(Uint32 interval, void *param)
{
    (void)interval;
    GameSfxSystem *sfx = (GameSfxSystem *)param;
    if (!sfx)
        return 0;

    sfx->timer_id = 0;

    if (sfx->awaiting_release)
    {
        sfx_note_off_last(sfx);
        sfx->awaiting_release = false;
        sfx->sequence_index++;

        if (sfx->sequence_index >= sfx->sequence_len)
            return 0;

        const SfxNote *next = &sfx->sequence[sfx->sequence_index];
        if (next->delay_ms == 0)
            sfx_start_note_at_index(sfx);
        else
            sfx_arm_timer(sfx, next->delay_ms);
        return 0;
    }

    sfx_start_note_at_index(sfx);
    return 0;
}

static void sfx_play_sequence(GameSfxSystem *sfx, const SfxNote *notes, int count)
{
    if (!sfx || !sfx->enabled || !sfx->synth || !notes || count <= 0)
        return;
    if (count > SFX_MAX_SEQ)
        count = SFX_MAX_SEQ;

    sfx_cancel_timer(sfx);
    sfx_note_off_all(sfx);

    memcpy(sfx->sequence, notes, (size_t)count * sizeof(SfxNote));
    sfx->sequence_len = count;
    sfx->sequence_index = 0;
    sfx->awaiting_release = false;

    const Uint32 delay = notes[0].delay_ms;
    if (delay > 0)
        sfx_arm_timer(sfx, delay);
    else
        sfx_start_note_at_index(sfx);
}

GameSfxSystem *game_sfx_create(Synthesizer *synth, int pulse_channel, int noise_channel)
{
    if (!synth || pulse_channel < 0 || pulse_channel >= 16 ||
        noise_channel < 0 || noise_channel >= 16 || pulse_channel == noise_channel)
        return NULL;

    GameSfxSystem *sfx = calloc(1, sizeof(GameSfxSystem));
    if (!sfx)
        return NULL;

    sfx->synth = synth;
    sfx->pulse_channel = pulse_channel;
    sfx->noise_channel = noise_channel;
    sfx->enabled = true;
    sfx->volume = 1.0f;
    g_active_sfx = sfx;
    return sfx;
}

void game_sfx_destroy(GameSfxSystem *sfx)
{
    if (!sfx)
        return;
    sfx_cancel_timer(sfx);
    sfx_note_off_all(sfx);
    if (g_active_sfx == sfx)
        g_active_sfx = NULL;
    free(sfx);
}

void game_sfx_set_enabled(GameSfxSystem *sfx, bool enabled)
{
    if (!sfx)
        return;
    sfx->enabled = enabled;
    if (!enabled)
    {
        sfx_cancel_timer(sfx);
        sfx_note_off_all(sfx);
    }
}

void game_sfx_set_volume(GameSfxSystem *sfx, float volume)
{
    if (!sfx)
        return;
    if (volume < 0.0f)
        volume = 0.0f;
    if (volume > 1.0f)
        volume = 1.0f;
    sfx->volume = volume;
}

void game_sfx_play_primitive(GameSfxSystem *sfx, GameSfxPrimitive prim)
{
    if (!sfx)
        return;

    switch (prim)
    {
    case SFX_PRIM_PULSE_BLIP:
        sfx_play_sequence(sfx, &PRIM_PULSE_BLIP, 1);
        break;
    case SFX_PRIM_PULSE_BLIP_HI:
        sfx_play_sequence(sfx, &PRIM_PULSE_BLIP_HI, 1);
        break;
    case SFX_PRIM_PULSE_BLIP_LO:
        sfx_play_sequence(sfx, &PRIM_PULSE_BLIP_LO, 1);
        break;
    case SFX_PRIM_CHIRP_UP:
        sfx_play_sequence(sfx, &PRIM_CHIRP_UP, 1);
        break;
    case SFX_PRIM_CHIRP_DOWN:
        sfx_play_sequence(sfx, &PRIM_CHIRP_DOWN, 1);
        break;
    case SFX_PRIM_ARP_UP: {
        SfxNote arp[2] = {PRIM_ARP_UP_A, PRIM_ARP_UP_B};
        arp[1].delay_ms = 5;
        sfx_play_sequence(sfx, arp, 2);
        break;
    }
    case SFX_PRIM_ARP_DOWN: {
        SfxNote arp[2] = {PRIM_ARP_DOWN_A, PRIM_ARP_DOWN_B};
        arp[1].delay_ms = 5;
        sfx_play_sequence(sfx, arp, 2);
        break;
    }
    case SFX_PRIM_BUZZ:
        sfx_play_sequence(sfx, &PRIM_BUZZ, 1);
        break;
    case SFX_PRIM_THUD:
        sfx_play_sequence(sfx, &PRIM_THUD, 1);
        break;
    case SFX_PRIM_NOISE_TICK:
        sfx_play_sequence(sfx, &PRIM_NOISE_TICK, 1);
        break;
    case SFX_PRIM_NOISE_HIT:
        sfx_play_sequence(sfx, &PRIM_NOISE_HIT, 1);
        break;
    case SFX_PRIM_NOISE_CRASH:
        sfx_play_sequence(sfx, &PRIM_NOISE_CRASH, 1);
        break;
    case SFX_PRIM_NOISE_RUMBLE:
        sfx_play_sequence(sfx, &PRIM_NOISE_RUMBLE, 1);
        break;
    default:
        break;
    }
}

void game_sfx_play_event(GameSfxSystem *sfx, GameSfxEvent event)
{
    if (!sfx)
        return;

#define PLAY(evt)                                                                                  \
    case SFX_EVENT_##evt:                                                                          \
        sfx_play_sequence(sfx, EVT_##evt, SFX_SEQ_LEN(EVT_##evt));                                 \
        break

    switch (event)
    {
        PLAY(FIREBALL_CAST);
        PLAY(FIREBALL_IMPACT);
        PLAY(UNARMED_SWING);
        PLAY(UNARMED_HIT);
        PLAY(BLOCK_HIT);
        PLAY(BLOCK_BREAK);
        PLAY(CREATE_WATER_CAST);
        PLAY(ICE_BOLT_CAST);
        PLAY(ICE_BOLT_IMPACT);
        PLAY(MAGIC_MISSILE_CAST);
        PLAY(HEALING_WORD_CAST);
        PLAY(BLINK_CAST);
        PLAY(LIGHTNING_BOLT_CAST);
        PLAY(LIGHTNING_IMPACT);
        PLAY(SHADOW_STRIKE_CAST);
        PLAY(SHADOW_STRIKE_IMPACT);
        PLAY(SUN_STRIKE_CAST);
        PLAY(SUN_STRIKE_IMPACT);
        PLAY(CHAIN_LIGHTNING_CAST);
        PLAY(METEOR_CAST);
        PLAY(METEOR_IMPACT);
        PLAY(LAGUNA_BLADE_CAST);
        PLAY(FINGER_OF_DEATH_CAST);
        PLAY(THUNDER_WRATH_CAST);
        PLAY(COLD_SNAP_CAST);
        PLAY(ARCANE_BOLT_CAST);
        PLAY(MEAT_HOOK_CAST);
        PLAY(TORNADO_CAST);
        PLAY(ECHO_SLAM_CAST);
        PLAY(ASSASSINATE_CAST);
        PLAY(RAVAGE_CAST);
        PLAY(REAPERS_SCYTHE_CAST);
        PLAY(DOMINATE_CAST);
        PLAY(TALK_CAST);
        PLAY(FLY_CAST);
        PLAY(TOWN_PORTAL_CAST);
    default:
        break;
    }
#undef PLAY
}

void play_skill_cast_sound(SkillId id)
{
    if (!g_active_sfx)
        return;

    GameSfxEvent ev = SFX_EVENT_FIREBALL_CAST;
    switch (id)
    {
    case SKILL_FIREBALL:
        ev = SFX_EVENT_FIREBALL_CAST;
        break;
    case SKILL_CREATE_WATER:
        ev = SFX_EVENT_CREATE_WATER_CAST;
        break;
    case SKILL_ICE_BOLT:
        ev = SFX_EVENT_ICE_BOLT_CAST;
        break;
    case SKILL_MAGIC_MISSILE:
        ev = SFX_EVENT_MAGIC_MISSILE_CAST;
        break;
    case SKILL_HEALING_WORD:
        ev = SFX_EVENT_HEALING_WORD_CAST;
        break;
    case SKILL_BLINK:
        ev = SFX_EVENT_BLINK_CAST;
        break;
    case SKILL_LIGHTNING_BOLT:
        ev = SFX_EVENT_LIGHTNING_BOLT_CAST;
        break;
    case SKILL_SHADOW_STRIKE:
        ev = SFX_EVENT_SHADOW_STRIKE_CAST;
        break;
    case SKILL_SUN_STRIKE:
        ev = SFX_EVENT_SUN_STRIKE_CAST;
        break;
    case SKILL_CHAIN_LIGHTNING:
        ev = SFX_EVENT_CHAIN_LIGHTNING_CAST;
        break;
    case SKILL_METEOR:
        ev = SFX_EVENT_METEOR_CAST;
        break;
    case SKILL_LAGUNA_BLADE:
        ev = SFX_EVENT_LAGUNA_BLADE_CAST;
        break;
    case SKILL_FINGER_OF_DEATH:
        ev = SFX_EVENT_FINGER_OF_DEATH_CAST;
        break;
    case SKILL_THUNDER_WRATH:
        ev = SFX_EVENT_THUNDER_WRATH_CAST;
        break;
    case SKILL_COLD_SNAP:
        ev = SFX_EVENT_COLD_SNAP_CAST;
        break;
    case SKILL_ARCANE_BOLT:
        ev = SFX_EVENT_ARCANE_BOLT_CAST;
        break;
    case SKILL_MEAT_HOOK:
        ev = SFX_EVENT_MEAT_HOOK_CAST;
        break;
    case SKILL_TORNADO:
        ev = SFX_EVENT_TORNADO_CAST;
        break;
    case SKILL_ECHO_SLAM:
        ev = SFX_EVENT_ECHO_SLAM_CAST;
        break;
    case SKILL_ASSASSINATE:
        ev = SFX_EVENT_ASSASSINATE_CAST;
        break;
    case SKILL_RAVAGE:
        ev = SFX_EVENT_RAVAGE_CAST;
        break;
    case SKILL_REAPERS_SCYTHE:
        ev = SFX_EVENT_REAPERS_SCYTHE_CAST;
        break;
    case SKILL_DOMINATE:
        ev = SFX_EVENT_DOMINATE_CAST;
        break;
    case SKILL_TALK:
        ev = SFX_EVENT_TALK_CAST;
        break;
    case SKILL_FLY:
        ev = SFX_EVENT_FLY_CAST;
        break;
    case SKILL_TOWN_PORTAL:
        ev = SFX_EVENT_TOWN_PORTAL_CAST;
        break;
    default:
        return;
    }
    game_sfx_play_event(g_active_sfx, ev);
}

void play_projectile_impact_sound(ProjectileKind kind)
{
    if (!g_active_sfx)
        return;

    switch (kind)
    {
    case PROJECTILE_ICE_BOLT:
        game_sfx_play_event(g_active_sfx, SFX_EVENT_ICE_BOLT_IMPACT);
        break;
    case PROJECTILE_LIGHTNING:
        game_sfx_play_event(g_active_sfx, SFX_EVENT_LIGHTNING_IMPACT);
        break;
    case PROJECTILE_SHADOW_STRIKE:
        game_sfx_play_event(g_active_sfx, SFX_EVENT_SHADOW_STRIKE_IMPACT);
        break;
    case PROJECTILE_METEOR:
        game_sfx_play_event(g_active_sfx, SFX_EVENT_METEOR_IMPACT);
        break;
    case PROJECTILE_MAGIC_MISSILE:
        game_sfx_play_event(g_active_sfx, SFX_EVENT_MAGIC_MISSILE_CAST); // soft pop reuse
        break;
    case PROJECTILE_FIREBALL:
    default:
        game_sfx_play_event(g_active_sfx, SFX_EVENT_FIREBALL_IMPACT);
        break;
    }
}

void play_fireball_cast_sound(void)
{
    play_skill_cast_sound(SKILL_FIREBALL);
}

void play_fireball_impact_sound(void)
{
    if (g_active_sfx)
        game_sfx_play_event(g_active_sfx, SFX_EVENT_FIREBALL_IMPACT);
}

void play_unarmed_swing_sound(void)
{
    if (g_active_sfx)
        game_sfx_play_event(g_active_sfx, SFX_EVENT_UNARMED_SWING);
}

void play_unarmed_hit_sound(void)
{
    if (g_active_sfx)
        game_sfx_play_event(g_active_sfx, SFX_EVENT_UNARMED_HIT);
}

void play_block_hit_sound(void)
{
    if (g_active_sfx)
        game_sfx_play_event(g_active_sfx, SFX_EVENT_BLOCK_HIT);
}

void play_block_break_sound(void)
{
    if (g_active_sfx)
        game_sfx_play_event(g_active_sfx, SFX_EVENT_BLOCK_BREAK);
}

GameSfxSystem *game_sfx_get_active(void)
{
    return g_active_sfx;
}
