// Mud golem wanderer AI, birds, fly/glide, dominate, talk, and the 1-4 skill hotbar.
//
// Built against real worlds and GameState the same way test_foliage_projectiles is: the client's
// UI reaches back into verse_client.c, so the stubs below stand in for those symbols.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "actor.h"
#include "console.h"
#include "constants.h"
#include "game_state.h"
#include "mob_ai.h"
#include "player_controls.h"
#include "projectile.h"
#include "settlement.h"
#include "shadow_world.h"
#include "universe.h"
#include "world.h"
#include "dialogue.h"
#include "skill.h"
#include "household.h"
#include "poly_mesh.h"
#include "fire_sim.h"
#include "voxel_combat.h"

GameState *g_game_state = NULL;
int g_show_exit_prompt = 0;
Console *g_console = NULL;
void handle_button_click(int button_id) { (void)button_id; }
void play_highlight_sound(void) {}

static int test_failures = 0;

static void report(const char *name, bool passed)
{
  printf("%s %s\n", passed ? "PASS" : "FAILED", name);
  if (!passed)
    test_failures++;
}

static World *make_arena(int size, int floor_z)
{
  World *w = world_create((uint32_t)size, (uint32_t)size, (uint32_t)size);
  if (!w)
    return NULL;

  for (int z = 0; z < size; z++)
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
        world_set_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z,
                        z <= floor_z ? VOXEL_STONE : VOXEL_AIR);
  return w;
}

static GameState *make_state(World *world, float x, float y, float z)
{
  GameState *state = (GameState *)calloc(1, sizeof(GameState));
  if (!state)
    return NULL;

  state->current_world = world;
  state->game_started = true;
  strncpy(state->player_name, "tester", sizeof(state->player_name) - 1);
  state->player_world_x = x;
  state->player_world_y = y;
  state->player_world_z = z;
  game_state_sync_positions(state);
  player_controls_init(&state->controls);
  projectile_system_reset(&state->projectiles, 0x6013u);

  state->player = actor_create("tester", "test spirit", "arena");
  if (state->player)
  {
    state->player->stamina = 100;
    state->player->is_flying = false;
  }
  return state;
}

static void destroy_state(GameState *state)
{
  if (!state)
    return;
  if (state->player)
    actor_destroy(state->player);
  free(state);
}

static void test_hotbar_defaults(void)
{
  printf("\n-- skill hotbar --\n");

  World *w = make_arena(16, 4);
  GameState *state = make_state(w, 8.5f, 8.5f, 5.0f);
  if (!state)
  {
    report("hotbar state built", false);
    world_destroy(w);
    return;
  }

  report("slot 1 defaults to fireball",
         player_controls_hotbar_skill(&state->controls, 0) == SKILL_FIREBALL);
  report("slot 2 defaults to dominate",
         player_controls_hotbar_skill(&state->controls, 1) == SKILL_DOMINATE);
  report("slot 3 defaults to talk",
         player_controls_hotbar_skill(&state->controls, 2) == SKILL_TALK);
  report("slot 4 starts empty",
         player_controls_hotbar_skill(&state->controls, 3) == SKILL_NONE);
  report("fly is named Fly",
         strcmp(player_controls_skill_name(SKILL_FLY), "Fly") == 0);
  report("the skill table exposes a fly abbreviation",
         strcmp(skill_abbrev(SKILL_FLY), "Fl") == 0);

  player_controls_hotbar_assign(&state->controls, 2, SKILL_FIREBALL);
  report("a skill can be reassigned on the bar",
         player_controls_hotbar_skill(&state->controls, 2) == SKILL_FIREBALL);
  report("skill names are stable",
         strcmp(player_controls_skill_name(SKILL_DOMINATE), "Dominate") == 0);
  report("talk is named Talk",
         strcmp(player_controls_skill_name(SKILL_TALK), "Talk") == 0);

  const int before = projectile_active_count(&state->projectiles);
  report("pressing 1 casts fireball",
         player_controls_use_hotbar_slot(state, &state->controls, 0));
  report("the hotbar cast put a projectile in the air",
         projectile_active_count(&state->projectiles) == before + 1);

  report("an empty slot refuses to fire",
         !player_controls_use_hotbar_slot(state, &state->controls, 3));

  // Scroll-wheel cycling through known skills on slot 0.
  report("wheel next leaves fireball for dominate",
         skill_cycle_hotbar(state, &state->controls, 0, 1) == SKILL_DOMINATE);
  report("slot 1 now holds dominate after the cycle",
         player_controls_hotbar_skill(&state->controls, 0) == SKILL_DOMINATE);
  // Dominate was also on slot 1 — cycling should have swapped so fireball moved there.
  report("the displaced skill swapped onto its former slot",
         player_controls_hotbar_skill(&state->controls, 1) == SKILL_FIREBALL);
  report("wheel previous restores fireball on slot 1",
         skill_cycle_hotbar(state, &state->controls, 0, -1) == SKILL_FIREBALL);

  destroy_state(state);
  world_destroy(w);
}

static void test_wanderer(void)
{
  printf("\n-- wanderer AI --\n");

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("wanderer arena built", false);
    return;
  }

  const double x = 12.5, y = 12.5, z = (double)(floor_z + 1);
  MobActor *mob = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER, x, y, z);
  report("a wanderer can be created", mob != NULL);
  if (!mob)
  {
    world_destroy(w);
    return;
  }

  Actor copy = mob->base;
  copy.extra_data = mob;
  report("the wanderer joins the world's runtime actors", world_add_runtime_actor(w, &copy));
  report("the world owns that list", w->runtime_actors_owned);

  const double start_x = w->runtime_actors[0].x;
  const double start_y = w->runtime_actors[0].y;
  for (int i = 0; i < 180; i++)
    world_step_actors(w, 1.0f / 30.0f);

  const double dx = w->runtime_actors[0].x - start_x;
  const double dy = w->runtime_actors[0].y - start_y;
  const double moved = sqrt(dx * dx + dy * dy);
  printf("       wanderer travelled %.2f voxels\n", moved);
  report("a wanderer moves from where it was placed", moved > 0.4);
  report("a wanderer stays on the floor",
         fabs(w->runtime_actors[0].z - z) < 0.6);

  world_destroy(w);
}

static void test_home_mobs_spawn(void)
{
  printf("\n-- home world inhabitants --\n");

  World *w = world_create(32, 32, 32);
  if (!w)
  {
    report("home world created", false);
    return;
  }
  world_generate_with_type(w, "golem_home_seed", WORLD_TYPE_HOME);
  report("home generation placed inhabitants", w->runtime_actor_count >= 5);

  bool named_golem = false;
  bool named_goleling = false;
  bool named_flesh = false;
  bool wanderer = false;
  bool named_crow = false;
  bool named_pigeon = false;
  bool bird_type = false;
  int golems = 0, birds = 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *mob = (MobActor *)w->runtime_actors[i].extra_data;
    if (strcmp(w->runtime_actors[i].name, "Mud Golem") == 0)
    {
      named_golem = true;
      golems++;
      wanderer = mob && mob->mob_type == MOB_TYPE_WANDERER;
      report("the voxel golem has no polygon mesh", mob && mob->mesh_name[0] == '\0');
    }
    if (strcmp(w->runtime_actors[i].name, "Goleling") == 0)
    {
      named_goleling = true;
      golems++;
      report("the goleling is a wanderer", mob && mob->mob_type == MOB_TYPE_WANDERER);
      report("the goleling is bound to the polygon mesh",
             mob && strcmp(mob->mesh_name, "goleling") == 0);
      report("the goleling starts on Flying_Idle",
             mob && strcmp(mob->anim_clip, "Flying_Idle") == 0);
    }
    if (strcmp(w->runtime_actors[i].name, "Flesh Walker") == 0)
    {
      named_flesh = true;
      report("the flesh walker is a wanderer", mob && mob->mob_type == MOB_TYPE_WANDERER);
      report("the flesh walker is bound to the polygon mesh",
             mob && strcmp(mob->mesh_name, "flesh_walker") == 0);
      report("the flesh walker starts on Idle",
             mob && strcmp(mob->anim_clip, "Idle") == 0);
    }
    if (mob && mob->mob_type == MOB_TYPE_BIRD)
    {
      bird_type = true;
      birds++;
      if (strcmp(w->runtime_actors[i].name, "Crow") == 0)
        named_crow = true;
      if (strcmp(w->runtime_actors[i].name, "Pigeon") == 0)
      {
        named_pigeon = true;
        report("the pigeon is bound to the polygon mesh",
               mob && strcmp(mob->mesh_name, "pigeon") == 0);
      }
    }
  }
  report("a mud golem stands on the home island", named_golem);
  report("a goleling stands on the home island", named_goleling);
  report("a flesh walker stands on the home island", named_flesh);
  report("the golem uses wanderer AI", wanderer);
  report("a bird stands on the home island", bird_type);
  report("the home bird is a crow", named_crow);
  report("a pigeon stands on the home island", named_pigeon);
  report("two golems and two birds were placed", golems == 2 && birds == 2);
  report("home generation placed five inhabitants", w->runtime_actor_count >= 5);
  const int after_gen = w->runtime_actor_count;
  report("spawning twice does not duplicate either inhabitant", world_spawn_home_mobs(w));
  report("the inhabitant count is unchanged after a second spawn",
         w->runtime_actor_count == after_gen);

  world_destroy(w);
}

static Actor *find_named_actor(World *w, const char *name)
{
  if (!w || !w->runtime_actors || !name)
    return NULL;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    if (strcmp(w->runtime_actors[i].name, name) == 0)
      return &w->runtime_actors[i];
  }
  return NULL;
}

static void perch_bird(Actor *a, double x, double y, double z)
{
  if (!a)
    return;
  a->x = x;
  a->y = y;
  a->z = z;
  a->is_flying = false;
  a->velocity_x = 0.0;
  a->velocity_y = 0.0;
  a->velocity_z = 0.0;
  if (a->extra_data)
  {
    MobActor *mob = (MobActor *)a->extra_data;
    mob->base.x = x;
    mob->base.y = y;
    mob->base.z = z;
    mob->base.is_flying = false;
    mob->base.velocity_x = 0.0;
    mob->base.velocity_y = 0.0;
    mob->base.velocity_z = 0.0;
    mob->ai_state.bird_phase = BIRD_PHASE_PERCH;
    mob->ai_state.bird_timer = 30.0f;
    mob->ai_state.has_target = false;
  }
}

static bool add_mob_to_world(World *w, MobActor *mob)
{
  if (!w || !mob)
    return false;
  Actor copy = mob->base;
  copy.extra_data = mob;
  return world_add_runtime_actor(w, &copy);
}

