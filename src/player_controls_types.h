#ifndef VERSE_PLAYER_CONTROLS_TYPES_H
#define VERSE_PLAYER_CONTROLS_TYPES_H

#include <stdbool.h>
#include <stdint.h>

// Action RPG control constants
#define PLAYER_RADIUS           0.30f
#define PLAYER_BASE_WEIGHT      50.0f
#define PLAYER_BASE_MAX_SPEED     6.0f
#define PLAYER_BASE_ACCEL        24.0f
#define PLAYER_DECEL             18.0f
#define PLAYER_SWING_DURATION_MS 350
#define PLAYER_SWING_RADIUS      1.6f
#define PLAYER_SWING_RADIUS_ARMED 2.2f
#define PLAYER_SWING_INNER_RADIUS 0.2f // same minimum distance apply_swing_damage skips
#define PLAYER_SWING_ARC_RAD     1.2f
#define PLAYER_SWING_ARC_ARMED   1.45f
#define PLAYER_ATTACK_RANGE      2.0f
// Hit resolves while progress is inside this window (see player_controls_update_attack).
#define PLAYER_SWING_HIT_START   0.35f
#define PLAYER_SWING_HIT_END     0.65f
// Unarmed reach grows with body strength (strength 10 → base radius).
#define PLAYER_UNARMED_RADIUS_PER_STR 0.04f
#define PLAYER_UNARMED_PILL_RADIUS_BASE 0.10f
#define PLAYER_UNARMED_PILL_RADIUS_PER_STR 0.008f
#define PLAYER_DEFAULT_TURN_SPEED_DEG 240.0f

// Horizontal movement is checked at positions, not along a swept path, so a single large step could
// start clear of an obstacle and land clear on its far side having never tested a position inside it.
// A one-voxel trunk needs 1 + 2*PLAYER_RADIUS of travel to be skipped that way, which at walking
// speed is a frame of about a quarter second — and the client's delta time is unclamped, so a
// world-generation or remesh hitch reaches that. Splitting the step keeps every obstacle sampled.
// The step is well under the 1.6 that would be strictly sufficient, because a diagonal approach to a
// corner has less depth to catch than a head-on one.
#define PLAYER_MAX_COLLISION_STEP  0.25f
// A bound on the work one frame can ask for. Past this the player stops short, which is the harmless
// direction to fail: a hitch that long has already broken the frame.
#define PLAYER_MAX_COLLISION_SUBSTEPS 32

// How far a walking body lifts itself over something in its way rather than stopping against it.
//
// A generated world is a height field, so the ground under a body changes by a voxel every few steps
// and almost none of those steps are onto a surface at exactly the height it is already standing at.
// Without this the player was blocked by every one of those rises: on real wilderness terrain they had
// no open direction at all in 73% of frames and covered 59 voxels in a minute of walking, which is
// what "stuck in the mesh" was. Actors already climbed a voxel this way (world_step_actors); only the
// player could not, so the two disagreed about the same ground.
//
// One voxel and no more. Two would let a body walk up a wall.
#define PLAYER_STEP_UP_VOXELS 1.0f

// Mouse look. Deltas feed aim_yaw/aim_pitch, and the existing turn-speed slew decides how fast
// facing_yaw/pitch catch up, so looking around obeys the same rotation limit as any other turn.
// The lead cap stops a fast flick from banking rotation the player keeps paying off for seconds
// after the mouse has stopped moving.
#define PLAYER_MOUSE_LOOK_DEG_PER_PX 0.22f
#define PLAYER_MAX_PITCH_DEG         80.0f
#define PLAYER_MAX_AIM_LEAD_DEG      75.0f

