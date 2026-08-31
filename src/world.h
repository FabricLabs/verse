#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

// Forward declaration to avoid circular includes
typedef struct GpuVoxelBuffer GpuVoxelBuffer;
typedef struct WorldParticleEffects WorldParticleEffects;
typedef struct FluidSim FluidSim;

// Include universe context to avoid circular dependency
#include "universe_context.h"
#include "household.h"

/*
  0: { name: 'AIR', color: 0x000000, opacity: 0.0 },
  1: { name: 'DIRT', color: 0x8B4513, opacity: 1.0 },
  2: { name: 'GRASS', color: 0x7CFC00, opacity: 1.0 },
  3: { name: 'STONE', color: 0x808080, opacity: 1.0 },
  4: { name: 'WATER', color: 0x1E90FF, opacity: 0.8 },
  5: { name: 'WOOD', color: 0x8B4513, opacity: 1.0 },
  6: { name: 'LEAVES', color: 0x228B22, opacity: 0.9 },
  7: { name: 'SAND', color: 0xF4A460, opacity: 1.0 }
*/

// Include canonical VoxelType definition from voxel.h
#include "voxel.h"

// Physical properties are now defined in voxel.h

// Voxel structure is now defined in voxel.h

// Gravity is a gameplay acceleration in voxels/s², not SI metres. Treating 6cm voxels as
// Earth gravity made a one-voxel jump last a tenth of a second; these defaults keep jump
// and fall timing in the platformer range, and each world can override them.
#define GRAVITY_DEFAULT 20.0f
#define GRAVITY_MIN 2.0f
#define GRAVITY_MAX 80.0f
#define GRAVITY_STEP 1.0f

// World generation types
typedef enum
{
  WORLD_TYPE_HOME,       // Island in the sky (current generator)
  WORLD_TYPE_FARM,       // 32x32x32 farm with soil and grass
  WORLD_TYPE_RANDOM,     // Farm + springs with water generation
  WORLD_TYPE_WILDERNESS, // New: layered wilderness with rarity-based goodies
  WORLD_TYPE_SOLID,      // Fill entire world with a single voxel type (see world_generate_solid_fill)
  WORLD_TYPE_UNDERWORLD, // Cave system with bedrock ceiling/floor, stalactites/stalagmites
  WORLD_TYPE_SCOURED,    // Single bedrock plane at z=0; everything else AIR
  WORLD_TYPE_LABYRINTH_SQUARE, // Maze on z=1 with bedrock walls, 3x3 center clearing
  WORLD_TYPE_WFC_TOWN,   // Wilderness-plane settlement (scale 1..9); terrain is wilderness + buildings
  WORLD_TYPE_CLOUD,      // Sparse flattened ellipsoid clouds of VOXEL_STEAM
  WORLD_TYPE_ARENA       // Bottom-half limestone with a carved centered hemispherical bowl
} WorldGenerationType;

// Whether a world holds anything for the fluid simulation to move.
typedef enum
{
  WORLD_FLUID_UNKNOWN = 0, // not yet established; a census will settle it
  WORLD_FLUID_NONE,        // definitely no fluid and no fluid source
  WORLD_FLUID_SOME         // fluid or a spring is present, or may be
} WorldFluidPresence;

