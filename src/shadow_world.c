#include "shadow_world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_voxel_buffer.h"
#include "task_scheduler.h"

// Search accounting is per-thread so that a fanned-out search does not have several workers
// incrementing the same counters. shadow_world_stats therefore reports what the calling thread did,
// which is what the single-threaded tests and benchmark want.
static _Thread_local ShadowSearchStats g_stats;

void shadow_world_reset_stats(void)
{
  memset(&g_stats, 0, sizeof(g_stats));
}

ShadowSearchStats shadow_world_stats(void)
{
  return g_stats;
}

// A condition search reads one index entry where a traversal would read one cell, but each index
// entry costs more: a division to recover coordinates, a region test, and a random-order voxel
// read rather than a sequential one. This is the break-even multiplier, and the benchmark prices
// both paths so it can be moved on evidence rather than taste.
#define SHADOW_INDEX_COST_RATIO 8

// ---------------------------------------------------------------------------
// Bit helpers

static inline bool bitfield_get(const uint8_t *bits, size_t index)
{
  return ((bits[index >> 3u] >> (index & 7u)) & 1u) != 0u;
}

static inline void bitfield_set(uint8_t *bits, size_t index, bool on)
{
  const uint8_t mask = (uint8_t)(1u << (index & 7u));
  if (on)
    bits[index >> 3u] |= mask;
  else
    bits[index >> 3u] &= (uint8_t)~mask;
}

static inline size_t level_index(const ShadowPyramidLevel *lv, int bx, int by, int bz)
{
  return ((size_t)bz * (size_t)lv->dim_y + (size_t)by) * (size_t)lv->dim_x + (size_t)bx;
}

static inline bool level_get(const ShadowPyramidLevel *lv, int bx, int by, int bz)
{
  return bitfield_get(lv->bits, level_index(lv, bx, by, bz));
}

static inline void level_set(ShadowPyramidLevel *lv, int bx, int by, int bz, bool on)
{
  bitfield_set(lv->bits, level_index(lv, bx, by, bz), on);
}

static int log2_exact(uint32_t v)
{
  int shift = 0;
  while ((1u << shift) < v)
    shift++;
  return ((1u << shift) == v) ? shift : -1;
}

// ---------------------------------------------------------------------------
// Lifecycle

static bool level_alloc(ShadowPyramidLevel *lv, int block, int ex, int ey, int ez)
{
  lv->block = block;
  lv->shift = log2_exact((uint32_t)block);
  lv->dim_x = (ex + block - 1) / block;
  lv->dim_y = (ey + block - 1) / block;
  lv->dim_z = (ez + block - 1) / block;
  const size_t bits = (size_t)lv->dim_x * (size_t)lv->dim_y * (size_t)lv->dim_z;
  lv->bytes = (bits + 7u) / 8u;
  lv->bits = (uint8_t *)calloc(1, lv->bytes ? lv->bytes : 1u);
  return lv->bits != NULL;
}

ShadowWorld *shadow_world_create(uint32_t world_w, uint32_t world_h, uint32_t world_d)
{
  if (world_w == 0u || world_h == 0u || world_d == 0u)
    return NULL;

  // Routing has to be a shift and a mask, so the dimensions must be powers of two. This is the
  // assertion the plan calls for; failing here is much easier to diagnose than silently mis-routing
  // a coordinate into a neighbouring world.
  const int sx = log2_exact(world_w);
  const int sy = log2_exact(world_h);
  const int sz = log2_exact(world_d);
  if (sx < 0 || sy < 0 || sz < 0)
    return NULL;

  ShadowWorld *sw = (ShadowWorld *)calloc(1, sizeof(ShadowWorld));
  if (!sw)
    return NULL;

  sw->world_w = world_w;
  sw->world_h = world_h;
  sw->world_d = world_d;
  sw->shift_x = sx;
  sw->shift_y = sy;
  sw->shift_z = sz;
  sw->mask_x = (int)world_w - 1;
  sw->mask_y = (int)world_h - 1;
  sw->mask_z = (int)world_d - 1;
  sw->extent_x = (int)world_w * SHADOW_CLUSTER_DIM;
  sw->extent_y = (int)world_h * SHADOW_CLUSTER_DIM;
  sw->extent_z = (int)world_d * SHADOW_CLUSTER_DIM;

  // Blocks must not straddle a slot boundary, or a per-slot rebuild could not be exact. With
  // power-of-two dimensions this reduces to being at least as large as the coarsest block.
  sw->pyramid_enabled = (world_w % (uint32_t)SHADOW_L3_BLOCK) == 0u &&
                        (world_h % (uint32_t)SHADOW_L3_BLOCK) == 0u &&
                        (world_d % (uint32_t)SHADOW_L3_BLOCK) == 0u;

  if (sw->pyramid_enabled)
  {
    if (!level_alloc(&sw->l1, SHADOW_L1_BLOCK, sw->extent_x, sw->extent_y, sw->extent_z) ||
        !level_alloc(&sw->l2, SHADOW_L2_BLOCK, sw->extent_x, sw->extent_y, sw->extent_z) ||
        !level_alloc(&sw->l3, SHADOW_L3_BLOCK, sw->extent_x, sw->extent_y, sw->extent_z))
    {
      shadow_world_destroy(sw);
      return NULL;
    }
    sw->brick_dim_x = (int)world_w / SHADOW_L1_BLOCK;
    sw->brick_dim_y = (int)world_h / SHADOW_L1_BLOCK;
    sw->brick_dim_z = (int)world_d / SHADOW_L1_BLOCK;
    sw->density_dim_x = (int)world_w / SHADOW_DENSITY_BLOCK;
    sw->density_dim_y = (int)world_h / SHADOW_DENSITY_BLOCK;
    sw->density_dim_z = (int)world_d / SHADOW_DENSITY_BLOCK;
  }

  return sw;
}

void shadow_world_destroy(ShadowWorld *sw)
{
  if (!sw)
    return;
  // The World pointers are borrowed; freeing them here would pull the ground out from under the
  // universe that owns them.
  free(sw->l1.bits);
  free(sw->l2.bits);
  free(sw->l3.bits);
  for (int i = 0; i < SHADOW_SLOT_COUNT; i++)
  {
    free(sw->slot_brick_masks[i]);
    free(sw->slot_density[i]);
    free(sw->slot_emissive[i]);
  }
  free(sw);
}

size_t shadow_world_pyramid_bytes(const ShadowWorld *sw)
{
  if (!sw)
    return 0u;
  size_t n = sw->l1.bytes + sw->l2.bytes + sw->l3.bytes;
  const size_t brick_n =
      (size_t)sw->brick_dim_x * (size_t)sw->brick_dim_y * (size_t)sw->brick_dim_z;
  const size_t dens_n =
      (size_t)sw->density_dim_x * (size_t)sw->density_dim_y * (size_t)sw->density_dim_z;
  for (int i = 0; i < SHADOW_SLOT_COUNT; i++)
  {
    if (sw->slot_brick_masks[i])
      n += brick_n * sizeof(uint64_t);
    if (sw->slot_density[i])
      n += dens_n;
    if (sw->slot_emissive[i])
      n += dens_n;
  }
  return n;
}

World *shadow_world_slot_world(const ShadowWorld *sw, int slot)
{
  if (!sw || slot < 0 || slot >= SHADOW_SLOT_COUNT)
    return NULL;
  return sw->slots[slot];
}

void shadow_world_slot_origin(const ShadowWorld *sw, int slot, int *ox, int *oy, int *oz)
{
  int dx = 0, dy = 0, dz = 0;
  shadow_slot_offsets(slot, &dx, &dy, &dz);
  if (ox)
    *ox = (dx + SHADOW_CLUSTER_RADIUS) * (int)sw->world_w;
  if (oy)
    *oy = (dy + SHADOW_CLUSTER_RADIUS) * (int)sw->world_h;
  if (oz)
    *oz = (dz + SHADOW_CLUSTER_RADIUS) * (int)sw->world_d;
}

int shadow_world_slot_of(const ShadowWorld *sw, int cx, int cy, int cz)
{
  if (!sw)
    return -1;
  return shadow_world_route(sw, cx, cy, cz, NULL, NULL, NULL);
}

void shadow_world_invalidate_slot(ShadowWorld *sw, int slot)
{
  if (!sw || slot < 0 || slot >= SHADOW_SLOT_COUNT)
    return;
  sw->pyramid_valid_mask &= ~SHADOW_SLOT_BIT(slot);
}

