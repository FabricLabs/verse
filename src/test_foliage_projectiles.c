// Foliage passability, projectile flight, and the spirit's fireball.
//
// Three things that came in together and are checked together because they meet: foliage stops
// blocking a body but only sometimes stops a projectile, and the fireball is the projectile that has
// to fly through it. Built against real worlds and the real GameState struct, which is plain data, so
// nothing here needs a window or an audio device.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "actor.h"
#include "console.h"
#include "constants.h"
#include "debris.h"
#include "game_state.h"
#include "item.h"
#include "player_controls.h"
#include "projectile.h"
#include "universe.h"
#include "voxel_combat.h"
#include "voxel_fracture.h"
#include "voxel_debris_volume.h"
#include "world.h"

// The client's UI reaches back into verse_client.c for these; standing in for them is what lets the
// test link the real gameplay code without the client's main(). Nothing here renders, so none of
// them are called.
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

// A small solid-floored room to fire across and walk around in.
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
  projectile_system_reset(&state->projectiles, 0x1234u);

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

// ---------------------------------------------------------------------------------------------
// Passability
// ---------------------------------------------------------------------------------------------

static void test_classification(void)
{
  printf("\n-- voxel passability --\n");

  static const VoxelType leaves[] = {
      VOXEL_LEAVES,           VOXEL_LEAVES_OAK,    VOXEL_LEAVES_BEECH,  VOXEL_LEAVES_BIRCH,
      VOXEL_LEAVES_PINE,      VOXEL_LEAVES_PECAN,  VOXEL_LEAVES_LOCUST, VOXEL_LEAVES_MAPLE,
      VOXEL_LEAVES_ELM,       VOXEL_LEAVES_HAZELNUT, VOXEL_LEAVES_CHESTNUT,
      VOXEL_LEAVES_WILLOW,    VOXEL_LEAVES_WALNUT, VOXEL_LEAVES_ACACIA,
      VOXEL_LEAVES_COTTONWOOD, VOXEL_LEAVES_CYPRESS, VOXEL_LEAVES_SPRUCE,
      VOXEL_LEAVES_JUNIPER,   VOXEL_LEAVES_REDWOOD};

  bool all_leaves_pass = true;
  bool all_leaves_foliage = true;
  for (size_t i = 0; i < sizeof(leaves) / sizeof(leaves[0]); i++)
  {
    if (world_voxel_type_blocks_movement(leaves[i]))
      all_leaves_pass = false;
    if (!world_voxel_type_is_foliage(leaves[i]))
      all_leaves_foliage = false;
  }
  report("all nineteen species of leaf let a body through", all_leaves_pass);
  report("all nineteen species of leaf count as foliage", all_leaves_foliage);

  report("tall grass lets a body through", !world_voxel_type_blocks_movement(VOXEL_GRASS_TALL));
  report("air lets a body through", !world_voxel_type_blocks_movement(VOXEL_AIR));

  report("water no longer stops the player at the shoreline",
         !world_voxel_type_blocks_movement(VOXEL_WATER));
  report("steam lets a body through", !world_voxel_type_blocks_movement(VOXEL_STEAM));
  report("gas lets a body through", !world_voxel_type_blocks_movement(VOXEL_GAS));
  report("oil lets a body through", !world_voxel_type_blocks_movement(VOXEL_OIL));

  // The other half of the classification, which matters just as much: making foliage passable must
  // not have made the world passable.
  report("magma still blocks, since nothing damages the player in it yet",
         world_voxel_type_blocks_movement(VOXEL_MAGMA));
  report("ground grass still blocks, so the island is still standable",
         world_voxel_type_blocks_movement(VOXEL_GRASS) &&
             world_voxel_type_blocks_movement(VOXEL_GRASS_WIDE) &&
             world_voxel_type_blocks_movement(VOXEL_GRASS_MOSS));
  report("bushes still block", world_voxel_type_blocks_movement(VOXEL_BUSH) &&
                                  world_voxel_type_blocks_movement(VOXEL_BUSH_FERN));
  report("trunks still block", world_voxel_type_blocks_movement(VOXEL_WOOD) &&
                                  world_voxel_type_blocks_movement(VOXEL_WOOD_OAK));
  report("stone still blocks", world_voxel_type_blocks_movement(VOXEL_STONE));
  report("a bush is not foliage, so it keeps its collision",
         !world_voxel_type_is_foliage(VOXEL_BUSH));
}

static void test_walking_through_foliage(void)
{
  printf("\n-- walking through foliage --\n");

  const int size = 24, floor_z = 8;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  const int stand_z = floor_z + 1;

  // A hedge of leaves across the player's path, two voxels tall so it covers body and head.
  for (int y = 0; y < size; y++)
  {
    world_set_voxel(w, 14, (uint32_t)y, (uint32_t)stand_z, VOXEL_LEAVES_OAK);
    world_set_voxel(w, 14, (uint32_t)y, (uint32_t)(stand_z + 1), VOXEL_LEAVES_OAK);
  }
  // A wall of trunk, likewise, further along.
  for (int y = 0; y < size; y++)
  {
    world_set_voxel(w, 18, (uint32_t)y, (uint32_t)stand_z, VOXEL_WOOD_OAK);
    world_set_voxel(w, 18, (uint32_t)y, (uint32_t)(stand_z + 1), VOXEL_WOOD_OAK);
  }
  // A tuft of tall grass in the open.
  world_set_voxel(w, 11, 12, (uint32_t)stand_z, VOXEL_GRASS_TALL);

  GameState *state = make_state(w, 8.5f, 12.5f, (float)stand_z);
  if (!state)
  {
    report("game state built", false);
    world_destroy(w);
    return;
  }

  report("the player may step into a leaf voxel", game_state_can_move_to(state, 14, 12, stand_z));
  report("the player may step into tall grass", game_state_can_move_to(state, 11, 12, stand_z));
  report("the player may not step into a trunk", !game_state_can_move_to(state, 18, 12, stand_z));

  // And the same through the collision volume the movement code actually uses, which tests the
  // player's whole cylinder rather than one voxel.
  report("the player's body fits inside a hedge of leaves",
         player_controls_can_occupy(state, 14.5f, 12.5f, (float)stand_z));
  report("the player's body does not fit inside a trunk",
         !player_controls_can_occupy(state, 18.5f, 12.5f, (float)stand_z));

  // Walk east into and through the hedge, then confirm the trunk wall stops them.
  state->controls.move_right = false;
  state->controls.move_left = false;
  for (int i = 0; i < 600; i++)
  {
    state->controls.aim_yaw = 0.0f;   // +X
    state->controls.facing_yaw = 0.0f;
    state->controls.move_forward = true;
    player_controls_apply_wasd(state, &state->controls, 1.0 / 60.0);
    game_state_apply_gravity(state, 1.0 / 60.0);
    game_state_sync_positions(state);
  }

  const float reached = state->player_world_x;
  printf("       walked to x=%.2f\n", reached);
  report("the player walks past where the leaf hedge stood", reached > 15.0f);
  report("the player is stopped by the trunk wall behind it", reached < 18.0f);

  // Leaves must not hold the player up either, or a canopy becomes a floor.
  World *w2 = make_arena(size, floor_z);
  if (w2)
  {
    const int high_z = floor_z + 6;
    world_set_voxel(w2, 12, 12, (uint32_t)high_z, VOXEL_LEAVES_PINE);

    GameState *s2 = make_state(w2, 12.5f, 12.5f, (float)(high_z + 1));
    if (s2)
    {
      for (int i = 0; i < 600; i++)
      {
        game_state_apply_gravity(s2, 1.0 / 60.0);
        game_state_sync_positions(s2);
      }
      // A standing player rests at the centre of the voxel they occupy, so the voxel index is the
      // meaningful thing to assert rather than the fractional height.
      const int landed_voxel = (int)floorf(s2->player_world_z);
      printf("       fell from z=%d past the leaf at %d, landed in voxel %d (floor top %d)\n",
             high_z + 1, high_z, landed_voxel, floor_z);
      report("a leaf voxel does not hold the player up", landed_voxel < high_z);
      report("the player lands on the stone floor instead", landed_voxel == floor_z + 1);
      destroy_state(s2);
    }
    world_destroy(w2);
  }

  destroy_state(state);
  world_destroy(w);
}

// ---------------------------------------------------------------------------------------------
// Projectiles
// ---------------------------------------------------------------------------------------------

// Fire one projectile along +X from `from_x` and return the impact.
static bool fire_along_x(World *w, float from_x, float y, float z, uint32_t seed,
                         float dt, ProjectileImpact *out)
{
  ProjectileSystem sys;
  projectile_system_reset(&sys, seed);

  const ProjectileSpawn spawn = {.x = from_x, .y = y, .z = z,
                                 .dir_x = 1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                 .speed = PLAYER_FIREBALL_SPEED,
                                 .life = PLAYER_FIREBALL_LIFE_S,
                                 .radius = PLAYER_FIREBALL_RADIUS,
                                 .gravity_scale = 0.0f,
                                 .damage = PLAYER_FIREBALL_DAMAGE,
                                 .kind = PROJECTILE_FIREBALL};
  if (!projectile_spawn(&sys, &spawn))
    return false;

  for (int i = 0; i < 4000; i++)
  {
    ProjectileImpact impacts[PROJECTILE_MAX];
    int n = 0;
    projectile_system_step(&sys, w, dt, impacts, PROJECTILE_MAX, &n);
    if (n > 0)
    {
      if (out)
        *out = impacts[0];
      return true;
    }
    if (projectile_active_count(&sys) == 0)
      return false;
  }
  return false;
}

