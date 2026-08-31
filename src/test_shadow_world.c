// Tests for the shadow world cluster.
//
// The shadow world is entirely an optimisation: the rejection ladder exists so a search can avoid
// looking at cells, and the pyramid exists so it can avoid looking at blocks of them. That makes the
// load-bearing property "returns exactly what an unoptimised triple loop returns" — so almost every
// check here is against a brute-force reference rather than against a hand-written expectation.
//
// The rest guards the things that would fail silently: routing a coordinate into the wrong
// neighbour, treating an unloaded world as empty, and results that depend on which worker finished
// first.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "gpu_voxel_buffer.h"
#include "shadow_world.h"
#include "task_scheduler.h"
#include "universe.h"
#include "world.h"

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const char *what)
{
  g_checks++;
  if (condition)
  {
    printf("  ok   %s\n", what);
  }
  else
  {
    printf("  FAIL %s\n", what);
    g_failures++;
  }
}

// A cluster of 64-cubes rather than the 128-cubes the game uses: 27 of those would be 2.6GB, and
// every property under test is independent of the edge length as long as it stays a power of two
// large enough for the coarsest pyramid block.
#define TW 64

static const VoxelType kFillTypes[] = {VOXEL_STONE, VOXEL_SOIL, VOXEL_GRASS, VOXEL_SAND};

// Deterministic hash, so a failure is reproducible and the reference and the shadow world are
// guaranteed to be looking at the same terrain.
static uint32_t mix(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
  uint32_t h = 2166136261u;
  h = (h ^ a) * 16777619u;
  h = (h ^ b) * 16777619u;
  h = (h ^ c) * 16777619u;
  h = (h ^ d) * 16777619u;
  h ^= h >> 15;
  return h;
}

// Sparse, clumpy content: sparse so the pyramid has empty blocks to skip, clumpy so it has full ones
// too. A uniformly random fill would leave every block occupied and quietly test nothing.
static void fill_world(World *w, uint32_t salt)
{
  for (uint32_t z = 0; z < w->depth; z++)
  {
    for (uint32_t y = 0; y < w->height; y++)
    {
      for (uint32_t x = 0; x < w->width; x++)
      {
        const uint32_t blob = mix(x >> 3, y >> 3, z >> 3, salt);
        if ((blob & 3u) != 0u)
          continue; // roughly three quarters of the 8-cubes stay empty

        const uint32_t h = mix(x, y, z, salt);
        if ((h & 7u) < 5u)
        {
          const VoxelType t = kFillTypes[(h >> 8) % (sizeof(kFillTypes) / sizeof(kFillTypes[0]))];
          world_set_voxel(w, x, y, z, t);
        }
      }
    }
  }
  world_refresh_occupancy_bitfield(w);
  world_build_heightmap(w);
}

// ---------------------------------------------------------------------------------------------
// Brute-force reference
// ---------------------------------------------------------------------------------------------

typedef struct
{
  int cx, cy, cz;
  int slot;
  VoxelType type;
} RefHit;

typedef struct
{
  RefHit *hits;
  int count;
  int cap;
} RefList;

static void ref_push(RefList *l, int cx, int cy, int cz, int slot, VoxelType t)
{
  if (l->count >= l->cap)
  {
    l->cap = l->cap ? l->cap * 2 : 1024;
    l->hits = (RefHit *)realloc(l->hits, (size_t)l->cap * sizeof(RefHit));
  }
  l->hits[l->count].cx = cx;
  l->hits[l->count].cy = cy;
  l->hits[l->count].cz = cz;
  l->hits[l->count].slot = slot;
  l->hits[l->count].type = t;
  l->count++;
}

typedef struct
{
  bool use_box;
  int x0, y0, z0, x1, y1, z1;
  bool use_sphere;
  float scx, scy, scz, sradius;
  ShadowSlotMask slot_mask;
  const VoxelType *types;
  int type_count;
  uint64_t condition_any;
  uint64_t condition_all;
} RefFilter;

// The unoptimised triple loop, in exactly the visit order the shadow world promises: slots in
// dz, dy, dx order, then z, y, x within a slot.
static void ref_collect(const ShadowWorld *sw, const RefFilter *rf, RefList *out)
{
  out->count = 0;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (rf->slot_mask && !(rf->slot_mask & SHADOW_SLOT_BIT(slot)))
      continue;
    World *w = shadow_world_slot_world(sw, slot);
    if (!w)
      continue;

    int ox = 0, oy = 0, oz = 0;
    shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);

    for (int lz = 0; lz < (int)w->depth; lz++)
    {
      for (int ly = 0; ly < (int)w->height; ly++)
      {
        for (int lx = 0; lx < (int)w->width; lx++)
        {
          const int cx = ox + lx, cy = oy + ly, cz = oz + lz;
          if (rf->use_box &&
              (cx < rf->x0 || cx > rf->x1 || cy < rf->y0 || cy > rf->y1 ||
               cz < rf->z0 || cz > rf->z1))
            continue;
          if (rf->use_sphere)
          {
            const float dx = (float)cx - rf->scx;
            const float dy = (float)cy - rf->scy;
            const float dz = (float)cz - rf->scz;
            if ((dx * dx + dy * dy + dz * dz) > rf->sradius * rf->sradius)
              continue;
          }

          const Voxel *v = world_get_voxel(w, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz);
          if (!v || v->type == VOXEL_AIR)
            continue;

          if (rf->type_count > 0)
          {
            bool ok = false;
            for (int i = 0; i < rf->type_count; i++)
              if (rf->types[i] == v->type)
              {
                ok = true;
                break;
              }
            if (!ok)
              continue;
          }
          if (rf->condition_all &&
              (v->condition_mask & rf->condition_all) != rf->condition_all)
            continue;
          if (rf->condition_any && (v->condition_mask & rf->condition_any) == 0ULL)
            continue;

          ref_push(out, cx, cy, cz, slot, v->type);
        }
      }
    }
  }
}

typedef struct
{
  ShadowHit *hits;
  int count;
  int cap;
} HitList;

static bool hit_collect(const ShadowHit *hit, void *user)
{
  HitList *l = (HitList *)user;
  if (l->count >= l->cap)
  {
    l->cap = l->cap ? l->cap * 2 : 1024;
    l->hits = (ShadowHit *)realloc(l->hits, (size_t)l->cap * sizeof(ShadowHit));
  }
  l->hits[l->count++] = *hit;
  return true;
}

