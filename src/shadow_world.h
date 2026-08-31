#ifndef SHADOW_WORLD_H
#define SHADOW_WORLD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "world.h"

// A ShadowWorld presents the cluster of worlds around the player as one coordinate space, and
// gives every spatial query a single shared traversal to run through.
//
// Radius 2 (5x5x5) is what a first-person view distance of three worlds needs: from near one edge
// of the player's world looking out, the sightline crosses the current world, the neighbour, and
// one more beyond. Radius 1 left that third world outside the cluster.
//
// It holds *borrowed* World pointers. It never copies voxels, so it cannot go stale. Occupancy
// pyramids scale with cluster volume; a merged tree of voxel copies would be multi-GB and need
// invalidating on every write.
//
// Thread safety: every query here is read-only, so any number may run concurrently, but they are
// invalid while physics tasks are mutating worlds in parallel. See shadow_world_refresh, and the
// phase ordering in world_physics_jobs.h.

#define SHADOW_CLUSTER_RADIUS 2
#define SHADOW_CLUSTER_DIM (2 * SHADOW_CLUSTER_RADIUS + 1)
#define SHADOW_SLOT_COUNT (SHADOW_CLUSTER_DIM * SHADOW_CLUSTER_DIM * SHADOW_CLUSTER_DIM)
#define SHADOW_CENTRE_SLOT (SHADOW_SLOT_COUNT / 2)

// Slot bitmasks need more than 32 bits once the cluster grows past 3x3x3. Clang/GCC __int128
// covers the 125 slots of a radius-2 cluster without a multi-word helper.
#if !defined(__SIZEOF_INT128__)
#error "shadow_world requires compiler support for __int128 (radius-2 slot masks)"
#endif
typedef unsigned __int128 ShadowSlotMask;
#define SHADOW_SLOT_BIT(slot) ((ShadowSlotMask)1 << (unsigned)(slot))

// Block edge lengths of the three pyramid levels above the per-voxel bitfield.
#define SHADOW_L1_BLOCK 4
#define SHADOW_L2_BLOCK 16
#define SHADOW_L3_BLOCK 64

// Devlog #17 brick mask: one bit per voxel inside an L1 (4³) block. Bit index within a brick is
// ((lz & 3) << 4) | ((ly & 3) << 2) | (lx & 3). A loaded mask lets the ray DDA test emptiness with
// register bit-ops instead of re-reading the occupancy bitfield for every empty cell in the brick.
#define SHADOW_BRICK_VOXELS (SHADOW_L1_BLOCK * SHADOW_L1_BLOCK * SHADOW_L1_BLOCK)

// Devlog #15 VVAO: L2 (16³) stores solid count scaled to 0..255. Trilinear sample at a voxel centre
// estimates local fullness; AO darkens when fullness > 50%.
#define SHADOW_DENSITY_BLOCK SHADOW_L2_BLOCK
#define SHADOW_DENSITY_VOXELS \
  (SHADOW_DENSITY_BLOCK * SHADOW_DENSITY_BLOCK * SHADOW_DENSITY_BLOCK)

static inline int shadow_brick_bit(int lx, int ly, int lz)
{
  return ((lz & (SHADOW_L1_BLOCK - 1)) << 4) | ((ly & (SHADOW_L1_BLOCK - 1)) << 2) |
         (lx & (SHADOW_L1_BLOCK - 1));
}

// Sampling is tri-state on purpose. Reporting a not-yet-streamed world as empty lets a projectile
// fly through terrain that merely has not loaded; reporting it as solid walls the player in. The
// API must never collapse the three.
typedef enum
{
  SHADOW_EMPTY = 0,
  SHADOW_SOLID = 1,
  SHADOW_UNLOADED = 2
} ShadowSample;

typedef enum
{
  SHADOW_REGION_ALL = 0,
  SHADOW_REGION_BOX,
  SHADOW_REGION_SPHERE,
  SHADOW_REGION_COLUMN,
  SHADOW_REGION_RAY
} ShadowRegionKind;

// Which of the two condition-search strategies to use. AUTO decides on a selectivity estimate; the
// forced modes exist so tests can prove both paths agree and so the benchmark can price the
// threshold instead of leaving it a guess.
typedef enum
{
  SHADOW_INDEX_AUTO = 0,
  SHADOW_INDEX_FORCE_TRAVERSAL,
  SHADOW_INDEX_FORCE_INDEX
} ShadowIndexPolicy;