bool shadow_world_attach(ShadowWorld *sw, int dx, int dy, int dz, World *world)
{
  if (!sw || dx < -SHADOW_CLUSTER_RADIUS || dx > SHADOW_CLUSTER_RADIUS || dy < -SHADOW_CLUSTER_RADIUS || dy > SHADOW_CLUSTER_RADIUS || dz < -SHADOW_CLUSTER_RADIUS || dz > SHADOW_CLUSTER_RADIUS)
    return false;

  const int slot = shadow_slot_index(dx, dy, dz);

  // A slot whose dimensions differ from the cluster's would break shift/mask routing for every
  // coordinate in it, so refuse rather than mis-route. World_autocrop can resize a world, and a
  // saved world can be loaded at any size, so this is reachable rather than theoretical.
  //
  // Refusing has to *clear* the slot, not leave it alone. Callers resync from the universe and
  // ignore the return; leaving the previous pointer in place would keep querying a world that has
  // since moved cell or been evicted, which is a wrong answer at best and a freed read at worst.
  if (world && (world->width != sw->world_w || world->height != sw->world_h ||
                world->depth != sw->world_d))
  {
    if (sw->slots[slot])
    {
      sw->slots[slot] = NULL;
      sw->loaded_mask &= ~SHADOW_SLOT_BIT(slot);
      shadow_world_invalidate_slot(sw, slot);
    }
    return false;
  }

  // Re-attaching the same world must not invalidate its coverage. Callers resync the whole cluster
  // from the universe every frame, and treating that as SHADOW_SLOT_COUNT changes would rebuild the entire pyramid
  // every frame — which costs more than everything the pyramid saves.
  if (sw->slots[slot] == world)
    return true;

  sw->slots[slot] = world;
  if (world)
    sw->loaded_mask |= SHADOW_SLOT_BIT(slot);
  else
    sw->loaded_mask &= ~SHADOW_SLOT_BIT(slot);
  shadow_world_invalidate_slot(sw, slot);
  return true;
}

void shadow_world_set_centre(ShadowWorld *sw, uint64_t ux, uint64_t uy, uint64_t uz)
{
  if (!sw)
    return;
  sw->centre_x = ux;
  sw->centre_y = uy;
  sw->centre_z = uz;
}

int shadow_world_recenter(ShadowWorld *sw, uint64_t ux, uint64_t uy, uint64_t uz,
                         int *out_delta_x, int *out_delta_y, int *out_delta_z)
{
  if (!sw)
    return 0;

  // Signed arithmetic on the cell indices, because universe keys are uint64_t and a cell just below
  // the origin wraps to a huge value. The difference is what matters and it is small.
  const int64_t mdx = (int64_t)ux - (int64_t)sw->centre_x;
  const int64_t mdy = (int64_t)uy - (int64_t)sw->centre_y;
  const int64_t mdz = (int64_t)uz - (int64_t)sw->centre_z;

  if (out_delta_x)
    *out_delta_x = (int)(-mdx * (int64_t)sw->world_w);
  if (out_delta_y)
    *out_delta_y = (int)(-mdy * (int64_t)sw->world_h);
  if (out_delta_z)
    *out_delta_z = (int)(-mdz * (int64_t)sw->world_d);

  World *moved[SHADOW_SLOT_COUNT];
  memset(moved, 0, sizeof(moved));

  int retained = 0;
  for (int nz = -SHADOW_CLUSTER_RADIUS; nz <= SHADOW_CLUSTER_RADIUS; nz++)
  {
    for (int ny = -SHADOW_CLUSTER_RADIUS; ny <= SHADOW_CLUSTER_RADIUS; ny++)
    {
      for (int nx = -SHADOW_CLUSTER_RADIUS; nx <= SHADOW_CLUSTER_RADIUS; nx++)
      {
        // The cell that will sit at this new offset was at this offset under the old framing.
        const int64_t ox = mdx + nx;
        const int64_t oy = mdy + ny;
        const int64_t oz = mdz + nz;
        if (ox < -SHADOW_CLUSTER_RADIUS || ox > SHADOW_CLUSTER_RADIUS || oy < -SHADOW_CLUSTER_RADIUS || oy > SHADOW_CLUSTER_RADIUS || oz < -SHADOW_CLUSTER_RADIUS || oz > SHADOW_CLUSTER_RADIUS)
          continue;

        World *w = sw->slots[shadow_slot_index((int)ox, (int)oy, (int)oz)];
        if (!w)
          continue;
        moved[shadow_slot_index(nx, ny, nz)] = w;
        retained++;
      }
    }
  }

  memcpy(sw->slots, moved, sizeof(moved));
  sw->centre_x = ux;
  sw->centre_y = uy;
  sw->centre_z = uz;

  sw->loaded_mask = 0;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (sw->slots[slot])
      sw->loaded_mask |= SHADOW_SLOT_BIT(slot);
  }

  // Pyramid bits are addressed by cluster position, so every retained world now sits under
  // different bits. Rebuilding all of them is the honest cost of a move; it happens on a world
  // transition rather than per frame, and the benchmark reports what it costs.
  sw->pyramid_valid_mask = 0;

  return retained;
}

// ---------------------------------------------------------------------------
// Reading a slot's occupancy

// Hoists everything the per-cell solidity test needs out of the World, so the inner loop is not
// re-checking whether a bitfield exists and whether its dimensions still match.
typedef struct
{
  const uint8_t *bits; // NULL when the world has no usable bitfield and voxels must be read
  const Voxel *voxels;
  uint32_t w, h, d;
} SlotReader;

static SlotReader slot_reader(const World *world)
{
  SlotReader r;
  r.voxels = world->voxels;
  r.w = world->width;
  r.h = world->height;
  r.d = world->depth;
  r.bits = NULL;
  if (world->occupancy_bits && world->occupancy_bits->bits &&
      world->occupancy_bits->width == world->width &&
      world->occupancy_bits->height == world->height &&
      world->occupancy_bits->depth == world->depth)
    r.bits = world->occupancy_bits->bits;
  return r;
}

static inline size_t reader_linear(const SlotReader *r, int lx, int ly, int lz)
{
  return ((size_t)lz * (size_t)r->h + (size_t)ly) * (size_t)r->w + (size_t)lx;
}

static inline bool reader_solid(const SlotReader *r, int lx, int ly, int lz)
{
  const size_t lin = reader_linear(r, lx, ly, lz);
  if (r->bits)
    return bitfield_get(r->bits, lin);
  return r->voxels[lin].type != VOXEL_AIR;
}

// ---------------------------------------------------------------------------
// Pyramid

static size_t slot_brick_index(const ShadowWorld *sw, int bx, int by, int bz)
{
  return ((size_t)bz * (size_t)sw->brick_dim_y + (size_t)by) * (size_t)sw->brick_dim_x +
         (size_t)bx;
}

static size_t slot_density_index(const ShadowWorld *sw, int bx, int by, int bz)
{
  return ((size_t)bz * (size_t)sw->density_dim_y + (size_t)by) * (size_t)sw->density_dim_x +
         (size_t)bx;
}

static void slot_accel_free(ShadowWorld *sw, int slot)
{
  free(sw->slot_brick_masks[slot]);
  free(sw->slot_density[slot]);
  free(sw->slot_emissive[slot]);
  sw->slot_brick_masks[slot] = NULL;
  sw->slot_density[slot] = NULL;
  sw->slot_emissive[slot] = NULL;
}

static bool slot_accel_ensure(ShadowWorld *sw, int slot)
{
  if (sw->slot_brick_masks[slot] && sw->slot_density[slot] && sw->slot_emissive[slot])
    return true;
  const size_t brick_n =
      (size_t)sw->brick_dim_x * (size_t)sw->brick_dim_y * (size_t)sw->brick_dim_z;
  const size_t dens_n =
      (size_t)sw->density_dim_x * (size_t)sw->density_dim_y * (size_t)sw->density_dim_z;
  if (!sw->slot_brick_masks[slot])
    sw->slot_brick_masks[slot] =
        (uint64_t *)calloc(brick_n ? brick_n : 1u, sizeof(uint64_t));
  if (!sw->slot_density[slot])
    sw->slot_density[slot] = (uint8_t *)calloc(dens_n ? dens_n : 1u, 1u);
  if (!sw->slot_emissive[slot])
    sw->slot_emissive[slot] = (uint8_t *)calloc(dens_n ? dens_n : 1u, 1u);
  return sw->slot_brick_masks[slot] && sw->slot_density[slot] && sw->slot_emissive[slot];
}

static void pyramid_clear_slot(ShadowWorld *sw, int slot)
{
  if (!sw->pyramid_enabled)
    return;

  int ox = 0, oy = 0, oz = 0;
  shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);

  ShadowPyramidLevel *levels[3] = {&sw->l1, &sw->l2, &sw->l3};
  for (int li = 0; li < 3; li++)
  {
    ShadowPyramidLevel *lv = levels[li];
    const int bx0 = ox >> lv->shift, bx1 = (ox + (int)sw->world_w) >> lv->shift;
    const int by0 = oy >> lv->shift, by1 = (oy + (int)sw->world_h) >> lv->shift;
    const int bz0 = oz >> lv->shift, bz1 = (oz + (int)sw->world_d) >> lv->shift;
    for (int bz = bz0; bz < bz1; bz++)
      for (int by = by0; by < by1; by++)
        for (int bx = bx0; bx < bx1; bx++)
          level_set(lv, bx, by, bz, false);
  }

  slot_accel_free(sw, slot);
}