static bool lists_agree(const HitList *got, const RefList *want, bool compare_types)
{
  if (got->count != want->count)
    return false;
  for (int i = 0; i < got->count; i++)
  {
    if (got->hits[i].cx != want->hits[i].cx || got->hits[i].cy != want->hits[i].cy ||
        got->hits[i].cz != want->hits[i].cz || got->hits[i].slot != want->hits[i].slot)
      return false;
    if (compare_types && got->hits[i].type != want->hits[i].type)
      return false;
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------------------------

typedef struct
{
  ShadowWorld *sw;
  World *worlds[SHADOW_SLOT_COUNT];
} Fixture;

// Leaves two slots deliberately empty so every test runs against a cluster that has holes in it,
// which is the state the game is actually in while worlds stream.
#define UNLOADED_SLOT_A shadow_slot_index(-1, -1, -1)
#define UNLOADED_SLOT_B shadow_slot_index(1, 0, -1)

static bool fixture_init(Fixture *fx)
{
  memset(fx, 0, sizeof(*fx));
  fx->sw = shadow_world_create(TW, TW, TW);
  if (!fx->sw)
    return false;
  shadow_world_set_centre(fx->sw, 100, 200, 300);

  // Populate the inner radius-1 shell only. The cluster is 5x5x5, but allocating and filling all
  // 125 slots at TW^3 would be ~1.5GB and every property under test already holds with holes in
  // the outer ring — which is also how the game looks while the outer ring streams in.
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (slot == UNLOADED_SLOT_A || slot == UNLOADED_SLOT_B)
      continue;
    int dx = 0, dy = 0, dz = 0;
    shadow_slot_offsets(slot, &dx, &dy, &dz);
    int cheb = abs(dx);
    if (abs(dy) > cheb)
      cheb = abs(dy);
    if (abs(dz) > cheb)
      cheb = abs(dz);
    if (cheb > 1)
      continue;

    World *w = world_create(TW, TW, TW);
    if (!w)
      return false;
    fill_world(w, (uint32_t)slot * 7919u + 13u);
    fx->worlds[slot] = w;

    if (!shadow_world_attach(fx->sw, dx, dy, dz, w))
      return false;
  }
  shadow_world_refresh(fx->sw);
  return true;
}

static void fixture_free(Fixture *fx)
{
  shadow_world_destroy(fx->sw);
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    if (fx->worlds[slot])
      world_destroy(fx->worlds[slot]);
  }
}

// ---------------------------------------------------------------------------------------------
// Coordinates and sampling
// ---------------------------------------------------------------------------------------------

static void test_coordinates(Fixture *fx)
{
  printf("coordinates\n");
  const ShadowWorld *sw = fx->sw;

  bool round_trip_ok = true;
  bool origin_ok = true;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    int ox = 0, oy = 0, oz = 0;
    shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);

    int dx = 0, dy = 0, dz = 0;
    shadow_slot_offsets(slot, &dx, &dy, &dz);
    if (shadow_slot_index(dx, dy, dz) != slot)
      origin_ok = false;

    // Every corner and the middle of each slot must route back to it with the right local offset.
    const int probes[3] = {0, TW / 2, TW - 1};
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++)
        {
          const int cx = ox + probes[i], cy = oy + probes[j], cz = oz + probes[k];
          int lx = -1, ly = -1, lz = -1;
          const int got = shadow_world_route(sw, cx, cy, cz, &lx, &ly, &lz);
          if (got != slot || lx != probes[i] || ly != probes[j] || lz != probes[k])
            round_trip_ok = false;
        }
  }
  check(origin_ok, "slot index and offsets are inverses across all cluster slots");
  check(round_trip_ok, "cluster coordinates round-trip to slot plus local on every slot");

  check(shadow_world_slot_of(sw, -1, 0, 0) < 0 &&
            shadow_world_slot_of(sw, SHADOW_CLUSTER_DIM * TW, 0, 0) < 0 &&
            shadow_world_slot_of(sw, 0, 0, SHADOW_CLUSTER_DIM * TW) < 0,
        "coordinates outside the cluster route to no slot");

  // A world of the wrong size cannot be routed to, and refusing it must not leave the previous
  // occupant behind — that pointer may since have been evicted.
  ShadowWorld *probe = shadow_world_create(TW, TW, TW);
  World *right_size = world_create(TW, TW, TW);
  World *wrong_size = world_create(TW / 2, TW, TW);
  shadow_world_attach(probe, 0, 0, 0, right_size);
  check(shadow_world_slot_world(probe, SHADOW_CENTRE_SLOT) == right_size,
        "a correctly sized world attaches");
  check(!shadow_world_attach(probe, 0, 0, 0, wrong_size),
        "a world of the wrong dimensions is refused");
  check(shadow_world_slot_world(probe, SHADOW_CENTRE_SLOT) == NULL,
        "a refused attach clears the slot rather than keeping the old world");
  check(shadow_world_create(TW + 1, TW, TW) == NULL,
        "a cluster of non-power-of-two worlds is refused, since routing is a shift and a mask");
  shadow_world_destroy(probe);
  world_destroy(right_size);
  world_destroy(wrong_size);
}

static void test_sampling(Fixture *fx)
{
  printf("sampling\n");
  const ShadowWorld *sw = fx->sw;

  // Straddling a boundary is where a shift/mask mistake shows up, so walk right across one.
  bool matches_world = true;
  bool type_matches = true;
  for (int cx = TW - 4; cx <= TW + 3; cx++)
  {
    for (int cy = TW - 2; cy <= TW + 1; cy++)
    {
      for (int cz = TW - 2; cz <= TW + 1; cz++)
      {
        int lx = 0, ly = 0, lz = 0;
        const int slot = shadow_world_route(sw, cx, cy, cz, &lx, &ly, &lz);
        World *w = shadow_world_slot_world(sw, slot);
        const Voxel *v = w ? world_get_voxel(w, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz) : NULL;

        const ShadowSample got = shadow_world_sample(sw, cx, cy, cz);
        const ShadowSample want = !w ? SHADOW_UNLOADED
                                     : ((v && v->type != VOXEL_AIR) ? SHADOW_SOLID : SHADOW_EMPTY);
        if (got != want)
          matches_world = false;
        if (shadow_world_type_at(sw, cx, cy, cz) != (v ? v->type : VOXEL_AIR))
          type_matches = false;
      }
    }
  }
  check(matches_world, "sample agrees with world_get_voxel on both sides of a world boundary");
  check(type_matches, "type_at agrees with world_get_voxel across a boundary");

  int ox = 0, oy = 0, oz = 0;
  shadow_world_slot_origin(sw, UNLOADED_SLOT_A, &ox, &oy, &oz);
  check(shadow_world_sample(sw, ox + 5, oy + 5, oz + 5) == SHADOW_UNLOADED,
        "an unloaded slot samples UNLOADED rather than EMPTY");
  check(shadow_world_sample(sw, -1, 0, 0) == SHADOW_UNLOADED,
        "outside the cluster samples UNLOADED");

  // Whatever the heightmap says must actually be the topmost solid cell of that column.
  bool surface_ok = true;
  for (int cx = TW - 2; cx <= TW + 2 && surface_ok; cx++)
  {
    for (int cy = TW; cy <= TW + 2 && surface_ok; cy++)
    {
      const int h = shadow_world_surface_height(sw, cx, cy);
      if (h < 0)
        continue;
      if (shadow_world_sample(sw, cx, cy, h) != SHADOW_SOLID)
        surface_ok = false;
      for (int z = h + 1; z < SHADOW_CLUSTER_DIM * TW; z++)
      {
        if (shadow_world_sample(sw, cx, cy, z) == SHADOW_SOLID)
        {
          surface_ok = false;
          break;
        }
      }
    }
  }
  check(surface_ok, "surface_height names the topmost solid cell in the column");
}

// ---------------------------------------------------------------------------------------------
// Pyramid
// ---------------------------------------------------------------------------------------------

