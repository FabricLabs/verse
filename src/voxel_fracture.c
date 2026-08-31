#include "voxel_fracture.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "voxel_combat.h"
#include "voxel_debris_volume.h"

static DebrisVolumeSystem *s_debris_volumes = NULL;

// Pending linear impulse applied to every volume extracted during the next disconnect pass
// (crack direction × impact speed). Cleared after disconnect returns.
static int s_extract_kick_armed = 0;
static float s_extract_kick_jx = 0.0f;
static float s_extract_kick_jy = 0.0f;
static float s_extract_kick_jz = 0.0f;
static float s_extract_hit_x = 0.0f;
static float s_extract_hit_y = 0.0f;
static float s_extract_hit_z = 0.0f;

void voxel_fracture_set_debris_volumes(struct DebrisVolumeSystem *sys)
{
  s_debris_volumes = sys;
}

static void kick_extracted_volume(DebrisVolume *vol)
{
  if (!vol || !s_extract_kick_armed)
    return;
  voxel_debris_volume_apply_impulse(vol, s_extract_kick_jx, s_extract_kick_jy, s_extract_kick_jz,
                                    s_extract_hit_x, s_extract_hit_y, s_extract_hit_z);
}

static const int k_face[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

typedef struct
{
  int x, y, z;
} FractCell;

bool voxel_fracture_is_structural(VoxelType type)
{
  if (type == VOXEL_AIR)
    return false;
  if (world_voxel_type_is_fluidlike(type))
    return false;
  return true;
}

static inline bool is_falling(const Voxel *v)
{
  return v && voxel_fracture_is_structural(v->type) && v->momentum[2] < -0.5f;
}

static inline void mark_falling(Voxel *v)
{
  if (!v)
    return;
  v->momentum[0] = 0.0f;
  v->momentum[1] = 0.0f;
  v->momentum[2] = -1.0f;
}

static inline void clear_falling(Voxel *v)
{
  if (!v)
    return;
  v->momentum[0] = 0.0f;
  v->momentum[1] = 0.0f;
  v->momentum[2] = 0.0f;
}

static inline bool is_support_seed(VoxelType type, int z)
{
  if (type == VOXEL_BEDROCK)
    return true;
  if (z <= 0 && voxel_fracture_is_structural(type))
    return true;
  return false;
}

static uint32_t stamp_hash(uint32_t seed, int x, int y, int z)
{
  uint32_t h = seed ^ (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(z * 83492791);
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return h;
}

static int collect_component(World *world, int sx, int sy, int sz, FractCell *out, int cap,
                             uint8_t *seen, int stride_y, int stride_z, int ox, int oy, int oz,
                             int bw, int bh, int bd, bool *reached_support)
{
  *reached_support = false;
  if (cap < 1 || !out || !seen)
    return 0;

  int n = 0;
  int head = 0;
  out[n++] = (FractCell){sx, sy, sz};
  {
    const int lx = sx - ox, ly = sy - oy, lz = sz - oz;
    seen[(size_t)lz * (size_t)stride_z + (size_t)ly * (size_t)stride_y + (size_t)lx] = 1;
  }

  while (head < n)
  {
    const FractCell c = out[head++];
    const Voxel *v = world_voxel_cptr_fast(world, c.x, c.y, c.z);
    if (!v || !voxel_fracture_is_structural(v->type))
      continue;
    if (is_support_seed(v->type, c.z))
      *reached_support = true;

    for (int f = 0; f < 6; f++)
    {
      const int nx = c.x + k_face[f][0];
      const int ny = c.y + k_face[f][1];
      const int nz = c.z + k_face[f][2];
      if (!world_pos_in_bounds_fast(world, nx, ny, nz))
        continue;
      const int lx = nx - ox, ly = ny - oy, lz = nz - oz;
      if (lx < 0 || ly < 0 || lz < 0 || lx >= bw || ly >= bh || lz >= bd)
        continue;
      const size_t si = (size_t)lz * (size_t)stride_z + (size_t)ly * (size_t)stride_y + (size_t)lx;
      if (seen[si])
        continue;
      const Voxel *nv = world_voxel_cptr_fast(world, nx, ny, nz);
      if (!nv || !voxel_fracture_is_structural(nv->type))
        continue;
      seen[si] = 1;
      if (n >= cap)
      {
        *reached_support = true; // work cap: leave standing
        return n;
      }
      out[n++] = (FractCell){nx, ny, nz};
      if (is_support_seed(nv->type, nz))
        *reached_support = true;
    }
  }
  return n;
}

#define FRACTURE_SEED_CAP 512
// Hard cap on cells marked/extracted in one disconnect call so chopping a whole grove cannot
// spend multiple frames in a single physics tick. Remaining islands stay until the next stamp.
#define FRACTURE_DISCONNECT_MARK_BUDGET 4096

// Find an unmarked cell in `comp` at (x,y,z). O(n); only used when batching oversized islands.
static int find_untaken_comp(const FractCell *comp, const uint8_t *taken, int n, int x, int y, int z)
{
  for (int i = 0; i < n; i++)
  {
    if (taken[i])
      continue;
    if (comp[i].x == x && comp[i].y == y && comp[i].z == z)
      return i;
  }
  return -1;
}

// Lift an unsupported component into one or more debris volumes (CELL_CAP-sized connected batches).
// Returns cells extracted; leftover indices stay 0 in `taken` for the caller to mark falling.
static int extract_component_volumes(World *world, const FractCell *comp, int n, uint8_t *taken)
{
  if (!s_debris_volumes || !comp || n <= 0 || !taken)
    return 0;

  int extracted_total = 0;
  for (;;)
  {
    if (extracted_total >= FRACTURE_DISCONNECT_MARK_BUDGET)
      break;
    if (voxel_debris_volume_active_count(s_debris_volumes) >= DEBRIS_VOLUME_MAX)
      break;

    int start = -1;
    for (int i = 0; i < n; i++)
    {
      if (taken[i])
        continue;
      Voxel *cell = world_voxel_ptr_fast(world, comp[i].x, comp[i].y, comp[i].z);
      if (!cell || cell->type == VOXEL_BEDROCK || !voxel_fracture_is_structural(cell->type))
      {
        taken[i] = 1; // skip permanently
        continue;
      }
      start = i;
      break;
    }
    if (start < 0)
      break;

    int xs[DEBRIS_VOLUME_CELL_CAP];
    int ys[DEBRIS_VOLUME_CELL_CAP];
    int zs[DEBRIS_VOLUME_CELL_CAP];
    int batch_idx[DEBRIS_VOLUME_CELL_CAP];
    int queue[DEBRIS_VOLUME_CELL_CAP];
    int qh = 0, qt = 0;
    int batch_n = 0;
    queue[qt++] = start;
    taken[start] = 1;

    while (qh < qt && batch_n < DEBRIS_VOLUME_CELL_CAP)
    {
      const int ci = queue[qh++];
      batch_idx[batch_n] = ci;
      xs[batch_n] = comp[ci].x;
      ys[batch_n] = comp[ci].y;
      zs[batch_n] = comp[ci].z;
      batch_n++;
      if (batch_n >= DEBRIS_VOLUME_CELL_CAP)
        break;

      for (int f = 0; f < 6; f++)
      {
        const int nx = comp[ci].x + k_face[f][0];
        const int ny = comp[ci].y + k_face[f][1];
        const int nz = comp[ci].z + k_face[f][2];
        const int ni = find_untaken_comp(comp, taken, n, nx, ny, nz);
        if (ni < 0)
          continue;
        Voxel *cell = world_voxel_ptr_fast(world, nx, ny, nz);
        if (!cell || cell->type == VOXEL_BEDROCK || !voxel_fracture_is_structural(cell->type))
        {
          taken[ni] = 1;
          continue;
        }
        if (qt >= DEBRIS_VOLUME_CELL_CAP)
          break;
        taken[ni] = 1;
        queue[qt++] = ni;
      }
    }
    // Queued but not packed into this batch must stay available for the next volume.
    for (int i = qh; i < qt; i++)
      taken[queue[i]] = 0;

    if (batch_n <= 0)
      break;

    DebrisVolume *out_vol = NULL;
    const int got =
        voxel_debris_volume_extract_ex(s_debris_volumes, world, xs, ys, zs, batch_n, &out_vol);
    if (got <= 0)
    {
      for (int i = 0; i < batch_n; i++)
        taken[batch_idx[i]] = 0;
      break;
    }
    kick_extracted_volume(out_vol);
    extracted_total += got;
  }

  return extracted_total;
}

// Seed BFS only from structural neighbours of the given air cells — not every surface in a box.
static int disconnect_from_air_cells(World *world, const FractCell *air, int air_n)
{
  if (!world || !world->voxels || !air || air_n <= 0)
    return 0;

  FractCell seeds[FRACTURE_SEED_CAP];
  int seed_n = 0;
  int minx = air[0].x, maxx = air[0].x;
  int miny = air[0].y, maxy = air[0].y;
  int minz = air[0].z, maxz = air[0].z;

  for (int i = 0; i < air_n; i++)
  {
    if (air[i].x < minx)
      minx = air[i].x;
    if (air[i].x > maxx)
      maxx = air[i].x;
    if (air[i].y < miny)
      miny = air[i].y;
    if (air[i].y > maxy)
      maxy = air[i].y;
    if (air[i].z < minz)
      minz = air[i].z;
    if (air[i].z > maxz)
      maxz = air[i].z;

    for (int f = 0; f < 6; f++)
    {
      const int nx = air[i].x + k_face[f][0];
      const int ny = air[i].y + k_face[f][1];
      const int nz = air[i].z + k_face[f][2];
      if (!world_pos_in_bounds_fast(world, nx, ny, nz))
        continue;
      const Voxel *nv = world_voxel_cptr_fast(world, nx, ny, nz);
      if (!nv || !voxel_fracture_is_structural(nv->type) || nv->type == VOXEL_BEDROCK)
        continue;
      bool dup = false;
      for (int s = 0; s < seed_n; s++)
        if (seeds[s].x == nx && seeds[s].y == ny && seeds[s].z == nz)
        {
          dup = true;
          break;
        }
      if (dup)
        continue;
      if (seed_n >= FRACTURE_SEED_CAP)
        break;
      seeds[seed_n++] = (FractCell){nx, ny, nz};
    }
  }

  if (seed_n == 0)
    return 0;

  const int margin = 24;
  int x0 = minx - margin, y0 = miny - margin, z0 = minz - margin;
  int x1 = maxx + margin, y1 = maxy + margin, z1 = maxz + margin;
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (z0 < 0)
    z0 = 0;
  if (x1 >= (int)world->width)
    x1 = (int)world->width - 1;
  if (y1 >= (int)world->height)
    y1 = (int)world->height - 1;
  if (z1 >= (int)world->depth)
    z1 = (int)world->depth - 1;

  const int bw = x1 - x0 + 1;
  const int bh = y1 - y0 + 1;
  const int bd = z1 - z0 + 1;
  const int stride_y = bw;
  const int stride_z = bw * bh;
  uint8_t *seen = (uint8_t *)calloc((size_t)bw * (size_t)bh * (size_t)bd, 1);
  FractCell *comp = (FractCell *)malloc((size_t)FRACTURE_COMPONENT_CAP * sizeof(FractCell));
  if (!seen || !comp)
  {
    free(seen);
    free(comp);
    return 0;
  }

  int marked = 0;
  for (int s = 0; s < seed_n; s++)
  {
    if (marked >= FRACTURE_DISCONNECT_MARK_BUDGET)
      break;

    const int lx = seeds[s].x - x0, ly = seeds[s].y - y0, lz = seeds[s].z - z0;
    if (lx < 0 || ly < 0 || lz < 0 || lx >= bw || ly >= bh || lz >= bd)
      continue;
    const size_t si = (size_t)lz * (size_t)stride_z + (size_t)ly * (size_t)stride_y + (size_t)lx;
    if (seen[si])
      continue;

    bool supported = false;
    const int n = collect_component(world, seeds[s].x, seeds[s].y, seeds[s].z, comp,
                                    FRACTURE_COMPONENT_CAP, seen, stride_y, stride_z, x0, y0, z0,
                                    bw, bh, bd, &supported);
    if (n <= 0 || supported)
      continue;

    // Pass 2: lift unsupported islands into transformed volumes (batched when larger than one slot).
    if (s_debris_volumes && n > 0)
    {
      uint8_t *taken = (uint8_t *)calloc((size_t)n, 1);
      if (taken)
      {
        const int extracted = extract_component_volumes(world, comp, n, taken);
        marked += extracted;
        for (int i = 0; i < n; i++)
        {
          if (taken[i])
            continue;
          if (marked >= FRACTURE_DISCONNECT_MARK_BUDGET)
            break;
          Voxel *cell = world_voxel_ptr_fast(world, comp[i].x, comp[i].y, comp[i].z);
          if (!cell || cell->type == VOXEL_BEDROCK)
            continue;
          if (!voxel_fracture_is_structural(cell->type))
            continue;
          mark_falling(cell);
          marked++;
        }
        free(taken);
        continue;
      }
    }

    for (int i = 0; i < n; i++)
    {
      if (marked >= FRACTURE_DISCONNECT_MARK_BUDGET)
        break;
      Voxel *cell = world_voxel_ptr_fast(world, comp[i].x, comp[i].y, comp[i].z);
      if (!cell || cell->type == VOXEL_BEDROCK)
        continue;
      if (!voxel_fracture_is_structural(cell->type))
        continue;
      mark_falling(cell);
      marked++;
    }
  }

  free(seen);
  free(comp);
  if (marked > 0)
    world->voxel_revision++;
  return marked;
}

int voxel_fracture_disconnect_region(World *world, int x0, int y0, int z0, int x1, int y1, int z1)
{
  if (!world || !world->voxels)
    return 0;

  if (x0 > x1)
  {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (y0 > y1)
  {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (z0 > z1)
  {
    int t = z0;
    z0 = z1;
    z1 = t;
  }
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (z0 < 0)
    z0 = 0;
  if (x1 >= (int)world->width)
    x1 = (int)world->width - 1;
  if (y1 >= (int)world->height)
    y1 = (int)world->height - 1;
  if (z1 >= (int)world->depth)
    z1 = (int)world->depth - 1;

  FractCell air[FRACTURE_SEED_CAP];
  int air_n = 0;
  for (int z = z0; z <= z1 && air_n < FRACTURE_SEED_CAP; z++)
    for (int y = y0; y <= y1 && air_n < FRACTURE_SEED_CAP; y++)
      for (int x = x0; x <= x1 && air_n < FRACTURE_SEED_CAP; x++)
      {
        const Voxel *v = world_voxel_cptr_fast(world, x, y, z);
        if (v && v->type == VOXEL_AIR)
          air[air_n++] = (FractCell){x, y, z};
      }

  return disconnect_from_air_cells(world, air, air_n);
}

int voxel_fracture_disconnect_at(World *world, int x, int y, int z)
{
  // The deleted cell is air; seed only its solid neighbours.
  const FractCell air = {x, y, z};
  return disconnect_from_air_cells(world, &air, 1);
}

static int stamp_cells(World *world, int cx, int cy, int cz, float dir_x, float dir_y, float dir_z,
                       float speed_vox_s, uint32_t seed, FractCell *out_air, int air_cap,
                       int *out_air_n)
{
  if (out_air_n)
    *out_air_n = 0;
  if (!world)
    return 0;

  float len = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
  if (len < 1e-4f)
  {
    dir_x = 1.0f;
    dir_y = 0.0f;
    dir_z = 0.0f;
    len = 1.0f;
  }
  dir_x /= len;
  dir_y /= len;
  dir_z /= len;

  int radius = 1;
  if (speed_vox_s >= 20.0f)
    radius = 2;
  if (speed_vox_s >= 32.0f)
    radius = 3;
  if (radius > FRACTURE_STAMP_RADIUS_MAX)
    radius = FRACTURE_STAMP_RADIUS_MAX;

  float px = -dir_y, py = dir_x, pz = 0.0f;
  float plen = sqrtf(px * px + py * py + pz * pz);
  if (plen < 1e-4f)
  {
    px = 0.0f;
    py = -dir_z;
    pz = dir_y;
    plen = sqrtf(px * px + py * py + pz * pz);
  }
  if (plen > 1e-4f)
  {
    px /= plen;
    py /= plen;
    pz /= plen;
  }
  float qx = dir_y * pz - dir_z * py;
  float qy = dir_z * px - dir_x * pz;
  float qz = dir_x * py - dir_y * px;
  float qlen = sqrtf(qx * qx + qy * qy + qz * qz);
  if (qlen > 1e-4f)
  {
    qx /= qlen;
    qy /= qlen;
    qz /= qlen;
  }

  // Worley feature points inside the stamp ball (Devlog #28 chunky rock vs shard ice).
  // More features + lower threshold → finer shards; fewer → chunky breaks.
  const int nfeat = (speed_vox_s >= 28.0f) ? 7 : (speed_vox_s >= 16.0f) ? 5 : 4;
  float feat[7][3];
  for (int i = 0; i < nfeat; i++)
  {
    const uint32_t h = stamp_hash(seed ^ (uint32_t)(i * 0x9e3779b9u), cx, cy, cz);
    const float u = ((float)(h & 0xFFu) / 255.0f) * 2.0f - 1.0f;
    const float v = ((float)((h >> 8) & 0xFFu) / 255.0f) * 2.0f - 1.0f;
    const float w = ((float)((h >> 16) & 0xFFu) / 255.0f) * 2.0f - 1.0f;
    const float scale = (float)radius * 0.85f;
    feat[i][0] = u * scale;
    feat[i][1] = v * scale;
    feat[i][2] = w * scale;
  }

  int deleted = 0;
  for (int dz = -radius; dz <= radius; dz++)
    for (int dy = -radius; dy <= radius; dy++)
      for (int dx = -radius; dx <= radius; dx++)
      {
        const int x = cx + dx;
        const int y = cy + dy;
        const int z = cz + dz;
        if (!world_pos_in_bounds_fast(world, x, y, z))
          continue;

        Voxel *v = world_voxel_ptr_fast(world, x, y, z);
        if (!v || !voxel_is_destructible(v->type))
          continue;

        const float fx = (float)dx + 0.5f;
        const float fy = (float)dy + 0.5f;
        const float fz = (float)dz + 0.5f;
        const float r2 = fx * fx + fy * fy + fz * fz;
        const float rad = (float)radius + 0.75f;
        if (r2 > rad * rad)
          continue;

        const float along = fabsf(fx * dir_x + fy * dir_y + fz * dir_z);
        const float across_p = fabsf(fx * px + fy * py + fz * pz);
        const float across_q = fabsf(fx * qx + fy * qy + fz * qz);
        // Three-plane cut through the impact (cardinal art orientations approximated by dir basis).
        const float plane = fminf(along, fminf(across_p, across_q));

        float f1 = 1e9f;
        for (int i = 0; i < nfeat; i++)
        {
          const float ex = fx - feat[i][0];
          const float ey = fy - feat[i][1];
          const float ez = fz - feat[i][2];
          const float d2 = ex * ex + ey * ey + ez * ez;
          if (d2 < f1)
            f1 = d2;
        }
        const float worley = sqrtf(f1) / ((float)radius + 0.75f);

        // Stone/wood bias: denser materials keep more cells (higher Worley threshold to delete).
        float mass = voxel_type_mass_kg(v->type);
        if (mass < 1.0f)
          mass = 1.0f;
        if (mass > 40.0f)
          mass = 40.0f;
        const float chunk_thresh = 0.22f + 0.20f * (1.0f - mass / 40.0f);

        const bool impact = (dx == 0 && dy == 0 && dz == 0);
        const bool cut = plane < 0.42f;
        const bool chunk = worley < chunk_thresh;
        if (!impact && !cut && !chunk)
          continue;

        const VoxelType was = v->type;
        const VoxelType left = voxel_type_after_break(was);
        world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, left);
        if (left != VOXEL_AIR)
        {
          // Coat scrape: host stays solid, so this cell must not seed a disconnect flood.
          Voxel *host = world_voxel_ptr_fast(world, x, y, z);
          if (host)
            voxel_set_damage(host, 0);
          deleted++;
          continue;
        }
        deleted++;
        if (out_air && out_air_n && *out_air_n < air_cap)
          out_air[(*out_air_n)++] = (FractCell){x, y, z};
      }

  return deleted;
}

int voxel_fracture_apply_crack(World *world, int cx, int cy, int cz, float dir_x, float dir_y,
                               float dir_z, float speed_vox_s, uint32_t seed)
{
  if (!world)
    return 0;

  float len = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
  if (len < 1e-5f)
  {
    dir_x = 1.0f;
    dir_y = 0.0f;
    dir_z = 0.0f;
    len = 1.0f;
  }
  const float ux = dir_x / len;
  const float uy = dir_y / len;
  const float uz = dir_z / len;
  // Impulse magnitude in "cell-mass · vox/s" so a fast crack tumbles shards off-centre.
  float jmag = fminf(fabsf(speed_vox_s), 28.0f) * 0.55f;
  if (jmag < 1.5f)
    jmag = 1.5f;
  s_extract_kick_jx = ux * jmag;
  s_extract_kick_jy = uy * jmag;
  s_extract_kick_jz = uz * jmag * 0.35f - 0.8f; // slight lift + fall
  s_extract_hit_x = (float)cx + 0.5f;
  s_extract_hit_y = (float)cy + 0.5f;
  s_extract_hit_z = (float)cz + 0.5f;
  s_extract_kick_armed = 1;

  FractCell air[FRACTURE_SEED_CAP];
  int air_n = 0;
  const int deleted =
      stamp_cells(world, cx, cy, cz, dir_x, dir_y, dir_z, speed_vox_s, seed, air, FRACTURE_SEED_CAP,
                  &air_n);
  if (air_n > 0)
    disconnect_from_air_cells(world, air, air_n);
  else
    voxel_fracture_disconnect_at(world, cx, cy, cz);

  s_extract_kick_armed = 0;
  return deleted;
}

int voxel_fracture_falling_count(const World *world)
{
  if (!world || !world->voxels)
    return 0;
  int n = 0;
  const size_t total = (size_t)world->width * world->height * world->depth;
  for (size_t i = 0; i < total; i++)
    if (is_falling(&world->voxels[i]))
      n++;
  return n;
}

static int uf_find(int *parent, int i)
{
  while (parent[i] != i)
  {
    parent[i] = parent[parent[i]];
    i = parent[i];
  }
  return i;
}

static void uf_union(int *parent, int a, int b)
{
  a = uf_find(parent, a);
  b = uf_find(parent, b);
  if (a != b)
    parent[b] = a;
}

static int index_of_cell(const FractCell *cells, int count, int x, int y, int z)
{
  for (int i = 0; i < count; i++)
    if (cells[i].x == x && cells[i].y == y && cells[i].z == z)
      return i;
  return -1;
}

bool voxel_fracture_step_falling(World *world)
{
  if (!world || !world->voxels)
    return false;

  const int w = (int)world->width;
  const int h = (int)world->height;
  const int d = (int)world->depth;
  int z0 = 0, z1 = d - 1;
  if (world->occupied_z_min >= 0 && world->occupied_z_max >= world->occupied_z_min)
  {
    z0 = world->occupied_z_min;
    z1 = world->occupied_z_max + 1; // falling cells may sit one above the band briefly
    if (z1 >= d)
      z1 = d - 1;
  }

  FractCell *cells = NULL;
  int count = 0;
  int cap = 0;

  for (int z = z0; z <= z1; z++)
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        Voxel *v = world_voxel_ptr_fast(world, x, y, z);
        if (!is_falling(v))
          continue;
        if (count >= cap)
        {
          int ncap = cap ? cap * 2 : 256;
          FractCell *nb = (FractCell *)realloc(cells, (size_t)ncap * sizeof(FractCell));
          if (!nb)
          {
            free(cells);
            return false;
          }
          cells = nb;
          cap = ncap;
        }
        cells[count++] = (FractCell){x, y, z};
      }

  if (count == 0)
  {
    free(cells);
    return false;
  }

  for (int i = 1; i < count; i++)
  {
    FractCell key = cells[i];
    int j = i - 1;
    while (j >= 0 && cells[j].z > key.z)
    {
      cells[j + 1] = cells[j];
      j--;
    }
    cells[j + 1] = key;
  }

  int *parent = (int *)malloc((size_t)count * sizeof(int));
  bool *cluster_blocked = (bool *)calloc((size_t)count, sizeof(bool));
  if (!parent || !cluster_blocked)
  {
    free(parent);
    free(cluster_blocked);
    free(cells);
    return false;
  }
  for (int i = 0; i < count; i++)
    parent[i] = i;

  for (int i = 0; i < count; i++)
  {
    for (int f = 0; f < 6; f++)
    {
      const int j = index_of_cell(cells, count, cells[i].x + k_face[f][0],
                                  cells[i].y + k_face[f][1], cells[i].z + k_face[f][2]);
      if (j >= 0)
        uf_union(parent, i, j);
    }
  }

  for (int i = 0; i < count; i++)
  {
    const int root = uf_find(parent, i);
    const int x = cells[i].x;
    const int y = cells[i].y;
    const int z = cells[i].z;
    if (z <= 0)
    {
      cluster_blocked[root] = true;
      continue;
    }
    const Voxel *below = world_voxel_cptr_fast(world, x, y, z - 1);
    if (!below)
    {
      cluster_blocked[root] = true;
      continue;
    }
    if (below->type == VOXEL_AIR)
      continue;
    const int j = index_of_cell(cells, count, x, y, z - 1);
    if (j < 0 || uf_find(parent, j) != root)
      cluster_blocked[root] = true;
  }

  for (int i = 0; i < count; i++)
  {
    if (!cluster_blocked[uf_find(parent, i)])
      continue;
    clear_falling(world_voxel_ptr_fast(world, cells[i].x, cells[i].y, cells[i].z));
  }

  bool moved = false;
  for (int i = 0; i < count; i++)
  {
    if (cluster_blocked[uf_find(parent, i)])
      continue;

    const int x = cells[i].x;
    const int y = cells[i].y;
    const int z = cells[i].z;
    Voxel *src = world_voxel_ptr_fast(world, x, y, z);
    if (!src || !is_falling(src))
      continue;
    Voxel *dst = world_voxel_ptr_fast(world, x, y, z - 1);
    if (!dst || dst->type != VOXEL_AIR)
      continue;

    const Voxel keep = *src;
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, VOXEL_AIR);
    world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)(z - 1), keep.type);
    dst = world_voxel_ptr_fast(world, x, y, z - 1);
    if (dst)
    {
      *dst = keep;
      mark_falling(dst);
    }
    moved = true;
  }

  free(cluster_blocked);
  free(parent);
  free(cells);
  return moved;
}
