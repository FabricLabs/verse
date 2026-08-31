#include "fog_volume.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Diffusion coefficient in 8-bit fixed point for a 7-point Laplacian. Stability wants the
// coefficient under 1/6; a tenth leaves headroom and still fills a body-sized cavity in under a
// second at FOG_VOLUME_HZ.
#define FOG_DIFFUSE_Q8 25

// A wake loses this fraction of its amplitude per step (with a floor of one unit), so a carved
// tunnel closes rather than ringing forever. Tuned so a stride-sized wake is gone in a few seconds.
#define FOG_DENSITY_LOSS 256

// Furthest a cell may be pushed from ambient, in FOG_VOLUME_UNIT. Past a couple of "full" cells the
// face bake blows out to black or white and stops reading as fog.
#define FOG_MAX_UNITS 3

// How hard a body carves at its centre, in FOG_VOLUME_UNIT. Softened by (1 - dist/radius).
#define FOG_WAKE_CARVE (FOG_VOLUME_UNIT * 2)

// Extra carve per (voxel/second) of body speed, so a sprint clears more than a stroll.
#define FOG_WAKE_SPEED_CARVE (FOG_VOLUME_UNIT / 4)

// How much of the carved mass is piled ahead into a bow wave. The rest dissipates — fog is not
// conserved the way water is; a cleared tunnel slowly filling from ambient is the look we want.
#define FOG_WAKE_BOW_FRACTION 4

// Sub-voxel radius of the bow-wave blob ahead of the body.
#define FOG_WAKE_BOW_RIM 6

#define FOG_VOLUME_EMPTY (-1)
#define FOG_VOLUME_TOMBSTONE (-2)

// Below this peak amplitude a patch counts as calm and is retired. A sixteenth of a unit is below
// what the face bake can show as alpha variation.
#define FOG_VOLUME_FLAT (FOG_VOLUME_UNIT / 16)

// Ambient face alpha for undisturbed steam. Matches the look of a flat steam cube well enough that
// a calm patch retiring is invisible; disturbed cells swing around this.
#define FOG_BASE_ALPHA 48

typedef struct
{
  const World *world;
  uint16_t x, y, z;
  bool used;
  uint64_t last_touch;
  int peak;
  FogVolumePatch patch;
} FogVolumeSlot;

struct FogVolume
{
  FogVolumeSlot *slots;
  int capacity;
  int live;

  int *table;
  int table_size;
  int table_used;

  int parity;
  float accum;
  uint64_t clock;
};

// ---------------------------------------------------------------------------
// Indexing

static inline int fog_at(int i, int j, int k)
{
  return (k * FOG_VOLUME_DIM + j) * FOG_VOLUME_DIM + i;
}

static inline bool fog_is_steam(VoxelType t)
{
  return t == VOXEL_STEAM || t == VOXEL_GAS;
}

// ---------------------------------------------------------------------------
// Key lookup (mirrors fluid_surface.c)

static inline uint32_t fog_volume_hash(const World *world, int x, int y, int z)
{
  uint64_t h = (uint64_t)(uintptr_t)world;
  h ^= (uint64_t)x * 0x9E3779B97F4A7C15ULL;
  h ^= (uint64_t)y * 0xC2B2AE3D27D4EB4FULL;
  h ^= (uint64_t)z * 0x165667B19E3779F9ULL;
  h ^= h >> 29;
  h *= 0xBF58476D1CE4E5B9ULL;
  h ^= h >> 32;
  return (uint32_t)h;
}

static inline bool fog_volume_slot_matches(const FogVolumeSlot *slot, const World *world, int x,
                                           int y, int z)
{
  return slot->used && slot->world == world && slot->x == (uint16_t)x && slot->y == (uint16_t)y &&
         slot->z == (uint16_t)z;
}

static int fog_volume_find(const FogVolume *s, const World *world, int x, int y, int z)
{
  const uint32_t mask = (uint32_t)s->table_size - 1u;
  uint32_t i = fog_volume_hash(world, x, y, z) & mask;
  for (int probe = 0; probe < s->table_size; probe++)
  {
    const int entry = s->table[i];
    if (entry == FOG_VOLUME_EMPTY)
      return -1;
    if (entry >= 0 && fog_volume_slot_matches(&s->slots[entry], world, x, y, z))
      return entry;
    i = (i + 1u) & mask;
  }
  return -1;
}