static void test_projectile_flight(void)
{
  printf("\n-- projectile flight --\n");

  const int size = 48, floor_z = 8;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  const float fly_z = (float)floor_z + 3.5f;

  // A stone wall to stop at.
  for (int z = floor_z + 1; z < size; z++)
    for (int y = 0; y < size; y++)
      world_set_voxel(w, 30, (uint32_t)y, (uint32_t)z, VOXEL_STONE);

  ProjectileImpact im;
  memset(&im, 0, sizeof(im));
  const bool hit = fire_along_x(w, 10.5f, 12.5f, fly_z, 0xAAAAu, 1.0f / 60.0f, &im);
  report("a projectile fired at a wall reports an impact", hit);
  report("the impact is on the wall it was aimed at",
         hit && im.kind == PROJECTILE_IMPACT_VOXEL && im.vx == 30 && im.type == VOXEL_STONE);

  // Nothing in the way: it should burn out rather than report a hit on anything.
  World *open = make_arena(size, floor_z);
  if (open)
  {
    memset(&im, 0, sizeof(im));
    const bool ended = fire_along_x(open, 10.5f, 12.5f, fly_z, 0xBBBBu, 1.0f / 60.0f, &im);
    report("a projectile with nothing in its path burns out",
           ended && im.kind == PROJECTILE_IMPACT_EXPIRED);
    world_destroy(open);
  }

  // A projectile aimed out of the world must be retired, not read out of bounds. Fired along -X from
  // near the edge, which is the case where a signed coordinate would wrap if it were treated as
  // unsigned.
  World *edge = make_arena(size, floor_z);
  if (edge)
  {
    ProjectileSystem sys;
    projectile_system_reset(&sys, 0xCCCCu);
    const ProjectileSpawn spawn = {.x = 1.5f, .y = 12.5f, .z = fly_z,
                                   .dir_x = -1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                   .speed = PLAYER_FIREBALL_SPEED, .life = PLAYER_FIREBALL_LIFE_S,
                                   .radius = PLAYER_FIREBALL_RADIUS, .gravity_scale = 0.0f,
                                   .damage = 5, .kind = PROJECTILE_FIREBALL};
    projectile_spawn(&sys, &spawn);
    for (int i = 0; i < 200 && projectile_active_count(&sys) > 0; i++)
      projectile_system_step(&sys, edge, 1.0f / 60.0f, NULL, 0, NULL);
    report("a projectile leaving the world is retired rather than read out of bounds",
           projectile_active_count(&sys) == 0);
    world_destroy(edge);
  }

  // The pool is finite and a refused spawn must be refused cleanly.
  {
    ProjectileSystem sys;
    projectile_system_reset(&sys, 0xDDDDu);
    const ProjectileSpawn spawn = {.x = 10.5f, .y = 12.5f, .z = fly_z,
                                   .dir_x = 1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                   .speed = 1.0f, .life = 10.0f, .radius = 0.4f,
                                   .gravity_scale = 0.0f, .damage = 1, .kind = PROJECTILE_FIREBALL};
    int accepted = 0;
    for (int i = 0; i < PROJECTILE_MAX + 8; i++)
      if (projectile_spawn(&sys, &spawn))
        accepted++;
    report("the pool accepts exactly its capacity", accepted == PROJECTILE_MAX);
    report("spawns beyond capacity are counted as dropped", sys.dropped_total == 8);

    // A degenerate direction is refused rather than producing a projectile that never moves.
    ProjectileSpawn bad = spawn;
    bad.dir_x = 0.0f;
    projectile_system_reset(&sys, 0xDDDDu);
    report("a zero direction is refused", !projectile_spawn(&sys, &bad));
  }

  world_destroy(w);
}

// A bound fireball that leaves the western cell continues in a loaded eastern neighbour and can
// hit a wall there, instead of expiring at the rim.
static void test_projectile_crosses_world_boundary(void)
{
  printf("\n-- projectile across a world boundary --\n");

  const int size = 16, floor_z = 4;
  const float fly_z = (float)floor_z + 3.5f;
  World *west = make_arena(size, floor_z);
  World *east = make_arena(size, floor_z);
  if (!west || !east)
  {
    report("projectile seam scenario built", false);
    world_destroy(west);
    world_destroy(east);
    return;
  }

  // Solid wall a few voxels inside the eastern world, along the fireball's +X path.
  for (int z = floor_z + 1; z < size; z++)
    for (int y = 0; y < size; y++)
      world_set_voxel(east, 4, (uint32_t)y, (uint32_t)z, VOXEL_STONE);

  Universe universe;
  memset(&universe, 0, sizeof(universe));
  if (!universe_init(&universe, "0123456789abcdef", 0, 1))
  {
    report("projectile seam universe initialised", false);
    world_destroy(west);
    world_destroy(east);
    return;
  }

  const uint64_t hz = (uint64_t)UNIVERSE_HOME_Z;
  universe_place(&universe, 0, 0, hz, west);
  universe_place(&universe, 1, 0, hz, east);
  west->universe_context = &universe;
  west->universe_x = 0;
  west->universe_y = 0;
  west->universe_z = hz;
  east->universe_context = &universe;
  east->universe_x = 1;
  east->universe_y = 0;
  east->universe_z = hz;

  ProjectileSystem sys;
  projectile_system_reset(&sys, 0x51AAu);
  const ProjectileSpawn spawn = {.x = (float)size - 1.5f,
                                 .y = 8.5f,
                                 .z = fly_z,
                                 .dir_x = 1.0f,
                                 .dir_y = 0.0f,
                                 .dir_z = 0.0f,
                                 .speed = PLAYER_FIREBALL_SPEED,
                                 .life = PLAYER_FIREBALL_LIFE_S,
                                 .radius = PLAYER_FIREBALL_RADIUS,
                                 .gravity_scale = 0.0f,
                                 .damage = 5,
                                 .kind = PROJECTILE_FIREBALL,
                                 .bind_universe = true,
                                 .universe_x = 0,
                                 .universe_y = 0,
                                 .universe_z = hz};
  report("bound fireball spawns near the eastern seam", projectile_spawn(&sys, &spawn));

  ProjectileImpact impacts[8];
  int n = 0;
  bool hit_east = false;
  for (int i = 0; i < 240; i++)
  {
    n = 0;
    projectile_system_step_universe(&sys, &universe, west, 1.0f / 60.0f, impacts, 8, &n);
    for (int j = 0; j < n; j++)
    {
      if (impacts[j].kind == PROJECTILE_IMPACT_VOXEL && impacts[j].world == east &&
          impacts[j].vx == 4)
      {
        hit_east = true;
        break;
      }
    }
    if (hit_east || projectile_active_count(&sys) == 0)
      break;
  }

  report("the fireball crossed into the eastern world and hit its wall", hit_east);

  {
    World *taken = NULL;
    universe_remove(&universe, 0, 0, hz, &taken);
    (void)taken;
    universe_remove(&universe, 1, 0, hz, &taken);
    (void)taken;
  }
  universe_free(&universe);
  world_destroy(west);
  world_destroy(east);
}