typedef struct
{
  int cx, cy, cz; // cluster-relative voxel coordinates
  int slot;       // 0..SHADOW_SLOT_COUNT-1; for an unloaded visit, the slot that would own the cell
  int lx, ly, lz; // coordinates local to that slot's world
  VoxelType type; // meaningful only when voxel != NULL
  // NULL when the filter was answered from occupancy bits alone, so a caller cannot accidentally
  // depend on data the fast path never loaded. Also NULL for unloaded visits.
  const Voxel *voxel;
  bool unloaded;
} ShadowHit;

typedef struct
{
  // Bit per slot; 0 means all slots. This is the "by world" filter, and the cheapest rejection
  // available: one bit test skips millions of cells.
  ShadowSlotMask slot_mask;

  ShadowRegionKind region;
  union
  {
    struct
    {
      int x0, y0, z0, x1, y1, z1; // inclusive
    } box;
    struct
    {
      float cx, cy, cz, radius;
    } sphere;
    struct
    {
      int x, y;
    } column;
    struct
    {
      float ox, oy, oz;
      float dx, dy, dz;
      int max_steps;
    } ray;
  } r;

  // Intersected with each slot's occupied_z_min/max. Leave both 0 for "no z restriction".
  int z_min, z_max;
  bool use_z_band;

  // Answerable from occupancy bits alone; never reads a Voxel.
  bool solid_only;

  const VoxelType *types; // NULL means any type
  int type_count;

  uint64_t condition_any; // hit needs at least one of these bits (0 disables)
  uint64_t condition_all; // hit needs all of these bits (0 disables)
  ShadowIndexPolicy index_policy;

  bool report_unloaded; // emit one visit per unloaded slot overlapping the region
  int max_results;      // 0 means unlimited; enables early termination
} ShadowFilter;

// Return false to stop the search.
typedef bool (*ShadowVisitFn)(const ShadowHit *hit, void *user);

typedef struct
{
  uint8_t *bits;
  size_t bytes;
  int dim_x, dim_y, dim_z; // block counts across the whole cluster
  int block;               // block edge length in voxels
  int shift;               // log2(block)
} ShadowPyramidLevel;

typedef struct ShadowWorld
{
  World *slots[SHADOW_SLOT_COUNT];
  uint64_t slot_revision[SHADOW_SLOT_COUNT]; // voxel_revision seen at last pyramid rebuild
  ShadowSlotMask loaded_mask;
  ShadowSlotMask pyramid_valid_mask;

  // Universe cell of the centre slot.
  uint64_t centre_x, centre_y, centre_z;

  // Per-slot world dimensions, required uniform so routing is a shift and a mask.
  uint32_t world_w, world_h, world_d;
  int shift_x, shift_y, shift_z;
  int mask_x, mask_y, mask_z;
  int extent_x, extent_y, extent_z; // cluster extent = SHADOW_CLUSTER_DIM * world dimension

  // False when the world dimensions are not a multiple of the coarsest block, in which case blocks
  // would straddle slots and a per-slot rebuild could not be exact. Every level then reports
  // "occupied" and the ladder falls through to the per-voxel bitfield: slower, never wrong.
  bool pyramid_enabled;
  ShadowPyramidLevel l1, l2, l3;

  // Per-slot Devlog #17 brick masks and Devlog #15 VVAO fields. Sized to one world's L1/L2 grid
  // so unloaded slots cost nothing. Index with local brick coords (lx>>2, …).
  uint64_t *slot_brick_masks[SHADOW_SLOT_COUNT];
  uint8_t *slot_density[SHADOW_SLOT_COUNT];
  uint8_t *slot_emissive[SHADOW_SLOT_COUNT];
  int brick_dim_x, brick_dim_y, brick_dim_z; // world_w / L1, …
  int density_dim_x, density_dim_y, density_dim_z;
} ShadowWorld;

// ---------------------------------------------------------------------------
// Lifecycle

ShadowWorld *shadow_world_create(uint32_t world_w, uint32_t world_h, uint32_t world_d);
void shadow_world_destroy(ShadowWorld *sw);

// Point a slot at a world, or clear it with NULL. Offsets are -SHADOW_CLUSTER_RADIUS..
// SHADOW_CLUSTER_RADIUS on each axis. The world is borrowed: the shadow world never frees it
// and never writes to it.
bool shadow_world_attach(ShadowWorld *sw, int dx, int dy, int dz, World *world);
World *shadow_world_slot_world(const ShadowWorld *sw, int slot);