// Jump and air control.
//
// The jump is specified as the height it reaches, in voxels, not the speed it starts at: gravity in
// voxels/second^2 is the world's gravity divided by the size of a voxel, so the launch speed that
// clears a step depends entirely on how big the voxels are. game_state_player_jump_velocity solves
// for that gravity-only baseline (1.35 clears a one-voxel step with margin). Strength then scales
// only the takeoff impulse — how hard the body is shoved upward — without changing gravity or the
// air-control ceiling derived from it. Strength 10 is 1.0×; each point above or below adjusts.
#define PLAYER_JUMP_APEX_VOXELS        1.35f
#define PLAYER_JUMP_STRENGTH_BASE      10u
#define PLAYER_JUMP_IMPULSE_PER_STR    0.016f
// Air control, as fractions of gravity. Under one, so holding up slows a fall into a glide but can
// never turn into sustained flight; the stamina drain is the other half of that limit.
#define PLAYER_UP_STRAFE_GRAVITY_FRAC   0.55f
#define PLAYER_DOWN_STRAFE_GRAVITY_FRAC 0.50f
#define PLAYER_JUMP_STAMINA_COST        12.0f

// Fireball: the spirit's one ranged skill.
//
// The cooldown is the real limiter and the stamina cost is the secondary one, so a player at full
// stamina still cannot stream fireballs, and a player who has been sprinting and jumping has to
// choose. Speed is well above walking pace but slow enough that the projectile is visible in flight
// rather than arriving the instant it is cast.
#define PLAYER_FIREBALL_STAMINA_COST  18.0f
#define PLAYER_FIREBALL_COOLDOWN_MS   900u
#define PLAYER_FIREBALL_SPEED         14.0f // voxels/second
#define PLAYER_FIREBALL_DAMAGE        18u
#define PLAYER_FIREBALL_LIFE_S        2.5f  // burns out after ~35 voxels
#define PLAYER_FIREBALL_RADIUS        0.5f
// Fraction of world gravity while in flight (0 = flat; 1 = falls like an actor).
// Kept low so the ball holds a long flat run then sinks gently rather than lobbing hard.
#define PLAYER_FIREBALL_GRAVITY_SCALE 0.18f
// Seconds of flat flight before gravity begins — roughly 8 voxels at FIREBALL_SPEED.
#define PLAYER_FIREBALL_GRAVITY_DELAY_S 0.55f
// Blast radius on impact / burnout: splash damage, heat, and ignition fall off to the rim.
#define PLAYER_FIREBALL_BLAST_RADIUS  2.5f
// Splash damage at the epicentre as a fraction of the projectile's direct damage.
#define PLAYER_FIREBALL_SPLASH_FRAC   0.45f
// Temperature rise applied to voxels at the epicentre (falloff with distance).
#define PLAYER_FIREBALL_HEAT_AMOUNT   56u
// Launched from chest height and a little in front, so it does not begin inside the caster's own
// voxel and immediately report a hit on the ground they are standing on.
#define PLAYER_FIREBALL_MUZZLE_FORWARD 0.6f

// Create Water: place a puddle at the caster's feet / aimed ground. Unlocked via the skill tree.
#define PLAYER_CREATE_WATER_STAMINA_COST  14.0f
#define PLAYER_CREATE_WATER_COOLDOWN_MS   1200u
#define PLAYER_CREATE_WATER_RANGE        4.0f

// Ice Bolt: chilled projectile that damages and freezes the target. Unlocked via the skill tree.
#define PLAYER_ICE_BOLT_STAMINA_COST  16.0f
#define PLAYER_ICE_BOLT_COOLDOWN_MS   1000u
#define PLAYER_ICE_BOLT_SPEED         16.0f
#define PLAYER_ICE_BOLT_DAMAGE        12u
#define PLAYER_ICE_BOLT_LIFE_S        2.5f
#define PLAYER_ICE_BOLT_RADIUS        0.45f
#define PLAYER_ICE_BOLT_GRAVITY_SCALE 0.15f
#define PLAYER_ICE_BOLT_GRAVITY_DELAY_S 0.50f
#define PLAYER_ICE_BOLT_BLAST_RADIUS  2.25f
#define PLAYER_ICE_BOLT_SPLASH_FRAC   0.40f
#define PLAYER_ICE_BOLT_FREEZE_S      3.0f
// Temperature drop per voxel hit (ambient is VOXEL_TEMP_AMBIENT; ~3 hits freezes water).
#define PLAYER_ICE_BOLT_COOL_AMOUNT   48u
// Chill added to actors per hit (0..255); freeze triggers at 255.
#define PLAYER_ICE_BOLT_ACTOR_CHILL   90u