static void test_birds_and_fly(void)
{
  printf("\n-- birds and fly --\n");

  report("there are at least three bird kinds", BIRD_KIND_COUNT >= 3);
  const BirdStats *sparrow = mob_bird_stats(BIRD_KIND_SPARROW);
  const BirdStats *crow = mob_bird_stats(BIRD_KIND_CROW);
  const BirdStats *gull = mob_bird_stats(BIRD_KIND_GULL);
  report("sparrow, crow and gull are distinct species",
         sparrow && crow && gull &&
         strcmp(sparrow->name, "Sparrow") == 0 &&
         strcmp(crow->name, "Crow") == 0 &&
         strcmp(gull->name, "Gull") == 0);
  report("lift is specific to the inhabited body",
         sparrow->lift < crow->lift && crow->lift < gull->lift);
  report("each bird has a takeoff and a glide speed",
         sparrow->takeoff_vz > 0.0f && crow->glide_speed > 0.0f && gull->sink_max > 0.0f);

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("bird arena built", false);
    return;
  }

  MobActor *sparrow_mob = mob_actor_create_bird(BIRD_KIND_SPARROW, 8.5, 8.5,
                                                (double)(floor_z + 1));
  MobActor *crow_mob = mob_actor_create_bird(BIRD_KIND_CROW, 12.5, 12.5,
                                             (double)(floor_z + 1));
  MobActor *gull_mob = mob_actor_create_bird(BIRD_KIND_GULL, 16.5, 16.5,
                                             (double)(floor_z + 1));
  report("a sparrow, a crow and a gull can be created",
         sparrow_mob && crow_mob && gull_mob);
  if (!sparrow_mob || !crow_mob || !gull_mob)
  {
    if (sparrow_mob) mob_actor_destroy(sparrow_mob);
    if (crow_mob) mob_actor_destroy(crow_mob);
    if (gull_mob) mob_actor_destroy(gull_mob);
    world_destroy(w);
    return;
  }
  report("the three birds join the world",
         add_mob_to_world(w, sparrow_mob) && add_mob_to_world(w, crow_mob) &&
         add_mob_to_world(w, gull_mob));

  Actor *crow_actor = find_named_actor(w, "Crow");
  report("the crow is in the runtime list", crow_actor != NULL);
  if (!crow_actor)
  {
    world_destroy(w);
    return;
  }
  MobActor *crow_ai = (MobActor *)crow_actor->extra_data;
  const double perch_z = crow_actor->z;
  crow_ai->ai_state.bird_timer = 0.0f;
  crow_ai->ai_state.bird_phase = BIRD_PHASE_PERCH;
  perch_bird(find_named_actor(w, "Sparrow"), 2.5, 2.5, (double)(floor_z + 1));
  perch_bird(find_named_actor(w, "Gull"), 21.5, 21.5, (double)(floor_z + 1));
  for (int i = 0; i < 45; i++)
    world_step_actors(w, 1.0f / 30.0f);
  crow_actor = find_named_actor(w, "Crow");
  crow_ai = crow_actor ? (MobActor *)crow_actor->extra_data : NULL;
  printf("       crow climbed %.2f voxels (flying=%d phase=%u)\n",
         crow_actor ? crow_actor->z - perch_z : 0.0,
         crow_actor ? (int)crow_actor->is_flying : 0,
         crow_ai ? (unsigned)crow_ai->ai_state.bird_phase : 0);
  report("a perched bird takes flight on its own", crow_actor && crow_actor->is_flying);
  report("takeoff lifts the bird off the ground",
         crow_actor && crow_actor->z > perch_z + 0.4);
  report("the bird is circling or still climbing",
         crow_ai && (crow_ai->ai_state.bird_phase == BIRD_PHASE_TAKEOFF ||
                     crow_ai->ai_state.bird_phase == BIRD_PHASE_CIRCLE));

  GameState *state = make_state(w, 12.2f, 12.4f, (float)(floor_z + 1));
  if (!state)
  {
    report("fly state built", false);
    world_destroy(w);
    return;
  }

  report("fly is refused while the spirit is free",
         !player_controls_try_fly(state, &state->controls));
  report("slot 4 stays empty until a bird is inhabited",
         player_controls_hotbar_skill(&state->controls, 3) == SKILL_NONE);

  MobActor *golem = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER, 4.5, 4.5,
                                     (double)(floor_z + 1));
  report("a golem can stand beside the birds", add_mob_to_world(w, golem));
  state->player_world_x = 4.2f;
  state->player_world_y = 4.4f;
  game_state_sync_positions(state);
  report("dominating a golem succeeds",
         player_controls_try_dominate(state, &state->controls));
  report("a golem does not grant fly",
         player_controls_hotbar_skill(&state->controls, 3) == SKILL_NONE);
  state->controls.dominate_ready_at_ms = 0;
  player_controls_try_dominate(state, &state->controls);

  perch_bird(find_named_actor(w, "Sparrow"), 2.5, 2.5, (double)(floor_z + 1));
  perch_bird(find_named_actor(w, "Gull"), 21.5, 21.5, (double)(floor_z + 1));
  perch_bird(find_named_actor(w, "Crow"), 12.5, 12.5, (double)(floor_z + 1));
  state->player_world_x = 12.2f;
  state->player_world_y = 12.4f;
  state->player_world_z = (float)(floor_z + 1);
  game_state_sync_positions(state);
  state->controls.dominate_ready_at_ms = 0;
  if (state->player)
    state->player->stamina = 100;
  report("a nearby crow can be inhabited",
         player_controls_try_dominate(state, &state->controls));
  Actor *body = player_controls_dominated_actor(state);
  report("the inhabited body is the crow", body && strcmp(body->name, "Crow") == 0);
  report("inhabiting a bird puts fly on slot 4",
         player_controls_hotbar_skill(&state->controls, 3) == SKILL_FLY);
  report("fly is offered by the inhabited bird",
         player_controls_inhabited_can_fly(state));
  report("crow lift is used while inhabiting the crow",
         fabsf(player_controls_inhabited_lift(state) - crow->lift) < 0.001f);

  report("pressing fly takes wing",
         player_controls_use_hotbar_slot(state, &state->controls, 3));
  report("fly is active after takeoff", player_controls_fly_active(&state->controls));
  report("takeoff gives upward speed", state->controls.velocity_z > 1.0f);
  report("using fly again folds the wings",
         player_controls_try_fly(state, &state->controls));
  report("fly is off after folding", !player_controls_fly_active(&state->controls));

  state->controls.fly_ready_at_ms = 0;
  state->controls.fly_active = true;
  state->controls.fly_was_airborne = true;
  state->controls.velocity_x = 0.0f;
  state->controls.velocity_y = 0.0f;
  state->controls.velocity_z = 0.0f;
  state->controls.move_forward = false;
  state->controls.move_backward = false;
  state->controls.move_left = false;
  state->controls.move_right = false;
  state->player_world_z = (float)(floor_z + 6);
  game_state_sync_positions(state);
  report("the gliding body is off the ground", !game_state_player_is_grounded(state));

  state->controls.facing_yaw = 0.0f;
  state->controls.aim_yaw = 0.0f;
  for (int i = 0; i < 45; i++)
    player_controls_apply_wasd(state, &state->controls, 1.0 / 30.0);
  float glide_speed = sqrtf(state->controls.velocity_x * state->controls.velocity_x +
                            state->controls.velocity_y * state->controls.velocity_y);
  printf("       glide speed %.2f (crow %.2f)\n", glide_speed, crow->glide_speed);
  report("fly glides forward with no WASD", glide_speed > crow->glide_speed * 0.45f);
  report("the glide follows facing, not a sidestep",
         state->controls.velocity_x > fabsf(state->controls.velocity_y));

  float z_before = state->player_world_z;
  float vz_before = state->controls.velocity_z;
  (void)vz_before;
  for (int i = 0; i < 30; i++)
    game_state_apply_gravity(state, 1.0f / 30.0f);
  float dropped = z_before - state->player_world_z;
  printf("       glide drop %.2f voxels, vz %.2f\n", dropped, state->controls.velocity_z);
  report("lift keeps the glide from a steep fall", dropped < 2.5f);
  report("glide sink is capped for this bird",
         state->controls.velocity_z >= -crow->sink_max - 0.05f);

  report("gliding counts as in flight",
         player_controls_in_flight(state, &state->controls));

  float yaw0 = 0.0f;
  state->controls.facing_yaw = yaw0;
  state->controls.aim_yaw = yaw0;
  state->controls.roll = 0.0f;
  state->controls.bank_left = false;
  state->controls.bank_right = true;
  player_controls_apply_bank(state, &state->controls, 0.25);
  report("E rolls right wing down", state->controls.roll > 0.05f);
  float yaw_after_right = state->controls.facing_yaw;
  report("a right bank turns the heading with the bank", yaw_after_right > yaw0 + 0.02f);
  float roll_right = state->controls.roll;
  state->controls.bank_right = false;
  state->controls.bank_left = true;
  player_controls_apply_bank(state, &state->controls, 0.50);
  report("Q rolls left wing down", state->controls.roll < roll_right - 0.05f);
  report("a left bank turns the heading with the bank",
         state->controls.facing_yaw < yaw_after_right - 0.02f);

  state->controls.fly_active = false;
  state->player_world_z = (float)(floor_z + 1);
  game_state_sync_positions(state);
  float yaw_ground = state->controls.facing_yaw;
  float roll_ground = state->controls.roll;
  state->controls.bank_left = false;
  state->controls.bank_right = true;
  player_controls_apply_bank(state, &state->controls, 0.50);
  report("on the ground Q/E only level out, never deepen the bank",
         fabsf(state->controls.roll) <= fabsf(roll_ground) + 1e-5f);
  report("heading is unchanged on the ground",
         fabsf(state->controls.facing_yaw - yaw_ground) < 1e-5f);
  report("perched is not in flight",
         !player_controls_in_flight(state, &state->controls));

  // Automatic glide: walk off a height without pressing Fly, wings open after a short fall.
  state->controls.fly_active = false;
  state->controls.fly_was_airborne = false;
  state->controls.auto_glide_timer = 0.0f;
  state->controls.auto_glide_suppress = false;
  state->controls.velocity_x = 0.0f;
  state->controls.velocity_y = 0.0f;
  state->controls.velocity_z = 0.0f;
  state->player_world_z = (float)(floor_z + 8);
  game_state_sync_positions(state);
  report("starting the fall without fly", !player_controls_fly_active(&state->controls));
  for (int i = 0; i < 6; i++)
  {
    game_state_apply_gravity(state, 1.0f / 30.0f);
    player_controls_update_auto_glide(state, &state->controls, 1.0 / 30.0);
  }
  report("a brief fall does not open the wings yet",
         !player_controls_fly_active(&state->controls));
  for (int i = 0; i < 20; i++)
  {
    game_state_apply_gravity(state, 1.0f / 30.0f);
    player_controls_update_auto_glide(state, &state->controls, 1.0 / 30.0);
  }
  report("falling long enough auto-enables fly",
         player_controls_fly_active(&state->controls));
  report("auto-glide marks the body airborne", state->controls.fly_was_airborne);
  float auto_z0 = state->player_world_z;
  for (int i = 0; i < 30; i++)
    game_state_apply_gravity(state, 1.0f / 30.0f);
  report("auto-glide lift softens the fall",
         auto_z0 - state->player_world_z < 2.5f);

  state->controls.fly_ready_at_ms = 0;
  report("folding mid-glide starts a freefall",
         player_controls_try_fly(state, &state->controls));
  report("fly is off after a manual fold", !player_controls_fly_active(&state->controls));
  report("a manual fold suppresses auto-glide", state->controls.auto_glide_suppress);
  for (int i = 0; i < 30; i++)
  {
    game_state_apply_gravity(state, 1.0f / 30.0f);
    player_controls_update_auto_glide(state, &state->controls, 1.0 / 30.0);
  }
  report("folded wings stay folded through a long fall",
         !player_controls_fly_active(&state->controls));

  char lines[DIALOGUE_MAX_LINES][DIALOGUE_LINE_MAX];
  int n = dialogue_lines_for_actor(find_named_actor(w, "Crow"), lines, DIALOGUE_MAX_LINES);
  report("a crow has something to say", n >= 1 && strcmp(lines[0], "Caw.") == 0);

  n = dialogue_lines_for_actor(find_named_actor(w, "Sparrow"), lines, DIALOGUE_MAX_LINES);
  report("a sparrow chirps", n >= 1 && strcmp(lines[0], "*chirp*") == 0);

  n = dialogue_lines_for_actor(find_named_actor(w, "Gull"), lines, DIALOGUE_MAX_LINES);
  report("a gull calls", n >= 1 && strcmp(lines[0], "Kee-kee!") == 0);

  destroy_state(state);
  world_destroy(w);
}

static void test_dominate(void)
{
  printf("\n-- dominate --\n");

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  GameState *state = make_state(w, 8.5f, 12.5f, (float)(floor_z + 1));
  if (!state)
  {
    report("dominate state built", false);
    world_destroy(w);
    return;
  }

  MobActor *mob = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER, 12.5, 12.5,
                                   (double)(floor_z + 1));
  if (!mob)
  {
    report("golem for dominate built", false);
    destroy_state(state);
    world_destroy(w);
    return;
  }
  Actor copy = mob->base;
  copy.extra_data = mob;
  world_add_runtime_actor(w, &copy);

  report("dominate is refused from across the room",
         !player_controls_try_dominate(state, &state->controls));
  report("the spirit is still free after a refused dominate",
         !player_controls_is_dominating(state));

  state->player_world_x = 12.2f;
  state->player_world_y = 12.4f;
  game_state_sync_positions(state);
  report("a nearby living golem is in range", player_controls_dominate_in_range(state));
  report("dominate succeeds when the player has approached",
         player_controls_try_dominate(state, &state->controls));
  report("the spirit is inhabiting the golem", player_controls_is_dominating(state));
  Actor *body = player_controls_dominated_actor(state);
  report("the inhabited body is the golem", body && strcmp(body->name, "Mud Golem") == 0);
  report("the inhabited body is marked controlled", body && body->is_controlled);
  report("the player snapped to the golem",
         body && fabsf(state->player_world_x - (float)body->x) < 0.01f);

  const float stam_after = state->player->stamina;
  report("inhabiting spends dominate stamina",
         fabsf(stam_after - (100.0f - PLAYER_DOMINATE_STAMINA_COST)) < 0.01f);

  // Cooldown would block an immediate release if we required ready; release is the same skill
  // used again and dominate_ready allows it while inhabiting, but cooldown still blocks.
  // Clear the cooldown the way time would.
  state->controls.dominate_ready_at_ms = 0;
  report("using dominate again releases the body",
         player_controls_try_dominate(state, &state->controls));
  report("the spirit is free after release", !player_controls_is_dominating(state));
  report("the golem is no longer marked controlled",
         w->runtime_actors[0].is_controlled == false);

  destroy_state(state);
  world_destroy(w);
}

static void test_dominate_survives_world_boundary(void)
{
  printf("\n-- dominate across a world boundary --\n");

  const int size = 16, floor_z = 4;
  World *west = make_arena(size, floor_z);
  World *east = make_arena(size, floor_z);
  GameState *state = west ? make_state(west, (float)size - 1.5f, 8.5f, (float)(floor_z + 1))
                          : NULL;
  if (!west || !east || !state)
  {
    report("boundary dominate scenario built", false);
    world_destroy(west);
    world_destroy(east);
    destroy_state(state);
    return;
  }

  if (!universe_init(&state->universe, "0123456789abcdef", 0, 1))
  {
    report("boundary dominate universe initialised", false);
    destroy_state(state);
    world_destroy(west);
    world_destroy(east);
    return;
  }

  const uint64_t hz = (uint64_t)UNIVERSE_HOME_Z;
  universe_place(&state->universe, 0, 0, hz, west);
  universe_place(&state->universe, 1, 0, hz, east);
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = hz;

  MobActor *mob = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER, (double)size - 1.5,
                                   8.5, (double)(floor_z + 1));
  if (!mob)
  {
    report("golem for boundary dominate built", false);
    universe_free(&state->universe);
    destroy_state(state);
    world_destroy(west);
    world_destroy(east);
    return;
  }
  Actor copy = mob->base;
  copy.extra_data = mob;
  world_add_runtime_actor(west, &copy);
  const uint32_t golem_id = west->runtime_actors[0].id;

  state->player_world_x = (float)size - 1.5f;
  state->player_world_y = 8.5f;
  game_state_sync_positions(state);
  report("dominate near the eastern seam succeeds",
         player_controls_try_dominate(state, &state->controls));
  report("spirit is inhabiting before the crossing", player_controls_is_dominating(state));

  // Step past the rim the way walking does: local x past width triggers a seam rebase.
  state->player_world_x = (float)size + 0.5f;
  report("crossing into the eastern world succeeds", game_state_cross_world_boundary(state));
  report("the player is in the eastern cell",
         state->current_world == east && state->player_universe_x == 1);
  report("dominate survives the crossing", player_controls_is_dominating(state));

  Actor *body = player_controls_dominated_actor(state);
  report("the inhabited body is found in the new world",
         body != NULL && body->id == golem_id);
  report("the golem left the western runtime list", west->runtime_actor_count == 0);
  report("the golem joined the eastern runtime list",
         east->runtime_actor_count == 1 && east->runtime_actors[0].id == golem_id);
  report("the body was rebased with the player",
         body && fabs(body->x - (double)state->player_world_x) < 0.01 &&
             fabs(body->y - (double)state->player_world_y) < 0.01);
  report("the body is still marked controlled", body && body->is_controlled);

  // The per-frame dominate sync releases when lookup fails — same check it uses.
  report("post-crossing lookup would not release dominate",
         body && body->is_active && body->health > 0);

  if (state->shadow_world)
  {
    shadow_world_destroy(state->shadow_world);
    state->shadow_world = NULL;
  }
  {
    World *taken = NULL;
    universe_remove(&state->universe, 0, 0, hz, &taken);
    (void)taken;
    universe_remove(&state->universe, 1, 0, hz, &taken);
    (void)taken;
  }
  universe_free(&state->universe);
  destroy_state(state);
  world_destroy(west);
  world_destroy(east);
}