void shadow_world_set_centre(ShadowWorld *sw, uint64_t ux, uint64_t uy, uint64_t uz);

// Re-frame the cluster around a new centre cell, keeping the slots that are still in range. Writes
// the amount to add to a cluster coordinate to rebase it from the old framing to the new one, so
// entity positions can follow. Returns the number of slots retained.
int shadow_world_recenter(ShadowWorld *sw, uint64_t ux, uint64_t uy, uint64_t uz,
                          int *out_delta_x, int *out_delta_y, int *out_delta_z);

// Rebuild pyramid coverage for slots whose voxel_revision has moved since the last refresh.
// Returns the number of slots rebuilt. Must not run concurrently with world mutation.
// Prefer shadow_world_refresh_budget from the play tick when many slots may be dirty.
int shadow_world_refresh(ShadowWorld *sw);

// Like shadow_world_refresh, but rebuilds at most max_rebuilds dirty slots per call. Centre and
// face slots are preferred so gameplay queries stay fresh; the rest catch up on later frames.
// max_rebuilds <= 0 rebuilds every dirty slot (same as refresh).
int shadow_world_refresh_budget(ShadowWorld *sw, int max_rebuilds);

void shadow_world_invalidate_slot(ShadowWorld *sw, int slot);

// ---------------------------------------------------------------------------
// Coordinates

static inline int shadow_slot_index(int dx, int dy, int dz)
{
  return ((dz + SHADOW_CLUSTER_RADIUS) * SHADOW_CLUSTER_DIM + (dy + SHADOW_CLUSTER_RADIUS)) *
             SHADOW_CLUSTER_DIM +
         (dx + SHADOW_CLUSTER_RADIUS);
}

static inline void shadow_slot_offsets(int slot, int *dx, int *dy, int *dz)
{
  *dx = (slot % SHADOW_CLUSTER_DIM) - SHADOW_CLUSTER_RADIUS;
  *dy = ((slot / SHADOW_CLUSTER_DIM) % SHADOW_CLUSTER_DIM) - SHADOW_CLUSTER_RADIUS;
  *dz = (slot / (SHADOW_CLUSTER_DIM * SHADOW_CLUSTER_DIM)) - SHADOW_CLUSTER_RADIUS;
}

static inline bool shadow_world_in_bounds(const ShadowWorld *sw, int cx, int cy, int cz)
{
  return cx >= 0 && cy >= 0 && cz >= 0 &&
         cx < sw->extent_x && cy < sw->extent_y && cz < sw->extent_z;
}

// Route a cluster coordinate to a slot and local coordinate. Returns -1 when out of the cluster.
static inline int shadow_world_route(const ShadowWorld *sw, int cx, int cy, int cz,
                                     int *lx, int *ly, int *lz)
{
  if (!shadow_world_in_bounds(sw, cx, cy, cz))
    return -1;
  if (lx)
    *lx = cx & sw->mask_x;
  if (ly)
    *ly = cy & sw->mask_y;
  if (lz)
    *lz = cz & sw->mask_z;
  return (((cz >> sw->shift_z) * SHADOW_CLUSTER_DIM) + (cy >> sw->shift_y)) * SHADOW_CLUSTER_DIM +
         (cx >> sw->shift_x);
}

// Slot owning a cluster coordinate, or -1 when outside the cluster.
int shadow_world_slot_of(const ShadowWorld *sw, int cx, int cy, int cz);

// Cluster coordinate of a slot's minimum corner.
void shadow_world_slot_origin(const ShadowWorld *sw, int slot, int *ox, int *oy, int *oz);

// ---------------------------------------------------------------------------
// The one shared traversal

// Runs the rejection ladder (slot mask, loaded, z band, L3, L2, L1, occupancy bit, then finally the
// 48-byte Voxel read) over the filter's region and calls fn for each hit. Visit order is fixed:
// slots in dz, dy, dx order, then z, y, x within a slot; along the ray for SHADOW_REGION_RAY.
// Returns the number of visits, or -1 on a malformed filter.
int shadow_world_for_each(const ShadowWorld *sw, const ShadowFilter *f,
                          ShadowVisitFn fn, void *user);

// Same, restricted to one slot. Sugar for a slot_mask with a single bit set.
int shadow_world_for_each_in_slot(const ShadowWorld *sw, int slot, const ShadowFilter *f,
                                  ShadowVisitFn fn, void *user);

// Convenience initialiser: whole cluster, no filtering.
ShadowFilter shadow_filter_all(void);

