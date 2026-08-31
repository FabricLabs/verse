#ifndef VERSE_PLAYER_CONTROLS_H
#define VERSE_PLAYER_CONTROLS_H

#include "player_controls_types.h"
#include "game_state.h"

float player_controls_compute_weight(GameState *state, PlayerControls *ctrl);
float player_controls_get_turn_speed_deg(GameState *state);
void player_controls_update_facing(GameState *state, PlayerControls *ctrl, double dt);
void player_controls_update_facing_from_screen(GameState *state, PlayerControls *ctrl,
                                               int mouse_x, int mouse_y);
void player_controls_update_facing_from_world(GameState *state, PlayerControls *ctrl,
                                              float world_x, float world_y);

// Feed relative mouse motion (in window pixels) into aim yaw/pitch. Rotation itself stays bound
// by turn speed, because player_controls_update_facing is what advances facing toward aim.
void player_controls_apply_mouse_look(PlayerControls *ctrl, int dx, int dy);

void player_controls_apply_wasd(GameState *state, PlayerControls *ctrl, double dt);
void player_controls_apply_vertical(GameState *state, PlayerControls *ctrl, double dt);
void player_controls_update_move_target(GameState *state, PlayerControls *ctrl, double dt);
void player_controls_update_command_queue(GameState *state, PlayerControls *ctrl, double dt);

bool player_controls_start_attack(GameState *state, PlayerControls *ctrl,
                                  float target_x, float target_y);
// Lock this swing's voxel damage to the cell under the cursor / crosshair.
void player_controls_set_attack_voxel(PlayerControls *ctrl, int x, int y, int z);
// First solid voxel under the crosshair within melee reach, or false if none.
bool player_controls_pick_melee_voxel(GameState *state, PlayerControls *ctrl,
                                      int *out_x, int *out_y, int *out_z);
void player_controls_update_attack(GameState *state, PlayerControls *ctrl);

// True when the spirit (or inhabited body) is swinging a held weapon this attack.
bool player_controls_is_armed(GameState *state);
// Strength of the active body (dominated, else the spirit): unarmed punches and jump launch impulse.
uint32_t player_controls_attack_strength(GameState *state);
// Takeoff speed for a jump: gravity-derived baseline scaled by active body strength.
float player_controls_jump_launch_speed(GameState *state);
float player_controls_swing_radius(const PlayerControls *ctrl);
float player_controls_swing_arc(const PlayerControls *ctrl);
// World-space radius of the unarmed arm/fist pill for the current swing.
float player_controls_unarmed_pill_radius(const PlayerControls *ctrl);
// 0..1 while attacking, or 0 when idle.
float player_controls_swing_progress(const PlayerControls *ctrl);

// Cast the spirit's fireball. Both forms spend stamina, start the cooldown, and turn the caster to
// face the shot; they differ only in where the direction comes from. False means the cast did not
// happen — on cooldown, not enough stamina, or no world to fire into — and nothing was spent.
//
// Aimed at a point, for the isometric view where the player clicks a target.
bool player_controls_cast_fireball_at(GameState *state, PlayerControls *ctrl,
                                     float target_x, float target_y, float target_z);
// Aimed along where the player is looking, for the first-person view.
bool player_controls_cast_fireball_forward(GameState *state, PlayerControls *ctrl);

// Whether a cast would be allowed right now, for a HUD that wants to grey the skill out.
bool player_controls_fireball_ready(GameState *state, const PlayerControls *ctrl);

// Create Water: place VOXEL_WATER at the caster's feet (and extinguish nearby fire).
bool player_controls_create_water_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_create_water(GameState *state, PlayerControls *ctrl);

// Ice Bolt: chilled projectile that damages and freezes the target.
bool player_controls_ice_bolt_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_ice_bolt_at(GameState *state, PlayerControls *ctrl,
                                      float target_x, float target_y, float target_z);
bool player_controls_cast_ice_bolt_forward(GameState *state, PlayerControls *ctrl);

// Magic Missile (D&D): volley of small arcane bolts.
bool player_controls_magic_missile_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_magic_missile_forward(GameState *state, PlayerControls *ctrl);

// Healing Word (D&D): restore health on the spirit or inhabited body.
bool player_controls_healing_word_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_healing_word(GameState *state, PlayerControls *ctrl);

