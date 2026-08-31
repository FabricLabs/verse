#include "player_controls.h"
#include "world.h"
#include "voxel.h"
#include "actor.h"
#include "mob_ai.h"
#include "item.h"
#include "craft.h"
#include "mob_loot.h"
#include "isometric_renderer.h"
#include "window.h"
#include "fp_renderer.h"
#include "dialogue.h"
#include "skill.h"
#include "constants.h"
#include "game_sfx.h"
#include "voxel_combat.h"
#include "voxel_fracture.h"
#include "fire_sim.h"
#include "particle_effects.h"
#include "debris.h"

#include <math.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

static float wrap_angle(float a)
{
    while (a > (float)M_PI)
        a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI)
        a += 2.0f * (float)M_PI;
    return a;
}

static float angle_diff(float from, float to)
{
    return wrap_angle(to - from);
}

// Defined later; free-spirit helpers need it early for weight / hover checks.
bool player_controls_is_dominating(const GameState *state);

static World *controls_world(GameState *state)
{
    if (!state)
        return NULL;
    if (state->current_world)
        return state->current_world;
    return state->main_menu_world;
}

static bool voxel_is_attackable(VoxelType type);

void player_controls_init(PlayerControls *ctrl)
{
    if (!ctrl)
        return;
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->base_weight = PLAYER_BASE_WEIGHT;
    ctrl->facing_yaw = 0.0f;
    ctrl->aim_yaw = 0.0f;
    ctrl->pitch = 0.0f;
    ctrl->aim_pitch = 0.0f;
    ctrl->roll = 0.0f;
    ctrl->hotbar[0] = SKILL_FIREBALL;
    ctrl->hotbar[1] = SKILL_DOMINATE;
    ctrl->hotbar[2] = SKILL_TALK;
    ctrl->hotbar[3] = SKILL_NONE;
    skill_refresh_hotbar(NULL, ctrl);
}

void player_controls_reset(PlayerControls *ctrl)
{
    if (!ctrl)
        return;
    ctrl->velocity_x = 0.0f;
    ctrl->velocity_y = 0.0f;
    ctrl->velocity_z = 0.0f;
    ctrl->move_forward = false;
    ctrl->move_backward = false;
    ctrl->move_left = false;
    ctrl->move_right = false;
    ctrl->move_up = false;
    ctrl->move_down = false;
    ctrl->boost = false;
    ctrl->bank_left = false;
    ctrl->bank_right = false;
    ctrl->roll = 0.0f;
    ctrl->fly_active = false;
    ctrl->fly_was_airborne = false;
    ctrl->auto_glide_timer = 0.0f;
    ctrl->auto_glide_suppress = false;
    ctrl->teleport_charging = false;
    ctrl->teleport_charge_start_ms = 0;
    ctrl->sun_strike_pending = false;
    ctrl->sun_strike_land_at_ms = 0;
    ctrl->is_attacking = false;
    ctrl->has_move_target = false;
    ctrl->move_target_is_attack_move = false;
    ctrl->attack_has_voxel_target = false;
    ctrl->attack_hit_applied = false;
    ctrl->attack_armed = false;
    ctrl->attack_strength = 10;
    ctrl->attack_radius = PLAYER_SWING_RADIUS;
    ctrl->command_count = 0;
    ctrl->command_head = 0;
}

float player_controls_get_turn_speed_deg(GameState *state)
{
    if (state && state->player && state->player->turn_speed > 0)
        return (float)state->player->turn_speed;
    return PLAYER_DEFAULT_TURN_SPEED_DEG;
}

void player_controls_update_facing(GameState *state, PlayerControls *ctrl, double dt)
{
    if (!ctrl)
        return;

    float turn_deg = player_controls_get_turn_speed_deg(state);
    float max_step = turn_deg * ((float)M_PI / 180.0f) * (float)dt;
    float diff = angle_diff(ctrl->facing_yaw, ctrl->aim_yaw);

    if (fabsf(diff) <= max_step)
        ctrl->facing_yaw = ctrl->aim_yaw;
    else
        ctrl->facing_yaw = wrap_angle(ctrl->facing_yaw + (diff > 0.0f ? max_step : -max_step));

    // Pitch is clamped rather than wrapped, so it slews directly instead of through angle_diff.
    float pitch_diff = ctrl->aim_pitch - ctrl->pitch;
    if (fabsf(pitch_diff) <= max_step)
        ctrl->pitch = ctrl->aim_pitch;
    else
        ctrl->pitch += (pitch_diff > 0.0f ? max_step : -max_step);
}

void player_controls_apply_mouse_look(PlayerControls *ctrl, int dx, int dy)
{
    if (!ctrl || (dx == 0 && dy == 0))
        return;

    const float rad_per_px = PLAYER_MOUSE_LOOK_DEG_PER_PX * (float)M_PI / 180.0f;
    const float max_lead = PLAYER_MAX_AIM_LEAD_DEG * (float)M_PI / 180.0f;
    const float max_pitch = PLAYER_MAX_PITCH_DEG * (float)M_PI / 180.0f;

    ctrl->aim_yaw = wrap_angle(ctrl->aim_yaw + (float)dx * rad_per_px);

    float lead = angle_diff(ctrl->facing_yaw, ctrl->aim_yaw);
    if (lead > max_lead)
        ctrl->aim_yaw = wrap_angle(ctrl->facing_yaw + max_lead);
    else if (lead < -max_lead)
        ctrl->aim_yaw = wrap_angle(ctrl->facing_yaw - max_lead);

    // Moving the mouse down looks down.
    ctrl->aim_pitch -= (float)dy * rad_per_px;
    if (ctrl->aim_pitch > max_pitch)
        ctrl->aim_pitch = max_pitch;
    else if (ctrl->aim_pitch < -max_pitch)
        ctrl->aim_pitch = -max_pitch;

    const float pitch_lead = ctrl->aim_pitch - ctrl->pitch;
    if (pitch_lead > max_lead)
        ctrl->aim_pitch = ctrl->pitch + max_lead;
    else if (pitch_lead < -max_lead)
        ctrl->aim_pitch = ctrl->pitch - max_lead;
}

bool player_controls_is_free_spirit(const GameState *state)
{
    return state && !player_controls_is_dominating(state);
}

bool player_controls_is_crouching(const GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl || !ctrl->move_down)
        return false;
    // Flight uses Ctrl as strafe-down, not crouch.
    if (state->player_flying || (state->player && state->player->is_flying))
        return false;
    if (ctrl->fly_active && !game_state_player_is_grounded(state))
        return false;
    return game_state_player_is_grounded(state);
}

float player_controls_eye_height(const GameState *state, const PlayerControls *ctrl)
{
    if (player_controls_is_crouching(state, ctrl))
        return PLAYER_CROUCH_EYE_HEIGHT;
    return PLAYER_EYE_HEIGHT;
}

void player_controls_set_spirit_hover(GameState *state, bool enabled)
{
    if (!state)
        return;
    state->player_flying = enabled;
    if (state->player)
        state->player->is_flying = enabled;
    state->controls.roll = 0.0f;
    if (enabled)
        state->controls.velocity_z = 0.0f;
}

float player_controls_spirit_hover_bob(const GameState *state)
{
    if (!player_controls_is_free_spirit(state))
        return 0.0f;
    if (!state->player_flying && !(state->player && state->player->is_flying))
        return 0.0f;
    const float t = (float)SDL_GetTicks() * 0.001f;
    return sinf(t * (2.0f * (float)M_PI * PLAYER_SPIRIT_HOVER_BOB_HZ)) *
           PLAYER_SPIRIT_HOVER_BOB_AMP;
}

float player_controls_compute_weight(GameState *state, PlayerControls *ctrl)
{
    if (!ctrl)
        return PLAYER_BASE_WEIGHT;

    // Free spirit is a light wisp; inhabited bodies keep their own ballast.
    ctrl->base_weight = player_controls_is_free_spirit(state) ? PLAYER_SPIRIT_WEIGHT
                                                              : PLAYER_BASE_WEIGHT;
    ctrl->equipment_weight = 0.0f;
    ctrl->container_weight = 0.0f;

    if (state && state->player)
    {
        Actor *p = state->player;
        if (!player_controls_is_free_spirit(state))
            ctrl->base_weight += (float)p->constitution * 0.5f;
        ctrl->container_weight += inventory_total_weight(&p->inventory);
    }

    if (state && player_controls_is_dominating(state))
    {
        Actor *body = player_controls_dominated_actor(state);
        const Equipment *eq = mob_actor_equipment_const(body);
        if (eq)
            ctrl->equipment_weight += equipment_total_weight(eq);
        if (body && mob_actor_is_bird(body))
            ctrl->equipment_weight += mob_actor_bird_weight(body);
        else if (body)
            ctrl->equipment_weight += PLAYER_GOLEM_WEIGHT;
    }

    ctrl->total_weight = ctrl->base_weight + ctrl->equipment_weight + ctrl->container_weight;
    return ctrl->total_weight;
}

bool player_controls_can_occupy(GameState *state, float wx, float wy, float wz)
{
    if (!state)
        return false;

    int vz = (int)floorf(wz);
    int vx_min = (int)floorf(wx - PLAYER_RADIUS);
    int vx_max = (int)floorf(wx + PLAYER_RADIUS);
    int vy_min = (int)floorf(wy - PLAYER_RADIUS);
    int vy_max = (int)floorf(wy + PLAYER_RADIUS);

    for (int vx = vx_min; vx <= vx_max; vx++)
    {
        for (int vy = vy_min; vy <= vy_max; vy++)
        {
            // Distance from the player's axis to the nearest point of this voxel's footprint, which
            // is the player's own position clamped into the voxel's square. Measuring to the voxel's
            // centre instead let the player walk through trees and boulders: a voxel is one unit
            // across, so its far corner sits 0.7 from its centre, and demanding the centre be within
            // 0.3 of the player skipped every voxel the player was clipping only the edge of. Any
            // approach that stayed more than PLAYER_RADIUS off a voxel's centre axis never tested
            // that voxel at all and passed straight through it.
            const float nx = fminf(fmaxf(wx, (float)vx), (float)vx + 1.0f);
            const float ny = fminf(fmaxf(wy, (float)vy), (float)vy + 1.0f);
            const float dx = wx - nx;
            const float dy = wy - ny;
            if (dx * dx + dy * dy > PLAYER_RADIUS * PLAYER_RADIUS)
                continue;

            if (!game_state_can_move_to(state, vx, vy, vz))
                return false;
        }
    }
    return true;
}

static bool controls_is_grounded(GameState *state);

// Whether a target position is reachable from where the player is standing, either straight ahead or
// by lifting a voxel over whatever is in the way. Reports the height the player ends up at.
//
// The step up is only offered to a body on the ground, and only when there is room to rise from where
// it currently stands as well as room at the destination — otherwise a jump grazing a wall would be
// snapped up it, and a body under a low ceiling would climb into it.
static bool player_controls_reachable(GameState *state, float tx, float ty, float wz,
                                      bool may_step_up, float *out_z)
{
    if (player_controls_can_occupy(state, tx, ty, wz))
    {
        *out_z = wz;
        return true;
    }
    if (!may_step_up)
        return false;

    const float stepped = wz + PLAYER_STEP_UP_VOXELS;
    if (player_controls_can_occupy(state, tx, ty, stepped) &&
        player_controls_can_occupy(state, state->player_world_x, state->player_world_y, stepped))
    {
        *out_z = stepped;
        return true;
    }
    return false;
}

// Move the player by (dx, dy) voxels, stopping at whatever they run into, sliding along it, and
// stepping up onto it when it is only a voxel high.
//
// Walked in steps of at most PLAYER_MAX_COLLISION_STEP rather than applied at once, so a large frame
// delta cannot carry the player clean through a tree or a boulder without any tested position ever
// landing inside it. At a normal frame rate the whole move is one step and this costs nothing extra.
//
// Both movement paths — held keys and a click-to-move order — go through here, because they had
// separate copies of the same move-and-slide and only one of them would otherwise have been fixed.
static void player_controls_move_horizontal(GameState *state, PlayerControls *ctrl, float dx,
                                            float dy)
{
    if (!state || !ctrl)
        return;

    const float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= 0.0f)
        return;

    int substeps = (int)ceilf(dist / PLAYER_MAX_COLLISION_STEP);
    if (substeps < 1)
        substeps = 1;
    if (substeps > PLAYER_MAX_COLLISION_SUBSTEPS)
        substeps = PLAYER_MAX_COLLISION_SUBSTEPS;

    const float step_x = dx / (float)substeps;
    const float step_y = dy / (float)substeps;

    // Climbing is for walking, not for flight or a jump: a body in the air is under gravity's control
    // and should hit what it flies into.
    const bool may_step_up = controls_is_grounded(state) && !ctrl->fly_active &&
                             !state->player_flying && ctrl->velocity_z <= 0.0f;

    for (int i = 0; i < substeps; i++)
    {
        const float wz = state->player_world_z;
        const float try_x = state->player_world_x + step_x;
        const float try_y = state->player_world_y + step_y;
        float end_z = wz;

        if (player_controls_reachable(state, try_x, try_y, wz, may_step_up, &end_z))
        {
            state->player_world_x = try_x;
            state->player_world_y = try_y;
            state->player_world_z = end_z;
            continue;
        }

        // Blocked as a pair, so give up the axis that is obstructed and keep the one that is not.
        // That is what lets the player slide along a wall they are pressed into at an angle instead
        // of stopping dead against it.
        if (player_controls_reachable(state, try_x, state->player_world_y, wz, may_step_up, &end_z))
        {
            state->player_world_x = try_x;
            state->player_world_z = end_z;
            ctrl->velocity_y = 0.0f;
            continue;
        }
        if (player_controls_reachable(state, state->player_world_x, try_y, wz, may_step_up, &end_z))
        {
            state->player_world_y = try_y;
            state->player_world_z = end_z;
            ctrl->velocity_x = 0.0f;
            continue;
        }

        ctrl->velocity_x = 0.0f;
        ctrl->velocity_y = 0.0f;
        break; // Cornered: the remaining substeps cannot go anywhere either.
    }

    // The substeps above are all taken in the frame of the world the move started in, and collision
    // resolves neighbouring worlds in that frame, so a move across a seam is legal before it is
    // accounted for. Settle it once here rather than per substep: the player's column has to be back
    // inside a world before gravity reads it.
    game_state_cross_world_boundary(state);
}

// Whether the player is standing is decided in one place, game_state, and read here. The two used
// to test it separately, which let the jump believe the player was on the ground in a frame where
// gravity had already decided they were not.
static bool controls_is_grounded(GameState *state)
{
    if (!state || !controls_world(state))
        return false;
    return game_state_player_is_grounded(state);
}

static float controls_get_stamina(GameState *state)
{
    if (state && state->player)
        return state->player->stamina;
    return PLAYER_DEFAULT_STAMINA_MAX;
}

static float controls_get_stamina_max(GameState *state)
{
    if (state && state->player)
        return actor_max_stamina(state->player);
    return PLAYER_DEFAULT_STAMINA_MAX;
}

static void controls_set_stamina(GameState *state, float stamina)
{
    if (!state || !state->player)
        return;

    float max_stamina = controls_get_stamina_max(state);
    if (stamina < 0.0f)
        stamina = 0.0f;
    if (stamina > max_stamina)
        stamina = max_stamina;
    state->player->stamina = stamina;
}

void player_controls_apply_vertical(GameState *state, PlayerControls *ctrl, double dt)
{
    if (!state || !ctrl || !state->game_started)
        return;

    // Swinging no longer freezes vertical control or stamina; regen must keep working at high FPS.

    bool flying = (state->player && state->player->is_flying) || state->player_flying;
    if (flying)
    {
        // Free spirit hover: soft Space/Ctrl rise and sink. No jump, no bank, no stamina burn.
        if (player_controls_is_free_spirit(state) && dt > 0.0)
        {
            float desired = 0.0f;
            if (ctrl->move_up)
                desired = PLAYER_SPIRIT_VERT_SPEED;
            else if (ctrl->move_down)
                desired = -PLAYER_SPIRIT_VERT_SPEED;

            float diff = desired - ctrl->velocity_z;
            float step = PLAYER_SPIRIT_VERT_ACCEL * (float)dt;
            if (fabsf(diff) <= step)
                ctrl->velocity_z = desired;
            else
                ctrl->velocity_z += (diff > 0.0f ? step : -step);
        }
        return;
    }

    float stamina = controls_get_stamina(state);
    float max_stamina = controls_get_stamina_max(state);
    bool grounded = controls_is_grounded(state);
    bool airborne = !grounded;

    const float jump_velocity = game_state_player_jump_velocity(state);
    const float gravity = game_state_player_gravity(state);

    if (ctrl->move_up)
    {
        if (grounded && ctrl->velocity_z <= 0.05f)
        {
            if (stamina >= PLAYER_JUMP_STAMINA_COST)
            {
                // Strength scales only this shove upward. Gravity and the air-control cap below
                // stay on the gravity-derived baseline.
                ctrl->velocity_z = player_controls_jump_launch_speed(state);
                stamina -= PLAYER_JUMP_STAMINA_COST;
            }
        }
        else if (airborne && stamina > 0.0f)
        {
            ctrl->velocity_z += gravity * PLAYER_UP_STRAFE_GRAVITY_FRAC * (float)dt;
            if (ctrl->velocity_z > jump_velocity)
                ctrl->velocity_z = jump_velocity;

            float drain = PLAYER_UP_STRAFE_STAMINA_RATE * (float)dt;
            stamina -= drain;
            if (stamina < 0.0f)
                stamina = 0.0f;
        }
    }
    else if (ctrl->move_down && airborne)
    {
        // Only ever adds downward speed. Clamping it the way the up strafe is clamped would have
        // turned holding down into a brake on any fall already faster than that limit; the fall's
        // own terminal velocity is the cap that applies here.
        ctrl->velocity_z -= gravity * PLAYER_DOWN_STRAFE_GRAVITY_FRAC * (float)dt;
    }
    else if (grounded && stamina < max_stamina && !ctrl->boost)
    {
        stamina += PLAYER_STAMINA_REGEN_RATE * (float)dt;
        if (stamina > max_stamina)
            stamina = max_stamina;
    }

    controls_set_stamina(state, stamina);
}