// World structure
typedef struct World
{
  uint16_t version;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  float gravity;    // Gravity in voxels/s² (per-world, seeded then editable)
  float rarity;     // 0..1 global rarity budget influencing nice block frequency
  char seed_id[65]; // Hex seed identifier (up to 64 chars) for deterministic per-world features
  char *log;        // Text-based log stored with world save
  Voxel *voxels;
  WorldGenerationType generation_type;
  uint64_t vector_clock; // Vector clock for state transitions
  uint32_t rng_state;    // Per-world deterministic RNG state
  // Universe placement depth (Z in the universe grid). 0 = origin level. Negative values are below.
  int32_t universe_depth;

  // Computed world properties for level calculation
  uint32_t history_event_count; // Number of events in world history
  uint32_t unique_player_count; // Number of unique players who visited
  uint32_t base_level;          // Base level inherited from parent world
  double score;                 // Computed score: ln(vector_clock) + ln(history_event_count) + ln(unique_player_count) + base_level
  double level;                 // Computed level: ln(score)

  // Runtime-only evolution metadata (not serialized)
  // Stores the epoch index when a flower bloomed at a voxel, and when it entered DECAYING
  // UINT32_MAX indicates "unset".
  uint32_t *bloom_epoch_map;    // size = width*height*depth
  uint32_t *decaying_epoch_map; // size = width*height*depth

  // Condition → voxel indices mapping for fast retrieval (runtime-only). An inverted index: a
  // search for "every voxel carrying condition bit N" reads one list instead of sweeping 2M cells.
  // Built on demand by world_refresh_condition_index; empty until then.
  struct VoxelIndexList
  {
    uint32_t *indices;
    size_t size;
    size_t cap;
  } condition_voxel_indices[64];

  // Freshness stamps for the inverted index above. An index that has silently gone stale is worse
  // than no index at all, because a search would return a different answer depending on which path
  // it happened to pick, so the two are compared before the index is trusted.
  //
  // condition_revision is bumped by every code path in this translation unit that can change a
  // voxel's condition_mask. Code that writes condition_mask directly without bumping it must call
  // world_refresh_condition_index afterwards or the index will be trusted while wrong.
  uint64_t condition_revision;
  uint64_t condition_index_revision;
  bool condition_index_valid;

  // Optional runtime actor list (editor/runtime only). If NULL, no actor stepping.
  struct Actor* runtime_actors;
  int runtime_actor_count;
  int runtime_actor_capacity;
  // True when runtime_actors was allocated by world_add_runtime_actor and should be freed
  // (along with each actor's extra_data) on world_destroy. False when the pointer is borrowed
  // (editor static array, test stack storage).
  bool runtime_actors_owned;

  // Runtime particle effects (not serialized). Default drifting dust in deep open air.
  WorldParticleEffects *particle_effects;

  // Universe context for world placement and consistency
  struct Universe* universe_context;
  uint64_t universe_x, universe_y, universe_z;  // Position in universe grid

  // Optional CPU-side occupancy bitfield (1 = solid, 0 = empty); mirrors voxels for fast ops
  GpuVoxelBuffer *occupancy_bits;
  // Optional cached topmost solid z per (x,y); -1 if none. Size = width*height.
  // Built on load/generation and optionally refreshed after edits.
  int16_t *heightmap;

  // Settlement stamp metadata (runtime). Set when a wilderness settlement is generated.
  bool settlement_has_anchor;
  int16_t settlement_anchor_x;
  int16_t settlement_anchor_y;
  int16_t settlement_anchor_z;
  bool settlement_has_note;
  int16_t settlement_note_x;
  int16_t settlement_note_y;
  int16_t settlement_note_z;
  int settlement_scale; // 0 = none; 1..9 when stamped
  // Town plaza / basin center used to face doors and bias decoration yaw.
  bool settlement_has_town_center;
  int16_t settlement_town_cx;
  int16_t settlement_town_cy;

  // Buildings placed during the last settlement_stamp (for occupation ↔ villager wiring).
#define WORLD_SETTLEMENT_PLACED_MAX 48
  int settlement_placed_count;
  struct {
    int16_t ox, oy, bw, bd, floor_z;
    uint8_t building_type; // SettlementBuildingType
    uint8_t occupation;    // VillagerProfession
  } settlement_placed[WORLD_SETTLEMENT_PLACED_MAX];

  // Live settlement roster + households (runtime; rebuilt when villagers spawn).
  SettlementRoster settlement_roster;
  Household households[HOUSEHOLD_MAX_FAMILIES];
  uint8_t household_count;

  // Editor state tracking
  bool is_dirty; // Set to true when world has unsaved changes

  // Cached answer to "does this world contain any fluid?". Deliberately conservative: it is
  // allowed to say WORLD_FLUID_UNKNOWN or WORLD_FLUID_SOME when the truth is NONE, but never
  // NONE when there is fluid, because the physics tick skips itself entirely on NONE.
  // See world_fluid_presence().
  WorldFluidPresence fluid_presence;

  // Fluid simulation state: the queue of cells that may still have somewhere to send fluid, plus
  // the arrivals the renderer turns into surface ripples. Runtime-only and allocated on the first
  // step of a world that holds fluid. See fluid_sim.h.
  FluidSim *fluid_sim;

  // Bumped whenever a voxel type changes, so caches keyed on world content (the minimap, meshes)
  // can tell "nothing has changed since I last looked" from "rebuild". Only writes that go
  // through world_set_voxel and the bulk entry points bump it, so a consumer that must not miss a
  // change should treat this as a hint and refresh periodically regardless.
  uint64_t voxel_revision;

  // The band of z layers that contain anything at all. The home island floats, so roughly a third
  // of its depth is empty sky that the renderer would otherwise walk layer by layer.
  //
  // -1 means "no information" and consumers must not clamp to it — that covers both a genuinely
  // empty world and one written by code that could not maintain this. Otherwise conservative in
  // the safe direction: the band widens as solids are placed and never narrows, so it can name
  // more layers than are occupied but never fewer.
  int occupied_z_min;
  int occupied_z_max;

  // Magma vent columns from a stamped volcano conduit. world_magma_hotspot_at also returns true
  // here so fluid_sim keeps the path molten between eruptions.
  uint16_t magma_vent_column_count;
  int16_t magma_vent_columns[256][2]; // x, y
  bool volcano_present;
  int16_t volcano_x, volcano_y, volcano_surface_z;
  uint32_t volcano_path_seed;
  uint16_t volcano_pump_cooldown; // weather ticks to wait after a pump
} World;