// Blink (Dota / Misty Step): short forward teleport.
bool player_controls_blink_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_blink(GameState *state, PlayerControls *ctrl);

// Lightning Bolt (D&D).
bool player_controls_lightning_bolt_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_lightning_bolt_forward(GameState *state, PlayerControls *ctrl);

// Shadow Strike (Dota Queen of Pain).
bool player_controls_shadow_strike_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_shadow_strike_forward(GameState *state, PlayerControls *ctrl);

// Sun Strike (Dota Invoker): delayed blast from the sky.
bool player_controls_sun_strike_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_sun_strike_pending(const PlayerControls *ctrl);
bool player_controls_cast_sun_strike(GameState *state, PlayerControls *ctrl);
void player_controls_update_sun_strike(GameState *state, PlayerControls *ctrl);

// Chain Lightning (D&D).
bool player_controls_chain_lightning_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_chain_lightning_in_range(GameState *state);
bool player_controls_cast_chain_lightning(GameState *state, PlayerControls *ctrl);

// Meteor: flaming rock falls from above onto the aimed ground.
bool player_controls_meteor_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_meteor_at(GameState *state, PlayerControls *ctrl,
                                    float target_x, float target_y, float target_z);
bool player_controls_cast_meteor_forward(GameState *state, PlayerControls *ctrl);

// Laguna Blade (Dota Lina).
bool player_controls_laguna_blade_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_laguna_blade(GameState *state, PlayerControls *ctrl);

// Finger of Death (D&D / Lion).
bool player_controls_finger_of_death_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_finger_of_death_in_range(GameState *state);
bool player_controls_cast_finger_of_death(GameState *state, PlayerControls *ctrl);

// Thunder Wrath (Dota Zeus).
bool player_controls_thunder_wrath_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_thunder_wrath(GameState *state, PlayerControls *ctrl);

// Cold Snap (Dota Invoker).
bool player_controls_cold_snap_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_cold_snap_forward(GameState *state, PlayerControls *ctrl);

// Arcane Bolt (Dota Skywrath).
bool player_controls_arcane_bolt_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_arcane_bolt_forward(GameState *state, PlayerControls *ctrl);

// Meat Hook (Dota Pudge).
bool player_controls_meat_hook_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_meat_hook_forward(GameState *state, PlayerControls *ctrl);

// Tornado (Dota Invoker).
bool player_controls_tornado_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_tornado(GameState *state, PlayerControls *ctrl);

// Echo Slam (Dota Earthshaker).
bool player_controls_echo_slam_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_echo_slam(GameState *state, PlayerControls *ctrl);

// Assassinate (Dota Sniper).
bool player_controls_assassinate_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_assassinate(GameState *state, PlayerControls *ctrl);

// Ravage (Dota Tidehunter).
bool player_controls_ravage_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_ravage(GameState *state, PlayerControls *ctrl);

// Reaper's Scythe (Dota Necrophos).
bool player_controls_reapers_scythe_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_cast_reapers_scythe(GameState *state, PlayerControls *ctrl);

// Skills bound to keys 1-4. Slot is 0-based.
void player_controls_hotbar_assign(PlayerControls *ctrl, int slot, SkillId skill);
SkillId player_controls_hotbar_skill(const PlayerControls *ctrl, int slot);
const char *player_controls_skill_name(SkillId skill);
bool player_controls_skill_ready(GameState *state, const PlayerControls *ctrl, SkillId skill);
// Activate the skill in a hotbar slot. Fireball is aimed along facing; dominate and talk
// target a nearby living actor.
bool player_controls_use_hotbar_slot(GameState *state, PlayerControls *ctrl, int slot);

// Talk: open (or advance) in-world dialogue with the nearest living actor.
bool player_controls_talk_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_try_talk(GameState *state, PlayerControls *ctrl);
// True when a living actor is within talk range (HUD hint), ignoring cooldown.
bool player_controls_talk_in_range(GameState *state);