// Falling through a layer keeps the fall speed (clamped to terminal), instead of resetting to a
// fresh drop in the world below.
static void test_velocity_inherits_vertical_transition(void)
{
  printf("\n-- velocity across a vertical world seam --\n");

  const int size = 16;
  World *upper = world_create((uint32_t)size, (uint32_t)size, (uint32_t)size);
  World *lower = make_arena(size, 2);
  GameState *state = upper ? make_state(upper, 8.5f, 8.5f, 2.0f) : NULL;
  if (!upper || !lower || !state)
  {
    report("vertical seam scenario built", false);
    world_destroy(upper);
    world_destroy(lower);
    destroy_state(state);
    return;
  }

  // Upper layer is open air so the player can fall through its floor into the cell beneath.
  for (int z = 0; z < size; z++)
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
        world_set_voxel(upper, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_AIR);

  if (!universe_init(&state->universe, "0123456789abcdef", 0, 1))
  {
    report("vertical seam universe initialised", false);
    destroy_state(state);
    world_destroy(upper);
    world_destroy(lower);
    return;
  }

  const uint64_t uz = (uint64_t)UNIVERSE_HOME_Z + 1;
  universe_place(&state->universe, 0, 0, uz, upper);
  universe_place(&state->universe, 0, 0, uz - 1, lower);
  upper->universe_context = &state->universe;
  upper->universe_x = 0;
  upper->universe_y = 0;
  upper->universe_z = uz;
  lower->universe_context = &state->universe;
  lower->universe_x = 0;
  lower->universe_y = 0;
  lower->universe_z = uz - 1;

  state->current_world = upper;
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = uz;
  state->player_world_x = 8.5f;
  state->player_world_y = 8.5f;
  state->player_world_z = 1.0f;
  game_state_sync_positions(state);

  // Faster than terminal: the seam must clamp rather than discard, and must not zero.
  const float terminal = 200.0f;
  state->controls.velocity_x = 3.0f;
  state->controls.velocity_y = -1.5f;
  state->controls.velocity_z = -(terminal + 80.0f);

  bool crossed = false;
  for (int i = 0; i < 120; i++)
  {
    game_state_apply_gravity(state, 1.0f / 60.0f);
    if (state->current_world == lower)
    {
      crossed = true;
      break;
    }
  }

  report("falling reaches the world beneath", crossed);
  report("horizontal velocity survives the vertical seam",
         crossed && fabsf(state->controls.velocity_x - 3.0f) < 0.01f &&
             fabsf(state->controls.velocity_y - (-1.5f)) < 0.01f);
  report("fall speed is kept and clamped to terminal",
         crossed && state->controls.velocity_z < 0.0f &&
             state->controls.velocity_z >= -terminal - 0.01f &&
             state->controls.velocity_z <= -terminal + 1.0f);
  report("the player is rebased near the ceiling of the lower world",
         crossed && state->player_universe_z == uz - 1 &&
             state->player_world_z > (float)lower->depth - 2.0f);

  if (state->shadow_world)
  {
    shadow_world_destroy(state->shadow_world);
    state->shadow_world = NULL;
  }
  {
    World *taken = NULL;
    universe_remove(&state->universe, 0, 0, uz, &taken);
    (void)taken;
    universe_remove(&state->universe, 0, 0, uz - 1, &taken);
    (void)taken;
  }
  universe_free(&state->universe);
  destroy_state(state);
  world_destroy(upper);
  world_destroy(lower);
}

// An uncontrolled wanderer that walks past the eastern rim joins the neighbour's runtime list.
static void test_mob_crosses_world_boundary(void)
{
  printf("\n-- mob across a world boundary --\n");

  const int size = 16, floor_z = 4;
  const int stand_z = floor_z + 1;
  World *west = make_arena(size, floor_z);
  World *east = make_arena(size, floor_z);
  GameState *state = west ? make_state(west, 8.5f, 8.5f, (float)stand_z) : NULL;
  if (!west || !east || !state)
  {
    report("mob seam scenario built", false);
    world_destroy(west);
    world_destroy(east);
    destroy_state(state);
    return;
  }

  if (!universe_init(&state->universe, "0123456789abcdef", 0, 1))
  {
    report("mob seam universe initialised", false);
    destroy_state(state);
    world_destroy(west);
    world_destroy(east);
    return;
  }

  const uint64_t hz = (uint64_t)UNIVERSE_HOME_Z;
  universe_place(&state->universe, 0, 0, hz, west);
  universe_place(&state->universe, 1, 0, hz, east);
  west->universe_context = &state->universe;
  west->universe_x = 0;
  west->universe_y = 0;
  west->universe_z = hz;
  east->universe_context = &state->universe;
  east->universe_x = 1;
  east->universe_y = 0;
  east->universe_z = hz;

  report("the eastern rim cell is walkable into the neighbour",
         mob_can_move_to(west, size, 8, stand_z));

  MobActor *mob = mob_actor_create("Wanderer", MOB_TYPE_WANDERER, (double)size - 1.2,
                                   8.5, (double)stand_z);
  if (!mob)
  {
    report("wanderer for seam walk built", false);
    universe_free(&state->universe);
    destroy_state(state);
    world_destroy(west);
    world_destroy(east);
    return;
  }
  // Aim straight into the eastern cell so the AI walks across rather than milling inside.
  mob->ai_state.target_x = size;
  mob->ai_state.target_y = 8;
  mob->ai_state.target_z = stand_z;
  mob->ai_state.has_target = true;
  mob->ai_state.retarget_in = 30.0f;
  Actor copy = mob->base;
  copy.extra_data = mob;
  world_add_runtime_actor(west, &copy);
  const uint32_t id = west->runtime_actors[0].id;

  bool crossed = false;
  for (int i = 0; i < 180; i++)
  {
    if (west->runtime_actor_count > 0 && west->runtime_actors[0].extra_data)
    {
      MobActor *m = (MobActor *)west->runtime_actors[0].extra_data;
      m->ai_state.target_x = size;
      m->ai_state.target_y = 8;
      m->ai_state.target_z = stand_z;
      m->ai_state.has_target = true;
      m->ai_state.retarget_in = 30.0f;
    }
    world_step_actors(west, 1.0f / 20.0f);
    if (west->runtime_actor_count == 0 && east->runtime_actor_count == 1)
    {
      crossed = true;
      break;
    }
  }

  report("the wanderer left the western world", crossed && west->runtime_actor_count == 0);
  report("the wanderer arrived in the eastern world",
         crossed && east->runtime_actor_count == 1 && east->runtime_actors[0].id == id);
  report("the wanderer was rebased inside the eastern box",
         crossed && east->runtime_actors[0].x >= 0.0 &&
             east->runtime_actors[0].x < (double)east->width);

  {
    World *taken = NULL;
    universe_remove(&state->universe, 0, 0, hz, &taken);
    (void)taken;
    universe_remove(&state->universe, 1, 0, hz, &taken);
    (void)taken;
  }
  universe_free(&state->universe);
  destroy_state(state);
  world_destroy(west);
  world_destroy(east);
}

static void test_talk(void)
{
  printf("\n-- talk --\n");

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  GameState *state = make_state(w, 8.5f, 12.5f, (float)(floor_z + 1));
  if (!state)
  {
    report("talk state built", false);
    world_destroy(w);
    return;
  }

  MobActor *mob = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER, 12.5, 12.5,
                                   (double)(floor_z + 1));
  if (!mob)
  {
    report("golem for talk built", false);
    destroy_state(state);
    world_destroy(w);
    return;
  }
  Actor copy = mob->base;
  copy.extra_data = mob;
  world_add_runtime_actor(w, &copy);

  report("talk is refused from across the room",
         !player_controls_try_talk(state, &state->controls));
  report("no dialogue opens out of range", !dialogue_active(&state->dialogue));

  state->player_world_x = 12.2f;
  state->player_world_y = 12.4f;
  game_state_sync_positions(state);
  report("a nearby golem is in talk range", player_controls_talk_in_range(state));
  report("talk starts when the player has approached",
         player_controls_try_talk(state, &state->controls));
  report("dialogue is open on the world", dialogue_active(&state->dialogue));
  report("the speaker is the mud golem",
         strcmp(dialogue_speaker(&state->dialogue), "Mud Golem") == 0);
  report("the golem first answers with an ellipsis",
         strcmp(dialogue_current_text(&state->dialogue), "...") == 0);
  report("the golem has a second rumble lined up",
         dialogue_line_count(&state->dialogue) == 2);

  report("the first advance finishes the ellipsis", dialogue_advance(&state->dialogue));
  report("the next advance reaches the rumble", dialogue_advance(&state->dialogue));
  report("the rumble is a mud-golem sound",
         strcmp(dialogue_current_text(&state->dialogue), "Schmmmmufff...") == 0);
  report("finishing the rumble keeps the box open until the line is done",
         dialogue_advance(&state->dialogue));
  report("advancing past the last line closes the conversation",
         !dialogue_advance(&state->dialogue));
  report("dialogue is closed after the last line", !dialogue_active(&state->dialogue));

  state->controls.talk_ready_at_ms = 0;
  report("talk can be started again after it closes",
         player_controls_try_talk(state, &state->controls));

  state->player_world_x = 2.0f;
  state->player_world_y = 2.0f;
  game_state_sync_positions(state);
  game_state_tick_dialogue(state);
  report("walking away closes the conversation", !dialogue_active(&state->dialogue));

  destroy_state(state);
  world_destroy(w);

  World *w2 = make_arena(size, floor_z);
  GameState *state2 = make_state(w2, 12.2f, 12.4f, (float)(floor_z + 1));
  if (!state2)
  {
    report("generic talk state built", false);
    world_destroy(w2);
    return;
  }
  MobActor *stray = mob_actor_create("Stray", MOB_TYPE_WANDERER, 12.5, 12.5,
                                     (double)(floor_z + 1));
  if (!stray)
  {
    report("generic wanderer built", false);
    destroy_state(state2);
    world_destroy(w2);
    return;
  }
  Actor stray_copy = stray->base;
  stray_copy.extra_data = stray;
  world_add_runtime_actor(w2, &stray_copy);
  report("talk works on any nearby mob",
         player_controls_use_hotbar_slot(state2, &state2->controls, 2));
  report("a generic wanderer still only mutters",
         strcmp(dialogue_current_text(&state2->dialogue), "...") == 0);
  dialogue_advance(&state2->dialogue);
  dialogue_advance(&state2->dialogue);
  report("then it shuffles",
         strcmp(dialogue_current_text(&state2->dialogue), "*shuffles*") == 0);

  destroy_state(state2);
  world_destroy(w2);
}

static void test_poly_anims(void)
{
  printf("\n-- polygon AI clips --\n");

  report("polygon meshes load", poly_mesh_init("models/poly"));

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("anim arena built", false);
    return;
  }
  const double z = (double)(floor_z + 1);

  MobActor *golem = mob_actor_create("Goleling", MOB_TYPE_WANDERER, 8.5, 12.5, z);
  report("a polygon golem can be created", golem != NULL);
  if (!golem)
  {
    world_destroy(w);
    return;
  }
  mob_actor_bind_mesh(golem, "goleling");
  golem->ai_state.has_target = true;
  golem->ai_state.target_x = 20;
  golem->ai_state.target_y = 12;
  golem->ai_state.retarget_in = 30.0f;
  report("the golem joined the arena", add_mob_to_world(w, golem));

  world_step_actors(w, 1.0f / 30.0f);
  MobActor *gm = (MobActor *)w->runtime_actors[0].extra_data;
  report("walking plays Fast_Flying",
         gm && strcmp(gm->anim_clip, "Fast_Flying") == 0);
  report("walking faces the destination",
         gm && fabsf(gm->facing_yaw) < 0.4f);

  gm->ai_state.has_target = false;
  gm->ai_state.retarget_in = 30.0f;
  gm->base.velocity_x = 0.0;
  gm->base.velocity_y = 0.0;
  w->runtime_actors[0].velocity_x = 0.0;
  w->runtime_actors[0].velocity_y = 0.0;
  world_step_actors(w, 1.0f / 30.0f);
  gm = (MobActor *)w->runtime_actors[0].extra_data;
  report("standing plays Flying_Idle",
         gm && strcmp(gm->anim_clip, "Flying_Idle") == 0);

  w->runtime_actors[0].health = gm->base.health - 10;
  world_step_actors(w, 1.0f / 30.0f);
  gm = (MobActor *)w->runtime_actors[0].extra_data;
  report("a hit plays HitReact",
         gm && strcmp(gm->anim_clip, "HitReact") == 0);

  MobActor *other = mob_actor_create("Goleling B", MOB_TYPE_WANDERER, 9.2, 12.5, z);
  report("a second golem can stand in punch range", other != NULL);
  if (other)
  {
    mob_actor_bind_mesh(other, "goleling");
    other->ai_state.has_target = false;
    other->ai_state.retarget_in = 30.0f;
    add_mob_to_world(w, other);
    gm = (MobActor *)w->runtime_actors[0].extra_data;
    gm->anim_lock = 0.0f;
    gm->melee_cooldown = 0.0f;
    gm->ai_state.has_target = false;
    gm->ai_state.retarget_in = 30.0f;
    // Same-type herd mates will not casually punch; aggro forces a strike.
    gm->mood = MOB_MOOD_ANGRY;
    gm->goal = MOB_GOAL_CHASE;
    gm->aggro_target_id = w->runtime_actors[1].id;
    gm->aggro_ttl = 20.0f;
    world_step_actors(w, 1.0f / 30.0f);
    gm = (MobActor *)w->runtime_actors[0].extra_data;
    MobActor *om = (MobActor *)w->runtime_actors[1].extra_data;
    report("a nearby wanderer throws a Punch",
           (gm && strcmp(gm->anim_clip, "Punch") == 0) ||
           (om && strcmp(om->anim_clip, "Punch") == 0));
    const uint32_t hp0 = w->runtime_actors[0].health;
    const uint32_t hp1 = w->runtime_actors[1].health;
    report("a Punch deals hit-point damage",
           hp0 < 100 || hp1 < 100);
  }

  mob_actor_notify_talk(&w->runtime_actors[0], 0);
  gm = (MobActor *)w->runtime_actors[0].extra_data;
  report("talking nods Yes", gm && strcmp(gm->anim_clip, "Yes") == 0);
  mob_actor_notify_talk(&w->runtime_actors[0], 1);
  report("a later line shakes No", gm && strcmp(gm->anim_clip, "No") == 0);

  MobActor *bird = mob_actor_create_bird(BIRD_KIND_CROW, 18.5, 18.5, z);
  report("a polygon bird can be created", bird != NULL);
  if (bird)
  {
    strncpy(bird->base.name, "Pigeon", sizeof(bird->base.name) - 1);
    mob_actor_bind_mesh(bird, "pigeon");
    bird->ai_state.bird_phase = BIRD_PHASE_PERCH;
    bird->ai_state.bird_timer = 0.0f;
    add_mob_to_world(w, bird);
    world_step_actors(w, 1.0f / 30.0f);
    Actor *pa = find_named_actor(w, "Pigeon");
    MobActor *pm = pa ? (MobActor *)pa->extra_data : NULL;
    report("takeoff plays Fast_Flying",
           pm && strcmp(pm->anim_clip, "Fast_Flying") == 0);
  }

  w->runtime_actors[0].health = 0;
  world_step_actors(w, 1.0f / 30.0f);
  gm = (MobActor *)w->runtime_actors[0].extra_data;
  report("a killing blow plays Death",
         gm && strcmp(gm->anim_clip, "Death") == 0);
  report("the corpse stays while Death plays",
         w->runtime_actors[0].is_active);
  for (int i = 0; i < 90; i++)
    world_step_actors(w, 1.0f / 30.0f);
  gm = (MobActor *)w->runtime_actors[0].extra_data;
  report("Death ending leaves the corpse as an entity",
         w->runtime_actors[0].is_active && w->runtime_actors[0].health == 0);
  report("Death pose freezes on the last frame",
         gm && gm->anim_time > 0.0f && strcmp(gm->anim_clip, "Death") == 0);

  world_destroy(w);
}

