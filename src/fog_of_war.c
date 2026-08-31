#include "fog_of_war.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "shadow_world.h"

#define FOG_CHUNK_SIZE 16
#define FOG_CHUNK_VOXELS (FOG_CHUNK_SIZE * FOG_CHUNK_SIZE * FOG_CHUNK_SIZE)
#define FOG_CHUNK_WORDS (FOG_CHUNK_VOXELS / 64)

#define FOG_RAY_COLS 64
#define FOG_RAY_ROWS 48
#define FOG_PLAYER_BUBBLE 3
#define FOG_BEAM_RADIUS_MAX 4
#define FOG_POSE_EPS 0.01f
#define FOG_ANGLE_EPS 0.0005f
// Map FoW only needs presence, not look-direction. Skip re-marking until the player walks a bit.
#define FOG_MAP_POSE_EPS 0.35f

typedef struct FogChunk
{
  uint64_t bits[FOG_CHUNK_WORDS];
} FogChunk;

typedef struct FogEntry
{
  const World *world;
  int32_t cx;
  int32_t cy;
  int32_t cz;
  FogChunk *chunk;
  bool used;
} FogEntry;

struct FogAtlas
{
  FogEntry *entries;
  size_t capacity;
  size_t count;
  uint64_t revision;
  float last_eye_x;
  float last_eye_y;
  float last_eye_z;
  float last_yaw;
  float last_pitch;
  bool pose_valid;
};