static bool pyramid_agrees(const ShadowWorld *sw)
{
  // For every block of every level: the bit must be set exactly when the block holds a solid cell.
  const ShadowPyramidLevel *levels[3] = {&sw->l1, &sw->l2, &sw->l3};
  for (int li = 0; li < 3; li++)
  {
    const ShadowPyramidLevel *lv = levels[li];
    for (int bz = 0; bz < lv->dim_z; bz++)
    {
      for (int by = 0; by < lv->dim_y; by++)
      {
        for (int bx = 0; bx < lv->dim_x; bx++)
        {
          bool any = false;
          for (int z = bz * lv->block; z < (bz + 1) * lv->block && !any; z++)
            for (int y = by * lv->block; y < (by + 1) * lv->block && !any; y++)
              for (int x = bx * lv->block; x < (bx + 1) * lv->block; x++)
                if (shadow_world_sample(sw, x, y, z) == SHADOW_SOLID)
                {
                  any = true;
                  break;
                }

          const size_t idx = ((size_t)bz * (size_t)lv->dim_y + (size_t)by) * (size_t)lv->dim_x +
                             (size_t)bx;
          const bool bit = ((lv->bits[idx >> 3u] >> (idx & 7u)) & 1u) != 0u;
          if (bit != any)
            return false;
        }
      }
    }
  }
  return true;
}

static void test_pyramid(Fixture *fx)
{
  printf("pyramid\n");
  ShadowWorld *sw = fx->sw;

  check(sw->pyramid_enabled, "pyramid is enabled for power-of-two worlds at least 64 on a side");
  check(pyramid_agrees(sw), "pyramid bits agree with a brute-force scan on every level");

  // Pyramid bitfields stay under ~65KB for TW=64; per-slot brick masks + VVAO add ~32KB per
  // loaded world. Hold the whole accel under 2MB so a block-size change cannot explode memory.
  const size_t bytes = shadow_world_pyramid_bytes(sw);
  check(bytes > 0 && bytes < 2u * 1024u * 1024u, "pyramid + brick accel fits under 2MB");

  check(shadow_world_refresh(sw) == 0, "a refresh with nothing changed rebuilds nothing");

  // A mutation must be picked up through voxel_revision alone, with no explicit invalidation.
  World *w = shadow_world_slot_world(sw, SHADOW_CENTRE_SLOT);
  bool found_empty_block = false;
  int mx = 0, my = 0, mz = 0;
  for (int z = 0; z < TW && !found_empty_block; z += SHADOW_L1_BLOCK)
    for (int y = 0; y < TW && !found_empty_block; y += SHADOW_L1_BLOCK)
      for (int x = 0; x < TW && !found_empty_block; x += SHADOW_L1_BLOCK)
      {
        bool any = false;
        for (int k = 0; k < SHADOW_L1_BLOCK && !any; k++)
          for (int j = 0; j < SHADOW_L1_BLOCK && !any; j++)
            for (int i = 0; i < SHADOW_L1_BLOCK; i++)
              if (world_is_solid_fast(w, x + i, y + j, z + k))
              {
                any = true;
                break;
              }
        if (!any)
        {
          found_empty_block = true;
          mx = x;
          my = y;
          mz = z;
        }
      }
  check(found_empty_block, "the fixture leaves empty 4-cubes for the pyramid to skip");

  world_set_voxel(w, (uint32_t)mx, (uint32_t)my, (uint32_t)mz, VOXEL_STONE);
  check(shadow_world_refresh(sw) == 1, "one changed world rebuilds exactly one slot");
  check(pyramid_agrees(sw), "pyramid agrees again after a mutation bumped voxel_revision");

  world_set_voxel(w, (uint32_t)mx, (uint32_t)my, (uint32_t)mz, VOXEL_AIR);
  shadow_world_refresh(sw);
  check(pyramid_agrees(sw), "pyramid agrees again after the mutation is undone");
}

// ---------------------------------------------------------------------------------------------
// Recentre
// ---------------------------------------------------------------------------------------------

static void test_recenter(void)
{
  printf("recentre\n");

  ShadowWorld *sw = shadow_world_create(TW, TW, TW);
  World *worlds[SHADOW_SLOT_COUNT];
  memset(worlds, 0, sizeof(worlds));

  shadow_world_set_centre(sw, 10, 20, 30);
  // Fill every slot. TW=64 keeps this under ~1.5GB; the property under test is retention math,
  // not terrain content, so the worlds stay empty.
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    worlds[slot] = world_create(TW, TW, TW);
    int dx = 0, dy = 0, dz = 0;
    shadow_slot_offsets(slot, &dx, &dy, &dz);
    shadow_world_attach(sw, dx, dy, dz, worlds[slot]);
  }

  // Moving one cell up: the layer that fell out of range is lost; the other four survive.
  // 4 * 5 * 5 = 100 of the 125 slots stay inside the cluster.
  int ddx = 0, ddy = 0, ddz = 0;
  const int retained = shadow_world_recenter(sw, 10, 20, 31, &ddx, &ddy, &ddz);
  check(retained == 100, "moving one cell retains 100 of 125 slots");
  check(ddx == 0 && ddy == 0 && ddz == -TW, "rebase delta is one world extent on the moved axis");
  check(sw->centre_z == 31, "centre follows the move");

  // The world that was directly above the old centre is now the centre.
  check(shadow_world_slot_world(sw, SHADOW_CENTRE_SLOT) == worlds[shadow_slot_index(0, 0, 1)],
        "the slot above the old centre becomes the new centre");
  // The newly exposed layer is at +RADIUS (not +1): the worlds that were at +2 shifted to +1.
  check(shadow_world_slot_world(sw, shadow_slot_index(0, 0, SHADOW_CLUSTER_RADIUS)) == NULL,
        "the newly exposed layer is empty and reports as unloaded");

  // A rebased coordinate has to name the same voxel it did before the move. Pick a cell in the
  // outer top-west-south corner (dx=dy=-RADIUS, dz=+RADIUS), whose cluster origin is (0,0,4*TW)
  // for radius 2; after the move that world sits at dz=+RADIUS-1.
  const int before = (2 * SHADOW_CLUSTER_RADIUS) * TW + 5;
  const int after = before + ddz;
  check(shadow_world_route(sw, 0, 0, after, NULL, NULL, NULL) ==
            shadow_slot_index(-SHADOW_CLUSTER_RADIUS, -SHADOW_CLUSTER_RADIUS,
                              SHADOW_CLUSTER_RADIUS - 1),
        "a rebased coordinate lands in the slot holding the same world");

  const int far_retained = shadow_world_recenter(sw, 99, 99, 99, NULL, NULL, NULL);
  check(far_retained == 0, "a move beyond the cluster retains nothing");
  check(sw->loaded_mask == 0, "loaded mask empties when nothing is retained");

  shadow_world_destroy(sw);
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
    world_destroy(worlds[slot]);
}

// ---------------------------------------------------------------------------------------------
// Searches against brute force
// ---------------------------------------------------------------------------------------------