// A fluid step over a 128^3 world costs ~90ms whether or not the world holds any fluid, because
// its passes sweep the whole volume before discovering there is nothing to move. Most worlds —
// the home island among them — contain no fluid at all, so answering that question up front is
// worth far more than making the passes themselves faster.
//
// Returns the cached answer, running a census first if the state is unknown. The census stops at
// the first fluid voxel it finds, so it is near-free on a world that does have fluid, and costs
// one linear pass over the voxel types on a world that does not — after which the answer is
// cached until something writes to the world.
WorldFluidPresence world_fluid_presence(World *world);

// Tell the world its fluid content may have changed. Anything that writes voxels without going
// through world_set_voxel must call this, or a world could gain fluid that the physics tick then
// refuses to simulate.
void world_invalidate_fluid_presence(World *world);

// True for the voxel types the fluid simulation moves, and for the springs that produce them.
// A spring is solid but generates fluid, so a world holding one is never fluid-free.
bool world_voxel_type_is_fluidlike(VoxelType type);

// Foliage: leaves of every species, and tall grass. Things made mostly of gaps, which a body can
// push through and a projectile can usually fly through.
bool world_voxel_type_is_foliage(VoxelType type);

// Sparse material templates (angled roofs, hung doors, glass panes, hollow props, foliage): the
// bake leaves transparent texels. Nested silhouette meshes show those as geometric gaps; parent
// AABB faces seal a miss with the flat voxel colour so cluster neighbour fill cannot treat a bake
// gap as empty sky.
bool world_voxel_type_has_material_gaps(VoxelType type);

// True when this voxel should be omitted from the greedy cube mesh and drawn as a nested 32³
// silhouette: sparse material type and/or a non-full shape×orient modifier (see voxel_shape.h).
bool world_voxel_needs_nested_silhouette(const Voxel *voxel);

// True when parent-world greedy meshing should omit this voxel's AABB.
//
// Fittings (doors, roofs, stairs, …) and shaped cells still need nested silhouettes. Vegetation
// (leaves, tall grass, bushes) keeps its parent AABB: home islands plant denser canopies than the
// nested instance budget can cover, and punching them out left only wind impostors while neighbour
// ray fills still showed solid leaf cubes.
bool world_voxel_omits_parent_aabb(const Voxel *voxel);