static void test_foliage_probability(void)
{
  printf("\n-- foliage stops some projectiles and not others --\n");

  const int size = 48, floor_z = 8;
  const float fly_z = (float)floor_z + 3.5f;

  // One voxel-thick curtain of leaves. Each shot crosses exactly one foliage voxel, so the fraction
  // that gets through should land near 1 - PROJECTILE_LEAVES_STOP_CHANCE.
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }
  for (int z = floor_z + 1; z < size; z++)
    for (int y = 0; y < size; y++)
      world_set_voxel(w, 20, (uint32_t)y, (uint32_t)z, VOXEL_LEAVES_OAK);
  // A backstop, so a shot that gets through the curtain reports a stone hit and can be told apart
  // from one the leaves caught.
  for (int z = floor_z + 1; z < size; z++)
    for (int y = 0; y < size; y++)
      world_set_voxel(w, 30, (uint32_t)y, (uint32_t)z, VOXEL_STONE);

  int through = 0, caught = 0;
  const int shots = 400;
  for (int i = 0; i < shots; i++)
  {
    ProjectileImpact im;
    memset(&im, 0, sizeof(im));
    // Varying the seed is what varies the roll, which is the same thing that distinguishes one cast
    // from the next in the game.
    if (!fire_along_x(w, 10.5f, 12.5f, fly_z, (uint32_t)(i * 2654435761u + 7u), 1.0f / 60.0f, &im))
      continue;
    if (im.type == VOXEL_STONE)
      through++;
    else if (world_voxel_type_is_foliage(im.type))
      caught++;
  }

  const float rate = (float)caught / (float)shots;
  printf("       %d of %d shots caught by one leaf voxel (%.0f%%, expected ~%.0f%%)\n", caught,
         shots, rate * 100.0f, PROJECTILE_LEAVES_STOP_CHANCE * 100.0f);

  report("some shots are caught by the leaves", caught > 0);
  report("some shots pass through the leaves", through > 0);
  report("every shot is accounted for", through + caught == shots);
  // Loose enough not to be flaky, tight enough to catch the chance being ignored or applied twice.
  report("the fraction caught is near the configured chance",
         fabsf(rate - PROJECTILE_LEAVES_STOP_CHANCE) < 0.10f);

  // Tall grass is the lighter obstacle, and must actually be lighter.
  World *g = make_arena(size, floor_z);
  if (g)
  {
    for (int z = floor_z + 1; z < size; z++)
      for (int y = 0; y < size; y++)
        world_set_voxel(g, 20, (uint32_t)y, (uint32_t)z, VOXEL_GRASS_TALL);
    for (int z = floor_z + 1; z < size; z++)
      for (int y = 0; y < size; y++)
        world_set_voxel(g, 30, (uint32_t)y, (uint32_t)z, VOXEL_STONE);

    int grass_caught = 0;
    for (int i = 0; i < shots; i++)
    {
      ProjectileImpact im;
      memset(&im, 0, sizeof(im));
      if (!fire_along_x(g, 10.5f, 12.5f, fly_z, (uint32_t)(i * 2654435761u + 7u), 1.0f / 60.0f, &im))
        continue;
      if (world_voxel_type_is_foliage(im.type))
        grass_caught++;
    }
    const float grass_rate = (float)grass_caught / (float)shots;
    printf("       %d of %d shots caught by one tall grass voxel (%.0f%%)\n", grass_caught, shots,
           grass_rate * 100.0f);
    report("tall grass stops fewer shots than leaves do", grass_rate < rate);
    report("the tall grass fraction is near its configured chance",
           fabsf(grass_rate - PROJECTILE_GRASS_STOP_CHANCE) < 0.08f);
    world_destroy(g);
  }

  // The roll is per voxel entered, not per frame. If it were per frame, a coarse time step would
  // sample the canopy fewer times than a fine one and the same shot would survive at one frame rate
  // and die at another. Same seed, four very different steps, same outcome.
  {
    const float steps[] = {1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f, 1.0f / 240.0f};
    bool consistent = true;
    for (uint32_t seed = 1; seed <= 40; seed++)
    {
      ProjectileImpact first;
      memset(&first, 0, sizeof(first));
      bool have_first = false;
      for (size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++)
      {
        ProjectileImpact im;
        memset(&im, 0, sizeof(im));
        if (!fire_along_x(w, 10.5f, 12.5f, fly_z, seed, steps[s], &im))
        {
          consistent = false;
          break;
        }
        if (!have_first)
        {
          first = im;
          have_first = true;
        }
        else if (im.type != first.type)
        {
          consistent = false;
        }
      }
    }
    report("a shot's fate does not depend on the frame rate", consistent);
  }

  world_destroy(w);
}

static void test_actor_hits(void)
{
  printf("\n-- projectiles hitting actors --\n");

  const int size = 32, floor_z = 8;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  const float fly_z = (float)floor_z + 3.5f;

  Actor target;
  memset(&target, 0, sizeof(target));
  target.id = 4242;
  strncpy(target.name, "straw dummy", sizeof(target.name) - 1);
  target.x = 20.5;
  target.y = 12.5;
  target.z = (double)fly_z;
  target.health = 100;
  target.is_active = true;

  w->runtime_actors = &target;
  w->runtime_actor_count = 1;

  ProjectileImpact im;
  memset(&im, 0, sizeof(im));
  const bool hit = fire_along_x(w, 10.5f, 12.5f, fly_z, 0xEEEEu, 1.0f / 60.0f, &im);
  report("a projectile fired at an actor reports an actor impact",
         hit && im.kind == PROJECTILE_IMPACT_ACTOR);
  report("the impact names the actor it hit", hit && im.actor_id == target.id);
  report("the impact carries the damage to apply", hit && im.damage == PLAYER_FIREBALL_DAMAGE);
  report("the flight simulation does not itself apply damage", target.health == 100);

  // Same XY aim, but the shot travels at spirit hover height while the body stands on the floor —
  // the live miss that a feet-centred sphere produces.
  {
    Actor grounded;
    memset(&grounded, 0, sizeof(grounded));
    grounded.id = 4243;
    strncpy(grounded.name, "grounded dummy", sizeof(grounded.name) - 1);
    grounded.x = 20.5;
    grounded.y = 12.5;
    grounded.z = (double)(floor_z + 1); // standing on the floor
    grounded.health = 100;
    grounded.is_active = true;
    w->runtime_actors = &grounded;
    w->runtime_actor_count = 1;

    ProjectileImpact high;
    memset(&high, 0, sizeof(high));
    const float spirit_z = (float)floor_z + 2.5f;
    const bool tall_hit =
        fire_along_x(w, 10.5f, 12.5f, spirit_z, 0xAAAAu, 1.0f / 60.0f, &high);
    report("a fireball at spirit height still hits a grounded actor",
           tall_hit && high.kind == PROJECTILE_IMPACT_ACTOR && high.actor_id == grounded.id);

    w->runtime_actors = &target;
    w->runtime_actor_count = 1;
  }

  // game_state_step_projectiles is the layer that turns an impact into damage.
  GameState *state = make_state(w, 10.5f, 12.5f, (float)(floor_z + 1));
  if (state)
  {
    const ProjectileSpawn spawn = {.x = 10.5f, .y = 12.5f, .z = fly_z,
                                   .dir_x = 1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                   .speed = PLAYER_FIREBALL_SPEED, .life = PLAYER_FIREBALL_LIFE_S,
                                   .radius = PLAYER_FIREBALL_RADIUS, .gravity_scale = 0.0f,
                                   .damage = PLAYER_FIREBALL_DAMAGE, .kind = PROJECTILE_FIREBALL};
    projectile_spawn(&state->projectiles, &spawn);
    for (int i = 0; i < 400 && projectile_active_count(&state->projectiles) > 0; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);

    printf("       dummy health %u -> %u\n", 100u, target.health);
    report("the game applies the damage the impact reported",
           target.health == 100 - PLAYER_FIREBALL_DAMAGE);
    destroy_state(state);
  }

  // A fireball caught by leaves burns them away, which is the visible consequence of the roll.
  World *canopy = make_arena(size, floor_z);
  if (canopy)
  {
    // Find a seed the leaves actually catch, then confirm that voxel is gone afterwards.
    bool burned = false;
    for (uint32_t seed = 1; seed <= 200 && !burned; seed++)
    {
      for (int z = floor_z + 1; z < size; z++)
        for (int y = 0; y < size; y++)
          world_set_voxel(canopy, 20, (uint32_t)y, (uint32_t)z, VOXEL_LEAVES_OAK);

      GameState *s = make_state(canopy, 10.5f, 12.5f, (float)(floor_z + 1));
      if (!s)
        break;
      projectile_system_reset(&s->projectiles, seed);

      const ProjectileSpawn spawn = {.x = 10.5f, .y = 12.5f, .z = fly_z,
                                     .dir_x = 1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                     .speed = PLAYER_FIREBALL_SPEED, .life = PLAYER_FIREBALL_LIFE_S,
                                     .radius = PLAYER_FIREBALL_RADIUS, .gravity_scale = 0.0f,
                                     .damage = PLAYER_FIREBALL_DAMAGE, .kind = PROJECTILE_FIREBALL};
      projectile_spawn(&s->projectiles, &spawn);
      for (int i = 0; i < 400 && projectile_active_count(&s->projectiles) > 0; i++)
        game_state_step_projectiles(s, 1.0 / 60.0);

      const Voxel *v = world_get_voxel(canopy, 20, 12, (uint32_t)floorf(fly_z));
      if (v && v->type == VOXEL_AIR)
        burned = true;
      destroy_state(s);
    }
    report("a fireball the canopy catches burns that leaf away", burned);
    world_destroy(canopy);
  }

  w->runtime_actors = NULL;
  w->runtime_actor_count = 0;
  world_destroy(w);
}

// ---------------------------------------------------------------------------------------------
// The fireball skill
// ---------------------------------------------------------------------------------------------