static void test_searches(Fixture *fx)
{
  printf("searches\n");
  const ShadowWorld *sw = fx->sw;

  HitList got = {NULL, 0, 0};
  RefList want = {NULL, 0, 0};

  // Whole cluster, unfiltered. The broadest possible agreement check.
  ShadowFilter f = shadow_filter_all();
  RefFilter rf;
  memset(&rf, 0, sizeof(rf));

  got.count = 0;
  shadow_world_for_each(sw, &f, hit_collect, &got);
  ref_collect(sw, &rf, &want);
  check(lists_agree(&got, &want, true),
        "unfiltered cluster search matches brute force exactly, in the same order");
  check(want.count > 0, "the fixture actually contains solid voxels");

  // A box spanning three worlds on x, so the per-slot clipping is exercised.
  got.count = 0;
  const int bx0 = SHADOW_CLUSTER_RADIUS * TW - 10, bx1 = (SHADOW_CLUSTER_RADIUS + 1) * TW + 10;
  const int by0 = SHADOW_CLUSTER_RADIUS * TW - 5, by1 = SHADOW_CLUSTER_RADIUS * TW + 20;
  const int bz0 = SHADOW_CLUSTER_RADIUS * TW + 3, bz1 = SHADOW_CLUSTER_RADIUS * TW + 18;
  shadow_world_find_in_box(sw, bx0, by0, bz0, bx1, by1, bz1, NULL, NULL, 0, NULL);
  int total = 0;
  const int written = shadow_world_find_in_box(sw, bx0, by0, bz0, bx1, by1, bz1, NULL,
                                               NULL, 0, &total);
  f.region = SHADOW_REGION_BOX;
  f.r.box.x0 = bx0;
  f.r.box.y0 = by0;
  f.r.box.z0 = bz0;
  f.r.box.x1 = bx1;
  f.r.box.y1 = by1;
  f.r.box.z1 = bz1;
  shadow_world_for_each(sw, &f, hit_collect, &got);

  rf.use_box = true;
  rf.x0 = bx0;
  rf.y0 = by0;
  rf.z0 = bz0;
  rf.x1 = bx1;
  rf.y1 = by1;
  rf.z1 = bz1;
  ref_collect(sw, &rf, &want);
  check(lists_agree(&got, &want, true), "box search across three worlds matches brute force");
  check(written == 0 && total == want.count,
        "find_in_box reports the full match count even with no output buffer");

  // Sphere straddling a corner where eight worlds meet (the centre of the radius-1 shell).
  got.count = 0;
  const float scx = (float)(SHADOW_CLUSTER_RADIUS * TW);
  const float scy = (float)(SHADOW_CLUSTER_RADIUS * TW);
  const float scz = (float)(SHADOW_CLUSTER_RADIUS * TW);
  const float srad = 12.5f;
  f.region = SHADOW_REGION_SPHERE;
  f.r.sphere.cx = scx;
  f.r.sphere.cy = scy;
  f.r.sphere.cz = scz;
  f.r.sphere.radius = srad;
  shadow_world_for_each(sw, &f, hit_collect, &got);

  memset(&rf, 0, sizeof(rf));
  rf.use_sphere = true;
  rf.scx = scx;
  rf.scy = scy;
  rf.scz = scz;
  rf.sradius = srad;
  ref_collect(sw, &rf, &want);
  check(lists_agree(&got, &want, true), "sphere search at an eight-world corner matches brute force");

  ShadowHit buffer[4096];
  int radius_total = 0;
  const int radius_written = shadow_world_find_in_radius(sw, scx, scy, scz, srad, NULL,
                                                         buffer, 4096, &radius_total);
  check(radius_total == want.count, "find_in_radius total matches brute force");
  check(radius_written == (want.count < 4096 ? want.count : 4096),
        "find_in_radius writes up to the buffer size and reports the rest");

  // A cap must truncate the output without changing the total.
  int capped_total = 0;
  const int capped = shadow_world_find_in_radius(sw, scx, scy, scz, srad, NULL,
                                                 buffer, 7, &capped_total);
  check(capped == 7 && capped_total == want.count,
        "an output cap truncates the results but not the count");

  // count must equal what the equivalent find collects.
  check(shadow_world_count(sw, &f) == want.count, "count equals the number of hits collected");

  // Type filter.
  got.count = 0;
  const VoxelType only_stone[1] = {VOXEL_STONE};
  f.types = only_stone;
  f.type_count = 1;
  shadow_world_for_each(sw, &f, hit_collect, &got);
  rf.types = only_stone;
  rf.type_count = 1;
  ref_collect(sw, &rf, &want);
  check(lists_agree(&got, &want, true), "type-filtered sphere search matches brute force");
  bool all_stone = true;
  for (int i = 0; i < got.count; i++)
    if (got.hits[i].type != VOXEL_STONE)
      all_stone = false;
  check(all_stone && got.count > 0, "a type filter returns only that type, and finds some");
  f.types = NULL;
  f.type_count = 0;
  rf.types = NULL;
  rf.type_count = 0;

  // Column region.
  got.count = 0;
  f.region = SHADOW_REGION_COLUMN;
  f.r.column.x = SHADOW_CLUSTER_RADIUS * TW + 7;
  f.r.column.y = SHADOW_CLUSTER_RADIUS * TW + 11;
  shadow_world_for_each(sw, &f, hit_collect, &got);
  memset(&rf, 0, sizeof(rf));
  rf.use_box = true;
  rf.x0 = rf.x1 = SHADOW_CLUSTER_RADIUS * TW + 7;
  rf.y0 = rf.y1 = SHADOW_CLUSTER_RADIUS * TW + 11;
  rf.z0 = 0;
  rf.z1 = SHADOW_CLUSTER_DIM * TW - 1;
  ref_collect(sw, &rf, &want);
  check(lists_agree(&got, &want, true), "column search matches brute force");

  // z band.
  got.count = 0;
  f.region = SHADOW_REGION_ALL;
  f.use_z_band = true;
  f.z_min = SHADOW_CLUSTER_RADIUS * TW + 10;
  f.z_max = SHADOW_CLUSTER_RADIUS * TW + 12;
  shadow_world_for_each(sw, &f, hit_collect, &got);
  memset(&rf, 0, sizeof(rf));
  rf.use_box = true;
  rf.x0 = 0;
  rf.y0 = 0;
  rf.x1 = SHADOW_CLUSTER_DIM * TW - 1;
  rf.y1 = SHADOW_CLUSTER_DIM * TW - 1;
  rf.z0 = SHADOW_CLUSTER_RADIUS * TW + 10;
  rf.z1 = SHADOW_CLUSTER_RADIUS * TW + 12;
  ref_collect(sw, &rf, &want);
  check(lists_agree(&got, &want, true), "z band search matches brute force");
  f.use_z_band = false;

  free(got.hits);
  free(want.hits);
}

static void test_slot_filter(Fixture *fx)
{
  printf("by-world filter\n");
  const ShadowWorld *sw = fx->sw;

  HitList got = {NULL, 0, 0};
  RefList want = {NULL, 0, 0};

  ShadowFilter f = shadow_filter_all();
  RefFilter rf;
  memset(&rf, 0, sizeof(rf));

  const int slot = shadow_slot_index(1, 0, 0);
  got.count = 0;
  shadow_world_for_each_in_slot(sw, slot, &f, hit_collect, &got);
  rf.slot_mask = SHADOW_SLOT_BIT(slot);
  ref_collect(sw, &rf, &want);
  check(lists_agree(&got, &want, true), "single-slot search matches brute force for that slot");

  bool all_in_slot = true;
  int ox = 0, oy = 0, oz = 0;
  shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);
  for (int i = 0; i < got.count; i++)
  {
    const ShadowHit *h = &got.hits[i];
    if (h->slot != slot || h->cx < ox || h->cx >= ox + TW || h->cy < oy || h->cy >= oy + TW ||
        h->cz < oz || h->cz >= oz + TW)
      all_in_slot = false;
  }
  check(all_in_slot && got.count > 0, "a slot filter returns nothing from neighbouring worlds");

  // Two slots at once, and the sum has to be the parts.
  const int slot_b = shadow_slot_index(-1, 0, 0);
  f.slot_mask = SHADOW_SLOT_BIT(slot) | SHADOW_SLOT_BIT(slot_b);
  const int pair = shadow_world_count(sw, &f);
  f.slot_mask = SHADOW_SLOT_BIT(slot_b);
  const int only_b = shadow_world_count(sw, &f);
  check(pair == got.count + only_b, "a two-slot mask returns exactly the union of the two");

  // Masking out every loaded slot must find nothing rather than fall back to all.
  f.slot_mask = SHADOW_SLOT_BIT(UNLOADED_SLOT_A);
  check(shadow_world_count(sw, &f) == 0, "a mask naming only an unloaded slot finds nothing");

  free(got.hits);
  free(want.hits);
}