// Magic Missile (D&D): small, fast, flat-flying bolts.
#define PLAYER_MAGIC_MISSILE_STAMINA_COST  10.0f
#define PLAYER_MAGIC_MISSILE_COOLDOWN_MS   700u
#define PLAYER_MAGIC_MISSILE_SPEED         20.0f
#define PLAYER_MAGIC_MISSILE_DAMAGE        8u
#define PLAYER_MAGIC_MISSILE_LIFE_S        2.0f
#define PLAYER_MAGIC_MISSILE_RADIUS        0.35f
#define PLAYER_MAGIC_MISSILE_COUNT         3

// Healing Word (D&D): restore health on the spirit (or inhabited body).
#define PLAYER_HEALING_WORD_STAMINA_COST  20.0f
#define PLAYER_HEALING_WORD_COOLDOWN_MS   4000u
#define PLAYER_HEALING_WORD_HEAL         28u

// Blink (Dota Antimage / Misty Step): short forward teleport.
#define PLAYER_BLINK_STAMINA_COST  18.0f
#define PLAYER_BLINK_COOLDOWN_MS   2500u
#define PLAYER_BLINK_DISTANCE      6.0f

// Lightning Bolt (D&D): fast electric projectile with a crackling splash.
#define PLAYER_LIGHTNING_BOLT_STAMINA_COST  20.0f
#define PLAYER_LIGHTNING_BOLT_COOLDOWN_MS   1100u
#define PLAYER_LIGHTNING_BOLT_SPEED         22.0f
#define PLAYER_LIGHTNING_BOLT_DAMAGE        22u
#define PLAYER_LIGHTNING_BOLT_LIFE_S        2.0f
#define PLAYER_LIGHTNING_BOLT_RADIUS        0.40f
#define PLAYER_LIGHTNING_BOLT_BLAST_RADIUS  2.0f
#define PLAYER_LIGHTNING_BOLT_SPLASH_FRAC   0.35f

// Shadow Strike (Dota Queen of Pain): venom bolt that poisons on hit.
#define PLAYER_SHADOW_STRIKE_STAMINA_COST  16.0f
#define PLAYER_SHADOW_STRIKE_COOLDOWN_MS   1200u
#define PLAYER_SHADOW_STRIKE_SPEED         15.0f
#define PLAYER_SHADOW_STRIKE_DAMAGE        10u
#define PLAYER_SHADOW_STRIKE_LIFE_S        2.4f
#define PLAYER_SHADOW_STRIKE_RADIUS        0.40f
#define PLAYER_SHADOW_STRIKE_POISON_S      5.0f
#define PLAYER_SHADOW_STRIKE_POISON_DPS    6.0f

// Sun Strike (Dota Invoker): mark ground ahead; a delayed solar blast lands from above.
#define PLAYER_SUN_STRIKE_STAMINA_COST  24.0f
#define PLAYER_SUN_STRIKE_COOLDOWN_MS   5000u
#define PLAYER_SUN_STRIKE_RANGE         10.0f
#define PLAYER_SUN_STRIKE_DELAY_MS      1700u
#define PLAYER_SUN_STRIKE_DAMAGE        36u
#define PLAYER_SUN_STRIKE_BLAST_RADIUS  2.75f

// Chain Lightning (D&D): arc from the nearest foe to nearby neighbours.
#define PLAYER_CHAIN_LIGHTNING_STAMINA_COST  26.0f
#define PLAYER_CHAIN_LIGHTNING_COOLDOWN_MS   2200u
#define PLAYER_CHAIN_LIGHTNING_RANGE        9.0f
#define PLAYER_CHAIN_LIGHTNING_JUMP         4.0f
#define PLAYER_CHAIN_LIGHTNING_DAMAGE       20u
#define PLAYER_CHAIN_LIGHTNING_MAX_JUMPS    4