void player_controls_update_facing_from_world(GameState *state, PlayerControls *ctrl,
                                              float world_x, float world_y)
{
    if (!state || !ctrl)
        return;

    float dx = world_x - state->player_world_x;
    float dy = world_y - state->player_world_y;
    if (dx * dx + dy * dy < 0.0001f)
        return;

    ctrl->aim_yaw = atan2f(dy, dx);
}

void player_controls_update_facing_from_screen(GameState *state, PlayerControls *ctrl,
                                               int mouse_x, int mouse_y)
{
    if (!state || !ctrl)
        return;

    // Aim from screen delta at the spirit (screen center), converted through the isometric
    // basis. Ground-picking is wrong here: it snaps to tiles and does not match the arrow.
    extern IsometricRenderer *g_isometric_renderer;
    float half_tw = 16.0f;
    float half_th = 8.0f;
    int center_x = 128;
    int center_y = 120;
    if (g_isometric_renderer)
    {
        half_tw = (float)(g_isometric_renderer->tile_width / 2);
        half_th = (float)(g_isometric_renderer->tile_height / 2);
        if (half_tw < 1.0f)
            half_tw = 1.0f;
        if (half_th < 1.0f)
            half_th = 1.0f;
        center_x = g_isometric_renderer->screen_width / 2;
        center_y = g_isometric_renderer->screen_height / 2;
    }

    float dsx = (float)(mouse_x - center_x);
    float dsy = (float)(mouse_y - center_y);
    if (dsx * dsx + dsy * dsy < 1.0f)
        return;

    // Inverse of isometric: sx=(x-y)*hw, sy=(x+y)*hh
    float dwx = dsx / half_tw + dsy / half_th;
    float dwy = dsy / half_th - dsx / half_tw;
    if (dwx * dwx + dwy * dwy < 0.0001f)
        return;

    ctrl->aim_yaw = atan2f(dwy, dwx);
}

// Horizontal speed multiplier from Shift (run/afterburner) and Ctrl crouch. Drains stamina when a
// boost is actually applied. Returns 1 when idle or out of stamina.
static float controls_apply_boost_speed(GameState *state, PlayerControls *ctrl, double dt,
                                        bool in_flight, bool moving)
{
    if (!state || !ctrl)
        return 1.0f;

    if (player_controls_is_crouching(state, ctrl))
        return PLAYER_CROUCH_SPEED_MULT;

    if (!ctrl->boost || !moving)
        return 1.0f;

    float stamina = controls_get_stamina(state);
    if (stamina <= 0.0f)
        return 1.0f;

    const float rate = in_flight ? PLAYER_AFTERBURN_STAMINA_RATE : PLAYER_RUN_STAMINA_RATE;
    const float mult = in_flight ? PLAYER_AFTERBURN_SPEED_MULT : PLAYER_RUN_SPEED_MULT;
    if (dt > 0.0)
    {
        stamina -= rate * (float)dt;
        if (stamina < 0.0f)
            stamina = 0.0f;
        controls_set_stamina(state, stamina);
    }
    return mult;
}

void player_controls_apply_wasd(GameState *state, PlayerControls *ctrl, double dt)
{
    if (!state || !ctrl || !state->game_started)
        return;

    // Left-click swings must not freeze WASD: facing can turn toward the target, but momentum
    // and held movement keys keep applying through the animation.

    player_controls_compute_weight(state, ctrl);

    const bool spirit_hover = player_controls_is_free_spirit(state) &&
                              (state->player_flying || (state->player && state->player->is_flying));
    const bool bird_glide = ctrl->fly_active && !controls_is_grounded(state) && !spirit_hover;
    const bool in_flight = spirit_hover || bird_glide;
    float weight_factor = 1.0f + ctrl->total_weight / 120.0f;
    float max_speed = (spirit_hover ? PLAYER_SPIRIT_MAX_SPEED : PLAYER_BASE_MAX_SPEED) / weight_factor;
    float accel = (spirit_hover ? PLAYER_SPIRIT_ACCEL : PLAYER_BASE_ACCEL) / weight_factor;
    float decel = (spirit_hover ? PLAYER_SPIRIT_DECEL : PLAYER_DECEL) / weight_factor;

    // input_y = forward/back (W/S), input_x = strafe (A/D)
    float input_x = 0.0f;
    float input_y = 0.0f;
    if (ctrl->move_forward)
        input_y += 1.0f;
    if (ctrl->move_backward)
        input_y -= 1.0f;
    if (ctrl->move_left)
        input_x -= 1.0f;
    if (ctrl->move_right)
        input_x += 1.0f;

    float desired_vx = 0.0f;
    float desired_vy = 0.0f;
    const bool has_input = (input_x != 0.0f || input_y != 0.0f);

    if (has_input)
    {
        float len = sqrtf(input_x * input_x + input_y * input_y);
        input_x /= len;
        input_y /= len;

        // facing_yaw = atan2(dy, dx) → forward = (cos, sin), right = (-sin, cos)
        float sin_yaw = sinf(ctrl->facing_yaw);
        float cos_yaw = cosf(ctrl->facing_yaw);
        float world_ix = input_y * cos_yaw - input_x * sin_yaw;
        float world_iy = input_y * sin_yaw + input_x * cos_yaw;

        max_speed *= controls_apply_boost_speed(state, ctrl, dt, in_flight, true);
        desired_vx = world_ix * max_speed;
        desired_vy = world_iy * max_speed;
    }
    else if (bird_glide)
    {
        float glide = player_controls_inhabited_glide_speed(state);
        glide *= controls_apply_boost_speed(state, ctrl, dt, true, true);
        desired_vx = cosf(ctrl->facing_yaw) * glide;
        desired_vy = sinf(ctrl->facing_yaw) * glide;
    }

    float dvx = desired_vx - ctrl->velocity_x;
    float dvy = desired_vy - ctrl->velocity_y;
    float rate = (desired_vx == 0.0f && desired_vy == 0.0f) ? decel : accel;

    float step = (float)rate * (float)dt;
    if (fabsf(dvx) > step)
        ctrl->velocity_x += (dvx > 0.0f ? step : -step);
    else
        ctrl->velocity_x = desired_vx;

    if (fabsf(dvy) > step)
        ctrl->velocity_y += (dvy > 0.0f ? step : -step);
    else
        ctrl->velocity_y = desired_vy;

    if (desired_vx == 0.0f && desired_vy == 0.0f)
    {
        float speed = sqrtf(ctrl->velocity_x * ctrl->velocity_x + ctrl->velocity_y * ctrl->velocity_y);
        if (speed < 0.05f)
        {
            ctrl->velocity_x = 0.0f;
            ctrl->velocity_y = 0.0f;
        }
    }

    player_controls_move_horizontal(state, ctrl, ctrl->velocity_x * (float)dt,
                                   ctrl->velocity_y * (float)dt);

    game_state_sync_positions(state);

    extern IsometricRenderer *g_isometric_renderer;
    if (g_isometric_renderer)
    {
        isometric_renderer_set_camera_world(g_isometric_renderer,
                                            state->player_world_x,
                                            state->player_world_y,
                                            state->player_world_z);
    }
}

bool player_controls_set_move_target(GameState *state, PlayerControls *ctrl,
                                     float wx, float wy, float wz)
{
    if (!state || !ctrl)
        return false;

    if (!player_controls_can_occupy(state, wx, wy, wz))
        return false;

    ctrl->move_target_x = wx;
    ctrl->move_target_y = wy;
    ctrl->move_target_z = wz;
    ctrl->has_move_target = true;
    ctrl->move_target_is_attack_move = false;
    ctrl->is_attacking = false;
    return true;
}

void player_controls_clear_command_queue(PlayerControls *ctrl)
{
    if (!ctrl)
        return;
    ctrl->command_count = 0;
    ctrl->command_head = 0;
    ctrl->has_move_target = false;
    ctrl->move_target_is_attack_move = false;
}

bool player_controls_enqueue_command(PlayerControls *ctrl, PlayerCommandType type,
                                     float x, float y, float z)
{
    if (!ctrl || type == PLAYER_CMD_NONE)
        return false;

    if (ctrl->command_count >= PLAYER_COMMAND_QUEUE_SIZE)
        return false;

    int index = (ctrl->command_head + ctrl->command_count) % PLAYER_COMMAND_QUEUE_SIZE;
    ctrl->command_queue[index].type = type;
    ctrl->command_queue[index].x = x;
    ctrl->command_queue[index].y = y;
    ctrl->command_queue[index].z = z;
    ctrl->command_count++;
    return true;
}

bool player_controls_enqueue_attack_move(GameState *state, PlayerControls *ctrl,
                                         float wx, float wy, float wz)
{
    if (!state || !ctrl)
        return false;

    if (!player_controls_enqueue_command(ctrl, PLAYER_CMD_ATTACK_MOVE, wx, wy, wz))
        return false;

    // If nothing is currently driving movement, activate the new command immediately.
    if (!ctrl->has_move_target && !ctrl->is_attacking)
    {
        ctrl->move_target_x = wx;
        ctrl->move_target_y = wy;
        ctrl->move_target_z = wz;
        ctrl->has_move_target = true;
        ctrl->move_target_is_attack_move = true;
        ctrl->command_head = (ctrl->command_head + 1) % PLAYER_COMMAND_QUEUE_SIZE;
        ctrl->command_count--;
    }

    return true;
}

static bool activate_next_command(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl || ctrl->command_count <= 0)
        return false;

    PlayerCommand cmd = ctrl->command_queue[ctrl->command_head];
    ctrl->command_head = (ctrl->command_head + 1) % PLAYER_COMMAND_QUEUE_SIZE;
    ctrl->command_count--;

    if (cmd.type == PLAYER_CMD_ATTACK)
    {
        return player_controls_start_attack(state, ctrl, cmd.x, cmd.y);
    }

    if (cmd.type == PLAYER_CMD_MOVE || cmd.type == PLAYER_CMD_ATTACK_MOVE)
    {
        if (!player_controls_can_occupy(state, cmd.x, cmd.y, cmd.z))
            return activate_next_command(state, ctrl);

        ctrl->move_target_x = cmd.x;
        ctrl->move_target_y = cmd.y;
        ctrl->move_target_z = cmd.z;
        ctrl->has_move_target = true;
        ctrl->move_target_is_attack_move = (cmd.type == PLAYER_CMD_ATTACK_MOVE);
        ctrl->is_attacking = false;
        return true;
    }

    return false;
}

static bool find_attack_move_target(GameState *state, PlayerControls *ctrl,
                                    float *out_x, float *out_y, int *out_vz)
{
    World *world = controls_world(state);
    if (!world || !ctrl || !out_x || !out_y)
        return false;

    float best_dist2 = PLAYER_ATTACK_RANGE * PLAYER_ATTACK_RANGE;
    bool found = false;
    float best_x = 0.0f;
    float best_y = 0.0f;
    int best_vz = 0;
    float px = state->player_world_x;
    float py = state->player_world_y;

    if (world->runtime_actors && world->runtime_actor_count > 0)
    {
        for (uint32_t i = 0; i < world->runtime_actor_count; i++)
        {
            Actor *a = &world->runtime_actors[i];
            if (!a->is_active || a->health == 0)
                continue;
            float dx = (float)a->x - px;
            float dy = (float)a->y - py;
            float d2 = dx * dx + dy * dy;
            if (d2 < best_dist2 && d2 > 0.04f)
            {
                best_dist2 = d2;
                best_x = (float)a->x;
                best_y = (float)a->y;
                best_vz = (int)floorf((float)a->z);
                found = true;
            }
        }
    }

    int scan_r = (int)ceilf(PLAYER_SWING_RADIUS);
    int base_z = (int)floorf(state->player_world_z);
    for (int ox = -scan_r; ox <= scan_r; ox++)
    {
        for (int oy = -scan_r; oy <= scan_r; oy++)
        {
            float tx = px + (float)ox;
            float ty = py + (float)oy;
            float dx = tx - px;
            float dy = ty - py;
            float d2 = dx * dx + dy * dy;
            if (d2 > best_dist2 || d2 < 0.04f)
                continue;

            int vx = (int)floorf(tx);
            int vy = (int)floorf(ty);
            for (int dz = -1; dz <= 1; dz++)
            {
                int vz = base_z + dz;
                if (!world_is_position_valid(world, vx, vy, vz))
                    continue;
                Voxel *voxel = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)vz);
                if (!voxel || !voxel_is_attackable(voxel->type))
                    continue;
                best_dist2 = d2;
                best_x = tx;
                best_y = ty;
                best_vz = vz;
                found = true;
                break;
            }
        }
    }

    if (found)
    {
        *out_x = best_x;
        *out_y = best_y;
        if (out_vz)
            *out_vz = best_vz;
    }
    return found;
}

static void integrate_move_toward(GameState *state, PlayerControls *ctrl, double dt,
                                  float target_x, float target_y)
{
    float dx = target_x - state->player_world_x;
    float dy = target_y - state->player_world_y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.001f)
        return;

    player_controls_compute_weight(state, ctrl);
    float weight_factor = 1.0f + ctrl->total_weight / 120.0f;
    float max_speed = PLAYER_BASE_MAX_SPEED / weight_factor;
    float accel = PLAYER_BASE_ACCEL / weight_factor;
    max_speed *= controls_apply_boost_speed(state, ctrl, dt, false, true);

    float nx = dx / dist;
    float ny = dy / dist;
    float desired_vx = nx * max_speed;
    float desired_vy = ny * max_speed;

    float step = accel * (float)dt;
    float dvx = desired_vx - ctrl->velocity_x;
    float dvy = desired_vy - ctrl->velocity_y;
    if (fabsf(dvx) > step)
        ctrl->velocity_x += (dvx > 0.0f ? step : -step);
    else
        ctrl->velocity_x = desired_vx;
    if (fabsf(dvy) > step)
        ctrl->velocity_y += (dvy > 0.0f ? step : -step);
    else
        ctrl->velocity_y = desired_vy;

    player_controls_move_horizontal(state, ctrl, ctrl->velocity_x * (float)dt,
                                   ctrl->velocity_y * (float)dt);

    game_state_sync_positions(state);

    extern IsometricRenderer *g_isometric_renderer;
    if (g_isometric_renderer)
    {
        isometric_renderer_set_camera_world(g_isometric_renderer,
                                            state->player_world_x,
                                            state->player_world_y,
                                            state->player_world_z);
    }
}

void player_controls_update_move_target(GameState *state, PlayerControls *ctrl, double dt)
{
    if (!state || !ctrl || !ctrl->has_move_target || ctrl->is_attacking)
        return;

    if (ctrl->move_target_is_attack_move)
    {
        float ax, ay;
        int avz = 0;
        if (find_attack_move_target(state, ctrl, &ax, &ay, &avz))
        {
            player_controls_start_attack(state, ctrl, ax, ay);
            // Prefer the concrete cell A-move locked onto, not every neighbour in the arc.
            player_controls_set_attack_voxel(ctrl, (int)floorf(ax), (int)floorf(ay), avz);
            return;
        }
    }

    float dx = ctrl->move_target_x - state->player_world_x;
    float dy = ctrl->move_target_y - state->player_world_y;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < 0.15f)
    {
        ctrl->has_move_target = false;
        ctrl->move_target_is_attack_move = false;
        ctrl->velocity_x = 0.0f;
        ctrl->velocity_y = 0.0f;
        activate_next_command(state, ctrl);
        return;
    }

    // Facing stays cursor-locked; do not override aim toward the move target.
    integrate_move_toward(state, ctrl, dt, ctrl->move_target_x, ctrl->move_target_y);
}

void player_controls_update_command_queue(GameState *state, PlayerControls *ctrl, double dt)
{
    if (!state || !ctrl)
        return;

    if (ctrl->is_attacking)
        return;

    if (!ctrl->has_move_target && ctrl->command_count > 0)
        activate_next_command(state, ctrl);

    if (ctrl->has_move_target)
        player_controls_update_move_target(state, ctrl, dt);
}

static bool voxel_is_attackable(VoxelType type)
{
    return voxel_type_is_leaves(type) || voxel_type_is_bush(type) || type == VOXEL_GRASS_TALL;
}

// True when the voxel centre is within swing reach of the player (3D, from body).
static bool melee_voxel_in_reach(const GameState *state, const PlayerControls *ctrl,
                                 int vx, int vy, int vz)
{
    if (!state || !ctrl)
        return false;
    const float swing_r = player_controls_swing_radius(ctrl);
    const float dx = ((float)vx + 0.5f) - state->player_world_x;
    const float dy = ((float)vy + 0.5f) - state->player_world_y;
    const float dz = ((float)vz + 0.5f) - state->player_world_z;
    const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    return dist <= swing_r && dist >= PLAYER_SWING_INNER_RADIUS;
}