static uint64_t fog_mix64(uint64_t x)
{
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

static size_t fog_hash(const World *w, int32_t cx, int32_t cy, int32_t cz, size_t cap)
{
  const uintptr_t wp = (uintptr_t)w;
  uint64_t h = fog_mix64((uint64_t)wp);
  h ^= fog_mix64((uint64_t)(uint32_t)cx);
  h ^= fog_mix64((uint64_t)(uint32_t)cy);
  h ^= fog_mix64((uint64_t)(uint32_t)cz);
  return (size_t)(h % cap);
}

static bool fog_grow(FogAtlas *atlas)
{
  const size_t new_cap = atlas->capacity ? atlas->capacity * 2 : 256;
  FogEntry *next = (FogEntry *)calloc(new_cap, sizeof(FogEntry));
  if (!next)
    return false;

  for (size_t i = 0; i < atlas->capacity; i++)
  {
    if (!atlas->entries[i].used)
      continue;
    const FogEntry *e = &atlas->entries[i];
    size_t idx = fog_hash(e->world, e->cx, e->cy, e->cz, new_cap);
    for (size_t probe = 0; probe < new_cap; probe++)
    {
      const size_t p = (idx + probe) % new_cap;
      if (!next[p].used)
      {
        next[p] = *e;
        break;
      }
    }
  }

  free(atlas->entries);
  atlas->entries = next;
  atlas->capacity = new_cap;
  return true;
}

static FogChunk *fog_find_chunk(const FogAtlas *atlas, const World *w, int32_t cx, int32_t cy,
                                int32_t cz)
{
  if (!atlas || !w || !atlas->entries || atlas->capacity == 0)
    return NULL;

  size_t idx = fog_hash(w, cx, cy, cz, atlas->capacity);
  for (size_t probe = 0; probe < atlas->capacity; probe++)
  {
    const size_t p = (idx + probe) % atlas->capacity;
    const FogEntry *e = &atlas->entries[p];
    if (!e->used)
      return NULL;
    if (e->world == w && e->cx == cx && e->cy == cy && e->cz == cz)
      return e->chunk;
  }
  return NULL;
}

static FogChunk *fog_get_or_create_chunk(FogAtlas *atlas, const World *w, int32_t cx, int32_t cy,
                                           int32_t cz)
{
  FogChunk *existing = fog_find_chunk(atlas, w, cx, cy, cz);
  if (existing)
    return existing;

  if (atlas->count + 1 >= atlas->capacity / 2)
  {
    if (!fog_grow(atlas))
      return NULL;
  }

  FogChunk *chunk = (FogChunk *)calloc(1, sizeof(FogChunk));
  if (!chunk)
    return NULL;

  size_t idx = fog_hash(w, cx, cy, cz, atlas->capacity);
  for (size_t probe = 0; probe < atlas->capacity; probe++)
  {
    const size_t p = (idx + probe) % atlas->capacity;
    if (!atlas->entries[p].used)
    {
      atlas->entries[p].world = w;
      atlas->entries[p].cx = cx;
      atlas->entries[p].cy = cy;
      atlas->entries[p].cz = cz;
      atlas->entries[p].chunk = chunk;
      atlas->entries[p].used = true;
      atlas->count++;
      return chunk;
    }
  }

  free(chunk);
  return NULL;
}

static bool fog_chunk_test(const FogChunk *chunk, uint32_t lx, uint32_t ly, uint32_t lz)
{
  const uint32_t bit = lx + ly * FOG_CHUNK_SIZE + lz * FOG_CHUNK_SIZE * FOG_CHUNK_SIZE;
  const uint32_t word = bit >> 6;
  const uint32_t shift = bit & 63u;
  return (chunk->bits[word] >> shift) & 1u;
}

static bool fog_chunk_set(FogChunk *chunk, uint32_t lx, uint32_t ly, uint32_t lz)
{
  const uint32_t bit = lx + ly * FOG_CHUNK_SIZE + lz * FOG_CHUNK_SIZE * FOG_CHUNK_SIZE;
  const uint32_t word = bit >> 6;
  const uint32_t shift = bit & 63u;
  const uint64_t mask = (uint64_t)1 << shift;
  if (chunk->bits[word] & mask)
    return false;
  chunk->bits[word] |= mask;
  return true;
}

FogAtlas *fog_atlas_create(void)
{
  FogAtlas *atlas = (FogAtlas *)calloc(1, sizeof(FogAtlas));
  if (!atlas)
    return NULL;
  atlas->capacity = 256;
  atlas->entries = (FogEntry *)calloc(atlas->capacity, sizeof(FogEntry));
  if (!atlas->entries)
  {
    free(atlas);
    return NULL;
  }
  return atlas;
}

void fog_atlas_destroy(FogAtlas *atlas)
{
  if (!atlas)
    return;
  if (atlas->entries)
  {
    for (size_t i = 0; i < atlas->capacity; i++)
    {
      if (atlas->entries[i].used)
        free(atlas->entries[i].chunk);
    }
    free(atlas->entries);
  }
  free(atlas);
}

void fog_atlas_clear(FogAtlas *atlas)
{
  if (!atlas)
    return;
  if (atlas->entries)
  {
    for (size_t i = 0; i < atlas->capacity; i++)
    {
      if (atlas->entries[i].used)
        free(atlas->entries[i].chunk);
    }
    memset(atlas->entries, 0, atlas->capacity * sizeof(FogEntry));
  }
  atlas->count = 0;
  atlas->revision++;
  atlas->pose_valid = false;
}

uint64_t fog_atlas_revision(const FogAtlas *atlas)
{
  return atlas ? atlas->revision : 0;
}

size_t fog_atlas_chunk_count(const FogAtlas *atlas)
{
  return atlas ? atlas->count : 0;
}

bool fog_is_explored(const FogAtlas *atlas, const World *w, uint32_t x, uint32_t y, uint32_t z)
{
  if (!atlas || !w)
    return true;
  if (x >= w->width || y >= w->height || z >= w->depth)
    return false;

  const int32_t cx = (int32_t)(x >> 4);
  const int32_t cy = (int32_t)(y >> 4);
  const int32_t cz = (int32_t)(z >> 4);
  const FogChunk *chunk = fog_find_chunk(atlas, w, cx, cy, cz);
  if (!chunk)
    return false;

  return fog_chunk_test(chunk, x & 15u, y & 15u, z & 15u);
}

void fog_mark_explored(FogAtlas *atlas, const World *w, uint32_t x, uint32_t y, uint32_t z)
{
  if (!atlas || !w)
    return;
  if (x >= w->width || y >= w->height || z >= w->depth)
    return;

  const int32_t cx = (int32_t)(x >> 4);
  const int32_t cy = (int32_t)(y >> 4);
  const int32_t cz = (int32_t)(z >> 4);
  FogChunk *chunk = fog_get_or_create_chunk(atlas, w, cx, cy, cz);
  if (!chunk)
    return;

  if (fog_chunk_set(chunk, x & 15u, y & 15u, z & 15u))
    atlas->revision++;
}

static void fog_mark_cluster_cell(FogAtlas *atlas, const ShadowWorld *cluster, int cx, int cy,
                                  int cz)
{
  int lx = 0, ly = 0, lz = 0;
  const int slot = shadow_world_route(cluster, cx, cy, cz, &lx, &ly, &lz);
  if (slot < 0)
    return;
  const World *world = shadow_world_slot_world(cluster, slot);
  if (!world)
    return;
  fog_mark_explored(atlas, world, (uint32_t)lx, (uint32_t)ly, (uint32_t)lz);
}

// Mark a disc across the beam, not along it. A cube around the hit would leak explored cells
// through walls; dropping the dominant axis keeps the extra marks on the face the ray actually sees.
static void fog_mark_beam(FogAtlas *atlas, const ShadowWorld *cluster, int vx, int vy, int vz,
                          float dx, float dy, float dz, int radius)
{
  fog_mark_cluster_cell(atlas, cluster, vx, vy, vz);
  if (radius <= 0)
    return;

  const float ax = fabsf(dx), ay = fabsf(dy), az = fabsf(dz);
  const int drop = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
  const int r2 = radius * radius;

  for (int i = -radius; i <= radius; i++)
  {
    for (int j = -radius; j <= radius; j++)
    {
      if (i == 0 && j == 0)
        continue;
      if (i * i + j * j > r2)
        continue;
      int x = vx, y = vy, z = vz;
      if (drop == 0)
      {
        y += i;
        z += j;
      }
      else if (drop == 1)
      {
        x += i;
        z += j;
      }
      else
      {
        x += i;
        y += j;
      }
      fog_mark_cluster_cell(atlas, cluster, x, y, z);
    }
  }
}

static int fog_beam_radius(float dist)
{
  // Coarsest ray spacing is about 0.04 * dist voxels; grow the disc just enough to fill it.
  int r = (int)(dist * 0.035f) + 1;
  if (r > FOG_BEAM_RADIUS_MAX)
    r = FOG_BEAM_RADIUS_MAX;
  return r;
}

static float fog_safe_inv(float v)
{
  const float eps = 1e-6f;
  if (v > -eps && v < eps)
    return 1e30f;
  return 1.0f / v;
}

static void fog_build_camera_basis(float yaw, float pitch, float *fwd_x, float *fwd_y, float *fwd_z,
                                   float *right_x, float *right_y, float *right_z, float *up_x,
                                   float *up_y, float *up_z)
{
  const float cy = cosf(yaw);
  const float sy = sinf(yaw);
  const float cp = cosf(pitch);
  const float sp = sinf(pitch);

  *fwd_x = cy * cp;
  *fwd_y = sy * cp;
  *fwd_z = sp;

  float len = sqrtf((*fwd_x) * (*fwd_x) + (*fwd_y) * (*fwd_y) + (*fwd_z) * (*fwd_z));
  if (len > 0.0f)
  {
    *fwd_x /= len;
    *fwd_y /= len;
    *fwd_z /= len;
  }

  *up_x = 0.0f;
  *up_y = 0.0f;
  *up_z = 1.0f;
  if (fabsf(*fwd_z) > 0.98f)
  {
    *up_x = 1.0f;
    *up_y = 0.0f;
    *up_z = 0.0f;
  }

  *right_x = (*up_y) * (*fwd_z) - (*up_z) * (*fwd_y);
  *right_y = (*up_z) * (*fwd_x) - (*up_x) * (*fwd_z);
  *right_z = (*up_x) * (*fwd_y) - (*up_y) * (*fwd_x);
  len = sqrtf((*right_x) * (*right_x) + (*right_y) * (*right_y) + (*right_z) * (*right_z));
  if (len > 0.0f)
  {
    *right_x /= len;
    *right_y /= len;
    *right_z /= len;
  }

  *up_x = (*fwd_y) * (*right_z) - (*fwd_z) * (*right_y);
  *up_y = (*fwd_z) * (*right_x) - (*fwd_x) * (*right_z);
  *up_z = (*fwd_x) * (*right_y) - (*fwd_y) * (*right_x);
  len = sqrtf((*up_x) * (*up_x) + (*up_y) * (*up_y) + (*up_z) * (*up_z));
  if (len > 0.0f)
  {
    *up_x /= len;
    *up_y /= len;
    *up_z /= len;
  }
}

static void fog_reveal_ray(FogAtlas *atlas, const ShadowWorld *cluster, float ox, float oy, float oz,
                           float dx, float dy, float dz, float max_range)
{
  float len = sqrtf(dx * dx + dy * dy + dz * dz);
  if (len < 1e-6f)
    return;
  dx /= len;
  dy /= len;
  dz /= len;

  int vx = (int)floorf(ox);
  int vy = (int)floorf(oy);
  int vz = (int)floorf(oz);

  const int step_x = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
  const int step_y = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);
  const int step_z = (dz > 0.0f) ? 1 : (dz < 0.0f ? -1 : 0);

  const float inv_x = fog_safe_inv(dx);
  const float inv_y = fog_safe_inv(dy);
  const float inv_z = fog_safe_inv(dz);

  const float bx = (float)vx + (step_x > 0 ? 1.0f : 0.0f);
  const float by = (float)vy + (step_y > 0 ? 1.0f : 0.0f);
  const float bz = (float)vz + (step_z > 0 ? 1.0f : 0.0f);

  float t_max_x = (step_x != 0) ? (bx - ox) * inv_x : 1e30f;
  float t_max_y = (step_y != 0) ? (by - oy) * inv_y : 1e30f;
  float t_max_z = (step_z != 0) ? (bz - oz) * inv_z : 1e30f;

  const float t_delta_x = fabsf(1.0f * inv_x);
  const float t_delta_y = fabsf(1.0f * inv_y);
  const float t_delta_z = fabsf(1.0f * inv_z);

  const float max_range_sq = max_range * max_range;
  const int max_steps = (int)(max_range * 1.75f) + 8;

  for (int step = 0; step < max_steps; step++)
  {
    const float cx = (float)vx + 0.5f - ox;
    const float cy = (float)vy + 0.5f - oy;
    const float cz = (float)vz + 0.5f - oz;
    if (cx * cx + cy * cy + cz * cz > max_range_sq)
      break;

    if (shadow_world_in_bounds(cluster, vx, vy, vz))
    {
      const float dist = sqrtf(cx * cx + cy * cy + cz * cz);
      fog_mark_beam(atlas, cluster, vx, vy, vz, dx, dy, dz, fog_beam_radius(dist));
      const ShadowSample sample = shadow_world_sample(cluster, vx, vy, vz);
      if (sample == SHADOW_SOLID)
        break;
      if (sample == SHADOW_UNLOADED)
        break;
    }

    if (t_max_x < t_max_y)
    {
      if (t_max_x < t_max_z)
      {
        vx += step_x;
        t_max_x += t_delta_x;
      }
      else
      {
        vz += step_z;
        t_max_z += t_delta_z;
      }
    }
    else
    {
      if (t_max_y < t_max_z)
      {
        vy += step_y;
        t_max_y += t_delta_y;
      }
      else
      {
        vz += step_z;
        t_max_z += t_delta_z;
      }
    }
  }
}