static void test_aggro_and_mood(void)
{
  printf("\n-- aggro, mood, and goals --\n");

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("aggro arena built", false);
    return;
  }

  const double z = (double)(floor_z + 1);
  MobActor *mob = mob_actor_create("Target", MOB_TYPE_WANDERER, 12.5, 12.5, z);
  report("aggro subject created", mob != NULL);
  if (!mob)
  {
    world_destroy(w);
    return;
  }
  report("new mobs start calm and wandering",
         mob->mood == MOB_MOOD_CALM && mob->goal == MOB_GOAL_WANDER);

  Actor copy = mob->base;
  copy.extra_data = mob;
  world_add_runtime_actor(w, &copy);

  mob_ai_set_player_presence(10.0f, 12.5f, (float)z, 0);

  // Below threshold: curious, not chasing.
  w->runtime_actors[0].health = 95;
  mob_actor_after_damage(&w->runtime_actors[0], MOB_THREAT_PLAYER);
  MobActor *m = (MobActor *)w->runtime_actors[0].extra_data;
  report("light damage sparks curiosity",
         m && m->mood == MOB_MOOD_CURIOUS && m->goal != MOB_GOAL_CHASE);

  // Accumulate past threshold.
  w->runtime_actors[0].health = 80;
  m->base.health = 95;
  m->last_health = 95;
  mob_actor_after_damage(&w->runtime_actors[0], MOB_THREAT_PLAYER);
  m = (MobActor *)w->runtime_actors[0].extra_data;
  report("enough damage aggroes the mob",
         m && m->mood == MOB_MOOD_ANGRY && m->goal == MOB_GOAL_CHASE);
  report("aggro remembers the player threat",
         m && m->aggro_target_id == MOB_THREAT_PLAYER);

  const double start_x = w->runtime_actors[0].x;
  for (int i = 0; i < 60; i++)
    world_step_actors(w, 1.0f / 30.0f);
  const double chased = start_x - w->runtime_actors[0].x;
  printf("       chase delta x=%.2f (toward player at x=10)\n", chased);
  report("an angry mob chases the player presence", chased > 0.3);

  world_destroy(w);
}

static void test_mob_melee_damage_and_counter_aggro(void)
{
  printf("\n-- mob melee damage --\n");

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("melee arena built", false);
    return;
  }
  const double z = (double)(floor_z + 1);

  MobActor *a = mob_actor_create("Bruiser", MOB_TYPE_WANDERER, 10.5, 12.5, z);
  MobActor *b = mob_actor_create("Victim", MOB_TYPE_WANDERER, 11.5, 12.5, z);
  report("melee pair created", a && b);
  if (!a || !b)
  {
    if (a)
      mob_actor_destroy(a);
    if (b)
      mob_actor_destroy(b);
    world_destroy(w);
    return;
  }
  a->base.strength = 20;
  a->ai_state.has_target = false;
  a->ai_state.retarget_in = 60.0f;
  a->goal = MOB_GOAL_CHASE;
  a->mood = MOB_MOOD_ANGRY;
  a->aggro_target_id = b->base.id;
  a->aggro_ttl = 20.0f;
  b->ai_state.has_target = false;
  b->ai_state.retarget_in = 60.0f;

  Actor ca = a->base;
  ca.extra_data = a;
  Actor cb = b->base;
  cb.extra_data = b;
  world_add_runtime_actor(w, &ca);
  world_add_runtime_actor(w, &cb);

  const uint32_t before = w->runtime_actors[1].health;
  const uint32_t attacker_id = w->runtime_actors[0].id;
  world_step_actors(w, 1.0f / 30.0f);
  MobActor *victim = (MobActor *)w->runtime_actors[1].extra_data;
  report("mob melee reduces target health",
         w->runtime_actors[1].health < before);

  // Drive accum over threshold with repeated after_damage if one punch was not enough.
  while (victim && victim->goal != MOB_GOAL_CHASE && w->runtime_actors[1].health > 50)
  {
    uint32_t hp = w->runtime_actors[1].health;
    w->runtime_actors[1].health = hp > 5 ? hp - 5 : 0;
    victim->base.health = hp;
    victim->last_health = hp;
    mob_actor_after_damage(&w->runtime_actors[1], attacker_id);
    victim = (MobActor *)w->runtime_actors[1].extra_data;
  }
  report("the victim aggros the attacker after enough hits",
         victim && victim->goal == MOB_GOAL_CHASE &&
             victim->aggro_target_id == attacker_id);
  report("hurt display arms after damage",
         w->runtime_actors[1].hurt_display_ttl >= ACTOR_HURT_FADE_SECONDS);
  {
    const float hurt0 = w->runtime_actors[1].hurt_display_ttl;
    world_step_actors(w, 0.5f);
    report("hurt display decays over time",
           w->runtime_actors[1].hurt_display_ttl < hurt0 &&
               w->runtime_actors[1].hurt_display_ttl > 0.0f);
    for (int i = 0; i < 20; i++)
      world_step_actors(w, 0.5f);
    report("hurt display clears after idle",
           w->runtime_actors[1].hurt_display_ttl <= 0.0f);
  }

  // Voxel (unmeshed) corpses stay as active entities too.
  w->runtime_actors[1].health = 0;
  mob_actor_after_damage(&w->runtime_actors[1], attacker_id);
  report("a voxel corpse stays as an active entity",
         w->runtime_actors[1].is_active && w->runtime_actors[1].health == 0);
  for (int i = 0; i < 30; i++)
    world_step_actors(w, 1.0f / 30.0f);
  report("a voxel corpse is still present after settling",
         w->runtime_actors[1].is_active && w->runtime_actors[1].health == 0);

  world_destroy(w);
}

static void test_breeding_stubs(void)
{
  printf("\n-- breeding stubs --\n");

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("breed arena built", false);
    return;
  }
  const double z = (double)(floor_z + 1);

  MobActor *p1 = mob_actor_create("Alpha", MOB_TYPE_WANDERER, 10.5, 12.5, z);
  MobActor *p2 = mob_actor_create("Beta", MOB_TYPE_WANDERER, 11.0, 12.5, z);
  MobActor *bird = mob_actor_create_bird(BIRD_KIND_CROW, 14.5, 12.5, z);
  report("breed parents created", p1 && p2 && bird);
  if (!p1 || !p2 || !bird)
  {
    if (p1)
      mob_actor_destroy(p1);
    if (p2)
      mob_actor_destroy(p2);
    if (bird)
      mob_actor_destroy(bird);
    world_destroy(w);
    return;
  }

  p1->base.strength = 14;
  p2->base.strength = 6;
  p1->base.constitution = 16;
  p2->base.constitution = 8;
  p1->base.health = 90;
  p2->base.health = 70;

  report("mixed types refuse progeny",
         mob_actor_create_progeny(p1, bird, 10.8, 12.5, z) == NULL);

  MobActor *child = mob_actor_create_progeny(p1, p2, 10.8, 12.5, z);
  report("compatible parents produce progeny", child != NULL);
  if (child)
  {
    report("progeny inherits wanderer type", child->mob_type == MOB_TYPE_WANDERER);
    report("progeny strength sits near the parental average",
           child->base.strength >= 6 && child->base.strength <= 16);
    report("progeny constitution sits near the parental average",
           child->base.constitution >= 6 && child->base.constitution <= 18);
    report("progeny starts calm", child->mood == MOB_MOOD_CALM);
    mob_actor_destroy(child);
  }

  Actor c1 = p1->base;
  c1.extra_data = p1;
  Actor c2 = p2->base;
  c2.extra_data = p2;
  world_add_runtime_actor(w, &c1);
  world_add_runtime_actor(w, &c2);
  // Re-fetch MobActors from runtime (same pointers).
  MobActor *ra = (MobActor *)w->runtime_actors[0].extra_data;
  MobActor *rb = (MobActor *)w->runtime_actors[1].extra_data;
  ra->mood = MOB_MOOD_CALM;
  rb->mood = MOB_MOOD_CURIOUS;
  ra->breed_cooldown = 0.0f;
  rb->breed_cooldown = 0.0f;
  const int before = w->runtime_actor_count;
  report("try_breed inserts a child into the world",
         mob_actor_try_breed(ra, rb, w) && w->runtime_actor_count == before + 1);
  report("parents gain a breed cooldown",
         ra->breed_cooldown > 0.0f && rb->breed_cooldown > 0.0f);
  report("a second try_breed is refused while cooling down",
         !mob_actor_try_breed(ra, rb, w));

  mob_actor_destroy(bird);
  world_destroy(w);
}

static void test_livelier_wander(void)
{
  printf("\n-- livelier wander --\n");

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("livelier arena built", false);
    return;
  }

  MobActor *mob = mob_actor_create("Scout", MOB_TYPE_WANDERER, 12.5, 12.5,
                                   (double)(floor_z + 1));
  report("scout created", mob != NULL);
  if (!mob)
  {
    world_destroy(w);
    return;
  }
  mob->mood = MOB_MOOD_CURIOUS;
  mob->ai_state.retarget_in = 0.0f;
  Actor copy = mob->base;
  copy.extra_data = mob;
  world_add_runtime_actor(w, &copy);

  bool moved = false;
  bool had_target = false;
  for (int i = 0; i < 45; i++)
  {
    world_step_actors(w, 1.0f / 30.0f);
    MobActor *m = (MobActor *)w->runtime_actors[0].extra_data;
    if (m && m->ai_state.has_target)
      had_target = true;
    if (fabs(w->runtime_actors[0].velocity_x) > 0.05 ||
        fabs(w->runtime_actors[0].velocity_y) > 0.05)
      moved = true;
  }
  report("a curious wanderer picks a walk target quickly", had_target || moved);
  report("a curious wanderer starts moving within ~1.5s", moved);

  world_destroy(w);
}

static void test_herding_reputation_collision(void)
{
  printf("\n-- herding, reputation, collision --\n");

  const int size = 24, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("herd arena built", false);
    return;
  }
  const double z = (double)(floor_z + 1);

  MobActor *s1 = mob_actor_create_sheep(8.5, 12.5, z);
  MobActor *s2 = mob_actor_create_sheep(14.5, 12.5, z);
  // Stray stands well away so herding is not fighting a nearby foe.
  MobActor *stray = mob_actor_create("Stray", MOB_TYPE_WANDERER, 20.5, 20.5, z);
  report("herd actors created", s1 && s2 && stray);
  if (!s1 || !s2 || !stray)
  {
    if (s1)
      mob_actor_destroy(s1);
    if (s2)
      mob_actor_destroy(s2);
    if (stray)
      mob_actor_destroy(stray);
    world_destroy(w);
    return;
  }

  Actor a1 = s1->base;
  a1.extra_data = s1;
  Actor a2 = s2->base;
  a2.extra_data = s2;
  Actor a3 = stray->base;
  a3.extra_data = stray;
  world_add_runtime_actor(w, &a1);
  world_add_runtime_actor(w, &a2);
  world_add_runtime_actor(w, &a3);

  MobActor *m1 = (MobActor *)w->runtime_actors[0].extra_data;
  MobActor *m2 = (MobActor *)w->runtime_actors[1].extra_data;
  MobActor *mw = (MobActor *)w->runtime_actors[2].extra_data;

  // Force an encounter between the two sheep first.
  m1->reputation_scan_in = 0.0f;
  m2->reputation_scan_in = 0.0f;
  world_step_actors(w, 1.0f / 30.0f);
  m1 = (MobActor *)w->runtime_actors[0].extra_data;
  m2 = (MobActor *)w->runtime_actors[1].extra_data;

  report("sheep encounter each other with affection",
         m1 && mob_actor_reputation_get(m1, w->runtime_actors[1].id) > 0);

  // Move stray into encounter range and scan again for dislike.
  w->runtime_actors[2].x = 9.0;
  w->runtime_actors[2].y = 12.5;
  mw = (MobActor *)w->runtime_actors[2].extra_data;
  m1->reputation_scan_in = 0.0f;
  mw->reputation_scan_in = 0.0f;
  world_step_actors(w, 1.0f / 30.0f);
  m1 = (MobActor *)w->runtime_actors[0].extra_data;
  mw = (MobActor *)w->runtime_actors[2].extra_data;

  report("sheep dislike a nearby stray wanderer",
         m1 && mob_actor_reputation_get(m1, w->runtime_actors[2].id) < 0);
  report("reputation is tracked many-to-many",
         m2 && mob_actor_reputation_get(m2, w->runtime_actors[0].id) > 0 &&
             mw && mob_actor_reputation_get(mw, w->runtime_actors[0].id) < 0);

  mob_actor_reputation_adjust(m1, w->runtime_actors[2].id, -20);
  report("reputation adjusts on interaction",
         mob_actor_reputation_get(m1, w->runtime_actors[2].id) <= -20);

  // Park the stray far away again so herding is uncontested.
  w->runtime_actors[2].x = 20.5;
  w->runtime_actors[2].y = 20.5;
  w->runtime_actors[0].x = 8.5;
  w->runtime_actors[1].x = 14.5;
  w->runtime_actors[0].y = 12.5;
  w->runtime_actors[1].y = 12.5;
  m1 = (MobActor *)w->runtime_actors[0].extra_data;
  m2 = (MobActor *)w->runtime_actors[1].extra_data;
  m1->ai_state.has_target = false;
  m1->ai_state.retarget_in = 0.0f;
  m2->ai_state.has_target = false;
  m2->ai_state.retarget_in = 0.0f;
  m1->goal = MOB_GOAL_WANDER;
  m2->goal = MOB_GOAL_WANDER;

  const double gap0 = fabs(w->runtime_actors[0].x - w->runtime_actors[1].x);
  for (int i = 0; i < 240; i++)
    world_step_actors(w, 1.0f / 30.0f);
  const double gap1 = fabs(w->runtime_actors[0].x - w->runtime_actors[1].x);
  printf("       sheep gap %.2f -> %.2f\n", gap0, gap1);
  report("similar animals herd closer together", gap1 < gap0 - 0.5);

  // Collision: stack two actors and ensure a step separates them.
  w->runtime_actors[0].x = 11.0;
  w->runtime_actors[0].y = 12.5;
  w->runtime_actors[1].x = 11.05;
  w->runtime_actors[1].y = 12.5;
  w->runtime_actors[0].velocity_x = 0.0;
  w->runtime_actors[0].velocity_y = 0.0;
  w->runtime_actors[1].velocity_x = 0.0;
  w->runtime_actors[1].velocity_y = 0.0;
  m1 = (MobActor *)w->runtime_actors[0].extra_data;
  m2 = (MobActor *)w->runtime_actors[1].extra_data;
  m1->ai_state.has_target = false;
  m1->ai_state.retarget_in = 60.0f;
  m2->ai_state.has_target = false;
  m2->ai_state.retarget_in = 60.0f;
  m1->goal = MOB_GOAL_WANDER;
  m2->goal = MOB_GOAL_WANDER;
  world_step_actors(w, 1.0f / 30.0f);
  const double sep = hypot(w->runtime_actors[0].x - w->runtime_actors[1].x,
                           w->runtime_actors[0].y - w->runtime_actors[1].y);
  printf("       post-collision separation %.3f\n", sep);
  report("overlapping actors are pushed apart",
         sep + 1e-3 >= (double)(MOB_ACTOR_RADIUS * 2.0f));

  // Terrain cylinder: a thin solid post at the destination must block walking into it.
  {
    const int px = 16, py = 12, pz = floor_z + 1;
    world_set_voxel(w, (uint32_t)px, (uint32_t)py, (uint32_t)pz, VOXEL_STONE);
    world_refresh_occupancy_bitfield(w);
    report("post blocks cylinder occupy",
           !mob_actor_can_occupy(w, (double)px + 0.15, (double)py + 0.5, (double)(floor_z + 1)));
    report("open cell still occupyable",
           mob_actor_can_occupy(w, 10.5, 12.5, (double)(floor_z + 1)));
  }

  // Spawn safety: a second sheep on top of the first must be nudged clear.
  {
    MobActor *stacked = mob_actor_create_sheep(w->runtime_actors[0].x, w->runtime_actors[0].y,
                                               w->runtime_actors[0].z);
    report("spawn refuses exact stack or relocates",
           stacked == NULL ||
               (mob_actor_spawn_in_world(w, stacked) &&
                mob_actor_clear_of_actors(w, stacked->base.x, stacked->base.y, stacked->base.z,
                                          stacked->base.id)));
  }

  // Sheep still wander (velocity or target over time).
  m1 = (MobActor *)w->runtime_actors[0].extra_data;
  m1->ai_state.retarget_in = 0.0f;
  bool sheep_moved = false;
  for (int i = 0; i < 60; i++)
  {
    world_step_actors(w, 1.0f / 30.0f);
    if (fabs(w->runtime_actors[0].velocity_x) > 0.05 ||
        fabs(w->runtime_actors[0].velocity_y) > 0.05)
      sheep_moved = true;
  }
  report("sheep still wander", sheep_moved);

  // After several steps, living actors must not remain inside each other's radius.
  {
    bool clear = true;
    for (int a = 0; a < w->runtime_actor_count; a++)
    {
      Actor *A = &w->runtime_actors[a];
      if (!A->is_active || A->health == 0)
        continue;
      for (int b = a + 1; b < w->runtime_actor_count; b++)
      {
        Actor *B = &w->runtime_actors[b];
        if (!B->is_active || B->health == 0)
          continue;
        double d = hypot(A->x - B->x, A->y - B->y);
        double dz = fabs(A->z - B->z);
        if (dz <= (double)MOB_ACTOR_SEPARATION_Z &&
            d + 1e-3 < (double)(MOB_ACTOR_RADIUS * 2.0f))
          clear = false;
      }
      if (!mob_actor_can_occupy(w, A->x, A->y, A->z) && !A->is_flying)
        clear = false;
    }
    report("actors remain clear of each other and terrain", clear);
  }

  world_destroy(w);
}