// Resolve the solid / attackable cell under the crosshair by casting along aim.
static bool melee_pick_look_voxel(GameState *state, PlayerControls *ctrl,
                                  int *out_x, int *out_y, int *out_z)
{
    World *world = controls_world(state);
    if (!world || !ctrl || !out_x || !out_y || !out_z)
        return false;

    const float yaw = ctrl->aim_yaw;
    const float pitch = ctrl->aim_pitch;
    const float cos_pitch = cosf(pitch);
    float dx = cosf(yaw) * cos_pitch;
    float dy = sinf(yaw) * cos_pitch;
    float dz = sinf(pitch);
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-5f)
        return false;
    dx /= len;
    dy /= len;
    dz /= len;

    const float ox = state->player_world_x;
    const float oy = state->player_world_y;
    const float oz = state->player_world_z + player_controls_eye_height(state, ctrl);
    const float swing_r = player_controls_swing_radius(ctrl);
    // A few extra DDA steps so thin misses at the rim still resolve the face under the crosshair.
    const int max_steps = (int)ceilf(swing_r * 4.0f) + 4;

    int hx = 0, hy = 0, hz = 0;
    if (!world_raycast_first_hit(world, ox, oy, oz, dx, dy, dz, max_steps, &hx, &hy, &hz))
        return false;

    const float cx = (float)hx + 0.5f - state->player_world_x;
    const float cy = (float)hy + 0.5f - state->player_world_y;
    const float cz = (float)hz + 0.5f - state->player_world_z;
    if (sqrtf(cx * cx + cy * cy + cz * cz) > swing_r)
        return false;

    *out_x = hx;
    *out_y = hy;
    *out_z = hz;
    return true;
}

static void apply_melee_voxel_hit(GameState *state, PlayerControls *ctrl, World *world,
                                  int vx, int vy, int vz, int *hits, int *voxels_destroyed,
                                  uint32_t *foliage_xp)
{
    if (!world_is_position_valid(world, vx, vy, vz))
        return;
    if (!melee_voxel_in_reach(state, ctrl, vx, vy, vz))
        return;

    Voxel *voxel = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)vz);
    if (!voxel)
        return;

    if (voxel_is_attackable(voxel->type))
    {
        world_set_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)vz, VOXEL_AIR);
        voxel_fracture_disconnect_at(world, vx, vy, vz);
        (*voxels_destroyed)++;
        *foliage_xp += XP_FOLIAGE_DESTROY;
        (*hits)++;
        return;
    }

    if (!voxel_is_destructible(voxel->type))
        return;

    const float swing_speed = 8.0f + (float)ctrl->attack_strength * 0.15f;
    if (voxel_apply_impulse(voxel, swing_speed))
    {
        voxel_fracture_apply_crack(world, vx, vy, vz,
                                  cosf(ctrl->facing_yaw), sinf(ctrl->facing_yaw), 0.0f,
                                  swing_speed, voxel_crack_seed(vx, vy, vz));
        (*voxels_destroyed)++;
        (*hits)++;
    }
    else
    {
        world->voxel_revision++;
        (*hits)++;
    }
}

static void apply_swing_damage(GameState *state, PlayerControls *ctrl)
{
    World *world = controls_world(state);
    if (!world || !ctrl)
        return;

    const float swing_r = player_controls_swing_radius(ctrl);
    const float half_arc = player_controls_swing_arc(ctrl) * 0.5f;
    float px = state->player_world_x;
    float py = state->player_world_y;
    int hits = 0;
    int voxels_destroyed = 0;
    uint32_t foliage_xp = 0;
    bool awarded_kill_xp = false;

    // Melee voxels: only the cell under the cursor / crosshair, never every neighbour in the arc.
    int vx = 0, vy = 0, vz = 0;
    bool have_voxel = ctrl->attack_has_voxel_target;
    if (have_voxel)
    {
        vx = ctrl->attack_voxel_x;
        vy = ctrl->attack_voxel_y;
        vz = ctrl->attack_voxel_z;
    }
    else
        have_voxel = melee_pick_look_voxel(state, ctrl, &vx, &vy, &vz);

    if (have_voxel)
        apply_melee_voxel_hit(state, ctrl, world, vx, vy, vz, &hits, &voxels_destroyed,
                              &foliage_xp);

    if (world->runtime_actors && world->runtime_actor_count > 0)
    {
        const uint32_t attacker_id =
            (state->dominated_actor_id != 0) ? state->dominated_actor_id : MOB_THREAT_PLAYER;
        for (uint32_t i = 0; i < world->runtime_actor_count; i++)
        {
            Actor *a = &world->runtime_actors[i];
            if (!a->is_active || a->health == 0)
                continue;
            float dx = (float)a->x - px;
            float dy = (float)a->y - py;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > swing_r || dist < PLAYER_SWING_INNER_RADIUS)
                continue;

            float angle = atan2f(dy, dx);
            if (fabsf(angle_diff(ctrl->facing_yaw, angle)) > half_arc)
                continue;

            uint32_t dmg;
            if (ctrl->attack_armed)
            {
                dmg = 5u + ctrl->attack_strength / 4u + 4u;
            }
            else
            {
                // Unarmed punches scale with the inhabited body's strength (or the spirit's).
                dmg = 2u + ctrl->attack_strength / 2u;
                if (dmg < 1u)
                    dmg = 1u;
            }
            actor_apply_damage(a, dmg);
            mob_actor_after_damage(a, attacker_id);
            if (a->health == 0)
            {
                uint32_t xp = XP_MOB_KILL_BASE + a->level * 5u;
                char reason[96];
                snprintf(reason, sizeof(reason), "Defeated %s (+%u XP)",
                         a->name[0] ? a->name : "foe", xp);
                game_state_award_experience(state, xp, reason);
                awarded_kill_xp = true;
            }
            hits++;
        }
    }

    // The first-person mesh cache throttles rebuilds so ambient fluid motion cannot stutter the
    // frame rate, but a voxel the player just destroyed has to vanish immediately, so force it.
    if (voxels_destroyed > 0)
        fp_renderer_invalidate_cache();

    if (hits > 0)
        play_unarmed_hit_sound();

    if (foliage_xp > 0)
    {
        // Always go through the award path so dominated mobs bank their own XP.
        char reason[96];
        if (awarded_kill_xp)
            snprintf(reason, sizeof(reason), "+%u XP", foliage_xp);
        else
            snprintf(reason, sizeof(reason), "Cleared foliage (+%u XP)", foliage_xp);
        game_state_award_experience(state, foliage_xp, reason);
    }
    else if (hits > 0 && !awarded_kill_xp)
    {
        snprintf(state->status_message, sizeof(state->status_message),
                 "Hit %d target(s)", hits);
    }
}

bool player_controls_is_attackable_target(GameState *state, float wx, float wy, float wz)
{
    World *world = controls_world(state);
    if (!world)
        return false;

    int vx = (int)floorf(wx);
    int vy = (int)floorf(wy);
    int vz = (int)floorf(wz);

    for (int dz = -1; dz <= 1; dz++)
    {
        int cz = vz + dz;
        if (!world_is_position_valid(world, vx, vy, cz))
            continue;
        Voxel *voxel = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)cz);
        if (voxel && voxel_is_attackable(voxel->type))
            return true;
    }

    if (world->runtime_actors && world->runtime_actor_count > 0)
    {
        for (uint32_t i = 0; i < world->runtime_actor_count; i++)
        {
            Actor *a = &world->runtime_actors[i];
            float dx = (float)a->x - wx;
            float dy = (float)a->y - wy;
            if (dx * dx + dy * dy < 1.0f)
                return true;
        }
    }
    return false;
}

bool player_controls_start_attack(GameState *state, PlayerControls *ctrl,
                                  float target_x, float target_y)
{
    if (!state || !ctrl)
        return false;

    // Preserve attack-move destination so the queue resumes after the swing.
    if (!ctrl->move_target_is_attack_move)
        ctrl->has_move_target = false;
    // Do not zero velocity: a swing should not cancel WASD momentum.
    ctrl->is_attacking = true;
    ctrl->attack_start_ms = SDL_GetTicks();
    ctrl->attack_hit_applied = false;
    ctrl->attack_has_voxel_target = false;
    ctrl->attack_target_x = target_x;
    ctrl->attack_target_y = target_y;
    ctrl->attack_armed = player_controls_is_armed(state);
    ctrl->attack_strength = player_controls_attack_strength(state);
    if (ctrl->attack_armed)
    {
        ctrl->attack_radius = PLAYER_SWING_RADIUS_ARMED;
    }
    else
    {
        float r = PLAYER_SWING_RADIUS +
                  PLAYER_UNARMED_RADIUS_PER_STR * ((float)ctrl->attack_strength - 10.0f);
        if (r < PLAYER_SWING_RADIUS * 0.6f)
            r = PLAYER_SWING_RADIUS * 0.6f;
        if (r > PLAYER_SWING_RADIUS * 1.6f)
            r = PLAYER_SWING_RADIUS * 1.6f;
        ctrl->attack_radius = r;
    }
    player_controls_update_facing_from_world(state, ctrl, target_x, target_y);

    snprintf(state->status_message, sizeof(state->status_message),
             ctrl->attack_armed ? "Swing!" : "Punch!");
    play_unarmed_swing_sound();
    return true;
}

void player_controls_set_attack_voxel(PlayerControls *ctrl, int x, int y, int z)
{
    if (!ctrl)
        return;
    ctrl->attack_has_voxel_target = true;
    ctrl->attack_voxel_x = x;
    ctrl->attack_voxel_y = y;
    ctrl->attack_voxel_z = z;
}

bool player_controls_pick_melee_voxel(GameState *state, PlayerControls *ctrl,
                                      int *out_x, int *out_y, int *out_z)
{
    return melee_pick_look_voxel(state, ctrl, out_x, out_y, out_z);
}

void player_controls_update_attack(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl || !ctrl->is_attacking)
        return;

    uint32_t elapsed = SDL_GetTicks() - ctrl->attack_start_ms;
    float progress = (float)elapsed / (float)PLAYER_SWING_DURATION_MS;

    if (!ctrl->attack_hit_applied && progress >= PLAYER_SWING_HIT_START &&
        progress <= PLAYER_SWING_HIT_END)
    {
        apply_swing_damage(state, ctrl);
        ctrl->attack_hit_applied = true;
    }

    if (elapsed >= PLAYER_SWING_DURATION_MS)
    {
        ctrl->is_attacking = false;
        ctrl->attack_hit_applied = false;
        ctrl->attack_armed = false;
    }
}

bool player_controls_is_armed(GameState *state)
{
    if (!state || !state->player)
        return false;
    if (inventory_count_item(&state->player->inventory, ITEM_WEAPON_CLUB) > 0 ||
        inventory_count_item(&state->player->inventory, ITEM_WEAPON_DAGGER) > 0 ||
        inventory_count_item(&state->player->inventory, ITEM_WEAPON_SPEAR) > 0 ||
        inventory_count_item(&state->player->inventory, ITEM_WEAPON_SWORD) > 0 ||
        inventory_count_item(&state->player->inventory, ITEM_WEAPON_STAFF) > 0)
        return true;
    // An inhabited body with a worn weapon (or any gear) also reads as armed.
    if (player_controls_is_dominating(state))
    {
        const Actor *body = player_controls_dominated_actor(state);
        const Equipment *eq = body ? mob_actor_equipment_const(body) : NULL;
        if (eq)
        {
            if (eq->slots[EQUIP_SLOT_MAIN_HAND] != ITEM_NONE)
                return true;
            for (int i = 0; i < EQUIP_SLOT_COUNT; i++)
            {
                if (eq->slots[i] != ITEM_NONE)
                    return true;
            }
        }
    }
    return false;
}

uint32_t player_controls_attack_strength(GameState *state)
{
    if (!state)
        return 10;
    if (player_controls_is_dominating(state))
    {
        const Actor *body = player_controls_dominated_actor(state);
        if (body && body->strength > 0)
            return body->strength;
    }
    if (state->player && state->player->strength > 0)
        return state->player->strength;
    return 10;
}

float player_controls_jump_launch_speed(GameState *state)
{
    const float base = game_state_player_jump_velocity(state);
    const uint32_t str = player_controls_attack_strength(state);
    float scale = 1.0f + PLAYER_JUMP_IMPULSE_PER_STR *
                  ((float)str - (float)PLAYER_JUMP_STRENGTH_BASE);
    if (scale < 0.5f)
        scale = 0.5f;
    return base * scale;
}

float player_controls_swing_radius(const PlayerControls *ctrl)
{
    if (ctrl && ctrl->is_attacking && ctrl->attack_radius > 0.1f)
        return ctrl->attack_radius;
    if (ctrl && ctrl->attack_armed)
        return PLAYER_SWING_RADIUS_ARMED;
    return PLAYER_SWING_RADIUS;
}

float player_controls_swing_arc(const PlayerControls *ctrl)
{
    if (ctrl && ctrl->attack_armed)
        return PLAYER_SWING_ARC_ARMED;
    return PLAYER_SWING_ARC_RAD;
}

float player_controls_unarmed_pill_radius(const PlayerControls *ctrl)
{
    uint32_t str = ctrl ? ctrl->attack_strength : 10;
    float r = PLAYER_UNARMED_PILL_RADIUS_BASE + PLAYER_UNARMED_PILL_RADIUS_PER_STR * (float)str;
    if (r < 0.08f)
        r = 0.08f;
    if (r > 0.32f)
        r = 0.32f;
    return r;
}

float player_controls_swing_progress(const PlayerControls *ctrl)
{
    if (!ctrl || !ctrl->is_attacking)
        return 0.0f;
    float p = (float)(SDL_GetTicks() - ctrl->attack_start_ms) / (float)PLAYER_SWING_DURATION_MS;
    if (p < 0.0f)
        return 0.0f;
    if (p > 1.0f)
        return 1.0f;
    return p;
}

bool player_controls_fireball_ready(GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl || !state->game_started)
        return false;
    if (!controls_world(state))
        return false;
    // SDL_GetTicks is unsigned and wraps after ~49 days. Comparing the difference rather than the
    // absolute values keeps the skill usable across the wrap instead of locking it out.
    if (ctrl->fireball_ready_at_ms != 0 &&
        (int32_t)(SDL_GetTicks() - ctrl->fireball_ready_at_ms) < 0)
        return false;
    return controls_get_stamina(state) >= PLAYER_FIREBALL_STAMINA_COST;
}

// Casting entity for skill power: spirit attributes (skills are unlocked on the soul).
static const Actor *controls_skill_caster(const GameState *state)
{
    return state ? state->player : NULL;
}

static uint8_t controls_skill_rank(const GameState *state, SkillId id)
{
    return skill_rank(state ? state->player : NULL, id);
}

static uint32_t controls_rank_scale_u32(const GameState *state, SkillId id, uint32_t base)
{
    const float m = skill_rank_mult(controls_skill_rank(state, id));
    return (uint32_t)((float)base * m + 0.5f);
}

// The shared half of both cast forms: spend the resources and put the projectile in the air.
// `dx, dy, dz` need not be normalised.
static bool cast_fireball(GameState *state, PlayerControls *ctrl, float dx, float dy, float dz)
{
    if (!player_controls_fireball_ready(state, ctrl))
        return false;

    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (!(len > 1e-5f))
        return false;

    const float inv = 1.0f / len;
    const float nx = dx * inv, ny = dy * inv, nz = dz * inv;
    const Actor *caster = controls_skill_caster(state);
    const float power = actor_skill_power(caster);
    const uint8_t rank = controls_skill_rank(state, SKILL_FIREBALL);
    const float rm = skill_rank_mult(rank);
    float radius = PLAYER_FIREBALL_RADIUS * (0.85f + 0.15f * power) * (0.85f + 0.15f * rm);
    if (radius > PLAYER_FIREBALL_RADIUS * 2.2f)
        radius = PLAYER_FIREBALL_RADIUS * 2.2f;

    const ProjectileSpawn spawn = {
        .x = state->player_world_x + nx * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .y = state->player_world_y + ny * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .z = state->player_world_z + nz * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .dir_x = nx,
        .dir_y = ny,
        .dir_z = nz,
        .speed = PLAYER_FIREBALL_SPEED,
        .life = PLAYER_FIREBALL_LIFE_S,
        .radius = radius,
        .gravity_scale = PLAYER_FIREBALL_GRAVITY_SCALE,
        .gravity_delay = PLAYER_FIREBALL_GRAVITY_DELAY_S,
        .damage = actor_skill_scale_u32(caster, controls_rank_scale_u32(state, SKILL_FIREBALL,
                                                                       PLAYER_FIREBALL_DAMAGE)),
        .owner_id = (state->dominated_actor_id != 0) ? state->dominated_actor_id
                                                     : MOB_THREAT_PLAYER,
        .kind = PROJECTILE_FIREBALL,
        .potency = actor_skill_potency(caster),
        .bind_universe = true,
        .universe_x = state->player_universe_x,
        .universe_y = state->player_universe_y,
        .universe_z = state->player_universe_z,
    };

    if (!projectile_spawn(&state->projectiles, &spawn))
        return false; // pool full, and nothing has been spent yet

    controls_set_stamina(state, controls_get_stamina(state) - PLAYER_FIREBALL_STAMINA_COST);
    ctrl->fireball_ready_at_ms = SDL_GetTicks() + PLAYER_FIREBALL_COOLDOWN_MS;
    if (ctrl->fireball_ready_at_ms == 0)
        ctrl->fireball_ready_at_ms = 1; // zero is the "ready" sentinel

    snprintf(state->status_message, sizeof(state->status_message), "Fireball!");
    play_skill_cast_sound(SKILL_FIREBALL);
    return true;
}