static void test_fireball_skill(void)
{
  printf("\n-- the spirit's fireball --\n");

  const int size = 32, floor_z = 8;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  GameState *state = make_state(w, 12.5f, 12.5f, (float)(floor_z + 1));
  if (!state)
  {
    report("game state built", false);
    world_destroy(w);
    return;
  }

  report("the skill starts ready", player_controls_fireball_ready(state, &state->controls));

  const float stamina_before = state->player->stamina;
  const bool cast = player_controls_cast_fireball_at(state, &state->controls, 20.5f, 12.5f,
                                                    state->player_world_z);
  report("a ready cast succeeds", cast);
  report("the cast puts a projectile in the air", projectile_active_count(&state->projectiles) == 1);
  report("the cast spends stamina",
         fabsf(state->player->stamina - (stamina_before - PLAYER_FIREBALL_STAMINA_COST)) < 0.01f);
  report("the cast turns the caster toward the target", fabsf(state->controls.aim_yaw) < 0.01f);

  // Cooldown. The skill must refuse immediately after a cast, and refuse without charging for it.
  const float stamina_after_first = state->player->stamina;
  const bool second = player_controls_cast_fireball_at(state, &state->controls, 20.5f, 12.5f,
                                                     state->player_world_z);
  report("a second cast is refused while on cooldown", !second);
  report("a refused cast spends nothing",
         fabsf(state->player->stamina - stamina_after_first) < 0.01f);
  report("a refused cast adds no projectile", projectile_active_count(&state->projectiles) == 1);
  report("the skill reports itself as not ready while cooling down",
         !player_controls_fireball_ready(state, &state->controls));

  // Clear the cooldown the way time would, rather than sleeping through it.
  state->controls.fireball_ready_at_ms = 0;
  report("the skill is ready again once the cooldown elapses",
         player_controls_fireball_ready(state, &state->controls));

  // Stamina gate, with the cooldown out of the way so it is the only thing that can refuse.
  state->player->stamina = (uint32_t)PLAYER_FIREBALL_STAMINA_COST - 1;
  report("the skill is not ready without the stamina for it",
         !player_controls_fireball_ready(state, &state->controls));
  const int in_air = projectile_active_count(&state->projectiles);
  report("a cast without the stamina for it is refused",
         !player_controls_cast_fireball_at(state, &state->controls, 20.5f, 12.5f,
                                          state->player_world_z));
  report("a cast refused for stamina adds no projectile",
         projectile_active_count(&state->projectiles) == in_air);

  // Exactly enough is enough.
  state->player->stamina = (uint32_t)PLAYER_FIREBALL_STAMINA_COST;
  state->controls.fireball_ready_at_ms = 0;
  report("exactly enough stamina is enough",
         player_controls_cast_fireball_at(state, &state->controls, 20.5f, 12.5f,
                                         state->player_world_z));

  // Aiming with the look direction, which is the first-person path.
  state->controls.fireball_ready_at_ms = 0;
  state->player->stamina = 100;
  state->controls.facing_yaw = (float)M_PI_2; // +Y
  state->controls.pitch = 0.0f;
  const int before_forward = projectile_active_count(&state->projectiles);
  report("a forward cast succeeds",
         player_controls_cast_fireball_forward(state, &state->controls));
  report("the forward cast puts a projectile in the air",
         projectile_active_count(&state->projectiles) == before_forward + 1);

  // It must actually travel the way the player is looking.
  bool aimed_along_y = false;
  for (int i = 0; i < PROJECTILE_MAX; i++)
  {
    const Projectile *p = &state->projectiles.items[i];
    if (!p->active)
      continue;
    if (p->vy > PLAYER_FIREBALL_SPEED * 0.9f && fabsf(p->vx) < 0.1f)
      aimed_along_y = true;
  }
  report("the forward cast flies where the player is looking", aimed_along_y);

  // The skill is unavailable outside a running game, so a click on the menu cannot cast.
  state->controls.fireball_ready_at_ms = 0;
  state->game_started = false;
  report("the skill is unavailable before the game starts",
         !player_controls_fireball_ready(state, &state->controls));
  state->game_started = true;

  destroy_state(state);
  world_destroy(w);
}

// ---------------------------------------------------------------------------------------------
// Solid voxel damage, cracks, and fractional debris
// ---------------------------------------------------------------------------------------------

static void test_fireball_voxel_damage(void)
{
  printf("\n-- fireball damage to solid voxels --\n");

  const int size = 32, floor_z = 8;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  // A stone pillar in the fireball's path.
  world_set_voxel(w, 20, 12, (uint32_t)(floor_z + 1), VOXEL_STONE);
  Voxel *pillar = world_get_voxel(w, 20, 12, (uint32_t)(floor_z + 1));
  report("pillar placed", pillar && pillar->type == VOXEL_STONE);

  GameState *state = make_state(w, 10.5f, 12.5f, (float)(floor_z + 1));
  if (!state)
  {
    report("game state built", false);
    world_destroy(w);
    return;
  }

  inventory_clear(&state->player->inventory);
  const float fly_z = (float)(floor_z + 1) + 0.5f;

  // One hit should crack the stone without destroying it (durability 180, damage 18).
  {
    const ProjectileSpawn spawn = {.x = 10.5f, .y = 12.5f, .z = fly_z,
                                   .dir_x = 1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                   .speed = PLAYER_FIREBALL_SPEED, .life = PLAYER_FIREBALL_LIFE_S,
                                   .radius = PLAYER_FIREBALL_RADIUS, .gravity_scale = 0.0f,
                                   .damage = PLAYER_FIREBALL_DAMAGE, .kind = PROJECTILE_FIREBALL};
    projectile_spawn(&state->projectiles, &spawn);
    for (int i = 0; i < 400 && projectile_active_count(&state->projectiles) > 0; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);

    pillar = world_get_voxel(w, 20, 12, (uint32_t)(floor_z + 1));
    report("one fireball cracks stone without destroying it",
           pillar && pillar->type == VOXEL_STONE && voxel_get_damage(pillar) > 0);
  }

  // Keep hitting until it shatters.
  int hits = 1;
  while (hits < 40)
  {
    pillar = world_get_voxel(w, 20, 12, (uint32_t)(floor_z + 1));
    if (!pillar || pillar->type == VOXEL_AIR)
      break;
    const ProjectileSpawn spawn = {.x = 10.5f, .y = 12.5f, .z = fly_z,
                                   .dir_x = 1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                   .speed = PLAYER_FIREBALL_SPEED, .life = PLAYER_FIREBALL_LIFE_S,
                                   .radius = PLAYER_FIREBALL_RADIUS, .gravity_scale = 0.0f,
                                   .damage = PLAYER_FIREBALL_DAMAGE, .kind = PROJECTILE_FIREBALL};
    projectile_spawn(&state->projectiles, &spawn);
    for (int i = 0; i < 400 && projectile_active_count(&state->projectiles) > 0; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);
    hits++;
  }

  pillar = world_get_voxel(w, 20, 12, (uint32_t)(floor_z + 1));
  report("enough fireballs shatter the stone to air",
         pillar && pillar->type == VOXEL_AIR);
  report("shattering scatters debris pieces", debris_active_count(&state->debris) > 0);
  report("shattering awards block XP to the spirit",
         state->player && state->player->experience >= XP_BLOCK_DESTROY);

  uint32_t piece_sum = 0;
  for (int i = 0; i < DEBRIS_MAX; i++)
    if (state->debris.items[i].active)
      piece_sum += state->debris.items[i].pieces;
  report("debris pieces sum to one full block (512)", piece_sum == VOXEL_PIECES_PER_BLOCK);
  report("VOXEL_PIECES_PER_BLOCK matches ITEM_BLOCK_PIECES",
         VOXEL_PIECES_PER_BLOCK == ITEM_BLOCK_PIECES);

  // Walk onto the pile and collect.
  state->player_world_x = 20.5f;
  state->player_world_y = 12.5f;
  state->player_world_z = (float)(floor_z + 1);
  for (int i = 0; i < 120; i++)
    game_state_step_debris(state, 1.0 / 60.0);

  report("debris can be picked up into inventory",
         inventory_count_pieces(&state->player->inventory, ITEM_STONE_BLOCK) == ITEM_BLOCK_PIECES);
  report("one block of stone accumulates as a whole unit",
         inventory_count_item(&state->player->inventory, ITEM_STONE_BLOCK) == 1);

  // Fractional add: another half-block of pieces.
  inventory_add_pieces(&state->player->inventory, ITEM_STONE_BLOCK, ITEM_BLOCK_PIECES / 2);
  report("half a block accumulates fractionally",
         inventory_count_pieces(&state->player->inventory, ITEM_STONE_BLOCK) ==
             ITEM_BLOCK_PIECES + ITEM_BLOCK_PIECES / 2);

  // Bedrock is immune.
  world_set_voxel(w, 22, 12, (uint32_t)(floor_z + 1), VOXEL_BEDROCK);
  {
    const ProjectileSpawn spawn = {.x = 18.5f, .y = 12.5f, .z = fly_z,
                                   .dir_x = 1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                   .speed = PLAYER_FIREBALL_SPEED, .life = PLAYER_FIREBALL_LIFE_S,
                                   .radius = PLAYER_FIREBALL_RADIUS, .gravity_scale = 0.0f,
                                   .damage = PLAYER_FIREBALL_DAMAGE, .kind = PROJECTILE_FIREBALL};
    projectile_spawn(&state->projectiles, &spawn);
    for (int i = 0; i < 400 && projectile_active_count(&state->projectiles) > 0; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);
    const Voxel *bed = world_get_voxel(w, 22, 12, (uint32_t)(floor_z + 1));
    report("bedrock shrugs off a fireball",
           bed && bed->type == VOXEL_BEDROCK && voxel_get_damage(bed) == 0);
  }

  // Solids stop the shot: a two-block wall takes damage only on the face that was hit. Foliage is
  // the only material that may let a projectile continue past it.
  world_set_voxel(w, 16, 12, (uint32_t)(floor_z + 1), VOXEL_STONE);
  world_set_voxel(w, 17, 12, (uint32_t)(floor_z + 1), VOXEL_STONE);
  {
    const ProjectileSpawn spawn = {.x = 10.5f, .y = 12.5f, .z = fly_z,
                                   .dir_x = 1.0f, .dir_y = 0.0f, .dir_z = 0.0f,
                                   .speed = PLAYER_FIREBALL_SPEED, .life = PLAYER_FIREBALL_LIFE_S,
                                   .radius = PLAYER_FIREBALL_RADIUS, .gravity_scale = 0.0f,
                                   .damage = PLAYER_FIREBALL_DAMAGE, .kind = PROJECTILE_FIREBALL};
    projectile_spawn(&state->projectiles, &spawn);
    for (int i = 0; i < 400 && projectile_active_count(&state->projectiles) > 0; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);
    const Voxel *front = world_get_voxel(w, 16, 12, (uint32_t)(floor_z + 1));
    const Voxel *back = world_get_voxel(w, 17, 12, (uint32_t)(floor_z + 1));
    report("a solid wall stops the fireball at the first stone",
           front && back && voxel_get_damage(front) > 0 && voxel_get_damage(back) == 0);
    report("the fireball is spent against the wall rather than flying on",
           projectile_active_count(&state->projectiles) == 0);
  }

  destroy_state(state);
  world_destroy(w);
}