static uint8_t voxel_emissive_u8(VoxelType t)
{
  switch (t)
  {
  case VOXEL_MAGMA:
  case VOXEL_SPRING_MAGMA:
    return 255;
  case VOXEL_CRYSTAL:
  case VOXEL_CRYSTAL_RED:
  case VOXEL_CRYSTAL_GREEN:
  case VOXEL_CRYSTAL_BLUE:
    return 140;
  default:
    return 0;
  }
}

static void pyramid_rebuild_slot(ShadowWorld *sw, int slot)
{
  if (!sw->pyramid_enabled)
    return;

  const World *world = sw->slots[slot];
  if (!world || !world->voxels)
  {
    pyramid_clear_slot(sw, slot);
    return;
  }

  int ox = 0, oy = 0, oz = 0;
  shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);
  const SlotReader r = slot_reader(world);

  // Level 1 + brick masks straight from the per-voxel occupancy: one bit per 4-cube, and a
  // uint64 child mask packing every voxel inside that cube (Devlog #17).
  if (!slot_accel_ensure(sw, slot))
  {
    pyramid_clear_slot(sw, slot);
    return;
  }
  uint64_t *bricks = sw->slot_brick_masks[slot];
  uint8_t *density = sw->slot_density[slot];
  uint8_t *emissive = sw->slot_emissive[slot];

  const int b = SHADOW_L1_BLOCK;
  const int nx = sw->brick_dim_x, ny = sw->brick_dim_y, nz = sw->brick_dim_z;
  const int l1x0 = ox >> sw->l1.shift, l1y0 = oy >> sw->l1.shift, l1z0 = oz >> sw->l1.shift;

  for (int bz = 0; bz < nz; bz++)
  {
    for (int by = 0; by < ny; by++)
    {
      for (int bx = 0; bx < nx; bx++)
      {
        uint64_t mask = 0ull;
        for (int lz = 0; lz < b; lz++)
          for (int ly = 0; ly < b; ly++)
            for (int lx = 0; lx < b; lx++)
            {
              if (reader_solid(&r, bx * b + lx, by * b + ly, bz * b + lz))
                mask |= (uint64_t)1ull << shadow_brick_bit(lx, ly, lz);
            }

        const int gbx = l1x0 + bx, gby = l1y0 + by, gbz = l1z0 + bz;
        level_set(&sw->l1, gbx, gby, gbz, mask != 0ull);
        bricks[slot_brick_index(sw, bx, by, bz)] = mask;
      }
    }
  }

  // Levels 2 and 3 fold up from the level below, which is four blocks per axis each time.
  struct
  {
    ShadowPyramidLevel *dst;
    const ShadowPyramidLevel *src;
  } folds[2] = {{&sw->l2, &sw->l1}, {&sw->l3, &sw->l2}};

  for (int fi = 0; fi < 2; fi++)
  {
    ShadowPyramidLevel *dst = folds[fi].dst;
    const ShadowPyramidLevel *src = folds[fi].src;
    const int ratio = dst->block / src->block;
    const int dx0 = ox >> dst->shift, dy0 = oy >> dst->shift, dz0 = oz >> dst->shift;
    const int dnx = (int)sw->world_w / dst->block;
    const int dny = (int)sw->world_h / dst->block;
    const int dnz = (int)sw->world_d / dst->block;

    for (int bz = 0; bz < dnz; bz++)
    {
      for (int by = 0; by < dny; by++)
      {
        for (int bx = 0; bx < dnx; bx++)
        {
          const int sx0 = (dx0 + bx) * ratio, sy0 = (dy0 + by) * ratio, sz0 = (dz0 + bz) * ratio;
          bool any = false;
          for (int sz = sz0; !any && sz < sz0 + ratio; sz++)
            for (int sy = sy0; !any && sy < sy0 + ratio; sy++)
              for (int sx = sx0; sx < sx0 + ratio; sx++)
              {
                if (level_get(src, sx, sy, sz))
                {
                  any = true;
                  break;
                }
              }
          level_set(dst, dx0 + bx, dy0 + by, dz0 + bz, any);
        }
      }
    }
  }

  // VVAO density + emissive glow maps at L2 (16³). Count solids and max emissive per brick.
  {
    const int db = SHADOW_DENSITY_BLOCK;
    const int dnx = sw->density_dim_x;
    const int dny = sw->density_dim_y;
    const int dnz = sw->density_dim_z;
    const int volume = SHADOW_DENSITY_VOXELS;

    for (int bz = 0; bz < dnz; bz++)
    {
      for (int by = 0; by < dny; by++)
      {
        for (int bx = 0; bx < dnx; bx++)
        {
          int solid = 0;
          uint8_t glow = 0;
          for (int lz = bz * db; lz < bz * db + db; lz++)
            for (int ly = by * db; ly < by * db + db; ly++)
              for (int lx = bx * db; lx < bx * db + db; lx++)
              {
                if (!reader_solid(&r, lx, ly, lz))
                  continue;
                solid++;
                VoxelType t = VOXEL_STONE;
                if (r.voxels)
                  t = r.voxels[reader_linear(&r, lx, ly, lz)].type;
                const uint8_t e = voxel_emissive_u8(t);
                if (e > glow)
                  glow = e;
              }
          const size_t idx = slot_density_index(sw, bx, by, bz);
          density[idx] = (uint8_t)((solid * 255) / volume);
          emissive[idx] = glow;
        }
      }
    }
  }
}

int shadow_world_refresh_budget(ShadowWorld *sw, int max_rebuilds)
{
  if (!sw)
    return 0;
  if (max_rebuilds <= 0)
    max_rebuilds = SHADOW_SLOT_COUNT;

  // Priority: centre, then the six face neighbours, then the rest of the cluster. Gameplay
  // queries (fog reveal, AI) care most about the player's cell and its edges.
  int order[SHADOW_SLOT_COUNT];
  int order_n = 0;
  order[order_n++] = SHADOW_CENTRE_SLOT;

  static const int k_faces[6][3] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
  };
  for (int i = 0; i < 6; i++)
    order[order_n++] = shadow_slot_index(k_faces[i][0], k_faces[i][1], k_faces[i][2]);

  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (slot == SHADOW_CENTRE_SLOT)
      continue;
    bool is_face = false;
    for (int i = 0; i < 6; i++)
    {
      if (slot == shadow_slot_index(k_faces[i][0], k_faces[i][1], k_faces[i][2]))
      {
        is_face = true;
        break;
      }
    }
    if (!is_face)
      order[order_n++] = slot;
  }

  int rebuilt = 0;
  for (int oi = 0; oi < order_n && rebuilt < max_rebuilds; oi++)
  {
    const int slot = order[oi];
    const ShadowSlotMask bit = SHADOW_SLOT_BIT(slot);
    const World *world = sw->slots[slot];

    if (!world)
    {
      if (sw->pyramid_valid_mask & bit)
      {
        pyramid_clear_slot(sw, slot);
        sw->pyramid_valid_mask &= ~bit;
        rebuilt++;
      }
      continue;
    }

    const bool valid = (sw->pyramid_valid_mask & bit) != 0u;
    if (valid && sw->slot_revision[slot] == world->voxel_revision)
      continue;

    pyramid_rebuild_slot(sw, slot);
    sw->slot_revision[slot] = world->voxel_revision;
    sw->pyramid_valid_mask |= bit;
    rebuilt++;
  }
  return rebuilt;
}

int shadow_world_refresh(ShadowWorld *sw)
{
  return shadow_world_refresh_budget(sw, 0);
}

// ---------------------------------------------------------------------------
// Point queries

ShadowSample shadow_world_sample(const ShadowWorld *sw, int cx, int cy, int cz)
{
  if (!sw)
    return SHADOW_UNLOADED;

  int lx = 0, ly = 0, lz = 0;
  const int slot = shadow_world_route(sw, cx, cy, cz, &lx, &ly, &lz);
  if (slot < 0)
    return SHADOW_UNLOADED;

  const World *world = sw->slots[slot];
  if (!world || !world->voxels)
    return SHADOW_UNLOADED;

  const SlotReader r = slot_reader(world);
  return reader_solid(&r, lx, ly, lz) ? SHADOW_SOLID : SHADOW_EMPTY;
}