// Meteor: call a flaming rock down from the sky onto the aimed ground.
#define PLAYER_METEOR_STAMINA_COST  30.0f
#define PLAYER_METEOR_COOLDOWN_MS   4500u
#define PLAYER_METEOR_RANGE         12.0f
#define PLAYER_METEOR_SPAWN_HEIGHT  18.0f
#define PLAYER_METEOR_SPEED         16.0f
#define PLAYER_METEOR_DAMAGE        40u
#define PLAYER_METEOR_LIFE_S        3.0f
#define PLAYER_METEOR_RADIUS        0.85f
#define PLAYER_METEOR_BLAST_RADIUS  4.0f
#define PLAYER_METEOR_SPLASH_FRAC   0.55f
#define PLAYER_METEOR_HEAT_AMOUNT   72u

// Laguna Blade (Dota Lina): instant lightning nuke along the look ray.
#define PLAYER_LAGUNA_BLADE_STAMINA_COST  32.0f
#define PLAYER_LAGUNA_BLADE_COOLDOWN_MS   6000u
#define PLAYER_LAGUNA_BLADE_RANGE         14.0f
#define PLAYER_LAGUNA_BLADE_DAMAGE        48u

// Finger of Death (D&D / Lion): execute the nearest living foe.
#define PLAYER_FINGER_OF_DEATH_STAMINA_COST  36.0f
#define PLAYER_FINGER_OF_DEATH_COOLDOWN_MS   8000u
#define PLAYER_FINGER_OF_DEATH_RANGE         8.0f
#define PLAYER_FINGER_OF_DEATH_DAMAGE        70u

// Thunder Wrath (Dota Zeus Thundergod's Wrath): strike every nearby living foe.
#define PLAYER_THUNDER_WRATH_STAMINA_COST  40.0f
#define PLAYER_THUNDER_WRATH_COOLDOWN_MS   10000u
#define PLAYER_THUNDER_WRATH_RANGE         16.0f
#define PLAYER_THUNDER_WRATH_DAMAGE        28u

// Cold Snap (Dota Invoker): frost bolt that freezes more readily.
#define PLAYER_COLD_SNAP_STAMINA_COST  18.0f
#define PLAYER_COLD_SNAP_COOLDOWN_MS   1400u
#define PLAYER_COLD_SNAP_SPEED         17.0f
#define PLAYER_COLD_SNAP_DAMAGE        10u
#define PLAYER_COLD_SNAP_LIFE_S        2.3f
#define PLAYER_COLD_SNAP_RADIUS        0.42f
#define PLAYER_COLD_SNAP_CHILL         120u
#define PLAYER_COLD_SNAP_FREEZE_S      2.5f

// Arcane Bolt (Dota Skywrath): single heavy magic dart.
#define PLAYER_ARCANE_BOLT_STAMINA_COST  14.0f
#define PLAYER_ARCANE_BOLT_COOLDOWN_MS   900u
#define PLAYER_ARCANE_BOLT_SPEED         18.0f
#define PLAYER_ARCANE_BOLT_DAMAGE        16u
#define PLAYER_ARCANE_BOLT_LIFE_S        2.2f
#define PLAYER_ARCANE_BOLT_RADIUS        0.38f

// Meat Hook (Dota Pudge): long-range grab that pins the victim briefly.
#define PLAYER_MEAT_HOOK_STAMINA_COST  22.0f
#define PLAYER_MEAT_HOOK_COOLDOWN_MS   3500u
#define PLAYER_MEAT_HOOK_SPEED         18.0f
#define PLAYER_MEAT_HOOK_DAMAGE        18u
#define PLAYER_MEAT_HOOK_LIFE_S        2.8f
#define PLAYER_MEAT_HOOK_RADIUS        0.50f
#define PLAYER_MEAT_HOOK_FREEZE_S      1.6f