// Chop a tree's trunk: the canopy must fall as one cluster, not hang in the air.
static void test_tree_disconnect(void)
{
  printf("\n-- neighborhood disconnect drops a chopped tree --\n");

  const int size = 32, floor_z = 4;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  const int tx = 16, ty = 16;
  // Trunk of wood from floor+1 up to floor+5, canopy leaves on top and sides.
  for (int z = floor_z + 1; z <= floor_z + 5; z++)
    world_set_voxel(w, (uint32_t)tx, (uint32_t)ty, (uint32_t)z, VOXEL_WOOD);
  for (int dx = -2; dx <= 2; dx++)
    for (int dy = -2; dy <= 2; dy++)
      for (int dz = 0; dz <= 2; dz++)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        world_set_voxel(w, (uint32_t)(tx + dx), (uint32_t)(ty + dy),
                        (uint32_t)(floor_z + 5 + dz), VOXEL_LEAVES);
      }

  const int before_leaves = 0;
  (void)before_leaves;
  int leaf_count = 0;
  for (int z = 0; z < size; z++)
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
      {
        const Voxel *v = world_get_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (v && v->type == VOXEL_LEAVES)
          leaf_count++;
      }
  report("tree has a canopy", leaf_count > 10);

  // Chop the lowest trunk voxel.
  world_set_voxel(w, (uint32_t)tx, (uint32_t)ty, (uint32_t)(floor_z + 1), VOXEL_AIR);
  const int marked = voxel_fracture_disconnect_at(w, tx, ty, floor_z + 1);
  report("disconnect marks the canopy as falling", marked > 5);
  report("falling count matches what disconnect marked",
         voxel_fracture_falling_count(w) == marked);

  // Step until settled or a safety bound.
  int steps = 0;
  while (voxel_fracture_falling_count(w) > 0 && steps < 40)
  {
    voxel_fracture_step_falling(w);
    steps++;
  }
  report("the cluster settles within a few ticks", steps > 0 && steps < 40);
  report("nothing is left marked falling", voxel_fracture_falling_count(w) == 0);

  // Canopy should have dropped onto the stump / floor — top leaves no longer at original height.
  int high_leaves = 0;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
    {
      const Voxel *v = world_get_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)(floor_z + 7));
      if (v && v->type == VOXEL_LEAVES)
        high_leaves++;
    }
  report("leaves no longer float at the old canopy height", high_leaves == 0);

  int resting_wood = 0;
  for (int z = 0; z < size; z++)
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
      {
        const Voxel *v = world_get_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (v && v->type == VOXEL_WOOD)
          resting_wood++;
      }
  report("trunk wood still exists after the fall", resting_wood >= 4);

  world_destroy(w);
}

static void test_tree_debris_volume(void)
{
  printf("\n-- disconnect extracts a canopy into a debris volume --\n");

  const int size = 32, floor_z = 4;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);
  voxel_fracture_set_debris_volumes(&vols);

  const int tx = 16, ty = 16;
  for (int z = floor_z + 1; z <= floor_z + 5; z++)
    world_set_voxel(w, (uint32_t)tx, (uint32_t)ty, (uint32_t)z, VOXEL_WOOD);
  for (int dx = -2; dx <= 2; dx++)
    for (int dy = -2; dy <= 2; dy++)
      for (int dz = 0; dz <= 2; dz++)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        world_set_voxel(w, (uint32_t)(tx + dx), (uint32_t)(ty + dy),
                        (uint32_t)(floor_z + 5 + dz), VOXEL_LEAVES);
      }

  world_set_voxel(w, (uint32_t)tx, (uint32_t)ty, (uint32_t)(floor_z + 1), VOXEL_AIR);
  const int extracted = voxel_fracture_disconnect_at(w, tx, ty, floor_z + 1);
  report("disconnect extracts cells into a volume", extracted > 5);
  report("no on-grid falling marks remain", voxel_fracture_falling_count(w) == 0);
  report("exactly one debris volume is active", voxel_debris_volume_active_count(&vols) == 1);

  int steps = 0;
  while (voxel_debris_volume_active_count(&vols) > 0 && steps < 360)
  {
    voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
    steps++;
  }
  report("the volume settles and writes back", steps > 0 && steps < 360);
  report("no debris volumes remain active", voxel_debris_volume_active_count(&vols) == 0);

  int high_leaves = 0;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
    {
      const Voxel *v = world_get_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)(floor_z + 7));
      if (v && v->type == VOXEL_LEAVES)
        high_leaves++;
    }
  report("leaves no longer float at the old canopy height", high_leaves == 0);

  int resting_leaves = 0;
  for (int z = 0; z < size; z++)
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
      {
        const Voxel *v = world_get_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (v && v->type == VOXEL_LEAVES)
          resting_leaves++;
      }
  report("canopy leaves were written back into the world", resting_leaves > 5);

  voxel_fracture_set_debris_volumes(NULL);
  world_destroy(w);
}

static void test_debris_volume_stack(void)
{
  printf("\n-- two debris volumes stack under TGS contacts --\n");

  const int size = 32, floor_z = 2;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);

  // Two 2x2x2 stone blocks. Lower rests near the floor; upper starts above it.
  int xs[8], ys[8], zs[8];
  int n = 0;
  for (int dz = 0; dz < 2; dz++)
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++)
      {
        xs[n] = 10 + dx;
        ys[n] = 10 + dy;
        zs[n] = floor_z + 1 + dz;
        world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_STONE);
        n++;
      }
  report("lower block extracted", voxel_debris_volume_extract(&vols, w, xs, ys, zs, n) == 8);

  n = 0;
  for (int dz = 0; dz < 2; dz++)
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++)
      {
        xs[n] = 10 + dx;
        ys[n] = 10 + dy;
        zs[n] = floor_z + 8 + dz;
        world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_STONE);
        n++;
      }
  report("upper block extracted", voxel_debris_volume_extract(&vols, w, xs, ys, zs, n) == 8);
  report("two volumes are active", voxel_debris_volume_active_count(&vols) == 2);

  int steps = 0;
  while (voxel_debris_volume_active_count(&vols) > 0 && steps < 240)
  {
    voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
    steps++;
  }
  report("both volumes eventually write back", voxel_debris_volume_active_count(&vols) == 0);
  report("stack settles within a few seconds", steps > 0 && steps < 240);

  int stone = 0;
  int max_z = -1;
  for (int z = floor_z + 1; z < size; z++)
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
      {
        const Voxel *v = world_get_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (v && v->type == VOXEL_STONE)
        {
          stone++;
          if (z > max_z)
            max_z = z;
        }
      }
  report("all sixteen stone cells returned above the floor", stone == 16);
  report("the stack rises above a single block height", max_z >= floor_z + 3);

  world_destroy(w);
}

static void test_debris_volume_spin(void)
{
  printf("\n-- debris volume yaw integrates under friction --\n");

  const int size = 32, floor_z = 2;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);

  int xs[8], ys[8], zs[8], n = 0;
  for (int dz = 0; dz < 2; dz++)
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++)
      {
        xs[n] = 12 + dx;
        ys[n] = 12 + dy;
        zs[n] = floor_z + 4 + dz;
        world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_STONE);
        n++;
      }
  report("block extracted", voxel_debris_volume_extract(&vols, w, xs, ys, zs, n) == 8);
  vols.items[0].wz = 4.0f; // rad/s yaw spin on the way down
  const float yaw0 = vols.items[0].yaw;

  int steps = 0;
  float max_abs_yaw = 0.0f;
  while (voxel_debris_volume_active_count(&vols) > 0 && steps < 240)
  {
    voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
    if (vols.items[0].active)
    {
      const float ay = fabsf(vols.items[0].yaw - yaw0);
      if (ay > max_abs_yaw)
        max_abs_yaw = ay;
    }
    steps++;
  }
  report("the volume spun before it settled", max_abs_yaw > 0.05f);
  report("the spinning volume still writes back", voxel_debris_volume_active_count(&vols) == 0);

  world_destroy(w);
}

