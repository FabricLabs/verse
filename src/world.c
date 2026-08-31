#include <pthread.h>

// Forward declare Perlin init used by universe seed setter
static void init_perlin_noise(unsigned int seed);

// Global universe noise seed (shared permutation)
static unsigned int g_universe_perlin_seed = 1u;
static int g_perlin_ready = 0;

// The permutation table and the entropy fields are derived from the universe seed and never change
// once built, so sharing them across threads is fine — but they were built lazily on first use, and
// a check-then-act like that lets two generating threads both decide the table is missing. One then
// samples a permutation the other is still shuffling. These make first use atomic without changing
// which seed wins: still the first caller's, exactly as before.
//
// world_generation_prepare_shared_state builds both up front so the pool never races to be first.
// The guards stay anyway, so a caller that forgets to prepare gets a slow first sample rather than a
// torn table.
static pthread_once_t g_perlin_once = PTHREAD_ONCE_INIT;
static pthread_once_t g_entropy_fields_once = PTHREAD_ONCE_INIT;

static void perlin_init_once(void)
{
  init_perlin_noise(g_universe_perlin_seed);
  g_perlin_ready = 1;
}

// -----------------------------------------------------------------------------
// Universe-wide noise normalization
// - Use this z-bias whenever building z-phases so all fields share a consistent
//   universe phase component across all worlds.
// - Keep warp parameters in one place so we can gradually normalize callers.
// TODO:
//   - All rotated_noise_coords[_warped] calls have been replaced with pure 4D Perlin noise
//     warp parameter sets below
//   - Replace ad-hoc perlin calls with helpers that inject universe_z_bias()
//   - Audit all gates to ensure consistent inequality direction and scaling
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
  pthread_once(&g_perlin_once, perlin_init_once);
}

// Setup only. This rewrites a table every generating thread samples, so it must be called before
// generation starts and never alongside it.
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
  // Retiring the once first means a later ensure_perlin_ready cannot decide the table still needs
  // building and re-shuffle it out from under the seed just set.
  pthread_once(&g_perlin_once, perlin_init_once);
  init_perlin_noise(g_universe_perlin_seed);
}
// world.c
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "gpu_voxel_buffer.h"
#include "world_gen_profile.h"
#include "universe_coords.h"
#include "voxel_shape.h"
// Forward declaration to avoid circular dependency
struct Universe;
// One bit per voxel instead of a 48-byte Voxel, so the solid/empty question that the renderer's
// face culling and world_height_at_fast ask millions of times per frame reads 256KB rather than
// 96MB at 128^3. Both already prefer this buffer when it exists; this builds it.
//
// world_set_voxel keeps the bits current for single writes, so this only needs calling after bulk
// changes — generation, load, region fills.
bool world_refresh_occupancy_bitfield(World *world)
{
  if (!world || !world->voxels)
    return false;

  // The heightmap is derived from the same voxels, so a bulk change that needed this refresh
  // invalidated it too. Rebuilding here keeps the two from disagreeing.
  if (world->heightmap)
    world_build_heightmap(world);

  if (world->occupancy_bits)
  {
    // Dimensions can change under a world across a load; rebuild rather than write out of bounds.
    if (world->occupancy_bits->width == world->width &&
        world->occupancy_bits->height == world->height &&
        world->occupancy_bits->depth == world->depth)
      return gpu_voxel_buffer_update_from_world(world->occupancy_bits, world);

    gpu_voxel_buffer_destroy(world->occupancy_bits);
    world->occupancy_bits = NULL;
  }

  world->occupancy_bits = gpu_voxel_buffer_create_from_world(world);
  world_refresh_occupied_z_range(world);
  return world->occupancy_bits != NULL;
}

// Throw away everything derived from the voxel array, in O(1), for code that writes voxels
// directly and cannot cheaply say what it changed.
//
// This is the safe counterpart to world_refresh_occupancy_bitfield: dropping the bitfield sends
// the renderer back to reading voxel types, which is slower but always right, whereas leaving a
// stale bitfield in place would have it treat occupied cells as air and drop terrain from the
// frame. Callers that can afford a pass should refresh instead.
void world_discard_derived_caches(World *world)
{
  if (!world)
    return;

  if (world->occupancy_bits)
  {
    gpu_voxel_buffer_destroy(world->occupancy_bits);
    world->occupancy_bits = NULL;
  }
  // The heightmap has to go for a different reason than the others: it is exact rather than
  // conservative, so a stale entry is a wrong answer, not a slow one. Dropping it sends column
  // queries back to scanning.
  free(world->heightmap);
  world->heightmap = NULL;
  world->fluid_presence = WORLD_FLUID_UNKNOWN;
  world->occupied_z_min = -1;
  world->occupied_z_max = -1;
  world->voxel_revision++;
}

// Recompute the occupied z band exactly. One linear pass, done alongside the bitfield rebuild
// rather than per frame.
void world_refresh_occupied_z_range(World *world)
{
  if (!world || !world->voxels)
    return;

  const int d = (int)world->depth;
  const size_t layer = (size_t)world->width * world->height;
  world->occupied_z_min = -1;
  world->occupied_z_max = -1;

  for (int z = 0; z < d; z++)
  {
    const Voxel *slice = &world->voxels[(size_t)z * layer];
    for (size_t i = 0; i < layer; i++)
    {
      if (slice[i].type != VOXEL_AIR)
      {
        if (world->occupied_z_min < 0)
          world->occupied_z_min = z;
        world->occupied_z_max = z;
        break;
      }
    }
  }
}

static inline int bit_is_set(const uint8_t *bits, uint32_t idx)
{
  return (bits[idx >> 3u] >> (idx & 7u)) & 1u;
}

// Keep the heightmap in step with a single solid/air transition, so surface queries stay O(1).
//
// Unlike the occupancy band and the fluid census, this cache cannot be conservative: a column whose
// recorded top is wrong gives a wrong answer rather than a slow one. Both directions are therefore
// handled exactly. Placing a solid is O(1); removing the topmost one costs a walk down that one
// column, and removing anything buried costs nothing.
static inline void world_sync_heightmap(World *world, int x, int y, int z, bool solid)
{
  if (!world->heightmap)
    return;

  const size_t column = (size_t)y * (size_t)world->width + (size_t)x;
  const int current = world->heightmap[column];

  if (solid)
  {
    if (z > current)
      world->heightmap[column] = (int16_t)z;
    return;
  }

  if (z != current)
    return;

  // Read the voxel array rather than the bitfield: this runs from both write paths, and only one of
  // them has updated the bits by this point.
  int next = z - 1;
  while (next >= 0 &&
         world->voxels[((size_t)next * (size_t)world->height + (size_t)y) * (size_t)world->width +
                       (size_t)x]
                 .type == VOXEL_AIR)
    next--;
  world->heightmap[column] = (int16_t)next;
}

bool world_build_heightmap(World *world)
{
  if (!world || !world->voxels)
    return false;
  // Heights are stored as int16_t, so a world deeper than that could not be represented.
  if (world->depth > (uint32_t)INT16_MAX)
    return false;

  const size_t columns = (size_t)world->width * (size_t)world->height;
  if (!world->heightmap)
  {
    world->heightmap = (int16_t *)malloc(columns * sizeof(int16_t));
    if (!world->heightmap)
      return false;
  }

  for (uint32_t y = 0; y < world->height; y++)
  {
    for (uint32_t x = 0; x < world->width; x++)
    {
      int h = -1;
      for (int z = (int)world->depth - 1; z >= 0; z--)
      {
        const size_t idx = ((size_t)z * (size_t)world->height + (size_t)y) * (size_t)world->width +
                           (size_t)x;
        if (world->voxels[idx].type != VOXEL_AIR)
        {
          h = z;
          break;
        }
      }
      world->heightmap[(size_t)y * (size_t)world->width + (size_t)x] = (int16_t)h;
    }
  }
  return true;
}

// Keep the occupancy bitfield in step with a voxel type written directly.
//
// Most code writes voxels through world_set_voxel, which maintains the bitfield itself. The fluid
// simulation instead holds a Voxel* and assigns to ->type, because it has to preserve the packed
// quantity in data8 that world_set_voxel would reset to full. The renderer's face culling reads
// the bitfield, so fluid moved without this would be drawn with the wrong faces or not at all.
static inline void world_sync_occupancy_bit(World *world, int x, int y, int z, VoxelType type)
{
  // Bump the revision before the bitfield check, and whether or not a bitfield exists: this is the
  // choke point for "a solid/air transition happened by direct write", and consumers keyed on the
  // revision need to hear about it either way. Callers only reach here when the type actually
  // changed, so this does not invalidate caches spuriously.
  world->voxel_revision++;
  world_sync_heightmap(world, x, y, z, type != VOXEL_AIR);

  if (!world->occupancy_bits || !world->occupancy_bits->bits ||
      world->occupancy_bits->width != world->width ||
      world->occupancy_bits->height != world->height ||
      world->occupancy_bits->depth != world->depth)
    return;

  const uint32_t lin = ((uint32_t)z * world->height + (uint32_t)y) * world->width + (uint32_t)x;
  uint8_t *bits = world->occupancy_bits->bits;
  if (gpu_voxel_buffer_type_marks_occupancy(type))
    bits[lin >> 3u] |= (uint8_t)(1u << (lin & 7u));
  else
    bits[lin >> 3u] &= (uint8_t)~(1u << (lin & 7u));
}

void world_voxel_type_written(World *world, int x, int y, int z, VoxelType type)
{
  if (!world || !world_pos_in_bounds_fast(world, x, y, z))
    return;
  world_sync_occupancy_bit(world, x, y, z, type);
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
  // Fallback: scan voxels
  for (int z = (int)world->depth - 1; z >= 0; z--)
  {
    const Voxel *v = world_voxel_cptr_fast(world, x, y, z);
    if (v && v->type != VOXEL_AIR)
      return z;
  }
  return -1;
}

int world_height_at_cached(const World *world, int x, int y)
{
  if (!world || x < 0 || y < 0 || x >= (int)world->width || y >= (int)world->height)
    return -1;
  if (world->heightmap)
  {
    const size_t idx = (size_t)y * (size_t)world->width + (size_t)x;
    return world->heightmap[idx];
  }
  return world_height_at_fast(world, x, y);
}

#include <math.h>
#include <sys/time.h>

#include "world.h"
#include "voxel_shape.h"
#include "fluid_sim.h"
#include "particle_effects.h"
#include "actor.h"
#include "mob_ai.h"
#include "world_bulk_ops.h"
#include "universe.h"
#include "entropy_field.h"
#include "universe_coords.h"
#include "universe_biome.h"
#include "settlement.h"
#include "water_erosion.h"
#include "water_table.h"
#include "volcano.h"

// Unified entropy field system (replaces bias system)
static EntropyField g_terrain_height_field = {0};
static EntropyField g_occupancy_field = {0};
static EntropyField g_stone_type_field = {0};
static EntropyField g_ore_density_field = {0};
static EntropyField g_crystal_density_field = {0};
static int g_entropy_fields_initialized = 0;

// Forward declarations for local helpers used later (now that World is defined)
static void layer_apply_dirt_over_clay(World *world, const int *top_of_column);
static void layer_seed_top_dirt(World *world, const int *top_of_column);
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float vlen(float x, float y, float z) { return sqrtf(x * x + y * y + z * z); }

// Forward declarations for soil generation
static inline float calculate_surface_slope(const World *world, uint32_t x, uint32_t y, uint32_t z);
static inline VoxelType select_soil_type_for_stone(const World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType stone_type);
static inline uint32_t calculate_stone_layer_height(const World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType stone_type);
static inline uint32_t calculate_geological_layer(const World *world, float w, uint32_t x, uint32_t y, uint32_t z);
static inline VoxelType get_geological_layer_type(const World *world, uint32_t x, uint32_t y, uint32_t geological_layer);
static inline float calculate_biome_distribution(const World *world, float w, uint32_t x, uint32_t y, uint32_t z);
static inline float calculate_difficulty_distribution(const World *world, float w, uint32_t x, uint32_t y, uint32_t z);
static inline float sample_nine_phase_noise_field(const World *world, float w, float x, float y, float z, float small_weight, float regional_weight, float universal_weight);

// Forward declarations for ore generation
static void apply_ore_generation(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType stone_type, float depth_ratio, uint32_t surface_cap, uint64_t *dbg_count_ore_au, uint64_t *dbg_count_ore_ag, uint64_t *dbg_count_ore_cu);

// Forward declarations for crystal generation
static void apply_crystal_generation(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType stone_type, float depth_ratio, bool enable_crystals, uint64_t *dbg_count_crystals);

// Initialize unified entropy field system
static void entropy_fields_init_once(void)
{
  // Terrain height variation field (controls column height)
  g_terrain_height_field = entropy_field_custom(
      g_universe_perlin_seed,
      0.0063f, 0.5f,  // Large-scale height variation
      0.0137f, 0.35f, // Medium-scale height variation
      0.029f, 0.15f,  // Fine-scale height variation
      1, 1.0f, 0.001f // Domain warping
  );

  // Occupancy variation field (controls terrain density)
  g_occupancy_field = entropy_field_custom(
      g_universe_perlin_seed + 1,
      0.0081f, 0.4f,   // Large-scale density variation
      0.0153f, 0.3f,   // Medium-scale density variation
      0.031f, 0.3f,    // Fine-scale density variation
      1, 0.8f, 0.0015f // Domain warping
  );

  // Stone type variation field (controls rock type distribution)
  g_stone_type_field = entropy_field_custom(
      g_universe_perlin_seed + 2,
      0.0057f, 0.6f,   // Large-scale stone variation
      0.0121f, 0.25f,  // Medium-scale stone variation
      0.025f, 0.15f,   // Fine-scale stone variation
      1, 1.2f, 0.0008f // Domain warping
  );

  // Ore density variation field (controls ore placement)
  g_ore_density_field = entropy_field_custom(
      g_universe_perlin_seed + 3,
      0.0049f, 0.7f,   // Large-scale ore variation
      0.0105f, 0.2f,   // Medium-scale ore variation
      0.021f, 0.1f,    // Fine-scale ore variation
      1, 1.5f, 0.0006f // Domain warping
  );

  // Crystal density variation field (controls crystal placement)
  g_crystal_density_field = entropy_field_custom(
      g_universe_perlin_seed + 4,
      0.0037f, 0.8f,   // Large-scale crystal variation
      0.0089f, 0.15f,  // Medium-scale crystal variation
      0.017f, 0.05f,   // Fine-scale crystal variation
      1, 2.0f, 0.0004f // Domain warping
  );

  g_entropy_fields_initialized = 1;
}

static inline void ensure_entropy_fields_initialized(void)
{
  pthread_once(&g_entropy_fields_once, entropy_fields_init_once);
}

// Unified entropy field sampling functions
static float sample_terrain_height_variation(const World *world, uint32_t x, uint32_t y, uint32_t z)
{
  ensure_entropy_fields_initialized();
  UniverseCoord universe_coord = get_world_universe_coords(world->universe_x, world->universe_y, world->universe_z, x, y, z, world->width, world->height, world->depth);
  float value = entropy_field_sample(&g_terrain_height_field, universe_coord.x, universe_coord.y, universe_coord.z);
  return value * 2.0f - 1.0f; // Convert [0,1] to [-1,1]
}

static float sample_occupancy_variation(const World *world, uint32_t x, uint32_t y, uint32_t z)
{
  ensure_entropy_fields_initialized();
  UniverseCoord universe_coord = get_world_universe_coords(world->universe_x, world->universe_y, world->universe_z, x, y, z, world->width, world->height, world->depth);
  float value = entropy_field_sample(&g_occupancy_field, universe_coord.x, universe_coord.y, universe_coord.z);
  return value * 2.0f - 1.0f; // Convert [0,1] to [-1,1]
}

static float sample_stone_type_variation(const World *world, uint32_t x, uint32_t y, uint32_t z)
{
  ensure_entropy_fields_initialized();
  UniverseCoord universe_coord = get_world_universe_coords(world->universe_x, world->universe_y, world->universe_z, x, y, z, world->width, world->height, world->depth);
  float value = entropy_field_sample(&g_stone_type_field, universe_coord.x, universe_coord.y, universe_coord.z);
  return value * 2.0f - 1.0f; // Convert [0,1] to [-1,1]
}

static float sample_ore_density_variation(const World *world, uint32_t x, uint32_t y, uint32_t z)
{
  ensure_entropy_fields_initialized();
  UniverseCoord universe_coord = get_world_universe_coords(world->universe_x, world->universe_y, world->universe_z, x, y, z, world->width, world->height, world->depth);
  float value = entropy_field_sample(&g_ore_density_field, universe_coord.x, universe_coord.y, universe_coord.z);
  return value * 2.0f - 1.0f; // Convert [0,1] to [-1,1]
}

static float sample_crystal_density_variation(const World *world, uint32_t x, uint32_t y, uint32_t z)
{
  ensure_entropy_fields_initialized();
  UniverseCoord universe_coord = get_world_universe_coords(world->universe_x, world->universe_y, world->universe_z, x, y, z, world->width, world->height, world->depth);
  float value = entropy_field_sample(&g_crystal_density_field, universe_coord.x, universe_coord.y, universe_coord.z);
  return value * 2.0f - 1.0f; // Convert [0,1] to [-1,1]
}
static inline void vnormalize(float *x, float *y, float *z)
{
  float L = vlen(*x, *y, *z);
  if (L > 1e-6f)
  {
    *x /= L;
    *y /= L;
    *z /= L;
  }
}
// -----------------------------------------------------------------------------
// Fixed per-type masses (kg) using canonical voxel volume 0.06^3 m^3 (~2.16e-4)
// Mass array is now defined in voxel.h
#include "constants.h"

// For SHA256 implementation
#include "noise-c/src/crypto/sha2/sha256.h"

// Forward declarations for static functions
// Perlin noise is used by generators defined before its body; declare it up-front.
double perlin_noise(double w, double x, double y, double z);
double simplex_noise(double w, double x, double y, double z);

// Implementation of sample_field_noise - moved here to avoid forward declaration issues
static inline float sample_field_noise(const World *world,
                                       float w, float x, float y, float z,
                                       float scale,
                                       float zseed)
{
  // Convert local coordinates to universe coordinates for spatial continuity
  UniverseCoord universe_coord = get_world_universe_coords_f(world->universe_x, world->universe_y, world->universe_z,
                                                            x, y, z,
                                                            world->width, world->height, world->depth);

  // Isotropic 4D Simplex noise with per-axis phase shifts derived from zseed
  // Keep base generation at constant w while avoiding axis-aligned banding
  double nw = (double)(w * scale + zseed * 0.73f);
  double nx = (double)(universe_coord.x * scale + zseed * 1.33f);
  double ny = (double)(universe_coord.y * scale + zseed * 1.97f);
  double nz = (double)(universe_coord.z * scale + zseed * 2.41f);
  return (float)((simplex_noise(nw, nx, ny, nz) + 1.0) / 2.0);
}

static void world_generate_home(World *world, const char *seed);
static void world_generate_farm(World *world, const char *seed);
static void world_generate_random(World *world, const char *seed);
static void world_generate_wilderness(World *world, const char *seed);
static void world_generate_underworld(World *world, const char *seed);
static void world_generate_cloud(World *world, const char *seed);
static void world_generate_arena(World *world, const char *seed);
static void world_generate_spring_water(World *world, uint32_t x, uint32_t y, uint32_t z);
static void world_push_spring_water(World *world, uint32_t x, uint32_t y, uint32_t z);
// Forward decl for rotated noise helper used before its definition

void world_generate_solid_fill(World *world, VoxelType fill_type)
{
  if (!world || !world->voxels)
    return;
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
        world_set_voxel(world, x, y, z, fill_type);
}

// Underworld generator: bedrock ceiling/floor with stalactites/stalagmites and a connecting 3x3 column.
static void world_generate_underworld(World *world, const char *seed)
{
  (void)seed;
  if (!world || !world->voxels)
    return;
  // Clear to AIR
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
        world_set_voxel(world, x, y, z, VOXEL_AIR);

  const uint32_t d = world->depth;
  if (d < 8)
  {
    world_generate_solid_fill(world, VOXEL_BEDROCK);
    return;
  }

  // Bedrock ceiling and floor one layer each, with an extra layer for thickness near edges
  for (uint32_t y = 0; y < world->height; y++)
    for (uint32_t x = 0; x < world->width; x++)
    {
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(world, x, y, d - 1, VOXEL_BEDROCK);
    }

  // Cave height guided by golden ratio of air thickness
  float phi = 0.618f;
  uint32_t air_span = (uint32_t)fmaxf(4.0f, floorf((float)d * phi));
  uint32_t ceiling_base = 1;  // below top bedrock
  uint32_t floor_top = d - 2; // above bottom bedrock
  uint32_t mid = ceiling_base + air_span / 2;

  // Shapes are deterministic via perlin_noise fields

  // Stalactites from ceiling down to ~50% of golden-ratio air
  uint32_t stal_hi = ceiling_base + air_span / 2;
  uint32_t stal_lo = floor_top - air_span / 2;
  if (stal_hi >= d)
    stal_hi = d - 2;
  if (stal_lo <= 1)
    stal_lo = 2;

  // Distribute tips using low-frequency noise
  const float fscale = 0.05f;
  for (uint32_t y = 0; y < world->height; y++)
  {
    for (uint32_t x = 0; x < world->width; x++)
    {
      // Use pure 4D Perlin noise without rotation
      float n = (float)perlin_noise(0.0, (float)x * fscale, (float)y * fscale, 17.0);
      if (n > 0.60f)
      {
        // Place stalactite tip at a height within [ceiling_base+1, stal_hi]
        uint32_t tip = ceiling_base + 1 + (uint32_t)((n - 0.60f) / 0.40f * (float)(stal_hi - (ceiling_base + 1) + 1));
        if (tip >= stal_hi)
          tip = stal_hi;
        for (uint32_t z = ceiling_base; z <= tip; z++)
          world_set_voxel(world, x, y, z, VOXEL_BEDROCK);
      }
      if (n < 0.40f)
      {
        // Place stalagmite tip ascending from floor within [stal_lo, floor_top-1]
        uint32_t tip = floor_top - 1 - (uint32_t)((0.40f - n) / 0.40f * (float)((floor_top - 1) - stal_lo + 1));
        if (tip <= stal_lo)
          tip = stal_lo;
        for (uint32_t z = floor_top; z >= tip; z--)
        {
          world_set_voxel(world, x, y, z, VOXEL_BEDROCK);
          if (z == 0)
            break;
        }
      }
    }
  }

  // Ensure at least one 3x3 connector column linking ceiling and floor
  uint32_t cx = world->width / 2;
  uint32_t cy = world->height / 2;
  uint32_t c0 = ceiling_base + air_span / 3;
  uint32_t c1 = floor_top - air_span / 3;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      for (uint32_t z = c0; z <= c1; z++)
        if (world_is_position_valid(world, (int)cx + dx, (int)cy + dy, (int)z))
          world_set_voxel(world, (uint32_t)((int)cx + dx), (uint32_t)((int)cy + dy), z, VOXEL_BEDROCK);
}

// Cloud world: sparse flattened ellipsoids of VOXEL_STEAM in otherwise AIR
static void world_generate_cloud(World *world, const char *seed)
{
  if (!world || !world->voxels)
    return;
  (void)seed;
  // Clear to AIR
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
        world_set_voxel(world, x, y, z, VOXEL_AIR);

  // Determine number of ellipsoids based on size
  int blobs = (int)((world->width * world->height * world->depth) / 4096);
  if (blobs < 3)
    blobs = 3;
  if (blobs > 64)
    blobs = 64;

  // Use universe-coordinate noise for consistent cloud placement across world boundaries
  for (int i = 0; i < blobs; i++)
  {
    // Use universe noise to determine cloud positions consistently
    float blob_noise_x = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 71.0f);
    float blob_noise_y = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 73.0f);
    float blob_noise_z = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 79.0f);
    float blob_noise_rx = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 83.0f);
    float blob_noise_ry = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 89.0f);
    float blob_noise_rz = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 97.0f);

    int cx = (int)(blob_noise_x * (float)world->width);
    int cy = (int)(blob_noise_y * (float)world->height);
    int cz = (int)(blob_noise_z * (float)world->depth);
    // Flattened ellipsoid radii (thin vertically)
    int rx = 2 + (int)(blob_noise_rx * (float)(world->width / 6 + 2));
    int ry = 2 + (int)(blob_noise_ry * (float)(world->height / 6 + 2));
    int rz = 1 + (int)(blob_noise_rz * (float)(world->depth / 16 + 1));
    if (rx < 2)
      rx = 2;
    if (ry < 2)
      ry = 2;
    if (rz < 1)
      rz = 1;
    // Sparse threshold to avoid solid masses
    for (int z = cz - rz; z <= cz + rz; z++)
    {
      for (int y = cy - ry; y <= cy + ry; y++)
      {
        for (int x = cx - rx; x <= cx + rx; x++)
        {
          if (!world_pos_in_bounds_fast(world, x, y, z))
            continue;
          float dx = (float)(x - cx) / (float)rx;
          float dy = (float)(y - cy) / (float)ry;
          float dz = (float)(z - cz) / (float)rz;
          float d2 = dx * dx + dy * dy + dz * dz;
          if (d2 <= 1.0f)
          {
            // Sparsity: keep ~60% voxels; denser toward center
            float center_bias = 1.0f - d2; // 1 at center, 0 at edge
            float steam_noise = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.1f, 101.0f);
            float threshold = 0.6f + center_bias * 0.3f; // 60% base + up to 30% center bias
            if (steam_noise < threshold)
            {
              world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_STEAM);
            }
          }
        }
      }
    }
  }

  // (removed misplaced HOME-underside raise block)
}

// Simple seeded random number generator.
//
// Thread-local, because generation runs several worlds at once on the task pool. seed_random resets
// this from the world's seed and the decoration passes then draw from it for thousands of calls, so
// one shared copy meant two concurrent worlds interleaved their draws and neither got the sequence
// its seed asks for. A home world generated alongside others came out with different trees every
// run. Per-thread is exact rather than approximate: one world is generated start to finish by one
// task on one thread, so each sees the same sequence it would have seen generating alone.
static _Thread_local unsigned int seed_state = 1;

// Box-Muller transform for normal distribution
static double box_muller_transform(double u1, double u2)
{
  return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

// Forward declaration for SHA256 function
static void calculate_sha256(const char *input, uint8_t output[32]);

// -----------------------------------------------------------------------------
// Deterministic per-world RNG utilities (simple LCG seeded from seed_id+context)
// -----------------------------------------------------------------------------
void world_rng_seed(World *world, const char *context)
{
  if (!world)
    return;
  // FNV-1a 32-bit
  uint32_t h = 2166136261u;
  const unsigned char *p = (const unsigned char *)world->seed_id;
  for (size_t i = 0; p && p[i]; i++)
  {
    h ^= (uint32_t)p[i];
    h *= 16777619u;
  }
  const unsigned char *c = (const unsigned char *)context;
  for (size_t i = 0; c && c[i]; i++)
  {
    h ^= (uint32_t)c[i];
    h *= 16777619u;
  }
  if (h == 0u)
    h = 1u;
  world->rng_state = h;
}

uint32_t world_rng_next(World *world)
{
  if (!world)
    return 0u;
  world->rng_state = world->rng_state * 1664525u + 1013904223u;
  return world->rng_state;
}

int world_rng_range(World *world, int max)
{
  if (max <= 0)
    return 0;
  return (int)(world_rng_next(world) % (uint32_t)max);
}
// Generalized 2D pathfinding (BFS) on a fixed z-plane. 4-neighborhood, inclusive path result.
bool world_find_path_2d(
    const World *world,
    uint32_t z,
    uint32_t sx, uint32_t sy,
    uint32_t dx, uint32_t dy,
    const bool passable[VOXEL_COUNT],
    VoxelCoord **out_path,
    size_t *out_len)
{
  if (!world || !world->voxels || !out_path || !out_len)
    return false;
  if (z >= world->depth)
    return false;
  if (sx >= world->width || sy >= world->height || dx >= world->width || dy >= world->height)
    return false;
  if (!passable)
    return false;

  const uint32_t W = world->width, H = world->height;
  size_t plane_sz = (size_t)W * (size_t)H;
  uint8_t *visited = (uint8_t *)calloc(plane_sz, 1);
  int *parent = (int *)malloc(plane_sz * sizeof(int));
  uint32_t *qx = (uint32_t *)malloc(plane_sz * sizeof(uint32_t));
  uint32_t *qy = (uint32_t *)malloc(plane_sz * sizeof(uint32_t));
  if (!visited || !parent || !qx || !qy)
  {
    if (visited)
      free(visited);
    if (parent)
      free(parent);
    if (qx)
      free(qx);
    if (qy)
      free(qy);
    return false;
  }
  for (size_t i = 0; i < plane_sz; i++)
    parent[i] = -1;

  size_t head = 0, tail = 0;
  size_t sidx = (size_t)sy * (size_t)W + (size_t)sx;
  visited[sidx] = 1;
  qx[tail] = sx;
  qy[tail] = sy;
  tail++;

  bool found = false;
  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (head < tail)
  {
    uint32_t px = qx[head];
    uint32_t py = qy[head];
    head++;
    if (px == dx && py == dy)
    {
      found = true;
      break;
    }
    for (int i = 0; i < 4; i++)
    {
      int nx = (int)px + dirs[i][0];
      int ny = (int)py + dirs[i][1];
      if (nx < 0 || ny < 0 || nx >= (int)W || ny >= (int)H)
        continue;
      size_t nidx = (size_t)ny * (size_t)W + (size_t)nx;
      if (visited[nidx])
        continue;
      const Voxel *v = world_get_voxel((World *)world, (uint32_t)nx, (uint32_t)ny, z);
      if (!v)
        continue;
      VoxelType t = v->type;
      if ((int)t < 0 || t >= VOXEL_COUNT)
        continue;
      if (!passable[t])
        continue;
      visited[nidx] = 1;
      parent[nidx] = (int)((int)py * (int)W + (int)px);
      qx[tail] = (uint32_t)nx;
      qy[tail] = (uint32_t)ny;
      tail++;
    }
  }

  if (!found)
  {
    free(visited);
    free(parent);
    free(qx);
    free(qy);
    *out_path = NULL;
    *out_len = 0;
    return false;
  }

  // Reconstruct path from dest back to src
  size_t path_cap = 64;
  VoxelCoord *path = (VoxelCoord *)malloc(path_cap * sizeof(VoxelCoord));
  size_t path_len = 0;
  int cx = (int)dx, cy = (int)dy;
  while (1)
  {
    if (path_len >= path_cap)
    {
      path_cap *= 2;
      VoxelCoord *np = (VoxelCoord *)realloc(path, path_cap * sizeof(VoxelCoord));
      if (!np)
      {
        free(path);
        free(visited);
        free(parent);
        free(qx);
        free(qy);
        return false;
      }
      path = np;
    }
    path[path_len].x = (uint32_t)cx;
    path[path_len].y = (uint32_t)cy;
    path[path_len].z = z;
    path[path_len].type = VOXEL_AIR;
    path_len++;
    if ((uint32_t)cx == sx && (uint32_t)cy == sy)
      break;
    size_t cidx = (size_t)cy * (size_t)W + (size_t)cx;
    int p = parent[cidx];
    if (p < 0)
      break; // safety
    cy = (int)(p / (int)W);
    cx = (int)(p % (int)W);
  }
  // Reverse path to be src->dst
  for (size_t i = 0, j = path_len ? path_len - 1 : 0; i < j; i++, j--)
  {
    VoxelCoord tmp = path[i];
    path[i] = path[j];
    path[j] = tmp;
  }

  free(visited);
  free(parent);
  free(qx);
  free(qy);
  *out_path = path;
  *out_len = path_len;
  return true;
}
// Forward decl for perlin used above (body appears later as a non-static definition)
// removed neighbor-aware helper for now (editor will derive seeds itself)

// -----------------------------------------------------------------------------
// Noise domain helpers removed - now using pure 4D Perlin noise without rotation or warping

// Forward declarations for shared helpers used before their definitions
static inline void paint_disk_at_z(World *world,
                                   int center_x, int center_y, int z,
                                   int radius,
                                   VoxelType type,
                                   bool overwrite);

static void scatter_type_among(World *world,
                               uint32_t surface_cap,
                               VoxelType host_type);
// Stone bands transform: picks a rock type based on depth t and column bias
static inline VoxelType transform_stone_bands(const World *world,
                                              uint32_t x, uint32_t y, uint32_t z,
                                              float t,
                                              float bias);

// Forward declarations for refactored wilderness strata helper functions
static inline VoxelType select_canonical_stone_type(const World *world, uint32_t x, uint32_t y, uint32_t z, float depth_ratio);
static inline bool is_stone_type_for_ores(VoxelType stone_type);
static inline bool is_stone_type_for_crystals(VoxelType stone_type);
static inline VoxelType generate_canonical_ore_type(const World *world, uint32_t x, uint32_t y, uint32_t z, float depth_ratio, VoxelType host_stone);
static inline VoxelType generate_canonical_crystal_type(const World *world, uint32_t x, uint32_t y, uint32_t z, float depth_ratio);
static inline void paint_simple_ore_vein(World *world, int center_x, int center_y, int center_z, VoxelType ore_type);
static inline void paint_ellipse_at_z(World *world,
                                      int center_x, int center_y, int z,
                                      int radius_x, int radius_y,
                                      VoxelType type,
                                      bool overwrite);
static inline void paint_ellipsoid_onto_rocks(World *world,
                                              int center_x, int center_y, int center_z,
                                              int radius_x, int radius_y, int radius_z,
                                              VoxelType type);
static inline void jitter_coords(World *world,
                                 int x, int y, int z,
                                 int max_xy, int max_z,
                                 int *out_x, int *out_y, int *out_z);
static inline float magma_proximity_weight(const World *world, int x, int y, int z);
static inline float magma_influence_contiguous(World *world, int x, int y, int z);
static void apply_water_cap(World *world, const int *top_of_column, uint32_t clamp_z_max, uint32_t thickness);

// --- Shared generator helpers -------------------------------------------------
static void apply_basalt_plane(World *world, int z);

static void generate_magma_plane(World *world, int z_magma, bool fill_remaining_with_basalt)
{
  if (!world || !world->voxels)
    return;
  if (z_magma < 0 || z_magma >= (int)world->depth)
    return;
  ensure_perlin_ready();
  // Sample strictly within world bounds to avoid extending search beyond boundaries
  int xmin = 0, xmax = (int)world->width - 1;
  int ymin = 0, ymax = (int)world->height - 1;
  for (int y = ymin; y <= ymax; y++)
  {
    for (int x = xmin; x <= xmax; x++)
    {
      // Seed pools only above bedrock hotspots so generated magma sits on vents that can sustain it.
      if (!world_magma_hotspot_at(world, x, y))
        continue;
      float n = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z_magma, 0.041f, 137.0f);
      int rad = 1 + (int)floorf(n * 5.0f);
      if (rad < 1)
        rad = 1;
      if (rad > 5)
        rad = 5;
      // Anisotropic ellipse radii for less circular pools (axis-aligned for continuity)
      int rx = rad + (int)floorf((sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z_magma, 0.053f, 173.0f) - 0.5f) * 2.0f);
      int ry = rad + (int)floorf((sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z_magma, 0.049f, 191.0f) - 0.5f) * 2.0f);
      if (rx < 1)
        rx = 1;
      if (ry < 1)
        ry = 1;
      if (rx > 6)
        rx = 6;
      if (ry > 6)
        ry = 6;
      paint_ellipse_at_z(world, x, y, z_magma, rx, ry, VOXEL_MAGMA, /*overwrite=*/true);
    }
  }
  if (fill_remaining_with_basalt)
  {
    // Deprecated inline basalt fill; use extracted helper
    apply_basalt_plane(world, z_magma);
  }
}

// Fill AIR cells on a plane with VOXEL_STONE_BASALT
static void apply_basalt_plane(World *world, int z)
{
  if (!world || !world->voxels)
    return;
  if (z < 0 || z >= (int)world->depth)
    return;
  for (uint32_t y = 0; y < world->height; y++)
  {
    for (uint32_t x = 0; x < world->width; x++)
    {
      Voxel *v = world_get_voxel(world, x, y, (uint32_t)z);
      if (v && v->type == VOXEL_AIR)
        v->type = VOXEL_STONE_BASALT;
    }
  }
}

// Variant with configurable gate threshold controlling magma seeding density
static void generate_magma_plane_threshold(World *world, int z_magma, float gate_threshold, bool fill_remaining_with_basalt)
{
  if (!world || !world->voxels)
    return;
  if (z_magma < 0 || z_magma >= (int)world->depth)
    return;
  ensure_perlin_ready();
  int xmin = 0, xmax = (int)world->width - 1;
  int ymin = 0, ymax = (int)world->height - 1;
  for (int y = ymin; y <= ymax; y++)
  {
    for (int x = xmin; x <= xmax; x++)
    {
      float n = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z_magma, 0.041f, 137.0f);
      if (n < gate_threshold)
      {
        int rad = 1 + (int)floorf(n * 5.0f);
        if (rad < 1)
          rad = 1;
        if (rad > 5)
          rad = 5;
        int rx = rad + (int)floorf((sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z_magma, 0.053f, 173.0f) - 0.5f) * 2.0f);
        int ry = rad + (int)floorf((sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z_magma, 0.049f, 191.0f) - 0.5f) * 2.0f);
        if (rx < 1)
          rx = 1;
        if (ry < 1)
          ry = 1;
        if (rx > 6)
          rx = 6;
        if (ry > 6)
          ry = 6;
        paint_ellipse_at_z(world, x, y, z_magma, rx, ry, VOXEL_MAGMA, /*overwrite=*/true);
      }
    }
  }
  if (fill_remaining_with_basalt)
  {
    apply_basalt_plane(world, z_magma);
  }
}

static void expand_magma_halo(World *world, int z, int radius)
{
  if (!world || radius <= 0)
    return;
  const uint32_t W = world->width, H = world->height;
  size_t plane = (size_t)W * (size_t)H;
  uint8_t *mask = (uint8_t *)calloc(plane, 1);
  if (!mask)
    return;
  // Seed mask with existing magma
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
    {
      const Voxel *v = world_get_voxel(world, x, y, (uint32_t)z);
      if (v && v->type == VOXEL_MAGMA)
        mask[(size_t)y * (size_t)W + (size_t)x] = 1;
    }
  int r2 = radius * radius;
  // Expand mask
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
    {
      if (mask[(size_t)y * (size_t)W + (size_t)x])
        continue; // already magma
      // Check neighbors within radius
      int xi = (int)x, yi = (int)y;
      int found = 0;
      for (int dy = -radius; dy <= radius && !found; dy++)
      {
        int yy = yi + dy;
        if (yy < 0 || yy >= (int)H)
          continue;
        int dy2 = dy * dy;
        for (int dx = -radius; dx <= radius; dx++)
        {
          int xx = xi + dx;
          if (xx < 0 || xx >= (int)W)
            continue;
          if (dx * dx + dy2 > r2)
            continue;
          if (mask[(size_t)yy * (size_t)W + (size_t)xx])
          {
            found = 1;
            break;
          }
        }
      }
      if (found)
      {
        Voxel *v = world_get_voxel(world, x, y, (uint32_t)z);
        if (v)
          v->type = VOXEL_MAGMA;
      }
    }
  free(mask);
  world_invalidate_fluid_presence(world); // magma written directly, bypassing world_set_voxel
}

// Unified magma application used by multiple world types. Places magma disks at z=1
// then expands a lateral halo. If fill_basalt is true, fills remaining cells at z=1 with basalt.
static void apply_unified_magma(World *world, bool fill_basalt)
{
  if (!world)
    return;
  if (world->depth > 1)
  {
    generate_magma_plane(world, /*z_magma=*/1, /*fill_remaining_with_basalt=*/fill_basalt);
    expand_magma_halo(world, /*z=*/1, /*radius=*/6);
  }
}

// Clear entire world volume to AIR
static void world_clear_to_air(World *world)
{
  if (!world || !world->voxels)
    return;
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
        world_set_voxel(world, x, y, z, VOXEL_AIR);
}

// Shared: write a contiguous bedrock floor at z=0 across the world
static void apply_bedrock_floor(World *world)
{
  if (!world || !world->voxels || world->depth == 0)
    return;
  for (uint32_t y = 0; y < world->height; y++)
    for (uint32_t x = 0; x < world->width; x++)
      world_set_voxel(world, x, y, 0u, VOXEL_BEDROCK);
}

// Forward declarations for helpers used by the unified pipeline
static inline unsigned long long now_ms(void);
static int *apply_wilderness_strata(World *world,
                                    uint32_t surface_cap,
                                    uint32_t clay_cap_layers,
                                    uint64_t *dbg_count_magma,
                                    uint64_t *dbg_count_ore_cu,
                                    uint64_t *dbg_count_ore_ag,
                                    uint64_t *dbg_count_ore_au,
                                    uint64_t *dbg_count_crystals,
                                    bool limit_to_edge_band,
                                    uint32_t edge_band_thickness,
                                    bool enable_crystals);
static int seeded_rand_range(int max);
static void layer_place_random_spring(World *world, uint32_t surface_cap);
static void layer_apply_clay_cap(World *world, int *top_of_column, uint32_t clay_cap_layers);
// Run magma and all subsequent layers
static void world_generate_from_magma_and_up(World *world, bool fill_basalt);
// Generate the shared baseline for SCOURED: clear->bedrock->magma->strata.
// If out_top_of_column is provided, returns the array from apply_wilderness_strata; caller must free().
// Optionally returns the computed surface_cap and clay_cap_layers used.
static void world_generate_scoured_base(World *world,
                                        bool fill_basalt,
                                        int **out_top_of_column,
                                        uint32_t *out_surface_cap,
                                        uint32_t *out_clay_cap_layers);

// Unified base generation pipeline: clear -> bedrock -> magma(halo) -> strata -> spring -> clay cap -> timings -> water cap -> scatter
static void world_generate_base_layers(World *world, bool fill_basalt)
{
  if (!world || !world->voxels)
    return;
  ensure_perlin_ready();

  // Clear to air and bedrock base
  world_clear_to_air(world);
  apply_bedrock_floor(world);

  // From magma upward
  world_generate_from_magma_and_up(world, fill_basalt);
}

// Standalone: run magma and all subsequent layers (strata, springs, clay, water, scatter)
static void world_generate_from_magma_and_up(World *world, bool fill_basalt)
{
  if (!world || !world->voxels)
    return;

  // Magma layer (use unified sampler/halo)
  apply_unified_magma(world, fill_basalt);

  // Surface cap and clay layers
  const uint32_t d = world->depth;
  uint32_t surface_cap = (uint32_t)(d * 0.618f);
  uint32_t clay_cap_layers = 3u;
  if (surface_cap > clay_cap_layers + 1u)
    surface_cap -= clay_cap_layers;
  if (surface_cap >= d)
    surface_cap = d - 1;
  if (surface_cap < 1)
    surface_cap = 1;

  // Strata
  uint64_t dbg_count_magma = 0, dbg_cu = 0, dbg_ag = 0, dbg_au = 0, dbg_cr = 0;
  int *top_of_column = apply_wilderness_strata(world,
                                               surface_cap,
                                               clay_cap_layers,
                                               &dbg_count_magma,
                                               &dbg_cu,
                                               &dbg_ag,
                                               &dbg_au,
                                               &dbg_cr,
                                               /*limit_to_edge_band=*/false,
                                               /*edge_band_thickness=*/0u,
                                               /*enable_crystals=*/true);

  // Random spring at z=1
  layer_place_random_spring(world, surface_cap);

  // Clay cap above tops
  layer_apply_clay_cap(world, top_of_column, clay_cap_layers);

  // Dirt on top of clay
  layer_apply_dirt_over_clay(world, top_of_column);

  // Water cap and final scatter
  if (top_of_column)
  {
    // Seed a subset of top dirt before water
    layer_seed_top_dirt(world, top_of_column);
    apply_water_cap(world, top_of_column, /*clamp_z_max=*/(uint32_t)(world->depth - 1), /*thickness=*/2u);
    free(top_of_column);
  }
  scatter_type_among(world, surface_cap, VOXEL_STONE);
}

// Build the SCOURED baseline only (shared between SCOURED and as prefix for WILDERNESS)
static void world_generate_scoured_base(World *world,
                                        bool fill_basalt,
                                        int **out_top_of_column,
                                        uint32_t *out_surface_cap,
                                        uint32_t *out_clay_cap_layers)
{
  printf("[DEBUG] world_generate_scoured_base called: world=%p, fill_basalt=%d\n", (void *)world, fill_basalt);
  if (!world || !world->voxels)
    return;
  ensure_perlin_ready();

  // Clear + bedrock
  WGEN_BEGIN(WGEN_PHASE_CLEAR);
  world_clear_to_air(world);
  WGEN_END(WGEN_PHASE_CLEAR);

  WGEN_BEGIN(WGEN_PHASE_BEDROCK);
  apply_bedrock_floor(world);
  WGEN_END(WGEN_PHASE_BEDROCK);

  // Continue from magma upward identically to unified path, but capture tops/caps
  WGEN_BEGIN(WGEN_PHASE_MAGMA);
  apply_unified_magma(world, fill_basalt);
  WGEN_END(WGEN_PHASE_MAGMA);

  const uint32_t d = world->depth;
  uint32_t surface_cap = (uint32_t)(d * 0.618f);
  uint32_t clay_cap_layers = 3u;
  if (surface_cap > clay_cap_layers + 1u)
    surface_cap -= clay_cap_layers;
  if (surface_cap >= d)
    surface_cap = d - 1;
  if (surface_cap < 1)
    surface_cap = 1;

  uint64_t dbg_count_magma = 0, dbg_cu = 0, dbg_ag = 0, dbg_au = 0, dbg_cr = 0;
  int *tops = apply_wilderness_strata(world,
                                      surface_cap,
                                      clay_cap_layers,
                                      &dbg_count_magma,
                                      &dbg_cu,
                                      &dbg_ag,
                                      &dbg_au,
                                      &dbg_cr,
                                      /*limit_to_edge_band=*/false,
                                      /*edge_band_thickness=*/0u,
                                      /*enable_crystals=*/true);
  if (out_top_of_column)
    *out_top_of_column = tops;
  else if (tops)
    free(tops);
  if (out_surface_cap)
    *out_surface_cap = surface_cap;
  if (out_clay_cap_layers)
    *out_clay_cap_layers = clay_cap_layers;
}

// Place a random spring with 5% probability at z=1 (one above bedrock)
static void layer_place_random_spring(World *world, uint32_t surface_cap)
{
  if (!world || world->width == 0 || world->height == 0 || surface_cap <= 1)
    return;
  // Use universe-coordinate noise for consistent spring placement across world boundaries
  float spring_probability = sample_field_noise(world, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f, 103.0f);
  if (spring_probability < 0.05f) // 5% chance
  {
    float spring_noise_x = sample_field_noise(world, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f, 107.0f);
    float spring_noise_y = sample_field_noise(world, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f, 109.0f);
    uint32_t sx = (uint32_t)(spring_noise_x * (float)world->width);
    uint32_t sy = (uint32_t)(spring_noise_y * (float)world->height);
    world_set_voxel(world, sx, sy, 1u, VOXEL_SPRING);
  }
}

// Scatter a small number of ellipsoids of a voxel type into empty space above ground
void layer_scatter_ellipsoids(World *world,
                              VoxelType type,
                              int min_count,
                              int max_count,
                              int min_radius,
                              int max_radius,
                              int min_height_offset,
                              int max_height_offset)
{
  if (!world || min_count <= 0 || max_count < min_count)
    return;
  // Use universe-coordinate noise for consistent ellipsoid placement across world boundaries
  float count_noise = sample_field_noise(world, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f, 113.0f);
  int count = min_count + (int)(count_noise * (float)(max_count - min_count + 1));
  const int W = (int)world->width;
  const int H = (int)world->height;
  const int D = (int)world->depth;
  for (int i = 0; i < count; i++)
  {
    float ellipsoid_noise_x = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 127.0f);
    float ellipsoid_noise_y = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 131.0f);
    int cx = (int)(ellipsoid_noise_x * (float)W);
    int cy = (int)(ellipsoid_noise_y * (float)H);
    int hz = world_height_at_fast(world, cx, cy);
    int min_cz = hz + min_height_offset;
    int upper = D - 2;
    int max_cz = upper;
    if (max_height_offset > 0)
    {
      int candidate = hz + max_height_offset;
      if (candidate < max_cz)
        max_cz = candidate;
    }
    if (min_cz > max_cz)
      min_cz = max_cz - 1;
    if (min_cz < 1)
      min_cz = 1;
    if (max_cz > upper)
      max_cz = upper;
    float ellipsoid_noise_z = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 137.0f);
    float ellipsoid_noise_rx = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 139.0f);
    float ellipsoid_noise_ry = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 149.0f);
    float ellipsoid_noise_rz = sample_field_noise(world, (float)i, 0.0f, 0.0f, 0.0f, 0.1f, 151.0f);
    int cz = min_cz + (int)(ellipsoid_noise_z * (float)(max_cz - min_cz + 1));
    int rx = min_radius + (int)(ellipsoid_noise_rx * (float)(max_radius - min_radius + 1));
    int ry = min_radius + (int)(ellipsoid_noise_ry * (float)(max_radius - min_radius + 1));
    int rz = min_radius + (int)(ellipsoid_noise_rz * (float)(max_radius - min_radius + 1));
    if (rx < 1)
      rx = 1;
    if (ry < 1)
      ry = 1;
    if (rz < 1)
      rz = 1;
    int xmin = clampi(cx - rx, 0, W - 1);
    int xmax = clampi(cx + rx, 0, W - 1);
    int ymin = clampi(cy - ry, 0, H - 1);
    int ymax = clampi(cy + ry, 0, H - 1);
    int zmin = clampi(cz - rz, 1, D - 1);
    int zmax = clampi(cz + rz, 1, D - 1);
    float inv_rx2 = 1.0f / (float)(rx * rx);
    float inv_ry2 = 1.0f / (float)(ry * ry);
    float inv_rz2 = 1.0f / (float)(rz * rz);
    for (int y = ymin; y <= ymax; y++)
    {
      for (int x = xmin; x <= xmax; x++)
      {
        for (int z = zmin; z <= zmax; z++)
        {
          float dx = (float)(x - cx);
          float dy = (float)(y - cy);
          float dz = (float)(z - cz);
          float v = dx * dx * inv_rx2 + dy * dy * inv_ry2 + dz * dz * inv_rz2;
          if (v <= 1.0f)
          {
            Voxel *vxl = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
            if (vxl && vxl->type == VOXEL_AIR)
            {
              vxl->type = type;
              // Initialize fluid quantity low; will rise via physics (0..6)
              voxel_set_quantity_or_wetness(vxl, 1);
            }
          }
        }
      }
    }
  }
}

// Spray material by scattering stretched ellipsoids along a vector path.
void layer_spray_material(World *world,
                          uint32_t origin_x, uint32_t origin_y, uint32_t origin_z,
                          float dir_x, float dir_y, float dir_z,
                          VoxelType type,
                          int length_voxels,
                          int min_radius,
                          int max_radius,
                          float base_density,
                          float radial_falloff)
{
  if (!world || !world->voxels || length_voxels <= 0 || base_density <= 0.0f)
    return;
  int W = (int)world->width, H = (int)world->height, D = (int)world->depth;
  float vx = dir_x, vy = dir_y, vz = dir_z;
  vnormalize(&vx, &vy, &vz);
  if (vlen(vx, vy, vz) < 1e-6f)
  {
    vz = 1.0f; // default up if zero
  }
  // Use universe-coordinate noise for consistent material spray across world boundaries
  // Clamp origin to world bounds just in case
  int ox = clampi((int)origin_x, 0, W - 1);
  int oy = clampi((int)origin_y, 0, H - 1);
  int oz = clampi((int)origin_z, 0, D - 1);
  // Compute a clamped length so the ideal end stays within bounds (best-effort)
  float t_bound = (float)length_voxels;
  if (vx > 1e-6f)
    t_bound = fminf(t_bound, ((float)(W - 1) - (float)ox) / vx);
  else if (vx < -1e-6f)
    t_bound = fminf(t_bound, ((float)ox - 0.0f) / (-vx));
  if (vy > 1e-6f)
    t_bound = fminf(t_bound, ((float)(H - 1) - (float)oy) / vy);
  else if (vy < -1e-6f)
    t_bound = fminf(t_bound, ((float)oy - 0.0f) / (-vy));
  if (vz > 1e-6f)
    t_bound = fminf(t_bound, ((float)(D - 1) - (float)oz) / vz);
  else if (vz < -1e-6f)
    t_bound = fminf(t_bound, ((float)oz - 0.0f) / (-vz));
  int steps = clampi((int)floorf(t_bound), 1, length_voxels);
  // Log the clamped endpoints for debugging
  {
    char msg[160];
    float ex = (float)ox + vx * (float)steps;
    float ey = (float)oy + vy * (float)steps;
    float ez = (float)oz + vz * (float)steps;
    snprintf(msg, sizeof(msg), "[spray] origin=(%d,%d,%d) dir=(%.2f,%.2f,%.2f) len=%d->%d end=(%.1f,%.1f,%.1f)",
             ox, oy, oz, vx, vy, vz, length_voxels, steps, ex, ey, ez);
    world_append_log(world, msg);
  }
  // Build small set of near-parallel branches around the main vector
  int branch_count = 3; // main + 2 side trails
  for (int b = 0; b < branch_count; b++)
  {
    float dens_scale = (b == 0) ? 1.0f : (b == 1 ? 0.7f : 0.55f);
    float rad_scale = (b == 0) ? 1.0f : (b == 1 ? 0.8f : 0.65f);
    // Construct a small angular deviation using an orthonormal basis
    // Basis from the main direction
    float upx = 0.0f, upy = 1.0f, upz = 0.0f;
    if (fabsf(vy) > 0.9f)
    {
      upx = 1.0f;
      upy = 0.0f;
      upz = 0.0f;
    }
    float u_x = vy * upz - vz * upy;
    float u_y = vz * upx - vx * upz;
    float u_z = vx * upy - vy * upx;
    vnormalize(&u_x, &u_y, &u_z);
    float w_x = vy * u_z - vz * u_y;
    float w_y = vz * u_x - vx * u_z;
    float w_z = vx * u_y - vy * u_x;
    vnormalize(&w_x, &w_y, &w_z);
    float spray_noise_angle = sample_field_noise(world, (float)b, (float)ox, (float)oy, (float)oz, 0.1f, 157.0f);
    float spray_noise_phi = sample_field_noise(world, (float)b, (float)ox, (float)oy, (float)oz, 0.1f, 163.0f);
    float angle = (b == 0) ? 0.0f : (0.15f + 0.05f * spray_noise_angle);
    float phi = (float)(2.0 * 3.14159265) * spray_noise_phi;
    float off_u = cosf(phi), off_w = sinf(phi);
    float bvx = vx * cosf(angle) + (off_u * u_x + off_w * w_x) * sinf(angle);
    float bvy = vy * cosf(angle) + (off_u * u_y + off_w * w_y) * sinf(angle);
    float bvz = vz * cosf(angle) + (off_u * u_z + off_w * w_z) * sinf(angle);
    vnormalize(&bvx, &bvy, &bvz);
    // Lateral offset of branch start around the origin centerline
    float spray_noise_lateral = sample_field_noise(world, (float)b, (float)ox, (float)oy, (float)oz, 0.1f, 167.0f);
    float lateral = (b == 0) ? 0.0f : (1.0f + spray_noise_lateral * 3.0f);
    float ofx = (float)ox + u_x * off_u * lateral + w_x * off_w * lateral;
    float ofy = (float)oy + u_y * off_u * lateral + w_y * off_w * lateral;
    float ofz = (float)oz + u_z * off_u * lateral + w_z * off_w * lateral;
    // Clamp initial branch origin
    int box = clampi((int)roundf(ofx), 0, W - 1);
    int boy = clampi((int)roundf(ofy), 0, H - 1);
    int boz = clampi((int)roundf(ofz), 0, D - 1);
    // Recompute bound for branch direction
    float t_bound_b = (float)steps;
    if (bvx > 1e-6f)
      t_bound_b = fminf(t_bound_b, ((float)(W - 1) - (float)box) / bvx);
    else if (bvx < -1e-6f)
      t_bound_b = fminf(t_bound_b, ((float)box - 0.0f) / (-bvx));
    if (bvy > 1e-6f)
      t_bound_b = fminf(t_bound_b, ((float)(H - 1) - (float)boy) / bvy);
    else if (bvy < -1e-6f)
      t_bound_b = fminf(t_bound_b, ((float)boy - 0.0f) / (-bvy));
    if (bvz > 1e-6f)
      t_bound_b = fminf(t_bound_b, ((float)(D - 1) - (float)boz) / bvz);
    else if (bvz < -1e-6f)
      t_bound_b = fminf(t_bound_b, ((float)boz - 0.0f) / (-bvz));
    int steps_b = clampi((int)floorf(t_bound_b), 1, steps);
    // Step along the branch direction placing candidate centers
    float fx = (float)box, fy = (float)boy, fz = (float)boz;
    for (int step = 0; step < steps_b; step++)
    {
      int cx = (int)roundf(fx);
      int cy = (int)roundf(fy);
      int cz = (int)roundf(fz);
      if (cx < 0 || cy < 0 || cz < 0 || cx >= W || cy >= H || cz >= D)
      {
        float step_len_skip = 1.0f;
        fx += bvx * step_len_skip;
        fy += bvy * step_len_skip;
        fz += bvz * step_len_skip;
        continue;
      }
      // Radius tapers with step
      float t = (float)step / (float)length_voxels;
      float spray_noise_radius = sample_field_noise(world, (float)step, (float)ox, (float)oy, (float)oz, 0.1f, 173.0f);
      int r_base = min_radius + (int)(spray_noise_radius * (float)(max_radius - min_radius + 1));
      int rx = (int)fmaxf(1.0f, (float)r_base * rad_scale * (1.0f - 0.3f * t));
      int ry = (int)fmaxf(1.0f, (float)r_base * rad_scale * (1.0f - 0.3f * t));
      int rz = (int)fmaxf(1.0f, (float)r_base * rad_scale * (1.5f + 1.5f * (1.0f - t)));
      // Orthonormal basis for this branch direction
      float bux = 0.0f, buy = 1.0f, buz = 0.0f;
      if (fabsf(bvy) > 0.9f)
      {
        bux = 1.0f;
        buy = 0.0f;
        buz = 0.0f;
      }
      float bb_u_x = bvy * buz - bvz * buy;
      float bb_u_y = bvz * bux - bvx * buz;
      float bb_u_z = bvx * buy - bvy * bux;
      vnormalize(&bb_u_x, &bb_u_y, &bb_u_z);
      float bb_w_x = bvy * bb_u_z - bvz * bb_u_y;
      float bb_w_y = bvz * bb_u_x - bvx * bb_u_z;
      float bb_w_z = bvx * bb_u_y - bvy * bb_u_x;
      vnormalize(&bb_w_x, &bb_w_y, &bb_w_z);
      // Bounding box
      int xmin = clampi(cx - rx, 0, W - 1);
      int xmax = clampi(cx + rx, 0, W - 1);
      int ymin = clampi(cy - ry, 0, H - 1);
      int ymax = clampi(cy + ry, 0, H - 1);
      int zmin = clampi(cz - rz, 0, D - 1);
      int zmax = clampi(cz + rz, 0, D - 1);
      for (int y = ymin; y <= ymax; y++)
      {
        for (int x = xmin; x <= xmax; x++)
        {
          for (int z = zmin; z <= zmax; z++)
          {
            float px = (float)(x - cx);
            float py = (float)(y - cy);
            float pz = (float)(z - cz);
            float vd = px * bvx + py * bvy + pz * bvz;
            float vu = px * bb_u_x + py * bb_u_y + pz * bb_u_z;
            float vw = px * bb_w_x + py * bb_w_y + pz * bb_w_z;
            float e = (vu * vu) / (float)(rx * rx) + (vw * vw) / (float)(ry * ry) + (vd * vd) / (float)(rz * rz);
            if (e > 1.0f)
              continue;
            float r_perp = sqrtf(vu * vu + vw * vw);
            float density = (base_density * dens_scale) * expf(-radial_falloff * r_perp) * (1.0f - 0.5f * t);
            // Use pure 4D Perlin noise without rotation or warping
            float rr = (float)perlin_noise(0.0, (float)x * 0.13f, (float)y * 0.13f, (float)z * 0.071f + 37.0f);
            if (rr < density)
            {
              Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
              if (v && v->type == VOXEL_AIR)
              {
                v->type = type;
                voxel_set_quantity_or_wetness(v, 1);
              }
            }
          }
        }
      }
      float spray_noise_step = sample_field_noise(world, (float)step, (float)ox, (float)oy, (float)oz, 0.1f, 179.0f);
      float step_len = 1.0f + 0.25f * (spray_noise_step - 0.5f);
      fx += bvx * step_len;
      fy += bvy * step_len;
      fz += bvz * step_len;
    }
  }
}

// Apply 1..N layers of clay above the top-of-column indices
static void layer_apply_clay_cap(World *world, int *top_of_column, uint32_t clay_cap_layers)
{
  if (!world || !top_of_column || clay_cap_layers == 0)
    return;
  const uint32_t W = world->width, H = world->height;
  for (uint32_t y = 0; y < H; y++)
  {
    for (uint32_t x = 0; x < W; x++)
    {
      size_t cidx = (size_t)y * (size_t)W + (size_t)x;
      int top_z = top_of_column[cidx];
      if (top_z < 0)
        continue;
      for (int k = 1; k <= (int)clay_cap_layers; k++)
      {
        int cz = top_z + k;
        if (cz >= 0 && cz < (int)world->depth)
        {
          Voxel *v = world_get_voxel(world, x, y, (uint32_t)cz);
          if (v && v->type == VOXEL_AIR)
          {
            v->type = VOXEL_SOIL_CLAY;
            if (cz > top_of_column[cidx])
              top_of_column[cidx] = cz;
          }
        }
      }
    }
  }
}

// Apply a single layer of VOXEL_SOIL on top of clay where exposed to AIR
static void layer_apply_dirt_over_clay(World *world, const int *top_of_column)
{
  if (!world || !top_of_column)
    return;
  const uint32_t W = world->width, H = world->height, D = world->depth;
  for (uint32_t y = 0; y < H; y++)
  {
    for (uint32_t x = 0; x < W; x++)
    {
      size_t cidx = (size_t)y * (size_t)W + (size_t)x;
      int top_z = top_of_column[cidx];
      if (top_z < 0)
        continue;
      uint32_t dz = (uint32_t)top_z + 1u;
      if (dz >= D)
        continue;
      Voxel *above = world_get_voxel(world, x, y, dz);
      Voxel *topv = world_get_voxel(world, x, y, (uint32_t)top_z);
      if (above && topv && above->type == VOXEL_AIR && topv->type == VOXEL_SOIL_CLAY)
      {
        above->type = VOXEL_SOIL;
      }
    }
  }
}

// Mark a random sampling of top dirt voxels with the "SEEDED" condition before water placement
static void layer_seed_top_dirt(World *world, const int *top_of_column)
{
  if (!world || !top_of_column)
    return;
  const uint32_t W = world->width, H = world->height, D = world->depth;
  // Use universe-coordinate noise for consistent dirt placement across world boundaries
  for (uint32_t y = 0; y < H; y++)
  {
    for (uint32_t x = 0; x < W; x++)
    {
      size_t cidx = (size_t)y * (size_t)W + (size_t)x;
      int top_z = top_of_column[cidx];
      if (top_z < 0)
        continue;
      uint32_t dz = (uint32_t)top_z + 1u;
      if (dz >= D)
        continue;
      Voxel *v = world_get_voxel(world, x, y, dz);
      if (!v || v->type != VOXEL_SOIL)
        continue;
      // 1-in-6 chance deterministically
      if ((world_rng_next(world) % 6) == 0)
      {
        world_add_voxel_condition(world, x, y, dz, "SEEDED");
      }
    }
  }
}

// Refactored wilderness strata generation with canonical voxel types and improved performance
static int *apply_wilderness_strata(
    World *world,
    uint32_t surface_cap,
    uint32_t clay_cap_layers,
    uint64_t *dbg_count_magma,
    uint64_t *dbg_count_ore_cu,
    uint64_t *dbg_count_ore_ag,
    uint64_t *dbg_count_ore_au,
    uint64_t *dbg_count_crystals,
    bool limit_to_edge_band,
    uint32_t edge_band_thickness,
    bool enable_crystals)
{
  if (!world)
    return NULL;

  WGEN_BEGIN(WGEN_PHASE_STRATA);

  const uint32_t W = world->width, H = world->height;
  int *top_of_column = (int *)malloc((size_t)W * (size_t)H * sizeof(int));
  if (!top_of_column)
    return NULL;
  for (uint32_t i = 0; i < W * H; i++)
    top_of_column[i] = -1;

  for (uint32_t z = 1; z < world->depth; z++)
  {
    for (uint32_t y = 0; y < world->height; y++)
    {
      for (uint32_t x = 0; x < world->width; x++)
      {
        if (limit_to_edge_band)
        {
          bool on_edge = (x < edge_band_thickness) || (y < edge_band_thickness) ||
                         (x >= W - edge_band_thickness) || (y >= H - edge_band_thickness);
          if (!on_edge)
            continue;
        }

        // Use 9-phase noise system for terrain height variation
        WGEN_BEGIN(WGEN_PHASE_TERRAIN_NOISE);
        float combined_noise = sample_nine_phase_noise_field(world, 0.0f, (float)x, (float)y, (float)z, 1.0f, 1.0f, 1.0f);
        WGEN_END(WGEN_PHASE_TERRAIN_NOISE);
        float surface_height = (float)(world->depth - 1) * (0.6f + 0.4f * combined_noise);

        // Skip if this z-level is above the surface height for this column
        if ((float)z > surface_height)
          continue;

        float t = (float)z / (float)world->depth;

        // No occupancy sampling here: sample_occupancy_variation() was called once per
        // voxel and its result never read (the compiler flagged it as unused). It is a
        // pure function of the coordinates, so dropping the call leaves generated worlds
        // bit-identical while removing ~18% of wilderness generation time. Caves come
        // from magma physics, not from an occupancy threshold.

        // Generate all terrain - let magma physics handle cave generation naturally
        // No artificial void generation - rely on magma physics for natural caves

        // Phase 2: Stone Processing (proper geological strata layers)
        WGEN_BEGIN(WGEN_PHASE_STONE);

        // Generate proper geological strata - each layer applied on top of previous
        // Calculate which geological layer this z position belongs to using the same noise
        float base_thickness = 8.0f + 4.0f * combined_noise; // 4-12 voxels (smaller for more layers)
        uint32_t geological_layer = (uint32_t)(z / base_thickness);
        VoxelType layer_stone_type = get_geological_layer_type(world, x, y, geological_layer);

        // Only place voxels where there's support (not dangling overhangs)
        bool has_support = false;
        if (z == 0)
        {
          // Bottom layer always has support
          has_support = true;
        }
        else
        {
          // Check if there's a voxel below to support this one
          Voxel *below_voxel = world_get_voxel(world, x, y, z - 1);
          if (below_voxel && below_voxel->type != VOXEL_AIR)
          {
            has_support = true;
          }
        }

        if (has_support)
        {
          // Apply the stone type for this geological layer
          // Top layers are air for natural height variation
          if (layer_stone_type == VOXEL_AIR)
          {
            // Top layers are air - this creates natural height variation
            world_set_voxel(world, x, y, z, VOXEL_AIR);
          }
          else if (layer_stone_type == VOXEL_STONE_BASALT || layer_stone_type == VOXEL_STONE_GRANITE)
          {
            // Magma generation for volcanic stones using standard entropy
            float mseed = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.061f, 137.0f);
          float depth_boost = expf(-0.5f * ((t - 0.18f) / 0.16f) * ((t - 0.18f) / 0.16f));
          float p_magma = 0.0015f * 200.0f * depth_boost;
          if (mseed < p_magma)
          {
            world_set_voxel(world, x, y, z, VOXEL_MAGMA);
            if (dbg_count_magma)
              (*dbg_count_magma)++;
          }
          else
          {
              world_set_voxel(world, x, y, z, layer_stone_type);
          }
        }
        else
        {
            // Other stone types (limestone, sandstone)
            world_set_voxel(world, x, y, z, layer_stone_type);
          }
        }
        else
        {
          // No support, leave as air (no dangling overhangs)
          world_set_voxel(world, x, y, z, VOXEL_AIR);
        }

        // Phase 2.5: Gravel Generation (only from sandstone - top stone layer)
        WGEN_BEGIN(WGEN_PHASE_GRAVEL);
        if (layer_stone_type == VOXEL_STONE_SANDSTONE && t > 0.9f) // Only on sandstone near surface
        {
          // Check if this is a surface voxel (no solid voxel above)
          bool is_surface = true;
          if (z < world->depth - 1)
          {
            Voxel *above_voxel = world_get_voxel(world, x, y, z + 1);
            if (above_voxel && above_voxel->type != VOXEL_AIR)
            {
              is_surface = false;
            }
          }

          if (is_surface)
          {
            float slope = calculate_surface_slope(world, x, y, z);
            if (slope < 0.3f) // Only on gentle slopes
            {
              // Generate gravel from sandstone
              float gravel_noise = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.040f, 53.0f);
              if (gravel_noise < 0.4f) // 40% chance to become gravel
              {
                world_set_voxel(world, x, y, z, VOXEL_GRAVEL);
              }
            }
          }
        }

        WGEN_END(WGEN_PHASE_GRAVEL);

        // Phase 2.6: Water Generation (for sandstone layers)
        WGEN_BEGIN(WGEN_PHASE_WATER);
        if (layer_stone_type == VOXEL_STONE_SANDSTONE)
        {
          // Water generation for sandstone using standard entropy
          float wseed = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.061f, 137.0f);
          float depth_boost = expf(-0.5f * ((t - 0.18f) / 0.16f) * ((t - 0.18f) / 0.16f));
          float p_water = 0.002f * 200.0f * depth_boost;
          if (wseed < p_water)
          {
            world_set_voxel(world, x, y, z, VOXEL_WATER);
          }
        }
        WGEN_END(WGEN_PHASE_WATER);

        // Phase 2.7: Soil Layers (multiple layers, one for each stone type)
        /*
        if (t > 0.95f) // Only very near surface
        {
          // Check if this is a surface voxel (no solid voxel above)
          bool is_surface = true;
          if (z < world->depth - 1)
          {
            Voxel *above_voxel = world_get_voxel(world, x, y, z + 1);
            if (above_voxel && above_voxel->type != VOXEL_AIR)
            {
              is_surface = false;
            }
          }

          if (is_surface)
          {
            float slope = calculate_surface_slope(world, x, y, z);
            if (slope < 0.2f) // Only on very gentle slopes
            {
              VoxelType soil_type = select_soil_type_for_stone(world, x, y, z, layer_stone_type);
              world_set_voxel(world, x, y, z, soil_type);
            }
          }
        }
        */

        size_t cidx = (size_t)y * (size_t)W + (size_t)x;
        if ((int)z > top_of_column[cidx])
          top_of_column[cidx] = (int)z;

        WGEN_END(WGEN_PHASE_STONE);

        // Phase 3: Ore Generation (optimized with canonical ore types)
        WGEN_BEGIN(WGEN_PHASE_ORE);
        apply_ore_generation(world, x, y, z, layer_stone_type, t, surface_cap, dbg_count_ore_au, dbg_count_ore_ag, dbg_count_ore_cu);
        WGEN_END(WGEN_PHASE_ORE);

        // Phase 4: Crystal Generation (optimized with canonical crystal types)
        WGEN_BEGIN(WGEN_PHASE_CRYSTAL);
        apply_crystal_generation(world, x, y, z, layer_stone_type, t, enable_crystals, dbg_count_crystals);
        WGEN_END(WGEN_PHASE_CRYSTAL);
      }
    }
  }

  WGEN_END(WGEN_PHASE_STRATA);

  (void)clay_cap_layers; // used by callers post-return
  return top_of_column;
}

// Helper functions for refactored wilderness strata generation

// Calculate surface slope at a given position
static inline float calculate_surface_slope(const World *world, uint32_t x, uint32_t y, uint32_t z)
{
  if (!world || x == 0 || y == 0 || x >= world->width - 1 || y >= world->height - 1)
    return 1.0f; // High slope at edges

  // Sample height at neighboring positions
  float h_center = (float)z;
  float h_north = (float)z;
  float h_south = (float)z;
  float h_east = (float)z;
  float h_west = (float)z;

  // Find actual surface heights by scanning down from current z
  for (uint32_t check_z = z; check_z > 0; check_z--)
  {
    Voxel *v = world_get_voxel(world, x, y, check_z);
    if (v && v->type != VOXEL_AIR)
    {
      h_center = (float)check_z;
      break;
    }
  }

  for (uint32_t check_z = z; check_z > 0; check_z--)
  {
    Voxel *v = world_get_voxel(world, x, y - 1, check_z);
    if (v && v->type != VOXEL_AIR)
    {
      h_north = (float)check_z;
      break;
    }
  }

  for (uint32_t check_z = z; check_z > 0; check_z--)
  {
    Voxel *v = world_get_voxel(world, x, y + 1, check_z);
    if (v && v->type != VOXEL_AIR)
    {
      h_south = (float)check_z;
      break;
    }
  }

  for (uint32_t check_z = z; check_z > 0; check_z--)
  {
    Voxel *v = world_get_voxel(world, x + 1, y, check_z);
    if (v && v->type != VOXEL_AIR)
    {
      h_east = (float)check_z;
      break;
    }
  }

  for (uint32_t check_z = z; check_z > 0; check_z--)
  {
    Voxel *v = world_get_voxel(world, x - 1, y, check_z);
    if (v && v->type != VOXEL_AIR)
    {
      h_west = (float)check_z;
      break;
    }
  }

  // Calculate slope as maximum height difference
  float slope_ns = fabsf(h_north - h_south);
  float slope_ew = fabsf(h_east - h_west);
  float max_slope = fmaxf(slope_ns, slope_ew);

  return max_slope / 2.0f; // Normalize to 0-1 range
}

// Select soil type based on underlying stone type
static inline VoxelType select_soil_type_for_stone(const World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType stone_type)
{
  float soil_noise = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.030f, 47.0f);

  // Different soil types form from different stone types
  switch (stone_type)
  {
  case VOXEL_STONE_BASALT:
    // Basalt forms rich, dark soil
    if (soil_noise < 0.5f)
      return VOXEL_SOIL; // Basic soil
    else
      return VOXEL_SOIL_LOAM; // Rich loam soil

  case VOXEL_STONE_GRANITE:
    // Granite forms sandy, well-drained soil
    if (soil_noise < 0.4f)
      return VOXEL_SOIL; // Basic soil
    else if (soil_noise < 0.7f)
      return VOXEL_SOIL_LOAM; // Loam soil
    else
      return VOXEL_SOIL_SILT; // Silt soil

  case VOXEL_STONE_LIMESTONE:
    // Limestone forms alkaline, clay-rich soil
    if (soil_noise < 0.3f)
      return VOXEL_SOIL_CLAY; // Clay soil
    else if (soil_noise < 0.6f)
      return VOXEL_SOIL; // Basic soil
    else
      return VOXEL_SOIL_LOAM; // Loam soil

  case VOXEL_STONE_SANDSTONE:
    // Sandstone forms sandy, well-drained soil
    if (soil_noise < 0.5f)
      return VOXEL_SOIL; // Basic soil
    else
      return VOXEL_SOIL_SILT; // Silt soil

  default:
    return VOXEL_SOIL; // Default to basic soil
  }
}

// Calculate stone layer height based on stone type and local conditions
static inline uint32_t calculate_stone_layer_height(const World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType stone_type)
{
  float layer_noise = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.025f, 41.0f);
  float height_var = sample_terrain_height_variation(world, x, y, z);

  // Base layer height varies by stone type
  uint32_t base_height = 1;
  if (stone_type == VOXEL_STONE_BASALT)
    base_height = 3; // Thick basalt layers
  else if (stone_type == VOXEL_STONE_GRANITE)
    base_height = 2; // Medium granite layers
  else if (stone_type == VOXEL_STONE_LIMESTONE)
    base_height = 2; // Medium limestone layers
  else
    base_height = 1; // Thin general stone layers

  // Add variation based on noise and height variation
  float variation = 0.5f + 1.5f * layer_noise + 0.5f * height_var;
  uint32_t layer_height = (uint32_t)(base_height * variation);

  // Ensure minimum height of 1 and maximum of 8
  if (layer_height < 1)
    layer_height = 1;
  if (layer_height > 8)
    layer_height = 8;

  return layer_height;
}

// Generalized 9-phase noise field function
static inline float sample_nine_phase_noise_field(const World *world, float w, float x, float y, float z,
                                                  float small_weight, float regional_weight, float universal_weight)
{
  // 9-phase noise with constant w for base generation; distinguish phases by scale/zseed
  // Small scale (local terrain features)
  float noise_small = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.020f, 17.0f);
  float noise_medium = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.010f, 23.0f);
  float noise_large = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.005f, 31.0f);

  // Regional scale (biome distribution)
  float noise_regional = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.002f, 41.0f);
  float noise_regional2 = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.001f, 47.0f);
  float noise_regional3 = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.0005f, 53.0f);

  // Universal scale (difficulty distribution)
  float noise_universal = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.0002f, 59.0f);
  float noise_universal2 = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.0001f, 61.0f);
  float noise_universal3 = sample_field_noise(world, w, (float)x, (float)y, (float)z, 0.00005f, 67.0f);

  // Combine all 9 phases with configurable weights
  float combined_noise = (noise_small * 0.4f * small_weight) +
                         (noise_medium * 0.3f * small_weight) +
                         (noise_large * 0.2f * small_weight) +
                         (noise_regional * 0.05f * regional_weight) +
                         (noise_regional2 * 0.03f * regional_weight) +
                         (noise_regional3 * 0.02f * regional_weight) +
                         (noise_universal * 0.01f * universal_weight) +
                         (noise_universal2 * 0.005f * universal_weight) +
                         (noise_universal3 * 0.005f * universal_weight);

  return combined_noise;
}

// Apply ore generation for a specific voxel position
static void apply_ore_generation(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType stone_type, float depth_ratio, uint32_t surface_cap, uint64_t *dbg_count_ore_au, uint64_t *dbg_count_ore_ag, uint64_t *dbg_count_ore_cu)
{
  if (!is_stone_type_for_ores(stone_type))
    return;

  uint32_t top_no_ore_margin = (uint32_t)fmaxf(3.0f, floorf(0.09375f * (float)world->depth));
  if (top_no_ore_margin >= surface_cap)
    top_no_ore_margin = surface_cap - 1;
  if (z >= surface_cap - top_no_ore_margin)
    return;

  // Simplified ore generation with canonical ore types
  VoxelType ore_type = generate_canonical_ore_type(world, x, y, z, depth_ratio, stone_type);
  if (ore_type != VOXEL_AIR)
  {
    // Place ore with simplified ellipsoid
    paint_simple_ore_vein(world, (int)x, (int)y, (int)z, ore_type);

    // These belong to the caller's per-world tally, not to a shared counter, which is why they
    // survived the removal of the static ones.
    if (dbg_count_ore_au && ore_type == VOXEL_ORE_GOLD)
      (*dbg_count_ore_au)++;
    else if (dbg_count_ore_ag && ore_type == VOXEL_ORE_SILVER)
      (*dbg_count_ore_ag)++;
    else if (dbg_count_ore_cu && ore_type == VOXEL_ORE_COPPER)
      (*dbg_count_ore_cu)++;
  }
}

// Apply crystal generation for a specific voxel position
static void apply_crystal_generation(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType stone_type, float depth_ratio, bool enable_crystals, uint64_t *dbg_count_crystals)
{
  if (!enable_crystals)
    return;

  if (!is_stone_type_for_crystals(stone_type))
    return;

  VoxelType crystal_type = generate_canonical_crystal_type(world, x, y, z, depth_ratio);
  if (crystal_type != VOXEL_AIR)
  {
    Voxel *voxel = world_get_voxel(world, x, y, z);
    if (voxel)
    {
      voxel->type = crystal_type;
      voxel_set_temperature(voxel, 255);
      if (dbg_count_crystals)
        (*dbg_count_crystals)++;
    }
  }
}

// Calculate which geological layer this z position belongs to using 9-phase noise
static inline uint32_t calculate_geological_layer(const World *world, float w, uint32_t x, uint32_t y, uint32_t z)
{
  // Use generalized 9-phase noise field for natural height variation
  float combined_noise = sample_nine_phase_noise_field(world, 0.0f, (float)x, (float)y, (float)z, 1.0f, 1.0f, 1.0f);

  // Calculate the surface height for this column using noise
  // This creates natural height variation instead of a flat surface
  float surface_height = (float)(world->depth - 1) * (0.6f + 0.4f * combined_noise);

  // If we're above the surface height, this is air
  if ((float)z > surface_height)
  {
    return 999; // Special value to indicate air/above surface
  }

  // Base layer thickness with noise variation: 8-25 voxels
  float base_thickness = 15.0f + 10.0f * combined_noise; // 8-25 voxels

  // Calculate which geological layer this z position belongs to
  uint32_t geological_layer = (uint32_t)(z / base_thickness);

  return geological_layer;
}

// Calculate biome distribution using regional scale noise
static inline float calculate_biome_distribution(const World *world, float w, uint32_t x, uint32_t y, uint32_t z)
{
  // Use generalized 9-phase noise field with emphasis on regional scales
  (void)w; // Base generation uses a constant w slice
  return sample_nine_phase_noise_field(world, 0.0f, (float)x, (float)y, (float)z, 0.1f, 1.0f, 0.1f);
}

// Calculate difficulty distribution using universal scale noise
static inline float calculate_difficulty_distribution(const World *world, float w, uint32_t x, uint32_t y, uint32_t z)
{
  // Use generalized 9-phase noise field with emphasis on universal scales
  (void)w; // Base generation uses a constant w slice
  return sample_nine_phase_noise_field(world, 0.0f, (float)x, (float)y, (float)z, 0.1f, 0.1f, 1.0f);
}

// Get the stone type for a specific geological layer
static inline VoxelType get_geological_layer_type(const World *world, uint32_t x, uint32_t y, uint32_t geological_layer)
{
  // Special case: above surface height (air)
  if (geological_layer == 999)
  {
    return VOXEL_AIR;
  }

  // Use noise to create variation in layer types - use universe coordinates for consistency
  float type_noise = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)geological_layer, 0.015f, 23.0f);

  UniverseBiomeSample biome = universe_biome_at(world, x, y, -1.0f);
  float desert = biome.weights[UNIVERSE_BIOME_DESERT];
  float volcanic = biome.weights[UNIVERSE_BIOME_VOLCANIC];
  float wetland = biome.weights[UNIVERSE_BIOME_WETLAND];
  float alpine = biome.weights[UNIVERSE_BIOME_ALPINE];

  // Default hardness stack: basalt → granite → limestone → sandstone
  VoxelType stack[4] = {
      VOXEL_STONE_BASALT,
      VOXEL_STONE_GRANITE,
      VOXEL_STONE_LIMESTONE,
      VOXEL_STONE_SANDSTONE,
  };

  // Biome lithology bias (shallow layers most affected).
  if (volcanic > 0.28f && type_noise < 0.35f + volcanic * 0.4f)
  {
    stack[0] = VOXEL_STONE_BASALT;
    stack[1] = VOXEL_STONE_BASALT;
    stack[2] = VOXEL_STONE_GRANITE;
    stack[3] = VOXEL_STONE_GRANITE;
  }
  else if (desert > 0.28f && type_noise < 0.40f + desert * 0.35f)
  {
    stack[1] = VOXEL_STONE_SANDSTONE;
    stack[2] = VOXEL_STONE_SANDSTONE;
    stack[3] = VOXEL_STONE_SANDSTONE;
  }
  else if (wetland > 0.28f && type_noise < 0.40f + wetland * 0.3f)
  {
    stack[1] = VOXEL_STONE_LIMESTONE;
    stack[2] = VOXEL_STONE_LIMESTONE;
    stack[3] = VOXEL_STONE_LIMESTONE;
  }
  else if (alpine > 0.28f && type_noise < 0.40f + alpine * 0.3f)
  {
    stack[1] = VOXEL_STONE_GRANITE;
    stack[2] = VOXEL_STONE_GRANITE;
    stack[3] = VOXEL_STONE_LIMESTONE;
  }

  if (geological_layer <= 3)
    return stack[geological_layer];
  return stack[3];
}

// Select canonical stone/soil type based on depth and standard entropy
static inline VoxelType select_canonical_stone_type(const World *world, uint32_t x, uint32_t y, uint32_t z, float depth_ratio)
{
  // Use standard entropy sampling for stone type variation
  float stone_type_var = sample_stone_type_variation(world, x, y, z);
  float noise = sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.020f, 23.0f);

  // Proper z-order weighting - materials should be strongly weighted by depth
  // Depth ratio: 0.0 = bedrock, 1.0 = surface

  // Natural stone generation - no rigid depth gates, terrain-aware distribution
  // Use noise and local conditions to create natural rock type distribution

  // Create natural rock type distribution based on noise patterns
  float rock_noise = noise + 0.2f * stone_type_var;

  // Add some depth influence but don't gate by rigid depth bands
  float depth_influence = 1.0f - (depth_ratio * depth_ratio); // More influence near bedrock

  // Natural rock type selection with terrain-aware distribution
  if (rock_noise < 0.4f * depth_influence)
  {
    // Volcanic rocks - more common in deeper areas but can appear anywhere
    if (rock_noise < 0.2f * depth_influence)
      return VOXEL_STONE_BASALT; // Dominant volcanic
    else
      return VOXEL_STONE_GRANITE; // Some igneous
  }
  else if (rock_noise < 0.7f + 0.2f * (1.0f - depth_influence))
  {
    // Sedimentary rocks - more common in shallower areas
    if (rock_noise < 0.55f + 0.1f * (1.0f - depth_influence))
      return VOXEL_STONE_LIMESTONE; // Limestone
    else
      return VOXEL_STONE_SANDSTONE; // Sandstone
  }
  else
  {
    // Mixed distribution - natural variation
    if (rock_noise < 0.9f)
      return VOXEL_STONE_SANDSTONE; // Sandstone
    else
      return VOXEL_STONE_LIMESTONE; // Some limestone
  }
}

// Check if stone/soil type can contain ores
static inline bool is_stone_type_for_ores(VoxelType stone_type)
{
  return (stone_type == VOXEL_STONE_BASALT ||
          stone_type == VOXEL_STONE_GRANITE ||
          stone_type == VOXEL_STONE_LIMESTONE ||
          stone_type == VOXEL_STONE_SANDSTONE ||
          stone_type == VOXEL_SOIL ||
          stone_type == VOXEL_SOIL_CLAY ||
          stone_type == VOXEL_SOIL_LOAM ||
          stone_type == VOXEL_SOIL_SILT);
}

// Check if stone type can contain crystals
static inline bool is_stone_type_for_crystals(VoxelType stone_type)
{
  return (stone_type == VOXEL_STONE_BASALT ||
          stone_type == VOXEL_STONE_GRANITE ||
          stone_type == VOXEL_STONE_LIMESTONE ||
          stone_type == VOXEL_STONE_SANDSTONE ||
          stone_type == VOXEL_STONE);
}

// Generate canonical ore type based on depth, host rock, biome, and entropy
static inline VoxelType generate_canonical_ore_type(const World *world, uint32_t x, uint32_t y, uint32_t z,
                                                   float depth_ratio, VoxelType host_stone)
{
  // Use standard entropy sampling for ore generation
  float ore_density_var = sample_ore_density_variation(world, x, y, z);

  // Depth-based ore distribution with rarity
  float rarity = 0.01f + 0.14f * ore_density_var;
  float surf_decay = 1.0f - depth_ratio; // Linear decay instead of quadratic

  UniverseBiomeSample biome = universe_biome_at(world, x, y, -1.0f);

  enum {
    HOST_BASALT = 1,
    HOST_GRANITE = 2,
    HOST_LIMESTONE = 4,
    HOST_SANDSTONE = 8,
    HOST_SOIL = 16,
    HOST_ANY_STONE = HOST_BASALT | HOST_GRANITE | HOST_LIMESTONE | HOST_SANDSTONE,
    HOST_ANY = HOST_ANY_STONE | HOST_SOIL
  };

  uint32_t host_bits = 0;
  if (host_stone == VOXEL_STONE_BASALT)
    host_bits = HOST_BASALT;
  else if (host_stone == VOXEL_STONE_GRANITE)
    host_bits = HOST_GRANITE;
  else if (host_stone == VOXEL_STONE_LIMESTONE)
    host_bits = HOST_LIMESTONE;
  else if (host_stone == VOXEL_STONE_SANDSTONE)
    host_bits = HOST_SANDSTONE;
  else
    host_bits = HOST_SOIL;

  typedef struct
  {
    float w;
    float depth_min;
    float depth_max;
    float k;
    VoxelType type;
    uint32_t host_mask;
  } OreChannel;

  // Host masks approximate real deposit styles; fantasy ores stay deep/rare.
  static const OreChannel ore_channels[] = {
      {1.0f, -1.0f, 0.20f, 0.005f, VOXEL_ORE_PLATINUM, HOST_BASALT | HOST_GRANITE},
      {2.0f, -1.0f, 0.20f, 0.008f, VOXEL_ORE_ADAMANTITE, HOST_BASALT | HOST_GRANITE},
      {12.0f, -1.0f, 0.15f, 0.003f, VOXEL_ORE_HEMATITE, HOST_BASALT | HOST_GRANITE},
      {3.0f, -1.0f, 0.30f, 0.5f, VOXEL_ORE_GOLD, HOST_GRANITE | HOST_BASALT | HOST_SANDSTONE},
      {4.0f, -1.0f, 0.50f, 0.6f, VOXEL_ORE_SILVER, HOST_GRANITE | HOST_LIMESTONE},
      {5.0f, -1.0f, 0.40f, 0.020f, VOXEL_ORE_MITHRIL, HOST_GRANITE | HOST_BASALT},
      {6.0f, -1.0f, 0.70f, 1.5f, VOXEL_ORE_COPPER, HOST_BASALT | HOST_GRANITE | HOST_SANDSTONE},
      {7.0f, -1.0f, 2.0f, 2.5f, VOXEL_ORE_IRON, HOST_ANY_STONE},
      {8.0f, -1.0f, 0.60f, 0.100f, VOXEL_ORE_TIN, HOST_GRANITE | HOST_SANDSTONE},
      {9.0f, -1.0f, 0.50f, 0.060f, VOXEL_ORE_LEAD, HOST_LIMESTONE | HOST_SANDSTONE},
      {10.0f, -1.0f, 0.60f, 0.080f, VOXEL_ORE_ZINC, HOST_LIMESTONE | HOST_SANDSTONE},
      {13.0f, -1.0f, 0.25f, 0.010f, VOXEL_ORE_TITANIUM, HOST_BASALT | HOST_GRANITE},
      {14.0f, -1.0f, 0.35f, 0.015f, VOXEL_ORE_COBALT, HOST_BASALT | HOST_GRANITE},
      {15.0f, -1.0f, 0.45f, 0.040f, VOXEL_ORE_NICKEL, HOST_BASALT | HOST_GRANITE},
      {16.0f, -1.0f, 0.55f, 0.070f, VOXEL_ORE_ALUMINUM, HOST_ANY_STONE | HOST_SOIL},
      {17.0f, -1.0f, 0.65f, 0.090f, VOXEL_ORE_MAGNESIUM, HOST_BASALT | HOST_LIMESTONE},
      {11.0f, 0.30f, 2.0f, 3.5f, VOXEL_ORE_COAL, HOST_LIMESTONE | HOST_SANDSTONE | HOST_SOIL},
  };

  for (size_t i = 0; i < sizeof(ore_channels) / sizeof(ore_channels[0]); i++)
  {
    const OreChannel *channel = &ore_channels[i];
    if (depth_ratio <= channel->depth_min || depth_ratio >= channel->depth_max)
      continue;
    if ((channel->host_mask & host_bits) == 0)
      continue;

    float k = channel->k;
    // Biome multipliers for deposit provinces
    if (channel->type == VOXEL_ORE_COAL)
      k *= 1.0f + 1.8f * biome.weights[UNIVERSE_BIOME_WETLAND] +
           0.6f * biome.weights[UNIVERSE_BIOME_TEMPERATE];
    else if (channel->type == VOXEL_ORE_COPPER)
      k *= 1.0f + 1.4f * biome.weights[UNIVERSE_BIOME_VOLCANIC] +
           0.8f * biome.weights[UNIVERSE_BIOME_DESERT];
    else if (channel->type == VOXEL_ORE_IRON || channel->type == VOXEL_ORE_TITANIUM ||
             channel->type == VOXEL_ORE_PLATINUM || channel->type == VOXEL_ORE_COBALT ||
             channel->type == VOXEL_ORE_NICKEL)
      k *= 1.0f + 1.4f * biome.weights[UNIVERSE_BIOME_VOLCANIC];
    else if (channel->type == VOXEL_ORE_GOLD || channel->type == VOXEL_ORE_SILVER ||
             channel->type == VOXEL_ORE_TIN)
      k *= 1.0f + 1.0f * biome.weights[UNIVERSE_BIOME_ALPINE];
    else if (channel->type == VOXEL_ORE_LEAD || channel->type == VOXEL_ORE_ZINC)
      k *= 1.0f + 1.2f * biome.weights[UNIVERSE_BIOME_WETLAND];

    float noise = sample_field_noise(world, channel->w, (float)x, (float)y, (float)z, 0.071f, 59.0f);
    if (noise < rarity * k * surf_decay)
      return channel->type;
  }

  return VOXEL_AIR; // No ore
}

// Generate canonical crystal type using standard entropy
static inline VoxelType generate_canonical_crystal_type(const World *world, uint32_t x, uint32_t y, uint32_t z, float depth_ratio)
{
  // Debug output
  // Crystals only form in the deepest fifth of the world. Test that before sampling:
  // the two samples below dominate this function's cost and were previously taken for
  // every candidate voxel only to be discarded by this same depth condition, so four out
  // of five calls paid for them for nothing. Both samplers are pure functions of the
  // coordinates, so gating first cannot change which crystals appear.
  if (depth_ratio <= 0.8f)
    return VOXEL_AIR;

  // Use standard entropy sampling for crystal generation
  float crystal_density_var = sample_crystal_density_variation(world, x, y, z);
  // Use a dedicated w offset for crystal sampling (special layer semantics)
  float crystal_noise = sample_field_noise(world, 2.0f, (float)x, (float)y, (float)z, 0.093f, 83.0f);

  float rarity = 0.01f + 0.14f * crystal_density_var;
  float surf_decay = (1.0f - depth_ratio) * (1.0f - depth_ratio);
  float threshold = rarity * 0.0020f * surf_decay;

  // Very rare crystal generation - target a few dozen crystals per world
  if (crystal_noise < threshold)
  {
    // Select crystal type based on standard entropy
    float type_noise = sample_field_noise(world, 3.0f, (float)x, (float)y, (float)z, 0.157f, 127.0f);
    int pick = (int)fminf(2.0f, floorf(type_noise * 3.0f));

    switch (pick)
    {
    case 0:
      return VOXEL_CRYSTAL_RED;
    case 1:
      return VOXEL_CRYSTAL_GREEN;
    case 2:
      return VOXEL_CRYSTAL_BLUE;
    default:
      return VOXEL_CRYSTAL;
    }
  }

  return VOXEL_AIR; // No crystal
}

// Simplified ore vein painting
static inline void paint_simple_ore_vein(World *world, int center_x, int center_y, int center_z, VoxelType ore_type)
{
  // Simple 3x3x2 ore vein instead of complex ellipsoid
  for (int dz = 0; dz < 2; dz++)
  {
    for (int dy = -1; dy <= 1; dy++)
    {
      for (int dx = -1; dx <= 1; dx++)
      {
        int px = center_x + dx;
        int py = center_y + dy;
        int pz = center_z + dz;

        if (!world_is_position_valid(world, px, py, pz))
          continue;

        Voxel *v = world_get_voxel(world, (uint32_t)px, (uint32_t)py, (uint32_t)pz);
        if (v && (v->type == VOXEL_STONE_BASALT || v->type == VOXEL_STONE_GRANITE ||
                  v->type == VOXEL_STONE_LIMESTONE || v->type == VOXEL_STONE_SANDSTONE ||
                  v->type == VOXEL_STONE ||
                  v->type == VOXEL_SOIL || v->type == VOXEL_SOIL_CLAY ||
                  v->type == VOXEL_SOIL_LOAM || v->type == VOXEL_SOIL_SILT))
        {
          v->type = ore_type;
        }
      }
    }
  }
}

// --- Magma field / bedrock hotspot APIs ---------------------------------------
float world_sample_magma_field(World *world, int x, int y, int z)
{
  if (!world)
    return 1.0f;
  return sample_field_noise(world, 0.0f, (float)x, (float)y, (float)z, 0.041f, 137.0f);
}

int world_magma_seed_at(World *world, int x, int y, int z)
{
  if (!world)
    return 0;
  float n = world_sample_magma_field(world, x, y, z);
  // Wilderness/scoured condition for seeding magma disks at z=1
  return (z == 1 && n < 0.08f) ? 1 : 0;
}

bool world_magma_hotspot_at(const World *world, int x, int y)
{
  if (!world || x < 0 || y < 0)
    return false;
  if ((uint32_t)x >= world->width || (uint32_t)y >= world->height)
    return false;

  // Stamped volcano conduits stay hot even when they wander off the natural noise vents.
  if (world_has_magma_vent_column(world, x, y))
    return true;

  // Vent sources live in the bedrock plane (z=0). Same noise family as the magma disk field so
  // generated pools sit on vents; a rare volcanic-province gate opens slightly cooler vents.
  const float n = world_sample_magma_field((World *)world, x, y, 0);
  if (n < 0.08f)
    return true;

  UniverseClimate climate = universe_climate_sample(world, (uint32_t)x, (uint32_t)y, 0.0f);
  return climate.volcanic > 0.55f && n < 0.12f;
}

// -----------------------------
// Shared distribution helpers
// -----------------------------
static inline void paint_disk_at_z(World *world,
                                   int center_x, int center_y, int z,
                                   int radius,
                                   VoxelType type,
                                   bool overwrite)
{
  int radius2 = radius * radius;
  for (int dy = -radius; dy <= radius; dy++)
  {
    for (int dx = -radius; dx <= radius; dx++)
    {
      if (dx * dx + dy * dy > radius2)
        continue;
      int px = center_x + dx;
      int py = center_y + dy;
      if (!world_is_position_valid(world, px, py, z))
        continue;
      Voxel *v = world_get_voxel(world, (uint32_t)px, (uint32_t)py, (uint32_t)z);
      if (!v)
        continue;
      if (!overwrite && v->type != VOXEL_AIR)
        continue;
      v->type = type;
    }
  }
}

// Paint a filled axis-aligned ellipse on plane z with radii rx, ry
static inline void paint_ellipse_at_z(World *world,
                                      int center_x, int center_y, int z,
                                      int radius_x, int radius_y,
                                      VoxelType type,
                                      bool overwrite)
{
  if (!world || !world->voxels)
    return;
  if (z < 0 || z >= (int)world->depth)
    return;
  if (radius_x < 0 || radius_y < 0)
    return;
  int rx = radius_x;
  int ry = radius_y;
  int xmin = center_x - rx;
  int xmax = center_x + rx;
  int ymin = center_y - ry;
  int ymax = center_y + ry;
  float inv_rx2 = (rx > 0) ? 1.0f / (float)(rx * rx) : 0.0f;
  float inv_ry2 = (ry > 0) ? 1.0f / (float)(ry * ry) : 0.0f;
  for (int y = ymin; y <= ymax; y++)
  {
    for (int x = xmin; x <= xmax; x++)
    {
      if (!world_is_position_valid(world, x, y, z))
        continue;
      float dx = (float)(x - center_x);
      float dy = (float)(y - center_y);
      float v = dx * dx * inv_rx2 + dy * dy * inv_ry2;
      if (v <= 1.0f)
      {
        Voxel *vxl = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (!vxl)
          continue;
        if (!overwrite && vxl->type != VOXEL_AIR)
          continue;
        vxl->type = type;
      }
    }
  }
}

// Scatter a low-density layer of a type (crystals) among a given host voxel type
// Uses contiguous noise fields and column-aware depth gating for seam continuity
static void scatter_type_among(World *world,
                               uint32_t surface_cap,
                               VoxelType host_type)
{
  if (!world || !world->voxels)
    return;
  const uint32_t W = world->width, H = world->height, D = world->depth;
  if (surface_cap == 0 || surface_cap >= D)
    surface_cap = (D > 0 ? D - 1 : 0);

  for (uint32_t y = 0; y < H; y++)
  {
    for (uint32_t x = 0; x < W; x++)
    {
      // Column height variation using entropy field (consistent with strata)
      float terrain_height_var = sample_terrain_height_variation(world, x, y, 0);

      float col_cap_f = (float)surface_cap * (1.0f + 0.12f * terrain_height_var);
      if (col_cap_f < 2.0f)
        col_cap_f = 2.0f;
      if (col_cap_f > (float)surface_cap)
        col_cap_f = (float)surface_cap;

      for (uint32_t z = 1; z <= surface_cap; z++)
      {
        Voxel *v = world_get_voxel(world, x, y, z);
        if (!v || v->type != host_type)
          continue;
        // Prefer the upper stone band ("sandstone")
        float t = (float)z / col_cap_f;
        float stone_type_var = sample_stone_type_variation(world, x, y, z);
        float c_top = 0.72f + 0.04f * stone_type_var;
        float s_top = 0.12f * (1.0f + 0.15f * stone_type_var);
        float g_top = expf(-0.5f * ((t - c_top) / s_top) * ((t - c_top) / s_top));
        float prox = magma_influence_contiguous(world, (int)x, (int)y, (int)z);
        // Use pure 4D Perlin noise without rotation or warping
        float r = (float)perlin_noise(0.0, (float)x * 0.091f, (float)y * 0.091f, (float)z * 0.073f + 127.0f + g_universe_perlin_seed * 0.00001f);
        float p = 0.000009f * (0.6f + world->rarity * 0.8f) * g_top * (1.0f + prox * 0.6f);
        if (r < p)
        {
          // Pick crystal color deterministically using pure 4D noise
          float cp = (float)perlin_noise(0.0, (float)x * 0.157f, (float)y * 0.157f, (float)z * 0.157f + 127.0f + g_universe_perlin_seed * 0.00001f);
          int pick = (int)fminf(2.0f, floorf(cp * 3.0f));
          VoxelType ctype = (pick == 0) ? VOXEL_CRYSTAL_RED : (pick == 1 ? VOXEL_CRYSTAL_GREEN : VOXEL_CRYSTAL_BLUE);
          v->type = ctype;
          voxel_set_temperature(v, 255);
        }
      }
    }
  }
}

// Apply a 1-voxel water cap above top_of_column (if provided) or above any existing clay/stone top.
// clamp_z_max caps water placement height. If top_of_column is NULL, we conservatively place
// water only where the immediate cell above a solid is AIR.
static void apply_water_cap(World *world, const int *top_of_column, uint32_t clamp_z_max, uint32_t thickness)
{
  if (!world || !world->voxels)
    return;
  const uint32_t W = world->width, H = world->height, D = world->depth;
  uint32_t zmax = clamp_z_max;
  if (zmax >= D)
    zmax = (D > 0 ? D - 1 : 0);

  // Hug the surface: place exactly one water voxel at the first air above solid per column
  (void)thickness; // ignored; we always place 1 layer that hugs terrain
  for (uint32_t y = 0; y < H; y++)
  {
    for (uint32_t x = 0; x < W; x++)
    {
      int wz = -1;
      if (top_of_column)
      {
        size_t cidx = (size_t)y * (size_t)W + (size_t)x;
        wz = top_of_column[cidx] + 1;
      }
      else
      {
        // Fallback: scan to find the first AIR directly above a solid
        for (uint32_t z = 1; z < D && z <= zmax; z++)
        {
          Voxel *cur = world_get_voxel(world, x, y, z);
          Voxel *below = world_get_voxel(world, x, y, z - 1);
          if (cur && below && cur->type == VOXEL_AIR && below->type != VOXEL_AIR)
          {
            wz = (int)z;
            break;
          }
        }
      }
      if (wz < 0 || (uint32_t)wz >= D)
        continue;
      if ((uint32_t)wz > zmax)
        continue;
      Voxel *wv = world_get_voxel(world, x, y, (uint32_t)wz);
      if (!wv)
        continue;
      wv->type = VOXEL_WATER;
      voxel_set_quantity_or_wetness(wv, FLUID_LEVEL_FULL);
    }
  }
  world_invalidate_fluid_presence(world); // water written directly, bypassing world_set_voxel
}

// Implementation of stone bands transform
static inline VoxelType transform_stone_bands(const World *world,
                                              uint32_t x, uint32_t y, uint32_t z,
                                              float t,
                                              float bias)
{
  // Match the previous inlined logic exactly
  const float stone_scale = 0.020f;
  float stone_scale_factor = 1.0f + 0.35f * bias;
  if (stone_scale_factor < 0.35f)
    stone_scale_factor = 0.35f;
  float sx1, sy1, sx2, sy2;
  // Use pure 4D Perlin noise without rotation or warping
  float u1 = (float)perlin_noise(0.0, (float)x * stone_scale * stone_scale_factor, (float)y * stone_scale * stone_scale_factor, (float)z * 0.005f + g_universe_perlin_seed * 0.00001f);
  float u2 = (float)perlin_noise(0.0, (float)x * stone_scale * 2.0f * stone_scale_factor, (float)y * stone_scale * 2.0f * stone_scale_factor, (float)z * 0.005f + 19.17f + g_universe_perlin_seed * 0.00001f);
  float u = 0.7f * u1 + 0.3f * u2;
  if (u < 0.0f)
    u = 0.0f;
  if (u > 1.0f)
    u = 1.0f;

  float c_bas = 0.12f + 0.05f * bias, s_bas = 0.10f * (1.0f + 0.25f * bias);
  float c_gra = 0.28f + 0.06f * bias, s_gra = 0.12f * (1.0f + 0.25f * bias);
  float c_lime = 0.46f + 0.08f * bias, s_lime = 0.16f * (1.0f + 0.25f * bias);
  float w_basalt = 0.85f * expf(-0.5f * ((t - c_bas) / s_bas) * ((t - c_bas) / s_bas));
  float w_granite = 0.70f * expf(-0.5f * ((t - c_gra) / s_gra) * ((t - c_gra) / s_gra));
  float w_limestone = 0.60f * expf(-0.5f * ((t - c_lime) / s_lime) * ((t - c_lime) / s_lime));
  float sum = w_basalt + w_granite + w_limestone;
  if (sum < 1e-6f)
    sum = 1.0f;
  float c_basalt_w = w_basalt / sum;
  float c_granite_w = c_basalt_w + w_granite / sum;
  float c_limestone_w = c_granite_w + w_limestone / sum;
  VoxelType tstone = VOXEL_STONE;
  if (u < c_basalt_w)
    tstone = VOXEL_STONE_BASALT;
  else if (u < c_granite_w)
    tstone = VOXEL_STONE_GRANITE;
  else if (u < c_limestone_w)
    tstone = VOXEL_STONE_LIMESTONE;
  else
    tstone = VOXEL_STONE;
  return tstone;
}



// Debug field samplers for boundary verification
float world_sample_occupancy_noise(World *world, int x, int y, int z)
{
  if (!world)
    return 0.0f;
  // Use pure 4D Perlin noise without rotation or warping
  return (float)perlin_noise(0.0, (float)x * 0.020f, (float)y * 0.020f, (float)z * 0.011f + universe_z_bias());
}

float world_sample_rarity_column(World *world, int x, int y)
{
  if (!world)
    return 0.0f;
  // Use pure 4D Perlin noise without rotation or warping
  float rare_col = (float)perlin_noise(0.0, (float)x * 0.0033f, (float)y * 0.0033f, g_universe_perlin_seed * 0.0001f + 13.0f);
  if (rare_col < 0.0f)
    rare_col = 0.0f;
  if (rare_col > 1.0f)
    rare_col = 1.0f;
  return 0.01f + rare_col * 0.14f;
}

float world_sample_stone_field(World *world, int x, int y, int z)
{
  if (!world)
    return 0.0f;
  const float stone_scale = 0.020f;
  // Use pure 4D Perlin noise without rotation or warping
  float u1 = (float)perlin_noise(0.0, (float)x * stone_scale, (float)y * stone_scale, (float)z * 0.005f + universe_z_bias());
  float u2 = (float)perlin_noise(0.0, (float)x * stone_scale * 2.0f, (float)y * stone_scale * 2.0f, (float)z * 0.005f + 19.17f + universe_z_bias());
  float u = 0.7f * u1 + 0.3f * u2;
  if (u < 0.0f)
    u = 0.0f;
  if (u > 1.0f)
    u = 1.0f;
  return u;
}

// Helpers to paint 3D ellipsoids into rock-only regions
static inline bool is_rock_type(VoxelType t)
{
  return (t == VOXEL_STONE_BASALT || t == VOXEL_STONE_GRANITE || t == VOXEL_STONE_LIMESTONE || t == VOXEL_STONE);
}

static inline void paint_ellipsoid_onto_rocks(World *world,
                                              int center_x, int center_y, int center_z,
                                              int radius_x, int radius_y, int radius_z,
                                              VoxelType type)
{
  if (!world)
    return;
  if (radius_x < 1)
    radius_x = 1;
  if (radius_y < 1)
    radius_y = 1;
  if (radius_z < 1)
    radius_z = 1;
  int rx2 = radius_x * radius_x;
  int ry2 = radius_y * radius_y;
  int rz2 = radius_z * radius_z;
  for (int oz = -radius_z; oz <= radius_z; oz++)
  {
    int zz = center_z + oz;
    if (zz < 0 || zz >= (int)world->depth)
      continue;
    for (int oy = -radius_y; oy <= radius_y; oy++)
    {
      int yy = center_y + oy;
      if (yy < 0 || yy >= (int)world->height)
        continue;
      for (int ox = -radius_x; ox <= radius_x; ox++)
      {
        int xx = center_x + ox;
        if (xx < 0 || xx >= (int)world->width)
          continue;
        // Ellipsoid equation: (ox^2/rx^2) + (oy^2/ry^2) + (oz^2/rz^2) <= 1
        int v = (ox * ox) * ry2 * rz2 + (oy * oy) * rx2 * rz2 + (oz * oz) * rx2 * ry2;
        int denom = rx2 * ry2 * rz2;
        if (v > denom)
          continue;
        Voxel *voxel = world_get_voxel(world, (uint32_t)xx, (uint32_t)yy, (uint32_t)zz);
        if (!voxel)
          continue;
        if (!is_rock_type(voxel->type))
          continue;
        voxel->type = type;
      }
    }
  }
}

// Jitter a target position slightly using a universe-seeded noise field to decorrelate patterns
static inline void jitter_coords(World *world,
                                 int x, int y, int z,
                                 int max_xy, int max_z,
                                 int *out_x, int *out_y, int *out_z)
{
  if (!world || !out_x || !out_y || !out_z)
    return;
  // Use pure 4D Perlin noise for jitter without rotation or warping
  float nx = (float)perlin_noise(0.0, (float)x * 0.137f, (float)y * 0.137f, (float)z * 0.013f + 17.0f + g_universe_perlin_seed * 0.00001f);
  float ny = (float)perlin_noise(0.0, (float)x * 0.137f + 37.0f, (float)y * 0.137f - 19.0f, (float)z * 0.011f + 29.0f + g_universe_perlin_seed * 0.00001f);
  float nz = (float)perlin_noise(0.0, (float)x * 0.137f - 23.0f, (float)y * 0.137f + 53.0f, 41.0f + g_universe_perlin_seed * 0.00001f);
  int dx = (int)lrintf(((nx - 0.5f) * 2.0f) * (float)max_xy);
  int dy = (int)lrintf(((ny - 0.5f) * 2.0f) * (float)max_xy);
  int dz = (int)lrintf(((nz - 0.5f) * 2.0f) * (float)max_z);
  int tx = x + dx;
  int ty = y + dy;
  int tz = z + dz;
  if (tx < 0)
    tx = 0;
  if (ty < 0)
    ty = 0;
  if (tz < 0)
    tz = 0;
  if (tx >= (int)world->width)
    tx = (int)world->width - 1;
  if (ty >= (int)world->height)
    ty = (int)world->height - 1;
  if (tz >= (int)world->depth)
    tz = (int)world->depth - 1;
  *out_x = tx;
  *out_y = ty;
  *out_z = tz;
}

// Estimate proximity to magma (0..1) by scanning a small 3D neighborhood for VOXEL_MAGMA
static inline float magma_proximity_weight(const World *world, int x, int y, int z)
{
  if (!world)
    return 0.0f;
  const int max_dz = 6;
  const int max_r = 6;
  float best = 0.0f;
  for (int dz = 0; dz <= max_dz && z - dz >= 0; dz++)
  {
    int zz = z - dz;
    for (int dy = -max_r; dy <= max_r; dy++)
    {
      for (int dx = -max_r; dx <= max_r; dx++)
      {
        int xx = x + dx;
        int yy = y + dy;
        if (!world_is_position_valid((World *)world, xx, yy, zz))
          continue;
        const Voxel *v = world_get_voxel((World *)world, (uint32_t)xx, (uint32_t)yy, (uint32_t)zz);
        if (!v || v->type != VOXEL_MAGMA)
          continue;
        int d2 = dx * dx + dy * dy + dz * dz;
        if (d2 == 0)
          return 1.0f;
        float w = expf(-(float)d2 / (2.0f * 9.0f)); // sigma^2=9 → sigma≈3
        if (w > best)
          best = w;
      }
    }
  }
  return best;
}

// Continuous magma-influence field derived from the same noise used for magma at z=1
// Avoids world-boundary artifacts by not scanning placed voxels
static inline float magma_influence_contiguous(World *world, int x, int y, int z)
{
  if (!world)
    return 0.0f;
  // Reuse the magma disk field: n < 0.08 → magma; measure how far below threshold
  float n = sample_field_noise(world, 0.0f, (float)x, (float)y, 1.0f, 0.041f, 137.0f);
  float s = 0.08f - n; // positive near magma seeds
  if (s <= 0.0f)
    return 0.0f;
  // Normalize and apply gentle smoothing
  float inf2d = s * 12.5f; // 0.08 range → scale to ~1
  if (inf2d > 1.0f)
    inf2d = 1.0f;
  // Vertical decay with distance from z=1
  int dz = (z > 1) ? (z - 1) : (1 - z);
  float vdecay = expf(-(float)dz / 3.0f);
  return inf2d * vdecay;
}

// -----------------------------
// Timing helper (ms)
// -----------------------------
static inline unsigned long long now_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)(tv.tv_usec / 1000ULL);
}

// Generate gravity value using bell curve distribution
static float generate_gravity_from_seed(const char *seed)
{
  // Create a deterministic hash from the seed
  uint8_t hash[32];
  calculate_sha256(seed, hash);

  // Use first 8 bytes to generate two uniform random numbers. The casts matter: uint8_t
  // promotes to int, so shifting a byte above 127 left by 24 overflows a signed int, which is
  // undefined rather than the wraparound the expression assumes.
  uint32_t u1_raw = ((uint32_t)hash[0] << 24) | ((uint32_t)hash[1] << 16) |
                    ((uint32_t)hash[2] << 8) | (uint32_t)hash[3];
  uint32_t u2_raw = ((uint32_t)hash[4] << 24) | ((uint32_t)hash[5] << 16) |
                    ((uint32_t)hash[6] << 8) | (uint32_t)hash[7];

  // Convert to uniform random numbers between 0 and 1
  double u1 = (double)u1_raw / (double)UINT32_MAX;
  double u2 = (double)u2_raw / (double)UINT32_MAX;

  // Apply Box-Muller transform to get normal distribution
  double normal_value = box_muller_transform(u1, u2);

  // Most worlds cluster near the game default; the tails reach floaty and heavy.
  double sigma = (double)(GRAVITY_MAX - GRAVITY_MIN) * 0.18;
  float gravity = GRAVITY_DEFAULT + (float)(normal_value * sigma);

  // Clamp to min/max bounds
  if (gravity < GRAVITY_MIN)
    gravity = GRAVITY_MIN;
  if (gravity > GRAVITY_MAX)
    gravity = GRAVITY_MAX;

  return gravity;
}

// Perlin noise implementation
#define PERLIN_NOISE_TABLE_SIZE 256
static int perlin_permutation[PERLIN_NOISE_TABLE_SIZE * 2];

// Initialization of the permutation table for Perlin noise
static void init_perlin_noise(unsigned int seed)
{
  int i;
  for (i = 0; i < PERLIN_NOISE_TABLE_SIZE; i++)
  {
    perlin_permutation[i] = i;
  }

  // Fisher-Yates shuffle
  for (i = PERLIN_NOISE_TABLE_SIZE - 1; i > 0; i--)
  {
    seed = seed * 1103515245 + 12345;
    int j = (seed % (i + 1));
    int temp = perlin_permutation[i];
    perlin_permutation[i] = perlin_permutation[j];
    perlin_permutation[j] = temp;
  }

  // Copy the permutation table
  for (i = 0; i < PERLIN_NOISE_TABLE_SIZE; i++)
  {
    perlin_permutation[PERLIN_NOISE_TABLE_SIZE + i] = perlin_permutation[i];
  }
}

// Linear interpolation
static double lerp(double a, double b, double t)
{
  return a + t * (b - a);
}

// Fade function for smoother interpolation
static double fade(double t)
{
  return t * t * t * (t * (t * 6 - 15) + 10);
}

// Gradient function for Perlin noise
static double grad(int hash, double w, double x, double y, double z)
{
  int h = hash & 15;
  double u = h < 8 ? x : y;
  double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  double t = h < 8 ? w : z;
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v) + ((h & 4) == 0 ? t : -t);
}

// Perlin noise function (4D)
double perlin_noise(double w, double x, double y, double z)
{
  int W = (int)floor(w) & 255;
  int X = (int)floor(x) & 255;
  int Y = (int)floor(y) & 255;
  int Z = (int)floor(z) & 255;

  w -= floor(w);
  x -= floor(x);
  y -= floor(y);
  z -= floor(z);

  double u = fade(x);
  double v = fade(y);
  double w_fade = fade(z);
  double t = fade(w);

  int A = perlin_permutation[W] + X;
  int AA = perlin_permutation[A] + Y;
  int AAA = perlin_permutation[AA] + Z;
  int AAB = perlin_permutation[AA + 1] + Z;
  int AB = perlin_permutation[A + 1] + Y;
  int ABA = perlin_permutation[AB] + Z;
  int ABB = perlin_permutation[AB + 1] + Z;
  int B = perlin_permutation[W + 1] + X;
  int BA = perlin_permutation[B] + Y;
  int BAA = perlin_permutation[BA] + Z;
  int BAB = perlin_permutation[BA + 1] + Z;
  int BB = perlin_permutation[B + 1] + Y;
  int BBA = perlin_permutation[BB] + Z;
  int BBB = perlin_permutation[BB + 1] + Z;

  double res = lerp(
      lerp(lerp(lerp(grad(perlin_permutation[AAA], w, x, y, z),
                     grad(perlin_permutation[BAA], w - 1, x, y, z), u),
                lerp(grad(perlin_permutation[ABA], w, x - 1, y, z),
                     grad(perlin_permutation[BBA], w - 1, x - 1, y, z), u), v),
           lerp(lerp(grad(perlin_permutation[AAB], w, x, y - 1, z),
                     grad(perlin_permutation[BAB], w - 1, x, y - 1, z), u),
                lerp(grad(perlin_permutation[ABB], w, x - 1, y - 1, z),
                     grad(perlin_permutation[BBB], w - 1, x - 1, y - 1, z), u), v), w_fade),
      lerp(lerp(lerp(grad(perlin_permutation[AAA + 1], w, x, y, z - 1),
                     grad(perlin_permutation[BAA + 1], w - 1, x, y, z - 1), u),
                lerp(grad(perlin_permutation[ABA + 1], w, x - 1, y, z - 1),
                     grad(perlin_permutation[BBA + 1], w - 1, x - 1, y, z - 1), u), v),
           lerp(lerp(grad(perlin_permutation[AAB + 1], w, x, y - 1, z - 1),
                     grad(perlin_permutation[BAB + 1], w - 1, x, y - 1, z - 1), u),
                lerp(grad(perlin_permutation[ABB + 1], w, x - 1, y - 1, z - 1),
                     grad(perlin_permutation[BBB + 1], w - 1, x - 1, y - 1, z - 1), u), v), w_fade), t);

  return (res + 1.0) / 2.0; // Normalize to 0-1
}

// 4D Simplex Noise Implementation
// Based on the improved Simplex noise algorithm
static const double F4 = 0.3090169943749474241022934171828190588601545899028814310677243113526302314094512248536036020946955687; // (sqrt(5.0) - 1.0) / 4.0
static const double G4 = 0.1381966011250105151795413165634361908082482168904848794921379077097996844140917698426886778947527456; // (5.0 - sqrt(5.0)) / 20.0

static double dot4d(int grad_index, double x, double y, double z, double w) {
  // 4D gradient vectors (32 of them)
  static const int grad4[32][4] = {
    {0,1,1,1}, {0,1,1,-1}, {0,1,-1,1}, {0,1,-1,-1},
    {0,-1,1,1}, {0,-1,1,-1}, {0,-1,-1,1}, {0,-1,-1,-1},
    {1,0,1,1}, {1,0,1,-1}, {1,0,-1,1}, {1,0,-1,-1},
    {-1,0,1,1}, {-1,0,1,-1}, {-1,0,-1,1}, {-1,0,-1,-1},
    {1,1,0,1}, {1,1,0,-1}, {1,-1,0,1}, {1,-1,0,-1},
    {-1,1,0,1}, {-1,1,0,-1}, {-1,-1,0,1}, {-1,-1,0,-1},
    {1,1,1,0}, {1,1,-1,0}, {1,-1,1,0}, {1,-1,-1,0},
    {-1,1,1,0}, {-1,1,-1,0}, {-1,-1,1,0}, {-1,-1,-1,0}
  };

  int gi = grad_index & 31;
  return grad4[gi][0] * x + grad4[gi][1] * y + grad4[gi][2] * z + grad4[gi][3] * w;
}

double simplex_noise(double w, double x, double y, double z) {
  // Skew the input space to determine which simplex cell we're in
  double s = (x + y + z + w) * F4; // Factor for 4D skewing
  int i = (int)floor(x + s);
  int j = (int)floor(y + s);
  int k = (int)floor(z + s);
  int l = (int)floor(w + s);

  double t = (i + j + k + l) * G4; // Factor for 4D unskewing
  double X0 = i - t; // Unskew the cell origin back to (x,y,z,w) space
  double Y0 = j - t;
  double Z0 = k - t;
  double W0 = l - t;
  double x0 = x - X0; // The x,y,z,w distances from the cell origin
  double y0 = y - Y0;
  double z0 = z - Z0;
  double w0 = w - W0;

  // For the 4D case, the simplex is a 4D shape I won't try to describe.
  // To find out which of the 24 possible simplices we're in, we need to
  // determine the magnitude ordering of x0, y0, z0 and w0.
  // The method below is a good way of finding the ordering of x,y,z,w and
  // then find the simplex we're in.
  int c1 = (x0 > y0) ? 32 : 0;
  int c2 = (x0 > z0) ? 16 : 0;
  int c3 = (y0 > z0) ? 8 : 0;
  int c4 = (x0 > w0) ? 4 : 0;
  int c5 = (y0 > w0) ? 2 : 0;
  int c6 = (z0 > w0) ? 1 : 0;
  int c = c1 + c2 + c3 + c4 + c5 + c6;

  int i1, j1, k1, l1; // The integer offsets for the second simplex corner
  int i2, j2, k2, l2; // The integer offsets for the third simplex corner
  int i3, j3, k3, l3; // The integer offsets for the fourth simplex corner

  // simplex[c] is a 4-vector with the numbers 0, 1, 2 and 3 in some order.
  // Many values of c will never occur, since e.g. x>y>z>w makes x<z, y<w and x<w
  // impossible. Only the 24 indices which have non-zero entries make any sense.
  // We use a thresholding to set the coordinates in turn from the largest magnitude.
  // The number 3 in the "simplex" array is at the position of the largest coordinate.
  int simplex[64][4] = {
    {0,1,2,3},{0,1,3,2},{0,0,0,0},{0,2,3,1},{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,2,3,0},
    {0,2,1,3},{0,0,0,0},{0,3,1,2},{0,3,2,1},{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,3,2,0},
    {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
    {1,2,0,3},{0,0,0,0},{1,3,0,2},{0,0,0,0},{0,0,0,0},{0,0,0,0},{2,3,0,1},{2,3,1,0},
    {1,0,2,3},{1,0,3,2},{0,0,0,0},{0,0,0,0},{0,0,0,0},{2,0,3,1},{0,0,0,0},{2,1,3,0},
    {0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
    {2,0,1,3},{0,0,0,0},{0,0,0,0},{0,0,0,0},{3,0,1,2},{3,0,2,1},{0,0,0,0},{3,1,2,0},
    {2,1,0,3},{0,0,0,0},{0,0,0,0},{0,0,0,0},{3,1,0,2},{0,0,0,0},{3,2,0,1},{3,2,1,0}
  };

  i1 = simplex[c][0] >= 3 ? 1 : 0;
  j1 = simplex[c][1] >= 3 ? 1 : 0;
  k1 = simplex[c][2] >= 3 ? 1 : 0;
  l1 = simplex[c][3] >= 3 ? 1 : 0;

  i2 = simplex[c][0] >= 2 ? 1 : 0;
  j2 = simplex[c][1] >= 2 ? 1 : 0;
  k2 = simplex[c][2] >= 2 ? 1 : 0;
  l2 = simplex[c][3] >= 2 ? 1 : 0;

  i3 = simplex[c][0] >= 1 ? 1 : 0;
  j3 = simplex[c][1] >= 1 ? 1 : 0;
  k3 = simplex[c][2] >= 1 ? 1 : 0;
  l3 = simplex[c][3] >= 1 ? 1 : 0;

  // The step size is 1/4 of the distance from the origin to the next simplex
  double x1 = x0 - i1 + G4; // Offsets for second corner in (x,y,z,w) coords
  double y1 = y0 - j1 + G4;
  double z1 = z0 - k1 + G4;
  double w1 = w0 - l1 + G4;
  double x2 = x0 - i2 + 2.0 * G4; // Offsets for third corner in (x,y,z,w) coords
  double y2 = y0 - j2 + 2.0 * G4;
  double z2 = z0 - k2 + 2.0 * G4;
  double w2 = w0 - l2 + 2.0 * G4;
  double x3 = x0 - i3 + 3.0 * G4; // Offsets for fourth corner in (x,y,z,w) coords
  double y3 = y0 - j3 + 3.0 * G4;
  double z3 = z0 - k3 + 3.0 * G4;
  double w3 = w0 - l3 + 3.0 * G4;
  double x4 = x0 - 1.0 + 4.0 * G4; // Offsets for last corner in (x,y,z,w) coords
  double y4 = y0 - 1.0 + 4.0 * G4;
  double z4 = z0 - 1.0 + 4.0 * G4;
  double w4 = w0 - 1.0 + 4.0 * G4;

  // Work out the hashed gradient indices of the five simplex corners
  int ii = i & 255;
  int jj = j & 255;
  int kk = k & 255;
  int ll = l & 255;
  int gi0 = perlin_permutation[ii + perlin_permutation[jj + perlin_permutation[kk + perlin_permutation[ll]]]] % 32;
  int gi1 = perlin_permutation[ii + i1 + perlin_permutation[jj + j1 + perlin_permutation[kk + k1 + perlin_permutation[ll + l1]]]] % 32;
  int gi2 = perlin_permutation[ii + i2 + perlin_permutation[jj + j2 + perlin_permutation[kk + k2 + perlin_permutation[ll + l2]]]] % 32;
  int gi3 = perlin_permutation[ii + i3 + perlin_permutation[jj + j3 + perlin_permutation[kk + k3 + perlin_permutation[ll + l3]]]] % 32;
  int gi4 = perlin_permutation[ii + 1 + perlin_permutation[jj + 1 + perlin_permutation[kk + 1 + perlin_permutation[ll + 1]]]] % 32;

  // Calculate the contribution from the five corners
  double t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0 - w0 * w0;
  double n0 = 0.0;
  if (t0 < 0) {
    n0 = 0.0;
  } else {
    t0 *= t0;
    n0 = t0 * t0 * dot4d(gi0, x0, y0, z0, w0);
  }

  double t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1 - w1 * w1;
  double n1 = 0.0;
  if (t1 < 0) {
    n1 = 0.0;
  } else {
    t1 *= t1;
    n1 = t1 * t1 * dot4d(gi1, x1, y1, z1, w1);
  }

  double t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2 - w2 * w2;
  double n2 = 0.0;
  if (t2 < 0) {
    n2 = 0.0;
  } else {
    t2 *= t2;
    n2 = t2 * t2 * dot4d(gi2, x2, y2, z2, w2);
  }

  double t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3 - w3 * w3;
  double n3 = 0.0;
  if (t3 < 0) {
    n3 = 0.0;
  } else {
    t3 *= t3;
    n3 = t3 * t3 * dot4d(gi3, x3, y3, z3, w3);
  }

  double t4 = 0.6 - x4 * x4 - y4 * y4 - z4 * z4 - w4 * w4;
  double n4 = 0.0;
  if (t4 < 0) {
    n4 = 0.0;
  } else {
    t4 *= t4;
    n4 = t4 * t4 * dot4d(gi4, x4, y4, z4, w4);
  }

  // Sum up and scale the result to cover the range [-1,1]
  return 27.0 * (n0 + n1 + n2 + n3 + n4);
}

// Helper function for 3D index calculation
static inline size_t world_get_index(World *world, uint32_t x, uint32_t y, uint32_t z)
{
  return (z * world->width * world->height) + (y * world->width) + x;
}
bool world_face_dimensions(const World *world, WorldFace face, uint32_t *out_w, uint32_t *out_h)
{
  if (!world || !out_w || !out_h)
    return false;
  switch (face)
  {
  case WORLD_FACE_POS_X:
  case WORLD_FACE_NEG_X:
    *out_w = world->depth;
    *out_h = world->height;
    return true; // z (horiz) by y (vert)
  case WORLD_FACE_POS_Y:
  case WORLD_FACE_NEG_Y:
    *out_w = world->width;
    *out_h = world->depth;
    return true; // x by z
  case WORLD_FACE_POS_Z:
  case WORLD_FACE_NEG_Z:
    *out_w = world->width;
    *out_h = world->height;
    return true; // x by y (top/bottom)
  default:
    return false;
  }
}

static void type_to_rgb(VoxelType t, uint8_t *r, uint8_t *g, uint8_t *b)
{
  world_voxel_type_color(t, r, g, b);
}

bool world_face_to_rgb24(const World *world,
                         WorldFace face,
                         uint32_t slice,
                         uint8_t **out_pixels,
                         uint32_t *out_w,
                         uint32_t *out_h,
                         uint32_t *out_stride)
{
  if (!world || !out_pixels || !out_w || !out_h || !out_stride)
    return false;
  uint32_t fw = 0, fh = 0;
  if (!world_face_dimensions(world, face, &fw, &fh))
    return false;

  // Validate slice index per face
  switch (face)
  {
  case WORLD_FACE_POS_X:
  case WORLD_FACE_NEG_X:
    if (slice >= world->width)
      return false;
    break;
  case WORLD_FACE_POS_Y:
  case WORLD_FACE_NEG_Y:
    if (slice >= world->height)
      return false;
    break;
  case WORLD_FACE_POS_Z:
  case WORLD_FACE_NEG_Z:
    if (slice >= world->depth)
      return false;
    break;
  default:
    return false;
  }

  uint8_t *pixels = (uint8_t *)malloc((size_t)fw * (size_t)fh * 3);
  if (!pixels)
    return false;
  uint32_t stride = fw * 3;

  for (uint32_t v = 0; v < fh; v++)
  {
    for (uint32_t u = 0; u < fw; u++)
    {
      uint32_t x = 0, y = 0, z = 0;
      switch (face)
      {
      case WORLD_FACE_POS_X:
        x = slice;
        y = v;
        z = u;
        break;
      case WORLD_FACE_NEG_X:
        x = world->width - 1 - slice;
        y = v;
        z = u;
        break;
      case WORLD_FACE_POS_Y:
        y = slice;
        x = u;
        z = v;
        break;
      case WORLD_FACE_NEG_Y:
        y = world->height - 1 - slice;
        x = u;
        z = v;
        break;
      case WORLD_FACE_POS_Z:
        z = slice;
        x = u;
        y = v;
        break;
      case WORLD_FACE_NEG_Z:
        z = world->depth - 1 - slice;
        x = u;
        y = v;
        break;
      default:
        break;
      }
      const Voxel *vox = world_get_voxel((World *)world, x, y, z);
      uint8_t r = 10, g = 10, b = 20;
      if (vox)
        type_to_rgb(vox->type, &r, &g, &b);
      uint8_t *dst = pixels + (size_t)v * stride + (size_t)u * 3;
      dst[0] = r;
      dst[1] = g;
      dst[2] = b;
    }
  }

  *out_pixels = pixels;
  *out_w = fw;
  *out_h = fh;
  *out_stride = stride;
  return true;
}

bool world_get_slice_voxels(const World *world,
                            WorldFace face,
                            uint32_t slice,
                            VoxelCoord **out_list,
                            size_t *out_count)
{
  if (!world || !out_list || !out_count)
    return false;
  uint32_t fw = 0, fh = 0;
  if (!world_face_dimensions(world, face, &fw, &fh))
    return false;
  // Validate slice bounds as in face_to_rgb24
  switch (face)
  {
  case WORLD_FACE_POS_X:
  case WORLD_FACE_NEG_X:
    if (slice >= world->width)
      return false;
    break;
  case WORLD_FACE_POS_Y:
  case WORLD_FACE_NEG_Y:
    if (slice >= world->height)
      return false;
    break;
  case WORLD_FACE_POS_Z:
  case WORLD_FACE_NEG_Z:
    if (slice >= world->depth)
      return false;
    break;
  default:
    return false;
  }
  // Worst-case allocate and compact
  VoxelCoord *tmp = (VoxelCoord *)malloc((size_t)fw * (size_t)fh * sizeof(VoxelCoord));
  if (!tmp)
    return false;
  size_t count = 0;
  for (uint32_t v = 0; v < fh; v++)
  {
    for (uint32_t u = 0; u < fw; u++)
    {
      uint32_t x = 0, y = 0, z = 0;
      switch (face)
      {
      case WORLD_FACE_POS_X:
        x = slice;
        y = v;
        z = u;
        break;
      case WORLD_FACE_NEG_X:
        x = world->width - 1 - slice;
        y = v;
        z = u;
        break;
      case WORLD_FACE_POS_Y:
        y = slice;
        x = u;
        z = v;
        break;
      case WORLD_FACE_NEG_Y:
        y = world->height - 1 - slice;
        x = u;
        z = v;
        break;
      case WORLD_FACE_POS_Z:
        z = slice;
        x = u;
        y = v;
        break;
      case WORLD_FACE_NEG_Z:
        z = world->depth - 1 - slice;
        x = u;
        y = v;
        break;
      default:
        break;
      }
      const Voxel *vox = world_get_voxel((World *)world, x, y, z);
      if (vox && vox->type != VOXEL_AIR)
      {
        tmp[count].x = x;
        tmp[count].y = y;
        tmp[count].z = z;
        tmp[count].type = vox->type;
        count++;
      }
    }
  }
  if (count == 0)
  {
    free(tmp);
    *out_list = NULL;
    *out_count = 0;
    return true;
  }
  VoxelCoord *out = (VoxelCoord *)malloc(count * sizeof(VoxelCoord));
  if (!out)
  {
    free(tmp);
    return false;
  }
  memcpy(out, tmp, count * sizeof(VoxelCoord));
  free(tmp);
  *out_list = out;
  *out_count = count;
  return true;
}

bool world_copy_subvolume(const World *src,
                          uint32_t x0, uint32_t y0, uint32_t z0,
                          World *dst,
                          uint32_t dx0, uint32_t dy0, uint32_t dz0,
                          uint32_t width, uint32_t height, uint32_t depth)
{
  if (!src || !dst || !src->voxels || !dst->voxels)
    return false;
  if (x0 + width > src->width || y0 + height > src->height || z0 + depth > src->depth)
    return false;
  if (dx0 + width > dst->width || dy0 + height > dst->height || dz0 + depth > dst->depth)
    return false;
  for (uint32_t z = 0; z < depth; z++)
    for (uint32_t y = 0; y < height; y++)
      for (uint32_t x = 0; x < width; x++)
      {
        const Voxel *sv = world_get_voxel((World *)src, x0 + x, y0 + y, z0 + z);
        if (!sv)
          continue;
        world_set_voxel(dst, dx0 + x, dy0 + y, dz0 + z, sv->type);
      }
  return true;
}

World *world_extract_subworld(const World *src,
                              uint32_t x0, uint32_t y0, uint32_t z0,
                              uint32_t width, uint32_t height, uint32_t depth)
{
  if (!src || width == 0 || height == 0 || depth == 0)
    return NULL;
  if (x0 + width > src->width || y0 + height > src->height || z0 + depth > src->depth)
    return NULL;
  World *out = world_create(width, height, depth);
  if (!out)
    return NULL;
  // Copy voxels
  (void)world_copy_subvolume(src, x0, y0, z0, out, 0, 0, 0, width, height, depth);
  // Copy seed_id and metadata marker for provenance
  if (src->seed_id[0])
  {
    strncpy(out->seed_id, src->seed_id, sizeof(out->seed_id) - 1);
    out->seed_id[sizeof(out->seed_id) - 1] = '\0';
  }
  out->generation_type = src->generation_type;
  return out;
}

uint64_t world_content_hash(const World *world)
{
  if (!world || !world->voxels)
    return 0;
  // 64-bit FNV-1a
  uint64_t h = 1469598103934665603ULL;
  const uint64_t prime = 1099511628211ULL;
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
      {
        const Voxel *v = world_get_voxel((World *)world, x, y, z);
        if (!v)
          continue;
        uint64_t t = (uint64_t)v->type & 0xFFULL;
        uint64_t d = v->data8;
        h ^= (t | (d << 8));
        h *= prime;
      }
  return h;
}

// --------------------- Visibility and ray casting API ------------------------

static inline float fsafe_inv(float v)
{
  const float eps = 1e-6f;
  if (v > -eps && v < eps)
    return 1e30f;
  return 1.0f / v;
}

static void normalize3(float *x, float *y, float *z)
{
  float len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
  if (len <= 0.0f)
  {
    *x = 0.0f;
    *y = 0.0f;
    *z = 1.0f;
    return;
  }
  *x /= len;
  *y /= len;
  *z /= len;
}

static void cross3(float ax, float ay, float az,
                   float bx, float by, float bz,
                   float *rx, float *ry, float *rz)
{
  *rx = ay * bz - az * by;
  *ry = az * bx - ax * bz;
  *rz = ax * by - ay * bx;
}

bool world_raycast_first_hit(
    const World *w,
    float ox, float oy, float oz,
    float dx, float dy, float dz,
    int max_steps,
    int *out_x, int *out_y, int *out_z)
{
  if (!w)
    return false;

  int vx = (int)floorf(ox);
  int vy = (int)floorf(oy);
  int vz = (int)floorf(oz);

  int stepX = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
  int stepY = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);
  int stepZ = (dz > 0.0f) ? 1 : (dz < 0.0f ? -1 : 0);

  float invDx = fsafe_inv(dx);
  float invDy = fsafe_inv(dy);
  float invDz = fsafe_inv(dz);

  float nextVoxBoundaryX = (float)vx + (stepX > 0 ? 1.0f : 0.0f);
  float nextVoxBoundaryY = (float)vy + (stepY > 0 ? 1.0f : 0.0f);
  float nextVoxBoundaryZ = (float)vz + (stepZ > 0 ? 1.0f : 0.0f);

  float tMaxX = (stepX != 0) ? (nextVoxBoundaryX - ox) * invDx : 1e30f;
  float tMaxY = (stepY != 0) ? (nextVoxBoundaryY - oy) * invDy : 1e30f;
  float tMaxZ = (stepZ != 0) ? (nextVoxBoundaryZ - oz) * invDz : 1e30f;

  float tDeltaX = (float)fabs(1.0f * invDx);
  float tDeltaY = (float)fabs(1.0f * invDy);
  float tDeltaZ = (float)fabs(1.0f * invDz);

  for (int i = 0; i < max_steps; i++)
  {
    if (world_pos_in_bounds_fast(w, vx, vy, vz))
    {
      const Voxel *v = world_voxel_cptr_fast(w, vx, vy, vz);
      if (v && v->type != VOXEL_AIR)
      {
        if (out_x)
          *out_x = vx;
        if (out_y)
          *out_y = vy;
        if (out_z)
          *out_z = vz;
        return true;
      }
    }

    if (tMaxX < tMaxY)
    {
      if (tMaxX < tMaxZ)
      {
        vx += stepX;
        tMaxX += tDeltaX;
      }
      else
      {
        vz += stepZ;
        tMaxZ += tDeltaZ;
      }
    }
    else
    {
      if (tMaxY < tMaxZ)
      {
        vy += stepY;
        tMaxY += tDeltaY;
      }
      else
      {
        vz += stepZ;
        tMaxZ += tDeltaZ;
      }
    }
  }
  return false;
}

uint8_t *world_visible_voxels_for_viewport(
    const World *w,
    float cam_x, float cam_y, float cam_z,
    float dir_x, float dir_y, float dir_z,
    float up_x, float up_y, float up_z,
    const char *projection,
    int viewport_w, int viewport_h,
    int sample_stride,
    size_t *out_visible_count)
{
  if (!w || !projection || strcmp(projection, "orthographic") != 0)
    return NULL;
  if (viewport_w <= 0 || viewport_h <= 0 || sample_stride <= 0)
    return NULL;

  size_t total = (size_t)w->width * (size_t)w->height * (size_t)w->depth;
  uint8_t *mask = (uint8_t *)calloc(total, 1);
  if (!mask)
    return NULL;

  // Normalize camera basis
  normalize3(&dir_x, &dir_y, &dir_z);
  // If up is degenerate, pick a fallback
  if (fabsf(up_x) + fabsf(up_y) + fabsf(up_z) < 1e-5f)
  {
    up_x = 0;
    up_y = 0;
    up_z = 1;
  }
  normalize3(&up_x, &up_y, &up_z);
  // Right = up x dir; then recompute up = dir x right to ensure orthonormality
  float rx, ry, rz;
  cross3(up_x, up_y, up_z, dir_x, dir_y, dir_z, &rx, &ry, &rz);
  normalize3(&rx, &ry, &rz);
  float ux, uy, uz;
  cross3(dir_x, dir_y, dir_z, rx, ry, rz, &ux, &uy, &uz);
  normalize3(&ux, &uy, &uz);

  // Fast path: isometric orthographic view along approximately (1,1,-1) with up (0,0,1)
  {
    const float inv_len = 1.0f / sqrtf(3.0f);
    float tx = 1.0f * inv_len, ty = 1.0f * inv_len, tz = -1.0f * inv_len;
    float du = fabsf(dir_x - tx) + fabsf(dir_y - ty) + fabsf(dir_z - tz);
    float uu = fabsf(up_x - 0.0f) + fabsf(up_y - 0.0f) + fabsf(up_z - 1.0f);
    if (du < 0.2f && uu < 0.2f)
    {
      size_t vis_count = 0;
      for (uint32_t y = 0; y < w->height; y++)
      {
        for (uint32_t x = 0; x < w->width; x++)
        {
          int top = -1;
          for (int z = (int)w->depth - 1; z >= 0; z--)
          {
            const Voxel *v = world_get_voxel((World *)w, x, y, (uint32_t)z);
            if (v && v->type != VOXEL_AIR)
            {
              top = z;
              break;
            }
          }
          if (top >= 0)
          {
            size_t idx = ((size_t)top * (size_t)w->height + (size_t)y) * (size_t)w->width + (size_t)x;
            if (mask[idx] == 0)
            {
              mask[idx] = 1;
              vis_count++;
            }
          }
        }
      }
      if (out_visible_count)
        *out_visible_count = vis_count;
      return mask;
    }
  }

  // Choose a sampling plane centered near the world center but offset by camera pos
  float cx = (float)w->width * 0.5f;
  float cy = (float)w->height * 0.5f;
  float cz = (float)w->depth * 0.5f;
  float plane_distance = (float)(w->width + w->height + w->depth);
  float plane_cx = cx - dir_x * plane_distance + cam_x;
  float plane_cy = cy - dir_y * plane_distance + cam_y;
  float plane_cz = cz - dir_z * plane_distance + cam_z;

  float plane_extent_u = (float)(w->width + w->height);
  float plane_extent_v = (float)(w->width + w->height);
  float scale_u = (2.0f * plane_extent_u) / (float)viewport_w;
  float scale_v = (2.0f * plane_extent_v) / (float)viewport_h;

  size_t vis_count = 0;
  int max_steps = (int)(w->width + w->height + w->depth);
  if (max_steps < 64)
    max_steps = 64;

  for (int py = 0; py < viewport_h; py += sample_stride)
  {
    float vy = ((float)py - (float)viewport_h * 0.5f) * scale_v;
    for (int px = 0; px < viewport_w; px += sample_stride)
    {
      float ux_off = ((float)px - (float)viewport_w * 0.5f) * scale_u;
      float ox = plane_cx + ux_off * rx + vy * ux;
      float oy = plane_cy + ux_off * ry + vy * uy;
      float oz = plane_cz + ux_off * rz + vy * uz;

      // Step along the ray manually with a coarse step, early-out when out of bounds
      float t = 0.0f;
      const float step = 0.75f; // coarse step for speed; good enough for visibility
      for (int s = 0; s < max_steps; s++)
      {
        float rxp = ox + dir_x * t;
        float ryp = oy + dir_y * t;
        float rzp = oz + dir_z * t;
        int vx = (int)rxp, vy = (int)ryp, vz = (int)rzp;
        if (vx < 0 || vy < 0 || vz < 0 || vx >= (int)w->width || vy >= (int)w->height || vz >= (int)w->depth)
        {
          if (t > plane_distance * 2.0f)
            break;
          t += step;
          continue;
        }
        const Voxel *v = world_get_voxel((World *)w, (uint32_t)vx, (uint32_t)vy, (uint32_t)vz);
        if (v && v->type != VOXEL_AIR)
        {
          size_t idx = ((size_t)vz * (size_t)w->height + (size_t)vy) * (size_t)w->width + (size_t)vx;
          if (idx < total && mask[idx] == 0)
          {
            mask[idx] = 1;
            vis_count++;
          }
          break;
        }
        t += step;
      }
    }
  }

  if (out_visible_count)
    *out_visible_count = vis_count;
  return mask;
}

bool worlds_visible_voxels_for_viewport(
    const World *const *worlds,
    int world_count,
    float cam_x, float cam_y, float cam_z,
    float dir_x, float dir_y, float dir_z,
    float up_x, float up_y, float up_z,
    const char *projection,
    int viewport_w, int viewport_h,
    int sample_stride,
    WorldVisibilityResult *results)
{
  if (!worlds || world_count <= 0 || !results)
    return false;
  for (int i = 0; i < world_count; i++)
  {
    size_t count = 0;
    results[i].world = worlds[i];
    results[i].mask = world_visible_voxels_for_viewport(worlds[i], cam_x, cam_y, cam_z,
                                                        dir_x, dir_y, dir_z,
                                                        up_x, up_y, up_z,
                                                        projection, viewport_w, viewport_h,
                                                        sample_stride, &count);
    results[i].visible_count = count;
  }
  return true;
}

size_t world_visible_count_for_viewport(
    const World *w,
    float cam_x, float cam_y, float cam_z,
    float dir_x, float dir_y, float dir_z,
    float up_x, float up_y, float up_z,
    const char *projection,
    int viewport_w, int viewport_h,
    int sample_stride)
{
  (void)cam_x;
  (void)cam_y;
  (void)cam_z;
  (void)up_x;
  (void)up_y;
  (void)up_z;
  (void)projection;
  (void)viewport_w;
  (void)viewport_h;
  (void)sample_stride;
  if (!w)
    return 0;
  // If direction is approximately (1,1,-1), use the O(WH) top-surface count
  // Normalize incoming direction to be robust to caller scale
  normalize3(&dir_x, &dir_y, &dir_z);
  const float inv_len = 1.0f / sqrtf(3.0f);
  float tx = 1.0f * inv_len, ty = 1.0f * inv_len, tz = -1.0f * inv_len;
  float du = fabsf(dir_x - tx) + fabsf(dir_y - ty) + fabsf(dir_z - tz);
  if (du < 0.2f)
  {
    size_t vis_count = 0;
    for (uint32_t y = 0; y < w->height; y++)
    {
      for (uint32_t x = 0; x < w->width; x++)
      {
        for (int z = (int)w->depth - 1; z >= 0; z--)
        {
          const Voxel *v = world_get_voxel((World *)w, x, y, (uint32_t)z);
          if (v && v->type != VOXEL_AIR)
          {
            vis_count++;
            break;
          }
        }
      }
    }
    return vis_count;
  }
  return 0;
}

static void seed_random(const char *seed)
{
  // Stable RNG for world-local random features (trees, etc.)
  seed_state = 1;
  if (seed)
  {
    // Simple hash of the entire seed string for RNG
    const char *p = seed;
    while (*p)
    {
      seed_state = seed_state * 31u + (unsigned char)(*p++);
    }
  }

  // Do not change Perlin permutation here; it is shared globally for the universe
}

static int seeded_rand()
{
  // Simple linear congruential generator
  seed_state = seed_state * 1103515245 + 12345;
  return (unsigned int)(seed_state / 65536) % 32768;
}

static int seeded_rand_range(int max)
{
  return seeded_rand() % max;
}

// Calculate SHA256 hash of a string
static void calculate_sha256(const char *input, uint8_t output[32])
{
  sha256_context_t ctx;
  sha256_reset(&ctx);
  sha256_update(&ctx, (const uint8_t *)input, strlen(input));
  sha256_finish(&ctx, output);
}

// For hashing binary input. calculate_sha256 measures its argument with strlen, so it can only
// be given a C string; a raw digest passed to it would be read past its end and truncated at
// whatever zero byte turned up.
static void calculate_sha256_bytes(const uint8_t *input, size_t length, uint8_t output[32])
{
  sha256_context_t ctx;
  sha256_reset(&ctx);
  sha256_update(&ctx, input, length);
  sha256_finish(&ctx, output);
}

// Compare two SHA256 hashes for equality
static bool sha256_equals(const uint8_t hash1[32], const uint8_t hash2[32])
{
  return memcmp(hash1, hash2, 32) == 0;
}

// Create a new world with given dimensions
World *world_create(uint32_t width, uint32_t height, uint32_t depth)
{
  // calloc, not malloc: World carries runtime-only pointers (runtime_actors,
  // heightmap, bloom/decaying epoch maps, universe_context, condition_voxel_indices)
  // that are populated lazily and guarded by NULL checks elsewhere. Leaving them
  // indeterminate makes those guards read uninitialised memory.
  World *world = (World *)calloc(1, sizeof(World));
  if (!world)
  {
    return NULL;
  }

  world->version = 0; // Reset to Version 0 (Version 1 at launch)
  world->width = width;
  world->height = height;
  world->depth = depth;
  world->gravity = GRAVITY_DEFAULT;
  world->rarity = 0.0f;
  world->seed_id[0] = '\0';
  world->rng_state = 0;
  world->universe_depth = 0; // default origin layer

  world->log = NULL;                           // Initialize log as NULL
  world->generation_type = WORLD_TYPE_SCOURED; // Default to scoured type
  world->vector_clock = 0;                     // Initialize vector clock

  // Initialize computed world properties
  world->history_event_count = 0;
  world->unique_player_count = 0;
  world->base_level = 0; // Brand new worlds start at level 0
  world->score = 0.0;
  world->level = 0.0;

  size_t voxel_count = width * height * depth;
  world->voxels = (Voxel *)malloc(voxel_count * sizeof(Voxel));
  world->occupancy_bits = NULL; // lazily created on demand

  if (!world->voxels)
  {
    free(world);
    return NULL;
  }

  // Initialize all voxels to air with no conditions
  for (size_t i = 0; i < voxel_count; i++)
  {
    world->voxels[i].type = VOXEL_AIR;
    world->voxels[i].condition_mask = 0ULL;
    world->voxels[i].data8 = 0ULL; // clear auxiliary fields
  }

  // Every voxel was just set to air, so both of these are known rather than merely assumed.
  world->fluid_presence = WORLD_FLUID_NONE;
  world->occupied_z_min = -1;
  world->occupied_z_max = -1;

  return world;
}

// Destroy a world and free its memory
void world_destroy(World *world)
{
  if (world)
  {
    world_clear_magma_vent_columns(world);
    if (world->particle_effects)
    {
      particle_effects_destroy(world->particle_effects);
      world->particle_effects = NULL;
    }

    // Every cache derived from the voxel array has to go too. These were previously left behind on
    // the grounds that nothing ever destroyed a world mid-session; once worlds are evicted as the
    // player moves, each one abandoned here is a leak of 256KB for the bitfield, 32KB for the
    // heightmap, and up to 16MB for the epoch maps.
    if (world->occupancy_bits)
    {
      gpu_voxel_buffer_destroy(world->occupancy_bits);
      world->occupancy_bits = NULL;
    }
    if (world->fluid_sim)
    {
      fluid_sim_destroy(world->fluid_sim);
      world->fluid_sim = NULL;
    }
    free(world->heightmap);
    world->heightmap = NULL;
    free(world->bloom_epoch_map);
    world->bloom_epoch_map = NULL;
    free(world->decaying_epoch_map);
    world->decaying_epoch_map = NULL;
    for (int ci = 0; ci < 64; ci++)
    {
      free(world->condition_voxel_indices[ci].indices);
      world->condition_voxel_indices[ci].indices = NULL;
      world->condition_voxel_indices[ci].size = 0;
      world->condition_voxel_indices[ci].cap = 0;
    }

    // runtime_actors is borrowed unless runtime_actors_owned: the editor points it at a static
    // array (world->runtime_actors = g_actors) and tests point it at stack storage.
    if (world->runtime_actors_owned)
      world_clear_runtime_actors(world);

    free(world->voxels);
    free(world->log);
    free(world);
  }
}

// Check if a position is within the world bounds
bool world_is_position_valid(World *world, uint32_t x, uint32_t y, uint32_t z)
{
  return world &&
         x < world->width &&
         y < world->height &&
         z < world->depth;
}

// Get a voxel at a specific position
Voxel *world_get_voxel(World *world, uint32_t x, uint32_t y, uint32_t z)
{
  if (!world_is_position_valid(world, x, y, z))
  {
    return NULL;
  }

  size_t index = world_get_index(world, x, y, z);
  return &world->voxels[index];
}

// Helper function to extract RGB values from hex color constant
static void extract_rgb_from_hex(uint32_t hex_color, uint8_t *r, uint8_t *g, uint8_t *b)
{
  *r = (uint8_t)((hex_color >> 16) & 0xFF);
  *g = (uint8_t)((hex_color >> 8) & 0xFF);
  *b = (uint8_t)(hex_color & 0xFF);
}

void world_voxel_type_color(VoxelType t, uint8_t *r, uint8_t *g, uint8_t *b)
{
  uint8_t rr = 64, gg = 64, bb = 64;
  switch (t)
  {
  case VOXEL_AIR:
    extract_rgb_from_hex(VOXEL_COLOR_AIR, &rr, &gg, &bb);
    break;
  case VOXEL_BEDROCK:
    extract_rgb_from_hex(VOXEL_COLOR_BEDROCK, &rr, &gg, &bb);
    break;
  case VOXEL_ACTOR:
    extract_rgb_from_hex(VOXEL_COLOR_ACTOR, &rr, &gg, &bb);
    break;
  case VOXEL_STONE:
    extract_rgb_from_hex(VOXEL_COLOR_STONE, &rr, &gg, &bb);
    break;
  case VOXEL_STONE_BASALT:
    extract_rgb_from_hex(VOXEL_COLOR_STONE_BASALT, &rr, &gg, &bb);
    break;
  case VOXEL_STONE_GRANITE:
    extract_rgb_from_hex(VOXEL_COLOR_STONE_GRANITE, &rr, &gg, &bb);
    break;
  case VOXEL_STONE_LIMESTONE:
    extract_rgb_from_hex(VOXEL_COLOR_STONE_LIMESTONE, &rr, &gg, &bb);
    break;
  case VOXEL_STONE_SANDSTONE:
    extract_rgb_from_hex(VOXEL_COLOR_STONE_SANDSTONE, &rr, &gg, &bb);
    break;
  case VOXEL_GRAVEL:
    extract_rgb_from_hex(VOXEL_COLOR_GRAVEL, &rr, &gg, &bb);
    break;
  case VOXEL_GRAVEL_BASALT:
    extract_rgb_from_hex(VOXEL_COLOR_GRAVEL_BASALT, &rr, &gg, &bb);
    break;
  case VOXEL_GRAVEL_GRANITE:
    extract_rgb_from_hex(VOXEL_COLOR_GRAVEL_GRANITE, &rr, &gg, &bb);
    break;
  case VOXEL_GRAVEL_LIMESTONE:
    extract_rgb_from_hex(VOXEL_COLOR_GRAVEL_LIMESTONE, &rr, &gg, &bb);
    break;
  case VOXEL_GRAVEL_SANDSTONE:
    extract_rgb_from_hex(VOXEL_COLOR_GRAVEL_SANDSTONE, &rr, &gg, &bb);
    break;
  case VOXEL_SAND:
    extract_rgb_from_hex(VOXEL_COLOR_SAND, &rr, &gg, &bb);
    break;
  case VOXEL_SAND_BASALT:
    extract_rgb_from_hex(VOXEL_COLOR_SAND_BASALT, &rr, &gg, &bb);
    break;
  case VOXEL_SAND_GRANITE:
    extract_rgb_from_hex(VOXEL_COLOR_SAND_GRANITE, &rr, &gg, &bb);
    break;
  case VOXEL_SAND_LIMESTONE:
    extract_rgb_from_hex(VOXEL_COLOR_SAND_LIMESTONE, &rr, &gg, &bb);
    break;
  case VOXEL_SAND_SANDSTONE:
    extract_rgb_from_hex(VOXEL_COLOR_SAND_SANDSTONE, &rr, &gg, &bb);
    break;
  case VOXEL_SOIL:
    extract_rgb_from_hex(VOXEL_COLOR_SOIL, &rr, &gg, &bb);
    break;
  case VOXEL_SOIL_CLAY:
    extract_rgb_from_hex(VOXEL_COLOR_SOIL_CLAY, &rr, &gg, &bb);
    break;
  case VOXEL_SOIL_LOAM:
    extract_rgb_from_hex(VOXEL_COLOR_SOIL_LOAM, &rr, &gg, &bb);
    break;
  case VOXEL_SOIL_SILT:
    extract_rgb_from_hex(VOXEL_COLOR_SOIL_SILT, &rr, &gg, &bb);
    break;
  case VOXEL_GRASS:
    extract_rgb_from_hex(VOXEL_COLOR_GRASS, &rr, &gg, &bb);
    break;
  case VOXEL_GRASS_WIDE:
    extract_rgb_from_hex(VOXEL_COLOR_GRASS_WIDE, &rr, &gg, &bb);
    break;
  case VOXEL_GRASS_SHARP:
    extract_rgb_from_hex(VOXEL_COLOR_GRASS_SHARP, &rr, &gg, &bb);
    break;
  case VOXEL_GRASS_CLOVER:
    extract_rgb_from_hex(VOXEL_COLOR_GRASS_CLOVER, &rr, &gg, &bb);
    break;
  case VOXEL_GRASS_MOSS:
    extract_rgb_from_hex(VOXEL_COLOR_GRASS_MOSS, &rr, &gg, &bb);
    break;
  case VOXEL_GRASS_TALL:
    extract_rgb_from_hex(VOXEL_COLOR_GRASS_TALL, &rr, &gg, &bb);
    break;
  case VOXEL_CANDLE:
    extract_rgb_from_hex(VOXEL_COLOR_CANDLE, &rr, &gg, &bb);
    break;
  case VOXEL_CAMPFIRE:
    extract_rgb_from_hex(VOXEL_COLOR_CAMPFIRE, &rr, &gg, &bb);
    break;
  case VOXEL_BUSH:
    extract_rgb_from_hex(VOXEL_COLOR_BUSH, &rr, &gg, &bb);
    break;
  case VOXEL_BUSH_FERN:
    extract_rgb_from_hex(VOXEL_COLOR_BUSH_FERN, &rr, &gg, &bb);
    break;
  case VOXEL_BUSH_VINES:
    extract_rgb_from_hex(VOXEL_COLOR_BUSH_VINES, &rr, &gg, &bb);
    break;
  case VOXEL_BUSH_THORNS:
    extract_rgb_from_hex(VOXEL_COLOR_BUSH_THORNS, &rr, &gg, &bb);
    break;
  case VOXEL_BUSH_BLUEBERRY:
    extract_rgb_from_hex(VOXEL_COLOR_BUSH_BLUEBERRY, &rr, &gg, &bb);
    break;
  case VOXEL_BUSH_BLACKBERRY:
    extract_rgb_from_hex(VOXEL_COLOR_BUSH_BLACKBERRY, &rr, &gg, &bb);
    break;
  case VOXEL_BUSH_RASPBERRY:
    extract_rgb_from_hex(VOXEL_COLOR_BUSH_RASPBERRY, &rr, &gg, &bb);
    break;
  case VOXEL_BUSH_STRAWBERRY:
    extract_rgb_from_hex(VOXEL_COLOR_BUSH_STRAWBERRY, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_OAK:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_OAK, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_BEECH:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_BEECH, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_BIRCH:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_BIRCH, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_PINE:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_PINE, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_PECAN:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_PECAN, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_LOCUST:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_LOCUST, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_MAPLE:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_MAPLE, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_ELM:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_ELM, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_HAZELNUT:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_HAZELNUT, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_CHESTNUT:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_CHESTNUT, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_WILLOW:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_WILLOW, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_WALNUT:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_WALNUT, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_ACACIA:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_ACACIA, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_COTTONWOOD:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_COTTONWOOD, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_CYPRESS:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_CYPRESS, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_SPRUCE:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_SPRUCE, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_JUNIPER:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_JUNIPER, &rr, &gg, &bb);
    break;
  case VOXEL_WOOD_REDWOOD:
    extract_rgb_from_hex(VOXEL_COLOR_WOOD_REDWOOD, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_OAK:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_OAK, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_BEECH:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_BEECH, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_BIRCH:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_BIRCH, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_PINE:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_PINE, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_PECAN:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_PECAN, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_LOCUST:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_LOCUST, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_MAPLE:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_MAPLE, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_ELM:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_ELM, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_HAZELNUT:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_HAZELNUT, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_CHESTNUT:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_CHESTNUT, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_WILLOW:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_WILLOW, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_WALNUT:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_WALNUT, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_ACACIA:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_ACACIA, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_COTTONWOOD:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_COTTONWOOD, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_CYPRESS:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_CYPRESS, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_SPRUCE:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_SPRUCE, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_JUNIPER:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_JUNIPER, &rr, &gg, &bb);
    break;
  case VOXEL_LEAVES_REDWOOD:
    extract_rgb_from_hex(VOXEL_COLOR_LEAVES_REDWOOD, &rr, &gg, &bb);
    break;
  case VOXEL_ORE:
    extract_rgb_from_hex(VOXEL_COLOR_ORE, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_COAL:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_COAL, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_ADAMANTITE:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_ADAMANTITE, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_HEMATITE:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_HEMATITE, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_MITHRIL:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_MITHRIL, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_COPPER:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_COPPER, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_SILVER:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_SILVER, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_GOLD:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_GOLD, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_TIN:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_TIN, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_IRON:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_IRON, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_LEAD:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_LEAD, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_ZINC:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_ZINC, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_TITANIUM:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_TITANIUM, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_ALUMINUM:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_ALUMINUM, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_MAGNESIUM:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_MAGNESIUM, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_COBALT:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_COBALT, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_NICKEL:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_NICKEL, &rr, &gg, &bb);
    break;
  case VOXEL_ORE_PLATINUM:
    extract_rgb_from_hex(VOXEL_COLOR_ORE_PLATINUM, &rr, &gg, &bb);
    break;
  case VOXEL_ADAMANTITE:
    extract_rgb_from_hex(VOXEL_COLOR_ADAMANTITE, &rr, &gg, &bb);
    break;
  case VOXEL_HEMATITE:
    extract_rgb_from_hex(VOXEL_COLOR_HEMATITE, &rr, &gg, &bb);
    break;
  case VOXEL_MITHRIL:
    extract_rgb_from_hex(VOXEL_COLOR_MITHRIL, &rr, &gg, &bb);
    break;
  case VOXEL_COPPER:
    extract_rgb_from_hex(VOXEL_COLOR_COPPER, &rr, &gg, &bb);
    break;
  case VOXEL_SILVER:
    extract_rgb_from_hex(VOXEL_COLOR_SILVER, &rr, &gg, &bb);
    break;
  case VOXEL_GOLD:
    extract_rgb_from_hex(VOXEL_COLOR_GOLD, &rr, &gg, &bb);
    break;
  case VOXEL_TIN:
    extract_rgb_from_hex(VOXEL_COLOR_TIN, &rr, &gg, &bb);
    break;
  case VOXEL_IRON:
    extract_rgb_from_hex(VOXEL_COLOR_IRON, &rr, &gg, &bb);
    break;
  case VOXEL_LEAD:
    extract_rgb_from_hex(VOXEL_COLOR_LEAD, &rr, &gg, &bb);
    break;
  case VOXEL_ZINC:
    extract_rgb_from_hex(VOXEL_COLOR_ZINC, &rr, &gg, &bb);
    break;
  case VOXEL_STEEL:
    extract_rgb_from_hex(VOXEL_COLOR_STEEL, &rr, &gg, &bb);
    break;
  case VOXEL_TITANIUM:
    extract_rgb_from_hex(VOXEL_COLOR_TITANIUM, &rr, &gg, &bb);
    break;
  case VOXEL_ALUMINUM:
    extract_rgb_from_hex(VOXEL_COLOR_ALUMINUM, &rr, &gg, &bb);
    break;
  case VOXEL_MAGNESIUM:
    extract_rgb_from_hex(VOXEL_COLOR_MAGNESIUM, &rr, &gg, &bb);
    break;
  case VOXEL_COBALT:
    extract_rgb_from_hex(VOXEL_COLOR_COBALT, &rr, &gg, &bb);
    break;
  case VOXEL_NICKEL:
    extract_rgb_from_hex(VOXEL_COLOR_NICKEL, &rr, &gg, &bb);
    break;
  case VOXEL_PLATINUM:
    extract_rgb_from_hex(VOXEL_COLOR_PLATINUM, &rr, &gg, &bb);
    break;
  case VOXEL_WATER:
    extract_rgb_from_hex(VOXEL_COLOR_WATER, &rr, &gg, &bb);
    break;
  case VOXEL_MAGMA:
    extract_rgb_from_hex(VOXEL_COLOR_MAGMA, &rr, &gg, &bb);
    break;
  case VOXEL_STEAM:
    extract_rgb_from_hex(VOXEL_COLOR_STEAM, &rr, &gg, &bb);
    break;
  case VOXEL_OIL:
    extract_rgb_from_hex(VOXEL_COLOR_OIL, &rr, &gg, &bb);
    break;
  case VOXEL_GAS:
    extract_rgb_from_hex(VOXEL_COLOR_GAS, &rr, &gg, &bb);
    break;
  case VOXEL_SPRING:
    extract_rgb_from_hex(VOXEL_COLOR_SPRING, &rr, &gg, &bb);
    break;
  case VOXEL_SPRING_WATER:
    extract_rgb_from_hex(VOXEL_COLOR_SPRING_WATER, &rr, &gg, &bb);
    break;
  case VOXEL_SPRING_MAGMA:
    extract_rgb_from_hex(VOXEL_COLOR_SPRING_MAGMA, &rr, &gg, &bb);
    break;
  case VOXEL_SPRING_STEAM:
    extract_rgb_from_hex(VOXEL_COLOR_SPRING_STEAM, &rr, &gg, &bb);
    break;
  case VOXEL_SPRING_OIL:
    extract_rgb_from_hex(VOXEL_COLOR_SPRING_OIL, &rr, &gg, &bb);
    break;
  case VOXEL_SPRING_GAS:
    extract_rgb_from_hex(VOXEL_COLOR_SPRING_GAS, &rr, &gg, &bb);
    break;
  case VOXEL_CRYSTAL:
    extract_rgb_from_hex(VOXEL_COLOR_CRYSTAL, &rr, &gg, &bb);
    break;
  case VOXEL_CRYSTAL_RED:
    extract_rgb_from_hex(VOXEL_COLOR_CRYSTAL_RED, &rr, &gg, &bb);
    break;
  case VOXEL_CRYSTAL_GREEN:
    extract_rgb_from_hex(VOXEL_COLOR_CRYSTAL_GREEN, &rr, &gg, &bb);
    break;
  case VOXEL_CRYSTAL_BLUE:
    extract_rgb_from_hex(VOXEL_COLOR_CRYSTAL_BLUE, &rr, &gg, &bb);
    break;
  case VOXEL_GLASS:
    extract_rgb_from_hex(VOXEL_COLOR_GLASS, &rr, &gg, &bb);
    break;
  case VOXEL_BRICK:
    extract_rgb_from_hex(VOXEL_COLOR_BRICK, &rr, &gg, &bb);
    break;
  case VOXEL_OBSIDIAN:
    extract_rgb_from_hex(VOXEL_COLOR_OBSIDIAN, &rr, &gg, &bb);
    break;
  case VOXEL_CLAY:
    extract_rgb_from_hex(VOXEL_COLOR_CLAY, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL, &rr, &gg, &bb);
    break;
  case VOXEL_SNOW:
    extract_rgb_from_hex(VOXEL_COLOR_SNOW, &rr, &gg, &bb);
    break;
  case VOXEL_ICE:
    extract_rgb_from_hex(VOXEL_COLOR_ICE, &rr, &gg, &bb);
    break;
  case VOXEL_PLASTIC:
    extract_rgb_from_hex(VOXEL_COLOR_PLASTIC, &rr, &gg, &bb);
    break;
  case VOXEL_CLOTH:
    extract_rgb_from_hex(VOXEL_COLOR_CLOTH, &rr, &gg, &bb);
    break;
  case VOXEL_PLANK:
    extract_rgb_from_hex(VOXEL_COLOR_PLANK, &rr, &gg, &bb);
    break;
  case VOXEL_THATCH:
    extract_rgb_from_hex(VOXEL_COLOR_THATCH, &rr, &gg, &bb);
    break;
  case VOXEL_STRAW:
    extract_rgb_from_hex(VOXEL_COLOR_STRAW, &rr, &gg, &bb);
    break;
  case VOXEL_COBBLE:
    extract_rgb_from_hex(VOXEL_COLOR_COBBLE, &rr, &gg, &bb);
    break;
  case VOXEL_PLASTER:
    extract_rgb_from_hex(VOXEL_COLOR_PLASTER, &rr, &gg, &bb);
    break;
  case VOXEL_TERRACOTTA:
    extract_rgb_from_hex(VOXEL_COLOR_TERRACOTTA, &rr, &gg, &bb);
    break;
  case VOXEL_ADOBE:
    extract_rgb_from_hex(VOXEL_COLOR_ADOBE, &rr, &gg, &bb);
    break;
  case VOXEL_GLASS_WHITE:
    extract_rgb_from_hex(VOXEL_COLOR_GLASS_WHITE, &rr, &gg, &bb);
    break;
  case VOXEL_GLASS_RED:
    extract_rgb_from_hex(VOXEL_COLOR_GLASS_RED, &rr, &gg, &bb);
    break;
  case VOXEL_GLASS_GREEN:
    extract_rgb_from_hex(VOXEL_COLOR_GLASS_GREEN, &rr, &gg, &bb);
    break;
  case VOXEL_GLASS_BLUE:
    extract_rgb_from_hex(VOXEL_COLOR_GLASS_BLUE, &rr, &gg, &bb);
    break;
  case VOXEL_GLASS_YELLOW:
    extract_rgb_from_hex(VOXEL_COLOR_GLASS_YELLOW, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL_WHITE:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL_WHITE, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL_BLACK:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL_BLACK, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL_BROWN:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL_BROWN, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL_GRAY:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL_GRAY, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL_RED:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL_RED, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL_BLUE:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL_BLUE, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL_GREEN:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL_GREEN, &rr, &gg, &bb);
    break;
  case VOXEL_WOOL_YELLOW:
    extract_rgb_from_hex(VOXEL_COLOR_WOOL_YELLOW, &rr, &gg, &bb);
    break;
  case VOXEL_LEATHER:
    extract_rgb_from_hex(VOXEL_COLOR_LEATHER, &rr, &gg, &bb);
    break;
  case VOXEL_FUR:
    extract_rgb_from_hex(VOXEL_COLOR_FUR, &rr, &gg, &bb);
    break;
  case VOXEL_FEATHER:
    extract_rgb_from_hex(VOXEL_COLOR_FEATHER, &rr, &gg, &bb);
    break;
  case VOXEL_SCALE:
    extract_rgb_from_hex(VOXEL_COLOR_SCALE, &rr, &gg, &bb);
    break;
  case VOXEL_SHELL:
    extract_rgb_from_hex(VOXEL_COLOR_SHELL, &rr, &gg, &bb);
    break;
  case VOXEL_HORN:
    extract_rgb_from_hex(VOXEL_COLOR_HORN, &rr, &gg, &bb);
    break;
  case VOXEL_PAPER:
    extract_rgb_from_hex(VOXEL_COLOR_PAPER, &rr, &gg, &bb);
    break;
  case VOXEL_ROPE:
    extract_rgb_from_hex(VOXEL_COLOR_ROPE, &rr, &gg, &bb);
    break;
  case VOXEL_CERAMIC:
    extract_rgb_from_hex(VOXEL_COLOR_CERAMIC, &rr, &gg, &bb);
    break;
  case VOXEL_RUBBER:
    extract_rgb_from_hex(VOXEL_COLOR_RUBBER, &rr, &gg, &bb);
    break;
  case VOXEL_WAX:
    extract_rgb_from_hex(VOXEL_COLOR_WAX, &rr, &gg, &bb);
    break;
  case VOXEL_ASH:
    extract_rgb_from_hex(VOXEL_COLOR_ASH, &rr, &gg, &bb);
    break;
  case VOXEL_DOOR:
    extract_rgb_from_hex(VOXEL_COLOR_DOOR, &rr, &gg, &bb);
    break;
  case VOXEL_ROOF_TILE:
    extract_rgb_from_hex(VOXEL_COLOR_ROOF_TILE, &rr, &gg, &bb);
    break;
  case VOXEL_CRATE:
    extract_rgb_from_hex(VOXEL_COLOR_CRATE, &rr, &gg, &bb);
    break;
  case VOXEL_BARREL:
    extract_rgb_from_hex(VOXEL_COLOR_BARREL, &rr, &gg, &bb);
    break;
  case VOXEL_BED:
    extract_rgb_from_hex(VOXEL_COLOR_BED, &rr, &gg, &bb);
    break;
  case VOXEL_DOOR_NS:
    extract_rgb_from_hex(VOXEL_COLOR_DOOR_NS, &rr, &gg, &bb);
    break;
  case VOXEL_THATCH_MIRROR:
    extract_rgb_from_hex(VOXEL_COLOR_THATCH_MIRROR, &rr, &gg, &bb);
    break;
  case VOXEL_ROOF_TILE_MIRROR:
    extract_rgb_from_hex(VOXEL_COLOR_ROOF_TILE_MIRROR, &rr, &gg, &bb);
    break;
  case VOXEL_GLASS_NS:
    extract_rgb_from_hex(VOXEL_COLOR_GLASS_NS, &rr, &gg, &bb);
    break;
  case VOXEL_STAIR:
    extract_rgb_from_hex(VOXEL_COLOR_STAIR, &rr, &gg, &bb);
    break;
  case VOXEL_STAIR_NS:
    extract_rgb_from_hex(VOXEL_COLOR_STAIR_NS, &rr, &gg, &bb);
    break;
  case VOXEL_CHAIR:
    extract_rgb_from_hex(VOXEL_COLOR_CHAIR, &rr, &gg, &bb);
    break;
  case VOXEL_TABLE:
    extract_rgb_from_hex(VOXEL_COLOR_TABLE, &rr, &gg, &bb);
    break;
  case VOXEL_CHEST:
    extract_rgb_from_hex(VOXEL_COLOR_CHEST, &rr, &gg, &bb);
    break;
  case VOXEL_FENCE:
    extract_rgb_from_hex(VOXEL_COLOR_FENCE, &rr, &gg, &bb);
    break;
  case VOXEL_FENCE_NS:
    extract_rgb_from_hex(VOXEL_COLOR_FENCE_NS, &rr, &gg, &bb);
    break;
  case VOXEL_FENCE_WATTLE:
    extract_rgb_from_hex(VOXEL_COLOR_FENCE_WATTLE, &rr, &gg, &bb);
    break;
  case VOXEL_FENCE_IRON:
    extract_rgb_from_hex(VOXEL_COLOR_FENCE_IRON, &rr, &gg, &bb);
    break;
  case VOXEL_RAMPART:
    extract_rgb_from_hex(VOXEL_COLOR_RAMPART, &rr, &gg, &bb);
    break;
  case VOXEL_PARAPET:
    extract_rgb_from_hex(VOXEL_COLOR_PARAPET, &rr, &gg, &bb);
    break;
  case VOXEL_CRAFTING_TABLE:
    extract_rgb_from_hex(VOXEL_COLOR_CRAFTING_TABLE, &rr, &gg, &bb);
    break;
  case VOXEL_ANVIL:
    extract_rgb_from_hex(VOXEL_COLOR_ANVIL, &rr, &gg, &bb);
    break;
  case VOXEL_FORGE:
    extract_rgb_from_hex(VOXEL_COLOR_FORGE, &rr, &gg, &bb);
    break;

  case VOXEL_BONE:
    extract_rgb_from_hex(VOXEL_COLOR_BONE, &rr, &gg, &bb);
    break;
  case VOXEL_FLESH:
    extract_rgb_from_hex(VOXEL_COLOR_FLESH, &rr, &gg, &bb);
    break;
  case VOXEL_ORGAN:
    extract_rgb_from_hex(VOXEL_COLOR_ORGAN, &rr, &gg, &bb);
    break;
  case VOXEL_BLOOD:
    extract_rgb_from_hex(VOXEL_COLOR_BLOOD, &rr, &gg, &bb);
    break;
  case VOXEL_BRAIN:
    extract_rgb_from_hex(VOXEL_COLOR_BRAIN, &rr, &gg, &bb);
    break;
  case VOXEL_FUNGUS:
    extract_rgb_from_hex(VOXEL_COLOR_FUNGUS, &rr, &gg, &bb);
    break;
  case VOXEL_WORLD:
    extract_rgb_from_hex(VOXEL_COLOR_WORLD, &rr, &gg, &bb);
    break;
  default:
    rr = 64;
    gg = 64;
    bb = 64;
    break;
  }
  if (r)
    *r = rr;
  if (g)
    *g = gg;
  if (b)
    *b = bb;
}

const char *world_voxel_type_name(VoxelType t)
{
  // Delegate to the canonical voxel_type_name function from voxel.h
  return voxel_type_name(t);
}

// Set a voxel at a specific position
bool world_voxel_type_is_fluidlike(VoxelType type)
{
  switch (type)
  {
  case VOXEL_WATER:
  case VOXEL_MAGMA:
  case VOXEL_STEAM:
  case VOXEL_OIL:
  case VOXEL_GAS:
  // Springs are solid but produce fluid, so a world holding one is never fluid-free.
  case VOXEL_SPRING:
  case VOXEL_SPRING_WATER:
  case VOXEL_SPRING_MAGMA:
  case VOXEL_SPRING_STEAM:
  case VOXEL_SPRING_OIL:
  case VOXEL_SPRING_GAS:
    return true;
  default:
    return false;
  }
}

bool world_voxel_type_is_foliage(VoxelType type)
{
  switch (type)
  {
  // A canopy is mostly gaps. All nineteen species are listed rather than matched by range, because
  // the enum is append-only for save compatibility, so a new species will not necessarily land
  // inside the block its relatives occupy.
  case VOXEL_LEAVES:
  case VOXEL_LEAVES_OAK:
  case VOXEL_LEAVES_BEECH:
  case VOXEL_LEAVES_BIRCH:
  case VOXEL_LEAVES_PINE:
  case VOXEL_LEAVES_PECAN:
  case VOXEL_LEAVES_LOCUST:
  case VOXEL_LEAVES_MAPLE:
  case VOXEL_LEAVES_ELM:
  case VOXEL_LEAVES_HAZELNUT:
  case VOXEL_LEAVES_CHESTNUT:
  case VOXEL_LEAVES_WILLOW:
  case VOXEL_LEAVES_WALNUT:
  case VOXEL_LEAVES_ACACIA:
  case VOXEL_LEAVES_COTTONWOOD:
  case VOXEL_LEAVES_CYPRESS:
  case VOXEL_LEAVES_SPRUCE:
  case VOXEL_LEAVES_JUNIPER:
  case VOXEL_LEAVES_REDWOOD:
  case VOXEL_GRASS_TALL:
    return true;
  default:
    return false;
  }
}

bool world_voxel_type_has_material_gaps(VoxelType type)
{
  if (world_voxel_type_is_foliage(type))
    return true;
  // Bushes keep collision (not foliage) but bake like a dense canopy so ground and sky read
  // through the shrub silhouette instead of a solid green cube.
  if (voxel_type_is_bush(type))
    return true;
  switch (type)
  {
  case VOXEL_THATCH:
  case VOXEL_THATCH_MIRROR:
  case VOXEL_ROOF_TILE:
  case VOXEL_ROOF_TILE_MIRROR:
  case VOXEL_DOOR:
  case VOXEL_DOOR_NS:
  case VOXEL_GLASS:
  case VOXEL_GLASS_NS:
  case VOXEL_GLASS_WHITE:
  case VOXEL_GLASS_RED:
  case VOXEL_GLASS_GREEN:
  case VOXEL_GLASS_BLUE:
  case VOXEL_GLASS_YELLOW:
  case VOXEL_CRATE:
  case VOXEL_BARREL:
  case VOXEL_BED:
  case VOXEL_STAIR:
  case VOXEL_STAIR_NS:
  case VOXEL_CHAIR:
  case VOXEL_TABLE:
  case VOXEL_CHEST:
  case VOXEL_FENCE:
  case VOXEL_FENCE_NS:
  case VOXEL_FENCE_WATTLE:
  case VOXEL_FENCE_IRON:
  case VOXEL_CRAFTING_TABLE:
  case VOXEL_ANVIL:
  case VOXEL_FORGE:
  case VOXEL_PARAPET:
  case VOXEL_FUNGUS:
  case VOXEL_FEATHER:
    return true;
  default:
    return false;
  }
}

bool world_voxel_needs_nested_silhouette(const Voxel *voxel)
{
  if (!voxel || voxel->type == VOXEL_AIR)
    return false;
  if (voxel_has_nontrivial_shape(voxel))
    return true;
  return world_voxel_type_has_material_gaps(voxel->type);
}

bool world_voxel_omits_parent_aabb(const Voxel *voxel)
{
  if (!world_voxel_needs_nested_silhouette(voxel))
    return false;
  // Shaped cells always need the nested mesh — a stair wedge is not a cube.
  if (voxel_has_nontrivial_shape(voxel))
    return true;
  const VoxelType t = voxel->type;
  // Dense vegetation stays in the parent mesh (see world.h). Fungus/feather are the same class of
  // soft prop: rare, but punching them for nested instances has the same hole-through-to-neighbour
  // failure mode when the instance is dropped by the budget.
  if (world_voxel_type_is_foliage(t) || voxel_type_is_bush(t) || t == VOXEL_FUNGUS ||
      t == VOXEL_FEATHER)
    return false;
  return true;
}

bool world_voxel_type_blocks_movement(VoxelType type)
{
  if (type == VOXEL_AIR)
    return false;

  // Foliage is a thing you push through, not a wall. It stays marked occupied in the bitfield so
  // tools that only read the bitfield still see a canopy cell, but render face-culling must treat
  // it as transparent (see voxel_is_transparent_type) or the ground under a tuft is never drawn and
  // the bake gaps show sky instead of grass.
  if (world_voxel_type_is_foliage(type))
    return false;

  switch (type)
  {
  // Fluids and vapours. world_check_collision has always treated these as passable, while the
  // player's own collision refused anything that was not air, so walking into a pond stopped you
  // dead at the shoreline.
  case VOXEL_WATER:
  case VOXEL_STEAM:
  case VOXEL_GAS:
  case VOXEL_OIL:
  case VOXEL_DOOR: // hung door panel — walk through until open/close interaction exists
  case VOXEL_DOOR_NS:
    return false;
  // Magma deliberately still blocks. There is no contact damage yet, so making it passable would
  // let the player wade through lava with no consequence at all, which reads more like a bug than
  // the shoreline problem this fixes.
  default:
    return true;
  }
}

void world_invalidate_fluid_presence(World *world)
{
  if (!world)
    return;
  world->fluid_presence = WORLD_FLUID_UNKNOWN;
  // Callers reach here after writing voxels without saying which, so the simulation's queue of
  // cells to visit cannot be trusted either: it could be missing fluid that was just placed.
  fluid_sim_invalidate(world);
}

WorldFluidPresence world_fluid_presence(World *world)
{
  if (!world || !world->voxels)
    return WORLD_FLUID_NONE;
  if (world->fluid_presence != WORLD_FLUID_UNKNOWN)
    return world->fluid_presence;

  // Linear over the voxel array rather than by (x,y,z), and reading only the type, because this
  // is bandwidth-bound. Stops at the first hit, so a world with fluid settles this immediately
  // and only a fluid-free world pays for the whole pass.
  const size_t total = (size_t)world->width * world->height * world->depth;
  const Voxel *voxels = world->voxels;
  for (size_t i = 0; i < total; i++)
  {
    if (world_voxel_type_is_fluidlike(voxels[i].type))
    {
      world->fluid_presence = WORLD_FLUID_SOME;
      return WORLD_FLUID_SOME;
    }
  }

  world->fluid_presence = WORLD_FLUID_NONE;
  return WORLD_FLUID_NONE;
}

bool world_set_voxel(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType type)
{
  if (!world_is_position_valid(world, x, y, z))
  {
    return false;
  }

  // Allow VOXEL_WORLD (255) as a special case, but validate other types
  if (type != VOXEL_WORLD && type >= VOXEL_COUNT)
  {
    return false;
  }

  size_t index = world_get_index(world, x, y, z);
  VoxelType prev = world->voxels[index].type;
  world->voxels[index].type = type;

  if (prev != type)
  {
    world->voxel_revision++;
    world_sync_heightmap(world, (int)x, (int)y, (int)z, type != VOXEL_AIR);
  }

  // Widen the occupied band to cover a newly placed solid. Removing one deliberately does not
  // narrow it: that would need a scan of the layer, and naming an empty layer as occupied only
  // costs the renderer a wasted pass, whereas missing an occupied one would drop terrain.
  if (type != VOXEL_AIR)
  {
    if (world->occupied_z_min < 0 || (int)z < world->occupied_z_min)
      world->occupied_z_min = (int)z;
    if ((int)z > world->occupied_z_max)
      world->occupied_z_max = (int)z;
  }

  // Fluid presence is kept conservative here: placing fluid is known to make the answer SOME,
  // but removing a fluid voxel cannot prove it was the last one, so that drops back to UNKNOWN
  // and lets the next census settle it.
  if (world_voxel_type_is_fluidlike(type))
    world->fluid_presence = WORLD_FLUID_SOME;
  else if (world_voxel_type_is_fluidlike(prev))
    world->fluid_presence = WORLD_FLUID_UNKNOWN;

  // Incremental occupancy bit update if buffer exists
  if (world->occupancy_bits && world->occupancy_bits->bits &&
      world->occupancy_bits->width == world->width &&
      world->occupancy_bits->height == world->height &&
      world->occupancy_bits->depth == world->depth)
  {
    uint32_t lin = ((uint32_t)z * world->height + (uint32_t)y) * world->width + (uint32_t)x;
    uint8_t *bits = world->occupancy_bits->bits;
    if (gpu_voxel_buffer_type_marks_occupancy(type))
      bits[lin >> 3u] |= (uint8_t)(1u << (lin & 7u));
    else
      bits[lin >> 3u] &= (uint8_t)~(1u << (lin & 7u));
    (void)prev;
  }
  // Placing fluid by type alone means "a cube of it", so the level field is set to brim-full.
  // Not to the field's maximum: levels above full are pressure, and a cube placed in open air
  // carrying the pressure of a deep column would immediately spit water upwards.
  if (type == VOXEL_WATER || type == VOXEL_MAGMA)
    voxel_set_quantity(&world->voxels[index], FLUID_LEVEL_FULL);

  // Tell the simulation something changed here. Any type change can matter, not just a fluid one:
  // breaking the floor of a pond gives its water somewhere new to go. Worlds with no fluid have no
  // simulation state to keep, so they are not given any by this.
  if (prev != type && (world->fluid_sim || world_voxel_type_is_fluidlike(type) ||
                       world_voxel_type_is_fluidlike(prev)))
    fluid_sim_touch(world, (int)x, (int)y, (int)z);

  return true;
}

bool world_autocrop(World *world)
{
  if (!world || !world->voxels)
    return false;
  uint32_t minx = world->width, miny = world->height, minz = world->depth;
  int32_t maxx = -1, maxy = -1, maxz = -1;
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
      {
        Voxel *v = world_get_voxel(world, x, y, z);
        if (!v || v->type == VOXEL_AIR)
          continue;
        if (x < minx)
          minx = x;
        if ((int32_t)x > maxx)
          maxx = (int32_t)x;
        if (y < miny)
          miny = y;
        if ((int32_t)y > maxy)
          maxy = (int32_t)y;
        if (z < minz)
          minz = z;
        if ((int32_t)z > maxz)
          maxz = (int32_t)z;
      }
  if (maxx < 0 || maxy < 0 || maxz < 0)
  {
    // No non-air voxels; nothing to crop
    return false;
  }
  uint32_t new_w = (uint32_t)(maxx - (int32_t)minx + 1);
  uint32_t new_h = (uint32_t)(maxy - (int32_t)miny + 1);
  uint32_t new_d = (uint32_t)(maxz - (int32_t)minz + 1);
  if (new_w == world->width && new_h == world->height && new_d == world->depth)
    return false;
  Voxel *new_vox = (Voxel *)malloc((size_t)new_w * (size_t)new_h * (size_t)new_d * sizeof(Voxel));
  if (!new_vox)
    return false;
  // Initialize to AIR
  for (size_t i = 0; i < (size_t)new_w * new_h * new_d; i++)
  {
    new_vox[i].type = VOXEL_AIR;
    new_vox[i].condition_mask = 0ULL;
  }
  // Copy
  for (uint32_t z = 0; z < new_d; z++)
    for (uint32_t y = 0; y < new_h; y++)
      for (uint32_t x = 0; x < new_w; x++)
      {
        Voxel *src = world_get_voxel(world, x + minx, y + miny, z + minz);
        Voxel *dst = &new_vox[(size_t)z * (size_t)new_w * (size_t)new_h + (size_t)y * (size_t)new_w + x];
        if (src)
        {
          *dst = *src;
        }
        else
        {
          dst->type = VOXEL_AIR;
          dst->condition_mask = 0ULL;
        }
      }
  // Replace
  free(world->voxels);
  world->voxels = new_vox;
  world->width = new_w;
  world->height = new_h;
  world->depth = new_d;
  world->vector_clock++;
  // Cropping renumbers every voxel and changes the column count, so everything derived from the old
  // dimensions is not merely stale but the wrong size.
  world->condition_revision++;
  world_discard_derived_caches(world);
  world_append_log(world, "EVENT AUTOCROP");
  return true;
}

// Draw a solid sphere centered in the world by clearing and refilling voxels
// (deduplicated; use the more robust implementation below)

bool world_draw_sphere(World *world, uint32_t radius, VoxelType type)
{
  if (!world || !world->voxels)
    return false;
  if (type >= VOXEL_COUNT && type != VOXEL_WORLD)
    return false;
  // Compute center
  float cx = (float)world->width * 0.5f - 0.5f;
  float cy = (float)world->height * 0.5f - 0.5f;
  float cz = (float)world->depth * 0.5f - 0.5f;
  // Clamp radius to fit inside bounds
  float maxr = cx;
  if (cy < maxr)
    maxr = cy;
  if (cz < maxr)
    maxr = cz;
  float R = (float)radius;
  if (R < 0.0f)
    R = 0.0f;
  if (R > maxr)
    R = maxr;
  float R2 = R * R;
  // Clear to AIR
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
        world_set_voxel(world, x, y, z, VOXEL_AIR);
  // Fill sphere
  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t y = 0; y < world->height; y++)
    {
      for (uint32_t x = 0; x < world->width; x++)
      {
        float dx = (float)x - cx;
        float dy = (float)y - cy;
        float dz = (float)z - cz;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= R2)
        {
          world_set_voxel(world, x, y, z, type);
        }
      }
    }
  }
  world->vector_clock++;
  world_append_log(world, "EVENT DRAW SPHERE");
  return true;
}

// Helper function to determine if a point is inside an ellipsoid
static bool is_inside_ellipsoid(double x, double y, double z,
                                double center_x, double center_y, double center_z,
                                double radius_x, double radius_y, double radius_z)
{
  double dx = (x - center_x) / radius_x;
  double dy = (y - center_y) / radius_y;
  double dz = (z - center_z) / radius_z;

  return (dx * dx + dy * dy + dz * dz) <= 1.0;
}

// Generate a floating island world (defaults to home type)
void world_generate(World *world, const char *seed)
{
  if (!world)
    return;

  // Use the new type-based generation system
  world_generate_with_type(world, seed, WORLD_TYPE_SCOURED);
}

// Convert a voxel type to hex character (expanded set, rough mapping)
static char voxel_type_to_hex(VoxelType type)
{
  if (type == VOXEL_WORLD)
    return 'W';
  if (type >= VOXEL_COUNT)
    return '0';
  // Map groups into hex bins; not backwards compatible, but stable in this build
  static const char table[] = "0123456789ABCDEF";
  return table[type & 0x0F];
}

// Convert a hex character to voxel type (expanded set; lossy mapping)
static VoxelType hex_to_voxel_type(char hex)
{
  if (hex == 'W')
    return VOXEL_WORLD;
  if (hex >= '0' && hex <= '9')
    return (VoxelType)(hex - '0');
  if (hex >= 'A' && hex <= 'F')
    return (VoxelType)(hex - 'A' + 10);
  if (hex >= 'a' && hex <= 'f')
    return (VoxelType)(hex - 'a' + 10);
  return VOXEL_AIR;
}

// Add a condition to a voxel
bool world_add_voxel_condition(World *world, uint32_t x, uint32_t y, uint32_t z, const char *condition_name)
{
  Voxel *voxel = world_get_voxel(world, x, y, z);
  if (!voxel || !condition_name)
    return false;
  unsigned long long bit = condition_bit_from_name(condition_name);
  if (!bit)
    return false;
  voxel->condition_mask |= bit;
  world->condition_revision++;
  return true;
}

// Remove a condition from a voxel
bool world_remove_voxel_condition(World *world, uint32_t x, uint32_t y, uint32_t z, const char *condition_name)
{
  Voxel *voxel = world_get_voxel(world, x, y, z);
  if (!voxel || !condition_name)
    return false;
  unsigned long long bit = condition_bit_from_name(condition_name);
  if (!bit)
    return false;
  voxel->condition_mask &= ~bit;
  world->condition_revision++;
  return true;
}

// Check if a voxel has a specific condition
bool world_has_voxel_condition(World *world, uint32_t x, uint32_t y, uint32_t z, const char *condition_name)
{
  Voxel *voxel = world_get_voxel(world, x, y, z);
  if (!voxel || !condition_name)
    return false;
  unsigned long long bit = condition_bit_from_name(condition_name);
  if (!bit)
    return false;
  return (voxel->condition_mask & bit) != 0ULL;
}

// Clear all conditions from a voxel
void world_clear_voxel_conditions(World *world, uint32_t x, uint32_t y, uint32_t z)
{
  Voxel *voxel = world_get_voxel(world, x, y, z);
  if (voxel)
  {
    voxel->condition_mask = 0ULL;
    world->condition_revision++;
  }
}

bool world_condition_index_is_fresh(const World *world)
{
  return world && world->condition_index_valid &&
         world->condition_index_revision == world->condition_revision;
}

bool world_refresh_condition_index(World *world)
{
  if (!world || !world->voxels)
    return false;

  for (int bit = 0; bit < 64; bit++)
    world->condition_voxel_indices[bit].size = 0;

  const size_t voxel_count = (size_t)world->width * world->height * world->depth;
  for (size_t i = 0; i < voxel_count; i++)
  {
    uint64_t mask = world->voxels[i].condition_mask;
    while (mask)
    {
      // Walk only the set bits: a voxel typically carries none, and a full 64-way test per voxel
      // would dominate the scan.
      const int bit = __builtin_ctzll(mask);
      mask &= mask - 1ULL;

      struct VoxelIndexList *list = &world->condition_voxel_indices[bit];
      if (list->size >= list->cap)
      {
        const size_t new_cap = list->cap ? list->cap * 2u : 64u;
        uint32_t *grown = (uint32_t *)realloc(list->indices, new_cap * sizeof(uint32_t));
        if (!grown)
        {
          world->condition_index_valid = false;
          return false;
        }
        list->indices = grown;
        list->cap = new_cap;
      }
      list->indices[list->size++] = (uint32_t)i;
    }
  }

  world->condition_index_revision = world->condition_revision;
  world->condition_index_valid = true;
  return true;
}

// Advance world by one epoch; simple bloom lifecycle example.
bool world_advance_epoch(World *world, uint32_t epoch_idx)
{
  if (!world)
    return false;
  bool changed = false;

  // After a minimum number of epochs, occasionally transform a FLOWERING grass into a FLOWER.
  if (epoch_idx >= MIN_EPOCHS_FOR_BLOOM)
  {
    // Deterministic PRNG based on epoch index and world RNG state
    uint32_t s = 1469598103u ^ epoch_idx ^ world->rng_state;
    for (int tries = 0; tries < 256; tries++)
    {
      s = s * 1664525u + 1013904223u;
      uint32_t rx = (s >> 8);
      s = s * 1664525u + 1013904223u;
      uint32_t ry = (s >> 8);
      s = s * 1664525u + 1013904223u;
      uint32_t rz = (s >> 8);
      uint32_t x = (world->width > 0) ? (rx % world->width) : 0;
      uint32_t y = (world->height > 0) ? (ry % world->height) : 0;
      uint32_t z = (world->depth > 0) ? (rz % world->depth) : 0;
      Voxel *v = world_get_voxel(world, x, y, z);
      if (!v)
        continue;
      // 5% chance to bloom if voxel has FLOWERING condition and is a grass variant
      if (world_has_voxel_condition(world, x, y, z, "FLOWERING") &&
          (v->type == VOXEL_GRASS || v->type == VOXEL_GRASS_WIDE || v->type == VOXEL_GRASS_SHARP || v->type == VOXEL_GRASS_CLOVER))
      {
        s = s * 1664525u + 1013904223u;
        if ((s % 100u) < 5u)
        {
          world_set_voxel(world, x, y, z, VOXEL_BUSH);
          world_remove_voxel_condition(world, x, y, z, "FLOWERING");
          changed = true;
          break;
        }
      }
    }
  }

  if (changed)
  {
    world->vector_clock++;
  }
  return changed;
}

// Run epoch update and return a minimal JSON patch string describing changes
char *world_advance_epoch_and_patch(World *world, uint32_t epoch_idx)
{
  if (!world)
    return NULL;
  bool changed = world_advance_epoch(world, epoch_idx);
  if (!changed)
    return NULL;
  // Minimal JSON payload; caller frees
  char *out = (char *)malloc(64);
  if (!out)
    return NULL;
  snprintf(out, 64, "{\"epoch\":%u,\"event\":\"bloom\"}", (unsigned)epoch_idx);
  return out;
}

// Serialize a world to a hex string
// Minimal growable text buffer, for sections whose length is not known up front: the
// type run-length encoding can be a handful of runs or one per voxel.
typedef struct
{
  char *data;
  size_t len;
  size_t cap;
} SerialBuf;

static bool serial_buf_appendf(SerialBuf *b, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int need = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (need < 0)
    return false;

  if (b->len + (size_t)need + 1 > b->cap)
  {
    size_t cap = b->cap ? b->cap : 512;
    while (cap < b->len + (size_t)need + 1)
      cap *= 2;
    char *grown = (char *)realloc(b->data, cap);
    if (!grown)
      return false;
    b->data = grown;
    b->cap = cap;
  }

  va_start(args, fmt);
  vsnprintf(b->data + b->len, b->cap - b->len, fmt, args);
  va_end(args);
  b->len += (size_t)need;
  return true;
}

// Build the full-fidelity voxel section.
//
// The legacy per-voxel stream stores one hex digit per voxel, i.e. only the low 4 bits of
// the type (`type & 0x0F`), so every type >= 16 is silently aliased on load — 4% of a
// wilderness world and 6% of a farm. It also carries no condition_mask and no data8, so
// wet/burning/damaged/heated voxels lose that state entirely.
//
// VOX2 records the real values. Types are run-length encoded because worlds are dominated
// by long runs (measured: 0.3%–2.4% of voxel count), and condition_mask/data8 are stored
// as sparse index lists because they are almost always zero outside gameplay.
static char *world_build_vox2_section(const World *world, size_t voxel_count, size_t *out_len)
{
  SerialBuf b = {NULL, 0, 0};
  bool ok = serial_buf_appendf(&b, "\nVOX2\n");

  // Run-length encoded types
  size_t runs = 0;
  for (size_t i = 0; i < voxel_count;)
  {
    VoxelType t = world->voxels[i].type;
    size_t j = i + 1;
    while (j < voxel_count && world->voxels[j].type == t)
      j++;
    runs++;
    i = j;
  }
  ok = ok && serial_buf_appendf(&b, "t %zu\n", runs);
  for (size_t i = 0; ok && i < voxel_count;)
  {
    VoxelType t = world->voxels[i].type;
    size_t j = i + 1;
    while (j < voxel_count && world->voxels[j].type == t)
      j++;
    ok = serial_buf_appendf(&b, "%zu:%d ", j - i, (int)t);
    i = j;
  }
  ok = ok && serial_buf_appendf(&b, "\n");

  // Sparse condition masks
  size_t cond_count = 0;
  for (size_t i = 0; i < voxel_count; i++)
    if (world->voxels[i].condition_mask)
      cond_count++;
  ok = ok && serial_buf_appendf(&b, "c %zu\n", cond_count);
  for (size_t i = 0; ok && i < voxel_count; i++)
    if (world->voxels[i].condition_mask)
      ok = serial_buf_appendf(&b, "%zu:%llx ", i,
                              (unsigned long long)world->voxels[i].condition_mask);
  ok = ok && serial_buf_appendf(&b, "\n");

  // Sparse packed auxiliary fields (entropy, quantity, damage, temperature, heat)
  size_t data_count = 0;
  for (size_t i = 0; i < voxel_count; i++)
    if (world->voxels[i].data8)
      data_count++;
  ok = ok && serial_buf_appendf(&b, "d %zu\n", data_count);
  for (size_t i = 0; ok && i < voxel_count; i++)
    if (world->voxels[i].data8)
      ok = serial_buf_appendf(&b, "%zu:%llx ", i, (unsigned long long)world->voxels[i].data8);
  ok = ok && serial_buf_appendf(&b, "\n");

  if (!ok)
  {
    free(b.data);
    return NULL;
  }
  *out_len = b.len;
  return b.data;
}

char *world_serialize(World *world)
{
  if (!world)
    return NULL;

  // Format: [version:4][width:8][height:8][depth:8][log_length:8][log_data...][voxels...]
  // Version is 4 hex chars (16 bits)
  // Each dimension is 8 hex chars (32 bits)
  // Log length is 8 hex chars (32 bits)
  // Log data is variable length
  // Each voxel is 1 hex char for type only, conditions are omitted for performance

  size_t voxel_count = world->width * world->height * world->depth;
  size_t log_length = world->log ? strlen(world->log) : 0;

  // Trailing metadata block. world_load restores these eleven fields, but nothing
  // used to write them, so every load silently reset them to world_create defaults.
  // Appending after the voxel payload keeps both directions compatible: readers that
  // stop at voxel_count ignore it, and saves without it still load (keeping defaults).
  char meta[512];
  int meta_len = snprintf(meta, sizeof(meta),
                          "\nMETA1\n"
                          "gen=%u\n"
                          "grav=%.9g\n"
                          "rar=%.9g\n"
                          "vclk=%llu\n"
                          "rng=%u\n"
                          "udep=%d\n"
                          "hist=%u\n"
                          "players=%u\n"
                          "base=%u\n"
                          "score=%.17g\n"
                          "level=%.17g\n"
                          "seed=%s\n",
                          (unsigned)world->generation_type,
                          (double)world->gravity,
                          (double)world->rarity,
                          (unsigned long long)world->vector_clock,
                          (unsigned)world->rng_state,
                          (int)world->universe_depth,
                          (unsigned)world->history_event_count,
                          (unsigned)world->unique_player_count,
                          (unsigned)world->base_level,
                          world->score,
                          world->level,
                          world->seed_id);
  if (meta_len < 0 || (size_t)meta_len >= sizeof(meta))
    return NULL;

  size_t vox2_len = 0;
  char *vox2 = world_build_vox2_section(world, voxel_count, &vox2_len);
  if (!vox2)
    return NULL;

  // Calculate buffer size: header (28) + log length (8) + log data + voxels + metadata + VOX2 + null terminator
  size_t buffer_size = 36 + log_length + voxel_count + (size_t)meta_len + vox2_len + 1;
  char *buffer = (char *)malloc(buffer_size);

  if (!buffer)
  {
    free(vox2);
    return NULL;
  }

  // Write version and dimensions
  sprintf(buffer, "%04X%08X%08X%08X", world->version, world->width, world->height, world->depth);

  // Write log length and log data
  sprintf(buffer + 28, "%08X", (uint32_t)log_length);
  size_t offset = 36; // Start after header and log length

  if (world->log && log_length > 0)
  {
    memcpy(buffer + offset, world->log, log_length);
    offset += log_length;
  }

  // Legacy voxel stream: one hex digit per voxel, kept byte-for-byte so existing readers
  // (assets/viewer.html, older builds) still work. VOX2 below carries the real values.
  for (size_t i = 0; i < voxel_count; i++)
  {
    buffer[offset++] = voxel_type_to_hex(world->voxels[i].type);
  }

  memcpy(buffer + offset, meta, (size_t)meta_len);
  offset += (size_t)meta_len;

  memcpy(buffer + offset, vox2, vox2_len);
  offset += vox2_len;
  free(vox2);

  buffer[offset] = '\0';
  return buffer;
}

// Read the optional VOX2 section written by world_serialize, restoring the values the
// legacy per-voxel stream cannot represent: types >= 16, condition masks, and packed
// auxiliary fields. Saves predating the section simply lack it, leaving the lossy
// legacy types in place — which is still what those files contain.
static void world_parse_vox2_section(World *world, const char *data,
                                    size_t data_length, size_t search_from,
                                    size_t voxel_count)
{
  if (!world || !data || search_from >= data_length)
    return;

  const char *sec = strstr(data + search_from, "\nVOX2\n");
  if (!sec)
    return;

  // Types: run-length encoded, "<count>:<type>" repeated
  const char *cur = strstr(sec, "\nt ");
  unsigned long runs = 0;
  if (cur && sscanf(cur, "\nt %lu", &runs) == 1)
  {
    cur = strchr(cur + 1, '\n');
    size_t at = 0;
    for (unsigned long r = 0; cur && r < runs; r++)
    {
      unsigned long long count = 0;
      int type = 0;
      if (sscanf(cur, " %llu:%d", &count, &type) != 2)
        break;
      if (at + count > voxel_count)
        break;
      if (type >= 0 && type < VOXEL_COUNT)
        for (unsigned long long k = 0; k < count; k++)
          world->voxels[at + k].type = (VoxelType)type;
      at += count;
      cur = strchr(cur, ' ') ? strchr(cur + 1, ' ') : NULL;
    }
  }

  // Condition masks: sparse "<index>:<hex>" pairs
  cur = strstr(sec, "\nc ");
  unsigned long cond_count = 0;
  if (cur && sscanf(cur, "\nc %lu", &cond_count) == 1)
  {
    cur = strchr(cur + 1, '\n');
    for (unsigned long i = 0; cur && i < cond_count; i++)
    {
      unsigned long long index = 0, mask = 0;
      if (sscanf(cur, " %llu:%llx", &index, &mask) != 2)
        break;
      if (index < voxel_count)
        world->voxels[index].condition_mask = (uint64_t)mask;
      cur = strchr(cur, ' ') ? strchr(cur + 1, ' ') : NULL;
    }
    world->condition_revision++;
  }

  // Packed auxiliary fields: sparse "<index>:<hex>" pairs
  cur = strstr(sec, "\nd ");
  unsigned long data_count = 0;
  if (cur && sscanf(cur, "\nd %lu", &data_count) == 1)
  {
    cur = strchr(cur + 1, '\n');
    for (unsigned long i = 0; cur && i < data_count; i++)
    {
      unsigned long long index = 0, value = 0;
      if (sscanf(cur, " %llu:%llx", &index, &value) != 2)
        break;
      if (index < voxel_count)
        world->voxels[index].data8 = (uint64_t)value;
      cur = strchr(cur, ' ') ? strchr(cur + 1, ' ') : NULL;
    }
  }
}

// Read the optional trailing metadata block written by world_serialize. Saves made
// before the block existed simply lack it, in which case world_create's defaults stand.
static void world_parse_metadata_block(World *world, const char *data,
                                       size_t data_length, size_t search_from)
{
  if (!world || !data || search_from >= data_length)
    return;

  const char *meta = strstr(data + search_from, "\nMETA1\n");
  if (!meta)
    return;

  unsigned gen = 0, rng = 0, hist = 0, players = 0, base = 0;
  unsigned long long vclk = 0ULL;
  int udep = 0;
  double grav = 0.0, rar = 0.0, score = 0.0, level = 0.0;
  char seed[65];
  const char *field;

  if ((field = strstr(meta, "\ngen=")) && sscanf(field, "\ngen=%u", &gen) == 1)
    world->generation_type = (WorldGenerationType)gen;
  if ((field = strstr(meta, "\ngrav=")) && sscanf(field, "\ngrav=%lg", &grav) == 1)
    world_set_gravity(world, (float)grav);
  if ((field = strstr(meta, "\nrar=")) && sscanf(field, "\nrar=%lg", &rar) == 1)
    world->rarity = (float)rar;
  if ((field = strstr(meta, "\nvclk=")) && sscanf(field, "\nvclk=%llu", &vclk) == 1)
    world->vector_clock = (uint64_t)vclk;
  if ((field = strstr(meta, "\nrng=")) && sscanf(field, "\nrng=%u", &rng) == 1)
    world->rng_state = (uint32_t)rng;
  if ((field = strstr(meta, "\nudep=")) && sscanf(field, "\nudep=%d", &udep) == 1)
    world->universe_depth = (int32_t)udep;
  if ((field = strstr(meta, "\nhist=")) && sscanf(field, "\nhist=%u", &hist) == 1)
    world->history_event_count = (uint32_t)hist;
  if ((field = strstr(meta, "\nplayers=")) && sscanf(field, "\nplayers=%u", &players) == 1)
    world->unique_player_count = (uint32_t)players;
  if ((field = strstr(meta, "\nbase=")) && sscanf(field, "\nbase=%u", &base) == 1)
    world->base_level = (uint32_t)base;
  if ((field = strstr(meta, "\nscore=")) && sscanf(field, "\nscore=%lg", &score) == 1)
    world->score = score;
  if ((field = strstr(meta, "\nlevel=")) && sscanf(field, "\nlevel=%lg", &level) == 1)
    world->level = level;
  // seed may legitimately be empty, so an empty match must not be treated as failure
  if ((field = strstr(meta, "\nseed=")))
  {
    if (sscanf(field, "\nseed=%64[^\n]", seed) == 1)
    {
      strncpy(world->seed_id, seed, sizeof(world->seed_id) - 1);
      world->seed_id[sizeof(world->seed_id) - 1] = '\0';
    }
    else
    {
      world->seed_id[0] = '\0';
    }
  }
}

// Deserialize a world from a hex string
static bool world_io_debug(void)
{
  static int cached = -1;
  if (cached < 0)
    cached = (getenv("VERSE_WORLD_IO_DEBUG") != NULL) ? 1 : 0;
  return cached != 0;
}

World *world_deserialize(const char *data)
{
  if (!data || strlen(data) < 36)
  {
    if (world_io_debug())
      printf("[world_deserialize] data too small (len=%zu)\n", data ? strlen(data) : 0UL);
    return NULL; // Minimum size for header + log length
  }

  // Read version and dimensions
  uint16_t version;
  uint32_t width, height, depth, log_length;
  char dim_buffer[9];

  // Extract version
  char version_buffer[5];
  strncpy(version_buffer, data, 4);
  version_buffer[4] = '\0';
  sscanf(version_buffer, "%hX", &version);

  // Extract width
  strncpy(dim_buffer, data + 4, 8);
  dim_buffer[8] = '\0';
  sscanf(dim_buffer, "%X", &width);

  // Extract height
  strncpy(dim_buffer, data + 12, 8);
  dim_buffer[8] = '\0';
  sscanf(dim_buffer, "%X", &height);

  // Extract depth
  strncpy(dim_buffer, data + 20, 8);
  dim_buffer[8] = '\0';
  sscanf(dim_buffer, "%X", &depth);

  // Extract log length
  strncpy(dim_buffer, data + 28, 8);
  dim_buffer[8] = '\0';
  sscanf(dim_buffer, "%X", &log_length);

  // Create new world
  World *world = world_create(width, height, depth);
  if (!world)
    return NULL;

  // Set the version
  world->version = version;

  // Read log data
  size_t log_offset = 36;
  if (log_length > 0 && log_offset + log_length <= strlen(data))
  {
    char *log_data = malloc(log_length + 1);
    if (log_data)
    {
      memcpy(log_data, data + log_offset, log_length);
      log_data[log_length] = '\0';
      world->log = log_data;
    }
  }

  // Read voxels (simplified format - just types, no conditions)
  size_t voxel_count = width * height * depth;
  size_t data_length = strlen(data);
  size_t voxel_offset = log_offset + log_length;

  if (data_length >= voxel_offset + voxel_count)
  {
    // Read voxel types
    for (size_t i = 0; i < voxel_count; i++)
    {
      if (voxel_offset + i < data_length)
      {
        world->voxels[i].type = hex_to_voxel_type(data[voxel_offset + i]);
      }
      else
      {
        // Default to air if data is truncated
        world->voxels[i].type = VOXEL_AIR;
      }
      // All conditions are initially empty
      world->voxels[i].condition_mask = 0ULL;
    }
    if (world_io_debug())
      printf("[world_deserialize] parsed dims=%ux%ux%u voxels=%zu log_len=%u\n",
             world->width, world->height, world->depth, voxel_count, (unsigned)log_length);

    world_parse_metadata_block(world, data, data_length, voxel_offset + voxel_count);
    world_parse_vox2_section(world, data, data_length, voxel_offset + voxel_count, voxel_count);
  }
  else
  {
    if (world_io_debug())
      printf("[world_deserialize] insufficient data for voxels (data_len=%zu, needed=%zu)\n",
             data_length, voxel_offset + voxel_count);
  }

  // The voxel array was written directly above, past world_create's claim that the world is empty.
  world_invalidate_fluid_presence(world);
  world_refresh_occupancy_bitfield(world);
  world->voxel_revision++;

  return world;
}

bool world_save(World *world, const char *filename)
{
  if (!world || !filename)
    return false;

  // Serialize the world
  char *data = world_serialize(world);
  if (!data)
    return false;

  // Open file for writing
  FILE *file = fopen(filename, "w");
  if (!file)
  {
    free(data);
    return false;
  }

  // Write data
  size_t len = strlen(data);
  size_t written = fwrite(data, 1, len, file);
  free(data);
  fclose(file);

  return written == len;
}

bool world_load(World *world, const char *filename)
{
  if (!world || !filename)
    return false;

  if (world_io_debug())
    printf("[world_load] opening '%s'\n", filename);
  // Open file for reading
  FILE *file = fopen(filename, "r");
  if (!file)
  {
    if (world_io_debug())
      printf("[world_load] fopen failed for '%s'\n", filename);
    return false;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0)
  {
    fclose(file);
    if (world_io_debug())
      printf("[world_load] empty or invalid file size: %ld\n", file_size);
    return false;
  }

  // Allocate buffer for file content
  char *buffer = (char *)malloc(file_size + 1);
  if (!buffer)
  {
    fclose(file);
    if (world_io_debug())
      printf("[world_load] malloc failed (size=%ld)\n", file_size);
    return false;
  }

  // Read file content
  size_t read_size = fread(buffer, 1, file_size, file);
  fclose(file);
  if (read_size != (size_t)file_size)
  {
    free(buffer);
    if (world_io_debug())
      printf("[world_load] fread mismatch (read=%zu, expected=%ld)\n", read_size, file_size);
    return false;
  }
  buffer[file_size] = '\0';

  // Deserialize into a temporary world
  World *loaded = world_deserialize(buffer);
  free(buffer);
  if (!loaded)
  {
    if (world_io_debug())
      printf("[world_load] deserialize failed for '%s'\n", filename);
    return false;
  }

  // Replace fields in the provided world instance
  // Free existing resources
  if (world->voxels)
    free(world->voxels);
  if (world->log)
  {
    free(world->log);
    world->log = NULL;
  }

  // Copy basic fields
  world->version = loaded->version;
  world->width = loaded->width;
  world->height = loaded->height;
  world->depth = loaded->depth;

  // Adopt buffers from loaded world to avoid extra copies
  world->voxels = loaded->voxels;
  loaded->voxels = NULL;
  world->log = loaded->log;
  loaded->log = NULL;

  // Initialize or copy remaining metadata sensibly
  // Gravity may not be persisted; use default if zero-ish, then clamp to the live range.
  world_set_gravity(world, loaded->gravity > 0.0f ? loaded->gravity : GRAVITY_DEFAULT);
  world->rarity = loaded->rarity;
  world->generation_type = loaded->generation_type;
  world->vector_clock = loaded->vector_clock;
  world->rng_state = loaded->rng_state;
  world->history_event_count = loaded->history_event_count;
  world->unique_player_count = loaded->unique_player_count;
  world->base_level = loaded->base_level;
  world->score = loaded->score;
  world->level = loaded->level;
  memcpy(world->seed_id, loaded->seed_id, sizeof(world->seed_id));
  world->universe_depth = loaded->universe_depth; // persist universe depth

  // Free the temporary world shell
  free(loaded);
  if (world_io_debug())
    printf("[world_load] success dims=%ux%ux%u from '%s'\n", world->width, world->height, world->depth, filename);
  return true;
}

bool world_save_by_seed(World *world, const char *seed)
{
  if (!world || !seed)
    return false;

  // Create worlds directory if it doesn't exist
  system("mkdir -p worlds");

  // Get the type name for the filename
  const char *type_name;
  switch (world->generation_type)
  {
  case WORLD_TYPE_HOME:
    type_name = "HOME";
    break;
  case WORLD_TYPE_FARM:
    type_name = "FARM";
    break;
  case WORLD_TYPE_RANDOM:
    type_name = "RANDOM";
    break;
  case WORLD_TYPE_WILDERNESS:
    type_name = "WILDERNESS";
    break;
  case WORLD_TYPE_SOLID:
    type_name = "SOLID";
    break;
  case WORLD_TYPE_UNDERWORLD:
    type_name = "UNDERWORLD";
    break;
  case WORLD_TYPE_SCOURED:
    type_name = "SCOURED";
    break;
  case WORLD_TYPE_LABYRINTH_SQUARE:
    type_name = "LABYRINTH";
    break;
  case WORLD_TYPE_WFC_TOWN:
    type_name = "WFC_TOWN";
    break;
  case WORLD_TYPE_CLOUD:
    type_name = "CLOUD";
    break;
  case WORLD_TYPE_ARENA:
    type_name = "ARENA";
    break;
  default:
    type_name = "UNKNOWN";
    break;
  }

  // Create filename: worlds/<SEED>.<TYPE>.world
  char filename[256];
  snprintf(filename, sizeof(filename), "worlds/%s.%s.world", seed, type_name);

  return world_save(world, filename);
}

bool world_load_by_seed(World *world, const char *seed)
{
  if (!world || !seed)
    return false;

  // Try to load with type-specific filename first
  // We need to try different types since we don't know which one it was saved as
  const char *type_names[] = {"HOME", "FARM", "RANDOM", "WILDERNESS", "SOLID",
                              "UNDERWORLD", "SCOURED", "LABYRINTH", "WFC_TOWN", "CLOUD", "ARENA", "UNKNOWN"};

  for (int i = 0; i < sizeof(type_names) / sizeof(type_names[0]); i++)
  {
    char filename[256];
    snprintf(filename, sizeof(filename), "worlds/%s.%s.world", seed, type_names[i]);

    if (world_load(world, filename))
    {
      return true; // Successfully loaded
    }
  }

  // Fallback to old format for backward compatibility
  char filename[256];
  snprintf(filename, sizeof(filename), "worlds/%s.world", seed);
  return world_load(world, filename);
}

bool world_exists_by_seed(const char *seed)
{
  if (!seed)
    return false;

  // Check for type-specific filenames first
  const char *type_names[] = {"HOME", "FARM", "RANDOM", "WILDERNESS", "SOLID",
                              "UNDERWORLD", "SCOURED", "LABYRINTH", "WFC_TOWN", "CLOUD", "ARENA", "UNKNOWN"};

  for (int i = 0; i < sizeof(type_names) / sizeof(type_names[0]); i++)
  {
    char filename[256];
    snprintf(filename, sizeof(filename), "worlds/%s.%s.world", seed, type_names[i]);

    FILE *file = fopen(filename, "r");
    if (file)
    {
      fclose(file);
      return true;
    }
  }

  // Fallback to old format for backward compatibility
  char filename[256];
  snprintf(filename, sizeof(filename), "worlds/%s.world", seed);
  FILE *file = fopen(filename, "r");
  if (file)
  {
    fclose(file);
    return true;
  }

  return false;
}

// Save world with universe context
bool world_save_with_universe(World *world, const char *seed, struct Universe *universe)
{
  if (!world || !seed || !universe)
    return false;

  // Create worlds directory if it doesn't exist
  system("mkdir -p worlds");

  // Get the type name for the filename
  const char *type_name;
  switch (world->generation_type)
  {
  case WORLD_TYPE_HOME:
    type_name = "HOME";
    break;
  case WORLD_TYPE_FARM:
    type_name = "FARM";
    break;
  case WORLD_TYPE_RANDOM:
    type_name = "RANDOM";
    break;
  case WORLD_TYPE_WILDERNESS:
    type_name = "WILDERNESS";
    break;
  case WORLD_TYPE_SOLID:
    type_name = "SOLID";
    break;
  case WORLD_TYPE_UNDERWORLD:
    type_name = "UNDERWORLD";
    break;
  case WORLD_TYPE_SCOURED:
    type_name = "SCOURED";
    break;
  case WORLD_TYPE_LABYRINTH_SQUARE:
    type_name = "LABYRINTH";
    break;
  case WORLD_TYPE_WFC_TOWN:
    type_name = "WFC_TOWN";
    break;
  case WORLD_TYPE_CLOUD:
    type_name = "CLOUD";
    break;
  case WORLD_TYPE_ARENA:
    type_name = "ARENA";
    break;
  default:
    type_name = "UNKNOWN";
    break;
  }

  // Create filename: worlds/<SEED>.<TYPE>.world
  char filename[256];
  snprintf(filename, sizeof(filename), "worlds/%s.%s.world", seed, type_name);

  // Save universe context information
  if (world->universe_context)
  {
    // Save universe coordinates in the world's metadata
    world->universe_x = world->universe_x;
    world->universe_y = world->universe_y;
    world->universe_z = world->universe_z;
  }

  return world_save(world, filename);
}

// Load world with universe context
World *world_load_with_universe(const char *seed, struct Universe *universe)
{
  if (!seed || !universe)
    return NULL;

  // Try to load with type-specific filename first
  const char *type_names[] = {"HOME", "FARM", "RANDOM", "WILDERNESS", "SOLID",
                              "UNDERWORLD", "SCOURED", "LABYRINTH", "WFC_TOWN", "CLOUD", "ARENA", "UNKNOWN"};

  for (int i = 0; i < sizeof(type_names) / sizeof(type_names[0]); i++)
  {
    char filename[256];
    snprintf(filename, sizeof(filename), "worlds/%s.%s.world", seed, type_names[i]);

    // Create a new world and try to load into it
    World *world = world_create(32, 32, 32); // Default size, will be overwritten
    if (world && world_load(world, filename))
    {
      // Set universe context
      world->universe_context = universe;
      // Note: universe coordinates are loaded from the saved file
      return world;
    }
    if (world)
    {
      world_destroy(world);
    }
  }

  // Fallback to old format for backward compatibility
  char filename[256];
  snprintf(filename, sizeof(filename), "worlds/%s.world", seed);
  World *world = world_create(32, 32, 32); // Default size, will be overwritten
  if (world && world_load(world, filename))
  {
    world->universe_context = universe;
    return world;
  }
  if (world)
  {
    world_destroy(world);
  }

  return NULL;
}

void construct_world_filename(char *buffer, size_t buffer_size, const char *id)
{
  if (!buffer || !id || buffer_size == 0)
    return;

  // For now, keep the old format for backward compatibility
  // This function is used by other parts of the system
  snprintf(buffer, buffer_size, "worlds/%s.world", id);
}

// Get world gravity
float world_get_gravity(World *world)
{
  if (!world)
    return GRAVITY_DEFAULT;
  return world->gravity;
}

void world_set_gravity(World *world, float gravity)
{
  if (!world)
    return;
  if (gravity < GRAVITY_MIN)
    gravity = GRAVITY_MIN;
  if (gravity > GRAVITY_MAX)
    gravity = GRAVITY_MAX;
  world->gravity = gravity;
}

static bool world_try_transfer_actor_across_boundary(World *world, int index);

// Step actor AI (when extra_data is a MobActor) and physics under gravity for dt seconds.
void world_step_actors(World *world, float dt_seconds)
{
  if (!world || !world->runtime_actors || world->runtime_actor_count <= 0)
    return;
  float g = world_get_gravity(world);
  if (g <= 0.0f)
    g = GRAVITY_DEFAULT;
  for (int i = 0; i < world->runtime_actor_count; i++)
  {
    Actor *a = &world->runtime_actors[i];
    if (!a->is_active)
      continue;

    if (a->hurt_display_ttl > 0.0f)
    {
      a->hurt_display_ttl -= dt_seconds;
      if (a->hurt_display_ttl < 0.0f)
        a->hurt_display_ttl = 0.0f;
    }

    // The inhabited body is integrated as the player; simulating it here would fight WASD.
    if (a->is_controlled)
      continue;

    if (a->extra_data)
    {
      MobActor *mob = (MobActor *)a->extra_data;
      mob->base.x = a->x;
      mob->base.y = a->y;
      mob->base.z = a->z;
      mob->base.velocity_x = a->velocity_x;
      mob->base.velocity_y = a->velocity_y;
      mob->base.velocity_z = a->velocity_z;
      mob->base.is_active = a->is_active;
      mob->base.is_controlled = a->is_controlled;
      mob->base.is_flying = a->is_flying;
      mob->base.health = a->health;
      mob->base.freeze_ttl = a->freeze_ttl;
      mob->base.chill = a->chill;
      mob_actor_update(mob, world, dt_seconds);
      a->x = mob->base.x;
      a->y = mob->base.y;
      a->z = mob->base.z;
      a->velocity_x = mob->base.velocity_x;
      a->velocity_y = mob->base.velocity_y;
      a->velocity_z = mob->base.velocity_z;
      a->is_active = mob->base.is_active;
      a->is_flying = mob->base.is_flying;
      a->freeze_ttl = mob->base.freeze_ttl;
      a->chill = mob->base.chill;
    }

    if (!a->is_flying)
      a->velocity_z -= g * dt_seconds;

    double new_x = a->x + a->velocity_x * dt_seconds;
    double new_y = a->y + a->velocity_y * dt_seconds;
    int iz = (int)floor(a->z);

    // Collision asks only whether the destination is passable, never whether something holds the
    // actor up there. Support is the loop below's job, and conflating the two is what stopped an
    // actor at the lip of every ledge and let it be walled in by its own footing.
    //
    // Use a radius cylinder (MOB_ACTOR_RADIUS) so corner-clipped solid voxels block walkers the
    // same way the player cylinder does — a single floor(x)/floor(y) sample let bodies clip posts.
    const bool xy_ok = mob_actor_can_occupy(world, new_x, new_y, a->z);
    if (xy_ok)
    {
      a->x = new_x;
      a->y = new_y;
    }
    else if (!a->is_flying &&
             mob_actor_can_occupy(world, new_x, new_y, (double)(iz + 1)) &&
             mob_actor_can_occupy(world, a->x, a->y, (double)(iz + 1)))
    {
      // Step up onto a one-voxel rise, provided there is room to rise from where the actor stands
      // as well as room where it is going.
      a->x = new_x;
      a->y = new_y;
      a->z = (double)(iz + 1);
      a->velocity_z = 0.0;
    }
    else
    {
      // Blocked as a pair. Give up the obstructed axis and keep the other, which is what lets an
      // actor slide along a wall it is pushed into at an angle. Zeroing both instead pinned it
      // there for as long as its AI kept aiming through the wall, which is most of the time.
      if (mob_actor_can_occupy(world, new_x, a->y, a->z))
      {
        a->x = new_x;
        a->velocity_y = 0.0;
      }
      else if (mob_actor_can_occupy(world, a->x, new_y, a->z))
      {
        a->y = new_y;
        a->velocity_x = 0.0;
      }
      else
      {
        a->velocity_x = 0.0;
        a->velocity_y = 0.0;
      }
    }

    // Soft actor-actor separation so bodies do not occupy the same space. Mutual half-pushes,
    // limited to the same vertical band so multi-floor settlements do not shove through floors.
    {
      const float min_d = MOB_ACTOR_RADIUS * 2.0f;
      const float min_d2 = min_d * min_d;
      for (int j = 0; j < world->runtime_actor_count; j++)
      {
        if (j == i)
          continue;
        Actor *o = &world->runtime_actors[j];
        if (!o->is_active || o->health == 0)
          continue;
        // Controlled bodies are integrated by the player; still keep AI off their cylinder.
        float dx = (float)(a->x - o->x);
        float dy = (float)(a->y - o->y);
        float dz = (float)(a->z - o->z);
        if (fabsf(dz) > MOB_ACTOR_SEPARATION_Z)
          continue;
        float d2 = dx * dx + dy * dy;
        if (d2 >= min_d2)
          continue;
        if (d2 < 1e-8f)
        {
          // Identical overlap: deterministic nudge from ids so both do not stay stacked.
          const float nudge = 0.08f;
          const float sx = (a->id > o->id) ? nudge : -nudge;
          const float sy = (a->id > o->id) ? nudge * 0.6f : -nudge * 0.6f;
          double px = a->x + (double)sx;
          double py = a->y + (double)sy;
          if (mob_actor_can_occupy(world, px, py, a->z))
          {
            a->x = px;
            a->y = py;
          }
          continue;
        }
        float dist = sqrtf(d2);
        float push = (min_d - dist) * 0.5f;
        float nx = dx / dist;
        float ny = dy / dist;

        double ax = a->x + (double)(nx * push);
        double ay = a->y + (double)(ny * push);
        double ox = o->x - (double)(nx * push);
        double oy = o->y - (double)(ny * push);

        const bool a_ok = mob_actor_can_occupy(world, ax, ay, a->z);
        const bool o_ok = !o->is_controlled && mob_actor_can_occupy(world, ox, oy, o->z);
        if (a_ok && o_ok)
        {
          a->x = ax;
          a->y = ay;
          o->x = ox;
          o->y = oy;
          if (o->extra_data)
          {
            MobActor *om = (MobActor *)o->extra_data;
            om->base.x = o->x;
            om->base.y = o->y;
          }
        }
        else if (a_ok)
        {
          // Other cannot move (wall / player-controlled): take the full separation.
          ax = a->x + (double)(nx * push * 2.0f);
          ay = a->y + (double)(ny * push * 2.0f);
          if (mob_actor_can_occupy(world, ax, ay, a->z))
          {
            a->x = ax;
            a->y = ay;
          }
        }
        else if (o_ok)
        {
          ox = o->x - (double)(nx * push * 2.0f);
          oy = o->y - (double)(ny * push * 2.0f);
          if (mob_actor_can_occupy(world, ox, oy, o->z))
          {
            o->x = ox;
            o->y = oy;
            if (o->extra_data)
            {
              MobActor *om = (MobActor *)o->extra_data;
              om->base.x = o->x;
              om->base.y = o->y;
            }
          }
        }
        else
        {
          // Both blocked along the separation axis — try a lateral slide for `a`.
          double lx = a->x + (double)(-ny * push);
          double ly = a->y + (double)(nx * push);
          if (mob_actor_can_occupy(world, lx, ly, a->z))
          {
            a->x = lx;
            a->y = ly;
          }
        }
      }
    }

    // Safety: if somehow inside solid terrain (tunnel / bad spawn), lift to open air.
    if (!a->is_flying && !mob_actor_can_occupy(world, a->x, a->y, a->z))
    {
      for (int raise = 1; raise <= 3; raise++)
      {
        double tz = a->z + (double)raise;
        if (tz >= (double)world->depth)
          break;
        if (mob_actor_can_occupy(world, a->x, a->y, tz))
        {
          a->z = tz;
          a->velocity_z = 0.0;
          break;
        }
      }
    }

    a->z += a->velocity_z * dt_seconds;

    int vx = (int)floor(a->x);
    int vy = (int)floor(a->y);
    const bool xy_oob = (vx < 0 || vy < 0 || vx >= (int)world->width || vy >= (int)world->height);
    int hz = -1;
    // Support search stays inside the box. A body that has already stepped past the rim waits for
    // the seam transfer below rather than looking up voxels in another cell's coordinates.
    if (!xy_oob)
    {
      int zstart = (int)floor(a->z);
      if (!world_pos_in_bounds_fast(world, vx, vy, zstart))
        zstart = (int)world->depth - 1;
      for (int z = zstart; z >= 0; z--)
      {
        if (world_is_solid_fast(world, vx, vy, z))
        {
          hz = z;
          break;
        }
      }
    }
    double ground_z = (hz >= 0 ? (double)hz + 1.0 : 0.5);
    // An empty column with a universe cell beneath must not invent a floor at z=0.5, or fauna can
    // never fall through a layer the way the player does.
    if (!xy_oob && hz < 0 && world->universe_context != NULL && world->universe_z > 0)
    {
      Voxel *bottom = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, 0);
      if (bottom && !world_voxel_type_blocks_movement(bottom->type))
        ground_z = -1.0; // allow the body below the rim so transfer can fire
    }
    if (!xy_oob && a->z < ground_z)
    {
      a->z = ground_z;
      if (a->velocity_z <= 0.0)
      {
        a->velocity_z = 0.0;
        a->is_flying = false;
      }
    }
    const bool seam_up =
        world->universe_context != NULL && a->z > (double)world->depth - 0.51;
    if (!seam_up && a->z > (double)world->depth - 0.51)
    {
      a->z = (double)world->depth - 0.51;
      if (a->velocity_z > 0.0)
        a->velocity_z = 0.0;
    }

    if (a->extra_data)
    {
      MobActor *mob = (MobActor *)a->extra_data;
      mob->base.x = a->x;
      mob->base.y = a->y;
      mob->base.z = a->z;
      mob->base.velocity_x = a->velocity_x;
      mob->base.velocity_y = a->velocity_y;
      mob->base.velocity_z = a->velocity_z;
      mob->base.is_active = a->is_active;
      mob->base.is_flying = a->is_flying;
    }

    // Uncontrolled fauna may walk (or fall) out of this cell into a loaded neighbour. Controlled
    // bodies cross with the player via game_state_carry_dominated_actor instead.
    if (!a->is_controlled && world_try_transfer_actor_across_boundary(world, i))
    {
      // Index i now holds the next actor (extract closed the gap); re-process it.
      i--;
      continue;
    }
  }
}

// Move a runtime actor that has left this world's box into the neighbouring universe cell, if that
// cell is loaded. Returns true when the actor was removed from `world` (caller should not advance).
static bool world_try_transfer_actor_across_boundary(World *world, int index)
{
  if (!world || !world->universe_context || index < 0 || index >= world->runtime_actor_count)
    return false;

  Actor *a = &world->runtime_actors[index];
  if (!a->is_active)
    return false;

  int cdx = 0, cdy = 0, cdz = 0;
  if (a->x < 0.0)
  {
    cdx = -1;
    a->x += (double)world->width;
  }
  else if (a->x >= (double)world->width)
  {
    cdx = 1;
    a->x -= (double)world->width;
  }
  if (a->y < 0.0)
  {
    cdy = -1;
    a->y += (double)world->height;
  }
  else if (a->y >= (double)world->height)
  {
    cdy = 1;
    a->y -= (double)world->height;
  }
  if (a->z < 0.0)
  {
    cdz = -1;
    a->z += (double)world->depth;
  }
  else if (a->z >= (double)world->depth)
  {
    cdz = 1;
    a->z -= (double)world->depth;
  }

  if (cdx == 0 && cdy == 0 && cdz == 0)
    return false;

  const int64_t nux = (int64_t)world->universe_x + cdx;
  const int64_t nuy = (int64_t)world->universe_y + cdy;
  const int64_t nuz = (int64_t)world->universe_z + cdz;
  if (nuz < 0)
  {
    // Below the wilderness plane: push back inside rather than destroying the actor.
    if (cdx != 0)
      a->x = (cdx < 0) ? 0.5 : (double)world->width - 0.5;
    if (cdy != 0)
      a->y = (cdy < 0) ? 0.5 : (double)world->height - 0.5;
    if (cdz != 0)
      a->z = 0.5;
    a->velocity_x = a->velocity_y = 0.0;
    if (a->velocity_z < 0.0)
      a->velocity_z = 0.0;
    return false;
  }

  World *next = universe_get(world->universe_context, (uint64_t)nux, (uint64_t)nuy, (uint64_t)nuz);
  if (!next)
  {
    // Neighbour not loaded yet: clamp to the rim and kill the outbound speed so AI does not
    // hammer the seam every tick.
    if (cdx != 0)
    {
      a->x = (cdx < 0) ? 0.5 : (double)world->width - 0.5;
      a->velocity_x = 0.0;
    }
    if (cdy != 0)
    {
      a->y = (cdy < 0) ? 0.5 : (double)world->height - 0.5;
      a->velocity_y = 0.0;
    }
    if (cdz != 0)
    {
      a->z = (cdz < 0) ? 0.5 : (double)world->depth - 0.5;
      a->velocity_z = 0.0;
    }
    return false;
  }

  Actor carried;
  const uint32_t id = a->id;
  const double rx = a->x, ry = a->y, rz = a->z;
  const double rvx = a->velocity_x, rvy = a->velocity_y, rvz = a->velocity_z;
  if (!world_extract_runtime_actor_by_id(world, id, &carried))
    return false;

  carried.x = rx;
  carried.y = ry;
  carried.z = rz;
  carried.velocity_x = rvx;
  carried.velocity_y = rvy;
  carried.velocity_z = rvz;
  if (carried.extra_data)
  {
    MobActor *mob = (MobActor *)carried.extra_data;
    mob->base.x = carried.x;
    mob->base.y = carried.y;
    mob->base.z = carried.z;
    mob->base.velocity_x = carried.velocity_x;
    mob->base.velocity_y = carried.velocity_y;
    mob->base.velocity_z = carried.velocity_z;
  }

  if (!world_add_runtime_actor(next, &carried))
  {
    (void)world_add_runtime_actor(world, &carried);
    return false;
  }
  return true;
}

bool world_actor_blocks_cell(const World *world, int x, int y, int z)
{
  if (!world || !world->runtime_actors || world->runtime_actor_count <= 0)
    return false;

  const float cx = (float)x + 0.5f;
  const float cy = (float)y + 0.5f;
  const float cz = (float)z + 0.5f;

  for (int i = 0; i < world->runtime_actor_count; i++)
  {
    const Actor *a = &world->runtime_actors[i];
    if (!a->is_active)
      continue;

    float radius = 0.35f;
    float height = 1.7f;
    if (mob_actor_is_bird(a) || mob_actor_is_bat(a))
    {
      radius = 0.28f;
      height = 0.55f;
    }
    else if (mob_actor_is_livestock(a))
    {
      radius = 0.40f;
      height = 1.1f;
    }
    else if (mob_actor_is_slime(a))
    {
      radius = 0.45f;
      height = 0.7f;
    }

    const float ax = (float)a->x;
    const float ay = (float)a->y;
    const float az = (float)a->z;
    const float dx = cx - ax;
    const float dy = cy - ay;
    if (dx * dx + dy * dy > radius * radius)
      continue;
    if (cz < az - 0.05f || cz > az + height)
      continue;
    return true;
  }
  return false;
}

bool world_add_runtime_actor(World *world, const struct Actor *actor)
{
  if (!world || !actor)
    return false;

  if (world->runtime_actor_count >= world->runtime_actor_capacity)
  {
    int new_capacity = world->runtime_actor_capacity ? world->runtime_actor_capacity * 2 : 16;
    struct Actor *new_actors = (struct Actor *)realloc(world->runtime_actors,
                                                       (size_t)new_capacity * sizeof(struct Actor));
    if (!new_actors)
      return false;
    world->runtime_actors = new_actors;
    world->runtime_actor_capacity = new_capacity;
  }

  world->runtime_actors[world->runtime_actor_count++] = *actor;
  world->runtime_actors_owned = true;
  return true;
}

struct Actor *world_find_runtime_actor(World *world, uint32_t id)
{
  if (!world || !world->runtime_actors || id == 0)
    return NULL;
  for (int i = 0; i < world->runtime_actor_count; i++)
  {
    if (world->runtime_actors[i].id == id)
      return &world->runtime_actors[i];
  }
  return NULL;
}

bool world_extract_runtime_actor_by_id(World *world, uint32_t id, struct Actor *out)
{
  if (!world || !world->runtime_actors || id == 0)
    return false;

  for (int i = 0; i < world->runtime_actor_count; i++)
  {
    if (world->runtime_actors[i].id != id)
      continue;

    if (out)
      *out = world->runtime_actors[i];

    // Close the gap; leave extra_data alone so the caller keeps the MobActor.
    const int last = world->runtime_actor_count - 1;
    if (i < last)
      memmove(&world->runtime_actors[i], &world->runtime_actors[i + 1],
              (size_t)(last - i) * sizeof(struct Actor));
    world->runtime_actor_count = last;
    return true;
  }
  return false;
}

void world_clear_runtime_actors(World *world)
{
  if (!world)
    return;

  if (world->runtime_actors)
  {
    if (world->runtime_actors_owned)
    {
      for (int i = 0; i < world->runtime_actor_count; i++)
      {
        if (world->runtime_actors[i].extra_data)
          mob_actor_destroy((MobActor *)world->runtime_actors[i].extra_data);
      }
      free(world->runtime_actors);
    }
    world->runtime_actors = NULL;
  }
  world->runtime_actor_count = 0;
  world->runtime_actor_capacity = 0;
  world->runtime_actors_owned = false;
}

// World log management functions
bool world_set_log(World *world, const char *log_message)
{
  if (!world)
    return false;

  // Free existing log if any
  if (world->log)
  {
    free(world->log);
    world->log = NULL;
  }

  if (log_message)
  {
    world->log = strdup(log_message);
    return world->log != NULL;
  }

  return true;
}

const char *world_get_log(World *world)
{
  if (!world)
    return NULL;
  return world->log;
}

bool world_append_log(World *world, const char *log_message)
{
  if (!world || !log_message)
    return false;

  if (world->log)
  {
    // Append to existing log
    size_t current_len = strlen(world->log);
    size_t new_len = current_len + strlen(log_message) + 2; // +2 for newline and null terminator

    char *new_log = realloc(world->log, new_len);
    if (!new_log)
      return false;

    world->log = new_log;
    strcat(world->log, "\n");
    strcat(world->log, log_message);
  }
  else
  {
    // Create new log
    world->log = strdup(log_message);
  }

  return world->log != NULL;
}

// Forward declarations for generators that are defined later in the file
static void world_generate_scoured(World *world, const char *seed);
static void world_generate_labyrinth_square(World *world, int num_entrances);
// Preferred side for labyrinth entrances, if provided via seed tag. -1 means unset.
static int s_labyrinth_forced_side = -1; // 0=NEG_X,1=POS_X,2=NEG_Y,3=POS_Y

// Generate world with specific type
void world_generation_prepare_shared_state(void)
{
  ensure_perlin_ready();
  ensure_entropy_fields_initialized();
}

void world_generate_with_type(World *world, const char *seed, WorldGenerationType type)
{
  if (!world)
    return;

  // Clearing this before the generators run rather than after is deliberate: from here the state
  // can only move to SOME (placing fluid) or stay UNKNOWN, never to NONE, since only world_create
  // and a completed census assert NONE. So whatever a generator does, the world it leaves behind
  // is described either accurately or conservatively.
  world_invalidate_fluid_presence(world);
  world_clear_magma_vent_columns(world);

  printf("[DEBUG] world_generate_with_type called: world=%p, seed=%s, type=%d\n",
         (void *)world, seed ? seed : "NULL", type);

  // Set the generation type
  world->generation_type = type;

  // Initialize the seeded random number generator
  seed_random(seed);

  // Generate deterministic gravity from seed
  world->gravity = generate_gravity_from_seed(seed);

  // Derive per-world identifiers and RNG from double SHA256 per BIP340 style
  {
    uint8_t h1[32], h2[32];
    calculate_sha256(seed ? seed : "", h1);
    // h1 is a 32-byte digest, not a string. Hashing it through the strlen-based helper read
    // past the end of the buffer and stopped at the first zero byte it happened to find, so
    // seed_id and rarity — which the next lines derive from h2 — depended on whatever was on
    // the stack behind h1 rather than on the seed. That made them differ between call paths
    // and, now that generation runs on a worker, between threads.
    calculate_sha256_bytes(h1, sizeof(h1), h2);
    for (int i = 0; i < 32; i++)
      sprintf(world->seed_id + i * 2, "%02x", h2[i]);
    world->seed_id[64] = '\0';
    world->rng_state = ((uint32_t)h2[0] << 24) | ((uint32_t)h2[1] << 16) | ((uint32_t)h2[2] << 8) | (uint32_t)h2[3];
    // Base rarity between ~1% and ~15%
    world->rng_state = world->rng_state * 1103515245u + 12345u;
    uint16_t r16 = (uint16_t)((world->rng_state >> 16) & 0xFFFF);
    float base = (float)r16 / 65535.0f;   // 0..1
    world->rarity = 0.01f + base * 0.14f; // 0.01..0.15
  }

  // Universe context will be set after world generation to avoid circular dependencies

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
    world_spawn_wilderness_mobs(world);
    break;
  case WORLD_TYPE_WFC_TOWN:
  {
    // Settlement worlds sit on the wilderness plane: same terrain, then stamp buildings by scale.
    world_generate_wilderness(world, seed);
    const int scale = universe_settlement_scale((int)world->universe_x, (int)world->universe_y);
    settlement_stamp(world, scale > 0 ? scale : 1, seed);
    // Re-stamp roads so spokes reach the plaza after buildings land, then place fauna
    // on that finished surface (not inside voxels or floating after pavement clears).
    settlement_stamp_roads(world);
    world_spawn_wilderness_mobs(world);
    // Villagers appear only in larger settlements (scale > 3).
    world_spawn_settlement_villagers(world);
    break;
  }
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
    if (world && world->voxels && world->depth > 1)
    {
    // Base: scoured floor at z=0 only
    apply_bedrock_floor:
      for (uint32_t y = 0; y < world->height; y++)
        for (uint32_t x = 0; x < world->width; x++)
          world_set_voxel(world, x, y, 0u, VOXEL_BEDROCK);
      // Build a maze of 1-voxel bedrock walls at z=1, with N entrances
      int entrances = 4; // TODO: parameterize
      // Parse optional preferred direction from seed: labdir=NEG_X|POS_X|NEG_Y|POS_Y or labdir=W|E|N|S
      if (seed && *seed)
      {
        const char *tag = strstr(seed, "labdir=");
        if (tag)
        {
          tag += 7; // after '='
          if (strncmp(tag, "NEG_X", 5) == 0 || *tag == 'W')
            s_labyrinth_forced_side = 0;
          else if (strncmp(tag, "POS_X", 5) == 0 || *tag == 'E')
            s_labyrinth_forced_side = 1;
          else if (strncmp(tag, "NEG_Y", 5) == 0 || *tag == 'S')
            s_labyrinth_forced_side = 2; // NEG_Y faces south
          else if (strncmp(tag, "POS_Y", 5) == 0 || *tag == 'N')
            s_labyrinth_forced_side = 3;
        }
      }
      world_generate_labyrinth_square(world, entrances);
      // Reset forced side after generation to avoid leaking preference
      s_labyrinth_forced_side = -1;
    }
    break;
  default:
    // Default to home type
    world_generate_home(world, seed);
    break;
  }
  // Build the occupancy bitfield once, now that the voxels are final. The renderer's face culling
  // and world_height_at_fast both prefer it, and paying for it here — a single pass at the end of
  // generation — is what lets them stop re-deriving it out of the voxel array every frame.
  world_refresh_occupancy_bitfield(world);
  world->voxel_revision++; // generators write directly; tell content caches to rebuild

  // Record genesis event
  char genesis_msg[96];
  snprintf(genesis_msg, sizeof(genesis_msg), "seed=%s", world->seed_id);
  world_append_log(world, "EVENT GENESIS");
  world_append_log(world, genesis_msg);
  world->history_event_count++;
  world->vector_clock++;

  // Particle sites are built lazily on first update so generation is not blocked by a
  // full-volume clearance scan (noticeable on wilderness/scoured).
}

void world_generate_with_type_and_fill(World *world, const char *seed, WorldGenerationType type, VoxelType fill_type)
{
  if (!world)
    return;
  if (type == WORLD_TYPE_SOLID)
  {
    world->generation_type = type;
    seed_random(seed);
    world->gravity = generate_gravity_from_seed(seed);
    world_generate_solid_fill(world, fill_type);
    world_refresh_occupancy_bitfield(world);
    world->voxel_revision++;
    return;
  }
  world_generate_with_type(world, seed, type);
}

// Generate standalone world with universe context at (0,0,0)
bool world_generate_standalone(World *world, const char *seed, WorldGenerationType type, struct Universe *universe)
{
  if (!world || !universe)
    return false;

  // Generate the world first
  world_generate_with_type(world, seed, type);

  // Now set universe context and place in universe
  if (universe_ensure_seed_consistency(universe, world, 0, 0, 0))
  {
    return true;
  }
  return false;
}

// Generate world in universe context at specific coordinates
bool world_generate_in_universe(World *world, const char *seed, WorldGenerationType type,
                                struct Universe *universe, uint64_t x, uint64_t y, uint64_t z)
{
  if (!world || !universe)
    return false;

  // Generate the world first
  world_generate_with_type(world, seed, type);

  // Now set universe context and place in universe
  if (universe_ensure_seed_consistency(universe, world, x, y, z))
  {
    return true;
  }
  return false;
}

// ---------------------------
// Tree generation helpers
// ---------------------------

typedef struct
{
  VoxelType trunk_type;
  VoxelType leaves_type;
  int min_trunk_height;
  int max_trunk_height; // inclusive
  int canopy_radius;    // horizontal half-extent of the leaf ellipsoid
  int canopy_height;    // leaf extent above the trunk tip
} TreeConfig;

// Ellipsoid leaf canopy centered near the trunk tip. Caps the tip with leaves so wood
// does not poke through; thins the outer shell so the silhouette is not a solid box.
static void plant_canopy_ellipsoid_Z(World *world, int x, int y, int trunk_top_z,
                                     int radius, int height_above, int skirt,
                                     VoxelType leaves)
{
  if (!world || radius < 1)
    return;
  if (skirt < 0)
    skirt = 0;
  if (height_above < 1)
    height_above = 1;

  // Centre the ellipsoid a little above the tip so bulk sits over the wood, not beside it
  const float mid = ((float)height_above - (float)skirt) * 0.5f;
  const float rx = (float)radius + 0.35f;
  const float ry = (float)radius + 0.35f;
  float rz = ((float)skirt + (float)height_above) * 0.5f + 0.35f;
  if (rz < 1.0f)
    rz = 1.0f;
  const float rx2 = rx * rx;
  const float ry2 = ry * ry;
  const float rz2 = rz * rz;

  for (int oz = -skirt; oz <= height_above; oz++)
  {
    const int lz = trunk_top_z + oz;
    const float nz = (float)oz - mid;
    for (int ox = -radius; ox <= radius; ox++)
    {
      for (int oy = -radius; oy <= radius; oy++)
      {
        const float d2 = ((float)ox * (float)ox) / rx2 +
                         ((float)oy * (float)oy) / ry2 +
                         (nz * nz) / rz2;
        if (d2 > 1.0f)
          continue;
        // Soft outer shell: drop some edge voxels so corners do not read as cubes
        if (d2 > 0.68f && seeded_rand_range(100) < 45)
          continue;
        const int lx = x + ox;
        const int ly = y + oy;
        if (!world_is_position_valid(world, lx, ly, lz))
          continue;
        // Keep the lower trunk; overwrite the tip (oz==0) and everything above with leaves
        if (ox == 0 && oy == 0 && oz < 0)
          continue;
        world_set_voxel(world, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz, leaves);
      }
    }
  }
}

static void plant_canopy_ellipsoid_Y(World *world, int x, int trunk_top_y, int z,
                                     int radius, int height_above, int skirt,
                                     VoxelType leaves)
{
  if (!world || radius < 1)
    return;
  if (skirt < 0)
    skirt = 0;
  if (height_above < 1)
    height_above = 1;

  const float mid = ((float)height_above - (float)skirt) * 0.5f;
  const float rx = (float)radius + 0.35f;
  const float rz = (float)radius + 0.35f;
  float ry = ((float)skirt + (float)height_above) * 0.5f + 0.35f;
  if (ry < 1.0f)
    ry = 1.0f;
  const float rx2 = rx * rx;
  const float ry2 = ry * ry;
  const float rz2 = rz * rz;

  for (int oy = -skirt; oy <= height_above; oy++)
  {
    const int ly = trunk_top_y + oy;
    const float ny = (float)oy - mid;
    for (int ox = -radius; ox <= radius; ox++)
    {
      for (int oz = -radius; oz <= radius; oz++)
      {
        const float d2 = ((float)ox * (float)ox) / rx2 +
                         (ny * ny) / ry2 +
                         ((float)oz * (float)oz) / rz2;
        if (d2 > 1.0f)
          continue;
        if (d2 > 0.68f && seeded_rand_range(100) < 45)
          continue;
        const int lx = x + ox;
        const int lz = z + oz;
        if (!world_is_position_valid(world, lx, ly, lz))
          continue;
        if (ox == 0 && oz == 0 && oy < 0)
          continue;
        world_set_voxel(world, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz, leaves);
      }
    }
  }
}

static bool tree_voxel_is_wood(VoxelType t)
{
  return t == VOXEL_WOOD || (t >= VOXEL_WOOD_OAK && t <= VOXEL_WOOD_REDWOOD);
}

static void plant_leaf_puff_Z(World *world, int cx, int cy, int cz, int radius, VoxelType leaves)
{
  if (!world || radius < 1)
    return;
  const int r2 = radius * radius;
  for (int ox = -radius; ox <= radius; ox++)
  {
    for (int oy = -radius; oy <= radius; oy++)
    {
      for (int oz = -radius; oz <= radius; oz++)
      {
        const int d2 = ox * ox + oy * oy + oz * oz;
        if (d2 > r2)
          continue;
        if (d2 > (r2 * 2) / 3 && seeded_rand_range(100) < 40)
          continue;
        const int lx = cx + ox;
        const int ly = cy + oy;
        const int lz = cz + oz;
        if (!world_is_position_valid(world, lx, ly, lz))
          continue;
        // Cap the tip; leave other wood alone so the main trunk stays intact
        if (!(ox == 0 && oy == 0 && oz == 0))
        {
          Voxel *v = world_get_voxel(world, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz);
          if (v && tree_voxel_is_wood(v->type))
            continue;
        }
        world_set_voxel(world, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz, leaves);
      }
    }
  }
}

static void plant_leaf_puff_Y(World *world, int cx, int cy, int cz, int radius, VoxelType leaves)
{
  plant_leaf_puff_Z(world, cx, cy, cz, radius, leaves);
}

static bool plant_tree_blob_canopy_Z(World *world, int x, int y, int ground_z, const TreeConfig *cfg)
{
  if (!world || !cfg)
    return false;
  int max_headroom = (int)world->depth - 2 - ground_z - cfg->canopy_height;
  if (max_headroom < cfg->min_trunk_height)
    return false;
  int trunk_range = cfg->max_trunk_height - cfg->min_trunk_height + 1;
  if (trunk_range < 1)
    trunk_range = 1;
  int trunk_h = cfg->min_trunk_height + (trunk_range > 1 ? seeded_rand_range(trunk_range) : 0);
  if (trunk_h > max_headroom)
    trunk_h = max_headroom;

  for (int h = 1; h <= trunk_h; h++)
  {
    int tz = ground_z + h;
    if (tz >= 0 && tz < (int)world->depth)
      world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)tz, cfg->trunk_type);
  }

  int skirt = cfg->canopy_radius / 2 + 1;
  if (skirt > trunk_h - 2)
    skirt = trunk_h > 2 ? trunk_h - 2 : 0;
  plant_canopy_ellipsoid_Z(world, x, y, ground_z + trunk_h,
                           cfg->canopy_radius, cfg->canopy_height, skirt, cfg->leaves_type);
  {
    const uint32_t h =
        (uint32_t)x * 0x9e3779b1u ^ (uint32_t)y * 0x85ebca6bu ^ (uint32_t)ground_z;
    int tx = (int)world->width / 2, ty = (int)world->height / 2;
    float weight = 0.0f;
    if (world->settlement_has_town_center)
    {
      tx = world->settlement_town_cx;
      ty = world->settlement_town_cy;
      weight = 0.45f;
    }
    const float yaw = decoration_yaw_blend(h, x, y, tx, ty, weight);
    const int r = cfg->canopy_radius + 1;
    world_paint_decoration_yaw(world, x - r, y - r, ground_z + 1, x + r, y + r,
                               ground_z + trunk_h + cfg->canopy_height + 1,
                               voxel_yaw_u8_from_radians(yaw));
  }
  return true;
}

static bool plant_tree_conifer_Z(World *world, int x, int y, int ground_z, const TreeConfig *cfg)
{
  if (!world || !cfg)
    return false;
  int max_headroom = (int)world->depth - 2 - ground_z - cfg->canopy_height - cfg->canopy_radius;
  if (max_headroom < cfg->min_trunk_height)
    return false;
  int trunk_range = cfg->max_trunk_height - cfg->min_trunk_height + 1;
  if (trunk_range < 1)
    trunk_range = 1;
  int trunk_h = cfg->min_trunk_height + (trunk_range > 1 ? seeded_rand_range(trunk_range) : 0);
  if (trunk_h > max_headroom)
    trunk_h = max_headroom;

  for (int h = 1; h <= trunk_h; h++)
  {
    int tz = ground_z + h;
    if (tz >= 0 && tz < (int)world->depth)
      world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)tz, cfg->trunk_type);
  }

  // Round-footprint cone that starts below the tip and caps the tip with needles
  int levels = cfg->canopy_height + cfg->canopy_radius;
  if (levels < 3)
    levels = 3;
  int skirt = cfg->canopy_radius / 2 + 1;
  if (skirt > trunk_h - 2)
    skirt = trunk_h > 2 ? trunk_h - 2 : 0;
  int trunk_top_z = ground_z + trunk_h;
  for (int i = 0; i < levels; i++)
  {
    int lz = trunk_top_z - skirt + i;
    int radius = (levels <= 1) ? 0 : (cfg->canopy_radius * (levels - 1 - i) + (levels - 2) / 2) / (levels - 1);
    if (radius < 0)
      radius = 0;
    const int r2 = radius * radius;
    for (int ox = -radius; ox <= radius; ox++)
    {
      for (int oy = -radius; oy <= radius; oy++)
      {
        const int d2 = ox * ox + oy * oy;
        if (d2 > r2)
          continue;
        // Soft rim
        if (radius > 0 && d2 > (r2 * 3) / 4 && seeded_rand_range(100) < 35)
          continue;
        int lx = x + ox;
        int ly = y + oy;
        if (!world_is_position_valid(world, lx, ly, lz))
          continue;
        // Preserve trunk below the tip; overwrite tip and above
        if (ox == 0 && oy == 0 && lz < trunk_top_z)
          continue;
        world_set_voxel(world, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz, cfg->leaves_type);
      }
    }
  }
  {
    int tip_z = trunk_top_z - skirt + levels;
    if (world_is_position_valid(world, x, y, tip_z))
      world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)tip_z, cfg->leaves_type);
  }
  {
    const uint32_t h =
        (uint32_t)x * 0x9e3779b1u ^ (uint32_t)y * 0x85ebca6bu ^ (uint32_t)ground_z ^ 0x51eedu;
    int tx = (int)world->width / 2, ty = (int)world->height / 2;
    float weight = 0.0f;
    if (world->settlement_has_town_center)
    {
      tx = world->settlement_town_cx;
      ty = world->settlement_town_cy;
      weight = 0.45f;
    }
    const float yaw = decoration_yaw_blend(h, x, y, tx, ty, weight);
    const int r = cfg->canopy_radius + 1;
    world_paint_decoration_yaw(world, x - r, y - r, ground_z + 1, x + r, y + r,
                               trunk_top_z + levels + 1, voxel_yaw_u8_from_radians(yaw));
  }
  return true;
}

static bool plant_tree_blob_canopy_Y(World *world, int x, int ground_y, int z, const TreeConfig *cfg)
{
  if (!world || !cfg)
    return false;
  int max_headroom = (int)world->height - 2 - ground_y - cfg->canopy_height;
  if (max_headroom < cfg->min_trunk_height)
    return false;
  int trunk_range = cfg->max_trunk_height - cfg->min_trunk_height + 1;
  if (trunk_range < 1)
    trunk_range = 1;
  int trunk_h = cfg->min_trunk_height + (trunk_range > 1 ? seeded_rand_range(trunk_range) : 0);
  if (trunk_h > max_headroom)
    trunk_h = max_headroom;

  for (int h = 1; h <= trunk_h; h++)
  {
    int ty = ground_y + h;
    if (ty >= 0 && ty < (int)world->height)
      world_set_voxel(world, (uint32_t)x, (uint32_t)ty, (uint32_t)z, cfg->trunk_type);
  }

  int skirt = cfg->canopy_radius / 2 + 1;
  if (skirt > trunk_h - 2)
    skirt = trunk_h > 2 ? trunk_h - 2 : 0;
  plant_canopy_ellipsoid_Y(world, x, ground_y + trunk_h, z,
                           cfg->canopy_radius, cfg->canopy_height, skirt, cfg->leaves_type);
  return true;
}

// Oak with branching starting mid-trunk (Z-up)
static bool plant_oak_branching_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig oak = {VOXEL_WOOD_OAK, VOXEL_LEAVES_OAK, 7, 14, 4, 6};
  if (!world)
    return false;
  int max_headroom = (int)world->depth - 2 - ground_z - oak.canopy_height;
  if (max_headroom < oak.min_trunk_height)
    return false;

  int trunk_range = oak.max_trunk_height - oak.min_trunk_height + 1;
  if (trunk_range < 1)
    trunk_range = 1;
  int trunk_h = oak.min_trunk_height + (trunk_range > 1 ? seeded_rand_range(trunk_range) : 0);
  if (trunk_h > max_headroom)
    trunk_h = max_headroom;

  // Trunk
  for (int h = 1; h <= trunk_h; h++)
  {
    int tz = ground_z + h;
    if (tz >= 0 && tz < (int)world->depth)
      world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)tz, oak.trunk_type);
  }

  // Branches from mid-trunk upwards (but below canopy)
  int branch_start = trunk_h / 2;
  if (branch_start < 4)
    branch_start = 4;
  if (trunk_h > branch_start)
  {
    for (int h = branch_start; h < trunk_h; h++)
    {
      if (seeded_rand_range(100) < 35) // ~35% chance per level to sprout one branch
      {
        int dir = seeded_rand_range(4); // 0:+X,1:-X,2:+Y,3:-Y
        int dx = (dir == 0) ? 1 : (dir == 1) ? -1
                                             : 0;
        int dy = (dir == 2) ? 1 : (dir == 3) ? -1
                                             : 0;
        int len = 2 + seeded_rand_range(3); // 2-4 voxels out
        int bx = x, by = y, bz = ground_z + h;
        for (int i = 1; i <= len; i++)
        {
          int lx = bx + dx * i;
          int ly = by + dy * i;
          if (!world_is_position_valid(world, lx, ly, bz))
            break;
          world_set_voxel(world, (uint32_t)lx, (uint32_t)ly, (uint32_t)bz, oak.trunk_type);
        }
        // Leaf puff at branch tip — spherical, and caps the tip wood
        plant_leaf_puff_Z(world, bx + dx * len, by + dy * len, bz, 2, oak.leaves_type);
      }
    }
  }

  int skirt = oak.canopy_radius / 2 + 1;
  if (skirt > trunk_h - 2)
    skirt = trunk_h > 2 ? trunk_h - 2 : 0;
  plant_canopy_ellipsoid_Z(world, x, y, ground_z + trunk_h,
                           oak.canopy_radius, oak.canopy_height, skirt, oak.leaves_type);
  return true;
}

// Oak with branching starting mid-trunk (Y-up variant)
static bool plant_oak_branching_Y(World *world, int x, int ground_y, int z)
{
  TreeConfig oak = {VOXEL_WOOD_OAK, VOXEL_LEAVES_OAK, 7, 14, 4, 6};
  if (!world)
    return false;
  int max_headroom = (int)world->height - 2 - ground_y - oak.canopy_height;
  if (max_headroom < oak.min_trunk_height)
    return false;

  int trunk_range = oak.max_trunk_height - oak.min_trunk_height + 1;
  if (trunk_range < 1)
    trunk_range = 1;
  int trunk_h = oak.min_trunk_height + (trunk_range > 1 ? seeded_rand_range(trunk_range) : 0);
  if (trunk_h > max_headroom)
    trunk_h = max_headroom;

  // Trunk along +Y
  for (int h = 1; h <= trunk_h; h++)
  {
    int ty = ground_y + h;
    if (ty >= 0 && ty < (int)world->height)
      world_set_voxel(world, (uint32_t)x, (uint32_t)ty, (uint32_t)z, oak.trunk_type);
  }

  // Branches from mid-trunk upwards
  int branch_start = trunk_h / 2;
  if (branch_start < 4)
    branch_start = 4;
  if (trunk_h > branch_start)
  {
    for (int h = branch_start; h < trunk_h; h++)
    {
      if (seeded_rand_range(100) < 35)
      {
        int dir = seeded_rand_range(4); // 0:+X,1:-X,2:+Z,3:-Z
        int dx = (dir == 0) ? 1 : (dir == 1) ? -1
                                             : 0;
        int dz = (dir == 2) ? 1 : (dir == 3) ? -1
                                             : 0;
        int len = 2 + seeded_rand_range(3); // 2-4 voxels out
        int bx = x, by = ground_y + h, bz = z;
        for (int i = 1; i <= len; i++)
        {
          int lx = bx + dx * i;
          int lz = bz + dz * i;
          if (!world_is_position_valid(world, lx, by, lz))
            break;
          world_set_voxel(world, (uint32_t)lx, (uint32_t)by, (uint32_t)lz, oak.trunk_type);
        }
        plant_leaf_puff_Y(world, bx + dx * len, by, bz + dz * len, 2, oak.leaves_type);
      }
    }
  }

  int skirt = oak.canopy_radius / 2 + 1;
  if (skirt > trunk_h - 2)
    skirt = trunk_h > 2 ? trunk_h - 2 : 0;
  plant_canopy_ellipsoid_Y(world, x, ground_y + trunk_h, z,
                           oak.canopy_radius, oak.canopy_height, skirt, oak.leaves_type);
  return true;
}

// Species wrappers
static bool world_try_plant_oak_Z(World *world, int x, int y, int ground_z)
{
  return plant_oak_branching_Z(world, x, y, ground_z);
}

static bool world_try_plant_birch_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig birch = {VOXEL_WOOD_BIRCH, VOXEL_LEAVES_BIRCH, 6, 12, 3, 5};
  return plant_tree_blob_canopy_Z(world, x, y, ground_z, &birch);
}

static bool world_try_plant_pine_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig pine = {VOXEL_WOOD_PINE, VOXEL_LEAVES_PINE, 8, 16, 4, 8};
  return plant_tree_conifer_Z(world, x, y, ground_z, &pine);
}

static bool world_try_plant_maple_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_MAPLE, VOXEL_LEAVES_MAPLE, 7, 13, 4, 6};
  return plant_tree_blob_canopy_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_beech_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_BEECH, VOXEL_LEAVES_BEECH, 6, 12, 3, 5};
  return plant_tree_blob_canopy_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_spruce_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_SPRUCE, VOXEL_LEAVES_SPRUCE, 9, 17, 4, 9};
  return plant_tree_conifer_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_juniper_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_JUNIPER, VOXEL_LEAVES_JUNIPER, 5, 10, 3, 5};
  return plant_tree_conifer_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_willow_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_WILLOW, VOXEL_LEAVES_WILLOW, 6, 12, 4, 6};
  return plant_tree_blob_canopy_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_cypress_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_CYPRESS, VOXEL_LEAVES_CYPRESS, 9, 18, 2, 8};
  return plant_tree_conifer_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_cottonwood_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_COTTONWOOD, VOXEL_LEAVES_COTTONWOOD, 8, 15, 4, 6};
  return plant_tree_blob_canopy_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_acacia_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_ACACIA, VOXEL_LEAVES_ACACIA, 5, 10, 5, 4};
  return plant_tree_blob_canopy_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_redwood_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_REDWOOD, VOXEL_LEAVES_REDWOOD, 14, 24, 4, 10};
  return plant_tree_conifer_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_elm_Z(World *world, int x, int y, int ground_z)
{
  TreeConfig cfg = {VOXEL_WOOD_ELM, VOXEL_LEAVES_ELM, 7, 14, 4, 6};
  return plant_tree_blob_canopy_Z(world, x, y, ground_z, &cfg);
}

static bool world_try_plant_biome_tree_Z(World *world, int x, int y, int ground_z,
                                        UniverseBiomeId biome, float pick)
{
  switch (biome)
  {
  case UNIVERSE_BIOME_TEMPERATE:
    if (pick < 0.30f)
      return world_try_plant_oak_Z(world, x, y, ground_z);
    if (pick < 0.55f)
      return world_try_plant_maple_Z(world, x, y, ground_z);
    if (pick < 0.75f)
      return world_try_plant_beech_Z(world, x, y, ground_z);
    return world_try_plant_birch_Z(world, x, y, ground_z);
  case UNIVERSE_BIOME_GRASSLAND:
    if (pick < 0.55f)
      return world_try_plant_oak_Z(world, x, y, ground_z);
    return world_try_plant_cottonwood_Z(world, x, y, ground_z);
  case UNIVERSE_BIOME_BOREAL:
    if (pick < 0.45f)
      return world_try_plant_spruce_Z(world, x, y, ground_z);
    if (pick < 0.80f)
      return world_try_plant_pine_Z(world, x, y, ground_z);
    return world_try_plant_juniper_Z(world, x, y, ground_z);
  case UNIVERSE_BIOME_DESERT:
    if (pick < 0.55f)
      return world_try_plant_acacia_Z(world, x, y, ground_z);
    return world_try_plant_juniper_Z(world, x, y, ground_z);
  case UNIVERSE_BIOME_WETLAND:
    if (pick < 0.40f)
      return world_try_plant_willow_Z(world, x, y, ground_z);
    if (pick < 0.75f)
      return world_try_plant_cypress_Z(world, x, y, ground_z);
    return world_try_plant_cottonwood_Z(world, x, y, ground_z);
  case UNIVERSE_BIOME_ALPINE:
    if (pick < 0.55f)
      return world_try_plant_spruce_Z(world, x, y, ground_z);
    return world_try_plant_pine_Z(world, x, y, ground_z);
  case UNIVERSE_BIOME_TROPICAL:
    if (pick < 0.25f)
      return world_try_plant_redwood_Z(world, x, y, ground_z);
    if (pick < 0.55f)
      return world_try_plant_maple_Z(world, x, y, ground_z);
    if (pick < 0.80f)
      return world_try_plant_elm_Z(world, x, y, ground_z);
    return world_try_plant_oak_Z(world, x, y, ground_z);
  case UNIVERSE_BIOME_VOLCANIC:
    if (pick < 0.55f)
      return world_try_plant_pine_Z(world, x, y, ground_z);
    return world_try_plant_juniper_Z(world, x, y, ground_z);
  default:
    return world_try_plant_oak_Z(world, x, y, ground_z);
  }
}

static bool world_try_plant_random_tree_Z(World *world, int x, int y, int ground_z)
{
  float pick = (float)seeded_rand_range(1000) / 1000.0f;
  UniverseBiomeId biome = universe_biome_primary_at(world, (uint32_t)x, (uint32_t)y);
  return world_try_plant_biome_tree_Z(world, x, y, ground_z, biome, pick);
}

static bool world_try_plant_oak_Y(World *world, int x, int ground_y, int z)
{
  return plant_oak_branching_Y(world, x, ground_y, z);
}

static bool world_try_plant_birch_Y(World *world, int x, int ground_y, int z)
{
  TreeConfig birch = {VOXEL_WOOD_BIRCH, VOXEL_LEAVES_BIRCH, 6, 12, 3, 5};
  return plant_tree_blob_canopy_Y(world, x, ground_y, z, &birch);
}

static bool world_try_plant_pine_Y(World *world, int x, int ground_y, int z)
{
  // For farm, keep trees modest relative to wilderness
  TreeConfig pine = {VOXEL_WOOD_PINE, VOXEL_LEAVES_PINE, 6, 10, 3, 4};
  return plant_tree_blob_canopy_Y(world, x, ground_y, z, &pine);
}

static bool world_try_plant_random_tree_Y(World *world, int x, int ground_y, int z)
{
  int t = seeded_rand_range(3);
  if (t == 0)
    return world_try_plant_oak_Y(world, x, ground_y, z);
  if (t == 1)
    return world_try_plant_birch_Y(world, x, ground_y, z);
  return world_try_plant_pine_Y(world, x, ground_y, z);
}

static bool home_is_grass(VoxelType type)
{
  switch (type)
  {
  case VOXEL_GRASS:
  case VOXEL_GRASS_WIDE:
  case VOXEL_GRASS_SHARP:
  case VOXEL_GRASS_CLOVER:
  case VOXEL_GRASS_MOSS:
    return true;
  default:
    return false;
  }
}

// Deterministic value noise for the home island's surface, hashed rather than Perlin so it does
// not disturb the universe-wide permutation table, and smoothstep-interpolated across a coarse
// lattice so the result reads as rolling ground instead of per-voxel speckle.
static float home_hash01(uint32_t x, uint32_t y, uint32_t salt)
{
  uint32_t h = x * 73856093u ^ y * 19349663u ^ salt * 83492791u;
  h ^= h >> 13;
  h *= 0x85ebca6bu;
  h ^= h >> 16;
  return (float)(h & 0xFFFFFFu) / (float)0xFFFFFFu;
}

static float home_surface_noise(uint32_t x, uint32_t y, uint32_t cell, uint32_t salt)
{
  const float fx = (float)x / (float)cell;
  const float fy = (float)y / (float)cell;
  const uint32_t ix = (uint32_t)floorf(fx);
  const uint32_t iy = (uint32_t)floorf(fy);

  float tx = fx - floorf(fx);
  float ty = fy - floorf(fy);
  tx = tx * tx * (3.0f - 2.0f * tx);
  ty = ty * ty * (3.0f - 2.0f * ty);

  const float v00 = home_hash01(ix, iy, salt);
  const float v10 = home_hash01(ix + 1, iy, salt);
  const float v01 = home_hash01(ix, iy + 1, salt);
  const float v11 = home_hash01(ix + 1, iy + 1, salt);

  const float a = v00 + (v10 - v00) * tx;
  const float b = v01 + (v11 - v01) * tx;
  return a + (b - a) * ty;
}

// Edge-only (wireframe) axis-aligned box. A voxel is kept when it lies on at least two faces of
// the box — i.e. on an edge or corner — so the interior and face centres stay empty.
static void home_place_wireframe_box(World *world, int x0, int y0, int z0, int sx, int sy, int sz,
                                     VoxelType type)
{
  if (!world || sx < 1 || sy < 1 || sz < 1)
    return;

  const int x1 = x0 + sx - 1;
  const int y1 = y0 + sy - 1;
  const int z1 = z0 + sz - 1;

  for (int z = z0; z <= z1; z++)
  {
    for (int y = y0; y <= y1; y++)
    {
      for (int x = x0; x <= x1; x++)
      {
        const int on_x = (x == x0 || x == x1) ? 1 : 0;
        const int on_y = (y == y0 || y == y1) ? 1 : 0;
        const int on_z = (z == z0 || z == z1) ? 1 : 0;
        if (on_x + on_y + on_z < 2)
          continue;
        if (!world_is_position_valid(world, x, y, z))
          continue;
        Voxel *voxel = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (voxel && voxel->type == VOXEL_AIR)
          world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, type);
      }
    }
  }
}

// D&D-style creature size references on the home arena: translucent wireframe boxes whose
// footprints match 5e space (one voxel ≈ one 5-ft cell) and whose heights track typical category
// maxima. Nested Medium→Gargantuan share a south-west corner so the relative volumes read at a
// glance; Tiny and Small stand beside them as 1³ markers. Kept off the spawn 9×9.
static void home_place_creature_size_wireframes(World *world, uint32_t center_x, uint32_t center_y,
                                                int surface_top, int plateau_radius)
{
  if (!world || plateau_radius < 4)
    return;

  const int base_z = surface_top + 1;
  if (base_z < 0 || base_z >= (int)world->depth)
    return;

  // South rim of the plateau. Prefer keeping the north face of the nest just outside the spawn
  // ±4 clear zone when the plateau is deep enough; otherwise the boxes share the arena.
  const int garg_fp = 4;
  const int rim_y = (int)center_y - (plateau_radius - 1);
  int nest_y = (int)center_y - 5 - (garg_fp - 1); // north face at cy-5
  if (nest_y < rim_y - (garg_fp - 1))
    nest_y = rim_y - (garg_fp - 1); // clamp: stay as far south as the plateau allows
  if (nest_y > rim_y)
    nest_y = rim_y; // plateau too small for a southward nest — sit on the rim
  // Nest origin: Gargantuan (4 wide) sits roughly on the axis, Tiny/Small just west of it.
  const int nest_x = (int)center_x - 2;

  // Tiny / Small: free-standing 1³ markers (same space in 5e; colour distinguishes them).
  home_place_wireframe_box(world, nest_x - 3, nest_y, base_z, 1, 1, 1, VOXEL_CRYSTAL);
  home_place_wireframe_box(world, nest_x - 2, nest_y, base_z, 1, 1, 1, VOXEL_CRYSTAL_GREEN);

  // Innermost first: each larger box only writes into remaining air, so shared edges keep the
  // smaller category's colour and the outer silhouette grows with size.
  static const struct
  {
    int footprint;
    int height;
    VoxelType type;
  } nested[] = {
      {1, 2, VOXEL_CRYSTAL_BLUE}, // Medium
      {2, 3, VOXEL_CRYSTAL_RED},  // Large
      {3, 4, VOXEL_GLASS},        // Huge
      {4, 6, VOXEL_ICE},          // Gargantuan
  };
  for (size_t i = 0; i < sizeof(nested) / sizeof(nested[0]); i++)
  {
    home_place_wireframe_box(world, nest_x, nest_y, base_z, nested[i].footprint, nested[i].footprint,
                             nested[i].height, nested[i].type);
  }
}

// A small hearth just north of the spawn clear-zone: stone ring, coal bed, campfire, and a candle
// on the rim so both flame emitters are present on a fresh home island.
static void home_place_campfire(World *world, uint32_t center_x, uint32_t center_y, int surface_top)
{
  if (!world || surface_top < 0 || surface_top + 1 >= (int)world->depth)
    return;

  // Outside the spawn ±4 clear zone (north), still on the flat plateau.
  const int cx = (int)center_x;
  const int cy = (int)center_y + 5;
  const int base_z = surface_top;

  if (!world_is_position_valid(world, cx, cy, base_z) ||
      !world_is_position_valid(world, cx, cy, base_z + 1))
    return;

  for (int dy = -1; dy <= 1; dy++)
  {
    for (int dx = -1; dx <= 1; dx++)
    {
      const int x = cx + dx;
      const int y = cy + dy;
      if (!world_is_position_valid(world, x, y, base_z))
        continue;
      if (dx == 0 && dy == 0)
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)base_z, VOXEL_ORE_COAL);
      else
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)base_z, VOXEL_STONE);
    }
  }

  world_set_voxel(world, (uint32_t)cx, (uint32_t)cy, (uint32_t)(base_z + 1), VOXEL_CAMPFIRE);
  world_add_voxel_condition(world, (uint32_t)cx, (uint32_t)cy, (uint32_t)(base_z + 1),
                            "BURNING_HIGH");

  // Candle on the east stone of the ring, one voxel up.
  if (world_is_position_valid(world, cx + 1, cy, base_z + 1))
  {
    world_set_voxel(world, (uint32_t)(cx + 1), (uint32_t)cy, (uint32_t)(base_z + 1), VOXEL_CANDLE);
    world_add_voxel_condition(world, (uint32_t)(cx + 1), (uint32_t)cy, (uint32_t)(base_z + 1),
                              "BURNING_MEDIUM");
  }
}

// Generate home world (island in the sky) - Built exclusively with exponential curve
static void world_generate_home(World *world, const char *seed)
{
  // Clear the world to air first
  world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                    VOXEL_AIR, BULK_OP_REPLACE, NULL, NULL);

  // Compute island parameters
  uint32_t center_x = world->width / 2;
  uint32_t center_y = world->height / 2;
  uint32_t center_z = world->depth / 2;

  // Calculate safe radius with 1-voxel border
  uint32_t max_radius = fmin(fmin(world->width, world->height), world->depth) / 2 - 1;
  uint32_t island_radius = fmin(max_radius, world->depth - 3);

  // Build the island from bottom to top using exponential curve exclusively
  // Start from the bottom and work our way up to create the semisphere
  for (uint32_t z = 0; z < center_z; z++)
  {
    // Calculate the radius at this Z level using an exponential curve
    // This creates the natural semisphere shape from bottom to top
    float z_ratio = (float)z / (float)center_z;
    float exponential_factor = z_ratio * z_ratio; // Exponential curve (z²)
    uint32_t radius_at_z = (uint32_t)(island_radius * exponential_factor);

    if (radius_at_z > 0)
    {
      // Fill a circular region at this Z level to form the semisphere
      for (uint32_t y = center_y - radius_at_z; y <= center_y + radius_at_z; y++)
      {
        for (uint32_t x = center_x - radius_at_z; x <= center_x + radius_at_z; x++)
        {
          // Check if this position is within the circular radius at this Z level
          int dx = (int)x - (int)center_x;
          int dy = (int)y - (int)center_y;
          int dist2 = dx * dx + dy * dy;

          if (dist2 <= (int)(radius_at_z * radius_at_z))
          {
            // This position is within the circle at this Z level
            Voxel *voxel = world_get_voxel(world, x, y, z);
            if (voxel)
            {
              voxel->type = VOXEL_STONE;
            }
          }
        }
      }
    }
  }

  // The core above is a solid stone paraboloid whose top is one flat disc. Left that way it
  // renders as a featureless grey plate, which is why standing on it read as being *inside* the
  // island: there was no relief, no ground cover and nothing on the surface to judge depth by.
  // What follows cuts relief into that top, lays soil and grass over the stone, and scatters
  // boulders, bushes and trees.
  //
  // There are deliberately no fluids here. The home island has no water or magma yet, so nothing
  // below places any, and no spring or fluid source is seeded.

  // Relief is cut *down* from the flat top rather than raised above it. That keeps the island's
  // silhouette, its reserved 1-voxel border and its pointed bottom exactly as the core built
  // them, and it leaves the central arena as the highest ground on the island — which is what
  // lets the player spawn on the topmost level with a one-voxel drop.
  const int surface_top = (int)center_z - 1;
  int plateau_radius = (int)island_radius / 5;
  if (plateau_radius < 6)
    plateau_radius = 6;
  const int relief_ramp = plateau_radius + 8; // blend from flat arena out to full relief
  const int max_relief = 5;

  uint32_t salt = 2166136261u;
  for (const unsigned char *p = (const unsigned char *)(seed ? seed : ""); *p; ++p)
  {
    salt ^= *p;
    salt *= 16777619u;
  }

  int *surface = (int *)malloc((size_t)world->width * world->height * sizeof(int));
  if (!surface)
    return; // island core is already built; skip the surface treatment rather than half-apply it

  // Pass 1: cut relief into the flat top, then cap each column with soil and grass.
  for (uint32_t y = 0; y < world->height; y++)
  {
    for (uint32_t x = 0; x < world->width; x++)
    {
      const size_t index = (size_t)y * world->width + x;
      surface[index] = -1;

      int top_z = -1;
      for (int z = surface_top; z >= 0; z--)
      {
        Voxel *voxel = world_get_voxel(world, x, y, (uint32_t)z);
        if (voxel && voxel->type != VOXEL_AIR)
        {
          top_z = z;
          break;
        }
      }
      if (top_z < 0)
        continue;

      const int dx = (int)x - (int)center_x;
      const int dy = (int)y - (int)center_y;
      const float radius = sqrtf((float)(dx * dx + dy * dy));

      // Full relief only past the ramp, so the arena does not end in a cliff
      float relief_scale = (radius - (float)plateau_radius) / (float)(relief_ramp - plateau_radius);
      if (relief_scale < 0.0f)
        relief_scale = 0.0f;
      if (relief_scale > 1.0f)
        relief_scale = 1.0f;

      // Two octaves: broad swells with a finer break-up on top
      const float broad = home_surface_noise(x, y, 24u, salt);
      const float fine = home_surface_noise(x, y, 9u, salt ^ 0x5bf03635u);
      const float height_noise = broad * 0.7f + fine * 0.3f;

      int drop = (int)(height_noise * (float)max_relief * relief_scale + 0.5f);
      if (drop > 0)
      {
        for (int z = top_z; z > top_z - drop && z >= 0; z--)
        {
          Voxel *voxel = world_get_voxel(world, x, y, (uint32_t)z);
          if (voxel)
            voxel->type = VOXEL_AIR;
        }
        top_z -= drop;
      }
      if (top_z < 0)
        continue;

      surface[index] = top_z;

      // Grass on top, soil under it, stone below that. Only existing stone is converted, so a
      // thin column at the rim simply becomes grass rather than growing downward.
      Voxel *cap = world_get_voxel(world, x, y, (uint32_t)top_z);
      if (cap && cap->type != VOXEL_AIR)
      {
        const float variety = home_hash01(x, y, salt ^ 0x27d4eb2fu);
        cap->type = (variety < 0.60f) ? VOXEL_GRASS
                    : (variety < 0.85f) ? VOXEL_GRASS_WIDE
                                        : VOXEL_GRASS_CLOVER;
      }

      const int soil_depth = 3;
      for (int z = top_z - 1; z >= 0 && z > top_z - 1 - soil_depth; z--)
      {
        Voxel *voxel = world_get_voxel(world, x, y, (uint32_t)z);
        if (!voxel || voxel->type == VOXEL_AIR)
          break;
        voxel->type = (home_hash01(x, y, (uint32_t)z ^ salt) < 0.5f) ? VOXEL_SOIL : VOXEL_SOIL_LOAM;
      }
    }
  }

  // Pass 2: scatter boulders, bushes and trees over the finished surface.
  //
  // Decorations are placed by their centre but occupy a footprint around it — a boulder is a
  // couple of voxels across and a canopy wider still — so the keep-out radius has to exceed the
  // arena by that footprint. Gating on plateau_radius alone let boulders just outside the arena
  // overhang into it, which is exactly the kind of thing gravity would drop the player onto.
  const int max_decoration_reach = 4;
  const int decoration_keep_out = plateau_radius + max_decoration_reach;

  for (uint32_t y = 0; y < world->height; y++)
  {
    for (uint32_t x = 0; x < world->width; x++)
    {
      const int top_z = surface[(size_t)y * world->width + x];
      if (top_z < 0)
        continue;

      const int dx = (int)x - (int)center_x;
      const int dy = (int)y - (int)center_y;
      if (dx * dx + dy * dy <= decoration_keep_out * decoration_keep_out)
        continue;

      Voxel *ground = world_get_voxel(world, x, y, (uint32_t)top_z);
      if (!ground || !home_is_grass(ground->type))
        continue;
      if (top_z + 1 >= (int)world->depth)
        continue;

      // Boulders: a low dome of harder rock. Each column of the dome is measured from its own
      // surface height rather than from the centre's, so the boulder drapes over sloping ground
      // instead of cutting into it as a flat slab would — which would also bury a neighbour's
      // soil band under rock.
      if (home_surface_noise(x, y, 13u, salt ^ 0x9e3779b9u) > 0.88f && seeded_rand_range(100) < 12)
      {
        const VoxelType rock = (home_hash01(x, y, salt ^ 0x1b873593u) < 0.5f)
                                   ? VOXEL_STONE_GRANITE
                                   : VOXEL_STONE_BASALT;
        const int radius = 1 + seeded_rand_range(2);
        for (int oy = -radius; oy <= radius; oy++)
        {
          for (int ox = -radius; ox <= radius; ox++)
          {
            const int horizontal2 = ox * ox + oy * oy;
            if (horizontal2 > radius * radius)
              continue;

            const int bx = (int)x + ox, by = (int)y + oy;
            if (bx < 0 || by < 0 || bx >= (int)world->width || by >= (int)world->height)
              continue;

            const int base = surface[(size_t)by * world->width + bx];
            if (base < 0)
              continue; // that column is off the island

            const int dome = (int)(sqrtf((float)(radius * radius - horizontal2)) + 0.5f);
            for (int oz = 0; oz <= dome; oz++)
            {
              if (!world_is_position_valid(world, bx, by, base + oz))
                continue;
              const bool crumbly =
                  home_hash01((uint32_t)bx, (uint32_t)by, salt ^ (uint32_t)(oz + 1)) < 0.22f;
              world_set_voxel(world, (uint32_t)bx, (uint32_t)by, (uint32_t)(base + oz),
                              crumbly ? VOXEL_GRAVEL : rock);
            }
          }
        }
        continue;
      }

      // Trees, clustered so they form copses on open grass rather than closing into a canopy
      if (home_surface_noise(x, y, 18u, salt ^ 0x165667b1u) > 0.74f && seeded_rand_range(100) < 9)
      {
        const float kind = home_hash01(x, y, salt ^ 0x2545f491u);
        if (kind < 0.34f)
          world_try_plant_oak_Z(world, (int)x, (int)y, top_z);
        else if (kind < 0.67f)
          world_try_plant_birch_Z(world, (int)x, (int)y, top_z);
        else
          world_try_plant_pine_Z(world, (int)x, (int)y, top_z);
        continue;
      }

      // Bushes fill in around the copses and are the most common cover
      if (home_surface_noise(x, y, 11u, salt ^ 0x7feb352du) > 0.52f && seeded_rand_range(100) < 14)
      {
        static const VoxelType bushes[] = {
            VOXEL_BUSH, VOXEL_BUSH_FERN, VOXEL_BUSH_VINES,
            VOXEL_BUSH_BLUEBERRY, VOXEL_BUSH_BLACKBERRY, VOXEL_BUSH_RASPBERRY};
        const int pick = seeded_rand_range((int)(sizeof(bushes) / sizeof(bushes[0])));
        world_set_voxel(world, x, y, (uint32_t)(top_z + 1), bushes[pick]);
        {
          Voxel *bv = world_get_voxel(world, x, y, (uint32_t)(top_z + 1));
          if (bv)
          {
            const uint32_t h = (uint32_t)x * 0x9e3779b1u ^ (uint32_t)y * 0x85ebca6bu;
            // Home island: face the camp center with a strong random component.
            voxel_set_yaw_radians(
                bv, decoration_yaw_blend(h, (int)x, (int)y, (int)center_x, (int)center_y, 0.4f));
          }
        }
        continue;
      }

      // Tall grass over whatever open ground is left. It can be this dense — far denser than the
      // bushes — precisely because it does not block movement: a meadow the player walks through
      // rather than around. Placed last so it fills the gaps between the other decorations instead
      // of competing with them for a column.
      if (home_surface_noise(x, y, 7u, salt ^ 0x27d4eb2fu) > 0.38f && seeded_rand_range(100) < 45)
      {
        world_set_voxel(world, x, y, (uint32_t)(top_z + 1), VOXEL_GRASS_TALL);
        Voxel *gv = world_get_voxel(world, x, y, (uint32_t)(top_z + 1));
        if (gv)
        {
          const uint32_t h = (uint32_t)x * 0x27d4eb2fu ^ (uint32_t)y * 0x165667b1u;
          voxel_set_yaw_radians(gv, decoration_yaw_blend(h, (int)x, (int)y, 0, 0, 0.0f));
        }
      }
    }
  }

  // Size-reference wireframes sit on the flat arena after decorations so trees/boulders cannot
  // claim those columns, and before mobs so inhabitants do not spawn inside the boxes.
  home_place_creature_size_wireframes(world, center_x, center_y, surface_top, plateau_radius);
  home_place_campfire(world, center_x, center_y, surface_top);

  free(surface);

  world_spawn_home_mobs(world);
}

// Arena generator: Refactored to use bulk operations
// 1) Fill the lower half (z < depth/2) with limestone (VOXEL_STONE_LIMESTONE).
// 2) Carve a centered lower hemisphere (semi-sphere) with radius = min(width,height,depth)/4 at world center.
static void world_generate_arena(World *world, const char *seed)
{
  (void)seed;
  if (!world || !world->voxels)
    return;

  // Clear everything to AIR first
  world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                    VOXEL_AIR, BULK_OP_REPLACE, NULL, NULL);

  const uint32_t half_z = world->depth / 2u;

  // Fill bottom half with limestone
  world_fill_region(world, 0, 0, 0, world->width, world->height, half_z,
                    VOXEL_STONE_LIMESTONE, BULK_OP_REPLACE, NULL, NULL);

  // Carve a centered lower hemisphere using half-sphere subtractive operation
  const uint32_t center_x = world->width / 2;
  const uint32_t center_y = world->height / 2;
  const uint32_t center_z = world->depth / 2;
  const uint32_t radius = fmin(fmin(world->width, world->height), world->depth) / 4;

  // Use subtractive mode to carve out the hemisphere
  world_fill_half_sphere(world, center_x, center_y, center_z, radius,
                         VOXEL_AIR, false, 1.0f, BULK_OP_SUBTRACTIVE, NULL, NULL);
}

// Generate farm world (32x32x32 with soil and grass) - Refactored to use bulk operations
static void world_generate_farm(World *world, const char *seed)
{
  (void)seed; // Suppress unused parameter warning
  printf("[DEBUG] world_generate_farm called for world %p\n", (void *)world);

  // Clear the world to air first
  world_fill_region(world, 0, 0, 0, world->width, world->height, world->depth,
                    VOXEL_AIR, BULK_OP_REPLACE, NULL, NULL);

  // Create layered terrain using bulk operations
  VoxelType layer_types[] = {VOXEL_BEDROCK, VOXEL_STONE, VOXEL_STONE, VOXEL_SOIL, VOXEL_SOIL};
  float layer_heights[] = {0.0f, 0.3f, 0.45f, 0.5f, 0.5f};
  uint32_t layer_count = 5;

  // Convert percentage heights to absolute heights
  float height_f = (float)world->height;
  float layer_heights_abs[] = {
      0.0f,             // Bedrock at bottom
      0.3f * height_f,  // Stone layer 1
      0.45f * height_f, // Stone layer 2
      0.5f * height_f,  // Dirt layer
      0.5f * height_f   // Soil layer
  };

  world_fill_layered_terrain(world, 0, 0, 0, world->width, world->height, world->depth,
                             layer_types, layer_heights_abs, layer_count,
                             BULK_OP_REPLACE, NULL, NULL);

  // Add grass patches on the top layer using noise pattern
  uint32_t top_y = (uint32_t)(world->height * 0.5);
  world_apply_noise_pattern(world, 0, top_y, 0, world->width, 1, world->depth,
                            VOXEL_GRASS, 0.1f, 0.7f, BULK_OP_MASKED,
                            voxel_filter_type, (void *)&(VoxelType){VOXEL_SOIL});

  // Add trees identical to home world generator
  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t x = 0; x < world->width; x++)
    {
      // Find the top block in this column
      int top_y = -1;
      for (uint32_t y = 0; y < world->height; y++)
      {
        Voxel *voxel = world_get_voxel(world, x, y, z);
        if (voxel && voxel->type != VOXEL_AIR)
        {
          top_y = y;
        }
      }

      // Add some trees with a low probability
      if (top_y >= 0 && seeded_rand_range(100) < 3)
      {
        Voxel *top_voxel = world_get_voxel(world, x, top_y, z);
        if (top_voxel && top_voxel->type == VOXEL_GRASS)
        {
          world_try_plant_random_tree_Y(world, (int)x, (int)top_y, (int)z);
        }
      }
    }
  }
}

static void world_generate_random(World *world, const char *seed)
{
  // Start with farm generation
  world_generate_farm(world, seed);

  // Generate loot table based on world level
  // Level 0: Common only (5% chance)
  // Level 1: Common (5% chance) and Rare (5% chance)
  // Level 2: Common (5% chance), Rare (5% chance), and Uncommon (5% chance)
  // Level 3: Common (5% chance), Rare (5% chance), Uncommon (5% chance), and Epic (5% chance)
  // Level 4: Common (5% chance), Rare (5% chance), Uncommon (5% chance), Epic (5% chance), and Legendary (5% chance)
  // Additions based on player level:
  // Player level 1: Common (1% chance)
  // Player level 2: Common (1% chance), Rare (1% chance)
  // Player level 3: Common (1% chance), Rare (1% chance), Uncommon (1% chance)
  // Player level 4: Common (1% chance), Rare (1% chance), Uncommon (1% chance), Epic (1% chance)
  // Player level 5: Common (1% chance), Rare (1% chance), Uncommon (1% chance), Epic (1% chance), Legendary (1% chance)
  // Player level 6: Common (1% chance), Rare (1% chance), Uncommon (1% chance), Epic (1% chance), Legendary (1% chance), Mythic (1% chance)
  // Player level 7: Common (1% chance), Rare (1% chance), Uncommon (1% chance), Epic (1% chance), Legendary (1% chance), Mythic (1% chance), Divine (1% chance)
  // Player level 8: Common (1% chance), Rare (1% chance), Uncommon (1% chance), Epic (1% chance), Legendary (1% chance), Mythic (1% chance), Divine (1% chance), Celestial (1% chance)

  // Rarity Table
  // Generate exponential rarities across 4 levels
  // Level 0: "Epic" (0% of worlds have 1 spring)
  // Level 1: "Very Rare" (6.25% of worlds have 1 spring)
  // Level 2: "Rare" (12.5% of worlds have 1 spring)
  // Level 3: "Uncommon" (25% of worlds have 1 spring)
  // Level 4: "Common" (50% of worlds have 1 spring)
  // Maximum 3 springs per world total (reject world and regenerate)
  // Springs are ONLY placed at bedrock level (y=0)

  int springs_placed = 0;
  const int max_springs = 3;

  // Calculate world hash for deterministic spring placement
  uint8_t world_hash[32];
  calculate_sha256(seed, world_hash);
  uint32_t world_hash_int = *(uint32_t *)world_hash;

  // Update world's score and level before spring generation
  world_update_score_and_level(world);

  // Get the world's computed level
  double world_level = world_get_level(world);

  // Calculate rarity based on world level
  // Higher levels = higher rarity thresholds = more springs
  // Map world level to rarity percentage (0-100%)
  double rarity_percentage = 0.0;
  if (world_level >= 4.0)
  {
    rarity_percentage = 50.0; // Common (50%)
  }
  else if (world_level >= 3.0)
  {
    rarity_percentage = 25.0; // Uncommon (25%)
  }
  else if (world_level >= 2.0)
  {
    rarity_percentage = 12.5; // Rare (12.5%)
  }
  else if (world_level >= 1.0)
  {
    rarity_percentage = 6.25; // Very Rare (6.25%)
  }
  else
  {
    rarity_percentage = 0.0; // Epic (0%) - brand new worlds
  }

  // Convert hash to percentage (0-100)
  double hash_percentage = (double)(world_hash_int & 0xFFFF) / 65535.0 * 100.0;

  // Check if this world should have springs based on its level
  if (hash_percentage < rarity_percentage)
  {
    // Find a suitable location for the spring at bedrock level
    int attempts = 0;
    const int max_attempts = 100;

    while (springs_placed < max_springs && attempts < max_attempts)
    {
      // Generate position using deterministic hash
      uint32_t pos_hash = world_hash_int ^ (attempts * 0x87654321);
      uint32_t x = pos_hash % world->width;
      uint32_t z = (pos_hash >> 8) % world->depth;
      uint32_t y = 0; // Bedrock level only

      // Check if position is suitable (bedrock level and no existing spring nearby)
      bool suitable = true;

      // Check for existing springs nearby (within 3 blocks)
      for (int dx = -3; dx <= 3; dx++)
      {
        for (int dz = -3; dz <= 3; dz++)
        {
          int check_x = x + dx;
          int check_z = z + dz;

          if (world_is_position_valid(world, check_x, y, check_z))
          {
            Voxel *check_voxel = world_get_voxel(world, check_x, y, check_z);
            if (check_voxel && check_voxel->type == VOXEL_SPRING)
            {
              suitable = false;
              break;
            }
          }
        }
        if (!suitable)
          break;
      }

      if (suitable)
      {
        // Place spring at bedrock level
        world_set_voxel(world, x, y, z, VOXEL_SPRING);
        springs_placed++;

        // Log spring placement with world level info
        char log_message[256];
        snprintf(log_message, sizeof(log_message),
                 "Placed spring at bedrock level (%u, %u, %u) - World Level %.2f, Rarity %.1f%%",
                 x, y, z, world_level, rarity_percentage);
        world_append_log(world, log_message);
        break;
      }

      attempts++;
    }
  }

  // Log final spring count with world level info
  char log_message[256];
  snprintf(log_message, sizeof(log_message),
           "RANDOM world generated with %d springs at bedrock level (max %d) - World Level %.2f, Score %.2f",
           springs_placed, max_springs, world_level, world->score);
  world_append_log(world, log_message);
}

// Universe-stable lattice coordinate so adjacent wilderness cells share decoration noise.
static inline uint32_t wilderness_global_x(const World *world, uint32_t x)
{
  return (uint32_t)(world->universe_x * (uint64_t)world->width + (uint64_t)x);
}

static inline uint32_t wilderness_global_y(const World *world, uint32_t y)
{
  return (uint32_t)(world->universe_y * (uint64_t)world->height + (uint64_t)y);
}

// Universe-grid salt only — per-cell derived seeds must not enter decoration so
// adjacent wilderness faces share the same surface field.
static uint32_t wilderness_surface_salt(const World *world, const char *seed)
{
  (void)seed;
  uint32_t salt = 2166136261u;
  salt ^= (uint32_t)world->universe_x * 73856093u;
  salt ^= (uint32_t)world->universe_y * 19349663u;
  salt ^= (uint32_t)world->universe_z * 83492791u;
  salt *= 16777619u;
  return salt;
}

// Max absolute height delta to orthogonal neighbours. Steep faces stay bare rock.
static int wilderness_column_relief(const int *tops, uint32_t W, uint32_t H, uint32_t x, uint32_t y)
{
  const int c = tops[(size_t)y * W + x];
  if (c < 0)
    return 99;
  int max_delta = 0;
  static const int ox[4] = {1, -1, 0, 0};
  static const int oy[4] = {0, 0, 1, -1};
  for (int i = 0; i < 4; i++)
  {
    const int nx = (int)x + ox[i];
    const int ny = (int)y + oy[i];
    if (nx < 0 || ny < 0 || nx >= (int)W || ny >= (int)H)
      continue;
    const int n = tops[(size_t)ny * W + (size_t)nx];
    if (n < 0)
      continue;
    const int d = abs(c - n);
    if (d > max_delta)
      max_delta = d;
  }
  return max_delta;
}

static bool wilderness_is_grass_cap(VoxelType type)
{
  return type == VOXEL_GRASS || type == VOXEL_GRASS_WIDE || type == VOXEL_GRASS_CLOVER ||
         type == VOXEL_GRASS_SHARP || type == VOXEL_GRASS_MOSS;
}

// Cap geological stone with grass and a shallow soil band. Steep rock stays exposed.
static void wilderness_apply_surface_cap(World *world, int *tops, uint32_t salt)
{
  if (!world || !tops)
    return;

  const uint32_t W = world->width;
  const uint32_t H = world->height;
  const int soil_depth = 3;

  for (uint32_t y = 0; y < H; y++)
  {
    for (uint32_t x = 0; x < W; x++)
    {
      const size_t index = (size_t)y * W + x;
      int top_z = tops[index];
      if (top_z < 0)
        continue;

      // Leave cliffs and sharp ridges as bare stone so geology still reads.
      if (wilderness_column_relief(tops, W, H, x, y) > 3)
        continue;

      const uint32_t gx = wilderness_global_x(world, x);
      const uint32_t gy = wilderness_global_y(world, y);

      Voxel *cap = world_get_voxel(world, x, y, (uint32_t)top_z);
      if (!cap || cap->type == VOXEL_AIR || cap->type == VOXEL_WATER ||
          cap->type == VOXEL_BEDROCK)
        continue;

      float elev = (world->depth > 1) ? (float)top_z / (float)(world->depth - 1) : 0.5f;
      UniverseBiomeSample biome = universe_biome_at(world, x, y, elev);
      float bare_chance = biome.weights[UNIVERSE_BIOME_DESERT] * 0.55f +
                          biome.weights[UNIVERSE_BIOME_VOLCANIC] * 0.45f +
                          biome.weights[UNIVERSE_BIOME_ALPINE] * 0.25f;
      if (home_hash01(gx, gy, salt ^ 0x0f0f0f0fu) < bare_chance)
        continue;

      const VoxelType under_stone = cap->type;
      const float variety = home_hash01(gx, gy, salt ^ 0x27d4eb2fu);

      // Biome grass palettes
      if (biome.primary == UNIVERSE_BIOME_BOREAL || biome.primary == UNIVERSE_BIOME_ALPINE)
        cap->type = (variety < 0.55f) ? VOXEL_GRASS_MOSS : VOXEL_GRASS_SHARP;
      else if (biome.primary == UNIVERSE_BIOME_DESERT)
        cap->type = (variety < 0.70f) ? VOXEL_GRASS_SHARP : VOXEL_SAND;
      else if (biome.primary == UNIVERSE_BIOME_WETLAND)
        cap->type = (variety < 0.50f) ? VOXEL_GRASS_CLOVER : VOXEL_GRASS_MOSS;
      else if (biome.primary == UNIVERSE_BIOME_GRASSLAND)
        cap->type = (variety < 0.65f) ? VOXEL_GRASS_WIDE : VOXEL_GRASS;
      else if (biome.primary == UNIVERSE_BIOME_VOLCANIC)
        cap->type = (variety < 0.60f) ? VOXEL_GRASS_MOSS : VOXEL_GRASS_SHARP;
      else if (biome.primary == UNIVERSE_BIOME_TROPICAL)
        cap->type = (variety < 0.45f) ? VOXEL_GRASS_CLOVER : VOXEL_GRASS_WIDE;
      else
        cap->type = (variety < 0.55f)   ? VOXEL_GRASS
                    : (variety < 0.80f) ? VOXEL_GRASS_WIDE
                                        : VOXEL_GRASS_CLOVER;

      for (int z = top_z - 1; z >= 0 && z > top_z - 1 - soil_depth; z--)
      {
        Voxel *voxel = world_get_voxel(world, x, y, (uint32_t)z);
        if (!voxel || voxel->type == VOXEL_AIR || voxel->type == VOXEL_BEDROCK ||
            voxel->type == VOXEL_WATER)
          break;
        if (wilderness_is_grass_cap(voxel->type))
          break;
        if (under_stone == VOXEL_STONE_BASALT || under_stone == VOXEL_STONE_GRANITE ||
            under_stone == VOXEL_STONE_LIMESTONE || under_stone == VOXEL_STONE_SANDSTONE)
          voxel->type = select_soil_type_for_stone(world, x, y, (uint32_t)z, under_stone);
        else if (biome.primary == UNIVERSE_BIOME_WETLAND)
          voxel->type = (home_hash01(gx, gy, (uint32_t)z ^ salt) < 0.55f) ? VOXEL_SOIL_CLAY
                                                                            : VOXEL_SOIL_SILT;
        else
          voxel->type =
              (home_hash01(gx, gy, (uint32_t)z ^ salt) < 0.5f) ? VOXEL_SOIL : VOXEL_SOIL_LOAM;
      }
    }
  }
}

// Scatter boulders, tree copses, bushes and tall grass over the capped surface.
static void decorate_wilderness_surface(World *world, const int *tops, uint32_t salt)
{
  if (!world || !tops)
    return;

  const uint32_t W = world->width;
  const uint32_t H = world->height;

  for (uint32_t y = 0; y < H; y++)
  {
    for (uint32_t x = 0; x < W; x++)
    {
      const int top_z = tops[(size_t)y * W + x];
      if (top_z < 0)
        continue;

      Voxel *cap = world_get_voxel(world, x, y, (uint32_t)top_z);
      if (!cap)
        continue;
      // Decorations prefer living ground; desert sand still takes sparse shrubs.
      const bool living = wilderness_is_grass_cap(cap->type);
      const bool desert_sand = (cap->type == VOXEL_SAND);
      if (!living && !desert_sand)
        continue;

      if (top_z + 1 >= (int)world->depth)
        continue;

      const uint32_t gx = wilderness_global_x(world, x);
      const uint32_t gy = wilderness_global_y(world, y);
      float elev = (world->depth > 1) ? (float)top_z / (float)(world->depth - 1) : 0.5f;
      UniverseBiomeSample biome = universe_biome_at(world, x, y, elev);
      UniverseBiomeId primary = biome.primary;

      float boulder_thresh = 0.90f;
      float boulder_chance = 0.10f;
      float tree_noise_min = 0.78f;
      float tree_chance = 0.07f;
      float bush_chance = 0.11f;
      float tall_chance = 0.38f;
      if (primary == UNIVERSE_BIOME_TEMPERATE)
      {
        tree_noise_min = 0.72f;
        tree_chance = 0.12f;
      }
      else if (primary == UNIVERSE_BIOME_GRASSLAND)
      {
        tree_chance = 0.025f;
        tall_chance = 0.55f;
        bush_chance = 0.06f;
      }
      else if (primary == UNIVERSE_BIOME_BOREAL)
      {
        tree_noise_min = 0.74f;
        tree_chance = 0.09f;
        tall_chance = 0.22f;
      }
      else if (primary == UNIVERSE_BIOME_DESERT)
      {
        tree_chance = 0.008f;
        bush_chance = 0.05f;
        tall_chance = 0.05f;
        boulder_chance = 0.14f;
      }
      else if (primary == UNIVERSE_BIOME_WETLAND)
      {
        tree_chance = 0.08f;
        bush_chance = 0.14f;
        tall_chance = 0.42f;
      }
      else if (primary == UNIVERSE_BIOME_ALPINE)
      {
        tree_chance = 0.03f;
        bush_chance = 0.07f;
        tall_chance = 0.12f;
        boulder_thresh = 0.86f;
        boulder_chance = 0.16f;
      }
      else if (primary == UNIVERSE_BIOME_TROPICAL)
      {
        tree_noise_min = 0.68f;
        tree_chance = 0.16f;
        bush_chance = 0.15f;
        tall_chance = 0.30f;
      }
      else if (primary == UNIVERSE_BIOME_VOLCANIC)
      {
        tree_chance = 0.015f;
        bush_chance = 0.05f;
        tall_chance = 0.08f;
        boulder_thresh = 0.84f;
        boulder_chance = 0.20f;
      }

      // Boulders — sparse rock piles on noisier columns
      if (home_surface_noise(gx, gy, 13u, salt ^ 0x9e3779b9u) > boulder_thresh &&
          home_hash01(gx, gy, salt ^ 0xa5a5a5a5u) < boulder_chance)
      {
        const VoxelType rock =
            (primary == UNIVERSE_BIOME_VOLCANIC ||
             home_hash01(gx, gy, salt ^ 0x1b873593u) < 0.5f)
                ? VOXEL_STONE_BASALT
                : VOXEL_STONE_GRANITE;
        const int radius = 1 + (int)(home_hash01(gx, gy, salt ^ 0x3c6ef372u) * 2.0f);
        for (int oy = -radius; oy <= radius; oy++)
        {
          for (int ox = -radius; ox <= radius; ox++)
          {
            const int horizontal2 = ox * ox + oy * oy;
            if (horizontal2 > radius * radius)
              continue;

            const int bx = (int)x + ox, by = (int)y + oy;
            if (bx < 0 || by < 0 || bx >= (int)W || by >= (int)H)
              continue;

            const int base = tops[(size_t)by * W + (size_t)bx];
            if (base < 0)
              continue;

            const int dome = (int)(sqrtf((float)(radius * radius - horizontal2)) + 0.5f);
            for (int oz = 0; oz <= dome; oz++)
            {
              if (!world_is_position_valid(world, bx, by, base + oz))
                continue;
              const bool crumbly =
                  home_hash01((uint32_t)bx, (uint32_t)by, salt ^ (uint32_t)(oz + 1)) < 0.22f;
              world_set_voxel(world, (uint32_t)bx, (uint32_t)by, (uint32_t)(base + oz),
                              crumbly ? VOXEL_GRAVEL : rock);
            }
          }
        }
        continue;
      }

      if (!living && primary != UNIVERSE_BIOME_DESERT)
        continue;

      // Trees in loose copses
      if (home_surface_noise(gx, gy, 18u, salt ^ 0x165667b1u) > tree_noise_min &&
          home_hash01(gx, gy, salt ^ 0xc2b2ae35u) < tree_chance)
      {
        const float kind = home_hash01(gx, gy, salt ^ 0x2545f491u);
        world_try_plant_biome_tree_Z(world, (int)x, (int)y, top_z, primary, kind);
        continue;
      }

      // Bushes around the copses
      if (home_surface_noise(gx, gy, 11u, salt ^ 0x7feb352du) > 0.55f &&
          home_hash01(gx, gy, salt ^ 0x27d4eb2fu) < bush_chance)
      {
        VoxelType bush = VOXEL_BUSH;
        const float bp = home_hash01(gx, gy, salt ^ 0x165667b1u);
        if (primary == UNIVERSE_BIOME_DESERT || primary == UNIVERSE_BIOME_VOLCANIC ||
            primary == UNIVERSE_BIOME_ALPINE)
          bush = VOXEL_BUSH_THORNS;
        else if (primary == UNIVERSE_BIOME_WETLAND || primary == UNIVERSE_BIOME_TROPICAL)
          bush = (bp < 0.5f) ? VOXEL_BUSH_VINES : VOXEL_BUSH_FERN;
        else if (primary == UNIVERSE_BIOME_BOREAL)
          bush = (bp < 0.5f) ? VOXEL_BUSH_FERN : VOXEL_BUSH_THORNS;
        else if (bp < 0.25f)
          bush = VOXEL_BUSH_BLUEBERRY;
        else if (bp < 0.45f)
          bush = VOXEL_BUSH_BLACKBERRY;
        else if (bp < 0.65f)
          bush = VOXEL_BUSH_RASPBERRY;
        else if (bp < 0.80f)
          bush = VOXEL_BUSH_FERN;
        else
          bush = VOXEL_BUSH;
        world_set_voxel(world, x, y, (uint32_t)(top_z + 1), bush);
        {
          Voxel *bv = world_get_voxel(world, x, y, (uint32_t)(top_z + 1));
          if (bv)
          {
            int tx = (int)world->width / 2, ty = (int)world->height / 2;
            float weight = 0.0f;
            if (world->settlement_has_town_center)
            {
              tx = world->settlement_town_cx;
              ty = world->settlement_town_cy;
              weight = 0.55f;
            }
            const uint32_t h = (uint32_t)(home_hash01(gx, gy, salt ^ 0xa5a5a5a5u) * 65535.0f);
            voxel_set_yaw_radians(bv, decoration_yaw_blend(h, (int)x, (int)y, tx, ty, weight));
          }
        }
        continue;
      }

      // Tall grass fills open meadow gaps; does not block movement
      if (living && home_surface_noise(gx, gy, 7u, salt ^ 0x27d4eb2fu) > 0.42f &&
          home_hash01(gx, gy, salt ^ 0x85ebca6bu) < tall_chance)
      {
        world_set_voxel(world, x, y, (uint32_t)(top_z + 1), VOXEL_GRASS_TALL);
        Voxel *gv = world_get_voxel(world, x, y, (uint32_t)(top_z + 1));
        if (gv)
        {
          const uint32_t h = (uint32_t)(home_hash01(gx, gy, salt ^ 0x11111111u) * 65535.0f);
          voxel_set_yaw_radians(gv, decoration_yaw_blend(h, (int)x, (int)y, 0, 0, 0.0f));
        }
      }
    }
  }
}

// Generate wilderness world: geological strata, then living surface (soil/grass/plants) and fauna.
static void world_generate_wilderness(World *world, const char *seed)
{
  printf("[DEBUG] world_generate_wilderness called for world %p\n", (void *)world);
  int *tops = NULL;
  uint32_t surface_cap = 0, clay_cap_layers = 0;
  world_generate_scoured_base(world, /*fill_basalt=*/true, &tops, &surface_cap, &clay_cap_layers);
  (void)surface_cap;
  (void)clay_cap_layers;

  if (tops)
  {
    const uint32_t salt = wilderness_surface_salt(world, seed);
    wilderness_apply_surface_cap(world, tops, salt);
    // Accelerated rain cycles carve surface rivers and ponds before plants/roads land.
    water_erosion_simulate_weather(world, salt ^ 0x51eed01eu, tops);
    // Groundwater saturates permeable stone/sand/soil from free water and climate moisture.
    water_table_build(world, tops, salt ^ 0x7ab1e001u);
    decorate_wilderness_surface(world, tops, salt);
    // Occasional lightning-shaped magma vent from bedrock to the living surface.
    wilderness_stamp_volcano(world, tops, salt ^ 0x701c4a07u);
    free(tops);
    tops = NULL;
  }

  // Pave roads before callers place fauna so stand spots sit on the finished surface.
  settlement_stamp_roads(world);
  universe_biome_apply_ambient_weather(world);
}

// Generate the "scoured" world: clear to AIR, bedrock floor at z=0, then apply wilderness magma without basalt fill
static void world_generate_scoured(World *world, const char *seed)
{
  (void)seed;
  printf("[DEBUG] world_generate_scoured called for world %p\n", (void *)world);
  // Explicitly build the shared baseline only
  world_generate_scoured_base(world, /*fill_basalt=*/true, /*out_top_of_column=*/NULL, /*out_surface_cap=*/NULL, /*out_clay_cap_layers=*/NULL);
}

// Generate a simple square labyrinth (maze) at z=1 with deterministic entrances
static void world_generate_labyrinth_square(World *world, int num_entrances)
{
  if (!world || world->depth <= 1)
    return;
  const uint32_t W = world->width, H = world->height;
  const uint32_t z = 1;
  // Attempt up to N regenerations until a path from center to an entrance is found
  const int max_attempts = 64;
  bool placed = false;
  uint32_t cx = W / 2, cy = H / 2;
  uint32_t xmin = 1, ymin = 1;
  uint32_t xmax = (W > 2 ? W - 2 : 0);
  uint32_t ymax = (H > 2 ? H - 2 : 0);
  if (xmax <= xmin || ymax <= ymin)
    return;

  for (int attempt = 0; attempt < max_attempts && !placed; attempt++)
  {
    // Clear plane
    for (uint32_t y = 0; y < H; y++)
      for (uint32_t x = 0; x < W; x++)
        world_set_voxel(world, x, y, z, VOXEL_AIR);

    // Place outer ring walls
    for (uint32_t x = xmin; x <= xmax; x++)
    {
      world_set_voxel(world, x, ymin, z, VOXEL_BEDROCK);
      world_set_voxel(world, x, ymax, z, VOXEL_BEDROCK);
    }
    for (uint32_t y = ymin; y <= ymax; y++)
    {
      world_set_voxel(world, xmin, y, z, VOXEL_BEDROCK);
      world_set_voxel(world, xmax, y, z, VOXEL_BEDROCK);
    }

    // Deterministic entrances distributed on sides; vary with attempt
    if (num_entrances < 1)
      num_entrances = 1;
    if (num_entrances > 8)
      num_entrances = 8;
    unsigned int r = world->rng_state ^ (0x9E3779B1u * (unsigned int)attempt);

    // If a preferred direction was set (via seed tag), force a single entrance on that side.
    // Otherwise, fall back to uniformly distributed entrances.
    int forced_side = s_labyrinth_forced_side; // 0=NEG_X,1=POS_X,2=NEG_Y,3=POS_Y, -1 unset
    int entrance_iterations = (forced_side >= 0 ? 1 : num_entrances);
    for (int i = 0; i < entrance_iterations; i++)
    {
      int side;
      if (forced_side >= 0)
      {
        side = forced_side;
      }
      else
      {
        r = r * 1664525u + 1013904223u;
        side = (int)(r % 4u);
      }
      r = r * 1664525u + 1013904223u;
      uint32_t span = (side < 2 ? (xmax - xmin - 2 + 1) : (ymax - ymin - 2 + 1));
      uint32_t pos = (span > 0) ? (uint32_t)(2 + (r % span)) : 2u;
      uint32_t ex = xmin, ey = ymin;
      if (side == 0)
      {
        ex = xmin;
        ey = ymin + pos;
      }
      else if (side == 1)
      {
        ex = xmax;
        ey = ymin + pos;
      }
      else if (side == 2)
      {
        ex = xmin + pos;
        ey = ymin;
      }
      else
      {
        ex = xmin + pos;
        ey = ymax;
      }
      world_set_voxel(world, ex, ey, z, VOXEL_AIR);
    }

    // Carve grid walls (checkerboard aisles) leaving a 3x3 clearing at center
    for (uint32_t y = ymin + 1; y < ymax; y++)
    {
      for (uint32_t x = xmin + 1; x < xmax; x++)
      {
        if (x >= cx - 1 && x <= cx + 1 && y >= cy - 1 && y <= cy + 1)
          continue;
        if (((x - xmin) % 2 == 0) || ((y - ymin) % 2 == 0))
          world_set_voxel(world, x, y, z, VOXEL_BEDROCK);
      }
    }

    // March connections: open occasional gaps between blocks to create corridors
    for (uint32_t y = ymin + 2; y + 2 <= ymax; y += 2)
    {
      for (uint32_t x = xmin + 2; x + 2 <= xmax; x += 2)
      {
        r = r * 1664525u + 1013904223u;
        int dir = (int)(r % 4u);
        uint32_t gx = x, gy = y;
        if (dir == 0 && x + 1 <= xmax)
          gx = x + 1;
        else if (dir == 1 && x > xmin + 1)
          gx = x - 1;
        else if (dir == 2 && y + 1 <= ymax)
          gy = y + 1;
        else if (dir == 3 && y > ymin + 1)
          gy = y - 1;
        world_set_voxel(world, gx, gy, z, VOXEL_AIR);
      }
    }

    // Ensure 3x3 center clearing
    for (int dy = -1; dy <= 1; dy++)
    {
      int yy = (int)cy + dy;
      if (yy < 0 || yy >= (int)H)
        continue;
      for (int dx = -1; dx <= 1; dx++)
      {
        int xx = (int)cx + dx;
        if (xx < 0 || xx >= (int)W)
          continue;
        world_set_voxel(world, (uint32_t)xx, (uint32_t)yy, z, VOXEL_AIR);
      }
    }

    // Prepare pathfinding for single path
    bool passable[VOXEL_COUNT] = {false};
    passable[VOXEL_AIR] = true;
    passable[VOXEL_SAND] = false;

    uint32_t targets[8][2];
    int tcount = 0;
    for (uint32_t x = xmin; x <= xmax; x++)
    {
      Voxel *v1 = world_get_voxel(world, x, ymin, z);
      Voxel *v2 = world_get_voxel(world, x, ymax, z);
      if (v1 && v1->type == VOXEL_AIR && tcount < 8)
      {
        targets[tcount][0] = x;
        targets[tcount][1] = ymin;
        tcount++;
      }
      if (v2 && v2->type == VOXEL_AIR && tcount < 8)
      {
        targets[tcount][0] = x;
        targets[tcount][1] = ymax;
        tcount++;
      }
    }
    for (uint32_t y = ymin; y <= ymax; y++)
    {
      Voxel *v1 = world_get_voxel(world, xmin, y, z);
      Voxel *v2 = world_get_voxel(world, xmax, y, z);
      if (v1 && v1->type == VOXEL_AIR && tcount < 8)
      {
        targets[tcount][0] = xmin;
        targets[tcount][1] = y;
        tcount++;
      }
      if (v2 && v2->type == VOXEL_AIR && tcount < 8)
      {
        targets[tcount][0] = xmax;
        targets[tcount][1] = y;
        tcount++;
      }
    }

    uint32_t start_x = cx, start_y = cy;
    Voxel *sv = world_get_voxel(world, start_x, start_y, z);
    if (!(sv && sv->type == VOXEL_AIR) || tcount == 0)
      continue; // regenerate

    // One-path-per-world guard
    static const World *s_last_world_for_labyrinth = NULL;
    static int s_paths_done_in_world = 0;
    if (s_last_world_for_labyrinth != (const World *)world)
    {
      s_last_world_for_labyrinth = (const World *)world;
      s_paths_done_in_world = 0;
    }
    if (s_paths_done_in_world >= 1)
      break; // already placed for this world

    // Closest entrance selection
    int best_i = -1;
    uint32_t best_d2 = 0xFFFFFFFFu;
    for (int i = 0; i < tcount; i++)
    {
      int dx = (int)targets[i][0] - (int)cx;
      int dy = (int)targets[i][1] - (int)cy;
      uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
      if (best_i < 0 || d2 < best_d2)
      {
        best_i = i;
        best_d2 = d2;
      }
    }
    if (best_i < 0)
      continue;

    VoxelCoord *path = NULL;
    size_t path_len = 0;
    if (world_find_path_2d(world, z, start_x, start_y,
                           targets[best_i][0], targets[best_i][1],
                           passable, &path, &path_len) &&
        path && path_len > 0)
    {
      for (size_t k = 0; k < path_len; k++)
      {
        world_set_voxel((World *)world, path[k].x, path[k].y, path[k].z, VOXEL_SAND);
      }
      free(path);
      s_paths_done_in_world++;
      placed = true;
      break;
    }
    if (path)
      free(path);
    // Otherwise, loop to regenerate with a different attempt seed
  }
}

// Update springs to generate water when voxel above is removed (with 5-second cooldown)
void world_update_springs(World *world, uint64_t current_time)
{
  if (!world || world->generation_type != WORLD_TYPE_RANDOM)
    return;

  // Find all springs and check if they should generate water
  for (uint32_t z = 0; z < world->depth; z++)
  {
    for (uint32_t x = 0; x < world->width; x++)
    {
      // Only check at bedrock level (y=0) where springs are placed
      uint32_t y = 0;
      Voxel *spring_voxel = world_get_voxel(world, x, y, z);
      if (spring_voxel && spring_voxel->type == VOXEL_SPRING)
      {
        // Check if the voxel above the spring is air (removed)
        if (world_is_position_valid(world, x, y + 1, z))
        {
          Voxel *above_voxel = world_get_voxel(world, x, y + 1, z);
          if (above_voxel && above_voxel->type == VOXEL_AIR)
          {
            // Check if 5 seconds have passed since last water generation
            static uint64_t last_water_generation = 0;
            const uint64_t water_cooldown = 5000000; // 5 seconds in microseconds

            if (current_time - last_water_generation >= water_cooldown)
            {
              // Voxel above was removed and cooldown expired, generate water
              world_generate_spring_water(world, x, y, z);
              last_water_generation = current_time;
            }
          }
        }
      }
    }
  }
}

// Forward declarations for local helpers
static bool apply_wetness_for_water(World *world, int start_x, int start_y, int start_z);
static bool find_downhill_next_step(World *world,
                                    int start_x, int start_y, int z,
                                    int max_radius,
                                    int *out_x, int *out_y);

// Find the lowest z-plane (minimum z) that is connected to the start voxel by a
// 26-neighborhood voxel path within a bounded local radius. The path may traverse
// AIR or WATER voxels only (solid voxels block). Returns the lowest reachable z,
// or -1 if no valid traversal occurs. This does not mutate world state.
int world_find_lowest_connected_plane(World *world,
                                      int start_x, int start_y, int start_z,
                                      int max_radius)
{
  if (!world)
    return -1;
  const int R = (max_radius > 0 ? max_radius : 8);
  const int W = R * 2 + 1;
  // Bound array size to a safe static cube (<= 31^3)
  if (W > 31)
    return -1;
  const int C = W * W * W;
  unsigned char visited[31 * 31 * 31];
  int qx[31 * 31 * 31];
  int qy[31 * 31 * 31];
  int qz[31 * 31 * 31];
// Helper for index mapping
#define IDX_L(x, y, z) (((z) * W + (y)) * W + (x))
  for (int i = 0; i < C; i++)
    visited[i] = 0;

// Only traverse AIR or WATER
#define PASSABLE_CELL(wx, wy, wz) (                                                   \
    world_is_position_valid(world, (uint32_t)(wx), (uint32_t)(wy), (uint32_t)(wz)) && \
    ({ Voxel* __pv = world_get_voxel(world, (uint32_t)(wx), (uint32_t)(wy), (uint32_t)(wz)); \
       __pv && (__pv->type == VOXEL_AIR || __pv->type == VOXEL_WATER); }))

  // 26-neighborhood offsets
  int ndirs[26][3];
  {
    int k = 0;
    for (int dz = -1; dz <= 1; dz++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
          if (dx == 0 && dy == 0 && dz == 0)
            continue;
          ndirs[k][0] = dx;
          ndirs[k][1] = dy;
          ndirs[k][2] = dz;
          k++;
        }
  }

  // Seed if start is traversable
  if (!PASSABLE_CELL(start_x, start_y, start_z))
    return -1;
  int qs = 0, qe = 0;
  int sx = start_x, sy = start_y, sz = start_z;
  int ox = sx - R, oy = sy - R, oz = sz - R;
  int lx = sx - ox, ly = sy - oy, lz = sz - oz;
  if (lx < 0 || ly < 0 || lz < 0 || lx >= W || ly >= W || lz >= W)
    return -1;
  visited[IDX_L(lx, ly, lz)] = 1;
  qx[qe] = sx;
  qy[qe] = sy;
  qz[qe] = sz;
  qe++;

  int lowest_z = sz;
  while (qs < qe)
  {
    int cx = qx[qs], cy = qy[qs], cz = qz[qs];
    qs++;
    if (cz < lowest_z)
      lowest_z = cz;
    for (int i = 0; i < 26; i++)
    {
      int nx = cx + ndirs[i][0];
      int ny = cy + ndirs[i][1];
      int nz = cz + ndirs[i][2];
      int lx = nx - ox, ly = ny - oy, lz = nz - oz;
      if (lx < 0 || ly < 0 || lz < 0 || lx >= W || ly >= W || lz >= W)
        continue;
      int id = IDX_L(lx, ly, lz);
      if (visited[id])
        continue;
      if (!PASSABLE_CELL(nx, ny, nz))
        continue;
      visited[id] = 1;
      qx[qe] = nx;
      qy[qe] = ny;
      qz[qe] = nz;
      qe++;
    }
  }
  return lowest_z;
}

// Simple local path search on the same Z to find nearest AIR cell whose below is AIR
// Returns the first step cell (absolute coordinates) to move into, or false if none found
static bool find_downhill_next_step(World *world,
                                    int start_x, int start_y, int z,
                                    int max_radius,
                                    int *out_x, int *out_y)
{
  if (!world || !out_x || !out_y)
    return false;
  const int R = (max_radius > 1 ? max_radius : 1);
  const int W = R * 2 + 1;
  // Small fixed-size stacks for visited and queue
  unsigned char visited[33 * 33]; // supports R up to 16 safely
  int parent_x[33 * 33];
  int parent_y[33 * 33];
  int qx[33 * 33];
  int qy[33 * 33];
  if (W > 33)
    return false; // cap for safety
  for (int i = 0; i < W * W; i++)
  {
    visited[i] = 0;
    parent_x[i] = -1;
    parent_y[i] = -1;
  }
  int cx0 = start_x;
  int cy0 = start_y;
  // Seed queue with immediate AIR neighbors (first step must be AIR)
  int qs = 0, qe = 0;
  const int ndirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int i = 0; i < 4; i++)
  {
    int nx = cx0 + ndirs[i][0];
    int ny = cy0 + ndirs[i][1];
    int ox = nx - cx0 + R;
    int oy = ny - cy0 + R;
    if (ox < 0 || oy < 0 || ox >= W || oy >= W)
      continue;
    size_t idx = (size_t)oy * (size_t)W + (size_t)ox;
    if (visited[idx])
      continue;
    if (!world_is_position_valid(world, nx, ny, z))
      continue;
    Voxel *vv = world_get_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)z);
    if (!vv || vv->type != VOXEL_AIR)
      continue;
    visited[idx] = 1;
    parent_x[idx] = cx0;
    parent_y[idx] = cy0;
    qx[qe] = nx;
    qy[qe] = ny;
    qe++;
  }
  while (qs < qe)
  {
    int cx = qx[qs];
    int cy = qy[qs];
    qs++;
    // If below is AIR, we found a drain candidate
    if (z - 1 >= 0 && world_is_position_valid(world, cx, cy, z - 1))
    {
      Voxel *down = world_get_voxel(world, (uint32_t)cx, (uint32_t)cy, (uint32_t)(z - 1));
      if (down && down->type == VOXEL_AIR)
      {
        // Reconstruct first step from (start_x,start_y) to (cx,cy)
        int tx = cx, ty = cy;
        // Walk back until parent is the start or none
        while (!(tx == cx0 && ty == cy0))
        {
          int ox = tx - cx0 + R;
          int oy = ty - cy0 + R;
          size_t idx = (size_t)oy * (size_t)W + (size_t)ox;
          int ptx = parent_x[idx];
          int pty = parent_y[idx];
          if (ptx == cx0 && pty == cy0)
          {
            *out_x = tx;
            *out_y = ty;
            return true;
          }
          tx = ptx;
          ty = pty;
          if (ptx == -1 && pty == -1)
            break; // safety
        }
        // If we didn't find parent chain, fallback to immediate step if any
        *out_x = cx;
        *out_y = cy;
        return true;
      }
    }
    // Expand neighbors on same plane through AIR only
    for (int i = 0; i < 4; i++)
    {
      int nx = cx + ndirs[i][0];
      int ny = cy + ndirs[i][1];
      int ox = nx - cx0 + R;
      int oy = ny - cy0 + R;
      if (ox < 0 || oy < 0 || ox >= W || oy >= W)
        continue;
      size_t idx = (size_t)oy * (size_t)W + (size_t)ox;
      if (visited[idx])
        continue;
      if (!world_is_position_valid(world, nx, ny, z))
        continue;
      Voxel *vv = world_get_voxel(world, (uint32_t)nx, (uint32_t)ny, (uint32_t)z);
      if (!vv || vv->type != VOXEL_AIR)
        continue;
      visited[idx] = 1;
      parent_x[idx] = cx;
      parent_y[idx] = cy;
      qx[qe] = nx;
      qy[qe] = ny;
      qe++;
    }
  }
  return false;
}

void world_step_fluids(World *world, int max_cells)
{
  // The rules and the cost model both live in fluid_sim.c now. What used to be here swept the
  // whole voxel array three times per tick and moved one unit of fluid per cell, which made a
  // step cost the same on a dry world as a flooded one and took a hundred ticks to empty a
  // bucket. See fluid_sim.h.
  fluid_sim_step(world, max_cells);
}

// Generate water from a spring (only when voxel above is air)
static void world_generate_spring_water(World *world, uint32_t x, uint32_t y, uint32_t z)
{
  // Check if spring is already full of water
  Voxel *spring_voxel = world_get_voxel(world, x, y, z);
  if (spring_voxel && spring_voxel->type == VOXEL_WATER)
  {
    // Spring is full, try to push water vertically upward
    world_push_spring_water(world, x, y, z);
  }
  else if (spring_voxel && spring_voxel->type == VOXEL_SPRING)
  {
    // Spring is empty, fill it with water. world_set_voxel sets the level to a full cube.
    world_set_voxel(world, x, y, z, VOXEL_WATER);
  }
}

// Push water from a full spring to adjacent positions
static void world_push_spring_water(World *world, uint32_t x, uint32_t y, uint32_t z)
{
  // Create deterministic random sequence based on world state
  uint32_t hash_input = x * 73856093 + y * 19349663 + z * 83492791 + world->vector_clock;
  uint32_t hash = hash_input ^ (hash_input >> 13);
  hash = hash ^ (hash << 17);
  hash = hash ^ (hash >> 5);

  // Try positions in order: below, diagonal down, horizontal, vertical
  int directions[][3] = {
      {0, -1, 0}, // Below
      {-1, -1, -1},
      {1, -1, -1},
      {-1, -1, 1},
      {1, -1, 1}, // Diagonal down
      {-1, 0, 0},
      {1, 0, 0},
      {0, 0, -1},
      {0, 0, 1}, // Horizontal
      {0, 1, 0}  // Above (but don't go up)
  };

  for (int i = 0; i < 10; i++)
  {
    int new_x = x + directions[i][0];
    int new_y = y + directions[i][1];
    int new_z = z + directions[i][2];

    if (world_is_position_valid(world, new_x, new_y, new_z))
    {
      Voxel *target_voxel = world_get_voxel(world, new_x, new_y, new_z);
      if (target_voxel && target_voxel->type == VOXEL_AIR)
      {
        // Found empty space, place water
        world_set_voxel(world, new_x, new_y, new_z, VOXEL_WATER);
        return;
      }
    }
  }
}

// Blend from a neighbor world across the shared face into dst up to max_depth voxels
// dirx/diry/dirz specify which neighbor direction the src lies in relative to dst.
// Exactly one of dirx,diry,dirz must be -1 or +1, others 0.
static void world_blend_from_neighbor_face(World *dst,
                                           const World *src,
                                           int dirx, int diry, int dirz,
                                           int max_depth)
{
  if (!dst || !src || max_depth <= 0)
    return;
  int axis = (dirx != 0) + (diry != 0) + (dirz != 0);
  if (axis != 1)
    return; // only face-adjacent

  const uint32_t W = dst->width, H = dst->height, D = dst->depth;
  const uint32_t SW = src->width, SH = src->height, SD = src->depth;
  // For each face, choose two spanning axes (u,v) and the marching axis (d)
  // Also map to src face slice index on the touching side
  if (dirx != 0)
  {
    // Shared plane spans y (0..min-1) and z (0..min-1)
    uint32_t max_y = (H < SH ? H : SH);
    uint32_t max_z = (D < SD ? D : SD);
    int start_x = (dirx > 0 ? 0 : (int)W - 1);
    int step_x = (dirx > 0 ? +1 : -1);
    uint32_t src_x = (dirx > 0 ? SW - 1 : 0);
    for (uint32_t y = 0; y < max_y; y++)
    {
      for (uint32_t z = 0; z < max_z; z++)
      {
        const Voxel *sv = world_get_voxel((World *)src, src_x, y, z);
        if (!sv || sv->type == VOXEL_AIR)
          continue;
        for (int d = 0; d < max_depth; d++)
        {
          int x = start_x + step_x * d;
          if (x < 0 || x >= (int)W)
            break;
          Voxel *dv = world_get_voxel(dst, (uint32_t)x, y, z);
          if (!dv)
            break;
          // Deterministic scatter with decay by depth using pure 4D noise
          float fdecay = expf(-(float)d / 2.0f); // ~e^{-d/2}
          float u = (float)perlin_noise(0.0, (float)x * 0.087f, (float)y * 0.087f, (float)z * 0.071f + 31.0f);
          if (u < 0.35f * fdecay)
          {
            dv->type = sv->type;
          }
        }
      }
    }
    return;
  }
  if (diry != 0)
  {
    uint32_t max_x = (W < SW ? W : SW);
    uint32_t max_z = (D < SD ? D : SD);
    int start_y = (diry > 0 ? 0 : (int)H - 1);
    int step_y = (diry > 0 ? +1 : -1);
    uint32_t src_y = (diry > 0 ? SH - 1 : 0);
    for (uint32_t x = 0; x < max_x; x++)
    {
      for (uint32_t z = 0; z < max_z; z++)
      {
        const Voxel *sv = world_get_voxel((World *)src, x, src_y, z);
        if (!sv || sv->type == VOXEL_AIR)
          continue;
        for (int d = 0; d < max_depth; d++)
        {
          int y = start_y + step_y * d;
          if (y < 0 || y >= (int)H)
            break;
          Voxel *dv = world_get_voxel(dst, x, (uint32_t)y, z);
          if (!dv)
            break;
          float fdecay = expf(-(float)d / 2.0f);
          float u = (float)perlin_noise(0.0, (float)x * 0.087f, (float)y * 0.087f, (float)z * 0.071f + 53.0f);
          if (u < 0.35f * fdecay)
          {
            dv->type = sv->type;
          }
        }
      }
    }
    return;
  }
  if (dirz != 0)
  {
    uint32_t max_x = (W < SW ? W : SW);
    uint32_t max_y = (H < SH ? H : SH);
    int start_z = (dirz > 0 ? 0 : (int)D - 1);
    int step_z = (dirz > 0 ? +1 : -1);
    uint32_t src_z = (dirz > 0 ? SD - 1 : 0);
    for (uint32_t y = 0; y < max_y; y++)
    {
      for (uint32_t x = 0; x < max_x; x++)
      {
        const Voxel *sv = world_get_voxel((World *)src, x, y, src_z);
        if (!sv || sv->type == VOXEL_AIR)
          continue;
        for (int d = 0; d < max_depth; d++)
        {
          int z = start_z + step_z * d;
          if (z < 0 || z >= (int)D)
            break;
          Voxel *dv = world_get_voxel(dst, x, y, (uint32_t)z);
          if (!dv)
            break;
          float fdecay = expf(-(float)d / 2.0f);
          float u = (float)perlin_noise(0.0, (float)x * 0.087f, (float)y * 0.087f, (float)z * 0.071f + 71.0f);
          if (u < 0.35f * fdecay)
          {
            dv->type = sv->type;
          }
        }
      }
    }
    return;
  }
}

void world_blend_adjacent_face(World *dst, const World *src, int dirx, int diry, int dirz, int max_depth)
{
  world_blend_from_neighbor_face(dst, src, dirx, diry, dirz, max_depth);
}

// Apply a single-step wetness to the cell directly below water. No propagation.
// Clamps wetness to a maximum of 6 and never traverses through blocks.
static bool apply_wetness_for_water(World *world, int start_x, int start_y, int start_z)
{
  if (!world)
    return false;
  int tz = start_z - 1;
  if (tz < 0)
    return false;
  if (!world_is_position_valid(world, start_x, start_y, tz))
    return false;
  Voxel *tv = world_get_voxel(world, (uint32_t)start_x, (uint32_t)start_y, (uint32_t)tz);
  if (!tv)
    return false;
  uint8_t wet = voxel_get_quantity_or_wetness(tv);
  if (wet < 6)
  {
    voxel_set_quantity_or_wetness(tv, (uint8_t)(wet + 1));
  }
  // Do not continue beyond this block; no lateral/vertical propagation here.
  return false;
}

// Global progress callback
static LoadingProgressCallback g_progress_callback = NULL;

void game_worlds_set_progress_callback(LoadingProgressCallback callback)
{
  g_progress_callback = callback;
}

// Generate a seed for a specific world coordinate
char *world_generate_seed_for_coord(const char *base_seed, WorldCoord coord)
{
  char *seed = malloc(128);
  if (!seed)
    return NULL;

  snprintf(seed, 128, "%s_%d_%d_%d", base_seed, coord.x, coord.y, coord.z);
  return seed;
}

// Create a new game worlds structure
GameWorlds *game_worlds_create(const char *character_name)
{
  GameWorlds *game_worlds = malloc(sizeof(GameWorlds));
  if (!game_worlds)
    return NULL;

  // Initialize all pointers to NULL
  game_worlds->home_world = NULL;
  game_worlds->farm_world = NULL;
  game_worlds->wilderness_world = NULL;
  game_worlds->random_teleport_world = NULL;
  for (int i = 0; i < 26; i++)
  {
    game_worlds->adjacent_farm_worlds[i] = NULL;
    game_worlds->adjacent_home_worlds[i] = NULL;
  }

  // Generate base seed from character name
  uint8_t hash[32];
  calculate_sha256(character_name, hash);

  game_worlds->base_seed = malloc(65);
  if (game_worlds->base_seed)
  {
    for (int i = 0; i < 32; i++)
    {
      sprintf(game_worlds->base_seed + (i * 2), "%02x", hash[i]);
    }
    game_worlds->base_seed[64] = '\0';
  }

  // Generate random seed for teleport world
  game_worlds->random_seed = malloc(65);
  if (game_worlds->random_seed)
  {
    // Use character name + "random" for different seed
    char random_input[128];
    snprintf(random_input, sizeof(random_input), "%s_random", character_name);
    calculate_sha256(random_input, hash);
    for (int i = 0; i < 32; i++)
    {
      sprintf(game_worlds->random_seed + (i * 2), "%02x", hash[i]);
    }
    game_worlds->random_seed[64] = '\0';
  }

  game_worlds->total_worlds = 1 + 1 + 1 + 26 + 26 + 1; // wilderness + home + farm + adjacents + random

  return game_worlds;
}

// Destroy game worlds structure
void game_worlds_destroy(GameWorlds *game_worlds)
{
  if (!game_worlds)
    return;

  if (game_worlds->home_world)
    world_destroy(game_worlds->home_world);
  if (game_worlds->farm_world)
    world_destroy(game_worlds->farm_world);
  if (game_worlds->random_teleport_world)
    world_destroy(game_worlds->random_teleport_world);

  for (int i = 0; i < 26; i++)
  {
    if (game_worlds->adjacent_farm_worlds[i])
      world_destroy(game_worlds->adjacent_farm_worlds[i]);
    if (game_worlds->adjacent_home_worlds[i])
      world_destroy(game_worlds->adjacent_home_worlds[i]);
  }

  if (game_worlds->base_seed)
    free(game_worlds->base_seed);
  if (game_worlds->random_seed)
    free(game_worlds->random_seed);

  free(game_worlds);
}

// Generate all worlds for a new game
bool game_worlds_generate_all(GameWorlds *game_worlds, const char *character_name)
{
  if (!game_worlds || !character_name)
    return false;

  int current_world = 0;
  int total_worlds = game_worlds->total_worlds;

  // 1. Generate home world
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Generating home world...");
  game_worlds->home_world = world_create(WORLD_SIZE_CUBE);
  if (game_worlds->home_world)
  {
    world_generate_with_type(game_worlds->home_world, game_worlds->base_seed, WORLD_TYPE_HOME);
  }

  // 2. Generate farm world
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Generating farm world...");
  game_worlds->farm_world = world_create(32, 32, 32);
  if (game_worlds->farm_world)
  {
    world_generate_with_type(game_worlds->farm_world, game_worlds->base_seed, WORLD_TYPE_FARM);
  }

  // 3. Generate adjacent farm worlds (3x3x3 cube minus center = 26 worlds)
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Generating adjacent farm worlds...");
  int adjacent_index = 0;
  for (int x = -1; x <= 1; x++)
  {
    for (int y = -1; y <= 1; y++)
    {
      for (int z = -1; z <= 1; z++)
      {
        if (x == 0 && y == 0 && z == 0)
          continue; // Skip center (main farm world)

        WorldCoord coord = {x, y, z};
        char *seed = world_generate_seed_for_coord(game_worlds->base_seed, coord);
        if (seed)
        {
          game_worlds->adjacent_farm_worlds[adjacent_index] = world_create(32, 32, 32);
          if (game_worlds->adjacent_farm_worlds[adjacent_index])
          {
            // Inherit base level from parent farm world
            uint32_t parent_base_level = world_get_base_level(game_worlds->farm_world);
            world_set_base_level(game_worlds->adjacent_farm_worlds[adjacent_index], parent_base_level);
            world_generate_with_type(game_worlds->adjacent_farm_worlds[adjacent_index], seed, WORLD_TYPE_FARM);
          }
          free(seed);
          adjacent_index++;
        }
      }
    }
  }

  // 4. Generate adjacent empty home worlds
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Generating adjacent home worlds...");
  adjacent_index = 0;
  for (int x = -1; x <= 1; x++)
  {
    for (int y = -1; y <= 1; y++)
    {
      for (int z = -1; z <= 1; z++)
      {
        if (x == 0 && y == 0 && z == 0)
          continue; // Skip center (main home world)

        WorldCoord coord = {x, y, z};
        char *seed = world_generate_seed_for_coord(game_worlds->base_seed, coord);
        if (seed)
        {
          game_worlds->adjacent_home_worlds[adjacent_index] = world_create(WORLD_SIZE_CUBE);
          if (game_worlds->adjacent_home_worlds[adjacent_index])
          {
            // Inherit base level from parent home world
            uint32_t parent_base_level = world_get_base_level(game_worlds->home_world);
            world_set_base_level(game_worlds->adjacent_home_worlds[adjacent_index], parent_base_level);
            world_generate_with_type(game_worlds->adjacent_home_worlds[adjacent_index], seed, WORLD_TYPE_HOME);
          }
          free(seed);
          adjacent_index++;
        }
      }
    }
  }

  // 5. Generate random teleport world
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Generating random teleport world...");
  game_worlds->random_teleport_world = world_create(WORLD_SIZE_CUBE);
  if (game_worlds->random_teleport_world)
  {
    world_generate_with_type(game_worlds->random_teleport_world, game_worlds->random_seed, WORLD_TYPE_HOME);
    // TODO: Add spawn table for random actors
  }

  // TODO: generate "universe map" of all worlds connected to the teleport world,
  // joined with the graph of the home world and all connected worlds.

  if (g_progress_callback)
    g_progress_callback(total_worlds, total_worlds, "World generation complete!");

  return true;
}

// Save all worlds
bool game_worlds_save_all(GameWorlds *game_worlds, const char *character_name)
{
  if (!game_worlds || !character_name)
    return false;

  int current_world = 0;
  int total_worlds = game_worlds->total_worlds;

  // Ensure worlds directory exists
  system("mkdir -p worlds");

  // 1. Save home world
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Saving home world...");
  if (game_worlds->home_world)
  {
    world_save_by_seed(game_worlds->home_world, game_worlds->base_seed);
  }

  // 2. Save farm world
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Saving farm world...");
  if (game_worlds->farm_world)
  {
    world_save_by_seed(game_worlds->farm_world, game_worlds->base_seed);
  }

  // 3. Save adjacent farm worlds
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Saving adjacent farm worlds...");
  for (int i = 0; i < 26; i++)
  {
    if (game_worlds->adjacent_farm_worlds[i])
    {
      WorldCoord coord;
      // Calculate coordinate from index (this is a simplified mapping)
      coord.x = (i % 3) - 1;
      coord.y = ((i / 3) % 3) - 1;
      coord.z = (i / 9) - 1;

      char *seed = world_generate_seed_for_coord(game_worlds->base_seed, coord);
      if (seed)
      {
        world_save_by_seed(game_worlds->adjacent_farm_worlds[i], seed);
        free(seed);
      }
    }
  }

  // 4. Save adjacent home worlds
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Saving adjacent home worlds...");
  for (int i = 0; i < 26; i++)
  {
    if (game_worlds->adjacent_home_worlds[i])
    {
      WorldCoord coord;
      coord.x = (i % 3) - 1;
      coord.y = ((i / 3) % 3) - 1;
      coord.z = (i / 9) - 1;

      char *seed = world_generate_seed_for_coord(game_worlds->base_seed, coord);
      if (seed)
      {
        world_save_by_seed(game_worlds->adjacent_home_worlds[i], seed);
        free(seed);
      }
    }
  }

  // 5. Save random teleport world
  if (g_progress_callback)
    g_progress_callback(current_world++, total_worlds, "Saving random teleport world...");
  if (game_worlds->random_teleport_world)
  {
    world_save_by_seed(game_worlds->random_teleport_world, game_worlds->random_seed);
  }

  if (g_progress_callback)
    g_progress_callback(total_worlds, total_worlds, "All worlds saved successfully!");

  return true;
}

// World level system implementation
void world_update_score_and_level(World *world)
{
  if (!world)
    return;

  // Calculate score: ln(vector_clock) + ln(history_event_count) + ln(unique_player_count) + base_level
  double score = 0.0;

  // Add ln(vector_clock) - handle case where vector_clock is 0
  if (world->vector_clock > 0)
  {
    score += log((double)world->vector_clock);
  }

  // Add ln(history_event_count) - handle case where count is 0
  if (world->history_event_count > 0)
  {
    score += log((double)world->history_event_count);
  }

  // Add ln(unique_player_count) - handle case where count is 0
  if (world->unique_player_count > 0)
  {
    score += log((double)world->unique_player_count);
  }

  // Add base_level
  score += (double)world->base_level;

  world->score = score;

  // Calculate level: ln(score) - handle case where score is 0 or negative
  if (score > 0)
  {
    world->level = log(score);
  }
  else
  {
    world->level = 0.0; // Default to level 0 for new worlds
  }
}

double world_get_level(World *world)
{
  if (!world)
    return 0.0;
  return world->level;
}

double world_get_score(World *world)
{
  if (!world)
    return 0.0;
  return world->score;
}

uint32_t world_get_base_level(World *world)
{
  if (!world)
    return 0;
  return world->base_level;
}

void world_set_base_level(World *world, uint32_t base_level)
{
  if (!world)
    return;
  world->base_level = base_level;
  world_update_score_and_level(world); // Recalculate score and level
}

void world_increment_history_event(World *world)
{
  if (!world)
    return;
  world->history_event_count++;
  world_update_score_and_level(world); // Recalculate score and level
}

void world_add_unique_player(World *world, const char *player_id)
{
  if (!world || !player_id)
    return;

  // For now, we'll just increment the count
  // In a full implementation, you'd want to track actual unique player IDs
  // This is a simplified version that assumes each call represents a unique player
  world->unique_player_count++;
  world_update_score_and_level(world); // Recalculate score and level
}