bool player_controls_cast_fireball_at(GameState *state, PlayerControls *ctrl,
                                     float target_x, float target_y, float target_z)
{
    if (!state || !ctrl)
        return false;

    const float dx = target_x - state->player_world_x;
    const float dy = target_y - state->player_world_y;
    const float dz = target_z - state->player_world_z;

    if (!cast_fireball(state, ctrl, dx, dy, dz))
        return false;

    // Turned after the fact rather than before, so a cast that was refused does not leave the
    // player facing a direction they never fired in.
    player_controls_update_facing_from_world(state, ctrl, target_x, target_y);
    return true;
}

bool player_controls_cast_fireball_forward(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl)
        return false;

    const float cos_pitch = cosf(ctrl->pitch);
    return cast_fireball(state, ctrl, cosf(ctrl->facing_yaw) * cos_pitch,
                         sinf(ctrl->facing_yaw) * cos_pitch, sinf(ctrl->pitch));
}

bool player_controls_create_water_ready(GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl || !state->game_started)
        return false;
    if (!skill_is_known(state->player, SKILL_CREATE_WATER))
        return false;
    if (!controls_world(state))
        return false;
    if (ctrl->create_water_ready_at_ms != 0 &&
        (int32_t)(SDL_GetTicks() - ctrl->create_water_ready_at_ms) < 0)
        return false;
    return controls_get_stamina(state) >= PLAYER_CREATE_WATER_STAMINA_COST;
}

bool player_controls_cast_create_water(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_create_water_ready(state, ctrl))
        return false;

    World *world = controls_world(state);
    if (!world)
        return false;

    // Prefer the air/replaceable cell at the caster's feet; otherwise one step ahead.
    int bx = (int)floorf(state->player_world_x);
    int by = (int)floorf(state->player_world_y);
    int bz = (int)floorf(state->player_world_z);
    const float fx = cosf(ctrl->facing_yaw);
    const float fy = sinf(ctrl->facing_yaw);
    int ax = (int)floorf(state->player_world_x + fx * 1.2f);
    int ay = (int)floorf(state->player_world_y + fy * 1.2f);

    int tx = bx, ty = by, tz = bz;
    Voxel *at_feet = world_get_voxel(world, (uint32_t)bx, (uint32_t)by, (uint32_t)bz);
    if (at_feet && at_feet->type != VOXEL_AIR && at_feet->type != VOXEL_WATER)
    {
        tx = ax;
        ty = ay;
        // Drop onto the first open cell above solid ground ahead.
        Voxel *ahead = world_get_voxel(world, (uint32_t)ax, (uint32_t)ay, (uint32_t)bz);
        if (ahead && ahead->type != VOXEL_AIR && ahead->type != VOXEL_WATER)
            tz = bz + 1;
    }

    if (tx < 0 || ty < 0 || tz < 0 ||
        tx >= (int)world->width || ty >= (int)world->height || tz >= (int)world->depth)
        return false;

    // Intelligence spreads a wider puddle: INT 10 => 1 cell, INT 20 => 3x3, INT 30 => 5x5.
    // Skill rank expands further (I/II/III).
    const Actor *caster = controls_skill_caster(state);
    int radius = (int)(caster && caster->intelligence > 0
                           ? caster->intelligence / ACTOR_SKILL_POWER_BASE_INT
                           : 1);
    radius += (int)controls_skill_rank(state, SKILL_CREATE_WATER) - 1;
    if (radius < 1)
        radius = 1;
    if (radius > 5)
        radius = 5;

    int placed = 0;
    for (int dy = 1 - radius; dy <= radius - 1; dy++)
    {
        for (int dx = 1 - radius; dx <= radius - 1; dx++)
        {
            const int x = tx + dx;
            const int y = ty + dy;
            const int z = tz;
            if (x < 0 || y < 0 || z < 0 ||
                x >= (int)world->width || y >= (int)world->height || z >= (int)world->depth)
                continue;
            Voxel *cell = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
            if (!cell)
                continue;
            if (cell->type != VOXEL_AIR && cell->type != VOXEL_WATER)
                continue;
            world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_WATER);
            placed++;
            for (int ez = -1; ez <= 1; ez++)
                for (int ey = -1; ey <= 1; ey++)
                    for (int ex = -1; ex <= 1; ex++)
                        fire_try_extinguish_with_water(world, x + ex, y + ey, z + ez);
        }
    }
    if (placed == 0)
        return false;

    controls_set_stamina(state, controls_get_stamina(state) - PLAYER_CREATE_WATER_STAMINA_COST);
    ctrl->create_water_ready_at_ms = SDL_GetTicks() + PLAYER_CREATE_WATER_COOLDOWN_MS;
    if (ctrl->create_water_ready_at_ms == 0)
        ctrl->create_water_ready_at_ms = 1;

    snprintf(state->status_message, sizeof(state->status_message), "Create Water!");
    play_skill_cast_sound(SKILL_CREATE_WATER);
    fp_renderer_invalidate_cache();
    return true;
}

bool player_controls_ice_bolt_ready(GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl || !state->game_started)
        return false;
    if (!skill_is_known(state->player, SKILL_ICE_BOLT))
        return false;
    if (!controls_world(state))
        return false;
    if (ctrl->ice_bolt_ready_at_ms != 0 &&
        (int32_t)(SDL_GetTicks() - ctrl->ice_bolt_ready_at_ms) < 0)
        return false;
    return controls_get_stamina(state) >= PLAYER_ICE_BOLT_STAMINA_COST;
}

static bool cast_ice_bolt(GameState *state, PlayerControls *ctrl, float dx, float dy, float dz)
{
    if (!player_controls_ice_bolt_ready(state, ctrl))
        return false;

    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (!(len > 1e-5f))
        return false;

    const float inv = 1.0f / len;
    const float nx = dx * inv, ny = dy * inv, nz = dz * inv;
    const Actor *caster = controls_skill_caster(state);

    const ProjectileSpawn spawn = {
        .x = state->player_world_x + nx * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .y = state->player_world_y + ny * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .z = state->player_world_z + nz * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .dir_x = nx,
        .dir_y = ny,
        .dir_z = nz,
        .speed = PLAYER_ICE_BOLT_SPEED,
        .life = PLAYER_ICE_BOLT_LIFE_S,
        .radius = PLAYER_ICE_BOLT_RADIUS,
        .gravity_scale = PLAYER_ICE_BOLT_GRAVITY_SCALE,
        .gravity_delay = PLAYER_ICE_BOLT_GRAVITY_DELAY_S,
        .damage = actor_skill_scale_u32(caster, PLAYER_ICE_BOLT_DAMAGE),
        .owner_id = (state->dominated_actor_id != 0) ? state->dominated_actor_id
                                                     : MOB_THREAT_PLAYER,
        .kind = PROJECTILE_ICE_BOLT,
        .potency = actor_skill_potency(caster),
        .bind_universe = true,
        .universe_x = state->player_universe_x,
        .universe_y = state->player_universe_y,
        .universe_z = state->player_universe_z,
    };

    if (!projectile_spawn(&state->projectiles, &spawn))
        return false;

    controls_set_stamina(state, controls_get_stamina(state) - PLAYER_ICE_BOLT_STAMINA_COST);
    ctrl->ice_bolt_ready_at_ms = SDL_GetTicks() + PLAYER_ICE_BOLT_COOLDOWN_MS;
    if (ctrl->ice_bolt_ready_at_ms == 0)
        ctrl->ice_bolt_ready_at_ms = 1;

    snprintf(state->status_message, sizeof(state->status_message), "Ice Bolt!");
    play_skill_cast_sound(SKILL_ICE_BOLT);
    return true;
}

bool player_controls_cast_ice_bolt_at(GameState *state, PlayerControls *ctrl,
                                      float target_x, float target_y, float target_z)
{
    if (!state || !ctrl)
        return false;

    const float dx = target_x - state->player_world_x;
    const float dy = target_y - state->player_world_y;
    const float dz = target_z - state->player_world_z;

    if (!cast_ice_bolt(state, ctrl, dx, dy, dz))
        return false;

    player_controls_update_facing_from_world(state, ctrl, target_x, target_y);
    return true;
}

bool player_controls_cast_ice_bolt_forward(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl)
        return false;

    const float cos_pitch = cosf(ctrl->pitch);
    return cast_ice_bolt(state, ctrl, cosf(ctrl->facing_yaw) * cos_pitch,
                         sinf(ctrl->facing_yaw) * cos_pitch, sinf(ctrl->pitch));
}

static World *skill_world(GameState *state)
{
    return controls_world(state);
}

static Actor *find_runtime_actor_by_id(World *world, uint32_t id)
{
    if (!world || !world->runtime_actors || id == 0)
        return NULL;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (a->id == id)
            return a;
    }
    return NULL;
}

static bool skill_cooldown_blocking(uint32_t ready_at_ms)
{
    return ready_at_ms != 0 && (int32_t)(SDL_GetTicks() - ready_at_ms) < 0;
}

static void stamp_skill_cooldown(uint32_t *ready_at_ms, uint32_t cooldown_ms)
{
    *ready_at_ms = SDL_GetTicks() + cooldown_ms;
    if (*ready_at_ms == 0)
        *ready_at_ms = 1;
}

Actor *player_controls_dominated_actor(GameState *state)
{
    if (!state || state->dominated_actor_id == 0)
        return NULL;
    return find_runtime_actor_by_id(skill_world(state), state->dominated_actor_id);
}

bool player_controls_is_dominating(const GameState *state)
{
    return state && state->dominated_actor_id != 0;
}

void player_controls_release_dominate(GameState *state)
{
    if (!state)
        return;
    Actor *a = player_controls_dominated_actor(state);
    if (a)
    {
        a->is_controlled = false;
        a->x = (double)state->player_world_x;
        a->y = (double)state->player_world_y;
        a->z = (double)state->player_world_z;
        a->velocity_x = (double)state->controls.velocity_x;
        a->velocity_y = (double)state->controls.velocity_y;
        a->velocity_z = (double)state->controls.velocity_z;
        if (a->extra_data)
        {
            MobActor *mob = (MobActor *)a->extra_data;
            mob->base.is_controlled = false;
            mob->base.x = a->x;
            mob->base.y = a->y;
            mob->base.z = a->z;
            mob->base.velocity_x = a->velocity_x;
            mob->base.velocity_y = a->velocity_y;
            mob->base.velocity_z = a->velocity_z;
            mob->ai_state.has_target = false;
            mob->ai_state.retarget_in = 0.5f;
            if ((mob->mob_type == MOB_TYPE_BIRD || mob->mob_type == MOB_TYPE_BAT) &&
                (state->controls.fly_active || !game_state_player_is_grounded(state)))
            {
                a->is_flying = true;
                mob->base.is_flying = true;
                mob->ai_state.bird_phase = BIRD_PHASE_APPROACH;
                mob->ai_state.bird_timer = 6.0f;
                mob->ai_state.has_target = false;
            }
        }
        snprintf(state->status_message, sizeof(state->status_message),
                 "You leave the %s", a->name[0] ? a->name : "body");
    }
    state->dominated_actor_id = 0;
    state->controls.fly_active = false;
    state->controls.fly_was_airborne = false;
    state->controls.auto_glide_timer = 0.0f;
    state->controls.auto_glide_suppress = false;
    // Spirit returns to its default hover-flight — no roll, soft float.
    player_controls_set_spirit_hover(state, true);
    skill_refresh_hotbar(state, &state->controls);
}

static Actor *nearest_living_actor(GameState *state, float max_range, float *out_dist)
{
    World *world = skill_world(state);
    if (!state || !world || !world->runtime_actors)
        return NULL;

    Actor *best = NULL;
    float best_d2 = max_range * max_range;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->is_controlled)
            continue;
        float dx = (float)a->x - state->player_world_x;
        float dy = (float)a->y - state->player_world_y;
        float dz = (float)a->z - state->player_world_z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= best_d2)
        {
            best_d2 = d2;
            best = a;
        }
    }
    if (best && out_dist)
        *out_dist = sqrtf(best_d2);
    return best;
}

static uint32_t spell_attacker_id(const GameState *state)
{
    return (state && state->dominated_actor_id != 0) ? state->dominated_actor_id
                                                     : MOB_THREAT_PLAYER;
}

static void spell_award_kill_if_dead(GameState *state, Actor *victim, uint32_t attacker_id)
{
    if (!state || !victim || victim->health != 0)
        return;
    uint32_t xp = XP_MOB_KILL_BASE + victim->level * 5u;
    char reason[96];
    snprintf(reason, sizeof(reason), "Defeated %s (+%u XP)",
             victim->name[0] ? victim->name : "foe", xp);
    game_state_award_experience(state, xp, reason);
    if (state->dominated_actor_id == victim->id)
        player_controls_release_dominate(state);
    (void)attacker_id;
}

static void spell_hit_actor(GameState *state, Actor *victim, uint32_t damage)
{
    if (!state || !victim || damage == 0)
        return;
    const uint32_t attacker = spell_attacker_id(state);
    actor_apply_damage(victim, damage);
    mob_actor_after_damage(victim, attacker);
    spell_award_kill_if_dead(state, victim, attacker);
}

static bool spell_skill_ready(GameState *state, const PlayerControls *ctrl, SkillId id,
                              uint32_t ready_at_ms, float stamina_cost)
{
    if (!state || !ctrl || !state->game_started)
        return false;
    if (!skill_is_known(state->player, id))
        return false;
    if (!controls_world(state))
        return false;
    if (skill_cooldown_blocking(ready_at_ms))
        return false;
    return controls_get_stamina(state) >= stamina_cost;
}

static void spell_spend(GameState *state, PlayerControls *ctrl, float stamina_cost,
                        uint32_t *ready_at_ms, uint32_t cooldown_ms)
{
    controls_set_stamina(state, controls_get_stamina(state) - stamina_cost);
    stamp_skill_cooldown(ready_at_ms, cooldown_ms);
    (void)ctrl;
}

static void aim_ground_ahead(GameState *state, const PlayerControls *ctrl, float range,
                             float *ox, float *oy, float *oz)
{
    const float fx = cosf(ctrl->facing_yaw);
    const float fy = sinf(ctrl->facing_yaw);
    float tx = state->player_world_x + fx * range;
    float ty = state->player_world_y + fy * range;
    float tz = state->player_world_z;

    World *world = controls_world(state);
    if (world)
    {
        const int ix = (int)floorf(tx);
        const int iy = (int)floorf(ty);
        int start_z = (int)floorf(state->player_world_z + 4.0f);
        if (start_z >= (int)world->depth)
            start_z = (int)world->depth - 1;
        for (int z = start_z; z >= 0; z--)
        {
            if (ix < 0 || iy < 0 || ix >= (int)world->width || iy >= (int)world->height)
                break;
            Voxel *v = world_get_voxel(world, (uint32_t)ix, (uint32_t)iy, (uint32_t)z);
            if (v && v->type != VOXEL_AIR && v->type != VOXEL_WATER && v->type != VOXEL_LEAVES &&
                v->type != VOXEL_GRASS)
            {
                tz = (float)z + 1.05f;
                break;
            }
        }
    }
    *ox = tx;
    *oy = ty;
    *oz = tz;
}

static bool spawn_player_projectile(GameState *state, PlayerControls *ctrl,
                                    float dx, float dy, float dz,
                                    float speed, float life, float radius,
                                    float gravity_scale, float gravity_delay,
                                    uint32_t damage, ProjectileKind kind)
{
    (void)ctrl;
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (!(len > 1e-5f))
        return false;
    const float inv = 1.0f / len;
    const float nx = dx * inv, ny = dy * inv, nz = dz * inv;
    const Actor *caster = controls_skill_caster(state);

    const ProjectileSpawn spawn = {
        .x = state->player_world_x + nx * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .y = state->player_world_y + ny * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .z = state->player_world_z + nz * PLAYER_FIREBALL_MUZZLE_FORWARD,
        .dir_x = nx,
        .dir_y = ny,
        .dir_z = nz,
        .speed = speed,
        .life = life,
        .radius = radius,
        .gravity_scale = gravity_scale,
        .gravity_delay = gravity_delay,
        .damage = actor_skill_scale_u32(caster, damage),
        .owner_id = spell_attacker_id(state),
        .kind = kind,
        .potency = actor_skill_potency(caster),
        .bind_universe = true,
        .universe_x = state->player_universe_x,
        .universe_y = state->player_universe_y,
        .universe_z = state->player_universe_z,
    };
    return projectile_spawn(&state->projectiles, &spawn);
}

bool player_controls_magic_missile_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_MAGIC_MISSILE, ctrl ? ctrl->magic_missile_ready_at_ms : 0,
                             PLAYER_MAGIC_MISSILE_STAMINA_COST);
}