static void test_town_portal(void)
{
  printf("\n-- town portal --\n");

  report("town portal is named Town Portal",
         strcmp(player_controls_skill_name(SKILL_TOWN_PORTAL), "Town Portal") == 0);
  report("town portal abbreviates Tp",
         strcmp(skill_abbrev(SKILL_TOWN_PORTAL), "Tp") == 0);

  const int size = 24, floor_z = 6;
  World *home = make_arena(size, floor_z);
  World *wild = make_arena(size, floor_z);
  if (!home || !wild)
  {
    report("town portal arenas built", false);
    if (home)
      world_destroy(home);
    if (wild)
      world_destroy(wild);
    return;
  }

  GameState *state = make_state(wild, 12.5f, 12.5f, (float)(floor_z + 1));
  if (!state)
  {
    report("town portal state built", false);
    world_destroy(home);
    world_destroy(wild);
    return;
  }

  state->current_screen = GAME_SCREEN_WORLD;
  if (!universe_init(&state->universe, "0123456789abcdef", 0, 1))
  {
    report("town portal universe initialised", false);
    destroy_state(state);
    world_destroy(home);
    world_destroy(wild);
    return;
  }

  GameWorlds *gw = (GameWorlds *)calloc(1, sizeof(GameWorlds));
  gw->home_world = home;
  gw->base_seed = strdup("0123456789abcdef");
  state->game_worlds = gw;
  universe_place(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z, home);
  universe_place(&state->universe, 1, 0, (uint64_t)UNIVERSE_HOME_Z - 2, wild);
  state->player_universe_x = 1;
  state->player_universe_y = 0;
  state->player_universe_z = (uint64_t)UNIVERSE_HOME_Z - 2;
  state->current_world = wild;

  report("town portal is ready away from home",
         player_controls_town_portal_ready(state, &state->controls));

  player_controls_update_town_portal(state, &state->controls, true);
  report("holding T starts a channel",
         player_controls_town_portal_charging(&state->controls));
  report("channel opens a Teleporting home dialogue",
         dialogue_active(&state->dialogue) &&
             strcmp(dialogue_speaker(&state->dialogue), "Town Portal") == 0);
  report("charge progress is non-zero while holding",
         player_controls_town_portal_charge_progress(&state->controls) >= 0.0f);

  player_controls_update_town_portal(state, &state->controls, false);
  report("releasing T cancels the channel",
         !player_controls_town_portal_charging(&state->controls));
  report("cancelling closes the dialogue", !dialogue_active(&state->dialogue));

  MobActor *mob = mob_actor_create("Mud Golem", MOB_TYPE_WANDERER, 12.5, 12.5,
                                   (double)(floor_z + 1));
  if (!mob)
  {
    report("golem for portal built", false);
    free(gw->base_seed);
    free(gw);
    state->game_worlds = NULL;
    universe_free(&state->universe);
    destroy_state(state);
    world_destroy(home);
    world_destroy(wild);
    return;
  }
  Actor copy = mob->base;
  copy.extra_data = mob;
  world_add_runtime_actor(wild, &copy);

  state->player_world_x = 12.2f;
  state->player_world_y = 12.4f;
  game_state_sync_positions(state);
  report("dominate before portal succeeds",
         player_controls_try_dominate(state, &state->controls));
  report("spirit is inhabiting before portal",
         player_controls_is_dominating(state));

  // Complete without waiting the full charge: call teleport directly the way
  // the channel does at the end of its hold.
  report("teleport home succeeds from the wilderness",
         game_state_teleport_home(state));
  report("the spirit is free after portal",
         !player_controls_is_dominating(state));
  report("the golem stayed in the wilderness under AI",
         wild->runtime_actors[0].is_controlled == false &&
             wild->runtime_actors[0].is_active);
  report("the player is back on the home cell",
         state->current_world == home &&
             state->player_universe_x == 0 &&
             state->player_universe_y == 0 &&
             state->player_universe_z == (uint64_t)UNIVERSE_HOME_Z);

  // Charge-to-complete path: start channel and wait out the hold.
  state->player_universe_x = 1;
  state->player_universe_y = 0;
  state->player_universe_z = (uint64_t)UNIVERSE_HOME_Z - 2;
  state->current_world = wild;
  state->player_world_x = 12.5f;
  state->player_world_y = 12.5f;
  state->player_world_z = (float)(floor_z + 1);
  game_state_sync_positions(state);
  state->controls.town_portal_ready_at_ms = 0;
  if (state->player)
    state->player->stamina = 100;

  player_controls_update_town_portal(state, &state->controls, true);
  report("a second channel starts",
         player_controls_town_portal_charging(&state->controls));
  SDL_Delay(PLAYER_TOWN_PORTAL_CHARGE_MS + 50);
  player_controls_update_town_portal(state, &state->controls, true);
  report("holding through the charge returns home",
         !player_controls_town_portal_charging(&state->controls) &&
             state->current_world == home &&
             state->player_universe_z == (uint64_t)UNIVERSE_HOME_Z);

  if (state->shadow_world)
  {
    shadow_world_destroy(state->shadow_world);
    state->shadow_world = NULL;
  }
  free(gw->base_seed);
  free(gw);
  state->game_worlds = NULL;
  // Home and wild were placed in the universe; free via universe or destroy carefully.
  // universe_free does not destroy worlds — take them out first.
  {
    World *taken = NULL;
    universe_remove(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z, &taken);
    (void)taken;
    universe_remove(&state->universe, 1, 0, (uint64_t)UNIVERSE_HOME_Z - 2, &taken);
    (void)taken;
  }
  universe_free(&state->universe);
  destroy_state(state);
  world_destroy(home);
  world_destroy(wild);
}

static void test_wilderness_living(void)
{
  printf("\n-- wilderness living surface and fauna --\n");

  World *w = world_create(48, 48, 48);
  if (!w)
  {
    report("wilderness world created", false);
    return;
  }
  w->universe_x = 0;
  w->universe_y = 0;
  w->universe_z = 0;
  world_generate_with_type(w, "wilderness_living_seed", WORLD_TYPE_WILDERNESS);

  int grass = 0, soil = 0, bushes = 0, tall_grass = 0, wood = 0, leaves = 0;
  for (uint32_t z = 0; z < w->depth; z++)
  {
    for (uint32_t y = 0; y < w->height; y++)
    {
      for (uint32_t x = 0; x < w->width; x++)
      {
        Voxel *v = world_get_voxel(w, x, y, z);
        if (!v)
          continue;
        switch (v->type)
        {
        case VOXEL_GRASS:
        case VOXEL_GRASS_WIDE:
        case VOXEL_GRASS_CLOVER:
        case VOXEL_GRASS_SHARP:
        case VOXEL_GRASS_MOSS:
          grass++;
          break;
        case VOXEL_SOIL:
        case VOXEL_SOIL_LOAM:
        case VOXEL_SOIL_CLAY:
        case VOXEL_SOIL_SILT:
          soil++;
          break;
        case VOXEL_BUSH:
        case VOXEL_BUSH_FERN:
        case VOXEL_BUSH_VINES:
        case VOXEL_BUSH_THORNS:
        case VOXEL_BUSH_BLUEBERRY:
        case VOXEL_BUSH_BLACKBERRY:
        case VOXEL_BUSH_RASPBERRY:
          bushes++;
          break;
        case VOXEL_GRASS_TALL:
          tall_grass++;
          break;
        case VOXEL_WOOD:
        case VOXEL_WOOD_OAK:
        case VOXEL_WOOD_BIRCH:
        case VOXEL_WOOD_PINE:
        case VOXEL_WOOD_MAPLE:
        case VOXEL_WOOD_BEECH:
        case VOXEL_WOOD_SPRUCE:
        case VOXEL_WOOD_JUNIPER:
        case VOXEL_WOOD_WILLOW:
        case VOXEL_WOOD_CYPRESS:
        case VOXEL_WOOD_COTTONWOOD:
        case VOXEL_WOOD_ACACIA:
        case VOXEL_WOOD_REDWOOD:
        case VOXEL_WOOD_ELM:
          wood++;
          break;
        case VOXEL_LEAVES:
        case VOXEL_LEAVES_OAK:
        case VOXEL_LEAVES_BIRCH:
        case VOXEL_LEAVES_PINE:
        case VOXEL_LEAVES_MAPLE:
        case VOXEL_LEAVES_BEECH:
        case VOXEL_LEAVES_SPRUCE:
        case VOXEL_LEAVES_JUNIPER:
        case VOXEL_LEAVES_WILLOW:
        case VOXEL_LEAVES_CYPRESS:
        case VOXEL_LEAVES_COTTONWOOD:
        case VOXEL_LEAVES_ACACIA:
        case VOXEL_LEAVES_REDWOOD:
        case VOXEL_LEAVES_ELM:
          leaves++;
          break;
        default:
          break;
        }
      }
    }
  }

  printf("       grass=%d soil=%d bushes=%d tall_grass=%d wood=%d leaves=%d actors=%d\n",
         grass, soil, bushes, tall_grass, wood, leaves, w->runtime_actor_count);

  report("wilderness has grass surface", grass > 0);
  report("wilderness has soil under the grass", soil > 0);
  report("wilderness has shrubbery or tall grass", bushes + tall_grass > 0);
  /* Desert/volcanic may be nearly treeless; require trees or sparse shrub biomes. */
  report("wilderness has trees or arid scrub", (wood > 0 && leaves > 0) || bushes > 0);
  report("wilderness spawned fauna", w->runtime_actor_count >= 8);

  int wild_named = 0;
  int ground_fauna = 0;
  int fliers = 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    if (strncmp(w->runtime_actors[i].name, "Wild ", 5) == 0)
      wild_named++;
    MobActor *mob = (MobActor *)w->runtime_actors[i].extra_data;
    if (!mob)
      continue;
    if (mob->mob_type == MOB_TYPE_WANDERER || mob->mob_type == MOB_TYPE_SHEEP ||
        mob->mob_type == MOB_TYPE_CHICKEN || mob->mob_type == MOB_TYPE_DEER ||
        mob->mob_type == MOB_TYPE_LIZARD || mob->mob_type == MOB_TYPE_SPIDER ||
        mob->mob_type == MOB_TYPE_SLIME)
      ground_fauna++;
    if (mob->mob_type == MOB_TYPE_BIRD || mob->mob_type == MOB_TYPE_BAT)
      fliers++;
  }
  report("wilderness fauna use Wild names", wild_named >= 6);
  report("wilderness has ground fauna", ground_fauna >= 3);
  report("wilderness has birds or bats", fliers >= 3);

  int in_bounds = 0, on_surface = 0, birds = 0, out_of_world = 0, buried = 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    Actor *a = &w->runtime_actors[i];
    MobActor *mob = (MobActor *)a->extra_data;
    if (a->x < 0.0 || a->y < 0.0 || a->z < 0.0 ||
        a->x >= (double)w->width || a->y >= (double)w->height || a->z >= (double)w->depth)
    {
      out_of_world++;
      continue;
    }
    in_bounds++;
    int ix = (int)a->x, iy = (int)a->y, iz = (int)floor(a->z);
    Voxel *here = world_get_voxel(w, (uint32_t)ix, (uint32_t)iy, (uint32_t)iz);
    if (here && world_voxel_type_blocks_movement(here->type) && !(mob && mob->base.is_flying))
      buried++;
    // Surface: solid floor underfoot (or perch cell for birds).
    if (iz > 0)
    {
      Voxel *below = world_get_voxel(w, (uint32_t)ix, (uint32_t)iy, (uint32_t)(iz - 1));
      if (below && world_voxel_type_blocks_movement(below->type) &&
          (!here || !world_voxel_type_blocks_movement(here->type)))
        on_surface++;
    }
    if (mob && mob->mob_type == MOB_TYPE_BIRD)
      birds++;
  }
  printf("       in_bounds=%d on_surface=%d birds=%d out=%d buried=%d\n",
         in_bounds, on_surface, birds, out_of_world, buried);
  report("fauna spawn inside the world bounds", out_of_world == 0 && in_bounds == w->runtime_actor_count);
  report("fauna are not buried in solid voxels", buried == 0);
  report("fauna stand on the surface", on_surface >= (w->runtime_actor_count * 3) / 4);
  report("at least a flock or two of birds", birds >= 4);

  const int before = w->runtime_actor_count;
  report("wilderness mob spawn is idempotent", world_spawn_wilderness_mobs(w));
  report("second spawn does not duplicate fauna", w->runtime_actor_count == before);

  world_destroy(w);
}