// Tornado (Dota Invoker): swirling AoE chill ahead of the caster.
#define PLAYER_TORNADO_STAMINA_COST  24.0f
#define PLAYER_TORNADO_COOLDOWN_MS   4000u
#define PLAYER_TORNADO_RANGE         8.0f
#define PLAYER_TORNADO_DAMAGE        16u
#define PLAYER_TORNADO_BLAST_RADIUS  3.0f
#define PLAYER_TORNADO_CHILL         70u

// Echo Slam (Dota Earthshaker): slam that scales with nearby foe count.
#define PLAYER_ECHO_SLAM_STAMINA_COST  28.0f
#define PLAYER_ECHO_SLAM_COOLDOWN_MS   5000u
#define PLAYER_ECHO_SLAM_RANGE         7.0f
#define PLAYER_ECHO_SLAM_DAMAGE        14u
#define PLAYER_ECHO_SLAM_ECHO_DAMAGE   8u

// Assassinate (Dota Sniper): long-range aimed execute shot.
#define PLAYER_ASSASSINATE_STAMINA_COST  30.0f
#define PLAYER_ASSASSINATE_COOLDOWN_MS   7000u
#define PLAYER_ASSASSINATE_RANGE         18.0f
#define PLAYER_ASSASSINATE_DAMAGE        42u

// Ravage (Dota Tidehunter): slam the ground, freezing nearby foes.
#define PLAYER_RAVAGE_STAMINA_COST  32.0f
#define PLAYER_RAVAGE_COOLDOWN_MS   8000u
#define PLAYER_RAVAGE_RANGE         6.5f
#define PLAYER_RAVAGE_DAMAGE        22u
#define PLAYER_RAVAGE_FREEZE_S      2.2f

// Reaper's Scythe (Dota Necrophos): execute the nearest foe; bonus vs wounded.
#define PLAYER_REAPERS_SCYTHE_STAMINA_COST  38.0f
#define PLAYER_REAPERS_SCYTHE_COOLDOWN_MS   9000u
#define PLAYER_REAPERS_SCYTHE_RANGE         9.0f
#define PLAYER_REAPERS_SCYTHE_DAMAGE        55u
#define PLAYER_REAPERS_SCYTHE_EXECUTE_PCT   35u // bonus damage if target below this HP%

// Dominate: the spirit inhabits a nearby living actor. Range is short so the player has to
// approach rather than possess from across the island. Release is the same skill used again.
#define PLAYER_DOMINATE_STAMINA_COST  22.0f
#define PLAYER_DOMINATE_COOLDOWN_MS   1200u
#define PLAYER_DOMINATE_RANGE         3.5f
// Extra kilograms while inhabiting a mud golem, so WASD feels like pushing clay rather than a spirit.
#define PLAYER_GOLEM_WEIGHT           80.0f

// Talk: approach a living actor and hear what they have to say. Cheap, short range.
#define PLAYER_TALK_RANGE             3.5f
#define PLAYER_TALK_LEAVE_RANGE       5.5f
#define PLAYER_TALK_COOLDOWN_MS       300u

// Loot: approach a corpse and press F to take its inventory and gear.
#define PLAYER_LOOT_RANGE             3.5f

// Town Portal: hold T to channel the spirit back to the home world. Long enough to interrupt
// mid-fight if you panic, short enough that it still feels like an escape.
#define PLAYER_TOWN_PORTAL_STAMINA_COST  15.0f
#define PLAYER_TOWN_PORTAL_COOLDOWN_MS   5000u
#define PLAYER_TOWN_PORTAL_CHARGE_MS     2500u

#define PLAYER_HOTBAR_SLOTS 4

