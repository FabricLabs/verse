/*
 * world_generation_dispatch.c - Main world generation dispatcher
 *
 * This module handles the main world_generate function that dispatches
 * to specific world generators based on the requested type.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "world_internal.h"
#include "world_generation.h"
#include "world_core.h"
#include "world_voxel.h"
#include "noise-c/src/crypto/sha2/sha256.h"

// Random number generator state
static uint32_t g_rand_state = 1;

// Initialize random number generator from seed
static void seed_random(const char *seed)
{
  g_rand_state = 1;
  if (seed)
  {
    // Simple hash of seed string
    while (*seed)
    {
      g_rand_state = g_rand_state * 31 + (unsigned int)*seed;
      seed++;
    }
  }
}

// Get next random number
static uint32_t next_random(void)
{
  g_rand_state = g_rand_state * 1103515245 + 12345;
  return g_rand_state;
}

// Generate deterministic gravity from seed
static float generate_gravity_from_seed(const char *seed)
{
  uint32_t state = 1;
  if (seed)
  {
    // Hash the seed to get a value
    while (*seed)
    {
      state = state * 31 + (unsigned int)*seed;
      seed++;
    }
  }

  // Generate gravity between 4.0 and 15.0 m/s²
  float normalized = (float)(state % 10000) / 10000.0f;
  return 4.0f + normalized * 11.0f;
}

// Calculate SHA256 hash
static void calculate_sha256(const char *input, uint8_t *output)
{
  sha256_context_t ctx;
  sha256_reset(&ctx);
  sha256_update(&ctx, (const uint8_t *)input, strlen(input));
  sha256_finish(&ctx, output);
}

// Main world generation function - simplified dispatch
void world_generate(World *world, const char *seed)
{
  if (!world)
    return;

  // Default to SCOURED type if not specified
  world_generate_with_type(world, seed, WORLD_TYPE_SCOURED);
}

// Generate world with specific type
void world_generate_with_type(World *world, const char *seed, WorldType type)
{
  if (!world)
    return;

  printf("[DEBUG] world_generate_with_type called: world=%p, seed=%s, type=%d\n",
         (void *)world, seed ? seed : "NULL", type);

  // Set the generation type
  world->generation_type = (uint32_t)type;

  // Initialize the seeded random number generator
  seed_random(seed);
  seed_rand_with_world_seed(seed); // Also init the generator module's RNG

  // Generate deterministic gravity from seed
  world->gravity = generate_gravity_from_seed(seed);

  // Derive per-world identifiers and RNG from double SHA256
  {
    uint8_t h1[32], h2[32];
    calculate_sha256(seed ? seed : "", h1);
    calculate_sha256((const char *)h1, h2);
    for (int i = 0; i < 32; i++)
      sprintf(world->seed_id + i * 2, "%02x", h2[i]);
    world->seed_id[64] = '\0';
    world->rng_state = ((uint32_t)h2[0] << 24) | ((uint32_t)h2[1] << 16) |
                       ((uint32_t)h2[2] << 8) | (uint32_t)h2[3];

    // Base rarity between ~1% and ~15%
    world->rng_state = world->rng_state * 1103515245u + 12345u;
    uint16_t r16 = (uint16_t)((world->rng_state >> 16) & 0xFFFF);
    float base = (float)r16 / 65535.0f;   // 0..1
    world->rarity = 0.01f + base * 0.14f; // 0.01..0.15
  }

  // Dispatch to appropriate generator
  switch (type)
  {
  case WORLD_TYPE_HOME:
    world_generate_home(world, seed);
    break;
  case WORLD_TYPE_FARM:
    world_generate_farm(world, seed);
    break;
  case WORLD_TYPE_RANDOM:
    world_generate_random(world, seed);
    break;
  case WORLD_TYPE_WILDERNESS:
    world_generate_wilderness(world, seed);
    break;
  case WORLD_TYPE_SOLID:
    world_generate_solid_fill(world, VOXEL_STONE);
    break;
  case WORLD_TYPE_UNDERWORLD:
    world_generate_underworld(world, seed);
    break;
  case WORLD_TYPE_SCOURED:
    world_generate_scoured(world, seed);
    break;
  case WORLD_TYPE_CLOUD:
    world_generate_cloud(world, seed);
    break;
  case WORLD_TYPE_ARENA:
    world_generate_arena(world, seed);
    break;
  case WORLD_TYPE_LABYRINTH_SQUARE:
    world_generate_labyrinth(world, seed);
    break;
  case WORLD_TYPE_WFC_TOWN:
    world_generate_wfc_town(world, seed);
    break;
  default:
    // Default to home type
    world_generate_home(world, seed);
    break;
  }

  // Record genesis Fabric message (gameplay event)
  char genesis_metadata[96];
  snprintf(genesis_metadata, sizeof(genesis_metadata), "seed=%s", world->seed_id);
  world_append_log(world, "EVENT GENESIS");
  world_append_log(world, genesis_metadata);
  world->vector_clock++;
}

// Generate with specific fill type (for SOLID worlds)
void world_generate_with_type_and_fill(World *world, const char *seed, WorldType type, VoxelType fill_type)
{
  if (!world)
    return;

  if (type == WORLD_TYPE_SOLID)
  {
    world->generation_type = (uint32_t)type;
    seed_random(seed);
    world->gravity = generate_gravity_from_seed(seed);
    world_generate_solid_fill(world, fill_type);
    return;
  }

  world_generate_with_type(world, seed, type);
}

// Append Fabric message to world log
// These are structured event messages that form the sequence needed to validate
// world state over gameplay sessions (e.g., "EVENT GENESIS", "EVENT AUTOCROP", etc.)
void world_append_log(World *world, const char *fabric_message)
{
  if (!world || !fabric_message)
    return;

  // TODO: Properly append to world->log buffer
  // For now, just print to indicate the event was recorded
  // In full implementation, this would append the Fabric message to world->log
  // with proper formatting and timestamps
  printf("[FABRIC MESSAGE] %s\n", fabric_message);
}

// Get world type from string
WorldType world_type_from_string(const char *type_str)
{
  if (!type_str)
    return WORLD_TYPE_UNKNOWN;

  if (strcmp(type_str, "HOME") == 0)
    return WORLD_TYPE_HOME;
  else if (strcmp(type_str, "FARM") == 0)
    return WORLD_TYPE_FARM;
  else if (strcmp(type_str, "RANDOM") == 0)
    return WORLD_TYPE_RANDOM;
  else if (strcmp(type_str, "WILDERNESS") == 0)
    return WORLD_TYPE_WILDERNESS;
  else if (strcmp(type_str, "SOLID") == 0)
    return WORLD_TYPE_SOLID;
  else if (strcmp(type_str, "UNDERWORLD") == 0)
    return WORLD_TYPE_UNDERWORLD;
  else if (strcmp(type_str, "SCOURED") == 0)
    return WORLD_TYPE_SCOURED;
  else if (strcmp(type_str, "LABYRINTH") == 0)
    return WORLD_TYPE_LABYRINTH_SQUARE;
  else if (strcmp(type_str, "WFC_TOWN") == 0)
    return WORLD_TYPE_WFC_TOWN;
  else if (strcmp(type_str, "CLOUD") == 0)
    return WORLD_TYPE_CLOUD;
  else if (strcmp(type_str, "ARENA") == 0)
    return WORLD_TYPE_ARENA;

  return WORLD_TYPE_UNKNOWN;
}

// Get string from world type
const char *world_type_to_string(WorldType type)
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