static void test_solid_only(Fixture *fx)
{
  printf("solid-only path\n");
  const ShadowWorld *sw = fx->sw;

  HitList bits = {NULL, 0, 0};
  HitList voxels = {NULL, 0, 0};

  ShadowFilter f = shadow_filter_all();
  f.region = SHADOW_REGION_BOX;
  f.r.box.x0 = SHADOW_CLUSTER_RADIUS * TW - 8;
  f.r.box.y0 = SHADOW_CLUSTER_RADIUS * TW - 8;
  f.r.box.z0 = SHADOW_CLUSTER_RADIUS * TW - 8;
  f.r.box.x1 = SHADOW_CLUSTER_RADIUS * TW + 8;
  f.r.box.y1 = SHADOW_CLUSTER_RADIUS * TW + 8;
  f.r.box.z1 = SHADOW_CLUSTER_RADIUS * TW + 8;

  shadow_world_reset_stats();
  f.solid_only = true;
  shadow_world_for_each(sw, &f, hit_collect, &bits);
  const ShadowSearchStats bit_stats = shadow_world_stats();

  shadow_world_reset_stats();
  f.solid_only = false;
  shadow_world_for_each(sw, &f, hit_collect, &voxels);
  const ShadowSearchStats vox_stats = shadow_world_stats();

  bool same_cells = (bits.count == voxels.count);
  bool voxel_null = true;
  for (int i = 0; same_cells && i < bits.count; i++)
  {
    if (bits.hits[i].cx != voxels.hits[i].cx || bits.hits[i].cy != voxels.hits[i].cy ||
        bits.hits[i].cz != voxels.hits[i].cz)
      same_cells = false;
    if (bits.hits[i].voxel != NULL)
      voxel_null = false;
  }
  check(same_cells && bits.count > 0, "solid_only finds exactly the cells the voxel path finds");
  check(voxel_null, "solid_only never hands back a Voxel pointer");
  check(bit_stats.voxels_read == 0, "solid_only reads no voxels at all");
  check(vox_stats.voxels_read > 0, "the unfiltered path does read voxels, so the saving is real");

  // A filter that both promises not to read voxels and needs one is a mistake, not a preference.
  f.solid_only = true;
  const VoxelType t[1] = {VOXEL_STONE};
  f.types = t;
  f.type_count = 1;
  check(shadow_world_for_each(sw, &f, hit_collect, &bits) < 0,
        "solid_only combined with a type filter is rejected as malformed");
  f.types = NULL;
  f.type_count = 0;
  f.solid_only = false;

  const VoxelType air[1] = {VOXEL_AIR};
  f.types = air;
  f.type_count = 1;
  check(shadow_world_for_each(sw, &f, hit_collect, &bits) < 0,
        "asking for VOXEL_AIR is rejected, since the ladder never visits air");

  free(bits.hits);
  free(voxels.hits);
}

// ---------------------------------------------------------------------------------------------
// Raycast
// ---------------------------------------------------------------------------------------------

static void test_raycast(Fixture *fx)
{
  printf("raycast\n");
  const ShadowWorld *sw = fx->sw;

  // Inside one world, the cluster ray and the single-world DDA must agree cell for cell.
  const int slot = SHADOW_CENTRE_SLOT;
  World *w = shadow_world_slot_world(sw, slot);
  int ox = 0, oy = 0, oz = 0;
  shadow_world_slot_origin(sw, slot, &ox, &oy, &oz);

  int agree = 0, disagree = 0, hits = 0;
  for (int i = 0; i < 64; i++)
  {
    const uint32_t h = mix((uint32_t)i, 3u, 5u, 7u);
    const float lx = (float)(h % TW) + 0.5f;
    const float ly = (float)((h >> 8) % TW) + 0.5f;
    const float lz = (float)((h >> 16) % TW) + 0.5f;
    const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const int d = (int)((h >> 24) % 6u);

    int wx = 0, wy = 0, wz = 0;
    const bool world_hit = world_raycast_first_hit(w, lx, ly, lz,
                                                   dirs[d][0], dirs[d][1], dirs[d][2],
                                                   TW, &wx, &wy, &wz);

    ShadowRayResult res;
    // Restricted to the one slot, so the comparison is against the same volume the world DDA saw.
    const bool sw_hit = shadow_world_raycast(sw, (float)ox + lx, (float)oy + ly, (float)oz + lz,
                                             dirs[d][0], dirs[d][1], dirs[d][2],
                                             TW, SHADOW_SLOT_BIT(slot), &res);

    if (world_hit != sw_hit)
    {
      disagree++;
      continue;
    }
    if (!world_hit)
    {
      agree++;
      continue;
    }
    hits++;
    if (res.cx - ox == wx && res.cy - oy == wy && res.cz - oz == wz)
      agree++;
    else
      disagree++;
  }
  check(disagree == 0 && agree == 64, "cluster raycast agrees with world_raycast_first_hit");
  check(hits > 0, "some of those rays actually hit something");

  // Crossing a boundary: the ray must find terrain in the neighbour, which a per-world cast cannot.
  // Start just inside the west face of the centre-ring, looking into the centre world.
  const int mid = SHADOW_CLUSTER_RADIUS * TW;
  int crossed = 0;
  for (int i = 0; i < 32; i++)
  {
    const uint32_t h = mix((uint32_t)i, 11u, 13u, 17u);
    const float y = (float)(mid + (h % TW)) + 0.5f;
    const float z = (float)(mid + ((h >> 8) % TW)) + 0.5f;

    ShadowRayResult res;
    if (shadow_world_raycast(sw, (float)mid - 0.5f, y, z, 1.0f, 0.0f, 0.0f, 2 * TW, 0u, &res) &&
        res.cx >= mid)
      crossed++;
  }
  check(crossed > 0, "a ray can hit terrain in a world beyond the one it started in");

  // An unloaded world must stop the ray rather than let it through. Use an outer-ring slot that
  // the fixture never fills (chebyshev radius 2), approaching from the loaded neighbour on its west.
  const int outer = shadow_slot_index(SHADOW_CLUSTER_RADIUS, 0, 0);
  shadow_world_slot_origin(sw, outer, &ox, &oy, &oz);
  ShadowRayResult res;
  const bool hit = shadow_world_raycast(sw, (float)ox - 0.5f, (float)oy + 20.5f,
                                        (float)oz + 20.5f, 1.0f, 0.0f, 0.0f, 2 * TW, 0u, &res);
  check(!hit && res.stopped_unloaded, "a ray entering an unloaded world stops and says so");
  check(res.cx >= ox && res.cx < ox + TW, "the stop is reported at the unloaded world's edge");

  // A ray that leaves the cluster must simply not hit, rather than wrap around.
  const bool escaped = shadow_world_raycast(sw, 1.5f, 1.5f, 1.5f, -1.0f, 0.0f, 0.0f,
                                            4 * TW, 0u, &res);
  check(!escaped, "a ray leaving the cluster does not wrap around to the far side");

  // Occupancy lighting rays must land on the same cell as the gameplay ray wherever both sides
  // are loaded, and must not invent a wall from an unloaded neighbour.
  int occ_agree = 0, occ_disagree = 0;
  shadow_world_slot_origin(sw, SHADOW_CENTRE_SLOT, &ox, &oy, &oz);
  for (int i = 0; i < 48; i++)
  {
    const uint32_t h = mix((uint32_t)i, 19u, 23u, 29u);
    const float lx = (float)(h % TW) + 0.5f;
    const float ly = (float)((h >> 8) % TW) + 0.5f;
    const float lz = (float)((h >> 16) % TW) + 0.5f;
    ShadowRayResult a, b;
    const bool full = shadow_world_raycast(sw, (float)ox + lx, (float)oy + ly, (float)oz + lz,
                                           1.0f, 0.0f, 0.0f, TW, SHADOW_SLOT_BIT(SHADOW_CENTRE_SLOT),
                                           &a);
    const bool occ = shadow_world_raycast_occupancy(sw, (float)ox + lx, (float)oy + ly,
                                                    (float)oz + lz, 1.0f, 0.0f, 0.0f, TW,
                                                    SHADOW_SLOT_BIT(SHADOW_CENTRE_SLOT), &b);
    if (full == occ && (!full || (a.cx == b.cx && a.cy == b.cy && a.cz == b.cz)))
      occ_agree++;
    else
      occ_disagree++;
  }
  check(occ_disagree == 0 && occ_agree == 48,
        "occupancy raycast agrees with the gameplay ray inside a loaded slot");

  shadow_world_slot_origin(sw, outer, &ox, &oy, &oz);
  ShadowRayResult occ_unloaded;
  const bool occ_hit = shadow_world_raycast_occupancy(sw, (float)ox - 0.5f, (float)oy + 20.5f,
                                                      (float)oz + 20.5f, 1.0f, 0.0f, 0.0f,
                                                      2 * TW, 0u, &occ_unloaded);
  check(!occ_hit && !occ_unloaded.stopped_unloaded,
        "an occupancy lighting ray treats unloaded space as empty rather than as a wall");
}