static void test_debris_volume_tip(void)
{
  printf("\n-- tall debris volume tips under lateral slide --\n");

  const int size = 32, floor_z = 2;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);

  // 1×1×6 stone column — tall enough that ground friction torque tips it.
  int xs[6], ys[6], zs[6], n = 0;
  for (int dz = 0; dz < 6; dz++)
  {
    xs[n] = 16;
    ys[n] = 16;
    zs[n] = floor_z + 1 + dz;
    world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_STONE);
    n++;
  }
  report("column extracted", voxel_debris_volume_extract(&vols, w, xs, ys, zs, n) == 6);
  vols.items[0].vx = 5.0f; // slide sideways on landing
  const float pitch0 = vols.items[0].pitch;

  int steps = 0;
  float max_abs_pitch = 0.0f;
  while (voxel_debris_volume_active_count(&vols) > 0 && steps < 360)
  {
    voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
    if (vols.items[0].active)
    {
      const float ap = fabsf(vols.items[0].pitch - pitch0);
      if (ap > max_abs_pitch)
        max_abs_pitch = ap;
    }
    steps++;
  }
  report("the column pitched before it settled", max_abs_pitch > 0.08f);
  report("the tipping volume still writes back", voxel_debris_volume_active_count(&vols) == 0);

  world_destroy(w);
}

static void test_place_vs_body(void)
{
  printf("\n-- place-vs-body refuses writeback into an actor --\n");
  World *w = world_create(24, 24, 16);
  report("place-vs-body world", w != NULL);
  if (!w)
    return;

  for (int y = 0; y < 24; y++)
    for (int x = 0; x < 24; x++)
      world_set_voxel(w, (uint32_t)x, (uint32_t)y, 0, VOXEL_BEDROCK);

  Actor body = {0};
  body.id = 42;
  body.is_active = true;
  body.x = 10.5;
  body.y = 10.5;
  body.z = 1.0;
  report("actor joined the world", world_add_runtime_actor(w, &body));
  report("the cell under the actor is blocked", world_actor_blocks_cell(w, 10, 10, 1));
  report("a distant cell is free", !world_actor_blocks_cell(w, 2, 2, 1));

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);
  int xs[1] = {5}, ys[1] = {5}, zs[1] = {1};
  world_set_voxel(w, 5, 5, 1, VOXEL_STONE);
  report("extracted a one-cell volume", voxel_debris_volume_extract(&vols, w, xs, ys, zs, 1) == 1);

  DebrisVolume *vol = NULL;
  for (int i = 0; i < DEBRIS_VOLUME_MAX; i++)
    if (vols.items[i].active)
    {
      vol = &vols.items[i];
      break;
    }
  report("volume slot found", vol != NULL);
  if (vol)
  {
    // Park on the actor's cell, fully at rest on the floor so writeback fires.
    vol->x = 10.0f;
    vol->y = 10.0f;
    vol->z = 1.0f;
    vol->vx = vol->vy = vol->vz = 0.0f;
    vol->wx = vol->wy = vol->wz = 0.0f;
    vol->yaw = vol->pitch = vol->roll = 0.0f;
    vol->rest_ticks = 2;
    for (int s = 0; s < 6; s++)
      voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
  }

  report("the volume wrote back", voxel_debris_volume_active_count(&vols) == 0);
  const Voxel *at_actor = world_voxel_cptr_fast(w, 10, 10, 1);
  report("the actor cell stayed empty", !at_actor || at_actor->type == VOXEL_AIR);

  int solid_near = 0;
  for (int z = 1; z <= 5; z++)
    for (int y = 7; y <= 13; y++)
      for (int x = 7; x <= 13; x++)
      {
        if (x == 10 && y == 10 && z == 1)
          continue;
        const Voxel *v = world_voxel_cptr_fast(w, x, y, z);
        if (v && v->type == VOXEL_STONE)
          solid_near++;
      }
  report("the stone relocated next to the actor", solid_near >= 1);

  world_destroy(w);
}

static void test_debris_material_friction(void)
{
  printf("\n-- debris volumes inherit material friction --\n");
  World *w = world_create(24, 24, 12);
  report("friction world", w != NULL);
  if (!w)
    return;
  for (int y = 0; y < 24; y++)
    for (int x = 0; x < 24; x++)
      world_set_voxel(w, (uint32_t)x, (uint32_t)y, 0, VOXEL_BEDROCK);

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);

  int xs[4] = {4, 5, 4, 5}, ys[4] = {4, 4, 5, 5}, zs[4] = {1, 1, 1, 1};
  for (int i = 0; i < 4; i++)
    world_set_voxel(w, (uint32_t)xs[i], (uint32_t)ys[i], (uint32_t)zs[i], VOXEL_STONE);
  report("stone extracted", voxel_debris_volume_extract(&vols, w, xs, ys, zs, 4) == 4);
  const float stone_mu = vols.items[0].friction;
  report("stone friction is grippy", stone_mu > 0.40f);

  int xi[4] = {10, 11, 10, 11}, yi[4] = {10, 10, 11, 11}, zi[4] = {1, 1, 1, 1};
  for (int i = 0; i < 4; i++)
    world_set_voxel(w, (uint32_t)xi[i], (uint32_t)yi[i], (uint32_t)zi[i], VOXEL_ICE);
  report("ice extracted", voxel_debris_volume_extract(&vols, w, xi, yi, zi, 4) == 4);
  const float ice_mu = vols.items[1].friction;
  report("ice is slipperier than stone", ice_mu < stone_mu - 0.15f);
  report("ice has bounce", vols.items[1].restitution > vols.items[0].restitution);

  world_destroy(w);
}

static void test_debris_volume_tumble(void)
{
  printf("\n-- off-centre impulse tumbles a debris volume --\n");

  const int size = 32, floor_z = 2;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);

  // L-shaped stone slab: asymmetric so contact torque and impulse lever arms matter.
  int xs[6], ys[6], zs[6], n = 0;
  for (int dy = 0; dy < 2; dy++)
    for (int dx = 0; dx < 2; dx++)
    {
      xs[n] = 14 + dx;
      ys[n] = 14 + dy;
      zs[n] = floor_z + 5;
      world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_STONE);
      n++;
    }
  xs[n] = 16;
  ys[n] = 14;
  zs[n] = floor_z + 5;
  world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_STONE);
  n++;
  xs[n] = 16;
  ys[n] = 14;
  zs[n] = floor_z + 6;
  world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_STONE);
  n++;

  DebrisVolume *vol = NULL;
  report("L-piece extracted",
         voxel_debris_volume_extract_ex(&vols, w, xs, ys, zs, n, &vol) == 6 && vol != NULL);
  if (!vol)
  {
    world_destroy(w);
    return;
  }

  // Hit the tall arm from the side — linear push plus tumble about COM.
  voxel_debris_volume_apply_impulse(vol, 8.0f, 1.5f, 2.0f, vol->x + 2.5f, vol->y + 0.5f,
                                    vol->z + 1.5f);
  report("impulse seeded angular velocity",
         fabsf(vol->wx) + fabsf(vol->wy) + fabsf(vol->wz) > 0.2f);

  const float yaw0 = vol->yaw;
  const float pitch0 = vol->pitch;
  const float roll0 = vol->roll;
  int steps = 0;
  float max_yaw = 0.0f, max_pitch = 0.0f, max_roll = 0.0f;
  while (voxel_debris_volume_active_count(&vols) > 0 && steps < 480)
  {
    voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
    if (vols.items[0].active)
    {
      const float ay = fabsf(vols.items[0].yaw - yaw0);
      const float ap = fabsf(vols.items[0].pitch - pitch0);
      const float ar = fabsf(vols.items[0].roll - roll0);
      if (ay > max_yaw)
        max_yaw = ay;
      if (ap > max_pitch)
        max_pitch = ap;
      if (ar > max_roll)
        max_roll = ar;
    }
    steps++;
  }
  report("the volume yawed while tumbling", max_yaw > 0.05f);
  report("the volume pitched or rolled while tumbling", max_pitch > 0.08f || max_roll > 0.08f);
  report("the tumbling volume still writes back", voxel_debris_volume_active_count(&vols) == 0);

  world_destroy(w);
}