// Loot: open the corpse loot modal for the nearest corpse within range.
bool player_controls_try_loot(GameState *state);
// Open nearby crafting station (table/anvil/forge), or false if none.
bool player_controls_try_craft(GameState *state);
// Open the recipe book (browse all recipes; hand crafts allowed).
bool player_controls_open_recipe_book(GameState *state);
void player_controls_close_craft(GameState *state);
// True when a corpse is within loot range (HUD / world tooltip).
bool player_controls_loot_in_range(GameState *state);
// Nearest lootable corpse in range, or NULL. Used to anchor the "F to loot" tip.
Actor *player_controls_nearest_corpse(GameState *state, float *out_dist);
// Close the loot modal (returns any held item to its source).
void player_controls_close_loot(GameState *state);
// Transfer every corpse bag/equip item into the player bag (Loot All).
int player_controls_loot_all(GameState *state);
// Keep the modal valid: close if the corpse is gone or out of range.
void player_controls_update_loot(GameState *state);
// Drag helpers for the loot modal.
bool player_controls_loot_pick(GameState *state, int src, int index);
bool player_controls_loot_drop_on_player(GameState *state, int dest_index);
bool player_controls_loot_drop_on_corpse(GameState *state, int dest_index);
void player_controls_loot_cancel_drag(GameState *state);

// Drop the entire bag stack at `slot` onto the ground near the player as an icon sprite.
bool player_controls_drop_inventory_slot(GameState *state, int slot);

// Dominate: inhabit the nearest living runtime actor within range, or release if already inhabiting.
bool player_controls_dominate_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_try_dominate(GameState *state, PlayerControls *ctrl);
void player_controls_release_dominate(GameState *state);
bool player_controls_is_dominating(const GameState *state);
Actor *player_controls_dominated_actor(GameState *state);
// True when a living actor is within dominate range (HUD hint), ignoring cooldown.
bool player_controls_dominate_in_range(GameState *state);

// Fly: context skill on an inhabited bird. Toggle a long glide; lift is body-specific.
bool player_controls_inhabited_can_fly(GameState *state);
bool player_controls_fly_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_try_fly(GameState *state, PlayerControls *ctrl);
bool player_controls_fly_active(const PlayerControls *ctrl);
// While inhabiting a bird, open Fly after a short freefall so a ledge walk becomes a glide.
void player_controls_update_auto_glide(GameState *state, PlayerControls *ctrl, double dt);
float player_controls_inhabited_lift(GameState *state);
float player_controls_inhabited_glide_speed(GameState *state);
float player_controls_inhabited_bank_rate(GameState *state);
// True while an inhabited bird is aloft under Fly — Q/E bank only then. Free spirit hover does not roll.
bool player_controls_in_flight(GameState *state, const PlayerControls *ctrl);
void player_controls_apply_bank(GameState *state, PlayerControls *ctrl, double dt);
// True when the player is a free spirit (not inhabiting a body).
bool player_controls_is_free_spirit(const GameState *state);
// Ctrl held while grounded (not flying): crouch for slower move + lower FP eye.
bool player_controls_is_crouching(const GameState *state, const PlayerControls *ctrl);
// First-person eye offset above player feet centre (crouch lowers it).
float player_controls_eye_height(const GameState *state, const PlayerControls *ctrl);
// Visual-only sinusoidal hover offset (voxels). Zero while dominating or not in spirit hover.
float player_controls_spirit_hover_bob(const GameState *state);
// Enter / leave the spirit's default hover-flight (clears bank).
void player_controls_set_spirit_hover(GameState *state, bool enabled);

// Town Portal / Teleport: hold T to channel the spirit home. Releases an inhabited body first
// so the mob stays where it is under AI while only the spirit travels.
bool player_controls_town_portal_ready(GameState *state, const PlayerControls *ctrl);
bool player_controls_town_portal_charging(const PlayerControls *ctrl);
float player_controls_town_portal_charge_progress(const PlayerControls *ctrl);
void player_controls_update_town_portal(GameState *state, PlayerControls *ctrl, bool key_held);
void player_controls_cancel_town_portal(GameState *state, PlayerControls *ctrl);

bool player_controls_can_occupy(GameState *state, float wx, float wy, float wz);
bool player_controls_set_move_target(GameState *state, PlayerControls *ctrl,
                                     float wx, float wy, float wz);
bool player_controls_enqueue_command(PlayerControls *ctrl, PlayerCommandType type,
                                     float x, float y, float z);
bool player_controls_enqueue_attack_move(GameState *state, PlayerControls *ctrl,
                                         float wx, float wy, float wz);
void player_controls_clear_command_queue(PlayerControls *ctrl);

bool player_controls_is_attackable_target(GameState *state, float wx, float wy, float wz);

#endif // VERSE_PLAYER_CONTROLS_H