static void fog_volume_table_insert(FogVolume *s, int slot_index)
{
  const FogVolumeSlot *slot = &s->slots[slot_index];
  const uint32_t mask = (uint32_t)s->table_size - 1u;
  uint32_t i = fog_volume_hash(slot->world, slot->x, slot->y, slot->z) & mask;
  for (;;)
  {
    if (s->table[i] < 0)
    {
      if (s->table[i] == FOG_VOLUME_EMPTY)
        s->table_used++;
      s->table[i] = slot_index;
      return;
    }
    i = (i + 1u) & mask;
  }
}

static void fog_volume_table_rebuild(FogVolume *s)
{
  memset(s->table, 0xff, (size_t)s->table_size * sizeof(int)); // EMPTY = -1
  s->table_used = 0;
  for (int i = 0; i < s->capacity; i++)
    if (s->slots[i].used)
      fog_volume_table_insert(s, i);
}

static void fog_volume_retire_slot(FogVolume *s, int index)
{
  FogVolumeSlot *slot = &s->slots[index];
  if (!slot->used)
    return;

  const World *world = slot->world;
  const int x = (int)slot->x, y = (int)slot->y, z = (int)slot->z;

  slot->used = false;
  slot->world = NULL;
  slot->peak = 0;
  s->live--;

  const uint32_t mask = (uint32_t)s->table_size - 1u;
  uint32_t i = fog_volume_hash(world, x, y, z) & mask;
  for (int probe = 0; probe < s->table_size; probe++)
  {
    if (s->table[i] == index)
    {
      s->table[i] = FOG_VOLUME_TOMBSTONE;
      return;
    }
    if (s->table[i] == FOG_VOLUME_EMPTY)
      return;
    i = (i + 1u) & mask;
  }
}

static int fog_volume_claim(FogVolume *s)
{
  // Prefer a free slot; otherwise the least-recently-touched live one.
  int free_i = -1;
  int oldest_i = -1;
  uint64_t oldest_touch = UINT64_MAX;
  for (int i = 0; i < s->capacity; i++)
  {
    if (!s->slots[i].used)
    {
      free_i = i;
      break;
    }
    if (s->slots[i].last_touch < oldest_touch)
    {
      oldest_touch = s->slots[i].last_touch;
      oldest_i = i;
    }
  }
  if (free_i >= 0)
    return free_i;
  if (oldest_i >= 0)
  {
    fog_volume_retire_slot(s, oldest_i);
    return oldest_i;
  }
  return -1;
}

static int fog_volume_slot_for(FogVolume *s, const World *world, int x, int y, int z, bool create)
{
  int index = fog_volume_find(s, world, x, y, z);
  if (index >= 0)
  {
    s->slots[index].last_touch = s->clock;
    return index;
  }
  if (!create)
    return -1;

  index = fog_volume_claim(s);
  if (index < 0)
    return -1;

  FogVolumeSlot *slot = &s->slots[index];
  memset(&slot->patch, 0, sizeof(slot->patch));
  slot->world = world;
  slot->x = (uint16_t)x;
  slot->y = (uint16_t)y;
  slot->z = (uint16_t)z;
  slot->used = true;
  slot->last_touch = s->clock;
  slot->peak = 0;
  s->live++;

  if (s->table_used * 4 >= s->table_size * 3)
    fog_volume_table_rebuild(s);
  else
    fog_volume_table_insert(s, index);
  return index;
}

// ---------------------------------------------------------------------------
// Lifecycle