bool player_controls_cast_magic_missile_forward(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_magic_missile_ready(state, ctrl))
        return false;

    const float cos_pitch = cosf(ctrl->pitch);
    const float base_yaw = ctrl->facing_yaw;
    const float pitch = ctrl->pitch;
    const uint8_t rank = controls_skill_rank(state, SKILL_MAGIC_MISSILE);
    const int count = PLAYER_MAGIC_MISSILE_COUNT + (int)(rank - 1u) * 2; // 3 / 5 / 7
    const uint32_t dmg = controls_rank_scale_u32(state, SKILL_MAGIC_MISSILE, PLAYER_MAGIC_MISSILE_DAMAGE);
    int spawned = 0;
    for (int i = 0; i < count; i++)
    {
        const float yaw_off = ((float)i - (count - 1) * 0.5f) * 0.12f;
        const float yaw = base_yaw + yaw_off;
        if (spawn_player_projectile(state, ctrl,
                                    cosf(yaw) * cos_pitch, sinf(yaw) * cos_pitch, sinf(pitch),
                                    PLAYER_MAGIC_MISSILE_SPEED, PLAYER_MAGIC_MISSILE_LIFE_S,
                                    PLAYER_MAGIC_MISSILE_RADIUS, 0.0f, 0.0f,
                                    dmg, PROJECTILE_MAGIC_MISSILE))
            spawned++;
    }
    if (spawned == 0)
        return false;

    spell_spend(state, ctrl, PLAYER_MAGIC_MISSILE_STAMINA_COST, &ctrl->magic_missile_ready_at_ms,
                PLAYER_MAGIC_MISSILE_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Magic Missile!");
    play_skill_cast_sound(SKILL_MAGIC_MISSILE);
    return true;
}

bool player_controls_healing_word_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_HEALING_WORD, ctrl ? ctrl->healing_word_ready_at_ms : 0,
                             PLAYER_HEALING_WORD_STAMINA_COST);
}

bool player_controls_cast_healing_word(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_healing_word_ready(state, ctrl))
        return false;

    Actor *target = player_controls_dominated_actor(state);
    if (!target)
        target = state->player;
    if (!target)
        return false;

    const Actor *caster = controls_skill_caster(state);
    const uint32_t heal = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_HEALING_WORD, PLAYER_HEALING_WORD_HEAL));
    const uint32_t max_hp = actor_max_health(target);
    const uint32_t before = target->health;
    uint32_t after = before + heal;
    if (after > max_hp)
        after = max_hp;
    if (after == before)
    {
        snprintf(state->status_message, sizeof(state->status_message), "Already at full health");
        return false;
    }
    actor_set_health(target, after);
    spell_spend(state, ctrl, PLAYER_HEALING_WORD_STAMINA_COST, &ctrl->healing_word_ready_at_ms,
                PLAYER_HEALING_WORD_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message),
             "Healing Word restores %u", after - before);
    play_skill_cast_sound(SKILL_HEALING_WORD);
    return true;
}

bool player_controls_blink_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_BLINK, ctrl ? ctrl->blink_ready_at_ms : 0,
                             PLAYER_BLINK_STAMINA_COST);
}

bool player_controls_cast_blink(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_blink_ready(state, ctrl))
        return false;

    const float fx = cosf(ctrl->facing_yaw);
    const float fy = sinf(ctrl->facing_yaw);
    float best_x = state->player_world_x;
    float best_y = state->player_world_y;
    float best_z = state->player_world_z;
    bool found = false;
    const uint8_t rank = controls_skill_rank(state, SKILL_BLINK);
    const float max_dist = PLAYER_BLINK_DISTANCE + (float)(rank - 1u) * 3.0f; // 6 / 9 / 12

    for (float dist = max_dist; dist >= 1.0f; dist -= 0.5f)
    {
        const float tx = state->player_world_x + fx * dist;
        const float ty = state->player_world_y + fy * dist;
        float tz = state->player_world_z;
        if (player_controls_can_occupy(state, tx, ty, tz) ||
            player_controls_can_occupy(state, tx, ty, tz + 1.0f))
        {
            if (!player_controls_can_occupy(state, tx, ty, tz))
                tz += 1.0f;
            best_x = tx;
            best_y = ty;
            best_z = tz;
            found = true;
            break;
        }
        if (player_controls_can_occupy(state, tx, ty, tz - 1.0f))
        {
            best_x = tx;
            best_y = ty;
            best_z = tz - 1.0f;
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    state->player_world_x = best_x;
    state->player_world_y = best_y;
    state->player_world_z = best_z;
    ctrl->velocity_x = 0.0f;
    ctrl->velocity_y = 0.0f;
    ctrl->velocity_z = 0.0f;
    ctrl->has_move_target = false;
    player_controls_clear_command_queue(ctrl);

    Actor *body = player_controls_dominated_actor(state);
    if (body)
    {
        body->x = (double)best_x;
        body->y = (double)best_y;
        body->z = (double)best_z;
        body->velocity_x = body->velocity_y = body->velocity_z = 0.0;
    }

    spell_spend(state, ctrl, PLAYER_BLINK_STAMINA_COST, &ctrl->blink_ready_at_ms,
                PLAYER_BLINK_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Blink!");
    play_skill_cast_sound(SKILL_BLINK);
    return true;
}

bool player_controls_lightning_bolt_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_LIGHTNING_BOLT,
                             ctrl ? ctrl->lightning_bolt_ready_at_ms : 0,
                             PLAYER_LIGHTNING_BOLT_STAMINA_COST);
}

bool player_controls_cast_lightning_bolt_forward(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_lightning_bolt_ready(state, ctrl))
        return false;

    const float cos_pitch = cosf(ctrl->pitch);
    if (!spawn_player_projectile(state, ctrl,
                                 cosf(ctrl->facing_yaw) * cos_pitch,
                                 sinf(ctrl->facing_yaw) * cos_pitch, sinf(ctrl->pitch),
                                 PLAYER_LIGHTNING_BOLT_SPEED, PLAYER_LIGHTNING_BOLT_LIFE_S,
                                 PLAYER_LIGHTNING_BOLT_RADIUS, 0.05f, 0.2f,
                                 PLAYER_LIGHTNING_BOLT_DAMAGE, PROJECTILE_LIGHTNING))
        return false;

    spell_spend(state, ctrl, PLAYER_LIGHTNING_BOLT_STAMINA_COST, &ctrl->lightning_bolt_ready_at_ms,
                PLAYER_LIGHTNING_BOLT_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Lightning Bolt!");
    play_skill_cast_sound(SKILL_LIGHTNING_BOLT);
    return true;
}

bool player_controls_shadow_strike_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_SHADOW_STRIKE,
                             ctrl ? ctrl->shadow_strike_ready_at_ms : 0,
                             PLAYER_SHADOW_STRIKE_STAMINA_COST);
}

bool player_controls_cast_shadow_strike_forward(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_shadow_strike_ready(state, ctrl))
        return false;

    const float cos_pitch = cosf(ctrl->pitch);
    if (!spawn_player_projectile(state, ctrl,
                                 cosf(ctrl->facing_yaw) * cos_pitch,
                                 sinf(ctrl->facing_yaw) * cos_pitch, sinf(ctrl->pitch),
                                 PLAYER_SHADOW_STRIKE_SPEED, PLAYER_SHADOW_STRIKE_LIFE_S,
                                 PLAYER_SHADOW_STRIKE_RADIUS, 0.1f, 0.3f,
                                 PLAYER_SHADOW_STRIKE_DAMAGE, PROJECTILE_SHADOW_STRIKE))
        return false;

    spell_spend(state, ctrl, PLAYER_SHADOW_STRIKE_STAMINA_COST, &ctrl->shadow_strike_ready_at_ms,
                PLAYER_SHADOW_STRIKE_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Shadow Strike!");
    play_skill_cast_sound(SKILL_SHADOW_STRIKE);
    return true;
}

bool player_controls_sun_strike_ready(GameState *state, const PlayerControls *ctrl)
{
    if (!ctrl)
        return false;
    if (ctrl->sun_strike_pending)
        return false;
    return spell_skill_ready(state, ctrl, SKILL_SUN_STRIKE, ctrl->sun_strike_ready_at_ms,
                             PLAYER_SUN_STRIKE_STAMINA_COST);
}

bool player_controls_sun_strike_pending(const PlayerControls *ctrl)
{
    return ctrl && ctrl->sun_strike_pending;
}

bool player_controls_cast_sun_strike(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_sun_strike_ready(state, ctrl))
        return false;

    float tx, ty, tz;
    aim_ground_ahead(state, ctrl, PLAYER_SUN_STRIKE_RANGE, &tx, &ty, &tz);
    const Actor *caster = controls_skill_caster(state);

    ctrl->sun_strike_pending = true;
    ctrl->sun_strike_land_at_ms = SDL_GetTicks() + PLAYER_SUN_STRIKE_DELAY_MS;
    if (ctrl->sun_strike_land_at_ms == 0)
        ctrl->sun_strike_land_at_ms = 1;
    ctrl->sun_strike_x = tx;
    ctrl->sun_strike_y = ty;
    ctrl->sun_strike_z = tz;
    ctrl->sun_strike_damage = actor_skill_scale_u32(caster, PLAYER_SUN_STRIKE_DAMAGE);
    ctrl->sun_strike_potency = actor_skill_potency(caster);

    spell_spend(state, ctrl, PLAYER_SUN_STRIKE_STAMINA_COST, &ctrl->sun_strike_ready_at_ms,
                PLAYER_SUN_STRIKE_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Sun Strike marked!");
    play_skill_cast_sound(SKILL_SUN_STRIKE);
    return true;
}

void player_controls_update_sun_strike(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl || !ctrl->sun_strike_pending)
        return;
    if ((int32_t)(SDL_GetTicks() - ctrl->sun_strike_land_at_ms) < 0)
        return;

    ctrl->sun_strike_pending = false;
    World *world = controls_world(state);
    if (!world)
        return;

    const float bx = ctrl->sun_strike_x;
    const float by = ctrl->sun_strike_y;
    const float bz = ctrl->sun_strike_z;
    const float blast_r = PLAYER_SUN_STRIKE_BLAST_RADIUS;
    const uint32_t dmg = ctrl->sun_strike_damage;

    particle_effects_spawn_blast(world, bx, by, bz, false, blast_r);
    {
        GameSfxSystem *sfx = game_sfx_get_active();
        if (sfx)
            game_sfx_play_event(sfx, SFX_EVENT_SUN_STRIKE_IMPACT);
    }

    if (world->runtime_actors)
    {
        const float r2 = blast_r * blast_r;
        for (int i = 0; i < world->runtime_actor_count; i++)
        {
            Actor *a = &world->runtime_actors[i];
            if (!a->is_active || a->health == 0 || a->is_controlled)
                continue;
            const float dx = (float)a->x - bx;
            const float dy = (float)a->y - by;
            const float dz = (float)a->z - bz;
            const float dist2 = dx * dx + dy * dy + dz * dz;
            if (dist2 > r2)
                continue;
            const float t = 1.0f - sqrtf(dist2) / blast_r;
            const uint32_t hit = (uint32_t)((float)dmg * (0.45f + 0.55f * t) + 0.5f);
            spell_hit_actor(state, a, hit);
        }
    }

    const int cx = (int)floorf(bx), cy = (int)floorf(by), cz = (int)floorf(bz);
    const int ir = (int)ceilf(blast_r);
    for (int dz = -ir; dz <= ir; dz++)
        for (int dy = -ir; dy <= ir; dy++)
            for (int dx = -ir; dx <= ir; dx++)
            {
                const float dist = sqrtf((float)(dx * dx + dy * dy + dz * dz));
                if (dist > blast_r)
                    continue;
                const float t = 1.0f - dist / blast_r;
                const int x = cx + dx, y = cy + dy, z = cz + dz;
                if (x < 0 || y < 0 || z < 0 ||
                    x >= (int)world->width || y >= (int)world->height || z >= (int)world->depth)
                    continue;
                Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                if (!v || v->type == VOXEL_AIR)
                    continue;
                const uint32_t heat = (uint32_t)((float)PLAYER_FIREBALL_HEAT_AMOUNT * t + 0.5f);
                if (heat > 0)
                {
                    if (voxel_get_temperature(v) == 0u && v->type != VOXEL_ICE)
                        voxel_set_temperature(v, VOXEL_TEMP_AMBIENT);
                    voxel_heat(v, heat > 255u ? (uint8_t)255u : (uint8_t)heat);
                    world->voxel_revision++;
                }
                if (t >= 0.4f)
                    fire_ignite_at(world, x, y, z, t >= 0.75f ? "BURNING_MEDIUM" : "BURNING_LOW");
            }

    snprintf(state->status_message, sizeof(state->status_message), "Sun Strike!");
    fp_renderer_invalidate_cache();
}

bool player_controls_chain_lightning_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_CHAIN_LIGHTNING,
                             ctrl ? ctrl->chain_lightning_ready_at_ms : 0,
                             PLAYER_CHAIN_LIGHTNING_STAMINA_COST);
}

bool player_controls_chain_lightning_in_range(GameState *state)
{
    return nearest_living_actor(state, PLAYER_CHAIN_LIGHTNING_RANGE, NULL) != NULL;
}

bool player_controls_cast_chain_lightning(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_chain_lightning_ready(state, ctrl))
        return false;

    World *world = controls_world(state);
    if (!world || !world->runtime_actors)
        return false;

    Actor *hit[PLAYER_CHAIN_LIGHTNING_MAX_JUMPS];
    int hit_count = 0;
    Actor *current = nearest_living_actor(state, PLAYER_CHAIN_LIGHTNING_RANGE, NULL);
    if (!current)
        return false;

    while (current && hit_count < PLAYER_CHAIN_LIGHTNING_MAX_JUMPS)
    {
        hit[hit_count++] = current;
        Actor *next = NULL;
        float best_d2 = PLAYER_CHAIN_LIGHTNING_JUMP * PLAYER_CHAIN_LIGHTNING_JUMP;
        for (int i = 0; i < world->runtime_actor_count; i++)
        {
            Actor *a = &world->runtime_actors[i];
            if (!a->is_active || a->health == 0 || a->is_controlled)
                continue;
            bool already = false;
            for (int h = 0; h < hit_count; h++)
                if (hit[h] == a)
                {
                    already = true;
                    break;
                }
            if (already)
                continue;
            const float dx = (float)a->x - (float)current->x;
            const float dy = (float)a->y - (float)current->y;
            const float dz = (float)a->z - (float)current->z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 <= best_d2)
            {
                best_d2 = d2;
                next = a;
            }
        }
        current = next;
    }

    const Actor *caster = controls_skill_caster(state);
    uint32_t dmg = actor_skill_scale_u32(caster, PLAYER_CHAIN_LIGHTNING_DAMAGE);
    for (int i = 0; i < hit_count; i++)
    {
        spell_hit_actor(state, hit[i], dmg);
        if (dmg > 4u)
            dmg = (dmg * 3u) / 4u; // falloff per jump
        particle_effects_spawn_blast(world, (float)hit[i]->x, (float)hit[i]->y,
                                     (float)hit[i]->z + 0.8f, false, 1.2f);
    }

    spell_spend(state, ctrl, PLAYER_CHAIN_LIGHTNING_STAMINA_COST, &ctrl->chain_lightning_ready_at_ms,
                PLAYER_CHAIN_LIGHTNING_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message),
             "Chain Lightning (%d)", hit_count);
    play_skill_cast_sound(SKILL_CHAIN_LIGHTNING);
    return true;
}

bool player_controls_meteor_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_METEOR, ctrl ? ctrl->meteor_ready_at_ms : 0,
                             PLAYER_METEOR_STAMINA_COST);
}

bool player_controls_cast_meteor_at(GameState *state, PlayerControls *ctrl,
                                    float target_x, float target_y, float target_z)
{
    if (!player_controls_meteor_ready(state, ctrl))
        return false;

    World *world = controls_world(state);
    if (!world)
        return false;

    float spawn_z = target_z + PLAYER_METEOR_SPAWN_HEIGHT;
    if (spawn_z >= (float)world->depth - 1.0f)
        spawn_z = (float)world->depth - 1.5f;
    if (spawn_z < target_z + 2.0f)
        spawn_z = target_z + 2.0f;

    const Actor *caster = controls_skill_caster(state);
    const ProjectileSpawn spawn = {
        .x = target_x,
        .y = target_y,
        .z = spawn_z,
        .dir_x = 0.0f,
        .dir_y = 0.0f,
        .dir_z = -1.0f,
        .speed = PLAYER_METEOR_SPEED,
        .life = PLAYER_METEOR_LIFE_S,
        .radius = PLAYER_METEOR_RADIUS,
        .gravity_scale = 1.35f,
        .gravity_delay = 0.0f,
        .damage = actor_skill_scale_u32(caster, controls_rank_scale_u32(state, SKILL_METEOR,
                                                                       PLAYER_METEOR_DAMAGE)),
        .owner_id = spell_attacker_id(state),
        .kind = PROJECTILE_METEOR,
        .potency = (uint16_t)((float)actor_skill_potency(caster) *
                                  skill_rank_mult(controls_skill_rank(state, SKILL_METEOR)) +
                              0.5f),
        .bind_universe = true,
        .universe_x = state->player_universe_x,
        .universe_y = state->player_universe_y,
        .universe_z = state->player_universe_z,
    };

    if (!projectile_spawn(&state->projectiles, &spawn))
        return false;

    spell_spend(state, ctrl, PLAYER_METEOR_STAMINA_COST, &ctrl->meteor_ready_at_ms,
                PLAYER_METEOR_COOLDOWN_MS);
    player_controls_update_facing_from_world(state, ctrl, target_x, target_y);
    snprintf(state->status_message, sizeof(state->status_message), "Meteor!");
    play_skill_cast_sound(SKILL_METEOR);
    return true;
}