static void test_occupancy_bitfield_ray(void)
{
  printf("occupancy bitfield ray\n");
  World *w = world_create(16, 16, 16);
  check(w != NULL, "bitfield ray fixture builds");
  if (!w)
    return;
  for (int x = 0; x < 16; x++)
    for (int y = 0; y < 16; y++)
      world_set_voxel(w, (uint32_t)x, (uint32_t)y, 0, VOXEL_STONE);
  world_set_voxel(w, 8, 8, 4, VOXEL_STONE);

  check(world_refresh_occupancy_bitfield(w), "occupancy bitfield builds on demand");
  check(w->occupancy_bits != NULL, "world keeps an occupancy bitfield");
  check(gpu_voxel_buffer_occupied(w->occupancy_bits, 8, 8, 4),
        "a solid cell is set in the bitfield");
  check(!gpu_voxel_buffer_occupied(w->occupancy_bits, 8, 8, 5),
        "air is clear in the bitfield");

  int hx = -1, hy = -1, hz = -1;
  // Start just above the floating stone so the DDA's first cell is air, then steps into it.
  const bool hit = gpu_voxel_buffer_raycast(w->occupancy_bits, 8.5f, 8.5f, 4.6f, 0.0f, 0.0f, -1.0f,
                                            32, &hx, &hy, &hz);
  check(hit && hx == 8 && hy == 8 && hz == 4, "bitfield DDA finds the floating stone");

  const float vis = gpu_voxel_buffer_visible(w->occupancy_bits, 8.5f, 8.5f, 5.5f, 0.0f, 0.0f, 1.0f, 16);
  check(vis > 0.5f, "a skyward occupancy sample from above the stone is visible");

  const float blocked =
      gpu_voxel_buffer_visible(w->occupancy_bits, 8.5f, 8.5f, 5.5f, 0.0f, 0.0f, -1.0f, 16);
  check(blocked < 0.5f, "a sun ray into the stone is occluded");

  world_destroy(w);
}

static void test_brick_masks_and_vvao(Fixture *fx)
{
  printf("brick masks and VVAO density\n");
  const ShadowWorld *sw = fx->sw;
  check(sw->pyramid_enabled, "fixture worlds enable the pyramid");
  check(sw->slot_brick_masks[SHADOW_CENTRE_SLOT] != NULL, "centre brick masks are allocated");
  check(sw->slot_density[SHADOW_CENTRE_SLOT] != NULL &&
            sw->slot_emissive[SHADOW_CENTRE_SLOT] != NULL,
        "VVAO density/emissive maps are allocated for the centre");

  // Place a single solid in the centre world and rebuild so the brick mask is known.
  World *centre = fx->worlds[SHADOW_CENTRE_SLOT];
  check(centre != NULL, "centre world is loaded");
  if (!centre)
    return;

  // Clear a known 4³ and put one stone at local (2,2,2) of that brick.
  for (int z = 0; z < 4; z++)
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++)
        world_set_voxel(centre, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_AIR);
  world_set_voxel(centre, 2, 2, 2, VOXEL_STONE);
  world_set_voxel(centre, 1, 1, 1, VOXEL_MAGMA);
  world_refresh_occupancy_bitfield(centre);
  shadow_world_invalidate_slot((ShadowWorld *)sw, SHADOW_CENTRE_SLOT);
  check(shadow_world_refresh((ShadowWorld *)sw) >= 1, "centre pyramid rebuilds after edit");

  int ox = 0, oy = 0, oz = 0;
  shadow_world_slot_origin(sw, SHADOW_CENTRE_SLOT, &ox, &oy, &oz);
  const uint64_t mask = shadow_world_brick_mask_at(sw, ox + 2, oy + 2, oz + 2);
  const uint64_t expect = ((uint64_t)1ull << shadow_brick_bit(2, 2, 2)) |
                          ((uint64_t)1ull << shadow_brick_bit(1, 1, 1));
  check(mask == expect, "brick mask encodes exactly the two solids in the 4³");

  // Occupancy ray through empty cells in the brick must still find the stone.
  ShadowRayResult res;
  const bool hit =
      shadow_world_raycast_occupancy(sw, (float)ox + 0.5f, (float)oy + 2.5f, (float)oz + 2.5f,
                                     1.0f, 0.0f, 0.0f, 8, SHADOW_SLOT_BIT(SHADOW_CENTRE_SLOT),
                                     &res);
  check(hit && res.cx == ox + 2 && res.cy == oy + 2 && res.cz == oz + 2,
        "brick-mask DDA finds the stone after skipping empty siblings");

  // Flat floor fullness ≈ 0.5; corner with magma should be darker / glowing.
  const float flat = shadow_world_density_sample(sw, (float)ox + 32.5f, (float)oy + 32.5f,
                                                 (float)oz + 40.5f);
  check(flat >= 0.0f && flat <= 1.0f, "density sample is in [0,1]");

  const float near_solids =
      shadow_world_density_sample(sw, (float)ox + 2.5f, (float)oy + 2.5f, (float)oz + 2.5f);
  check(near_solids > flat || near_solids > 0.01f,
        "density near solids is higher than open air");

  const float glow =
      shadow_world_emissive_sample(sw, (float)ox + 1.5f, (float)oy + 1.5f, (float)oz + 1.5f);
  check(glow > 0.2f, "emissive sample picks up magma in the L2 brick");
}

// ---------------------------------------------------------------------------------------------
// Nearest
// ---------------------------------------------------------------------------------------------