FogVolume *fog_volume_create(int max_patches)
{
  if (max_patches <= 0)
    return NULL;

  FogVolume *s = calloc(1, sizeof(FogVolume));
  if (!s)
    return NULL;

  s->capacity = max_patches;
  s->slots = calloc((size_t)max_patches, sizeof(FogVolumeSlot));
  // Power-of-two table at least 2x capacity for open addressing.
  int table_size = 1;
  while (table_size < max_patches * 2)
    table_size <<= 1;
  s->table_size = table_size;
  s->table = malloc((size_t)table_size * sizeof(int));
  if (!s->slots || !s->table)
  {
    free(s->slots);
    free(s->table);
    free(s);
    return NULL;
  }
  memset(s->table, 0xff, (size_t)table_size * sizeof(int));
  return s;
}

void fog_volume_destroy(FogVolume *volume)
{
  if (!volume)
    return;
  free(volume->slots);
  free(volume->table);
  free(volume);
}

int fog_volume_live_count(const FogVolume *volume)
{
  return volume ? volume->live : 0;
}

const int16_t *fog_volume_densities(FogVolume *volume, const World *world, int x, int y, int z)
{
  if (!volume || !world)
    return NULL;
  const int index = fog_volume_find(volume, world, x, y, z);
  if (index < 0)
    return NULL;
  return volume->slots[index].patch.density[volume->parity];
}

// ---------------------------------------------------------------------------
// Density writes

static inline int fog_volume_add(int16_t *cur, int i, int j, int k, int delta)
{
  if (i < 0 || j < 0 || k < 0 || i >= FOG_VOLUME_DIM || j >= FOG_VOLUME_DIM ||
      k >= FOG_VOLUME_DIM)
    return 0;
  const int limit = FOG_MAX_UNITS * FOG_VOLUME_UNIT;
  const int at = fog_at(i, j, k);
  const int before = (int)cur[at];
  int v = before + delta;
  if (v > limit)
    v = limit;
  if (v < -limit)
    v = -limit;
  cur[at] = (int16_t)v;
  return v - before;
}

static inline int fog_decay(int value, int divisor)
{
  if (value == 0)
    return 0;
  const int magnitude = value > 0 ? value : -value;
  const int loss = magnitude / divisor + 1;
  if (loss >= magnitude)
    return 0;
  return value > 0 ? value - loss : value + loss;
}

// ---------------------------------------------------------------------------
// Body wake