// Whether this material stops a body from entering the voxel.
//
// Deliberately not the same question as "is it solid". Foliage and water block nothing but are
// still drawn, still meshed, and still set their bit in the occupancy field; a canopy you can walk
// into is still a canopy you can see. Anything deciding where a body may stand or step should ask
// this, so that the answer is the same everywhere.
bool world_voxel_type_blocks_movement(VoxelType type);

// Build or refresh the world's occupancy bitfield (1 = solid)
// If not present, allocates it; if present, updates it in-place.
bool world_refresh_occupancy_bitfield(World *world);

// Recompute occupied_z_min / occupied_z_max exactly. Called as part of the bitfield refresh;
// only needed on its own after bulk direct writes that skipped world_set_voxel.
void world_refresh_occupied_z_range(World *world);

// Drop every cache derived from the voxel array, in constant time. For code that writes voxels
// directly and cannot say what it touched: consumers fall back to reading voxel types, which is
// slower but cannot be wrong. Prefer world_refresh_occupancy_bitfield where a pass is affordable.
void world_discard_derived_caches(World *world);

// Bring the caches keyed on voxel types — the occupancy bitfield, the heightmap, the revision
// counter — back in step after a direct write to world->voxels that changed a cell's type.
// world_set_voxel does this itself; only code that writes the array is expected to call it.
void world_voxel_type_written(World *world, int x, int y, int z, VoxelType type);

// Quickly get the highest solid z for a column using the bitfield; returns -1 if none.
// Falls back to a scan if the bitfield isn't present.
int world_height_at_fast(const World *world, int x, int y);

// Build or refresh the world's heightmap (topmost solid z per (x,y)).
// Allocates if absent; returns true on success.
bool world_build_heightmap(World *world);

// Get topmost solid z using the cached heightmap if present; otherwise fallback.
int world_height_at_cached(const World *world, int x, int y);

// ============================================================================
// DIRTY STATE MANAGEMENT
// ============================================================================

// Note: Functions are implemented as static inline below

// -----------------------------------------------------------------------------
// Fast voxel access helpers (inline, safe for renderer/physics hot paths)
static inline bool world_pos_in_bounds_fast(const World *w, int x, int y, int z)
{
  return w && x >= 0 && y >= 0 && z >= 0 &&
         x < (int)w->width && y < (int)w->height && z < (int)w->depth;
}

static inline size_t world_linear_index_fast(const World *w, int x, int y, int z)
{
  return ((size_t)z * (size_t)w->height + (size_t)y) * (size_t)w->width + (size_t)x;
}

static inline Voxel *world_voxel_ptr_fast(World *w, int x, int y, int z)
{
  return &w->voxels[world_linear_index_fast(w, x, y, z)];
}

static inline const Voxel *world_voxel_cptr_fast(const World *w, int x, int y, int z)
{
  return &w->voxels[world_linear_index_fast(w, x, y, z)];
}

static inline bool world_is_solid_fast(World *w, int x, int y, int z)
{
  if (!world_pos_in_bounds_fast(w, x, y, z)) return false;
  const Voxel *v = world_voxel_cptr_fast(w, x, y, z);
  return v && v->type != VOXEL_AIR;
}

// -----------------------------------------------------------------------------
// Voxel auxiliary data helpers (8 x 8-bit packed fields)
// voxel_get_field and voxel_set_field are now defined in voxel.h

// Voxel field indices and functions are now defined in voxel.h

// Backward-compat aliases for quantity/wetness functions
static inline uint8_t voxel_get_quantity_or_wetness(const Voxel *v) { return voxel_get_quantity(v); }
static inline void voxel_set_quantity_or_wetness(Voxel *v, uint8_t val) { voxel_set_quantity(v, val); }

// World creation and destruction
World *world_create(uint32_t width, uint32_t height, uint32_t depth);
void world_destroy(World *world);
// Trim world bounds to the minimal axis-aligned box containing all non-AIR voxels.
// Returns false if world is NULL or contains no non-air voxels (no change).
bool world_autocrop(World *world);
// Draw a solid sphere centered in the world. Clears existing voxels to AIR,
// then sets all voxels within radius to the provided type. Radius is clamped
// to fit within world bounds. Returns false on invalid input or allocation.
bool world_draw_sphere(World *world, uint32_t radius, VoxelType type);