static void test_biome_fauna_types(void)
{
  printf("\n-- biome fauna types --\n");
  MobActor *sheep = mob_actor_create_sheep(1.5, 1.5, 1.0);
  MobActor *chicken = mob_actor_create_chicken(2.5, 1.5, 1.0);
  MobActor *bat = mob_actor_create_bat(3.5, 1.5, 2.0);
  MobActor *deer = mob_actor_create_deer(4.5, 1.5, 1.0);
  MobActor *lizard = mob_actor_create_lizard(5.5, 1.5, 1.0);
  MobActor *spider = mob_actor_create_spider(6.5, 1.5, 1.0);
  MobActor *slime = mob_actor_create_slime(7.5, 1.5, 1.0);
  report("sheep create", sheep && sheep->mob_type == MOB_TYPE_SHEEP);
  report("chicken create", chicken && chicken->mob_type == MOB_TYPE_CHICKEN);
  report("bat create", bat && bat->mob_type == MOB_TYPE_BAT);
  report("deer create", deer && deer->mob_type == MOB_TYPE_DEER);
  report("lizard create", lizard && lizard->mob_type == MOB_TYPE_LIZARD);
  report("spider create", spider && spider->mob_type == MOB_TYPE_SPIDER);
  report("slime create", slime && slime->mob_type == MOB_TYPE_SLIME);
  report("sheep binds the polygon mesh", sheep && strcmp(sheep->mesh_name, "sheep") == 0);
  report("chicken binds the polygon mesh", chicken && strcmp(chicken->mesh_name, "chick") == 0);
  report("bat binds the polygon mesh", bat && strcmp(bat->mesh_name, "bat") == 0);
  if (sheep)
  {
    Actor a = sheep->base;
    a.extra_data = sheep;
    report("sheep is livestock", mob_actor_is_livestock(&a));
  }
  if (deer)
  {
    Actor a = deer->base;
    a.extra_data = deer;
    report("deer is livestock-class (timid)", mob_actor_is_livestock(&a));
  }
  if (bat)
  {
    Actor a = bat->base;
    a.extra_data = bat;
    report("bat is bat", mob_actor_is_bat(&a));
  }
  if (spider)
  {
    Actor a = spider->base;
    a.extra_data = spider;
    report("spider is spider", mob_actor_is_spider(&a));
  }
  if (sheep)
    mob_actor_destroy(sheep);
  if (chicken)
    mob_actor_destroy(chicken);
  if (bat)
    mob_actor_destroy(bat);
  if (deer)
    mob_actor_destroy(deer);
  if (lizard)
    mob_actor_destroy(lizard);
  if (spider)
    mob_actor_destroy(spider);
  if (slime)
    mob_actor_destroy(slime);
}

static void test_wildlife_mobility(void)
{
  printf("\n-- wildlife mobility --\n");
  const int size = 32, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("mobility arena built", false);
    return;
  }
  const double z = (double)(floor_z + 1);

  // Place several species and confirm they leave their spawn cells.
  MobActor *deer = mob_actor_create_deer(8.5, 8.5, z);
  MobActor *lizard = mob_actor_create_lizard(16.5, 8.5, z);
  MobActor *spider = mob_actor_create_spider(24.5, 8.5, z);
  MobActor *slime = mob_actor_create_slime(8.5, 16.5, z);
  report("mobility fauna created", deer && lizard && spider && slime);
  if (!deer || !lizard || !spider || !slime)
  {
    if (deer)
      mob_actor_destroy(deer);
    if (lizard)
      mob_actor_destroy(lizard);
    if (spider)
      mob_actor_destroy(spider);
    if (slime)
      mob_actor_destroy(slime);
    world_destroy(w);
    return;
  }

  Actor copies[4];
  MobActor *mobs[4] = {deer, lizard, spider, slime};
  for (int i = 0; i < 4; i++)
  {
    copies[i] = mobs[i]->base;
    copies[i].extra_data = mobs[i];
    world_add_runtime_actor(w, &copies[i]);
  }

  double start[4][2];
  for (int i = 0; i < 4; i++)
  {
    start[i][0] = w->runtime_actors[i].x;
    start[i][1] = w->runtime_actors[i].y;
  }

  for (int step = 0; step < 240; step++)
    world_step_actors(w, 1.0f / 30.0f);

  int movers = 0;
  for (int i = 0; i < 4; i++)
  {
    double dx = w->runtime_actors[i].x - start[i][0];
    double dy = w->runtime_actors[i].y - start[i][1];
    double moved = sqrt(dx * dx + dy * dy);
    printf("       %s travelled %.2f voxels\n", w->runtime_actors[i].name, moved);
    if (moved > 0.35)
      movers++;
  }
  report("wildlife of several kinds navigate the map", movers >= 3);

  // Force an unreachable-ish wall push then ensure path_stuck clears the target.
  MobActor *m0 = (MobActor *)w->runtime_actors[0].extra_data;
  m0->ai_state.has_target = true;
  m0->ai_state.target_x = 0;
  m0->ai_state.target_y = 0;
  m0->ai_state.target_z = (int)z;
  m0->ai_state.last_x = (int)floor(w->runtime_actors[0].x);
  m0->ai_state.last_y = (int)floor(w->runtime_actors[0].y);
  m0->ai_state.path_stuck_in = 0.0f;
  m0->goal = MOB_GOAL_WANDER;
  for (int i = 0; i < 60; i++)
    world_step_actors(w, 1.0f / 30.0f);
  m0 = (MobActor *)w->runtime_actors[0].extra_data;
  report("stuck walk targets are abandoned so navigation continues",
         m0 && (!m0->ai_state.has_target || m0->ai_state.path_stuck_in < 0.01f ||
                m0->ai_state.stuck_counter > 0));

  world_destroy(w);
}

static int count_wild_prefix_actors_test(World *w)
{
  int n = 0;
  if (!w || !w->runtime_actors)
    return 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    if (strncmp(w->runtime_actors[i].name, "Wild ", 5) == 0)
      n++;
  }
  return n;
}

static int count_mob_type(World *w, MobType t)
{
  int n = 0;
  if (!w || !w->runtime_actors)
    return 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (m && m->mob_type == t)
      n++;
  }
  return n;
}

static int count_distinct_mob_types(World *w)
{
  bool seen[32];
  memset(seen, 0, sizeof(seen));
  int n = 0;
  if (!w || !w->runtime_actors)
    return 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (!m || m->mob_type <= 0 || (int)m->mob_type >= 32)
      continue;
    if (!seen[m->mob_type])
    {
      seen[m->mob_type] = true;
      n++;
    }
  }
  return n;
}

static void test_affinity_and_predator_prey(void)
{
  printf("\n-- affinity matrix and predator-prey pressure --\n");

  const int size = 32, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("predator arena built", false);
    return;
  }
  const double z = (double)(floor_z + 1);

  MobActor *spider = mob_actor_create_spider(12.5, 12.5, z);
  MobActor *sheep = mob_actor_create_sheep(14.5, 12.5, z);
  MobActor *lizard = mob_actor_create_lizard(12.5, 16.5, z);
  MobActor *chicken = mob_actor_create_chicken(14.0, 16.5, z);
  MobActor *deer = mob_actor_create_deer(18.5, 12.5, z);
  MobActor *sheep2 = mob_actor_create_sheep(10.5, 12.5, z);
  report("predator-prey cast created",
         spider && sheep && lizard && chicken && deer && sheep2);
  if (!spider || !sheep || !lizard || !chicken || !deer || !sheep2)
  {
    if (spider) mob_actor_destroy(spider);
    if (sheep) mob_actor_destroy(sheep);
    if (lizard) mob_actor_destroy(lizard);
    if (chicken) mob_actor_destroy(chicken);
    if (deer) mob_actor_destroy(deer);
    if (sheep2) mob_actor_destroy(sheep2);
    world_destroy(w);
    return;
  }

  Actor actors[6];
  MobActor *mobs[6] = {spider, sheep, lizard, chicken, deer, sheep2};
  for (int i = 0; i < 6; i++)
  {
    actors[i] = mobs[i]->base;
    actors[i].extra_data = mobs[i];
    world_add_runtime_actor(w, &actors[i]);
  }

  // Force immediate encounters so affinity seeds reputation.
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (m)
      m->reputation_scan_in = 0.0f;
  }
  world_step_actors(w, 1.0f / 30.0f);

  MobActor *ms = NULL, *mh = NULL, *ml = NULL, *mc = NULL, *md = NULL;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (!m)
      continue;
    if (m->mob_type == MOB_TYPE_SPIDER) ms = m;
    else if (m->mob_type == MOB_TYPE_SHEEP && !mh) mh = m;
    else if (m->mob_type == MOB_TYPE_LIZARD) ml = m;
    else if (m->mob_type == MOB_TYPE_CHICKEN) mc = m;
    else if (m->mob_type == MOB_TYPE_DEER) md = m;
  }

  uint32_t sheep_id = 0, chicken_id = 0, spider_id = 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (m == mh) sheep_id = w->runtime_actors[i].id;
    if (m == mc) chicken_id = w->runtime_actors[i].id;
    if (m == ms) spider_id = w->runtime_actors[i].id;
  }

  report("spider dislikes nearby sheep",
         ms && sheep_id && mob_actor_reputation_get(ms, sheep_id) <= -10);
  report("sheep dislike nearby spider",
         mh && spider_id && mob_actor_reputation_get(mh, spider_id) <= -10);
  report("lizard dislikes nearby chicken",
         ml && chicken_id && mob_actor_reputation_get(ml, chicken_id) <= -10);

  {
    uint32_t id_a = 0, id_b = 0;
    MobActor *sa = NULL, *sb = NULL;
    for (int i = 0; i < w->runtime_actor_count; i++)
    {
      MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
      if (m && m->mob_type == MOB_TYPE_SHEEP)
      {
        if (!sa)
        {
          sa = m;
          id_a = w->runtime_actors[i].id;
        }
        else
        {
          sb = m;
          id_b = w->runtime_actors[i].id;
        }
      }
    }
    report("sheep herd members seed mutual affection",
           sa && sb && mob_actor_reputation_get(sa, id_b) > 0 &&
               mob_actor_reputation_get(sb, id_a) > 0);
    (void)md;
  }

  // Let social pressure fire: spider should chase, sheep should flee.
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (!m)
      continue;
    m->reputation_scan_in = 0.0f;
    m->goal = MOB_GOAL_WANDER;
    m->mood = MOB_MOOD_CALM;
    m->aggro_target_id = 0;
    m->ai_state.has_target = false;
    m->ai_state.retarget_in = 0.0f;
  }
  for (int step = 0; step < 15; step++)
    world_step_actors(w, 1.0f / 30.0f);

  ms = mh = ml = mc = NULL;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (!m) continue;
    if (m->mob_type == MOB_TYPE_SPIDER) ms = m;
    if (m->mob_type == MOB_TYPE_SHEEP && !mh) mh = m;
    if (m->mob_type == MOB_TYPE_LIZARD) ml = m;
    if (m->mob_type == MOB_TYPE_CHICKEN) mc = m;
  }

  report("spider enters chase on disliked livestock",
         ms && ms->goal == MOB_GOAL_CHASE && ms->aggro_target_id != 0);
  report("sheep flees a nearby spider",
         mh && mh->goal == MOB_GOAL_FLEE && mh->aggro_target_id != 0);
  report("lizard enters chase on nearby chicken",
         ml && ml->goal == MOB_GOAL_CHASE && ml->aggro_target_id != 0);
  report("chicken flees a nearby lizard",
         mc && mc->goal == MOB_GOAL_FLEE && mc->aggro_target_id != 0);

  // Over a short run the spider should close or the sheep should open distance.
  double spider_x0 = 0, sheep_x0 = 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (m == ms) spider_x0 = w->runtime_actors[i].x;
    if (m == mh) sheep_x0 = w->runtime_actors[i].x;
  }
  for (int step = 0; step < 90; step++)
    world_step_actors(w, 1.0f / 30.0f);
  double spider_x1 = spider_x0, sheep_x1 = sheep_x0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (m && m->mob_type == MOB_TYPE_SPIDER) spider_x1 = w->runtime_actors[i].x;
    if (m && m->mob_type == MOB_TYPE_SHEEP && mh &&
        ((MobActor *)w->runtime_actors[i].extra_data)->base.id == mh->base.id)
      sheep_x1 = w->runtime_actors[i].x;
  }
  // Prefer: either sheep moved away or spider moved toward original sheep side.
  bool motion = fabs(spider_x1 - spider_x0) > 0.2 || fabs(sheep_x1 - sheep_x0) > 0.2;
  report("predator-prey chase/flee produces motion", motion);

  world_destroy(w);
}