typedef enum SkillId
{
    SKILL_NONE = 0,
    SKILL_FIREBALL = 1,
    SKILL_DOMINATE = 2,
    SKILL_TALK = 3,
    SKILL_FLY = 4,
    SKILL_TOWN_PORTAL = 5,
    SKILL_CREATE_WATER = 6,
    SKILL_ICE_BOLT = 7,
    SKILL_MAGIC_MISSILE = 8,
    SKILL_HEALING_WORD = 9,
    SKILL_BLINK = 10,
    SKILL_LIGHTNING_BOLT = 11,
    SKILL_SHADOW_STRIKE = 12,
    SKILL_SUN_STRIKE = 13,
    SKILL_CHAIN_LIGHTNING = 14,
    SKILL_METEOR = 15,
    SKILL_LAGUNA_BLADE = 16,
    SKILL_FINGER_OF_DEATH = 17,
    SKILL_THUNDER_WRATH = 18,
    SKILL_COLD_SNAP = 19,
    SKILL_ARCANE_BOLT = 20,
    SKILL_MEAT_HOOK = 21,
    SKILL_TORNADO = 22,
    SKILL_ECHO_SLAM = 23,
    SKILL_ASSASSINATE = 24,
    SKILL_RAVAGE = 25,
    SKILL_REAPERS_SCYTHE = 26,
    SKILL_COUNT
} SkillId;

// Fly: a context skill granted by an inhabited bird. Lift is body-specific and
// cancels most of gravity so the player glides down without holding W.
#define PLAYER_FLY_COOLDOWN_MS        400u
// Walk off a ledge (or fold mid-air) and freefall this long before the wings catch on their own.
#define PLAYER_AUTO_GLIDE_DELAY_S     0.40f
// Q/E bank: roll the body (and FP camera). Positive roll = right wing down.
// While banked, yaw turns with the roll (coordinated turn). Keys released → level out.
#define PLAYER_BANK_RATE_RAD          1.8f
#define PLAYER_MAX_BANK_RAD           0.90f
#define PLAYER_BANK_TURN_PER_ROLL     2.2f
#define PLAYER_BANK_LEVEL_RATE_RAD    2.8f

#define PLAYER_SPARROW_WEIGHT         6.0f
#define PLAYER_CROW_WEIGHT            14.0f
#define PLAYER_GULL_WEIGHT            18.0f

// Free spirit hover-flight: fairy float, not a walking body. No bank/roll; soft accel;
// subtle sinusoidal idle bob (visual only — physics stay at the hover centre).
#define PLAYER_SPIRIT_WEIGHT            16.0f
#define PLAYER_SPIRIT_MAX_SPEED          5.0f
#define PLAYER_SPIRIT_ACCEL             12.0f
#define PLAYER_SPIRIT_DECEL              8.0f
#define PLAYER_SPIRIT_VERT_SPEED         3.0f
#define PLAYER_SPIRIT_VERT_ACCEL         7.0f
#define PLAYER_SPIRIT_HOVER_BOB_AMP      0.14f // voxels of visual bob
#define PLAYER_SPIRIT_HOVER_BOB_HZ       0.55f

// Shift hold: ground run / flight afterburner. Ctrl hold: crouch (ground) or strafe-down (air).
#define PLAYER_RUN_SPEED_MULT            1.55f
#define PLAYER_RUN_STAMINA_RATE          28.0f
#define PLAYER_AFTERBURN_SPEED_MULT      1.85f
#define PLAYER_AFTERBURN_STAMINA_RATE    24.0f
#define PLAYER_CROUCH_SPEED_MULT         0.45f
#define PLAYER_EYE_HEIGHT                0.60f
#define PLAYER_CROUCH_EYE_HEIGHT         0.28f

#define PLAYER_UP_STRAFE_STAMINA_RATE   40.0f
#define PLAYER_STAMINA_REGEN_RATE       18.0f
#define PLAYER_DEFAULT_STAMINA_MAX     100.0f
#define PLAYER_COMMAND_QUEUE_SIZE        8

typedef enum PlayerCommandType
{
    PLAYER_CMD_NONE = 0,
    PLAYER_CMD_MOVE = 1,
    PLAYER_CMD_ATTACK_MOVE = 2,
    PLAYER_CMD_ATTACK = 3
} PlayerCommandType;

typedef struct PlayerCommand
{
    PlayerCommandType type;
    float x, y, z;
} PlayerCommand;