// Voxel manipulation
Voxel *world_get_voxel(World *world, uint32_t x, uint32_t y, uint32_t z);
bool world_set_voxel(World *world, uint32_t x, uint32_t y, uint32_t z, VoxelType type);
bool world_is_position_valid(World *world, uint32_t x, uint32_t y, uint32_t z);

// True when a live runtime actor's body intersects cell (x,y,z). Used to refuse placing solids
// into bodies (debris writeback / editor place) — Devlog #28 place-vs-body hygiene.
bool world_actor_blocks_cell(const World *world, int x, int y, int z);

// Canonical RGB color hint for a voxel type (used by renderers/importers)
void world_voxel_type_color(VoxelType t, uint8_t *r, uint8_t *g, uint8_t *b);
// Human-readable voxel type name
const char *world_voxel_type_name(VoxelType t);

// Voxel condition manipulation
bool world_add_voxel_condition(World *world, uint32_t x, uint32_t y, uint32_t z, const char *condition_name);
bool world_remove_voxel_condition(World *world, uint32_t x, uint32_t y, uint32_t z, const char *condition_name);
bool world_has_voxel_condition(World *world, uint32_t x, uint32_t y, uint32_t z, const char *condition_name);
void world_clear_voxel_conditions(World *world, uint32_t x, uint32_t y, uint32_t z);

// Rebuild condition_voxel_indices from a full scan and stamp it fresh. O(volume) once, after which
// condition searches are O(matches). Returns false only on allocation failure.
bool world_refresh_condition_index(World *world);

// True when condition_voxel_indices can be trusted to list exactly the voxels carrying each bit.
bool world_condition_index_is_fresh(const World *world);

// Evolution epoch helpers (runtime-only; not persisted)
void world_mark_bloom_epoch(World *world, uint32_t x, uint32_t y, uint32_t z, uint32_t epoch_idx);
uint32_t world_get_bloom_epoch(World *world, uint32_t x, uint32_t y, uint32_t z);
void world_mark_decaying_epoch(World *world, uint32_t x, uint32_t y, uint32_t z, uint32_t epoch_idx);
uint32_t world_get_decaying_epoch(World *world, uint32_t x, uint32_t y, uint32_t z);
void world_clear_epoch_marks(World *world, uint32_t x, uint32_t y, uint32_t z);

// Advance world by one epoch; applies bloom/decay lifecycle.
// Returns true if any voxel state changed (bloomed, started decaying, or withered).
bool world_advance_epoch(World *world, uint32_t epoch_idx);
// Run epoch update and return a heap-allocated JSON patch string describing changes.
// Caller must free(). Returns NULL if no changes or on error.
char *world_advance_epoch_and_patch(World *world, uint32_t epoch_idx);

// World generation
void world_generate(World *world, const char *seed);
void world_generate_with_type(World *world, const char *seed, WorldGenerationType type);
// Variant that allows passing a fill voxel for WORLD_TYPE_SOLID. For other types, fill_type is ignored.
void world_generate_with_type_and_fill(World *world, const char *seed, WorldGenerationType type, VoxelType fill_type);

// Generate standalone world with universe context at (0,0,0)
bool world_generate_standalone(World *world, const char *seed, WorldGenerationType type, struct Universe* universe);

// Generate world in universe context at specific coordinates
bool world_generate_in_universe(World *world, const char *seed, WorldGenerationType type,
                               struct Universe* universe, uint64_t x, uint64_t y, uint64_t z);
// Generate a solid world filled entirely with the provided voxel type
void world_generate_solid_fill(World *world, VoxelType fill_type);
void world_update_springs(World *world, uint64_t current_time);
// Lightweight cellular-automaton fluid step; processes up to max_cells water voxels
void world_step_fluids(World *world, int max_cells);
// Set a global universe noise seed so all worlds share the same Perlin permutation.
// Setup only: this rewrites a table every generating thread reads, so call it before generation
// begins and never while it is running.
void world_set_universe_noise_seed(const char *base_seed);