static void fog_reveal_player_bubble(FogAtlas *atlas, const World *world, float px, float py,
                                     float pz)
{
  if (!atlas || !world)
    return;

  const int ix = (int)floorf(px);
  const int iy = (int)floorf(py);
  const int iz = (int)floorf(pz);

  for (int dz = -FOG_PLAYER_BUBBLE; dz <= FOG_PLAYER_BUBBLE; dz++)
  {
    for (int dy = -FOG_PLAYER_BUBBLE; dy <= FOG_PLAYER_BUBBLE; dy++)
    {
      for (int dx = -FOG_PLAYER_BUBBLE; dx <= FOG_PLAYER_BUBBLE; dx++)
      {
        const int x = ix + dx;
        const int y = iy + dy;
        const int z = iz + dz;
        if (x < 0 || y < 0 || z < 0)
          continue;
        if (x >= (int)world->width || y >= (int)world->height || z >= (int)world->depth)
          continue;
        fog_mark_explored(atlas, world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
      }
    }
  }
}

void fog_reveal_around(FogAtlas *atlas, const ShadowWorld *cluster, float px, float py, float pz,
                       int radius)
{
  if (!atlas || !cluster || radius <= 0)
    return;

  if (atlas->pose_valid && fabsf(atlas->last_eye_x - px) < FOG_MAP_POSE_EPS &&
      fabsf(atlas->last_eye_y - py) < FOG_MAP_POSE_EPS &&
      fabsf(atlas->last_eye_z - pz) < FOG_MAP_POSE_EPS)
    return;

  atlas->last_eye_x = px;
  atlas->last_eye_y = py;
  atlas->last_eye_z = pz;
  atlas->pose_valid = true;

  int origin_x = 0, origin_y = 0, origin_z = 0;
  shadow_world_slot_origin(cluster, SHADOW_CENTRE_SLOT, &origin_x, &origin_y, &origin_z);

  const int cx = (int)floorf(px) + origin_x;
  const int cy = (int)floorf(py) + origin_y;
  const int cz = (int)floorf(pz) + origin_z;
  const int r2 = radius * radius;

  for (int dz = -radius; dz <= radius; dz++)
  {
    for (int dy = -radius; dy <= radius; dy++)
    {
      for (int dx = -radius; dx <= radius; dx++)
      {
        if (dx * dx + dy * dy + dz * dz > r2)
          continue;
        fog_mark_cluster_cell(atlas, cluster, cx + dx, cy + dy, cz + dz);
      }
    }
  }
}

void fog_reveal_from_view(FogAtlas *atlas, const ShadowWorld *cluster, float eye_x, float eye_y,
                          float eye_z, float yaw, float pitch, float fov_deg_v, float aspect,
                          float max_range)
{
  if (!atlas || !cluster)
    return;

  if (atlas->pose_valid &&
      fabsf(atlas->last_eye_x - eye_x) < FOG_POSE_EPS &&
      fabsf(atlas->last_eye_y - eye_y) < FOG_POSE_EPS &&
      fabsf(atlas->last_eye_z - eye_z) < FOG_POSE_EPS &&
      fabsf(atlas->last_yaw - yaw) < FOG_ANGLE_EPS &&
      fabsf(atlas->last_pitch - pitch) < FOG_ANGLE_EPS)
    return;

  atlas->last_eye_x = eye_x;
  atlas->last_eye_y = eye_y;
  atlas->last_eye_z = eye_z;
  atlas->last_yaw = yaw;
  atlas->last_pitch = pitch;
  atlas->pose_valid = true;

  const World *centre = shadow_world_slot_world(cluster, SHADOW_CENTRE_SLOT);
  if (centre)
    fog_reveal_player_bubble(atlas, centre, eye_x, eye_y, eye_z);

  if (max_range <= 0.0f)
    return;

  int origin_x = 0, origin_y = 0, origin_z = 0;
  shadow_world_slot_origin(cluster, SHADOW_CENTRE_SLOT, &origin_x, &origin_y, &origin_z);

  const float cluster_eye_x = eye_x + (float)origin_x;
  const float cluster_eye_y = eye_y + (float)origin_y;
  const float cluster_eye_z = eye_z + (float)origin_z;

  float fwd_x, fwd_y, fwd_z;
  float right_x, right_y, right_z;
  float up_x, up_y, up_z;
  fog_build_camera_basis(yaw, pitch, &fwd_x, &fwd_y, &fwd_z, &right_x, &right_y, &right_z, &up_x,
                         &up_y, &up_z);

  const float half_tan_v = tanf(fov_deg_v * 0.5f * (float)M_PI / 180.0f);
  const float half_tan_h = half_tan_v * ((aspect > 0.0f) ? aspect : 1.0f);
  const float origin_eps = 0.05f;

  for (int row = 0; row < FOG_RAY_ROWS; row++)
  {
    const float ny = 1.0f - (2.0f * ((float)row + 0.5f) / (float)FOG_RAY_ROWS);
    const float row_rx = fwd_x + ny * half_tan_v * up_x;
    const float row_ry = fwd_y + ny * half_tan_v * up_y;
    const float row_rz = fwd_z + ny * half_tan_v * up_z;

    for (int col = 0; col < FOG_RAY_COLS; col++)
    {
      const float nx = (2.0f * ((float)col + 0.5f) / (float)FOG_RAY_COLS) - 1.0f;
      float dx = row_rx + nx * half_tan_h * right_x;
      float dy = row_ry + nx * half_tan_h * right_y;
      float dz = row_rz + nx * half_tan_h * right_z;

      const float dlen = sqrtf(dx * dx + dy * dy + dz * dz);
      if (dlen < 1e-6f)
        continue;
      dx /= dlen;
      dy /= dlen;
      dz /= dlen;

      const float ox = cluster_eye_x + dx * origin_eps;
      const float oy = cluster_eye_y + dy * origin_eps;
      const float oz = cluster_eye_z + dz * origin_eps;
      fog_reveal_ray(atlas, cluster, ox, oy, oz, dx, dy, dz, max_range);
    }
  }
}