bool player_controls_cast_meteor_forward(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl)
        return false;
    float tx, ty, tz;
    aim_ground_ahead(state, ctrl, PLAYER_METEOR_RANGE, &tx, &ty, &tz);
    return player_controls_cast_meteor_at(state, ctrl, tx, ty, tz);
}

bool player_controls_laguna_blade_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_LAGUNA_BLADE,
                             ctrl ? ctrl->laguna_blade_ready_at_ms : 0,
                             PLAYER_LAGUNA_BLADE_STAMINA_COST);
}

bool player_controls_cast_laguna_blade(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_laguna_blade_ready(state, ctrl))
        return false;

    World *world = controls_world(state);
    if (!world || !world->runtime_actors)
        return false;

    const float cos_pitch = cosf(ctrl->pitch);
    const float fx = cosf(ctrl->facing_yaw) * cos_pitch;
    const float fy = sinf(ctrl->facing_yaw) * cos_pitch;
    const float fz = sinf(ctrl->pitch);

    Actor *best = NULL;
    float best_along = PLAYER_LAGUNA_BLADE_RANGE;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->is_controlled)
            continue;
        const float dx = (float)a->x - state->player_world_x;
        const float dy = (float)a->y - state->player_world_y;
        const float dz = (float)a->z + 0.8f - state->player_world_z;
        const float along = dx * fx + dy * fy + dz * fz;
        if (along < 0.5f || along > PLAYER_LAGUNA_BLADE_RANGE)
            continue;
        const float px = dx - fx * along;
        const float py = dy - fy * along;
        const float pz = dz - fz * along;
        if (px * px + py * py + pz * pz > 1.8f * 1.8f)
            continue;
        if (along < best_along)
        {
            best_along = along;
            best = a;
        }
    }
    if (!best)
    {
        snprintf(state->status_message, sizeof(state->status_message), "Laguna Blade misses");
        return false;
    }

    const Actor *caster = controls_skill_caster(state);
    const uint32_t dmg = actor_skill_scale_u32(caster, PLAYER_LAGUNA_BLADE_DAMAGE);
    spell_hit_actor(state, best, dmg);
    particle_effects_spawn_blast(world, (float)best->x, (float)best->y, (float)best->z + 0.9f,
                                 false, 1.8f);

    spell_spend(state, ctrl, PLAYER_LAGUNA_BLADE_STAMINA_COST, &ctrl->laguna_blade_ready_at_ms,
                PLAYER_LAGUNA_BLADE_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message),
             "Laguna Blade hits %s!", best->name[0] ? best->name : "foe");
    play_skill_cast_sound(SKILL_LAGUNA_BLADE);
    return true;
}

bool player_controls_finger_of_death_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_FINGER_OF_DEATH,
                             ctrl ? ctrl->finger_of_death_ready_at_ms : 0,
                             PLAYER_FINGER_OF_DEATH_STAMINA_COST);
}

bool player_controls_finger_of_death_in_range(GameState *state)
{
    return nearest_living_actor(state, PLAYER_FINGER_OF_DEATH_RANGE, NULL) != NULL;
}

bool player_controls_cast_finger_of_death(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_finger_of_death_ready(state, ctrl))
        return false;

    Actor *target = nearest_living_actor(state, PLAYER_FINGER_OF_DEATH_RANGE, NULL);
    if (!target)
        return false;

    World *world = controls_world(state);
    const Actor *caster = controls_skill_caster(state);
    const uint32_t dmg = actor_skill_scale_u32(caster, PLAYER_FINGER_OF_DEATH_DAMAGE);
    spell_hit_actor(state, target, dmg);
    if (world)
        particle_effects_spawn_blast(world, (float)target->x, (float)target->y,
                                     (float)target->z + 0.9f, false, 2.0f);

    spell_spend(state, ctrl, PLAYER_FINGER_OF_DEATH_STAMINA_COST, &ctrl->finger_of_death_ready_at_ms,
                PLAYER_FINGER_OF_DEATH_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message),
             "Finger of Death on %s!", target->name[0] ? target->name : "foe");
    play_skill_cast_sound(SKILL_FINGER_OF_DEATH);
    return true;
}

bool player_controls_thunder_wrath_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_THUNDER_WRATH,
                             ctrl ? ctrl->thunder_wrath_ready_at_ms : 0,
                             PLAYER_THUNDER_WRATH_STAMINA_COST);
}

bool player_controls_cast_thunder_wrath(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_thunder_wrath_ready(state, ctrl))
        return false;

    World *world = controls_world(state);
    if (!world || !world->runtime_actors)
        return false;

    const Actor *caster = controls_skill_caster(state);
    const uint32_t dmg = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_THUNDER_WRATH, PLAYER_THUNDER_WRATH_DAMAGE));
    const float range =
        PLAYER_THUNDER_WRATH_RANGE * skill_rank_mult(controls_skill_rank(state, SKILL_THUNDER_WRATH));
    const float r2 = range * range;
    int hits = 0;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->is_controlled)
            continue;
        const float dx = (float)a->x - state->player_world_x;
        const float dy = (float)a->y - state->player_world_y;
        const float dz = (float)a->z - state->player_world_z;
        if (dx * dx + dy * dy + dz * dz > r2)
            continue;
        spell_hit_actor(state, a, dmg);
        particle_effects_spawn_blast(world, (float)a->x, (float)a->y, (float)a->z + 1.2f, false,
                                     1.4f);
        hits++;
    }
    if (hits == 0)
        return false;

    spell_spend(state, ctrl, PLAYER_THUNDER_WRATH_STAMINA_COST, &ctrl->thunder_wrath_ready_at_ms,
                PLAYER_THUNDER_WRATH_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message),
             "Thunder Wrath (%d)", hits);
    play_skill_cast_sound(SKILL_THUNDER_WRATH);
    return true;
}

bool player_controls_cold_snap_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_COLD_SNAP, ctrl ? ctrl->cold_snap_ready_at_ms : 0,
                             PLAYER_COLD_SNAP_STAMINA_COST);
}

bool player_controls_cast_cold_snap_forward(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_cold_snap_ready(state, ctrl))
        return false;
    const float cos_pitch = cosf(ctrl->pitch);
    const uint32_t dmg =
        controls_rank_scale_u32(state, SKILL_COLD_SNAP, PLAYER_COLD_SNAP_DAMAGE);
    if (!spawn_player_projectile(state, ctrl, cosf(ctrl->facing_yaw) * cos_pitch,
                                 sinf(ctrl->facing_yaw) * cos_pitch, sinf(ctrl->pitch),
                                 PLAYER_COLD_SNAP_SPEED, PLAYER_COLD_SNAP_LIFE_S,
                                 PLAYER_COLD_SNAP_RADIUS, 0.12f, 0.25f, dmg, PROJECTILE_ICE_BOLT))
        return false;
    spell_spend(state, ctrl, PLAYER_COLD_SNAP_STAMINA_COST, &ctrl->cold_snap_ready_at_ms,
                PLAYER_COLD_SNAP_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Cold Snap!");
    play_skill_cast_sound(SKILL_COLD_SNAP);
    return true;
}

bool player_controls_arcane_bolt_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_ARCANE_BOLT, ctrl ? ctrl->arcane_bolt_ready_at_ms : 0,
                             PLAYER_ARCANE_BOLT_STAMINA_COST);
}

bool player_controls_cast_arcane_bolt_forward(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_arcane_bolt_ready(state, ctrl))
        return false;
    const float cos_pitch = cosf(ctrl->pitch);
    const uint32_t dmg =
        controls_rank_scale_u32(state, SKILL_ARCANE_BOLT, PLAYER_ARCANE_BOLT_DAMAGE);
    if (!spawn_player_projectile(state, ctrl, cosf(ctrl->facing_yaw) * cos_pitch,
                                 sinf(ctrl->facing_yaw) * cos_pitch, sinf(ctrl->pitch),
                                 PLAYER_ARCANE_BOLT_SPEED, PLAYER_ARCANE_BOLT_LIFE_S,
                                 PLAYER_ARCANE_BOLT_RADIUS, 0.0f, 0.0f, dmg,
                                 PROJECTILE_MAGIC_MISSILE))
        return false;
    spell_spend(state, ctrl, PLAYER_ARCANE_BOLT_STAMINA_COST, &ctrl->arcane_bolt_ready_at_ms,
                PLAYER_ARCANE_BOLT_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Arcane Bolt!");
    play_skill_cast_sound(SKILL_ARCANE_BOLT);
    return true;
}

bool player_controls_meat_hook_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_MEAT_HOOK, ctrl ? ctrl->meat_hook_ready_at_ms : 0,
                             PLAYER_MEAT_HOOK_STAMINA_COST);
}

bool player_controls_cast_meat_hook_forward(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_meat_hook_ready(state, ctrl))
        return false;
    // Instant grab along the look ray (Pudge hook simplified): damage + pin.
    World *world = controls_world(state);
    if (!world || !world->runtime_actors)
        return false;

    const float cos_pitch = cosf(ctrl->pitch);
    const float fx = cosf(ctrl->facing_yaw) * cos_pitch;
    const float fy = sinf(ctrl->facing_yaw) * cos_pitch;
    const float fz = sinf(ctrl->pitch);
    const float range = 11.0f + (float)(controls_skill_rank(state, SKILL_MEAT_HOOK) - 1u) * 3.0f;

    Actor *best = NULL;
    float best_along = range;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->is_controlled)
            continue;
        const float dx = (float)a->x - state->player_world_x;
        const float dy = (float)a->y - state->player_world_y;
        const float dz = (float)a->z + 0.8f - state->player_world_z;
        const float along = dx * fx + dy * fy + dz * fz;
        if (along < 0.8f || along > range)
            continue;
        const float px = dx - fx * along, py = dy - fy * along, pz = dz - fz * along;
        if (px * px + py * py + pz * pz > 2.0f * 2.0f)
            continue;
        if (along < best_along)
        {
            best_along = along;
            best = a;
        }
    }
    if (!best)
        return false;

    const Actor *caster = controls_skill_caster(state);
    const uint32_t dmg = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_MEAT_HOOK, PLAYER_MEAT_HOOK_DAMAGE));
    spell_hit_actor(state, best, dmg);
    best->freeze_ttl =
        PLAYER_MEAT_HOOK_FREEZE_S * skill_rank_mult(controls_skill_rank(state, SKILL_MEAT_HOOK));
    best->chill = 255;
    particle_effects_spawn_blast(world, (float)best->x, (float)best->y, (float)best->z + 0.8f, true,
                                 1.5f);
    spell_spend(state, ctrl, PLAYER_MEAT_HOOK_STAMINA_COST, &ctrl->meat_hook_ready_at_ms,
                PLAYER_MEAT_HOOK_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Meat Hook!");
    play_skill_cast_sound(SKILL_MEAT_HOOK);
    return true;
}

bool player_controls_tornado_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_TORNADO, ctrl ? ctrl->tornado_ready_at_ms : 0,
                             PLAYER_TORNADO_STAMINA_COST);
}

bool player_controls_cast_tornado(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_tornado_ready(state, ctrl))
        return false;
    World *world = controls_world(state);
    if (!world)
        return false;
    float tx, ty, tz;
    aim_ground_ahead(state, ctrl, PLAYER_TORNADO_RANGE, &tx, &ty, &tz);
    const uint8_t rank = controls_skill_rank(state, SKILL_TORNADO);
    const float blast = PLAYER_TORNADO_BLAST_RADIUS * skill_rank_mult(rank);
    const Actor *caster = controls_skill_caster(state);
    const uint32_t dmg = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_TORNADO, PLAYER_TORNADO_DAMAGE));
    particle_effects_spawn_blast(world, tx, ty, tz, true, blast);
    int hits = 0;
    if (world->runtime_actors)
    {
        const float r2 = blast * blast;
        for (int i = 0; i < world->runtime_actor_count; i++)
        {
            Actor *a = &world->runtime_actors[i];
            if (!a->is_active || a->health == 0 || a->is_controlled)
                continue;
            const float dx = (float)a->x - tx, dy = (float)a->y - ty, dz = (float)a->z - tz;
            if (dx * dx + dy * dy + dz * dz > r2)
                continue;
            spell_hit_actor(state, a, dmg);
            uint16_t chill = (uint16_t)a->chill + PLAYER_TORNADO_CHILL * rank;
            a->chill = chill > 255u ? 255u : (uint8_t)chill;
            if (a->chill >= 255u)
                a->freeze_ttl = 1.5f * skill_rank_mult(rank);
            hits++;
        }
    }
    spell_spend(state, ctrl, PLAYER_TORNADO_STAMINA_COST, &ctrl->tornado_ready_at_ms,
                PLAYER_TORNADO_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Tornado (%d)", hits);
    play_skill_cast_sound(SKILL_TORNADO);
    return true;
}

bool player_controls_echo_slam_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_ECHO_SLAM, ctrl ? ctrl->echo_slam_ready_at_ms : 0,
                             PLAYER_ECHO_SLAM_STAMINA_COST);
}

bool player_controls_cast_echo_slam(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_echo_slam_ready(state, ctrl))
        return false;
    World *world = controls_world(state);
    if (!world || !world->runtime_actors)
        return false;
    const uint8_t rank = controls_skill_rank(state, SKILL_ECHO_SLAM);
    const float range = PLAYER_ECHO_SLAM_RANGE * skill_rank_mult(rank);
    const float r2 = range * range;
    Actor *victims[64];
    int n = 0;
    for (int i = 0; i < world->runtime_actor_count && n < 64; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->is_controlled)
            continue;
        const float dx = (float)a->x - state->player_world_x;
        const float dy = (float)a->y - state->player_world_y;
        const float dz = (float)a->z - state->player_world_z;
        if (dx * dx + dy * dy + dz * dz > r2)
            continue;
        victims[n++] = a;
    }
    if (n == 0)
        return false;
    const Actor *caster = controls_skill_caster(state);
    const uint32_t base = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_ECHO_SLAM, PLAYER_ECHO_SLAM_DAMAGE));
    const uint32_t echo = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_ECHO_SLAM, PLAYER_ECHO_SLAM_ECHO_DAMAGE));
    for (int i = 0; i < n; i++)
    {
        spell_hit_actor(state, victims[i], base + echo * (uint32_t)(n - 1));
        particle_effects_spawn_blast(world, (float)victims[i]->x, (float)victims[i]->y,
                                     (float)victims[i]->z + 0.5f, false, 1.6f);
    }
    spell_spend(state, ctrl, PLAYER_ECHO_SLAM_STAMINA_COST, &ctrl->echo_slam_ready_at_ms,
                PLAYER_ECHO_SLAM_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Echo Slam (%d)", n);
    play_skill_cast_sound(SKILL_ECHO_SLAM);
    return true;
}

bool player_controls_assassinate_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_ASSASSINATE, ctrl ? ctrl->assassinate_ready_at_ms : 0,
                             PLAYER_ASSASSINATE_STAMINA_COST);
}

bool player_controls_cast_assassinate(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_assassinate_ready(state, ctrl))
        return false;
    World *world = controls_world(state);
    if (!world || !world->runtime_actors)
        return false;
    const float cos_pitch = cosf(ctrl->pitch);
    const float fx = cosf(ctrl->facing_yaw) * cos_pitch;
    const float fy = sinf(ctrl->facing_yaw) * cos_pitch;
    const float fz = sinf(ctrl->pitch);
    const float range =
        PLAYER_ASSASSINATE_RANGE * skill_rank_mult(controls_skill_rank(state, SKILL_ASSASSINATE));
    Actor *best = NULL;
    float best_along = range;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->is_controlled)
            continue;
        const float dx = (float)a->x - state->player_world_x;
        const float dy = (float)a->y - state->player_world_y;
        const float dz = (float)a->z + 0.8f - state->player_world_z;
        const float along = dx * fx + dy * fy + dz * fz;
        if (along < 1.0f || along > range)
            continue;
        const float px = dx - fx * along, py = dy - fy * along, pz = dz - fz * along;
        if (px * px + py * py + pz * pz > 1.6f * 1.6f)
            continue;
        if (along < best_along)
        {
            best_along = along;
            best = a;
        }
    }
    if (!best)
        return false;
    const Actor *caster = controls_skill_caster(state);
    const uint32_t dmg = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_ASSASSINATE, PLAYER_ASSASSINATE_DAMAGE));
    spell_hit_actor(state, best, dmg);
    particle_effects_spawn_blast(world, (float)best->x, (float)best->y, (float)best->z + 0.9f, false,
                                 1.3f);
    spell_spend(state, ctrl, PLAYER_ASSASSINATE_STAMINA_COST, &ctrl->assassinate_ready_at_ms,
                PLAYER_ASSASSINATE_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Assassinate!");
    play_skill_cast_sound(SKILL_ASSASSINATE);
    return true;
}

bool player_controls_ravage_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_RAVAGE, ctrl ? ctrl->ravage_ready_at_ms : 0,
                             PLAYER_RAVAGE_STAMINA_COST);
}