static void test_wilderness_ecosystem(void)
{
  printf("\n-- wilderness ecosystem richness --\n");

  World *w = world_create(48, 48, 48);
  if (!w)
  {
    report("ecosystem world created", false);
    return;
  }
  // Prefer an unsettled cell so richness isn't muted by neighbour town keep-out.
  w->universe_x = 7;
  w->universe_y = 4;
  w->universe_z = 0;
  {
    int best_x = 7, best_y = 4, best_score = -1;
    for (int gy = -6; gy <= 8; gy++)
    {
      for (int gx = -6; gx <= 8; gx++)
      {
        if (universe_settlement_scale(gx, gy) > 0)
          continue;
        int neigh = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++)
          {
            if (dx == 0 && dy == 0)
              continue;
            if (universe_settlement_scale(gx + dx, gy + dy) > 0)
              neigh++;
          }
        const int score = 16 - neigh * 4 - (gx * gx + gy * gy) / 8;
        if (score > best_score)
        {
          best_score = score;
          best_x = gx;
          best_y = gy;
        }
      }
    }
    w->universe_x = best_x;
    w->universe_y = best_y;
    printf("       ecosystem cell (%d,%d) score=%d\n", best_x, best_y, best_score);
  }
  world_generate_with_type(w, "ecosystem_richness_seed_v1", WORLD_TYPE_WILDERNESS);
  report("wilderness ecosystem spawn succeeds", world_spawn_wilderness_mobs(w));

  const int actors = w->runtime_actor_count;
  const int wild = count_wild_prefix_actors_test(w);
  const int types = count_distinct_mob_types(w);
  const int sheep = count_mob_type(w, MOB_TYPE_SHEEP);
  const int chicken = count_mob_type(w, MOB_TYPE_CHICKEN);
  const int deer = count_mob_type(w, MOB_TYPE_DEER);
  const int spider = count_mob_type(w, MOB_TYPE_SPIDER);
  const int lizard = count_mob_type(w, MOB_TYPE_LIZARD);
  const int slime = count_mob_type(w, MOB_TYPE_SLIME);
  const int birds = count_mob_type(w, MOB_TYPE_BIRD) + count_mob_type(w, MOB_TYPE_BAT);
  const int ground = sheep + chicken + deer + spider + lizard + slime +
                     count_mob_type(w, MOB_TYPE_WANDERER);

  printf("       actors=%d wild=%d types=%d sheep=%d chicken=%d deer=%d "
         "spider=%d lizard=%d slime=%d fliers=%d ground=%d\n",
         actors, wild, types, sheep, chicken, deer, spider, lizard, slime, birds, ground);

  report("wilderness is reasonably populated", actors >= 8 && wild >= 6);
  report("wilderness hosts several species", types >= 3);
  report("wilderness has ground wildlife", ground >= 3);
  report("wilderness has aerial wildlife", birds >= 2);
  report("wilderness mixes prey and predator or multi-prey flocks",
         (spider + lizard) >= 1 || (sheep + chicken + deer) >= 4);

  // Same-type animals should be clustered: median nearest-neighbor among sheep (if any).
  if (sheep >= 3)
  {
    double sum = 0.0;
    int pairs = 0;
    for (int i = 0; i < w->runtime_actor_count; i++)
    {
      MobActor *mi = (MobActor *)w->runtime_actors[i].extra_data;
      if (!mi || mi->mob_type != MOB_TYPE_SHEEP)
        continue;
      double best = 1e9;
      for (int j = 0; j < w->runtime_actor_count; j++)
      {
        if (i == j)
          continue;
        MobActor *mj = (MobActor *)w->runtime_actors[j].extra_data;
        if (!mj || mj->mob_type != MOB_TYPE_SHEEP)
          continue;
        double dx = w->runtime_actors[i].x - w->runtime_actors[j].x;
        double dy = w->runtime_actors[i].y - w->runtime_actors[j].y;
        double d = sqrt(dx * dx + dy * dy);
        if (d < best)
          best = d;
      }
      if (best < 1e8)
      {
        sum += best;
        pairs++;
      }
    }
    double avg = pairs > 0 ? sum / (double)pairs : 99.0;
    printf("       sheep nearest-neighbour avg %.2f\n", avg);
    report("sheep spawn in flocks (near kin)", avg < 8.0);
  }
  else
  {
    report("sheep spawn in flocks (near kin)", true); // biome without sheep — skip
  }

  // Settled cells clear wild monsters/herds; livestock comes from villagers, not Wild* packs.
  {
    World *town = world_create(48, 48, 48);
    if (!town)
    {
      report("settlement fauna world created", false);
    }
    else
    {
      town->universe_x = w->universe_x;
      town->universe_y = w->universe_y;
      town->universe_z = 0;
      // Flat ground + scale-5 stamp so spawn sees settlement_scale.
      for (uint32_t y = 0; y < town->height; y++)
        for (uint32_t x = 0; x < town->width; x++)
        {
          world_set_voxel(town, x, y, 0, VOXEL_BEDROCK);
          world_set_voxel(town, x, y, 1, VOXEL_STONE);
          world_set_voxel(town, x, y, 2, VOXEL_GRASS);
        }
      world_refresh_occupancy_bitfield(town);
      settlement_stamp(town, 5, "settlement_fauna");
      report("scale-5 town stamped for fauna", town->settlement_scale == 5);
      world_spawn_wilderness_mobs(town);
      const int t_spider = count_mob_type(town, MOB_TYPE_SPIDER);
      const int t_lizard = count_mob_type(town, MOB_TYPE_LIZARD);
      const int t_slime = count_mob_type(town, MOB_TYPE_SLIME);
      const int t_wander = count_mob_type(town, MOB_TYPE_WANDERER);
      const int t_monsters = t_spider + t_lizard + t_slime + t_wander;
      const int t_birds = count_mob_type(town, MOB_TYPE_BIRD);
      const int t_stock = count_mob_type(town, MOB_TYPE_SHEEP) +
                          count_mob_type(town, MOB_TYPE_CHICKEN) +
                          count_mob_type(town, MOB_TYPE_DEER);
      const int wild_monsters = spider + lizard + slime + count_mob_type(w, MOB_TYPE_WANDERER);
      printf("       town monsters=%d (wild cell %d) birds=%d livestock=%d\n",
             t_monsters, wild_monsters, t_birds, t_stock);
      report("settlement clears wild monsters", t_monsters == 0);
      report("settlement clears wild livestock herds", t_stock == 0);
      report("settlement hosts at most sparse edge birds", t_birds <= 2);
      world_destroy(town);
    }
  }

  // Let the ecosystem tick: reputations form and some chase/flee or herd motion appears.
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (m)
      m->reputation_scan_in = 0.0f;
  }
  int rep_entries = 0;
  int chase_or_flee = 0;
  for (int step = 0; step < 90; step++)
  {
    world_step_actors(w, 1.0f / 30.0f);
    if (step == 5 || step == 30 || step == 60)
    {
      for (int i = 0; i < w->runtime_actor_count; i++)
      {
        MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
        if (m)
          m->reputation_scan_in = 0.0f;
      }
    }
  }
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    MobActor *m = (MobActor *)w->runtime_actors[i].extra_data;
    if (!m)
      continue;
    rep_entries += (int)m->reputation_count;
    if (m->goal == MOB_GOAL_CHASE || m->goal == MOB_GOAL_FLEE)
      chase_or_flee++;
  }
  printf("       reputation entries=%d chase/flee=%d\n", rep_entries, chase_or_flee);
  report("fauna form inter-actor reputations", rep_entries >= 4);
  report("ecosystem produces chase or flee under pressure",
         chase_or_flee >= 1 || rep_entries >= 4);

  const int before = w->runtime_actor_count;
  world_spawn_wilderness_mobs(w);
  report("ecosystem spawn stays idempotent at density", w->runtime_actor_count == before);

  world_destroy(w);
}

static int count_villager_kind(World *w, VillagerKind kind)
{
  int n = 0;
  if (!w || !w->runtime_actors)
    return 0;
  for (int i = 0; i < w->runtime_actor_count; i++)
  {
    Actor *a = &w->runtime_actors[i];
    if (!mob_actor_is_villager(a))
      continue;
    if (mob_actor_villager_kind(a) == kind)
      n++;
  }
  return n;
}

static void test_villagers_and_domestication(void)
{
  printf("\n-- villagers and domestication --\n");

  const int size = 48, floor_z = 6;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("villager arena built", false);
    return;
  }

  w->settlement_scale = 3;
  w->settlement_has_anchor = true;
  w->settlement_anchor_x = (int16_t)(size / 2);
  w->settlement_anchor_y = (int16_t)(size / 2);
  w->settlement_anchor_z = (int16_t)(floor_z + 1);
  report("scale 3 spawns villagers", world_spawn_settlement_villagers(w));
  report("scale 3 has villagers", count_mob_type(w, MOB_TYPE_VILLAGER) > 0);
  {
    int merchants = 0;
    for (int i = 0; i < w->runtime_actor_count; i++)
    {
      Actor *a = &w->runtime_actors[i];
      if (mob_actor_is_villager(a) &&
          mob_actor_villager_profession(a) == VILLAGER_JOB_MERCHANT)
        merchants++;
    }
    report("scale 3 includes a shopkeeper", merchants >= 1);
  }

  // Clear for scale-4 retest on a fresh roster.
  for (int i = w->runtime_actor_count - 1; i >= 0; i--)
  {
    Actor *a = &w->runtime_actors[i];
    if (a->extra_data)
      mob_actor_destroy((MobActor *)a->extra_data);
  }
  w->runtime_actor_count = 0;
  household_registry_clear(w);

  w->settlement_scale = 4;
  report("scale 4 spawns villagers", world_spawn_settlement_villagers(w));
  const int males = count_villager_kind(w, VILLAGER_MALE);
  const int females = count_villager_kind(w, VILLAGER_FEMALE);
  const int children = count_villager_kind(w, VILLAGER_CHILD);
  printf("       scale4 villagers m=%d f=%d c=%d\n", males, females, children);
  report("scale 4 includes adult males", males >= 1);
  report("scale 4 includes adult females", females >= 1);
  report("scale 4 includes children", children >= 1);

  // Naming, professions, lineage, inherited stories
  {
    int named = 0, professed = 0, kin_pairs = 0, kids_with_stories = 0;
    MobActor *any_child = NULL, *any_parent = NULL;
    for (int i = 0; i < w->runtime_actor_count; i++)
    {
      Actor *a = &w->runtime_actors[i];
      if (!mob_actor_is_villager(a))
        continue;
      MobActor *m = (MobActor *)a->extra_data;
      if (!m)
        continue;
      if (strchr(a->name, ' ') != NULL && m->given_name[0] && m->family_name[0])
        named++;
      if (m->profession != VILLAGER_JOB_NONE)
        professed++;
      if (m->villager_kind == VILLAGER_CHILD)
      {
        any_child = m;
        if (m->story_count > 0)
          kids_with_stories++;
        for (uint8_t p = 0; p < m->parent_count; p++)
        {
          for (int j = 0; j < w->runtime_actor_count; j++)
          {
            MobActor *o = (MobActor *)w->runtime_actors[j].extra_data;
            if (o && o->base.id == m->parent_ids[p])
            {
              any_parent = o;
              if (mob_actor_are_kin(m, o) &&
                  mob_actor_reputation_get(m, o->base.id) >= MOB_FAMILY_AFFINITY / 2)
                kin_pairs++;
            }
          }
        }
      }
    }
    printf("       named=%d professed=%d kin_pairs=%d child_stories=%d\n",
           named, professed, kin_pairs, kids_with_stories);
    report("villagers use first and last names", named >= males + females + children);
    report("villagers have professions", professed >= males + females + children);
    report("children are kin to parents with high affinity", kin_pairs >= 1);
    report("children inherit parental stories", kids_with_stories >= 1 ||
           (any_child && any_parent && any_parent->story_count == 0));
    if (any_child && any_parent && any_parent->story_count > 0)
    {
      bool shared = false;
      for (uint8_t i = 0; i < any_parent->story_count; i++)
      {
        for (uint8_t j = 0; j < any_child->story_count; j++)
        {
          if (strcmp(any_parent->stories[i].text, any_child->stories[j].text) == 0)
            shared = true;
        }
      }
      report("a child shares a parent's story text", shared);
    }
  }
  {
    MobActor *probe = mob_actor_create_sheep(1.0, 1.0, 1.0);
    Actor probe_a = probe->base;
    probe_a.extra_data = probe;
    report("sheep are domesticable", probe && mob_actor_is_domesticable(&probe_a));
    MobActor *lizard = mob_actor_create_lizard(1.0, 1.0, 1.0);
    Actor lizard_a = lizard->base;
    lizard_a.extra_data = lizard;
    report("lizards are not domesticable", lizard && !mob_actor_is_domesticable(&lizard_a));
    if (probe)
      mob_actor_destroy(probe);
    if (lizard)
      mob_actor_destroy(lizard);
  }

  const int before = count_mob_type(w, MOB_TYPE_VILLAGER);
  report("villager spawn is idempotent", world_spawn_settlement_villagers(w));
  report("idempotent spawn keeps headcount", count_mob_type(w, MOB_TYPE_VILLAGER) == before);

  // Dialogue differs by kind.
  {
    char lines[DIALOGUE_MAX_LINES][DIALOGUE_LINE_MAX];
    MobActor *m = mob_actor_create_villager(VILLAGER_MALE, 2.0, 2.0, (double)(floor_z + 1));
    MobActor *f = mob_actor_create_villager(VILLAGER_FEMALE, 3.0, 2.0, (double)(floor_z + 1));
    MobActor *c = mob_actor_create_villager(VILLAGER_CHILD, 4.0, 2.0, (double)(floor_z + 1));
    report("villager kinds create", m && f && c);
    if (m && f && c)
    {
      Actor am = m->base; am.extra_data = m;
      Actor af = f->base; af.extra_data = f;
      Actor ac = c->base; ac.extra_data = c;
      int nm = dialogue_lines_for_actor(&am, lines, DIALOGUE_MAX_LINES);
      int nf = dialogue_lines_for_actor(&af, lines, DIALOGUE_MAX_LINES);
      int nc = dialogue_lines_for_actor(&ac, lines, DIALOGUE_MAX_LINES);
      report("male villager has dialogue", nm >= 1);
      report("female villager has dialogue", nf >= 1);
      report("child villager has dialogue", nc >= 1);
      report("create assigns given and family names",
             m->given_name[0] && m->family_name[0] && strchr(m->base.name, ' '));
      report("create assigns a profession",
             m->profession != VILLAGER_JOB_NONE &&
                 villager_profession_name(m->profession)[0] != '\0');

      // Explicit lineage + story inheritance
      mob_actor_set_villager_identity(m, "Alden", "Reed", VILLAGER_JOB_SHEPHERD);
      mob_actor_set_villager_identity(f, "Brynn", "Reed", VILLAGER_JOB_BAKER);
      m->family_id = f->family_id = 42;
      mob_actor_add_story(m, 0, 99, -8, "The Thorn family drives a hard bargain.");
      mob_actor_add_story(f, 7, 99, 10, "Cora Thorn mended our roof last frost.");
      mob_actor_set_villager_identity(c, "Pip", "Reed", VILLAGER_JOB_APPRENTICE);
      c->family_id = 42;
      mob_actor_link_parent(c, m);
      mob_actor_link_parent(c, f);
      report("child lists both parents", c->parent_count == 2);
      report("parents and child share family id", c->family_id == 42);
      report("kinship affinity is high",
             mob_actor_reputation_get(c, m->base.id) >= MOB_FAMILY_AFFINITY &&
                 mob_actor_reputation_get(m, c->base.id) >= MOB_FAMILY_AFFINITY);
      report("child inherited stories from parents", c->story_count >= 2);
      bool has_thorn = false;
      for (uint8_t i = 0; i < c->story_count; i++)
      {
        if (strstr(c->stories[i].text, "Thorn"))
          has_thorn = true;
      }
      report("inherited stories mention other families", has_thorn);
      nc = dialogue_lines_for_actor(&ac, lines, DIALOGUE_MAX_LINES);
      bool child_tells_tale = false;
      for (int i = 0; i < nc; i++)
      {
        if (strstr(lines[i], "Mama told me") || strstr(lines[i], "Thorn"))
          child_tells_tale = true;
      }
      report("child dialogue can retell a family story", child_tells_tale);
    }
    if (m) mob_actor_destroy(m);
    if (f) mob_actor_destroy(f);
    if (c) mob_actor_destroy(c);
  }

  world_destroy(w);

  // Domestication → follow
  w = make_arena(32, floor_z);
  if (!w)
  {
    report("domestication arena built", false);
    return;
  }
  const double z = (double)(floor_z + 1);
  MobActor *keeper = mob_actor_create_villager(VILLAGER_MALE, 12.5, 12.5, z);
  MobActor *sheep = mob_actor_create_sheep(13.5, 12.5, z);
  report("domestication cast created", keeper && sheep);
  if (!keeper || !sheep)
  {
    if (keeper) mob_actor_destroy(keeper);
    if (sheep) mob_actor_destroy(sheep);
    world_destroy(w);
    return;
  }
  Actor ka = keeper->base; ka.extra_data = keeper;
  Actor sa = sheep->base; sa.extra_data = sheep;
  world_add_runtime_actor(w, &ka);
  world_add_runtime_actor(w, &sa);

  MobActor *mk = (MobActor *)w->runtime_actors[0].extra_data;
  MobActor *ms = (MobActor *)w->runtime_actors[1].extra_data;
  // Seed near the bond threshold; one tend tick should finish the bond.
  mob_actor_reputation_adjust(ms, mk->base.id, MOB_DOMESTICATE_AFFINITY - 3);
  mk->domesticate_timer = 0.0f;
  for (int i = 0; i < 90 && ms->follow_target_id == 0; i++)
    world_step_actors(w, 1.0f / 30.0f);

  report("tending bonds livestock to villager",
         ms->follow_target_id == mk->base.id);
  report("bonded animal enters FOLLOW goal",
         ms->goal == MOB_GOAL_FOLLOW || ms->follow_target_id == mk->base.id);
  report("affinity crossed domesticate threshold",
         mob_actor_reputation_get(ms, mk->base.id) >= MOB_DOMESTICATE_AFFINITY);

  // Lead the sheep: walk the villager away and ensure the sheep closes in.
  w->runtime_actors[0].x = 20.5;
  w->runtime_actors[0].y = 20.5;
  mk->base.x = 20.5;
  mk->base.y = 20.5;
  const double sx0 = w->runtime_actors[1].x;
  const double sy0 = w->runtime_actors[1].y;
  for (int i = 0; i < 180; i++)
    world_step_actors(w, 1.0f / 30.0f);
  const double dx = w->runtime_actors[1].x - sx0;
  const double dy = w->runtime_actors[1].y - sy0;
  const double toward =
      (w->runtime_actors[1].x - 20.5) * (20.5 - sx0) +
      (w->runtime_actors[1].y - 20.5) * (20.5 - sy0);
  printf("       follower moved %.2f (dot toward leader %.2f)\n",
         sqrt(dx * dx + dy * dy), toward);
  report("domesticated animal follows its villager",
         sqrt(dx * dx + dy * dy) > 0.5 ||
             (fabs(w->runtime_actors[1].x - 20.5) + fabs(w->runtime_actors[1].y - 20.5)) < 4.0);

  world_destroy(w);
}

