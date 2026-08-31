/*
 * world.c - Core world management (cleaned up version)
 *
 * This file now contains only the remaining functions that haven't been
 * extracted to modular files. Most functionality has been moved to:
 * - world_core.c - World lifecycle management
 * - world_voxel.c - Voxel operations
 * - world_noise.c - Noise generation functions
 * - world_serialize.c - Save/load functionality
 * - world_physics.c - Physics simulation
 * - world_generation_*.c - Various world generators
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <sys/time.h>

#include "world.h"
#include "world_internal.h"
#include "world_core.h"
#include "world_voxel.h"
#include "world_noise.h"
#include "world_serialize.h"
#include "world_physics.h"
#include "world_generation.h"
#include "actor.h"
#include "world_bulk_ops.h"
#include "universe.h"
#include "entropy_field.h"
#include "universe_coords.h"
#include "gpu_voxel_buffer.h"
#include "constants.h"
#include "voxel.h"

// Global universe noise seed (shared permutation)
static unsigned int g_universe_perlin_seed = 1u;
static int g_perlin_ready = 0;

// Forward declaration
extern void init_perlin_noise(unsigned int seed);

// -----------------------------------------------------------------------------
// Universe-wide noise normalization
// -----------------------------------------------------------------------------
static inline float universe_z_bias(void)
{
  // Small additive phase derived from the universe seed; matches prior usage
  return (float)g_universe_perlin_seed * 0.00001f;
}

typedef struct NoiseWarpParams
{
  float amp;   // domain-warp amplitude
  float scale; // domain-warp scale
} NoiseWarpParams;

// Canonical warp parameter suggestions (reference; not yet fully applied)
static const NoiseWarpParams WARP_OCCUPANCY = {3.1f, 0.0053f};
static const NoiseWarpParams WARP_RARITY = {1.2f, 0.0043f};
static const NoiseWarpParams WARP_STONE_COARSE = {2.7f, 0.0061f};
static const NoiseWarpParams WARP_STONE_FINE = {1.9f, 0.0097f};
static const NoiseWarpParams WARP_MAGMA_GATE = {1.1f, 0.0071f};
static const NoiseWarpParams WARP_SANDSTONE = {0.9f, 0.0067f};
static const NoiseWarpParams WARP_JITTER = {1.3f, 0.0077f};

static inline void ensure_perlin_ready(void)
{
  if (!g_perlin_ready)
  {
    init_perlin_noise(g_universe_perlin_seed);
    g_perlin_ready = 1;
  }
}

void world_set_universe_noise_seed(const char *base_seed)
{
  unsigned int s = 1u;
  if (base_seed && *base_seed)
  {
    const char *p = base_seed;
    while (*p)
    {
      s = s * 31u + (unsigned char)(*p++);
    }
  }
  g_universe_perlin_seed = s;
  init_perlin_noise(g_universe_perlin_seed);
}

// GPU voxel buffer refresh stub
bool world_refresh_occupancy_bitfield(World *world)
{
  // No-op in this build: occupancy bitfield updates are disabled here and may
  // be handled by the editor/runtime where GPU helpers are linked.
  (void)world;
  return false;
}

// Fast height calculation using occupancy bitfield
static inline int bit_is_set(const uint8_t *bits, uint32_t idx)
{
  return (bits[idx >> 3u] >> (idx & 7u)) & 1u;
}

int world_height_at_fast(const World *world, int x, int y)
{
  if (!world || x < 0 || y < 0 || x >= (int)world->width || y >= (int)world->height)
    return -1;

  // Use bitfield if present
  if (world->occupancy_bits && world->occupancy_bits->bits &&
      world->occupancy_bits->width == world->width &&
      world->occupancy_bits->height == world->height &&
      world->occupancy_bits->depth == world->depth)
  {
    const uint32_t W = world->occupancy_bits->width;
    const uint32_t H = world->occupancy_bits->height;
    const uint32_t D = world->occupancy_bits->depth;
    const uint8_t *bits = world->occupancy_bits->bits;
    for (int z = (int)D - 1; z >= 0; z--)
    {
      uint32_t idx = ((uint32_t)z * H + (uint32_t)y) * W + (uint32_t)x;
      if (bit_is_set(bits, idx))
        return z;
    }
    return -1;
  }

  // Fallback to direct voxel check
  return world_height_at(world, x, y);
}

// World stepping functions (delegates to physics module)
void world_step(World *world, float dt_seconds)
{
  if (!world)
    return;

  // Step physics systems
  world_step_actors(world, dt_seconds);
  world_step_fluids(world, 1000); // Process up to 1000 fluid cells
  world_step_temperature(world, 500); // Process up to 500 temperature updates

  // Update vector clock
  world->vector_clock++;
}

// Condition voxel management
void world_add_condition_voxel(World *world, uint32_t condition, uint32_t index)
{
  if (!world || condition >= 64)
    return;

  struct VoxelIndexList *list = &world->condition_voxel_indices[condition];

  // Grow array if needed
  if (list->size >= list->cap)
  {
    size_t new_cap = list->cap ? list->cap * 2 : 16;
    uint32_t *new_indices = realloc(list->indices, new_cap * sizeof(uint32_t));
    if (!new_indices)
      return;
    list->indices = new_indices;
    list->cap = new_cap;
  }

  list->indices[list->size++] = index;
}

void world_remove_condition_voxel(World *world, uint32_t condition, uint32_t index)
{
  if (!world || condition >= 64)
    return;

  struct VoxelIndexList *list = &world->condition_voxel_indices[condition];

  // Find and remove
  for (size_t i = 0; i < list->size; i++)
  {
    if (list->indices[i] == index)
    {
      // Swap with last and shrink
      list->indices[i] = list->indices[list->size - 1];
      list->size--;
      break;
    }
  }
}

void world_clear_condition_indices(World *world)
{
  if (!world)
    return;

  for (int i = 0; i < 64; i++)
  {
    if (world->condition_voxel_indices[i].indices)
    {
      free(world->condition_voxel_indices[i].indices);
      world->condition_voxel_indices[i].indices = NULL;
      world->condition_voxel_indices[i].size = 0;
      world->condition_voxel_indices[i].cap = 0;
    }
  }
}

// Utility functions
const char* world_type_to_string(WorldGenerationType type)
{
  switch (type)
  {
  case WORLD_TYPE_HOME:
    return "HOME";
  case WORLD_TYPE_FARM:
    return "FARM";
  case WORLD_TYPE_RANDOM:
    return "RANDOM";
  case WORLD_TYPE_WILDERNESS:
    return "WILDERNESS";
  case WORLD_TYPE_SOLID:
    return "SOLID";
  case WORLD_TYPE_UNDERWORLD:
    return "UNDERWORLD";
  case WORLD_TYPE_SCOURED:
    return "SCOURED";
  case WORLD_TYPE_LABYRINTH_SQUARE:
    return "LABYRINTH";
  case WORLD_TYPE_WFC_TOWN:
    return "WFC_TOWN";
  case WORLD_TYPE_CLOUD:
    return "CLOUD";
  case WORLD_TYPE_ARENA:
    return "ARENA";
  default:
    return "UNKNOWN";
  }
}

WorldGenerationType world_type_from_string(const char* str)
{
  if (!str)
    return WORLD_TYPE_HOME;

  if (strcmp(str, "HOME") == 0)
    return WORLD_TYPE_HOME;
  else if (strcmp(str, "FARM") == 0)
    return WORLD_TYPE_FARM;
  else if (strcmp(str, "RANDOM") == 0)
    return WORLD_TYPE_RANDOM;
  else if (strcmp(str, "WILDERNESS") == 0)
    return WORLD_TYPE_WILDERNESS;
  else if (strcmp(str, "SOLID") == 0)
    return WORLD_TYPE_SOLID;
  else if (strcmp(str, "UNDERWORLD") == 0)
    return WORLD_TYPE_UNDERWORLD;
  else if (strcmp(str, "SCOURED") == 0)
    return WORLD_TYPE_SCOURED;
  else if (strcmp(str, "LABYRINTH") == 0)
    return WORLD_TYPE_LABYRINTH_SQUARE;
  else if (strcmp(str, "WFC_TOWN") == 0)
    return WORLD_TYPE_WFC_TOWN;
  else if (strcmp(str, "CLOUD") == 0)
    return WORLD_TYPE_CLOUD;
  else if (strcmp(str, "ARENA") == 0)
    return WORLD_TYPE_ARENA;
  else
    return WORLD_TYPE_HOME;
}

// Log management
void world_append_log(World *world, const char *message)
{
  if (!world || !message)
    return;

  size_t old_len = world->log ? strlen(world->log) : 0;
  size_t msg_len = strlen(message);
  size_t new_len = old_len + msg_len + 2; // +1 for newline, +1 for null

  char *new_log = realloc(world->log, new_len);
  if (!new_log)
    return;

  if (old_len > 0)
  {
    new_log[old_len] = '\n';
    strcpy(new_log + old_len + 1, message);
  }
  else
  {
    strcpy(new_log, message);
  }

  world->log = new_log;
}

// Actor management (runtime)
bool world_add_runtime_actor(World* world, struct Actor* actor)
{
  if (!world || !actor)
    return false;

  // Ensure capacity
  if (world->runtime_actor_count >= world->runtime_actor_capacity)
  {
    int new_capacity = world->runtime_actor_capacity ? world->runtime_actor_capacity * 2 : 16;
    struct Actor* new_actors = realloc(world->runtime_actors, new_capacity * sizeof(struct Actor));
    if (!new_actors)
      return false;
    world->runtime_actors = new_actors;
    world->runtime_actor_capacity = new_capacity;
  }

  // Add actor
  world->runtime_actors[world->runtime_actor_count++] = *actor;
  return true;
}

void world_clear_runtime_actors(World* world)
{
  if (!world)
    return;

  if (world->runtime_actors)
  {
    free(world->runtime_actors);
    world->runtime_actors = NULL;
  }
  world->runtime_actor_count = 0;
  world->runtime_actor_capacity = 0;
}

// Level calculation
void world_update_level(World* world)
{
  if (!world)
    return;

  // Score = ln(vector_clock) + ln(history_event_count) + ln(unique_player_count) + base_level
  double vc_component = world->vector_clock > 0 ? log(world->vector_clock) : 0;
  double he_component = world->history_event_count > 0 ? log(world->history_event_count) : 0;
  double up_component = world->unique_player_count > 0 ? log(world->unique_player_count) : 0;

  world->score = vc_component + he_component + up_component + world->base_level;

  // Level = ln(score), minimum 0
  world->level = world->score > 1 ? log(world->score) : 0;
}

// Evolution epoch management
void world_init_epoch_maps(World* world)
{
  if (!world || !world->voxels)
    return;

  size_t total_voxels = (size_t)world->width * world->height * world->depth;

  // Allocate bloom epoch map
  if (!world->bloom_epoch_map)
  {
    world->bloom_epoch_map = malloc(total_voxels * sizeof(uint32_t));
    if (world->bloom_epoch_map)
    {
      for (size_t i = 0; i < total_voxels; i++)
      {
        world->bloom_epoch_map[i] = UINT32_MAX; // Unset
      }
    }
  }

  // Allocate decaying epoch map
  if (!world->decaying_epoch_map)
  {
    world->decaying_epoch_map = malloc(total_voxels * sizeof(uint32_t));
    if (world->decaying_epoch_map)
    {
      for (size_t i = 0; i < total_voxels; i++)
      {
        world->decaying_epoch_map[i] = UINT32_MAX; // Unset
      }
    }
  }
}

void world_free_epoch_maps(World* world)
{
  if (!world)
    return;

  if (world->bloom_epoch_map)
  {
    free(world->bloom_epoch_map);
    world->bloom_epoch_map = NULL;
  }

  if (world->decaying_epoch_map)
  {
    free(world->decaying_epoch_map);
    world->decaying_epoch_map = NULL;
  }
}

// Distance field computation for optimization
float world_compute_distance_to_surface(const World* world, int x, int y, int z)
{
  if (!world)
    return -1.0f;

  // Simple implementation: find nearest solid voxel
  float min_dist_sq = INFINITY;
  int search_radius = 5; // Limited search for performance

  for (int dz = -search_radius; dz <= search_radius; dz++)
  {
    for (int dy = -search_radius; dy <= search_radius; dy++)
    {
      for (int dx = -search_radius; dx <= search_radius; dx++)
      {
        int nx = x + dx;
        int ny = y + dy;
        int nz = z + dz;

        if (nx >= 0 && ny >= 0 && nz >= 0 &&
            nx < (int)world->width && ny < (int)world->height && nz < (int)world->depth)
        {
          Voxel* v = world_get_voxel((World*)world, nx, ny, nz);
          if (v && v->type != VOXEL_AIR)
          {
            float dist_sq = dx*dx + dy*dy + dz*dz;
            if (dist_sq < min_dist_sq)
            {
              min_dist_sq = dist_sq;
            }
          }
        }
      }
    }
  }

  return sqrtf(min_dist_sq);
}