bool player_controls_cast_ravage(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_ravage_ready(state, ctrl))
        return false;
    World *world = controls_world(state);
    if (!world || !world->runtime_actors)
        return false;
    const uint8_t rank = controls_skill_rank(state, SKILL_RAVAGE);
    const float range = PLAYER_RAVAGE_RANGE * skill_rank_mult(rank);
    const float r2 = range * range;
    const Actor *caster = controls_skill_caster(state);
    const uint32_t dmg = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_RAVAGE, PLAYER_RAVAGE_DAMAGE));
    int hits = 0;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || a->is_controlled)
            continue;
        const float dx = (float)a->x - state->player_world_x;
        const float dy = (float)a->y - state->player_world_y;
        const float dz = (float)a->z - state->player_world_z;
        if (dx * dx + dy * dy + dz * dz > r2)
            continue;
        spell_hit_actor(state, a, dmg);
        a->chill = 255;
        a->freeze_ttl = PLAYER_RAVAGE_FREEZE_S * skill_rank_mult(rank);
        particle_effects_spawn_blast(world, (float)a->x, (float)a->y, (float)a->z + 0.4f, true, 1.8f);
        hits++;
    }
    if (hits == 0)
        return false;
    spell_spend(state, ctrl, PLAYER_RAVAGE_STAMINA_COST, &ctrl->ravage_ready_at_ms,
                PLAYER_RAVAGE_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Ravage (%d)", hits);
    play_skill_cast_sound(SKILL_RAVAGE);
    return true;
}

bool player_controls_reapers_scythe_ready(GameState *state, const PlayerControls *ctrl)
{
    return spell_skill_ready(state, ctrl, SKILL_REAPERS_SCYTHE,
                             ctrl ? ctrl->reapers_scythe_ready_at_ms : 0,
                             PLAYER_REAPERS_SCYTHE_STAMINA_COST);
}

bool player_controls_cast_reapers_scythe(GameState *state, PlayerControls *ctrl)
{
    if (!player_controls_reapers_scythe_ready(state, ctrl))
        return false;
    Actor *target = nearest_living_actor(state, PLAYER_REAPERS_SCYTHE_RANGE *
                                                    skill_rank_mult(controls_skill_rank(
                                                        state, SKILL_REAPERS_SCYTHE)),
                                         NULL);
    if (!target)
        return false;
    World *world = controls_world(state);
    const Actor *caster = controls_skill_caster(state);
    uint32_t dmg = actor_skill_scale_u32(
        caster, controls_rank_scale_u32(state, SKILL_REAPERS_SCYTHE, PLAYER_REAPERS_SCYTHE_DAMAGE));
    const uint32_t max_hp = actor_max_health(target);
    if (max_hp > 0 && (target->health * 100u) / max_hp < PLAYER_REAPERS_SCYTHE_EXECUTE_PCT)
        dmg = (dmg * 3u) / 2u;
    spell_hit_actor(state, target, dmg);
    if (world)
        particle_effects_spawn_blast(world, (float)target->x, (float)target->y,
                                     (float)target->z + 0.9f, false, 2.2f);
    spell_spend(state, ctrl, PLAYER_REAPERS_SCYTHE_STAMINA_COST, &ctrl->reapers_scythe_ready_at_ms,
                PLAYER_REAPERS_SCYTHE_COOLDOWN_MS);
    snprintf(state->status_message, sizeof(state->status_message), "Reaper's Scythe!");
    play_skill_cast_sound(SKILL_REAPERS_SCYTHE);
    return true;
}

static Actor *nearest_corpse_actor(GameState *state, float max_range, float *out_dist)
{
    World *world = skill_world(state);
    if (!state || !world || !world->runtime_actors)
        return NULL;

    Actor *best = NULL;
    float best_d2 = max_range * max_range;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health != 0 || a->is_controlled)
            continue;
        float dx = (float)a->x - state->player_world_x;
        float dy = (float)a->y - state->player_world_y;
        float dz = (float)a->z - state->player_world_z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= best_d2)
        {
            best_d2 = d2;
            best = a;
        }
    }
    if (best && out_dist)
        *out_dist = sqrtf(best_d2);
    return best;
}

Actor *player_controls_nearest_corpse(GameState *state, float *out_dist)
{
    return nearest_corpse_actor(state, PLAYER_LOOT_RANGE, out_dist);
}

bool player_controls_loot_in_range(GameState *state)
{
    if (!state || !state->game_started)
        return false;
    return nearest_corpse_actor(state, PLAYER_LOOT_RANGE, NULL) != NULL;
}

static int corpse_transfer_loot(Actor *corpse, Actor *looter, char *status, size_t status_sz)
{
    if (!corpse || !looter)
        return 0;

    // Ensure the carcass table has been rolled (in case death did not fire yet).
    mob_actor_generate_corpse_loot(corpse);

    int moved = 0;

    for (uint16_t i = 0; i < corpse->inventory.capacity; i++)
    {
        ItemStack *s = &corpse->inventory.slots[i];
        if (s->id == ITEM_NONE || s->pieces == 0)
            continue;
        ItemStack take = *s;
        if (!inventory_add_stack(&looter->inventory, &take))
        {
            if (status && status_sz)
                snprintf(status, status_sz, "Inventory full");
            return moved;
        }
        memset(s, 0, sizeof(*s));
        moved++;
    }

    Equipment *eq = mob_actor_equipment(corpse);
    if (eq)
    {
        for (int slot = 0; slot < EQUIP_SLOT_COUNT; slot++)
        {
            ItemId id = eq->slots[slot];
            if (id == ITEM_NONE)
                continue;
            ItemStack gear = equipment_slot_to_stack(id, corpse->id * 31u + (uint32_t)slot * 17u);
            if (!inventory_add_stack(&looter->inventory, &gear))
            {
                if (status && status_sz)
                    snprintf(status, status_sz, "Inventory full");
                return moved;
            }
            eq->slots[slot] = ITEM_NONE;
            moved++;
        }
    }

    if (status && status_sz)
    {
        if (moved > 0)
            snprintf(status, status_sz, "Looted corpse");
        else
            snprintf(status, status_sz, "Nothing to loot");
    }
    return moved;
}

static void loot_clear_held(GameState *state)
{
    if (!state)
        return;
    memset(&state->loot_held, 0, sizeof(state->loot_held));
    state->loot_held_src = 0;
    state->loot_held_index = -1;
}

void player_controls_loot_cancel_drag(GameState *state)
{
    if (!state || state->loot_held.id == ITEM_NONE || state->loot_held.pieces == 0)
    {
        loot_clear_held(state);
        return;
    }

    ItemStack held = state->loot_held;
    int src = state->loot_held_src;
    int index = state->loot_held_index;
    loot_clear_held(state);

    Actor *corpse = state->loot_corpse;
    Actor *player = state->player;
    if (src == 1 && corpse && index >= 0 && index < (int)corpse->inventory.capacity)
    {
        ItemStack *slot = &corpse->inventory.slots[index];
        if (slot->id == ITEM_NONE || slot->pieces == 0)
            *slot = held;
        else
            inventory_add_stack(&corpse->inventory, &held);
        return;
    }
    if (src == 2 && corpse)
    {
        Equipment *eq = mob_actor_equipment(corpse);
        if (eq && index >= 0 && index < EQUIP_SLOT_COUNT && eq->slots[index] == ITEM_NONE)
        {
            eq->slots[index] = held.id;
            return;
        }
        inventory_add_stack(&corpse->inventory, &held);
        return;
    }
    if (src == 3 && player && index >= 0 && index < (int)player->inventory.capacity)
    {
        ItemStack *slot = &player->inventory.slots[index];
        if (slot->id == ITEM_NONE || slot->pieces == 0)
            *slot = held;
        else
            inventory_add_stack(&player->inventory, &held);
        return;
    }
    if (player)
        inventory_add_stack(&player->inventory, &held);
    else if (corpse)
        inventory_add_stack(&corpse->inventory, &held);
}

bool player_controls_drop_inventory_slot(GameState *state, int slot)
{
    if (!state || !state->player || !state->current_world)
        return false;
    Inventory *inv = &state->player->inventory;
    if (slot < 0 || slot >= (int)inv->capacity)
        return false;

    ItemStack *bag = &inv->slots[slot];
    if (bag->id == ITEM_NONE || bag->pieces == 0)
        return false;

    ItemStack stack = *bag;
    const uint32_t seed =
        (uint32_t)slot * 2654435761u ^ (uint32_t)(state->player_world_x * 16.0f) ^
        (uint32_t)(state->player_world_y * 16.0f);

    if (!debris_spawn_item_stack(&state->debris, state->current_world,
                                 state->player_world_x, state->player_world_y,
                                 state->player_world_z + 0.4f, &stack, seed))
    {
        snprintf(state->status_message, sizeof(state->status_message), "Cannot drop here");
        return false;
    }

    memset(bag, 0, sizeof(*bag));
    snprintf(state->status_message, sizeof(state->status_message), "Dropped %s",
             item_name(stack.id));
    return true;
}

void player_controls_close_loot(GameState *state)
{
    if (!state)
        return;
    player_controls_loot_cancel_drag(state);
    state->loot_corpse = NULL;
    if (state->current_screen == GAME_SCREEN_LOOT)
        state->current_screen = GAME_SCREEN_WORLD;
}

int player_controls_loot_all(GameState *state)
{
    if (!state || !state->player || !state->loot_corpse)
        return 0;
    player_controls_loot_cancel_drag(state);
    int moved = corpse_transfer_loot(state->loot_corpse, state->player,
                                     state->status_message, sizeof(state->status_message));
    // Close when the carcass is empty (or bag full mid-transfer left leftovers).
    bool empty = true;
    Actor *corpse = state->loot_corpse;
    for (uint16_t i = 0; i < corpse->inventory.capacity; i++)
    {
        if (corpse->inventory.slots[i].id != ITEM_NONE && corpse->inventory.slots[i].pieces > 0)
        {
            empty = false;
            break;
        }
    }
    Equipment *eq = mob_actor_equipment(corpse);
    if (eq)
    {
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++)
        {
            if (eq->slots[s] != ITEM_NONE)
            {
                empty = false;
                break;
            }
        }
    }
    if (empty)
        player_controls_close_loot(state);
    return moved;
}

void player_controls_update_loot(GameState *state)
{
    if (!state || state->current_screen != GAME_SCREEN_LOOT)
        return;
    Actor *corpse = state->loot_corpse;
    if (!corpse || !corpse->is_active || corpse->health != 0)
    {
        snprintf(state->status_message, sizeof(state->status_message), "Corpse gone");
        player_controls_close_loot(state);
        return;
    }
    float dx = (float)corpse->x - state->player_world_x;
    float dy = (float)corpse->y - state->player_world_y;
    float dz = (float)corpse->z - state->player_world_z;
    float r = PLAYER_LOOT_RANGE + 0.75f;
    if (dx * dx + dy * dy + dz * dz > r * r)
    {
        snprintf(state->status_message, sizeof(state->status_message), "Too far to loot");
        player_controls_close_loot(state);
    }
}

bool player_controls_loot_pick(GameState *state, int src, int index)
{
    if (!state || !state->player)
        return false;
    if (state->loot_held.id != ITEM_NONE && state->loot_held.pieces > 0)
        return false; // already holding

    if (src == 1)
    {
        Actor *corpse = state->loot_corpse;
        if (!corpse || index < 0 || index >= (int)corpse->inventory.capacity)
            return false;
        ItemStack *slot = &corpse->inventory.slots[index];
        if (slot->id == ITEM_NONE || slot->pieces == 0)
            return false;
        state->loot_held = *slot;
        memset(slot, 0, sizeof(*slot));
        state->loot_held_src = 1;
        state->loot_held_index = (int16_t)index;
        return true;
    }
    if (src == 2)
    {
        Actor *corpse = state->loot_corpse;
        Equipment *eq = corpse ? mob_actor_equipment(corpse) : NULL;
        if (!eq || index < 0 || index >= EQUIP_SLOT_COUNT)
            return false;
        ItemId id = eq->slots[index];
        if (id == ITEM_NONE)
            return false;
        state->loot_held = equipment_slot_to_stack(id, corpse->id * 31u + (uint32_t)index * 17u);
        eq->slots[index] = ITEM_NONE;
        state->loot_held_src = 2;
        state->loot_held_index = (int16_t)index;
        return true;
    }
    if (src == 3)
    {
        Actor *player = state->player;
        if (index < 0 || index >= (int)player->inventory.capacity)
            return false;
        ItemStack *slot = &player->inventory.slots[index];
        if (slot->id == ITEM_NONE || slot->pieces == 0)
            return false;
        state->loot_held = *slot;
        memset(slot, 0, sizeof(*slot));
        state->loot_held_src = 3;
        state->loot_held_index = (int16_t)index;
        return true;
    }
    return false;
}

bool player_controls_loot_drop_on_player(GameState *state, int dest_index)
{
    if (!state || !state->player || state->loot_held.id == ITEM_NONE || state->loot_held.pieces == 0)
        return false;

    Inventory *inv = &state->player->inventory;
    ItemStack held = state->loot_held;

    if (dest_index >= 0 && dest_index < (int)inv->capacity)
    {
        ItemStack *slot = &inv->slots[dest_index];
        if (slot->id == ITEM_NONE || slot->pieces == 0)
        {
            *slot = held;
            loot_clear_held(state);
            return true;
        }
        // Swap with occupied slot.
        ItemStack tmp = *slot;
        *slot = held;
        state->loot_held = tmp;
        // Source becomes "player bag" at dest for cancel semantics.
        state->loot_held_src = 3;
        state->loot_held_index = (int16_t)dest_index;
        return true;
    }

    if (!inventory_add_stack(inv, &held))
    {
        snprintf(state->status_message, sizeof(state->status_message), "Inventory full");
        return false;
    }
    loot_clear_held(state);
    return true;
}

bool player_controls_loot_drop_on_corpse(GameState *state, int dest_index)
{
    if (!state || !state->loot_corpse || state->loot_held.id == ITEM_NONE || state->loot_held.pieces == 0)
        return false;

    Inventory *inv = &state->loot_corpse->inventory;
    ItemStack held = state->loot_held;

    if (dest_index >= 0 && dest_index < (int)inv->capacity)
    {
        ItemStack *slot = &inv->slots[dest_index];
        if (slot->id == ITEM_NONE || slot->pieces == 0)
        {
            *slot = held;
            loot_clear_held(state);
            return true;
        }
        ItemStack tmp = *slot;
        *slot = held;
        state->loot_held = tmp;
        state->loot_held_src = 1;
        state->loot_held_index = (int16_t)dest_index;
        return true;
    }

    if (!inventory_add_stack(inv, &held))
        return false;
    loot_clear_held(state);
    return true;
}

bool player_controls_try_loot(GameState *state)
{
    if (!state || !state->game_started || !state->player)
        return false;

    if (state->current_screen == GAME_SCREEN_LOOT)
    {
        player_controls_close_loot(state);
        return true;
    }

    Actor *corpse = nearest_corpse_actor(state, PLAYER_LOOT_RANGE, NULL);
    if (!corpse)
    {
        snprintf(state->status_message, sizeof(state->status_message), "No corpse nearby");
        return false;
    }

    mob_actor_generate_corpse_loot(corpse);
    player_controls_loot_cancel_drag(state);
    state->loot_corpse = corpse;
    state->current_screen = GAME_SCREEN_LOOT;
    snprintf(state->status_message, sizeof(state->status_message), "Looting...");
    return true;
}

bool player_controls_try_craft(GameState *state)
{
    if (!state || !state->game_started || !state->player || !state->current_world)
        return false;

    if (state->current_screen == GAME_SCREEN_CRAFT)
    {
        player_controls_close_craft(state);
        return true;
    }

    CraftStation station = CRAFT_STATION_COUNT;
    if (!craft_find_nearby_station(state->current_world,
                                   state->player_world_x, state->player_world_y,
                                   state->player_world_z, PLAYER_LOOT_RANGE,
                                   &station, NULL, NULL, NULL))
    {
        snprintf(state->status_message, sizeof(state->status_message),
                 "No crafting station nearby");
        return false;
    }
    return game_state_open_craft(state, station, false);
}

bool player_controls_open_recipe_book(GameState *state)
{
    if (!state || !state->game_started || !state->player)
        return false;
    if (state->current_screen == GAME_SCREEN_CRAFT && state->craft.book_mode)
    {
        player_controls_close_craft(state);
        return true;
    }
    return game_state_open_craft(state, CRAFT_STATION_HAND, true);
}

void player_controls_close_craft(GameState *state)
{
    if (!state)
        return;
    game_state_close_craft(state);
}


bool player_controls_dominate_in_range(GameState *state)
{
    if (!state || !state->game_started)
        return false;
    if (player_controls_is_dominating(state))
        return true;
    return nearest_living_actor(state, PLAYER_DOMINATE_RANGE, NULL) != NULL;
}

bool player_controls_dominate_ready(GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl || !state->game_started)
        return false;
    if (!skill_world(state))
        return false;
    // Release is always available while inhabiting; cooldown only gates a fresh possess.
    if (player_controls_is_dominating(state))
        return true;
    if (skill_cooldown_blocking(ctrl->dominate_ready_at_ms))
        return false;
    if (controls_get_stamina(state) < PLAYER_DOMINATE_STAMINA_COST)
        return false;
    return nearest_living_actor(state, PLAYER_DOMINATE_RANGE, NULL) != NULL;
}