// Builds the noise tables generation shares — the Perlin permutation and the entropy fields — so
// that several worlds can be generated at once without racing to be the one that builds them.
// Idempotent, and calling it is optional: generation still initialises them on first use. Call it on
// one thread before fanning generation out across the task pool.
void world_generation_prepare_shared_state(void);
// Debug helpers for verifying magma field continuity
float world_sample_magma_field(World *world, int x, int y, int z);
int world_magma_seed_at(World *world, int x, int y, int z);

// Bedrock-plane vents from universe noise, plus stamped volcano conduit columns. Magma in a
// hotspot column stays molten; elsewhere it cools to basalt. Safe on a null world (returns false).
bool world_magma_hotspot_at(const World *world, int x, int y);
bool world_has_magma_vent_column(const World *world, int x, int y);
void world_clear_magma_vent_columns(World *world);
bool world_register_magma_vent_column(World *world, int x, int y);

// Debug helpers: sample key generation fields for boundary verification
float world_sample_occupancy_noise(World *world, int x, int y, int z);
float world_sample_rarity_column(World *world, int x, int y);
float world_sample_stone_field(World *world, int x, int y, int z);
// Blend from a neighbor across the shared face into dst up to max_depth voxels
void world_blend_adjacent_face(World *dst, const World *src, int dirx, int diry, int dirz, int max_depth);

// World physics
float world_get_gravity(World *world);
void world_set_gravity(World *world, float gravity);
// Step actors under gravity and ground collision for dt seconds (editor/runtime)
void world_step_actors(World *world, float dt_seconds);
// Copy an actor into the world's runtime list, growing the heap array if needed. The world then
// owns that array (runtime_actors_owned). extra_data, if set, is treated as a MobActor* and freed
// with the list.
bool world_add_runtime_actor(World *world, const struct Actor *actor);
// Lookup a runtime actor by id (NULL if missing).
struct Actor *world_find_runtime_actor(World *world, uint32_t id);
// Remove an actor by id without freeing extra_data. Copies the entry into *out when non-NULL.
// Used to carry an inhabited body across a world seam without destroying the MobActor.
bool world_extract_runtime_actor_by_id(World *world, uint32_t id, struct Actor *out);
void world_clear_runtime_actors(World *world);

// World log management
bool world_set_log(World *world, const char *log_message);
const char *world_get_log(World *world);
bool world_append_log(World *world, const char *log_message);
void world_record_event(World *world, const char *event_type, const char *details);

// World level system
void world_update_score_and_level(World *world);
double world_get_level(World *world);
double world_get_score(World *world);
uint32_t world_get_base_level(World *world);
void world_set_base_level(World *world, uint32_t base_level);
void world_increment_history_event(World *world);
void world_add_unique_player(World *world, const char *player_id);

// World file operations
char *world_serialize(World *world);
World *world_deserialize(const char *data);
bool world_save(World *world, const char *filename);
bool world_load(World *world, const char *filename);
bool world_save_by_seed(World *world, const char *seed);
bool world_load_by_seed(World *world, const char *seed);

// Save world with universe context
bool world_save_with_universe(World *world, const char *seed, struct Universe* universe);

// Load world with universe context
World *world_load_with_universe(const char *seed, struct Universe* universe);
bool world_exists_by_seed(const char *seed);
void construct_world_filename(char *buffer, size_t buffer_size, const char *id);

// Layer utilities
void layer_scatter_ellipsoids(World *world,
                              VoxelType type,
                              int min_count,
                              int max_count,
                              int min_radius,
                              int max_radius,
                              int min_height_offset,
                              int max_height_offset);

// Spray material along a vector from an origin by scattering stretched ellipsoids into AIR.
// Ellipsoids are elongated along the direction vector and taper in both size and probability
// as distance from the centerline increases.
void layer_spray_material(World *world,
                          uint32_t origin_x, uint32_t origin_y, uint32_t origin_z,
                          float dir_x, float dir_y, float dir_z,
                          VoxelType type,
                          int length_voxels,
                          int min_radius,
                          int max_radius,
                          float base_density,
                          float radial_falloff);