void fog_volume_body_wake(FogVolume *volume, const World *world, float x, float y, float z,
                          float radius, float vx, float vy, float vz)
{
  if (!volume || !world || radius < 0.05f)
    return;

  volume->clock++;

  const float speed = sqrtf(vx * vx + vy * vy + vz * vz);
  float dir_x = 0.0f, dir_y = 0.0f, dir_z = 0.0f;
  if (speed > 1e-3f)
  {
    dir_x = vx / speed;
    dir_y = vy / speed;
    dir_z = vz / speed;
  }

  const float r = radius;
  const int x0 = (int)floorf(x - r) - 1;
  const int y0 = (int)floorf(y - r) - 1;
  const int z0 = (int)floorf(z - r) - 1;
  const int x1 = (int)ceilf(x + r) + 1;
  const int y1 = (int)ceilf(y + r) + 1;
  const int z1 = (int)ceilf(z + r) + 1;

  const int carve_amp =
      FOG_WAKE_CARVE + (int)(speed * (float)FOG_WAKE_SPEED_CARVE + 0.5f);
  const int limit = FOG_MAX_UNITS * FOG_VOLUME_UNIT;
  int amp = carve_amp;
  if (amp > limit)
    amp = limit;

  for (int vz_i = z0; vz_i <= z1; vz_i++)
  {
    for (int vy_i = y0; vy_i <= y1; vy_i++)
    {
      for (int vx_i = x0; vx_i <= x1; vx_i++)
      {
        if (!world_pos_in_bounds_fast(world, vx_i, vy_i, vz_i))
          continue;
        const Voxel *v = world_voxel_cptr_fast(world, vx_i, vy_i, vz_i);
        if (!v || !fog_is_steam(v->type))
          continue;

        // Cheap reject: body sphere vs voxel AABB.
        const float cx = (float)vx_i + 0.5f;
        const float cy = (float)vy_i + 0.5f;
        const float cz = (float)vz_i + 0.5f;
        const float dx = fabsf(x - cx) - 0.5f;
        const float dy = fabsf(y - cy) - 0.5f;
        const float dz = fabsf(z - cz) - 0.5f;
        const float ox = dx > 0.0f ? dx : 0.0f;
        const float oy = dy > 0.0f ? dy : 0.0f;
        const float oz = dz > 0.0f ? dz : 0.0f;
        if (ox * ox + oy * oy + oz * oz > r * r)
          continue;

        const int index = fog_volume_slot_for(volume, world, vx_i, vy_i, vz_i, true);
        if (index < 0)
          continue;

        int16_t *cur = volume->slots[index].patch.density[volume->parity];
        int carved = 0;
        int peak = volume->slots[index].peak;

        // Sample a coarse grid of sub-voxels rather than every cell: a body radius of ~1 voxel
        // covers ~30k cells, and carving every one every frame is more than the wake needs. Stride
        // 2 still leaves a readable cavity at face resolution.
        const int stride = 2;
        for (int k = 0; k < FOG_VOLUME_DIM; k += stride)
        {
          for (int j = 0; j < FOG_VOLUME_DIM; j += stride)
          {
            for (int i = 0; i < FOG_VOLUME_DIM; i += stride)
            {
              const float sx = (float)vx_i + ((float)i + 0.5f) / (float)FOG_VOLUME_DIM;
              const float sy = (float)vy_i + ((float)j + 0.5f) / (float)FOG_VOLUME_DIM;
              const float sz = (float)vz_i + ((float)k + 0.5f) / (float)FOG_VOLUME_DIM;
              const float ddx = sx - x;
              const float ddy = sy - y;
              const float ddz = sz - z;
              const float dist_sq = ddx * ddx + ddy * ddy + ddz * ddz;
              if (dist_sq >= r * r)
                continue;
              const float dist = sqrtf(dist_sq);
              const float w = 1.0f - dist / r;
              const int depth = (int)((float)amp * w * w);
              if (depth <= 0)
                continue;

              // Carve a small block around the sample so stride-2 does not leave a dotted tunnel.
              for (int kk = 0; kk < stride && k + kk < FOG_VOLUME_DIM; kk++)
                for (int jj = 0; jj < stride && j + jj < FOG_VOLUME_DIM; jj++)
                  for (int ii = 0; ii < stride && i + ii < FOG_VOLUME_DIM; ii++)
                    carved -= fog_volume_add(cur, i + ii, j + jj, k + kk, -depth);

              if (depth > peak)
                peak = depth;
            }
          }
        }

        // Bow wave: pile a fraction of what was carved a little ahead of the body, inside this
        // voxel when the offset lands here. Cross-voxel bow is handled by the body overlapping
        // neighbours on the next iteration of the outer loops.
        if (carved > 0 && speed > 1e-3f)
        {
          const float bow_x = x + dir_x * (r * 0.85f);
          const float bow_y = y + dir_y * (r * 0.85f);
          const float bow_z = z + dir_z * (r * 0.85f);
          const int bi = (int)floorf((bow_x - (float)vx_i) * (float)FOG_VOLUME_DIM);
          const int bj = (int)floorf((bow_y - (float)vy_i) * (float)FOG_VOLUME_DIM);
          const int bk = (int)floorf((bow_z - (float)vz_i) * (float)FOG_VOLUME_DIM);
          if (bi >= -FOG_WAKE_BOW_RIM && bj >= -FOG_WAKE_BOW_RIM && bk >= -FOG_WAKE_BOW_RIM &&
              bi < FOG_VOLUME_DIM + FOG_WAKE_BOW_RIM && bj < FOG_VOLUME_DIM + FOG_WAKE_BOW_RIM &&
              bk < FOG_VOLUME_DIM + FOG_WAKE_BOW_RIM)
          {
            int pile = carved / FOG_WAKE_BOW_FRACTION;
            const int rim2 = FOG_WAKE_BOW_RIM * FOG_WAKE_BOW_RIM;
            int cells = 0;
            for (int dk = -FOG_WAKE_BOW_RIM; dk <= FOG_WAKE_BOW_RIM; dk++)
              for (int dj = -FOG_WAKE_BOW_RIM; dj <= FOG_WAKE_BOW_RIM; dj++)
                for (int di = -FOG_WAKE_BOW_RIM; di <= FOG_WAKE_BOW_RIM; di++)
                {
                  if (di * di + dj * dj + dk * dk > rim2)
                    continue;
                  const int ii = bi + di, jj = bj + dj, kk = bk + dk;
                  if (ii >= 0 && jj >= 0 && kk >= 0 && ii < FOG_VOLUME_DIM &&
                      jj < FOG_VOLUME_DIM && kk < FOG_VOLUME_DIM)
                    cells++;
                }
            int left = cells;
            for (int dk = -FOG_WAKE_BOW_RIM; dk <= FOG_WAKE_BOW_RIM && left > 0; dk++)
              for (int dj = -FOG_WAKE_BOW_RIM; dj <= FOG_WAKE_BOW_RIM && left > 0; dj++)
                for (int di = -FOG_WAKE_BOW_RIM; di <= FOG_WAKE_BOW_RIM && left > 0; di++)
                {
                  if (di * di + dj * dj + dk * dk > rim2)
                    continue;
                  const int ii = bi + di, jj = bj + dj, kk = bk + dk;
                  if (ii < 0 || jj < 0 || kk < 0 || ii >= FOG_VOLUME_DIM ||
                      jj >= FOG_VOLUME_DIM || kk >= FOG_VOLUME_DIM)
                    continue;
                  const int share = pile / left;
                  const int applied = fog_volume_add(cur, ii, jj, kk, share);
                  pile -= applied;
                  if (applied > peak)
                    peak = applied;
                  left--;
                }
          }
        }

        volume->slots[index].peak = peak;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Diffusion step

static inline int fog_neighbour_cell(const int16_t *side, int i, int j, int k, int fallback)
{
  return side ? (int)side[fog_at(i, j, k)] : fallback;
}

void fog_volume_step(FogVolume *volume, float dt_seconds)
{
  if (!volume || volume->live == 0)
    return;

  volume->accum += dt_seconds > 0.0f ? dt_seconds : 0.0f;
  const float period = 1.0f / (float)FOG_VOLUME_HZ;
  int steps = (int)(volume->accum / period);
  if (steps <= 0)
    return;
  if (steps > 2)
    steps = 2;
  volume->accum -= (float)steps * period;

  // Drop patches whose voxel is no longer steam.
  for (int i = 0; i < volume->capacity; i++)
  {
    FogVolumeSlot *slot = &volume->slots[i];
    if (!slot->used)
      continue;
    const Voxel *v = world_pos_in_bounds_fast(slot->world, slot->x, slot->y, slot->z)
                         ? world_voxel_cptr_fast(slot->world, slot->x, slot->y, slot->z)
                         : NULL;
    if (!v || !fog_is_steam(v->type))
      fog_volume_retire_slot(volume, i);
  }

  const int limit = FOG_MAX_UNITS * FOG_VOLUME_UNIT;

  for (int step = 0; step < steps; step++)
  {
    const int cur_buf = volume->parity;
    const int next_buf = 1 - cur_buf;

    // Spawn neighbour patches ahead of a spreading wake, so a tunnel can continue into the next
    // steam voxel instead of reflecting off a fake wall at the patch boundary.
    static const int face_offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                                           {0, 1, 0},  {0, 0, -1}, {0, 0, 1}};
    for (int si = 0; si < volume->capacity && volume->live < volume->capacity; si++)
    {
      const FogVolumeSlot *slot = &volume->slots[si];
      if (!slot->used || slot->peak < FOG_VOLUME_FLAT)
        continue;
      const int16_t *d = slot->patch.density[cur_buf];
      for (int f = 0; f < 6; f++)
      {
        int reach = 0;
        // Sample the face of this patch that borders neighbour f.
        for (int a = 0; a < FOG_VOLUME_DIM; a++)
          for (int b = 0; b < FOG_VOLUME_DIM; b++)
          {
            int i, j, k;
            if (f == 0)
            {
              i = 0;
              j = a;
              k = b;
            }
            else if (f == 1)
            {
              i = FOG_VOLUME_DIM - 1;
              j = a;
              k = b;
            }
            else if (f == 2)
            {
              i = a;
              j = 0;
              k = b;
            }
            else if (f == 3)
            {
              i = a;
              j = FOG_VOLUME_DIM - 1;
              k = b;
            }
            else if (f == 4)
            {
              i = a;
              j = b;
              k = 0;
            }
            else
            {
              i = a;
              j = b;
              k = FOG_VOLUME_DIM - 1;
            }
            const int v = d[fog_at(i, j, k)];
            const int mag = v < 0 ? -v : v;
            if (mag > reach)
              reach = mag;
          }
        if (reach < FOG_VOLUME_FLAT)
          continue;
        const int nx = (int)slot->x + face_offsets[f][0];
        const int ny = (int)slot->y + face_offsets[f][1];
        const int nz = (int)slot->z + face_offsets[f][2];
        if (!world_pos_in_bounds_fast(slot->world, nx, ny, nz))
          continue;
        if (!fog_is_steam(world_voxel_cptr_fast(slot->world, nx, ny, nz)->type))
          continue;
        if (volume->live >= volume->capacity)
          break;
        fog_volume_slot_for(volume, slot->world, nx, ny, nz, true);
      }
    }

    for (int si = 0; si < volume->capacity; si++)
    {
      FogVolumeSlot *slot = &volume->slots[si];
      if (!slot->used)
        continue;

      const int16_t *side[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
      for (int f = 0; f < 6; f++)
      {
        const int n = fog_volume_find(volume, slot->world, slot->x + face_offsets[f][0],
                                      slot->y + face_offsets[f][1],
                                      slot->z + face_offsets[f][2]);
        if (n >= 0)
          side[f] = volume->slots[n].patch.density[cur_buf];
      }

      const int16_t *cur = slot->patch.density[cur_buf];
      int16_t *next = slot->patch.density[next_buf];
      int peak = 0;

      for (int k = 0; k < FOG_VOLUME_DIM; k++)
      {
        for (int j = 0; j < FOG_VOLUME_DIM; j++)
        {
          for (int i = 0; i < FOG_VOLUME_DIM; i++)
          {
            const int at = fog_at(i, j, k);
            const int c = (int)cur[at];

            const int xm = i > 0 ? (int)cur[fog_at(i - 1, j, k)]
                                 : fog_neighbour_cell(side[0], FOG_VOLUME_DIM - 1, j, k, c);
            const int xp = i < FOG_VOLUME_DIM - 1
                               ? (int)cur[fog_at(i + 1, j, k)]
                               : fog_neighbour_cell(side[1], 0, j, k, c);
            const int ym = j > 0 ? (int)cur[fog_at(i, j - 1, k)]
                                 : fog_neighbour_cell(side[2], i, FOG_VOLUME_DIM - 1, k, c);
            const int yp = j < FOG_VOLUME_DIM - 1
                               ? (int)cur[fog_at(i, j + 1, k)]
                               : fog_neighbour_cell(side[3], i, 0, k, c);
            const int zm = k > 0 ? (int)cur[fog_at(i, j, k - 1)]
                                 : fog_neighbour_cell(side[4], i, j, FOG_VOLUME_DIM - 1, c);
            const int zp = k < FOG_VOLUME_DIM - 1
                               ? (int)cur[fog_at(i, j, k + 1)]
                               : fog_neighbour_cell(side[5], i, j, 0, c);

            const int laplacian = xm + xp + ym + yp + zm + zp - 6 * c;
            int value = c + (laplacian * FOG_DIFFUSE_Q8) / 256;
            value = fog_decay(value, FOG_DENSITY_LOSS);

            if (value > limit)
              value = limit;
            else if (value < -limit)
              value = -limit;
            next[at] = (int16_t)value;
            const int mag = value < 0 ? -value : value;
            if (mag > peak)
              peak = mag;
          }
        }
      }

      slot->peak = peak;
    }
    volume->parity = next_buf;
  }

  if (steps > 0)
  {
    for (int si = 0; si < volume->capacity; si++)
      if (volume->slots[si].used && volume->slots[si].peak < FOG_VOLUME_FLAT)
        fog_volume_retire_slot(volume, si);
  }
}

// ---------------------------------------------------------------------------
// Bake

bool fog_volume_bake_face(FogVolume *volume, const World *world, int x, int y, int z, FogFace face,
                          uint8_t base_r, uint8_t base_g, uint8_t base_b, uint32_t *out_texels)
{
  if (!volume || !world || !out_texels)
    return false;
  if (!world_pos_in_bounds_fast(world, x, y, z))
    return false;

  const Voxel *voxel = world_voxel_cptr_fast(world, x, y, z);
  if (!voxel || !fog_is_steam(voxel->type))
    return false;

  const int index = fog_volume_slot_for(volume, world, x, y, z, false);
  if (index < 0)
    return false;

  const int16_t *d = volume->slots[index].patch.density[volume->parity];

  for (int v = 0; v < FOG_VOLUME_DIM; v++)
  {
    for (int u = 0; u < FOG_VOLUME_DIM; u++)
    {
      // Integrate a few cells into the volume from this face so a deep cavity reads thinner than
      // a surface dent. Depth of 8 sub-voxels is enough to see a body-sized tunnel.
      int sum = 0;
      const int samples = 8;
      for (int s = 0; s < samples; s++)
      {
        int i, j, k;
        if (face == FOG_FACE_TOP)
        {
          i = u;
          j = v;
          k = FOG_VOLUME_DIM - 1 - s;
        }
        else if (face == FOG_FACE_LEFT)
        {
          i = u;
          j = FOG_VOLUME_DIM - 1 - s;
          k = FOG_VOLUME_DIM - 1 - v;
        }
        else // FOG_FACE_RIGHT
        {
          i = FOG_VOLUME_DIM - 1 - s;
          j = u;
          k = FOG_VOLUME_DIM - 1 - v;
        }
        sum += (int)d[fog_at(i, j, k)];
      }
      const int mean = sum / samples;

      // Negative density (cleared) thins the face; positive (piled) thickens it.
      int alpha = FOG_BASE_ALPHA + (mean * 80) / FOG_VOLUME_UNIT;
      if (alpha < 8)
        alpha = 8;
      if (alpha > 180)
        alpha = 180;

      // Mild shading from the local gradient so a wake edge catches the light.
      int i0, j0, k0;
      if (face == FOG_FACE_TOP)
      {
        i0 = u;
        j0 = v;
        k0 = FOG_VOLUME_DIM - 1;
      }
      else if (face == FOG_FACE_LEFT)
      {
        i0 = u;
        j0 = FOG_VOLUME_DIM - 1;
        k0 = FOG_VOLUME_DIM - 1 - v;
      }
      else
      {
        i0 = FOG_VOLUME_DIM - 1;
        j0 = u;
        k0 = FOG_VOLUME_DIM - 1 - v;
      }
      const int centre = (int)d[fog_at(i0, j0, k0)];
      const int iu = i0 > 0 ? (int)d[fog_at(i0 - 1, j0, k0)] : centre;
      const int ju = j0 > 0 ? (int)d[fog_at(i0, j0 - 1, k0)] : centre;
      const int slope = (iu - centre) + (ju - centre);
      int shade = 256 + (slope * 96) / (FOG_VOLUME_UNIT / 8 + 1);
      if (shade < 160)
        shade = 160;
      if (shade > 320)
        shade = 320;

      int r = (base_r * shade) / 256;
      int g = (base_g * shade) / 256;
      int b = (base_b * shade) / 256;
      if (r > 255)
        r = 255;
      if (g > 255)
        g = 255;
      if (b > 255)
        b = 255;
      if (r < 0)
        r = 0;
      if (g < 0)
        g = 0;
      if (b < 0)
        b = 0;

      out_texels[v * FOG_VOLUME_DIM + u] =
          ((uint32_t)alpha << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
  }
  return true;
}