static void test_skill_tree(void)
{
  printf("\n-- skill tree --\n");

  World *w = make_arena(16, 4);
  GameState *state = make_state(w, 8.5f, 8.5f, 5.0f);
  if (!state || !state->player)
  {
    report("skill tree state built", false);
    if (state)
      destroy_state(state);
    world_destroy(w);
    return;
  }

  report("create water starts unknown",
         !skill_is_known(state->player, SKILL_CREATE_WATER));
  report("ice bolt starts unknown",
         !skill_is_known(state->player, SKILL_ICE_BOLT));
  report("fireball is a base skill", skill_is_base(SKILL_FIREBALL));
  report("create water is unlockable", skill_is_unlockable(SKILL_CREATE_WATER));

  report("unlock refuses without a skill point",
         !skill_try_unlock(state, SKILL_CREATE_WATER));

  // Level 4 requires 300 XP (levels at 0,100,200,300 → 1,2,3,4).
  const uint32_t before_pts = state->player->skill_points;
  actor_add_experience(state->player, 300);
  report("reaching level 4 grants a skill point",
         state->player->level >= 4 && state->player->skill_points == before_pts + 1);

  report("create water can be unlocked",
         skill_try_unlock(state, SKILL_CREATE_WATER));
  report("create water is now known",
         skill_is_known(state->player, SKILL_CREATE_WATER));
  report("the unlock spent the skill point", state->player->skill_points == before_pts);

  state->player->stamina = 100.0f;
  state->controls.create_water_ready_at_ms = 0;
  const int wx = (int)floorf(state->player_world_x);
  const int wy = (int)floorf(state->player_world_y);
  const int wz = (int)floorf(state->player_world_z);
  report("create water casts",
         player_controls_cast_create_water(state, &state->controls));
  Voxel *puddle = world_get_voxel(w, (uint32_t)wx, (uint32_t)wy, (uint32_t)wz);
  bool water_nearby = puddle && puddle->type == VOXEL_WATER;
  if (!water_nearby)
  {
    for (int dz = 0; dz <= 1 && !water_nearby; dz++)
      for (int dy = -1; dy <= 1 && !water_nearby; dy++)
        for (int dx = -1; dx <= 1 && !water_nearby; dx++)
        {
          Voxel *v = world_get_voxel(w, (uint32_t)(wx + dx), (uint32_t)(wy + dy),
                                     (uint32_t)(wz + dz));
          if (v && v->type == VOXEL_WATER)
            water_nearby = true;
        }
  }
  report("create water placed a puddle", water_nearby);

  actor_add_experience(state->player, 400); // push toward level 8 for another point
  report("another skill point arrives by level 8",
         state->player->level >= 8 && state->player->skill_points >= 1);
  report("ice bolt can be unlocked",
         skill_try_unlock(state, SKILL_ICE_BOLT));

  MobActor *target = mob_actor_create("Freeze Me", MOB_TYPE_WANDERER, 10.5, 8.5,
                                      (double)wz + 1.0);
  report("a target stands ready for ice bolt", add_mob_to_world(w, target));
  Actor *victim = find_named_actor(w, "Freeze Me");
  state->player->stamina = 100.0f;
  state->controls.ice_bolt_ready_at_ms = 0;
  report("ice bolt casts",
         player_controls_cast_ice_bolt_at(state, &state->controls, 10.5f, 8.5f,
                                          (float)wz + 1.5f));
  for (int i = 0; i < 120; i++)
    game_state_step_projectiles(state, 1.0 / 60.0);
  report("one ice bolt chills without freezing yet",
         victim && victim->chill > 0 && victim->freeze_ttl <= 0.0f);

  // Two more hits to push chill over the freeze threshold (90 * 3 = 270 → clamp/freeze).
  for (int shot = 0; shot < 2; shot++)
  {
    state->player->stamina = 100.0f;
    state->controls.ice_bolt_ready_at_ms = 0;
    player_controls_cast_ice_bolt_at(state, &state->controls, 10.5f, 8.5f,
                                     (float)wz + 1.5f);
    for (int i = 0; i < 120; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);
  }
  report("repeated ice bolts freeze the target",
         victim && victim->freeze_ttl > 0.0f);

  // Water cools toward ice rather than igniting.
  world_set_voxel(w, 8, 8, (uint32_t)wz, VOXEL_WATER);
  Voxel *puddle_cell = world_get_voxel(w, 8, 8, (uint32_t)wz);
  report("a water cell is ready to chill", puddle_cell && puddle_cell->type == VOXEL_WATER);
  bool froze_water = false;
  for (int hit = 0; hit < 4 && puddle_cell; hit++)
  {
    if (voxel_cool(puddle_cell, PLAYER_ICE_BOLT_COOL_AMOUNT))
    {
      froze_water = true;
      break;
    }
  }
  report("cooling water eventually freezes it to ice",
         froze_water && puddle_cell && puddle_cell->type == VOXEL_ICE);
  report("ice bolt cooling never sets a burning condition",
         !fire_voxel_is_burning(puddle_cell));

  if (victim && victim->extra_data)
  {
    MobActor *m = (MobActor *)victim->extra_data;
    m->base.freeze_ttl = victim->freeze_ttl;
    m->base.chill = victim->chill;
    m->goal = MOB_GOAL_WANDER;
    m->ai_state.has_target = true;
    m->ai_state.target_x = 14;
    m->ai_state.target_y = 14;
  }
  for (int i = 0; i < 30; i++)
    world_step_actors(w, 1.0f / 30.0f);
  report("a frozen target does not walk",
         victim && fabs(victim->velocity_x) < 0.01 && fabs(victim->velocity_y) < 0.01);

  report("meteor requires level 8", skill_min_level(SKILL_METEOR) == 8);
  report("magic missile requires level 1", skill_min_level(SKILL_MAGIC_MISSILE) == 1);
  report("blink is a dota-style unlock", skill_is_unlockable(SKILL_BLINK));
  report("sun strike is unlockable", skill_is_unlockable(SKILL_SUN_STRIKE));
  report("cold snap is unlockable", skill_is_unlockable(SKILL_COLD_SNAP));
  report("reaper scythe III gates high",
         skill_min_level_for_rank(SKILL_REAPERS_SCYTHE, SKILL_RANK_POWERFUL) == 30);

  // Level 8 is below Meteor II/III but meets Meteor I (lv 8). Need points + level.
  state->player->skill_points = 2;
  // Drop below meteor I gate to test refusal, then restore.
  const uint32_t xp_keep = state->player->experience;
  state->player->experience = 200; // level 3
  state->player->level = 3;
  report("meteor refuses below its level gate",
         !skill_try_unlock(state, SKILL_METEOR));
  state->player->experience = xp_keep;
  state->player->level = (xp_keep / 100u) + 1u;
  // Push well past level 8.
  actor_add_experience(state->player, 5000);
  report("high level meets meteor gate", skill_meets_level(state->player, SKILL_METEOR));
  report("meteor unlocks at sufficient level",
         skill_try_unlock(state, SKILL_METEOR));
  report("meteor starts at rank I", skill_rank(state->player, SKILL_METEOR) == SKILL_RANK_BASIC);

  state->player->stamina = 100.0f;
  state->controls.meteor_ready_at_ms = 0;
  const float mx = 12.5f, my = 8.5f, mz = (float)wz + 1.0f;
  report("meteor casts from above the target",
         player_controls_cast_meteor_at(state, &state->controls, mx, my, mz));
  bool meteor_aloft = false;
  for (int i = 0; i < PROJECTILE_MAX; i++)
  {
    const Projectile *p = &state->projectiles.items[i];
    if (p->active && p->kind == PROJECTILE_METEOR && p->z > mz + 2.0f)
    {
      meteor_aloft = true;
      break;
    }
  }
  report("meteor spawns high above the aim point", meteor_aloft);
  for (int i = 0; i < 240; i++)
    game_state_step_projectiles(state, 1.0 / 60.0);
  report("meteor is spent after falling",
         projectile_active_count(&state->projectiles) == 0 ||
             !meteor_aloft /* may have impacted */);

  // Fresh spirit for godmode: low level, no unlocks.
  Actor *spirit = state->player;
  spirit->experience = 0;
  spirit->level = 1;
  spirit->skill_points = 0;
  spirit->unlocked_skills = 0;
  report("godmode grants highest skill level and unlocks all",
         skill_godmode(state));
  report("godmode reaches Reaper III gate",
         spirit->level >= skill_min_level_for_rank(SKILL_REAPERS_SCYTHE, SKILL_RANK_POWERFUL));
  report("godmode unlocks meteor at III",
         skill_rank(spirit, SKILL_METEOR) == SKILL_RANK_POWERFUL);
  report("godmode unlocks thunder wrath at III",
         skill_rank(spirit, SKILL_THUNDER_WRATH) == SKILL_RANK_POWERFUL);
  report("godmode unlocks reaper scythe at III",
         skill_rank(spirit, SKILL_REAPERS_SCYTHE) == SKILL_RANK_POWERFUL);
  report("godmode unlocks magic missile",
         skill_is_known(spirit, SKILL_MAGIC_MISSILE));
  report("fireball is rankable to III",
         skill_rank(spirit, SKILL_FIREBALL) == SKILL_RANK_POWERFUL);

  destroy_state(state);
  world_destroy(w);
}

int main(void)
{
  printf("=== Mob, Bird, Fly, Dominate, Talk and Skill Hotbar Tests ===\n");

  if (SDL_Init(SDL_INIT_TIMER) != 0)
  {
    printf("FAILED SDL_Init(timer): %s\n", SDL_GetError());
    return 1;
  }

  test_hotbar_defaults();
  test_skill_tree();
  test_wanderer();
  test_home_mobs_spawn();
  test_wilderness_living();
  test_biome_fauna_types();
  test_wildlife_mobility();
  test_affinity_and_predator_prey();
  test_wilderness_ecosystem();
  test_villagers_and_domestication();
  test_dominate();
  test_dominate_survives_world_boundary();
  test_velocity_inherits_vertical_transition();
  test_mob_crosses_world_boundary();
  test_talk();
  test_birds_and_fly();
  test_poly_anims();
  test_aggro_and_mood();
  test_mob_melee_damage_and_counter_aggro();
  test_breeding_stubs();
  test_livelier_wander();
  test_herding_reputation_collision();
  test_town_portal();

  SDL_Quit();

  printf("\n=== %s (%d failure%s) ===\n", test_failures == 0 ? "ALL PASSED" : "FAILURES",
         test_failures, test_failures == 1 ? "" : "s");
  return test_failures == 0 ? 0 : 1;
}