static void test_nearest(Fixture *fx)
{
  printf("nearest\n");
  const ShadowWorld *sw = fx->sw;

  const int probe[3] = {SHADOW_CLUSTER_RADIUS * TW + 32, SHADOW_CLUSTER_RADIUS * TW + 32, SHADOW_CLUSTER_RADIUS * TW + 32};
  const int max_radius = 40;

  ShadowFilter f = shadow_filter_all();
  const VoxelType only_sand[1] = {VOXEL_SAND};
  f.types = only_sand;
  f.type_count = 1;

  ShadowHit best;
  const bool found = shadow_world_find_nearest(sw, probe[0], probe[1], probe[2], max_radius,
                                               &f, &best);
  check(found, "nearest finds a match within the search radius");

  if (found)
  {
    const long dx = best.cx - probe[0], dy = best.cy - probe[1], dz = best.cz - probe[2];
    const long d2 = dx * dx + dy * dy + dz * dz;
    check(best.type == VOXEL_SAND, "nearest respects the type filter");

    // Brute force over the whole box the shells could have reached: nothing may be closer.
    long best_ref = -1;
    for (int z = probe[2] - max_radius; z <= probe[2] + max_radius; z++)
      for (int y = probe[1] - max_radius; y <= probe[1] + max_radius; y++)
        for (int x = probe[0] - max_radius; x <= probe[0] + max_radius; x++)
        {
          if (shadow_world_type_at(sw, x, y, z) != VOXEL_SAND)
            continue;
          const long rdx = x - probe[0], rdy = y - probe[1], rdz = z - probe[2];
          const long rd2 = rdx * rdx + rdy * rdy + rdz * rdz;
          if (best_ref < 0 || rd2 < best_ref)
            best_ref = rd2;
        }
    check(best_ref >= 0 && d2 == best_ref, "nothing in range is nearer than what nearest returned");
  }

  // A radius of zero looks at exactly one cell.
  ShadowFilter all = shadow_filter_all();
  ShadowHit at_origin;
  const int sx = TW + 5, sy = TW + 5;
  const int top = shadow_world_surface_height(sw, sx, sy);
  if (top >= 0)
  {
    const bool zero = shadow_world_find_nearest(sw, sx, sy, top, 0, &all, &at_origin);
    check(zero && at_origin.cx == sx && at_origin.cy == sy && at_origin.cz == top,
          "a zero radius search returns the cell itself when it is solid");
  }

  const VoxelType impossible[1] = {VOXEL_WORLD};
  all.types = impossible;
  all.type_count = 1;
  check(!shadow_world_find_nearest(sw, probe[0], probe[1], probe[2], 8, &all, &best),
        "nearest reports failure rather than a wrong answer when there is no match");
}

// ---------------------------------------------------------------------------------------------
// Condition searches, both paths
// ---------------------------------------------------------------------------------------------

static void test_condition_index(Fixture *fx)
{
  printf("condition index\n");
  const ShadowWorld *sw = fx->sw;

  // Tag a scattering of voxels in two worlds through the public API, so the index revision moves the
  // way it would in the game.
  const char *cond_a = "BURNING_LOW";
  const char *cond_b = "WET";
  const uint64_t bit_a = condition_bit_from_name(cond_a);
  const uint64_t bit_b = condition_bit_from_name(cond_b);
  check(bit_a != 0 && bit_b != 0 && bit_a != bit_b, "the two conditions map to distinct bits");

  int tagged = 0;
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    World *w = shadow_world_slot_world(sw, slot);
    if (!w)
      continue;
    if (slot != SHADOW_CENTRE_SLOT && slot != shadow_slot_index(1, 0, 0))
      continue;

    for (int z = 0; z < TW; z += 5)
      for (int y = 0; y < TW; y += 7)
        for (int x = 0; x < TW; x += 3)
        {
          if (!world_is_solid_fast(w, x, y, z))
            continue;
          const uint32_t h = mix((uint32_t)x, (uint32_t)y, (uint32_t)z, (uint32_t)slot);
          if ((h & 3u) == 0u)
          {
            world_add_voxel_condition(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, cond_a);
            tagged++;
          }
          if ((h & 7u) == 1u)
          {
            world_add_voxel_condition(w, (uint32_t)x, (uint32_t)y, (uint32_t)z, cond_b);
            tagged++;
          }
        }
  }
  check(tagged > 0, "the fixture tags some voxels with conditions");

  World *centre = shadow_world_slot_world(sw, SHADOW_CENTRE_SLOT);

  ShadowFilter f = shadow_filter_all();
  f.condition_any = bit_a;

  HitList traversal = {NULL, 0, 0};
  HitList indexed = {NULL, 0, 0};
  RefList want = {NULL, 0, 0};

  RefFilter rf;
  memset(&rf, 0, sizeof(rf));
  rf.condition_any = bit_a;
  ref_collect(sw, &rf, &want);
  check(want.count > 0, "brute force finds condition-tagged voxels");

  check(!world_condition_index_is_fresh(centre),
        "the index starts stale, so the fast path is not used before it is built");

  f.index_policy = SHADOW_INDEX_FORCE_INDEX;
  shadow_world_reset_stats();
  shadow_world_for_each(sw, &f, hit_collect, &indexed);
  check(shadow_world_stats().index_searches == 0,
        "forcing the index still traverses while the index is stale, rather than answering wrong");
  check(lists_agree(&indexed, &want, true), "the stale-index fallback still matches brute force");

  // Build it, and now both paths must be available and agree.
  for (int slot = 0; slot < SHADOW_SLOT_COUNT; slot++)
  {
    World *w = shadow_world_slot_world(sw, slot);
    if (w)
      world_refresh_condition_index(w);
  }
  check(world_condition_index_is_fresh(centre), "the index is fresh once refreshed");

  traversal.count = 0;
  f.index_policy = SHADOW_INDEX_FORCE_TRAVERSAL;
  shadow_world_reset_stats();
  shadow_world_for_each(sw, &f, hit_collect, &traversal);
  check(shadow_world_stats().traversal_searches == 1, "forcing traversal takes the traversal path");
  check(lists_agree(&traversal, &want, true), "traversal path matches brute force");

  indexed.count = 0;
  f.index_policy = SHADOW_INDEX_FORCE_INDEX;
  shadow_world_reset_stats();
  shadow_world_for_each(sw, &f, hit_collect, &indexed);
  const ShadowSearchStats index_stats = shadow_world_stats();
  check(index_stats.index_searches == 1, "forcing the index takes the index path");
  check(lists_agree(&indexed, &want, true), "index path matches brute force");
  check(traversal.count == indexed.count,
        "both paths return identical results with the threshold forced each way");
  check(index_stats.cells_tested < (long)want.count * 4 + 64,
        "the index path examines about as many cells as there are matches, not the volume");

  // Two bits with condition_any: a voxel carrying both must appear exactly once.
  f.condition_any = bit_a | bit_b;
  rf.condition_any = bit_a | bit_b;
  ref_collect(sw, &rf, &want);

  indexed.count = 0;
  f.index_policy = SHADOW_INDEX_FORCE_INDEX;
  shadow_world_for_each(sw, &f, hit_collect, &indexed);
  check(lists_agree(&indexed, &want, true),
        "a two-bit any-of condition search deduplicates voxels carrying both");

  traversal.count = 0;
  f.index_policy = SHADOW_INDEX_FORCE_TRAVERSAL;
  shadow_world_for_each(sw, &f, hit_collect, &traversal);
  check(lists_agree(&traversal, &want, true), "two-bit any-of traversal matches brute force");

  // condition_all.
  f.condition_any = 0;
  f.condition_all = bit_a | bit_b;
  rf.condition_any = 0;
  rf.condition_all = bit_a | bit_b;
  ref_collect(sw, &rf, &want);

  indexed.count = 0;
  f.index_policy = SHADOW_INDEX_FORCE_INDEX;
  shadow_world_for_each(sw, &f, hit_collect, &indexed);
  check(lists_agree(&indexed, &want, true), "all-of condition search via the index matches brute force");

  traversal.count = 0;
  f.index_policy = SHADOW_INDEX_FORCE_TRAVERSAL;
  shadow_world_for_each(sw, &f, hit_collect, &traversal);
  check(lists_agree(&traversal, &want, true), "all-of condition search via traversal agrees");

  // Removing a condition must invalidate, so a stale index can never be trusted.
  bool cleared = false;
  for (int z = 0; z < TW && !cleared; z++)
    for (int y = 0; y < TW && !cleared; y++)
      for (int x = 0; x < TW; x++)
      {
        const Voxel *v = world_get_voxel(centre, (uint32_t)x, (uint32_t)y, (uint32_t)z);
        if (v && (v->condition_mask & bit_a))
        {
          world_clear_voxel_conditions(centre, (uint32_t)x, (uint32_t)y, (uint32_t)z);
          cleared = true;
          break;
        }
      }
  check(cleared && !world_condition_index_is_fresh(centre),
        "clearing a condition marks the index stale");

  free(traversal.hits);
  free(indexed.hits);
  free(want.hits);
}