bool player_controls_try_dominate(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl)
        return false;

    if (player_controls_is_dominating(state))
    {
        player_controls_release_dominate(state);
        stamp_skill_cooldown(&ctrl->dominate_ready_at_ms, PLAYER_DOMINATE_COOLDOWN_MS);
        return true;
    }

    if (!player_controls_dominate_ready(state, ctrl))
    {
        if (skill_cooldown_blocking(ctrl->dominate_ready_at_ms))
            snprintf(state->status_message, sizeof(state->status_message), "Dominate not ready");
        else if (controls_get_stamina(state) < PLAYER_DOMINATE_STAMINA_COST)
            snprintf(state->status_message, sizeof(state->status_message), "Too weary to dominate");
        else
            snprintf(state->status_message, sizeof(state->status_message),
                     "Approach a living body to dominate it");
        return false;
    }

    Actor *target = nearest_living_actor(state, PLAYER_DOMINATE_RANGE, NULL);
    if (!target)
        return false;

    controls_set_stamina(state, controls_get_stamina(state) - PLAYER_DOMINATE_STAMINA_COST);
    stamp_skill_cooldown(&ctrl->dominate_ready_at_ms, PLAYER_DOMINATE_COOLDOWN_MS);

    target->is_controlled = true;
    target->velocity_x = 0.0;
    target->velocity_y = 0.0;
    target->velocity_z = 0.0;
    if (target->extra_data)
    {
        MobActor *mob = (MobActor *)target->extra_data;
        mob->base.is_controlled = true;
        mob->base.velocity_x = 0.0;
        mob->base.velocity_y = 0.0;
        mob->ai_state.has_target = false;
    }

    state->dominated_actor_id = target->id;
    state->player_world_x = (float)target->x;
    state->player_world_y = (float)target->y;
    state->player_world_z = (float)target->z;
    // Leave spirit hover so the body uses gravity / bird Fly instead of G-key float.
    player_controls_set_spirit_hover(state, false);
    game_state_sync_positions(state);
    play_skill_cast_sound(SKILL_DOMINATE);
    snprintf(state->status_message, sizeof(state->status_message),
             "You inhabit the %s", target->name[0] ? target->name : "body");

    if (mob_actor_is_bird(target))
    {
        skill_refresh_hotbar(state, ctrl);
        bool airborne = !game_state_player_is_grounded(state);
        if (target->is_flying || airborne)
        {
            ctrl->fly_active = true;
            ctrl->fly_was_airborne = true;
            ctrl->auto_glide_timer = 0.0f;
            ctrl->auto_glide_suppress = false;
        }
        else
        {
            ctrl->fly_active = false;
            ctrl->fly_was_airborne = false;
            ctrl->auto_glide_timer = 0.0f;
            ctrl->auto_glide_suppress = false;
        }
    }
    else
    {
        ctrl->fly_active = false;
        ctrl->fly_was_airborne = false;
        ctrl->auto_glide_timer = 0.0f;
        ctrl->auto_glide_suppress = false;
        skill_refresh_hotbar(state, ctrl);
    }
    return true;
}

bool player_controls_talk_in_range(GameState *state)
{
    if (!state || !state->game_started)
        return false;
    if (dialogue_active(&state->dialogue))
        return true;
    return nearest_living_actor(state, PLAYER_TALK_RANGE, NULL) != NULL;
}

bool player_controls_talk_ready(GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl || !state->game_started)
        return false;
    if (!skill_world(state))
        return false;
    if (dialogue_active(&state->dialogue))
        return true;
    if (skill_cooldown_blocking(ctrl->talk_ready_at_ms))
        return false;
    return nearest_living_actor(state, PLAYER_TALK_RANGE, NULL) != NULL;
}

bool player_controls_try_talk(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl)
        return false;

    if (player_controls_town_portal_charging(ctrl))
    {
        snprintf(state->status_message, sizeof(state->status_message),
                 "Keep holding T to teleport");
        return false;
    }

    Actor *target = nearest_living_actor(state, PLAYER_TALK_RANGE, NULL);

    if (dialogue_active(&state->dialogue))
    {
        if (target && target->id == dialogue_speaker_id(&state->dialogue))
        {
            dialogue_advance(&state->dialogue);
            return true;
        }
        if (!target)
        {
            dialogue_close(&state->dialogue);
            snprintf(state->status_message, sizeof(state->status_message),
                     "Approach someone to talk");
            return false;
        }
        // A different nearby speaker: start a new conversation.
    }

    if (!player_controls_talk_ready(state, ctrl))
    {
        if (skill_cooldown_blocking(ctrl->talk_ready_at_ms))
            snprintf(state->status_message, sizeof(state->status_message), "Talk not ready");
        else
            snprintf(state->status_message, sizeof(state->status_message),
                     "Approach someone to talk");
        return false;
    }

    if (!target)
        return false;

    const Chronicle *chron =
        state->universe.chronicle_ready ? &state->universe.chronicle : NULL;
    if (!dialogue_begin_conversation(&state->dialogue, target, skill_world(state), chron))
        return false;

    stamp_skill_cooldown(&ctrl->talk_ready_at_ms, PLAYER_TALK_COOLDOWN_MS);
    player_controls_update_facing_from_world(state, ctrl, (float)target->x, (float)target->y);
    play_skill_cast_sound(SKILL_TALK);
    snprintf(state->status_message, sizeof(state->status_message),
             "You speak with %s", target->name[0] ? target->name : "someone");
    return true;
}

bool player_controls_inhabited_can_fly(GameState *state)
{
    if (!state || !state->game_started)
        return false;
    Actor *body = player_controls_dominated_actor(state);
    return mob_actor_is_bird(body);
}

bool player_controls_fly_active(const PlayerControls *ctrl)
{
    return ctrl && ctrl->fly_active;
}

void player_controls_update_auto_glide(GameState *state, PlayerControls *ctrl, double dt)
{
    if (!state || !ctrl || dt <= 0.0)
        return;

    // A deliberate fold is freefall until the body finds ground again.
    if (ctrl->auto_glide_suppress)
    {
        ctrl->auto_glide_timer = 0.0f;
        if (controls_is_grounded(state))
            ctrl->auto_glide_suppress = false;
        return;
    }

    // Spirit G-hover and bodies that cannot fly never auto-open wings.
    if (state->player_flying || (state->player && state->player->is_flying) ||
        !player_controls_inhabited_can_fly(state) || ctrl->fly_active)
    {
        ctrl->auto_glide_timer = 0.0f;
        return;
    }

    if (controls_is_grounded(state) || ctrl->velocity_z >= 0.0f)
    {
        ctrl->auto_glide_timer = 0.0f;
        return;
    }

    ctrl->auto_glide_timer += (float)dt;
    if (ctrl->auto_glide_timer < PLAYER_AUTO_GLIDE_DELAY_S)
        return;

    ctrl->auto_glide_timer = 0.0f;
    ctrl->fly_active = true;
    ctrl->fly_was_airborne = true;
    // No takeoff boost and no cooldown stamp: this is catching a fall, not launching.
    snprintf(state->status_message, sizeof(state->status_message), "Wings catch the air");
}

float player_controls_inhabited_lift(GameState *state)
{
    Actor *body = player_controls_dominated_actor(state);
    if (mob_actor_is_bird(body))
        return mob_actor_bird_lift(body);
    return 0.0f;
}

float player_controls_inhabited_glide_speed(GameState *state)
{
    Actor *body = player_controls_dominated_actor(state);
    if (mob_actor_is_bird(body))
        return mob_actor_bird_glide_speed(body);
    return 4.0f;
}

float player_controls_inhabited_bank_rate(GameState *state)
{
    Actor *body = player_controls_dominated_actor(state);
    if (mob_actor_is_bird(body))
        return mob_actor_bird_bank_rate(body);
    return PLAYER_BANK_RATE_RAD;
}

bool player_controls_fly_ready(GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl || !player_controls_inhabited_can_fly(state))
        return false;
    if (ctrl->fly_active)
        return true;
    if (skill_cooldown_blocking(ctrl->fly_ready_at_ms))
        return false;
    return true;
}

bool player_controls_try_fly(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl)
        return false;

    if (!player_controls_inhabited_can_fly(state))
    {
        snprintf(state->status_message, sizeof(state->status_message),
                 "This body cannot fly");
        return false;
    }

    if (ctrl->fly_active)
    {
        ctrl->fly_active = false;
        ctrl->auto_glide_timer = 0.0f;
        // Manual fold: allow a true freefall until the next landing.
        ctrl->auto_glide_suppress = true;
        stamp_skill_cooldown(&ctrl->fly_ready_at_ms, PLAYER_FLY_COOLDOWN_MS);
        snprintf(state->status_message, sizeof(state->status_message), "Wings fold");
        return true;
    }

    if (skill_cooldown_blocking(ctrl->fly_ready_at_ms))
    {
        snprintf(state->status_message, sizeof(state->status_message), "Fly not ready");
        return false;
    }

    ctrl->fly_active = true;
    ctrl->auto_glide_suppress = false;
    stamp_skill_cooldown(&ctrl->fly_ready_at_ms, PLAYER_FLY_COOLDOWN_MS);
    play_skill_cast_sound(SKILL_FLY);

    if (controls_is_grounded(state) && ctrl->velocity_z <= 0.05f)
    {
        Actor *body = player_controls_dominated_actor(state);
        const BirdStats *stats = mob_bird_stats(mob_actor_bird_kind(body));
        ctrl->velocity_z = stats->takeoff_vz;
        ctrl->fly_was_airborne = false;
        snprintf(state->status_message, sizeof(state->status_message), "You take wing");
    }
    else
    {
        ctrl->fly_was_airborne = true;
        snprintf(state->status_message, sizeof(state->status_message), "Wings catch the air");
    }
    return true;
}

bool player_controls_in_flight(GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl)
        return false;
    // Free spirit hover floats without banking. Only an inhabited bird under Fly rolls.
    if (player_controls_is_free_spirit(state))
        return false;
    if (ctrl->fly_active && !controls_is_grounded(state))
        return true;
    return false;
}

void player_controls_apply_bank(GameState *state, PlayerControls *ctrl, double dt)
{
    if (!state || !ctrl || dt <= 0.0)
        return;

    // Spirit hover never banks — pin roll flat.
    if (player_controls_is_free_spirit(state))
    {
        ctrl->roll = 0.0f;
        return;
    }

    // On the ground the wings are folded: kill any leftover bank and ignore Q/E.
    if (!player_controls_in_flight(state, ctrl))
    {
        if (ctrl->roll == 0.0f)
            return;
        float level = PLAYER_BANK_LEVEL_RATE_RAD * (float)dt;
        if (fabsf(ctrl->roll) <= level)
            ctrl->roll = 0.0f;
        else
            ctrl->roll += (ctrl->roll > 0.0f ? -level : level);
        return;
    }

    float target = 0.0f;
    if (ctrl->bank_left)
        target -= PLAYER_MAX_BANK_RAD;
    if (ctrl->bank_right)
        target += PLAYER_MAX_BANK_RAD;
    if (target > PLAYER_MAX_BANK_RAD)
        target = PLAYER_MAX_BANK_RAD;
    if (target < -PLAYER_MAX_BANK_RAD)
        target = -PLAYER_MAX_BANK_RAD;

    float rate = (target == 0.0f) ? PLAYER_BANK_LEVEL_RATE_RAD
                                  : player_controls_inhabited_bank_rate(state);
    float diff = target - ctrl->roll;
    float step = rate * (float)dt;
    if (fabsf(diff) <= step)
        ctrl->roll = target;
    else
        ctrl->roll += (diff > 0.0f ? step : -step);

    // Coordinated turn: a banked wing produces yaw. Positive roll (right wing down) turns
    // heading left in world yaw (increasing atan2), matching how the crow's glide reads on screen.
    if (fabsf(ctrl->roll) > 1e-4f)
    {
        float yaw_delta = ctrl->roll * PLAYER_BANK_TURN_PER_ROLL * (float)dt;
        ctrl->facing_yaw = wrap_angle(ctrl->facing_yaw + yaw_delta);
        ctrl->aim_yaw = wrap_angle(ctrl->aim_yaw + yaw_delta);
    }
}

void player_controls_cancel_town_portal(GameState *state, PlayerControls *ctrl)
{
    if (!ctrl)
        return;
    ctrl->teleport_charging = false;
    ctrl->teleport_charge_start_ms = 0;
    if (state && dialogue_active(&state->dialogue) && dialogue_speaker_id(&state->dialogue) == 0)
        dialogue_close(&state->dialogue);
}

bool player_controls_town_portal_charging(const PlayerControls *ctrl)
{
    return ctrl && ctrl->teleport_charging;
}

float player_controls_town_portal_charge_progress(const PlayerControls *ctrl)
{
    if (!ctrl || !ctrl->teleport_charging)
        return 0.0f;
    uint32_t elapsed = SDL_GetTicks() - ctrl->teleport_charge_start_ms;
    float progress = (float)elapsed / (float)PLAYER_TOWN_PORTAL_CHARGE_MS;
    if (progress < 0.0f)
        return 0.0f;
    if (progress > 1.0f)
        return 1.0f;
    return progress;
}

static World *town_portal_home_world(GameState *state)
{
    if (!state)
        return NULL;
    if (state->game_worlds && state->game_worlds->home_world)
        return state->game_worlds->home_world;
    return universe_get(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z);
}

bool player_controls_town_portal_ready(GameState *state, const PlayerControls *ctrl)
{
    if (!state || !ctrl || !state->game_started)
        return false;
    if (ctrl->teleport_charging)
        return true;
    if (skill_cooldown_blocking(ctrl->town_portal_ready_at_ms))
        return false;
    if (controls_get_stamina(state) < PLAYER_TOWN_PORTAL_STAMINA_COST)
        return false;
    return town_portal_home_world(state) != NULL;
}

static bool player_controls_complete_town_portal(GameState *state, PlayerControls *ctrl)
{
    if (!state || !ctrl)
        return false;

    ctrl->teleport_charging = false;
    ctrl->teleport_charge_start_ms = 0;
    if (dialogue_active(&state->dialogue) && dialogue_speaker_id(&state->dialogue) == 0)
        dialogue_close(&state->dialogue);

    if (!town_portal_home_world(state))
    {
        snprintf(state->status_message, sizeof(state->status_message), "Cannot return home");
        return false;
    }

    if (controls_get_stamina(state) < PLAYER_TOWN_PORTAL_STAMINA_COST)
    {
        snprintf(state->status_message, sizeof(state->status_message), "Too weary to teleport");
        return false;
    }

    if (!game_state_teleport_home(state))
    {
        snprintf(state->status_message, sizeof(state->status_message), "Cannot return home");
        return false;
    }

    controls_set_stamina(state, controls_get_stamina(state) - PLAYER_TOWN_PORTAL_STAMINA_COST);
    stamp_skill_cooldown(&ctrl->town_portal_ready_at_ms, PLAYER_TOWN_PORTAL_COOLDOWN_MS);
    play_skill_cast_sound(SKILL_TOWN_PORTAL);
    snprintf(state->status_message, sizeof(state->status_message), "You return home");
    return true;
}

void player_controls_update_town_portal(GameState *state, PlayerControls *ctrl, bool key_held)
{
    if (!state || !ctrl)
        return;

    if (!state->game_started || state->current_screen != GAME_SCREEN_WORLD ||
        state->show_world_editor_modal || state->show_exit_prompt || state->show_name_input)
    {
        if (ctrl->teleport_charging)
            player_controls_cancel_town_portal(state, ctrl);
        return;
    }

    if (ctrl->teleport_charging)
    {
        if (!key_held)
        {
            player_controls_cancel_town_portal(state, ctrl);
            return;
        }
        if ((SDL_GetTicks() - ctrl->teleport_charge_start_ms) >= PLAYER_TOWN_PORTAL_CHARGE_MS)
            player_controls_complete_town_portal(state, ctrl);
        return;
    }

    if (!key_held)
        return;

    if (!player_controls_town_portal_ready(state, ctrl))
    {
        if (skill_cooldown_blocking(ctrl->town_portal_ready_at_ms))
            snprintf(state->status_message, sizeof(state->status_message),
                     "Town Portal not ready");
        else if (controls_get_stamina(state) < PLAYER_TOWN_PORTAL_STAMINA_COST)
            snprintf(state->status_message, sizeof(state->status_message),
                     "Too weary to teleport");
        else if (!town_portal_home_world(state))
            snprintf(state->status_message, sizeof(state->status_message),
                     "No home to return to");
        return;
    }

    ctrl->teleport_charging = true;
    ctrl->teleport_charge_start_ms = SDL_GetTicks();
    {
        static const char *lines[] = {"Teleporting home..."};
        dialogue_begin(&state->dialogue, 0, "Town Portal", lines, 1);
    }
}

void player_controls_hotbar_assign(PlayerControls *ctrl, int slot, SkillId skill)
{
    if (!ctrl || slot < 0 || slot >= PLAYER_HOTBAR_SLOTS)
        return;
    ctrl->hotbar[slot] = skill;
}

SkillId player_controls_hotbar_skill(const PlayerControls *ctrl, int slot)
{
    if (!ctrl || slot < 0 || slot >= PLAYER_HOTBAR_SLOTS)
        return SKILL_NONE;
    return ctrl->hotbar[slot];
}

const char *player_controls_skill_name(SkillId skill)
{
    return skill_name(skill);
}

bool player_controls_skill_ready(GameState *state, const PlayerControls *ctrl, SkillId skill)
{
    return skill_ready(state, ctrl, skill);
}

bool player_controls_use_hotbar_slot(GameState *state, PlayerControls *ctrl, int slot)
{
    if (!state || !ctrl)
        return false;
    SkillId skill = player_controls_hotbar_skill(ctrl, slot);
    if (skill == SKILL_NONE)
    {
        snprintf(state->status_message, sizeof(state->status_message), "No skill in slot %d",
                 slot + 1);
        return false;
    }
    return skill_use(state, ctrl, skill);
}