static void test_debris_volume_secondary_shatter(void)
{
  printf("\n-- hard landing secondary-fractures a debris volume --\n");

  const int size = 32, floor_z = 2;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);

  // 3×3×3 ice slab — fragile enough that a slam exceeds the shatter threshold.
  int xs[27], ys[27], zs[27], n = 0;
  for (int dz = 0; dz < 3; dz++)
    for (int dy = 0; dy < 3; dy++)
      for (int dx = 0; dx < 3; dx++)
      {
        xs[n] = 12 + dx;
        ys[n] = 12 + dy;
        zs[n] = floor_z + 10 + dz;
        world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_ICE);
        n++;
      }
  report("ice block extracted", voxel_debris_volume_extract(&vols, w, xs, ys, zs, n) == 27);
  const int cells0 = vols.items[0].cell_count;
  vols.items[0].vz = -22.0f; // slam into the floor

  int max_active = 1;
  int min_cells = cells0;
  int steps = 0;
  while (voxel_debris_volume_active_count(&vols) > 0 && steps < 480)
  {
    voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
    const int active = voxel_debris_volume_active_count(&vols);
    if (active > max_active)
      max_active = active;
    for (int i = 0; i < DEBRIS_VOLUME_MAX; i++)
      if (vols.items[i].active && vols.items[i].cell_count < min_cells)
        min_cells = vols.items[i].cell_count;
    steps++;
  }

  report("the slam removed cells or spawned shards", min_cells < cells0 || max_active > 1);
  report("secondary shatter still settles", voxel_debris_volume_active_count(&vols) == 0);

  int ice = 0;
  for (int z = 0; z < size; z++)
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
      {
        const Voxel *v = world_get_voxel(w, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (v && v->type == VOXEL_ICE)
          ice++;
      }
  // Stamp deletes some ice; survivors write back. Expect a clear majority of the slab returned.
  report("most ice cells wrote back after shatter", ice >= 12 && ice <= 27);

  world_destroy(w);
}

static void test_debris_volume_scale(void)
{
  printf("\n-- oversized island batches into multiple debris volumes --\n");

  const int size = 48, floor_z = 2;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);
  voxel_fracture_set_debris_volumes(&vols);

  // 11×11×11 stone (~1331 cells) floating above the floor — larger than one volume slot.
  const int cx = 24, cy = 24, z0 = floor_z + 4;
  int expected = 0;
  for (int dz = 0; dz < 11; dz++)
    for (int dy = 0; dy < 11; dy++)
      for (int dx = 0; dx < 11; dx++)
      {
        world_set_voxel(w, (uint32_t)(cx + dx - 5), (uint32_t)(cy + dy - 5),
                        (uint32_t)(z0 + dz), VOXEL_STONE);
        expected++;
      }

  // Seed disconnect from air under the blob; flood should lift the whole unsupported mass.
  const int extracted = voxel_fracture_disconnect_at(w, cx, cy, z0 - 1);
  const int active = voxel_debris_volume_active_count(&vols);
  const int falling = voxel_fracture_falling_count(w);
  printf("       extracted=%d active_volumes=%d falling=%d expected_cells=%d cap=%d\n", extracted,
         active, falling, expected, DEBRIS_VOLUME_CELL_CAP);

  report("oversized island extracted as debris volumes", extracted >= DEBRIS_VOLUME_CELL_CAP);
  report("more than one volume was allocated", active >= 2);
  report("little or no on-grid falling remained", falling < expected / 10);

  int steps = 0;
  while (voxel_debris_volume_active_count(&vols) > 0 && steps < 720)
  {
    voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
    steps++;
  }
  report("batched volumes eventually write back", voxel_debris_volume_active_count(&vols) == 0);

  voxel_fracture_set_debris_volumes(NULL);
  world_destroy(w);
}

static void test_debris_volume_mass_inertia(void)
{
  printf("\n-- heavier debris volumes accelerate less from the same impulse --\n");

  const int size = 32, floor_z = 2;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }

  DebrisVolumeSystem vols;
  voxel_debris_volume_reset(&vols);

  int xs[8], ys[8], zs[8], n = 0;
  for (int dz = 0; dz < 2; dz++)
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++)
      {
        xs[n] = 8 + dx;
        ys[n] = 8 + dy;
        zs[n] = floor_z + 6 + dz;
        world_set_voxel(w, (uint32_t)xs[n], (uint32_t)ys[n], (uint32_t)zs[n], VOXEL_STONE);
        n++;
      }
  report("stone block extracted", voxel_debris_volume_extract(&vols, w, xs, ys, zs, n) == 8);

  int xl[8], yl[8], zl[8], nl = 0;
  for (int dz = 0; dz < 2; dz++)
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++)
      {
        xl[nl] = 16 + dx;
        yl[nl] = 16 + dy;
        zl[nl] = floor_z + 6 + dz;
        world_set_voxel(w, (uint32_t)xl[nl], (uint32_t)yl[nl], (uint32_t)zl[nl], VOXEL_LEAVES);
        nl++;
      }
  report("leaf block extracted", voxel_debris_volume_extract(&vols, w, xl, yl, zl, nl) == 8);

  DebrisVolume *stone = &vols.items[0];
  DebrisVolume *leaves = &vols.items[1];
  report("stone is heavier than leaves", stone->inv_mass < leaves->inv_mass * 0.5f);

  const float jx = 6.0f;
  voxel_debris_volume_apply_impulse(stone, jx, 0.0f, 0.0f, stone->x + stone->com_lx,
                                    stone->y + stone->com_ly, stone->z + stone->com_lz);
  voxel_debris_volume_apply_impulse(leaves, jx, 0.0f, 0.0f, leaves->x + leaves->com_lx,
                                    leaves->y + leaves->com_ly, leaves->z + leaves->com_lz);
  report("the same impulse accelerates leaves more than stone", leaves->vx > stone->vx * 1.5f);

  int xb[27], yb[27], zb[27], nb = 0;
  for (int dz = 0; dz < 3; dz++)
    for (int dy = 0; dy < 3; dy++)
      for (int dx = 0; dx < 3; dx++)
      {
        xb[nb] = 22 + dx;
        yb[nb] = 8 + dy;
        zb[nb] = floor_z + 8 + dz;
        world_set_voxel(w, (uint32_t)xb[nb], (uint32_t)yb[nb], (uint32_t)zb[nb], VOXEL_STONE);
        nb++;
      }
  report("large stone extracted", voxel_debris_volume_extract(&vols, w, xb, yb, zb, nb) == 27);
  DebrisVolume *big = &vols.items[2];
  stone->vx = stone->vy = stone->vz = 0.0f;
  voxel_debris_volume_apply_impulse(stone, jx, 0.0f, 0.0f, stone->x + stone->com_lx,
                                    stone->y + stone->com_ly, stone->z + stone->com_lz);
  voxel_debris_volume_apply_impulse(big, jx, 0.0f, 0.0f, big->x + big->com_lx, big->y + big->com_ly,
                                    big->z + big->com_lz);
  report("a bigger stone pile accelerates less than a small one", big->vx < stone->vx * 0.5f);

  // Free-flight drag: same initial slide, massive pile sheds speed faster.
  stone->vx = big->vx = 8.0f;
  stone->vy = big->vy = 0.0f;
  stone->vz = big->vz = 0.0f;
  stone->wx = stone->wy = stone->wz = 0.0f;
  big->wx = big->wy = big->wz = 0.0f;
  stone->z = (float)(floor_z + 18);
  big->z = (float)(floor_z + 18);
  for (int i = 0; i < 24; i++)
    voxel_debris_volume_step(&vols, w, 1.0f / 60.0f, 20.0f);
  report("a bigger stone pile sheds horizontal speed faster in air", big->vx < stone->vx * 0.92f);

  world_destroy(w);
}

// ---------------------------------------------------------------------------------------------
// Gravity arc and impact blast (splash damage / temperature)
// ---------------------------------------------------------------------------------------------