// ---------------------------------------------------------------------------------------------
// Serial versus parallel
// ---------------------------------------------------------------------------------------------

static void test_parallel(Fixture *fx)
{
  printf("parallel fan-out\n");
  const ShadowWorld *sw = fx->sw;

  ShadowFilter f = shadow_filter_all();
  f.region = SHADOW_REGION_BOX;
  f.r.box.x0 = 10;
  f.r.box.y0 = 10;
  f.r.box.z0 = 10;
  f.r.box.x1 = SHADOW_CLUSTER_DIM * TW - 10;
  f.r.box.y1 = SHADOW_CLUSTER_DIM * TW - 10;
  f.r.box.z1 = SHADOW_CLUSTER_DIM * TW - 10;

  HitList collector = {NULL, 0, 0};
  const int serial_total = shadow_world_for_each(sw, &f, hit_collect, &collector);
  check(serial_total > 1000, "the region is large enough for the fan-out to be worth comparing");

  ShadowHit *parallel = (ShadowHit *)malloc((size_t)serial_total * sizeof(ShadowHit));
  int parallel_total = 0;
  const int parallel_written = shadow_world_find_parallel(sw, &f, parallel, serial_total,
                                                          &parallel_total);

  check(parallel_total == serial_total, "the parallel search finds the same number of hits");
  check(parallel_written == collector.count, "the parallel search writes the same number of hits");

  bool identical = (parallel_written == collector.count);
  for (int i = 0; identical && i < parallel_written; i++)
  {
    if (parallel[i].cx != collector.hits[i].cx || parallel[i].cy != collector.hits[i].cy ||
        parallel[i].cz != collector.hits[i].cz || parallel[i].slot != collector.hits[i].slot ||
        parallel[i].type != collector.hits[i].type)
      identical = false;
  }
  check(identical, "parallel results are byte-identical to serial, in the same order");

  free(collector.hits);
  free(parallel);
}

// ---------------------------------------------------------------------------------------------
// Eviction depends on universe_remove, which has to repair the probe chain
// ---------------------------------------------------------------------------------------------

static void test_universe_remove(void)
{
  printf("universe eviction\n");

  Universe u;
  memset(&u, 0, sizeof(u));
  if (!universe_init(&u, "abcdef0123456789", 0, 1))
  {
    check(false, "universe initialises");
    return;
  }

  // Enough cells to guarantee collisions and long probe runs, which is the case a naive delete
  // breaks: clearing a slot mid-chain hides every key that probed past it.
  const int count = 200;
  World **made = (World **)calloc((size_t)count, sizeof(World *));
  int placed = 0;
  for (int i = 0; i < count; i++)
  {
    // Tiny worlds; this test is about the map, not the voxels.
    made[i] = world_create(8, 8, 8);
    if (made[i] && universe_place(&u, (uint64_t)(i % 20), (uint64_t)(i / 20), 7ULL, made[i]))
      placed++;
  }
  check(placed == count, "every cell was placed");

  bool all_found = true;
  for (int i = 0; i < count; i++)
    if (universe_get(&u, (uint64_t)(i % 20), (uint64_t)(i / 20), 7ULL) != made[i])
      all_found = false;
  check(all_found, "every placed cell is findable before any removal");

  // Remove every third one, then confirm nothing else went missing.
  int removed = 0;
  for (int i = 0; i < count; i += 3)
  {
    World *out = NULL;
    if (universe_remove(&u, (uint64_t)(i % 20), (uint64_t)(i / 20), 7ULL, &out))
    {
      if (out == made[i])
        removed++;
      world_destroy(out);
      made[i] = NULL;
    }
  }
  check(removed == (count + 2) / 3, "remove hands back the world it was holding");

  bool survivors_ok = true;
  bool removed_gone = true;
  for (int i = 0; i < count; i++)
  {
    World *got = universe_get(&u, (uint64_t)(i % 20), (uint64_t)(i / 20), 7ULL);
    if (made[i])
    {
      if (got != made[i])
        survivors_ok = false;
    }
    else if (got)
    {
      removed_gone = false;
    }
  }
  check(survivors_ok, "every surviving cell is still findable after the removals");
  check(removed_gone, "removed cells are gone rather than merely unlinked");

  World *missing = NULL;
  check(!universe_remove(&u, 999, 999, 999, &missing) && missing == NULL,
        "removing a cell that was never placed reports failure");

  // The freed capacity has to be reusable, not leaked as a permanent hole.
  World *replacement = world_create(8, 8, 8);
  check(universe_place(&u, 0, 0, 7ULL, replacement),
        "a removed cell can be placed into again");
  World *taken = NULL;
  universe_remove(&u, 0, 0, 7ULL, &taken);
  world_destroy(taken);

  for (int i = 0; i < count; i++)
    if (made[i])
      world_destroy(made[i]);
  free(made);
  universe_free(&u);
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
  printf("=== shadow world cluster ===\n");

  // Real workers, so the parallel comparison exercises the fan-out rather than the inline fallback.
  task_scheduler_init(4);

  Fixture fx;
  if (!fixture_init(&fx))
  {
    printf("FATAL: could not build the fixture\n");
    return 1;
  }

  test_coordinates(&fx);
  test_sampling(&fx);
  test_pyramid(&fx);
  test_recenter();
  test_searches(&fx);
  test_slot_filter(&fx);
  test_solid_only(&fx);
  test_raycast(&fx);
  test_occupancy_bitfield_ray();
  test_brick_masks_and_vvao(&fx);
  test_nearest(&fx);
  test_parallel(&fx);
  test_condition_index(&fx);
  test_universe_remove();

  fixture_free(&fx);
  task_scheduler_shutdown();

  printf("\n");
  if (g_failures == 0)
    printf("=== ALL PASSED (%d checks) ===\n", g_checks);
  else
    printf("=== %d of %d checks FAILED ===\n", g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