// World coordinate system for adjacent worlds
typedef struct
{
  int x, y, z; // World coordinates (not voxel coordinates)
} WorldCoord;

// Generate a seed for a specific world coordinate
char *world_generate_seed_for_coord(const char *base_seed, WorldCoord coord);

// Generate all worlds for a new game
typedef struct
{
  World *home_world;
  World *farm_world;
  World *wilderness_world;
  World *adjacent_farm_worlds[26]; // 3x3x3 - 1 = 26 adjacent worlds
  World *adjacent_home_worlds[26]; // 26 adjacent empty worlds
  World *random_teleport_world;
  int total_worlds;
  char *base_seed;
  char *random_seed;
} GameWorlds;

// Unified adjacency map (hash -> World*) coexisting with legacy arrays
typedef struct AdjacencyNode
{
  char id[65];
  World *world;
  struct AdjacencyNode *next;
} AdjacencyNode;

typedef struct AdjacencyMap
{
  AdjacencyNode **buckets;
  int bucket_count;
  int size;
} AdjacencyMap;

bool game_worlds_adjacency_init(AdjacencyMap *map, int bucket_count);
void game_worlds_adjacency_free(AdjacencyMap *map);
World *game_worlds_adjacency_get(AdjacencyMap *map, const char *id);
World *game_worlds_adjacency_put(AdjacencyMap *map, const char *id, World *world);

GameWorlds *game_worlds_create(const char *character_name);
void game_worlds_destroy(GameWorlds *game_worlds);
bool game_worlds_generate_all(GameWorlds *game_worlds, const char *character_name);
bool game_worlds_save_all(GameWorlds *game_worlds, const char *character_name);

// Loading bar callback
typedef void (*LoadingProgressCallback)(int current, int total, const char *message);
void game_worlds_set_progress_callback(LoadingProgressCallback callback);

// World deterministic RNG utilities
void world_rng_seed(World *world, const char *context);
uint32_t world_rng_next(World *world);
int world_rng_range(World *world, int max);

// -----------------------------------------------------------------------------
// Faces and slice utilities
typedef enum
{
  WORLD_FACE_POS_X = 0,
  WORLD_FACE_NEG_X = 1,
  WORLD_FACE_POS_Y = 2,
  WORLD_FACE_NEG_Y = 3,
  WORLD_FACE_POS_Z = 4,
  WORLD_FACE_NEG_Z = 5
} WorldFace;

typedef struct
{
  uint32_t x;
  uint32_t y;
  uint32_t z;
  VoxelType type;
} VoxelCoord;

// Get the 2D dimensions of a face slice (out_w = horizontal pixels, out_h = vertical)
// Returns false if face is invalid.
bool world_face_dimensions(const World *world, WorldFace face, uint32_t *out_w, uint32_t *out_h);

// Render a given face slice (fixed slice index along that face normal) to a newly-allocated
// RGB24 framebuffer (row-major, 3 bytes per pixel). Caller must free(*out_pixels).
// Returns false on invalid inputs. out_stride is bytes per row.
bool world_face_to_rgb24(const World *world,
                         WorldFace face,
                         uint32_t slice,
                         uint8_t **out_pixels,
                         uint32_t *out_w,
                         uint32_t *out_h,
                         uint32_t *out_stride);

// Generalized 2D pathfinding on a fixed z-plane using 4-neighborhood.
// - passable: table of VOXEL_COUNT booleans indicating which voxel types may be traversed.
// - Returns true and allocates out_path (caller must free) containing inclusive voxels from (sx,sy,z) to (dx,dy,z).
// - Returns false if no path exists or inputs invalid.
bool world_find_path_2d(
    const World *world,
    uint32_t z,
    uint32_t sx, uint32_t sy,
    uint32_t dx, uint32_t dy,
    const bool passable[VOXEL_COUNT],
    VoxelCoord **out_path,
    size_t *out_len);