static void test_projectile_gravity_and_blast(void)
{
  printf("\n-- gravity arc and impact blast --\n");

  const int size = 32, floor_z = 8;
  World *w = make_arena(size, floor_z);
  if (!w)
  {
    report("arena built", false);
    return;
  }
  world_set_gravity(w, GRAVITY_DEFAULT);

  // Flat for the gravity delay, then a slow sink. Half a second is still inside the delay window;
  // a longer flight past the delay must have negative vz and a lower z.
  {
    ProjectileSystem sys;
    projectile_system_reset(&sys, 0x600Du);
    const float launch_z = (float)(floor_z + 6);
    const ProjectileSpawn spawn = {.x = 4.5f,
                                   .y = 12.5f,
                                   .z = launch_z,
                                   .dir_x = 1.0f,
                                   .dir_y = 0.0f,
                                   .dir_z = 0.0f,
                                   .speed = PLAYER_FIREBALL_SPEED,
                                   .life = PLAYER_FIREBALL_LIFE_S,
                                   .radius = PLAYER_FIREBALL_RADIUS,
                                   .gravity_scale = PLAYER_FIREBALL_GRAVITY_SCALE,
                                   .gravity_delay = PLAYER_FIREBALL_GRAVITY_DELAY_S,
                                   .damage = PLAYER_FIREBALL_DAMAGE,
                                   .kind = PROJECTILE_FIREBALL};
    report("gravity-scaled fireball spawns", projectile_spawn(&sys, &spawn));

    for (int i = 0; i < 30; i++) // 0.5 s — still inside the delay
      projectile_system_step(&sys, w, 1.0f / 60.0f, NULL, 0, NULL);

    const Projectile *p = NULL;
    for (int i = 0; i < PROJECTILE_MAX; i++)
      if (sys.items[i].active)
      {
        p = &sys.items[i];
        break;
      }
    report("the fireball is still in flight after half a second", p != NULL);
    if (p)
    {
      report("gravity has not yet pulled the fireball down during the delay",
             fabsf(p->vz) < 0.05f && fabsf(p->z - launch_z) < 0.05f);
    }

    for (int i = 0; i < 60; i++) // +1.0 s past the delay
      projectile_system_step(&sys, w, 1.0f / 60.0f, NULL, 0, NULL);

    p = NULL;
    for (int i = 0; i < PROJECTILE_MAX; i++)
      if (sys.items[i].active)
      {
        p = &sys.items[i];
        break;
      }
    report("the fireball is still in flight after the delay", p != NULL);
    if (p)
    {
      report("gravity pulls the fireball's vertical speed downward after the delay",
             p->vz < -0.5f);
      report("the fireball has fallen below its launch height after the delay",
             p->z < launch_z - 0.2f);
    }
  }

  // Cast path stamps the gravity scale and delay so lobbed spells stay lobbed.
  {
    GameState *state = make_state(w, 8.5f, 12.5f, (float)(floor_z + 2));
    if (!state)
    {
      report("cast state built", false);
    }
    else
    {
      report("fireball cast succeeds with gravity",
             player_controls_cast_fireball_at(state, &state->controls, 18.5f, 12.5f,
                                              state->player_world_z + 2.0f));
      bool scaled = false;
      for (int i = 0; i < PROJECTILE_MAX; i++)
      {
        const Projectile *p = &state->projectiles.items[i];
        if (p->active && p->kind == PROJECTILE_FIREBALL &&
            fabsf(p->gravity_scale - PLAYER_FIREBALL_GRAVITY_SCALE) < 1e-4f &&
            fabsf(p->gravity_delay - PLAYER_FIREBALL_GRAVITY_DELAY_S) < 1e-4f)
          scaled = true;
      }
      report("a cast fireball carries the gravity scale and delay", scaled);
      destroy_state(state);
    }
  }

  // Splash: a fireball that hits one actor also hurts a bystander, and a wall impact heats
  // neighbouring stone. Ice cools neighbouring water the same way.
  {
    GameState *state = make_state(w, 6.5f, 12.5f, (float)(floor_z + 2));
    if (!state)
    {
      report("blast state built", false);
      world_destroy(w);
      return;
    }

    Actor primary;
    memset(&primary, 0, sizeof(primary));
    primary.id = 9001;
    strncpy(primary.name, "direct", sizeof(primary.name) - 1);
    primary.x = 12.5;
    primary.y = 12.5;
    primary.z = (double)(floor_z + 2);
    primary.health = 100;
    primary.is_active = true;

    Actor splash;
    memset(&splash, 0, sizeof(splash));
    splash.id = 9002;
    strncpy(splash.name, "splash", sizeof(splash.name) - 1);
    splash.x = 12.5;
    splash.y = 14.0; // ~1.5 voxels off the primary — inside blast radius
    splash.z = (double)(floor_z + 2);
    splash.health = 100;
    splash.is_active = true;

    Actor actors[2] = {primary, splash};
    w->runtime_actors = actors;
    w->runtime_actor_count = 2;

    const float fly_z = (float)(floor_z + 2) + 0.5f;
    const ProjectileSpawn spawn = {.x = 6.5f,
                                   .y = 12.5f,
                                   .z = fly_z,
                                   .dir_x = 1.0f,
                                   .dir_y = 0.0f,
                                   .dir_z = 0.0f,
                                   .speed = PLAYER_FIREBALL_SPEED,
                                   .life = PLAYER_FIREBALL_LIFE_S,
                                   .radius = PLAYER_FIREBALL_RADIUS,
                                   .gravity_scale = 0.0f, // keep the aim honest for the splash check
                                   .damage = PLAYER_FIREBALL_DAMAGE,
                                   .kind = PROJECTILE_FIREBALL};
    projectile_spawn(&state->projectiles, &spawn);
    for (int i = 0; i < 400 && projectile_active_count(&state->projectiles) > 0; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);

    report("the direct target took full fireball damage",
           actors[0].health == 100 - PLAYER_FIREBALL_DAMAGE);
    report("a nearby actor took splash damage",
           actors[1].health < 100 && actors[1].health > actors[0].health);

    // Clear actors and fire into a stone pillar so the blast centre is the wall itself.
    w->runtime_actors = NULL;
    w->runtime_actor_count = 0;
    world_set_voxel(w, 16, 12, (uint32_t)(floor_z + 2), VOXEL_STONE);
    world_set_voxel(w, 17, 12, (uint32_t)(floor_z + 2), VOXEL_STONE);
    Voxel *neighbour = world_get_voxel(w, 17, 12, (uint32_t)(floor_z + 2));
    if (neighbour)
      voxel_set_temperature(neighbour, VOXEL_TEMP_AMBIENT);

    const ProjectileSpawn wall_shot = {.x = 8.5f,
                                       .y = 12.5f,
                                       .z = fly_z,
                                       .dir_x = 1.0f,
                                       .dir_y = 0.0f,
                                       .dir_z = 0.0f,
                                       .speed = PLAYER_FIREBALL_SPEED,
                                       .life = PLAYER_FIREBALL_LIFE_S,
                                       .radius = PLAYER_FIREBALL_RADIUS,
                                       .gravity_scale = 0.0f,
                                       .damage = PLAYER_FIREBALL_DAMAGE,
                                       .kind = PROJECTILE_FIREBALL};
    projectile_spawn(&state->projectiles, &wall_shot);
    for (int i = 0; i < 400 && projectile_active_count(&state->projectiles) > 0; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);

    neighbour = world_get_voxel(w, 17, 12, (uint32_t)(floor_z + 2));
    report("the blast heats neighbouring stone",
           neighbour && voxel_get_temperature(neighbour) > VOXEL_TEMP_AMBIENT);

    world_set_voxel(w, 16, 12, (uint32_t)(floor_z + 2), VOXEL_STONE);
    world_set_voxel(w, 17, 12, (uint32_t)(floor_z + 2), VOXEL_WATER);
    Voxel *puddle = world_get_voxel(w, 17, 12, (uint32_t)(floor_z + 2));
    if (puddle)
      voxel_set_temperature(puddle, VOXEL_TEMP_AMBIENT);

    const ProjectileSpawn ice = {.x = 8.5f,
                                 .y = 12.5f,
                                 .z = fly_z,
                                 .dir_x = 1.0f,
                                 .dir_y = 0.0f,
                                 .dir_z = 0.0f,
                                 .speed = PLAYER_ICE_BOLT_SPEED,
                                 .life = PLAYER_ICE_BOLT_LIFE_S,
                                 .radius = PLAYER_ICE_BOLT_RADIUS,
                                 .gravity_scale = 0.0f,
                                 .damage = PLAYER_ICE_BOLT_DAMAGE,
                                 .kind = PROJECTILE_ICE_BOLT,
                                 .potency = 100};
    projectile_spawn(&state->projectiles, &ice);
    for (int i = 0; i < 400 && projectile_active_count(&state->projectiles) > 0; i++)
      game_state_step_projectiles(state, 1.0 / 60.0);

    puddle = world_get_voxel(w, 17, 12, (uint32_t)(floor_z + 2));
    const uint8_t cool_temp = puddle ? voxel_get_temperature(puddle) : 255;
    report("ice blast cools neighbouring water",
           puddle && (puddle->type == VOXEL_ICE || cool_temp < VOXEL_TEMP_AMBIENT));

    destroy_state(state);
  }

  world_destroy(w);
}

int main(void)
{
  printf("=== Foliage, Projectile and Fireball Tests ===\n");

  // The skill stamps cooldowns with SDL_GetTicks.
  if (SDL_Init(SDL_INIT_TIMER) != 0)
  {
    printf("FAILED SDL_Init(timer): %s\n", SDL_GetError());
    return 1;
  }

  test_classification();
  test_walking_through_foliage();
  test_projectile_flight();
  test_projectile_crosses_world_boundary();
  test_foliage_probability();
  test_actor_hits();
  test_fireball_skill();
  test_fireball_voxel_damage();
  test_projectile_gravity_and_blast();
  test_tree_disconnect();
  test_tree_debris_volume();
  test_debris_volume_stack();
  test_debris_volume_spin();
  test_debris_volume_tip();
  test_debris_volume_tumble();
  test_debris_volume_secondary_shatter();
  test_debris_volume_scale();
  test_debris_volume_mass_inertia();
  test_place_vs_body();
  test_debris_material_friction();

  SDL_Quit();

  printf("\n=== %s (%d failure%s) ===\n", test_failures == 0 ? "ALL PASSED" : "FAILURES",
         test_failures, test_failures == 1 ? "" : "s");
  return test_failures == 0 ? 0 : 1;
}