VoxelType shadow_world_type_at(const ShadowWorld *sw, int cx, int cy, int cz)
{
  if (!sw)
    return VOXEL_AIR;

  int lx = 0, ly = 0, lz = 0;
  const int slot = shadow_world_route(sw, cx, cy, cz, &lx, &ly, &lz);
  if (slot < 0)
    return VOXEL_AIR;

  const World *world = sw->slots[slot];
  if (!world || !world->voxels)
    return VOXEL_AIR;

  const size_t lin = ((size_t)lz * world->height + (size_t)ly) * world->width + (size_t)lx;
  return world->voxels[lin].type;
}

int shadow_world_surface_height(const ShadowWorld *sw, int cx, int cy)
{
  if (!sw)
    return -1;
  if (cx < 0 || cy < 0 || cx >= sw->extent_x || cy >= sw->extent_y)
    return -1;

  const int lx = cx & sw->mask_x;
  const int ly = cy & sw->mask_y;
  const int sx = cx >> sw->shift_x;
  const int sy = cy >> sw->shift_y;

  // Top down, so the first world with anything in this column wins. Routed to the worlds' own
  // accelerated column lookup rather than rescanning: world_height_at_cached reads the heightmap
  // when one exists and falls back to the bitfield otherwise.
  for (int sz = SHADOW_CLUSTER_DIM - 1; sz >= 0; sz--)
  {
    const int slot = (sz * SHADOW_CLUSTER_DIM + sy) * SHADOW_CLUSTER_DIM + sx;
    const World *world = sw->slots[slot];
    if (!world)
      continue;
    const int h = world_height_at_cached(world, lx, ly);
    if (h >= 0)
      return sz * (int)sw->world_d + h;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// The one shared traversal
//
// Every search below funnels through this. The order of the tests is the whole performance
// argument: slot mask, then loaded, then z band, then the three pyramid levels, then the per-voxel
// occupancy bit, and only then the 48-byte Voxel read. A solid_only search over the cluster touches
// 27 x 256KB of bits rather than 27 x 96MB of voxels.
//
// Note that the ladder enumerates *solid* cells: the occupancy bit is a rejection, so air is never
// visited. Searching for empty space is a different problem and deliberately not expressible here.

ShadowFilter shadow_filter_all(void)
{
  ShadowFilter f;
  memset(&f, 0, sizeof(f));
  f.region = SHADOW_REGION_ALL;
  return f;
}

typedef struct
{
  const ShadowWorld *sw;
  const ShadowFilter *f;
  ShadowVisitFn fn;
  void *user;
  int visits;
  bool stopped;
} SearchCtx;

static inline bool type_matches(const ShadowFilter *f, VoxelType t)
{
  if (f->type_count <= 0 || !f->types)
    return true;
  for (int i = 0; i < f->type_count; i++)
  {
    if (f->types[i] == t)
      return true;
  }
  return false;
}

static inline bool condition_matches(const ShadowFilter *f, uint64_t mask)
{
  if (f->condition_all && (mask & f->condition_all) != f->condition_all)
    return false;
  if (f->condition_any && (mask & f->condition_any) == 0ULL)
    return false;
  return true;
}

static inline bool sphere_contains(const ShadowFilter *f, int cx, int cy, int cz)
{
  const float dx = (float)cx - f->r.sphere.cx;
  const float dy = (float)cy - f->r.sphere.cy;
  const float dz = (float)cz - f->r.sphere.cz;
  return (dx * dx + dy * dy + dz * dz) <= (f->r.sphere.radius * f->r.sphere.radius);
}

static bool emit_hit(SearchCtx *ctx, int slot, int cx, int cy, int cz,
                     int lx, int ly, int lz, const Voxel *v, bool unloaded)
{
  ShadowHit hit;
  hit.cx = cx;
  hit.cy = cy;
  hit.cz = cz;
  hit.slot = slot;
  hit.lx = lx;
  hit.ly = ly;
  hit.lz = lz;
  hit.type = v ? v->type : VOXEL_AIR;
  hit.voxel = v;
  hit.unloaded = unloaded;

  ctx->visits++;
  g_stats.hits++;

  if (ctx->fn && !ctx->fn(&hit, ctx->user))
  {
    ctx->stopped = true;
    return false;
  }
  if (ctx->f->max_results > 0 && ctx->visits >= ctx->f->max_results)
  {
    ctx->stopped = true;
    return false;
  }
  return true;
}

static void filter_bbox(const ShadowWorld *sw, const ShadowFilter *f,
                        int *x0, int *y0, int *z0, int *x1, int *y1, int *z1)
{
  int ax0 = 0, ay0 = 0, az0 = 0;
  int ax1 = sw->extent_x - 1, ay1 = sw->extent_y - 1, az1 = sw->extent_z - 1;

  switch (f->region)
  {
  case SHADOW_REGION_BOX:
    ax0 = f->r.box.x0;
    ay0 = f->r.box.y0;
    az0 = f->r.box.z0;
    ax1 = f->r.box.x1;
    ay1 = f->r.box.y1;
    az1 = f->r.box.z1;
    break;
  case SHADOW_REGION_SPHERE:
  {
    const float rad = f->r.sphere.radius;
    ax0 = (int)floorf(f->r.sphere.cx - rad);
    ay0 = (int)floorf(f->r.sphere.cy - rad);
    az0 = (int)floorf(f->r.sphere.cz - rad);
    ax1 = (int)ceilf(f->r.sphere.cx + rad);
    ay1 = (int)ceilf(f->r.sphere.cy + rad);
    az1 = (int)ceilf(f->r.sphere.cz + rad);
    break;
  }
  case SHADOW_REGION_COLUMN:
    ax0 = ax1 = f->r.column.x;
    ay0 = ay1 = f->r.column.y;
    break;
  case SHADOW_REGION_ALL:
  case SHADOW_REGION_RAY:
  default:
    break;
  }

  if (f->use_z_band)
  {
    if (az0 < f->z_min)
      az0 = f->z_min;
    if (az1 > f->z_max)
      az1 = f->z_max;
  }

  if (ax0 < 0)
    ax0 = 0;
  if (ay0 < 0)
    ay0 = 0;
  if (az0 < 0)
    az0 = 0;
  if (ax1 > sw->extent_x - 1)
    ax1 = sw->extent_x - 1;
  if (ay1 > sw->extent_y - 1)
    ay1 = sw->extent_y - 1;
  if (az1 > sw->extent_z - 1)
    az1 = sw->extent_z - 1;

  *x0 = ax0;
  *y0 = ay0;
  *z0 = az0;
  *x1 = ax1;
  *y1 = ay1;
  *z1 = az1;
}

// Scans one slot's share of the region in strict z, y, x order, skipping runs of x through the
// pyramid. Keeping the order strict rather than block-major is what makes results reproducible.
static void search_slot_region(SearchCtx *ctx, int slot,
                               int bx0, int by0, int bz0, int bx1, int by1, int bz1)
{
  const ShadowWorld *sw = ctx->sw;
  const ShadowFilter *f = ctx->f;
  const World *world = sw->slots[slot];
  const SlotReader r = slot_reader(world);

  const bool need_vox = !f->solid_only;
  const bool sphere = (f->region == SHADOW_REGION_SPHERE);
  const bool use_pyr = sw->pyramid_enabled &&
                       (sw->pyramid_valid_mask & SHADOW_SLOT_BIT(slot)) != 0u;

  const int l1s = sw->l1.shift, l2s = sw->l2.shift, l3s = sw->l3.shift;

  for (int cz = bz0; cz <= bz1; cz++)
  {
    const int lz = cz & sw->mask_z;
    const int b1z = cz >> l1s, b2z = cz >> l2s, b3z = cz >> l3s;

    for (int cy = by0; cy <= by1; cy++)
    {
      const int ly = cy & sw->mask_y;
      const int b1y = cy >> l1s, b2y = cy >> l2s, b3y = cy >> l3s;

      int cx = bx0;
      while (cx <= bx1)
      {
        if (use_pyr)
        {
          if (!level_get(&sw->l3, cx >> l3s, b3y, b3z))
          {
            g_stats.blocks_rejected_l3++;
            cx = ((cx >> l3s) + 1) << l3s;
            continue;
          }
          if (!level_get(&sw->l2, cx >> l2s, b2y, b2z))
          {
            g_stats.blocks_rejected_l2++;
            cx = ((cx >> l2s) + 1) << l2s;
            continue;
          }
          if (!level_get(&sw->l1, cx >> l1s, b1y, b1z))
          {
            g_stats.blocks_rejected_l1++;
            cx = ((cx >> l1s) + 1) << l1s;
            continue;
          }
        }

        g_stats.cells_tested++;
        const int lx = cx & sw->mask_x;
        if (!reader_solid(&r, lx, ly, lz))
        {
          g_stats.cells_rejected_bit++;
          cx++;
          continue;
        }

        if (sphere && !sphere_contains(f, cx, cy, cz))
        {
          cx++;
          continue;
        }

        const Voxel *v = NULL;
        if (need_vox)
        {
          v = &r.voxels[reader_linear(&r, lx, ly, lz)];
          g_stats.voxels_read++;
          if (!type_matches(f, v->type) || !condition_matches(f, v->condition_mask))
          {
            cx++;
            continue;
          }
        }

        if (!emit_hit(ctx, slot, cx, cy, cz, lx, ly, lz, v, false))
          return;
        cx++;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Ray traversal
//
// Deliberately the same incremental DDA as world_raycast_first_hit, including its tie-breaks, so a
// cluster ray and a single-world ray visit the same cells in the same order and the two can be
// checked against each other. The pyramid is used to skip the *work* inside empty blocks — the bit
// lookup and voxel read, which are the cache misses — while the cell sequence stays identical.

typedef struct
{
  int vx, vy, vz;
  int step_x, step_y, step_z;
  float t_max_x, t_max_y, t_max_z;
  float t_delta_x, t_delta_y, t_delta_z;
} RayDDA;

static inline float ray_safe_inv(float v)
{
  const float eps = 1e-6f;
  if (v > -eps && v < eps)
    return 1e30f;
  return 1.0f / v;
}

static void ray_init(RayDDA *d, float ox, float oy, float oz, float dx, float dy, float dz)
{
  d->vx = (int)floorf(ox);
  d->vy = (int)floorf(oy);
  d->vz = (int)floorf(oz);

  d->step_x = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
  d->step_y = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);
  d->step_z = (dz > 0.0f) ? 1 : (dz < 0.0f ? -1 : 0);

  const float inv_x = ray_safe_inv(dx);
  const float inv_y = ray_safe_inv(dy);
  const float inv_z = ray_safe_inv(dz);

  const float bx = (float)d->vx + (d->step_x > 0 ? 1.0f : 0.0f);
  const float by = (float)d->vy + (d->step_y > 0 ? 1.0f : 0.0f);
  const float bz = (float)d->vz + (d->step_z > 0 ? 1.0f : 0.0f);

  d->t_max_x = (d->step_x != 0) ? (bx - ox) * inv_x : 1e30f;
  d->t_max_y = (d->step_y != 0) ? (by - oy) * inv_y : 1e30f;
  d->t_max_z = (d->step_z != 0) ? (bz - oz) * inv_z : 1e30f;

  d->t_delta_x = (float)fabs(1.0f * inv_x);
  d->t_delta_y = (float)fabs(1.0f * inv_y);
  d->t_delta_z = (float)fabs(1.0f * inv_z);
}

static inline void ray_step(RayDDA *d)
{
  if (d->t_max_x < d->t_max_y)
  {
    if (d->t_max_x < d->t_max_z)
    {
      d->vx += d->step_x;
      d->t_max_x += d->t_delta_x;
    }
    else
    {
      d->vz += d->step_z;
      d->t_max_z += d->t_delta_z;
    }
  }
  else
  {
    if (d->t_max_y < d->t_max_z)
    {
      d->vy += d->step_y;
      d->t_max_y += d->t_delta_y;
    }
    else
    {
      d->vz += d->step_z;
      d->t_max_z += d->t_delta_z;
    }
  }
}

static int search_ray(SearchCtx *ctx, int *out_steps)
{
  const ShadowWorld *sw = ctx->sw;
  const ShadowFilter *f = ctx->f;
  const int max_steps = f->r.ray.max_steps;

  if (out_steps)
    *out_steps = 0;
  if (max_steps <= 0)
    return 0;

  RayDDA d;
  ray_init(&d, f->r.ray.ox, f->r.ray.oy, f->r.ray.oz,
           f->r.ray.dx, f->r.ray.dy, f->r.ray.dz);

  const bool need_vox = !f->solid_only;
  int steps = 0;

  // Rebuilding the reader for every cell showed up as the ray being slower than a plain per-world
  // DDA, which defeats the point. A ray crosses at most three worlds, so caching it per slot removes
  // almost all of that work.
  int reader_slot = -1;
  SlotReader r;
  memset(&r, 0, sizeof(r));

  while (steps < max_steps)
  {
    int lx = 0, ly = 0, lz = 0;
    const int slot = shadow_world_route(sw, d.vx, d.vy, d.vz, &lx, &ly, &lz);

    if (slot < 0)
    {
      // Outside the cluster. Keep stepping rather than stopping: a ray may leave and re-enter, and
      // world_raycast_first_hit behaves the same way at its own bounds.
      ray_step(&d);
      steps++;
      continue;
    }

    if (f->slot_mask && !(f->slot_mask & SHADOW_SLOT_BIT(slot)))
    {
      ray_step(&d);
      steps++;
      continue;
    }

    const World *world = sw->slots[slot];
    if (!world)
    {
      g_stats.slots_rejected_unloaded++;
      if (f->report_unloaded)
      {
        if (!emit_hit(ctx, slot, d.vx, d.vy, d.vz, lx, ly, lz, NULL, true))
          break;
      }
      ray_step(&d);
      steps++;
      continue;
    }

    if (sw->pyramid_enabled && (sw->pyramid_valid_mask & SHADOW_SLOT_BIT(slot)))
    {
      int skip_shift = -1;
      if (!level_get(&sw->l3, d.vx >> sw->l3.shift, d.vy >> sw->l3.shift, d.vz >> sw->l3.shift))
      {
        g_stats.blocks_rejected_l3++;
        skip_shift = sw->l3.shift;
      }
      else if (!level_get(&sw->l2, d.vx >> sw->l2.shift, d.vy >> sw->l2.shift, d.vz >> sw->l2.shift))
      {
        g_stats.blocks_rejected_l2++;
        skip_shift = sw->l2.shift;
      }
      else if (!level_get(&sw->l1, d.vx >> sw->l1.shift, d.vy >> sw->l1.shift, d.vz >> sw->l1.shift))
      {
        g_stats.blocks_rejected_l1++;
        skip_shift = sw->l1.shift;
      }

      if (skip_shift >= 0)
      {
        const int bx = d.vx >> skip_shift, by = d.vy >> skip_shift, bz = d.vz >> skip_shift;
        do
        {
          ray_step(&d);
          steps++;
        } while (steps < max_steps &&
                 (d.vx >> skip_shift) == bx &&
                 (d.vy >> skip_shift) == by &&
                 (d.vz >> skip_shift) == bz);
        continue;
      }

      // L1 occupied: use the brick child mask so empty cells inside the 4³ do not touch the
      // occupancy bitfield (Devlog #17 register-resident empty-space skip).
      if (sw->slot_brick_masks[slot])
      {
        const int bx = lx >> sw->l1.shift;
        const int by = ly >> sw->l1.shift;
        const int bz = lz >> sw->l1.shift;
        const uint64_t brick = sw->slot_brick_masks[slot][slot_brick_index(sw, bx, by, bz)];
        const int bit = shadow_brick_bit(lx, ly, lz);
        if ((brick & ((uint64_t)1ull << bit)) == 0ull)
        {
          g_stats.cells_tested++;
          g_stats.cells_rejected_bit++;
          const int gbx = d.vx >> sw->l1.shift;
          const int gby = d.vy >> sw->l1.shift;
          const int gbz = d.vz >> sw->l1.shift;
          // Stay inside this brick while the bit is clear: bit-ops only, no reader reload.
          do
          {
            ray_step(&d);
            steps++;
            if (steps >= max_steps)
              break;
            if ((d.vx >> sw->l1.shift) != gbx || (d.vy >> sw->l1.shift) != gby ||
                (d.vz >> sw->l1.shift) != gbz)
              break;
            const int nbit = shadow_brick_bit(d.vx, d.vy, d.vz);
            if ((brick & ((uint64_t)1ull << nbit)) != 0ull)
              break; // solid cell — fall through to hit handling below after re-route
            g_stats.cells_tested++;
            g_stats.cells_rejected_bit++;
          } while (1);
          if (steps >= max_steps)
            break;
          if ((d.vx >> sw->l1.shift) != gbx || (d.vy >> sw->l1.shift) != gby ||
              (d.vz >> sw->l1.shift) != gbz)
            continue;
          {
            int nlx = 0, nly = 0, nlz = 0;
            const int nslot = shadow_world_route(sw, d.vx, d.vy, d.vz, &nlx, &nly, &nlz);
            if (nslot != slot)
              continue;
            lx = nlx;
            ly = nly;
            lz = nlz;
          }
        }
      }
    }

    g_stats.cells_tested++;
    if (slot != reader_slot)
    {
      r = slot_reader(world);
      reader_slot = slot;
    }
    if (reader_solid(&r, lx, ly, lz))
    {
      const Voxel *v = NULL;
      bool matched = true;
      if (need_vox)
      {
        v = &r.voxels[reader_linear(&r, lx, ly, lz)];
        g_stats.voxels_read++;
        matched = type_matches(f, v->type) && condition_matches(f, v->condition_mask);
      }
      if (matched)
      {
        if (!emit_hit(ctx, slot, d.vx, d.vy, d.vz, lx, ly, lz, v, false))
          break;
      }
    }
    else
    {
      g_stats.cells_rejected_bit++;
    }

    ray_step(&d);
    steps++;
  }

  if (out_steps)
    *out_steps = steps;
  return ctx->visits;
}

// ---------------------------------------------------------------------------
// Inverted-index fast path
//
// When a search names a condition, traversing the volume is the wrong shape: the answer is already
// written down in World.condition_voxel_indices, so the loop can be inverted to O(matches). The
// index is only trusted when the world says it is in sync — a stale index would make the answer
// depend on which path the search happened to pick, which is worse than having no index.

// Chooses the candidate lists for a slot. A condition_all search only has to walk one of the
// required bits, since the answer is a subset of each; the shortest is the cheapest. A pure
// condition_any search has to walk them all and merge.
static int index_candidates(const World *world, const ShadowFilter *f, int *bits, size_t *out_len)
{
  int count = 0;
  size_t total = 0;

  if (f->condition_all)
  {
    int best_bit = -1;
    size_t best_len = (size_t)-1;
    uint64_t mask = f->condition_all;
    while (mask)
    {
      const int bit = __builtin_ctzll(mask);
      mask &= mask - 1ULL;
      const size_t len = world->condition_voxel_indices[bit].size;
      if (len < best_len)
      {
        best_len = len;
        best_bit = bit;
      }
    }
    if (best_bit < 0)
      return 0;
    bits[count++] = best_bit;
    total = best_len;
  }
  else
  {
    uint64_t mask = f->condition_any;
    while (mask)
    {
      const int bit = __builtin_ctzll(mask);
      mask &= mask - 1ULL;
      bits[count++] = bit;
      total += world->condition_voxel_indices[bit].size;
    }
  }

  if (out_len)
    *out_len = total;
  return count;
}

static bool index_path_available(const ShadowWorld *sw, const ShadowFilter *f, size_t *out_len)
{
  if (!f->condition_any && !f->condition_all)
    return false;

  size_t total = 0;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (f->slot_mask && !(f->slot_mask & SHADOW_SLOT_BIT(slot)))
      continue;
    const World *world = sw->slots[slot];
    if (!world)
      continue;
    if (!world_condition_index_is_fresh(world))
      return false;

    int bits[64];
    size_t len = 0;
    index_candidates(world, f, bits, &len);
    total += len;
  }

  if (out_len)
    *out_len = total;
  return true;
}

static void search_slot_index(SearchCtx *ctx, int slot,
                              int bx0, int by0, int bz0, int bx1, int by1, int bz1)
{
  const ShadowWorld *sw = ctx->sw;
  const ShadowFilter *f = ctx->f;
  const World *world = sw->slots[slot];
  const SlotReader r = slot_reader(world);
  const bool sphere = (f->region == SHADOW_REGION_SPHERE);

  int sox = 0, soy = 0, soz = 0;
  shadow_world_slot_origin(sw, slot, &sox, &soy, &soz);

  int bits[64];
  const int nbits = index_candidates(world, f, bits, NULL);
  if (nbits <= 0)
    return;

  size_t cursor[64];
  for (int k = 0; k < nbits; k++)
    cursor[k] = 0;

  const size_t plane = (size_t)world->width * (size_t)world->height;

  // A straight k-way merge on ascending linear index. That keeps the visit order identical to the
  // traversal path (a linear index ascends in z, then y, then x) and drops the duplicates a voxel
  // carrying several of the requested bits would otherwise produce.
  for (;;)
  {
    uint32_t best = UINT32_MAX;
    bool found = false;
    for (int k = 0; k < nbits; k++)
    {
      const struct VoxelIndexList *list = &world->condition_voxel_indices[bits[k]];
      if (cursor[k] < list->size && (!found || list->indices[cursor[k]] < best))
      {
        best = list->indices[cursor[k]];
        found = true;
      }
    }
    if (!found)
      break;

    for (int k = 0; k < nbits; k++)
    {
      const struct VoxelIndexList *list = &world->condition_voxel_indices[bits[k]];
      while (cursor[k] < list->size && list->indices[cursor[k]] == best)
        cursor[k]++;
    }

    const int lz = (int)((size_t)best / plane);
    const size_t rem = (size_t)best - (size_t)lz * plane;
    const int ly = (int)(rem / world->width);
    const int lx = (int)(rem % world->width);

    const int cx = sox + lx, cy = soy + ly, cz = soz + lz;
    if (cx < bx0 || cx > bx1 || cy < by0 || cy > by1 || cz < bz0 || cz > bz1)
      continue;

    g_stats.cells_tested++;
    if (!reader_solid(&r, lx, ly, lz))
    {
      g_stats.cells_rejected_bit++;
      continue;
    }
    if (sphere && !sphere_contains(f, cx, cy, cz))
      continue;

    const Voxel *v = &r.voxels[reader_linear(&r, lx, ly, lz)];
    g_stats.voxels_read++;
    if (!type_matches(f, v->type) || !condition_matches(f, v->condition_mask))
      continue;

    if (!emit_hit(ctx, slot, cx, cy, cz, lx, ly, lz, v, false))
      return;
  }
}

// ---------------------------------------------------------------------------

int shadow_world_for_each(const ShadowWorld *sw, const ShadowFilter *f,
                          ShadowVisitFn fn, void *user)
{
  if (!sw || !f)
    return -1;

  // solid_only promises the search never reads a Voxel, so a filter that needs one contradicts it.
  // Silently honouring one and dropping the other would give a wrong answer that looks right.
  if (f->solid_only && (f->type_count > 0 || f->condition_any || f->condition_all))
    return -1;

  // The ladder rejects on the occupancy bit, so air cells are never visited and a filter asking for
  // them could only ever return nothing.
  for (int i = 0; i < f->type_count; i++)
  {
    if (f->types && f->types[i] == VOXEL_AIR)
      return -1;
  }

  SearchCtx ctx;
  ctx.sw = sw;
  ctx.f = f;
  ctx.fn = fn;
  ctx.user = user;
  ctx.visits = 0;
  ctx.stopped = false;

  if (f->region == SHADOW_REGION_RAY)
    return search_ray(&ctx, NULL);

  int x0, y0, z0, x1, y1, z1;
  filter_bbox(sw, f, &x0, &y0, &z0, &x1, &y1, &z1);
  if (x0 > x1 || y0 > y1 || z0 > z1)
    return 0;

  bool use_index = false;
  if (f->index_policy != SHADOW_INDEX_FORCE_TRAVERSAL)
  {
    size_t index_len = 0;
    if (index_path_available(sw, f, &index_len))
    {
      if (f->index_policy == SHADOW_INDEX_FORCE_INDEX)
      {
        use_index = true;
      }
      else
      {
        const double cells = (double)(x1 - x0 + 1) * (double)(y1 - y0 + 1) * (double)(z1 - z0 + 1);
        use_index = ((double)index_len * SHADOW_INDEX_COST_RATIO) < cells;
      }
    }
  }
  if (use_index)
    g_stats.index_searches++;
  else if (f->condition_any || f->condition_all)
    g_stats.traversal_searches++;

  for (int slot = 0; slot < SHADOW_SLOT_COUNT && !ctx.stopped; slot++)
  {
    g_stats.slots_considered++;

    if (f->slot_mask && !(f->slot_mask & SHADOW_SLOT_BIT(slot)))
    {
      g_stats.slots_rejected_mask++;
      continue;
    }

    int sox = 0, soy = 0, soz = 0;
    shadow_world_slot_origin(sw, slot, &sox, &soy, &soz);

    int sx0 = sox > x0 ? sox : x0;
    int sy0 = soy > y0 ? soy : y0;
    int sz0 = soz > z0 ? soz : z0;
    int sx1 = (sox + (int)sw->world_w - 1) < x1 ? (sox + (int)sw->world_w - 1) : x1;
    int sy1 = (soy + (int)sw->world_h - 1) < y1 ? (soy + (int)sw->world_h - 1) : y1;
    int sz1 = (soz + (int)sw->world_d - 1) < z1 ? (soz + (int)sw->world_d - 1) : z1;
    if (sx0 > sx1 || sy0 > sy1 || sz0 > sz1)
      continue;

    const World *world = sw->slots[slot];
    if (!world || !world->voxels)
    {
      g_stats.slots_rejected_unloaded++;
      if (f->report_unloaded)
      {
        int lx = sx0 & sw->mask_x, ly = sy0 & sw->mask_y, lz = sz0 & sw->mask_z;
        if (!emit_hit(&ctx, slot, sx0, sy0, sz0, lx, ly, lz, NULL, true))
          break;
      }
      continue;
    }

    // A world that knows which z layers hold anything can hand back the rest for free. -1 means it
    // does not know, and clamping to that would drop real terrain.
    if (world->occupied_z_min >= 0)
    {
      const int wz0 = soz + world->occupied_z_min;
      const int wz1 = soz + world->occupied_z_max;
      if (sz0 < wz0)
        sz0 = wz0;
      if (sz1 > wz1)
        sz1 = wz1;
      if (sz0 > sz1)
      {
        g_stats.slots_rejected_zband++;
        continue;
      }
    }

    if (use_index)
      search_slot_index(&ctx, slot, sx0, sy0, sz0, sx1, sy1, sz1);
    else
      search_slot_region(&ctx, slot, sx0, sy0, sz0, sx1, sy1, sz1);
  }

  return ctx.visits;
}

int shadow_world_for_each_in_slot(const ShadowWorld *sw, int slot, const ShadowFilter *f,
                                  ShadowVisitFn fn, void *user)
{
  if (!sw || !f || slot < 0 || slot >= SHADOW_SLOT_COUNT)
    return -1;
  ShadowFilter local = *f;
  local.slot_mask = SHADOW_SLOT_BIT(slot);
  return shadow_world_for_each(sw, &local, fn, user);
}

// ---------------------------------------------------------------------------
// Search kinds. Each one configures the traversal above; none of them steps over voxels itself.

static bool ray_visit(const ShadowHit *hit, void *user)
{
  ShadowRayResult *res = (ShadowRayResult *)user;
  res->cx = hit->cx;
  res->cy = hit->cy;
  res->cz = hit->cz;
  res->slot = hit->slot;
  if (hit->unloaded)
  {
    res->stopped_unloaded = true;
    res->hit = false;
    res->type = VOXEL_AIR;
  }
  else
  {
    res->hit = true;
    res->type = hit->type;
  }
  return false; // the first thing the ray reaches is the answer
}

static bool raycast_filtered(const ShadowWorld *sw,
                             float ox, float oy, float oz,
                             float dx, float dy, float dz,
                             int max_steps, ShadowSlotMask slot_mask,
                             bool solid_only, bool report_unloaded,
                             ShadowRayResult *out)
{
  if (!sw || !out)
    return false;

  memset(out, 0, sizeof(*out));

  ShadowFilter f = shadow_filter_all();
  f.region = SHADOW_REGION_RAY;
  f.r.ray.ox = ox;
  f.r.ray.oy = oy;
  f.r.ray.oz = oz;
  f.r.ray.dx = dx;
  f.r.ray.dy = dy;
  f.r.ray.dz = dz;
  f.r.ray.max_steps = max_steps;
  f.slot_mask = slot_mask;
  f.max_results = 1;
  f.solid_only = solid_only;
  f.report_unloaded = report_unloaded;

  SearchCtx ctx;
  ctx.sw = sw;
  ctx.f = &f;
  ctx.fn = ray_visit;
  ctx.user = out;
  ctx.visits = 0;
  ctx.stopped = false;

  int steps = 0;
  search_ray(&ctx, &steps);
  out->steps = steps;
  return out->hit;
}

bool shadow_world_raycast(const ShadowWorld *sw,
                          float ox, float oy, float oz,
                          float dx, float dy, float dz,
                          int max_steps, ShadowSlotMask slot_mask,
                          ShadowRayResult *out)
{
  // A gameplay ray has to be told when it leaves loaded space. Assuming empty would let a shot
  // pass through terrain that simply has not streamed in yet.
  return raycast_filtered(sw, ox, oy, oz, dx, dy, dz, max_steps, slot_mask, false, true, out);
}

bool shadow_world_raycast_occupancy(const ShadowWorld *sw,
                                    float ox, float oy, float oz,
                                    float dx, float dy, float dz,
                                    int max_steps, ShadowSlotMask slot_mask,
                                    ShadowRayResult *out)
{
  // Lighting: occupancy bits only, and a hole in the stream is sky rather than a shadow caster.
  return raycast_filtered(sw, ox, oy, oz, dx, dy, dz, max_steps, slot_mask, true, false, out);
}

typedef struct
{
  ShadowHit *out;
  int max;
  int count;
  int total;
} Collector;

static bool collect_visit(const ShadowHit *hit, void *user)
{
  Collector *c = (Collector *)user;
  c->total++;
  if (c->out && c->count < c->max)
    c->out[c->count++] = *hit;
  return true;
}

int shadow_world_find_in_box(const ShadowWorld *sw,
                             int x0, int y0, int z0, int x1, int y1, int z1,
                             const ShadowFilter *base,
                             ShadowHit *out, int max_out, int *out_total)
{
  if (out_total)
    *out_total = 0;
  if (!sw)
    return -1;

  ShadowFilter f = base ? *base : shadow_filter_all();
  f.region = SHADOW_REGION_BOX;
  f.r.box.x0 = x0;
  f.r.box.y0 = y0;
  f.r.box.z0 = z0;
  f.r.box.x1 = x1;
  f.r.box.y1 = y1;
  f.r.box.z1 = z1;

  Collector c = {out, max_out, 0, 0};
  const int visited = shadow_world_for_each(sw, &f, collect_visit, &c);
  if (visited < 0)
    return visited;
  if (out_total)
    *out_total = c.total;
  return c.count;
}

int shadow_world_find_in_radius(const ShadowWorld *sw,
                                float cx, float cy, float cz, float radius,
                                const ShadowFilter *base,
                                ShadowHit *out, int max_out, int *out_total)
{
  if (out_total)
    *out_total = 0;
  if (!sw)
    return -1;

  ShadowFilter f = base ? *base : shadow_filter_all();
  f.region = SHADOW_REGION_SPHERE;
  f.r.sphere.cx = cx;
  f.r.sphere.cy = cy;
  f.r.sphere.cz = cz;
  f.r.sphere.radius = radius;

  Collector c = {out, max_out, 0, 0};
  const int visited = shadow_world_for_each(sw, &f, collect_visit, &c);
  if (visited < 0)
    return visited;
  if (out_total)
    *out_total = c.total;
  return c.count;
}

static bool count_visit(const ShadowHit *hit, void *user)
{
  (void)hit;
  (*(long *)user)++;
  return true;
}

int shadow_world_count(const ShadowWorld *sw, const ShadowFilter *f)
{
  long n = 0;
  const int visited = shadow_world_for_each(sw, f, count_visit, &n);
  if (visited < 0)
    return visited;
  return (int)n;
}

typedef struct
{
  int cx, cy, cz;
  bool have;
  long best_d2;
  ShadowHit best;
} NearestCtx;

static bool nearest_visit(const ShadowHit *hit, void *user)
{
  NearestCtx *n = (NearestCtx *)user;
  const long dx = hit->cx - n->cx;
  const long dy = hit->cy - n->cy;
  const long dz = hit->cz - n->cz;
  const long d2 = dx * dx + dy * dy + dz * dz;
  // Strictly less-than, so ties resolve to whichever the fixed visit order reached first.
  if (!n->have || d2 < n->best_d2)
  {
    n->have = true;
    n->best_d2 = d2;
    n->best = *hit;
  }
  return true;
}

bool shadow_world_find_nearest(const ShadowWorld *sw, int cx, int cy, int cz, int max_radius,
                               const ShadowFilter *base, ShadowHit *out)
{
  if (!sw || max_radius < 0)
    return false;

  ShadowFilter f = base ? *base : shadow_filter_all();
  f.region = SHADOW_REGION_BOX;
  f.max_results = 0; // the shell loop terminates the search, not a result cap

  NearestCtx n;
  n.cx = cx;
  n.cy = cy;
  n.cz = cz;
  n.have = false;
  n.best_d2 = 0;
  memset(&n.best, 0, sizeof(n.best));

  // Expanding Chebyshev shells. Every cell in shell r is at Euclidean distance of at least r, so
  // once something has been found at d, no shell beyond d can improve on it and the search stops —
  // which is what makes the cost scale with the distance to the answer rather than with the volume.
  for (int r = 0; r <= max_radius; r++)
  {
    if (n.have && (long)r * (long)r > n.best_d2)
      break;

    // The shell as six disjoint boxes, so no cell is examined twice.
    int boxes[6][6];
    int box_count = 0;
    if (r == 0)
    {
      boxes[box_count][0] = cx;
      boxes[box_count][1] = cy;
      boxes[box_count][2] = cz;
      boxes[box_count][3] = cx;
      boxes[box_count][4] = cy;
      boxes[box_count][5] = cz;
      box_count++;
    }
    else
    {
      const int xl = cx - r, xh = cx + r;
      const int yl = cy - r, yh = cy + r;
      const int zl = cz - r, zh = cz + r;

      const int caps[2] = {zl, zh};
      for (int i = 0; i < 2; i++)
      {
        boxes[box_count][0] = xl;
        boxes[box_count][1] = yl;
        boxes[box_count][2] = caps[i];
        boxes[box_count][3] = xh;
        boxes[box_count][4] = yh;
        boxes[box_count][5] = caps[i];
        box_count++;
      }

      const int walls_y[2] = {yl, yh};
      for (int i = 0; i < 2; i++)
      {
        boxes[box_count][0] = xl;
        boxes[box_count][1] = walls_y[i];
        boxes[box_count][2] = zl + 1;
        boxes[box_count][3] = xh;
        boxes[box_count][4] = walls_y[i];
        boxes[box_count][5] = zh - 1;
        box_count++;
      }

      const int walls_x[2] = {xl, xh};
      for (int i = 0; i < 2; i++)
      {
        boxes[box_count][0] = walls_x[i];
        boxes[box_count][1] = yl + 1;
        boxes[box_count][2] = zl + 1;
        boxes[box_count][3] = walls_x[i];
        boxes[box_count][4] = yh - 1;
        boxes[box_count][5] = zh - 1;
        box_count++;
      }
    }

    for (int b = 0; b < box_count; b++)
    {
      f.r.box.x0 = boxes[b][0];
      f.r.box.y0 = boxes[b][1];
      f.r.box.z0 = boxes[b][2];
      f.r.box.x1 = boxes[b][3];
      f.r.box.y1 = boxes[b][4];
      f.r.box.z1 = boxes[b][5];
      if (f.r.box.x0 > f.r.box.x1 || f.r.box.y0 > f.r.box.y1 || f.r.box.z0 > f.r.box.z1)
        continue;
      if (shadow_world_for_each(sw, &f, nearest_visit, &n) < 0)
        return false;
    }
  }

  if (!n.have)
    return false;
  if (out)
    *out = n.best;
  return true;
}

// ---------------------------------------------------------------------------
// Parallel fan-out

typedef struct
{
  const ShadowWorld *sw;
  ShadowFilter f;
  int slot;
  ShadowHit *hits;
  int count;
  int cap;
  int total;
  bool oom;
} SlotTask;

static bool slot_task_visit(const ShadowHit *hit, void *user)
{
  SlotTask *t = (SlotTask *)user;
  t->total++;
  if (t->count >= t->cap)
  {
    const int grown_cap = t->cap ? t->cap * 2 : 256;
    ShadowHit *grown = (ShadowHit *)realloc(t->hits, (size_t)grown_cap * sizeof(ShadowHit));
    if (!grown)
    {
      t->oom = true;
      return false;
    }
    t->hits = grown;
    t->cap = grown_cap;
  }
  t->hits[t->count++] = *hit;
  return true;
}

static void slot_task_run(void *user)
{
  SlotTask *t = (SlotTask *)user;
  shadow_world_for_each(t->sw, &t->f, slot_task_visit, t);
}

int shadow_world_find_parallel(const ShadowWorld *sw, const ShadowFilter *f,
                               ShadowHit *out, int max_out, int *out_total)
{
  if (out_total)
    *out_total = 0;
  if (!sw || !f)
    return -1;
  // A ray is inherently ordered along its own path, so splitting it across slots would not give the
  // first hit. Run it serially rather than return a different answer than the serial path.
  if (f->region == SHADOW_REGION_RAY)
  {
    Collector c = {out, max_out, 0, 0};
    const int visited = shadow_world_for_each(sw, f, collect_visit, &c);
    if (visited < 0)
      return visited;
    if (out_total)
      *out_total = c.total;
    return c.count;
  }

  SlotTask tasks[SHADOW_SLOT_COUNT];
  memset(tasks, 0, sizeof(tasks));

  TaskGroup *group = task_group_create("shadow-search");

  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (f->slot_mask && !(f->slot_mask & SHADOW_SLOT_BIT(slot)))
      continue;

    tasks[slot].sw = sw;
    tasks[slot].f = *f;
    tasks[slot].f.slot_mask = SHADOW_SLOT_BIT(slot);
    // The cap is applied when the per-slot results are concatenated. Applying it inside each task
    // would cap per slot and give a different answer than the serial search.
    tasks[slot].f.max_results = 0;
    tasks[slot].slot = slot;

    if (group)
      task_group_submit(group, slot_task_run, &tasks[slot]);
    else
      slot_task_run(&tasks[slot]);
  }

  if (group)
    task_group_destroy(group); // waits for every task before returning

  // Concatenating in slot order is what makes this identical to the serial search: the answer must
  // not depend on which worker happened to finish first.
  int written = 0;
  int total = 0;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    total += tasks[slot].total;
    for (int i = 0; i < tasks[slot].count; i++)
    {
      if (out && written < max_out)
        out[written++] = tasks[slot].hits[i];
    }
    free(tasks[slot].hits);
  }

  if (out_total)
    *out_total = total;
  return written;
}

uint64_t shadow_world_brick_mask_at(const ShadowWorld *sw, int cx, int cy, int cz)
{
  if (!sw || !sw->pyramid_enabled)
    return 0ull;
  int lx = 0, ly = 0, lz = 0;
  const int slot = shadow_world_route(sw, cx, cy, cz, &lx, &ly, &lz);
  if (slot < 0 || !sw->slot_brick_masks[slot])
    return 0ull;
  const int bx = lx >> sw->l1.shift;
  const int by = ly >> sw->l1.shift;
  const int bz = lz >> sw->l1.shift;
  return sw->slot_brick_masks[slot][slot_brick_index(sw, bx, by, bz)];
}

static float sample_slot_l2(const ShadowWorld *sw, int slot, const uint8_t *field, float lx,
                            float ly, float lz, float missing)
{
  if (!sw || !field)
    return missing;

  const float inv = 1.0f / (float)SHADOW_DENSITY_BLOCK;
  const float fx = lx * inv - 0.5f;
  const float fy = ly * inv - 0.5f;
  const float fz = lz * inv - 0.5f;

  int x0 = (int)floorf(fx);
  int y0 = (int)floorf(fy);
  int z0 = (int)floorf(fz);
  const float tx = fx - (float)x0;
  const float ty = fy - (float)y0;
  const float tz = fz - (float)z0;

  float acc = 0.0f;
  float wsum = 0.0f;
  for (int dz = 0; dz <= 1; dz++)
    for (int dy = 0; dy <= 1; dy++)
      for (int dx = 0; dx <= 1; dx++)
      {
        const int bx = x0 + dx;
        const int by = y0 + dy;
        const int bz = z0 + dz;
        if (bx < 0 || by < 0 || bz < 0 || bx >= sw->density_dim_x || by >= sw->density_dim_y ||
            bz >= sw->density_dim_z)
          continue;
        const size_t idx = slot_density_index(sw, bx, by, bz);
        const float wx = (dx == 0) ? (1.0f - tx) : tx;
        const float wy = (dy == 0) ? (1.0f - ty) : ty;
        const float wz = (dz == 0) ? (1.0f - tz) : tz;
        const float w = wx * wy * wz;
        acc += w * ((float)field[idx] * (1.0f / 255.0f));
        wsum += w;
      }
  if (wsum < 1e-6f)
    return missing;
  return acc / wsum;
  (void)slot;
}

float shadow_world_density_sample(const ShadowWorld *sw, float x, float y, float z)
{
  if (!sw || !sw->pyramid_enabled)
    return 0.5f;
  int lx = 0, ly = 0, lz = 0;
  const int slot = shadow_world_route(sw, (int)floorf(x), (int)floorf(y), (int)floorf(z), &lx, &ly,
                                      &lz);
  if (slot < 0 || !sw->slot_density[slot])
    return 0.5f;
  // Use fractional cluster coords remapped into the slot's local space.
  int ox = 0, oy = 0, oz = 0;
  shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);
  return sample_slot_l2(sw, slot, sw->slot_density[slot], x - (float)ox, y - (float)oy,
                        z - (float)oz, 0.5f);
}

float shadow_world_emissive_sample(const ShadowWorld *sw, float x, float y, float z)
{
  if (!sw || !sw->pyramid_enabled)
    return 0.0f;
  int lx = 0, ly = 0, lz = 0;
  const int slot = shadow_world_route(sw, (int)floorf(x), (int)floorf(y), (int)floorf(z), &lx, &ly,
                                      &lz);
  if (slot < 0 || !sw->slot_emissive[slot])
    return 0.0f;
  int ox = 0, oy = 0, oz = 0;
  shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);
  return sample_slot_l2(sw, slot, sw->slot_emissive[slot], x - (float)ox, y - (float)oy,
                        z - (float)oz, 0.0f);
}