// Collect all non-air voxels from a face slice. Returns a newly-allocated array of VoxelCoord
// and count in out_count. Caller must free(*out_list).
bool world_get_slice_voxels(const World *world,
                            WorldFace face,
                            uint32_t slice,
                            VoxelCoord **out_list,
                            size_t *out_count);

// -----------------------------------------------------------------------------
// 3D subvolume extraction utilities (for renderbox/tiling workflows)

// Copy a subvolume from src starting at (x0,y0,z0) of size (width,height,depth)
// into dst at (dx0,dy0,dz0). Returns false if bounds invalid or pointers null.
bool world_copy_subvolume(const World *src,
                          uint32_t x0, uint32_t y0, uint32_t z0,
                          World *dst,
                          uint32_t dx0, uint32_t dy0, uint32_t dz0,
                          uint32_t width, uint32_t height, uint32_t depth);

// Create and return a new world initialized from a subvolume of src.
// The new world has dimensions (width,height,depth) and contains a deep copy
// of voxels from src in the given region. Returns NULL on allocation/bounds error.
World *world_extract_subworld(const World *src,
                              uint32_t x0, uint32_t y0, uint32_t z0,
                              uint32_t width, uint32_t height, uint32_t depth);

// Compute a simple 64-bit content hash over voxel types and aux data of a world
uint64_t world_content_hash(const World *world);

// -----------------------------------------------------------------------------
// Visibility and ray casting API (orthographic/isometric friendly)

// Cast a ray through the voxel grid and return the first solid voxel hit.
// Returns true if a hit occurred and writes coordinates to out_x/out_y/out_z.
bool world_raycast_first_hit(
    const World *world,
    float origin_x, float origin_y, float origin_z,
    float dir_x, float dir_y, float dir_z,
    int max_steps,
    int *out_x, int *out_y, int *out_z);

// Compute a visibility mask for a world using an orthographic projection with a
// given camera. The camera is defined by a position, forward direction, and up
// vector. The function samples a viewport of size viewport_w x viewport_h in
// screen-space with the provided pixel stride and casts parallel rays along the
// forward direction. Returns a newly-allocated mask of size
// (width*height*depth) with 0/1 entries; caller must free(). Optionally returns
// the number of visible voxels in out_visible_count.
uint8_t *world_visible_voxels_for_viewport(
    const World *world,
    float cam_x, float cam_y, float cam_z,
    float dir_x, float dir_y, float dir_z,
    float up_x, float up_y, float up_z,
    const char *projection, // currently only "orthographic" is supported
    int viewport_w, int viewport_h,
    int sample_stride,
    size_t *out_visible_count);

// Fast count-only variant. For orthographic isometric it runs in O(width*height)
// without allocating a visibility mask. For other camera setups, returns 0 for now.
size_t world_visible_count_for_viewport(
    const World *world,
    float cam_x, float cam_y, float cam_z,
    float dir_x, float dir_y, float dir_z,
    float up_x, float up_y, float up_z,
    const char *projection,
    int viewport_w, int viewport_h,
    int sample_stride);

// For multiple worlds: compute visibility per-world with the same camera.
// The results array must have capacity world_count. Each result.mask must be
// freed by the caller when no longer needed.
typedef struct
{
  const World *world;
  uint8_t *mask;
  size_t visible_count;
} WorldVisibilityResult;

bool worlds_visible_voxels_for_viewport(
    const World *const *worlds,
    int world_count,
    float cam_x, float cam_y, float cam_z,
    float dir_x, float dir_y, float dir_z,
    float up_x, float up_y, float up_z,
    const char *projection,
    int viewport_w, int viewport_h,
    int sample_stride,
    WorldVisibilityResult *results);

// ============================================================================
// DIRTY STATE MANAGEMENT IMPLEMENTATION
// ============================================================================

// Mark world as having unsaved changes
static inline void world_mark_dirty(World *world)
{
  if (world) world->is_dirty = true;
}

// Mark world as clean (no unsaved changes)
static inline void world_mark_clean(World *world)
{
  if (world) world->is_dirty = false;
}

// Check if world has unsaved changes
static inline bool world_is_dirty(const World *world)
{
  return world && world->is_dirty;
}

#endif // WORLD_H