typedef struct PlayerControls
{
    float facing_yaw;
    float aim_yaw;

    // Vertical look, used by the first-person camera. Same aim/actual split as yaw.
    float pitch;
    float aim_pitch;

    // Bank angle around the look axis (radians). Only meaningful in flight; levels on the ground.
    float roll;

    float velocity_x;
    float velocity_y;
    float velocity_z;

    float total_weight;
    float base_weight;
    float equipment_weight;
    float container_weight;

    bool move_forward;
    bool move_backward;
    bool move_left;
    bool move_right;
    bool move_up;
    bool move_down;
    bool boost;       // Shift — run (ground) / afterburner (flight)
    bool bank_left;   // Q — roll left (left wing down)
    bool bank_right;  // E — roll right (right wing down)

    // Earliest tick, in SDL_GetTicks ms, at which a fireball may be cast again. Zero means ready.
    uint32_t fireball_ready_at_ms;
    uint32_t dominate_ready_at_ms;
    uint32_t talk_ready_at_ms;
    uint32_t fly_ready_at_ms;
    uint32_t town_portal_ready_at_ms;
    uint32_t create_water_ready_at_ms;
    uint32_t ice_bolt_ready_at_ms;
    uint32_t magic_missile_ready_at_ms;
    uint32_t healing_word_ready_at_ms;
    uint32_t blink_ready_at_ms;
    uint32_t lightning_bolt_ready_at_ms;
    uint32_t shadow_strike_ready_at_ms;
    uint32_t sun_strike_ready_at_ms;
    uint32_t chain_lightning_ready_at_ms;
    uint32_t meteor_ready_at_ms;
    uint32_t laguna_blade_ready_at_ms;
    uint32_t finger_of_death_ready_at_ms;
    uint32_t thunder_wrath_ready_at_ms;
    uint32_t cold_snap_ready_at_ms;
    uint32_t arcane_bolt_ready_at_ms;
    uint32_t meat_hook_ready_at_ms;
    uint32_t tornado_ready_at_ms;
    uint32_t echo_slam_ready_at_ms;
    uint32_t assassinate_ready_at_ms;
    uint32_t ravage_ready_at_ms;
    uint32_t reapers_scythe_ready_at_ms;

    // Town Portal channel: hold T until charge completes. Cancelled by releasing the key.
    bool teleport_charging;
    uint32_t teleport_charge_start_ms;

    // Sun Strike: delayed blast marked on the ground ahead of the caster.
    bool sun_strike_pending;
    uint32_t sun_strike_land_at_ms;
    float sun_strike_x, sun_strike_y, sun_strike_z;
    uint32_t sun_strike_damage;
    uint16_t sun_strike_potency;

    // True while the Fly skill is on. Distinct from player_flying (the G-key spirit hover).
    bool fly_active;
    bool fly_was_airborne;
    // Seconds spent falling without fly while inhabiting a bird; trips auto-glide.
    float auto_glide_timer;
    // Set when the player folds wings on purpose; blocks auto-glide until landing.
    bool auto_glide_suppress;

    // Skills bound to keys 1-4. SKILL_NONE is an empty slot.
    SkillId hotbar[PLAYER_HOTBAR_SLOTS];

    bool is_attacking;
    uint32_t attack_start_ms;
    float attack_target_x;
    float attack_target_y;
    // When set, melee voxel damage hits this cell (under cursor / look ray) instead of
    // every solid in the swing arc.
    bool attack_has_voxel_target;
    int attack_voxel_x;
    int attack_voxel_y;
    int attack_voxel_z;
    bool attack_hit_applied;
    bool attack_armed; // true when swinging a held weapon rather than fists
    // Snapshotted at swing start so damage, reach, and the FP pill stay in sync.
    uint32_t attack_strength;
    float attack_radius;

    float move_target_x;
    float move_target_y;
    float move_target_z;
    bool has_move_target;
    bool move_target_is_attack_move;

    PlayerCommand command_queue[PLAYER_COMMAND_QUEUE_SIZE];
    int command_count;
    int command_head;
} PlayerControls;

void player_controls_init(PlayerControls *ctrl);
void player_controls_reset(PlayerControls *ctrl);

#endif // VERSE_PLAYER_CONTROLS_TYPES_H