// ---------------------------------------------------------------------------
// Search kinds. Each is a thin configuration of the traversal above; none contains its own ladder.

ShadowSample shadow_world_sample(const ShadowWorld *sw, int cx, int cy, int cz);

// Resolved voxel type at a cluster coordinate; VOXEL_AIR when empty or unloaded. Prefer
// shadow_world_sample when solidity is all that is needed, since this reads 48 bytes.
VoxelType shadow_world_type_at(const ShadowWorld *sw, int cx, int cy, int cz);

typedef struct
{
  bool hit;
  bool stopped_unloaded; // ray left loaded space before hitting anything
  int cx, cy, cz;
  int slot;
  VoxelType type;
  int steps;
} ShadowRayResult;

bool shadow_world_raycast(const ShadowWorld *sw,
                          float ox, float oy, float oz,
                          float dx, float dy, float dz,
                          int max_steps, ShadowSlotMask slot_mask,
                          ShadowRayResult *out);

// Occupancy-only lighting ray: MIP skip, never reads a Voxel, and treats unloaded slots as empty
// so a missing neighbour does not fake a wall of shadow. Hit cells still agree with
// shadow_world_raycast wherever both sides of the comparison are loaded and solid.
bool shadow_world_raycast_occupancy(const ShadowWorld *sw,
                                    float ox, float oy, float oz,
                                    float dx, float dy, float dz,
                                    int max_steps, ShadowSlotMask slot_mask,
                                    ShadowRayResult *out);

// Collect matches into a caller-supplied buffer. Returns the number written, which is capped at
// max_out; out_total, when non-NULL, receives the number that matched before the cap.
int shadow_world_find_in_radius(const ShadowWorld *sw,
                                float cx, float cy, float cz, float radius,
                                const ShadowFilter *base,
                                ShadowHit *out, int max_out, int *out_total);

int shadow_world_find_in_box(const ShadowWorld *sw,
                             int x0, int y0, int z0, int x1, int y1, int z1,
                             const ShadowFilter *base,
                             ShadowHit *out, int max_out, int *out_total);

// Expanding Chebyshev shells, so cost scales with the distance to the answer rather than with the
// volume of the search region.
bool shadow_world_find_nearest(const ShadowWorld *sw, int cx, int cy, int cz, int max_radius,
                               const ShadowFilter *base, ShadowHit *out);

int shadow_world_count(const ShadowWorld *sw, const ShadowFilter *f);

// Topmost solid z in a cluster column, or -1 when the column is empty or its slots are unloaded.
// Routed to the worlds' cached heightmaps rather than rescanning.
int shadow_world_surface_height(const ShadowWorld *sw, int cx, int cy);

// ---------------------------------------------------------------------------
// Parallel fan-out. Runs one slot per task on the shared worker pool, then concatenates per-slot
// results in slot order, so the output does not depend on which worker finished first.
int shadow_world_find_parallel(const ShadowWorld *sw, const ShadowFilter *f,
                               ShadowHit *out, int max_out, int *out_total);

// ---------------------------------------------------------------------------
// Introspection, for tests and the benchmark.

typedef struct
{
  long slots_considered;
  long slots_rejected_mask;
  long slots_rejected_unloaded;
  long slots_rejected_zband;
  long blocks_rejected_l3;
  long blocks_rejected_l2;
  long blocks_rejected_l1;
  long cells_tested;
  long cells_rejected_bit;
  long voxels_read;
  long hits;
  long index_searches;     // condition searches answered from the inverted index
  long traversal_searches; // condition searches answered by traversing
} ShadowSearchStats;

void shadow_world_reset_stats(void);
ShadowSearchStats shadow_world_stats(void);

size_t shadow_world_pyramid_bytes(const ShadowWorld *sw);

// Brick child mask for the L1 block containing (cx,cy,cz), or 0 when pyramid data is missing.
uint64_t shadow_world_brick_mask_at(const ShadowWorld *sw, int cx, int cy, int cz);

// Trilinear VVAO sample in [0,1]: 0.5 ≈ flat surface, higher is more occluded (crevice).
// Returns 0.5 when density data is unavailable so callers can treat it as "no extra AO".
float shadow_world_density_sample(const ShadowWorld *sw, float x, float y, float z);

// Max emissive (0..1) in the L2 neighbourhood of (cx,cy,cz), trilinear-filtered.
float shadow_world_emissive_sample(const ShadowWorld *sw, float x, float y, float z);

#endif // SHADOW_WORLD_H
