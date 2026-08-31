#include "voxel_debris_volume.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "voxel_combat.h"
#include "voxel_fracture.h"

#define DEBRIS_SHATTER_COOL_FRAMES 14
#define DEBRIS_SHATTER_MAX_PER_STEP 2
#define DEBRIS_SHATTER_MIN_CELLS 4

typedef struct
{
  float speed;
  float hx, hy, hz;
} DebrisPendingImpact;

static float material_friction(VoxelType t)
{
  if (t == VOXEL_ICE || t == VOXEL_WATER)
    return 0.12f;
  if (voxel_type_is_glass(t) || t == VOXEL_CRYSTAL || t == VOXEL_CRYSTAL_RED ||
      t == VOXEL_CRYSTAL_GREEN || t == VOXEL_CRYSTAL_BLUE)
    return 0.18f;
  if (t == VOXEL_LEAVES || t == VOXEL_BUSH)
    return 0.62f;
  if (t >= VOXEL_WOOD && t <= VOXEL_WOOD_REDWOOD)
    return 0.40f;
  if (t == VOXEL_PLANK || voxel_type_is_wool(t))
    return 0.50f;
  // Density relative to stone (voxel masses are ~0.5 kg, not SI tonnes).
  const float ref = voxel_type_mass_kg(VOXEL_STONE);
  float ratio = (ref > 1e-6f) ? (voxel_type_mass_kg(t) / ref) : 1.0f;
  if (ratio < 0.2f)
    ratio = 0.2f;
  if (ratio > 1.5f)
    ratio = 1.5f;
  // Stone ~0.48, lighter soils ~0.40, denser ores toward 0.55.
  return 0.38f + 0.12f * ((ratio - 0.2f) / 1.3f);
}

static float material_restitution(VoxelType t)
{
  if (voxel_type_is_glass(t) || t == VOXEL_CRYSTAL || t == VOXEL_CRYSTAL_RED ||
      t == VOXEL_CRYSTAL_GREEN || t == VOXEL_CRYSTAL_BLUE || t == VOXEL_ICE)
    return 0.28f;
  if (t >= VOXEL_WOOD && t <= VOXEL_WOOD_REDWOOD)
    return 0.12f;
  if (t == VOXEL_LEAVES || t == VOXEL_BUSH)
    return 0.02f;
  return 0.06f; // stone settles quickly
}

static void rebuild_mass_props(DebrisVolume *vol)
{
  const int n = vol->cell_count;
  if (n <= 0)
  {
    vol->inv_mass = 0.0f;
    vol->inv_ix = vol->inv_iy = vol->inv_iz = 0.0f;
    return;
  }

  float mass = 0.0f;
  float sx = 0.0f, sy = 0.0f, sz = 0.0f;
  for (int i = 0; i < n; i++)
  {
    float mi = voxel_type_mass_kg(vol->cells[i].type);
    if (mi < 0.02f)
      mi = 0.02f; // air-thin leftovers still need finite inertia
    const float cx = (float)vol->cells[i].lx + 0.5f;
    const float cy = (float)vol->cells[i].ly + 0.5f;
    const float cz = (float)vol->cells[i].lz + 0.5f;
    mass += mi;
    sx += mi * cx;
    sy += mi * cy;
    sz += mi * cz;
  }
  if (mass < 0.05f)
    mass = 0.05f;
  vol->inv_mass = 1.0f / mass;
  vol->com_lx = sx / mass;
  vol->com_ly = sy / mass;
  vol->com_lz = sz / mass;

  float ixx = 0.0f, iyy = 0.0f, izz = 0.0f;
  for (int i = 0; i < n; i++)
  {
    float mi = voxel_type_mass_kg(vol->cells[i].type);
    if (mi < 0.02f)
      mi = 0.02f;
    const float dx = (float)vol->cells[i].lx + 0.5f - vol->com_lx;
    const float dy = (float)vol->cells[i].ly + 0.5f - vol->com_ly;
    const float dz = (float)vol->cells[i].lz + 0.5f - vol->com_lz;
    ixx += mi * (dy * dy + dz * dz);
    iyy += mi * (dx * dx + dz * dz);
    izz += mi * (dx * dx + dy * dy);
  }
  // Thin plates / rods still need non-zero inertia so friction can tip them.
  const float i_floor = 0.05f * mass;
  if (ixx < i_floor)
    ixx = i_floor;
  if (iyy < i_floor)
    iyy = i_floor;
  if (izz < i_floor)
    izz = i_floor;
  vol->inv_ix = 1.0f / (ixx + 1e-4f);
  vol->inv_iy = 1.0f / (iyy + 1e-4f);
  vol->inv_iz = 1.0f / (izz + 1e-4f);
}

// Relative heaviness vs an 8-cell stone block. Used for game-feel drag / terminal speed —
// massive piles shed speed and spin faster than light foliage scraps.
static float volume_mass_scale(const DebrisVolume *vol)
{
  if (!vol || vol->inv_mass < 1e-8f)
    return 1.0f;
  const float mass = 1.0f / vol->inv_mass;
  const float ref = voxel_type_mass_kg(VOXEL_STONE) * 8.0f;
  float s = mass / fmaxf(ref, 0.05f);
  if (s < 0.35f)
    s = 0.35f;
  if (s > 5.0f)
    s = 5.0f;
  return s;
}

void voxel_debris_volume_reset(DebrisVolumeSystem *sys)
{
  if (!sys)
    return;
  memset(sys, 0, sizeof(*sys));
}

int voxel_debris_volume_active_count(const DebrisVolumeSystem *sys)
{
  if (!sys)
    return 0;
  int n = 0;
  for (int i = 0; i < DEBRIS_VOLUME_MAX; i++)
    if (sys->items[i].active)
      n++;
  return n;
}

static DebrisVolume *alloc_volume(DebrisVolumeSystem *sys)
{
  for (int i = 0; i < DEBRIS_VOLUME_MAX; i++)
    if (!sys->items[i].active)
      return &sys->items[i];
  return NULL;
}

int voxel_debris_volume_extract(DebrisVolumeSystem *sys, World *world, const int *xs, const int *ys,
                                const int *zs, int count)
{
  return voxel_debris_volume_extract_ex(sys, world, xs, ys, zs, count, NULL);
}

int voxel_debris_volume_extract_ex(DebrisVolumeSystem *sys, World *world, const int *xs,
                                   const int *ys, const int *zs, int count, DebrisVolume **out_vol)
{
  if (out_vol)
    *out_vol = NULL;
  if (!sys || !world || !xs || !ys || !zs || count <= 0)
    return 0;
  if (count > DEBRIS_VOLUME_CELL_CAP)
    return 0;

  DebrisVolume *vol = alloc_volume(sys);
  if (!vol)
    return 0;

  int minx = xs[0], miny = ys[0], minz = zs[0];
  for (int i = 1; i < count; i++)
  {
    if (xs[i] < minx)
      minx = xs[i];
    if (ys[i] < miny)
      miny = ys[i];
    if (zs[i] < minz)
      minz = zs[i];
  }

  memset(vol, 0, sizeof(*vol));
  vol->home = world;
  vol->x = (float)minx;
  vol->y = (float)miny;
  vol->z = (float)minz;
  vol->vz = -0.5f;

  int written = 0;
  float fric_sum = 0.0f;
  float rest_sum = 0.0f;
  for (int i = 0; i < count; i++)
  {
    Voxel *v = world_voxel_ptr_fast(world, xs[i], ys[i], zs[i]);
    if (!v || !voxel_fracture_is_structural(v->type) || v->type == VOXEL_BEDROCK)
      continue;

    const int lx = xs[i] - minx;
    const int ly = ys[i] - miny;
    const int lz = zs[i] - minz;
    if (lx < -127 || lx > 127 || ly < -127 || ly > 127 || lz < -127 || lz > 127)
      continue;

    DebrisVolumeCell *c = &vol->cells[written++];
    c->lx = (int8_t)lx;
    c->ly = (int8_t)ly;
    c->lz = (int8_t)lz;
    c->type = v->type;
    c->damage = voxel_get_damage(v);
    fric_sum += material_friction(v->type);
    rest_sum += material_restitution(v->type);

    world_set_voxel(world, (uint32_t)xs[i], (uint32_t)ys[i], (uint32_t)zs[i], VOXEL_AIR);
  }

  if (written <= 0)
  {
    memset(vol, 0, sizeof(*vol));
    return 0;
  }

  vol->cell_count = written;
  vol->friction = fric_sum / (float)written;
  vol->restitution = rest_sum / (float)written;
  rebuild_mass_props(vol);
  vol->active = true;
  world->voxel_revision++;
  if (out_vol)
    *out_vol = vol;
  return written;
}

void voxel_debris_volume_apply_impulse(DebrisVolume *vol, float jx, float jy, float jz, float hit_x,
                                       float hit_y, float hit_z)
{
  if (!vol || !vol->active)
    return;
  vol->vx += jx * vol->inv_mass;
  vol->vy += jy * vol->inv_mass;
  vol->vz += jz * vol->inv_mass;
  const float rx = hit_x - (vol->x + vol->com_lx);
  const float ry = hit_y - (vol->y + vol->com_ly);
  const float rz = hit_z - (vol->z + vol->com_lz);
  vol->wx += (ry * jz - rz * jy) * vol->inv_ix;
  vol->wy += (rz * jx - rx * jz) * vol->inv_iy;
  vol->wz += (rx * jy - ry * jx) * vol->inv_iz;
}

void voxel_debris_volume_cell_world(const DebrisVolume *vol, const DebrisVolumeCell *c, float *wx,
                                    float *wy, float *wz)
{
  if (!vol || !c || !wx || !wy || !wz)
    return;
  // Local offset from COM, then ZYX intrinsic rotation.
  float lx = (float)c->lx + 0.5f - vol->com_lx;
  float ly = (float)c->ly + 0.5f - vol->com_ly;
  float lz = (float)c->lz + 0.5f - vol->com_lz;

  const float cr = cosf(vol->roll), sr = sinf(vol->roll);
  float x1 = lx;
  float y1 = ly * cr - lz * sr;
  float z1 = ly * sr + lz * cr;

  const float cp = cosf(vol->pitch), sp = sinf(vol->pitch);
  float x2 = x1 * cp + z1 * sp;
  float y2 = y1;
  float z2 = -x1 * sp + z1 * cp;

  const float cy = cosf(vol->yaw), sy = sinf(vol->yaw);
  const float rx = x2 * cy - y2 * sy;
  const float ry = x2 * sy + y2 * cy;
  const float rz = z2;

  *wx = vol->x + vol->com_lx + rx - 0.5f;
  *wy = vol->y + vol->com_ly + ry - 0.5f;
  *wz = vol->z + vol->com_lz + rz - 0.5f;
}

static void cell_world_pos(const DebrisVolume *vol, const DebrisVolumeCell *c, float *wx, float *wy,
                           float *wz)
{
  voxel_debris_volume_cell_world(vol, c, wx, wy, wz);
}

static bool solid_at(World *world, int x, int y, int z)
{
  if (!world_pos_in_bounds_fast(world, x, y, z))
    return true;
  const Voxel *v = world_voxel_cptr_fast(world, x, y, z);
  return v && v->type != VOXEL_AIR && !world_voxel_type_is_fluidlike(v->type);
}

static bool volume_overlaps_world(World *world, const DebrisVolume *vol, float ox, float oy, float oz)
{
  // Temporarily shift origin for the probe without mutating yaw/COM.
  DebrisVolume probe = *vol;
  probe.x = ox;
  probe.y = oy;
  probe.z = oz;
  for (int i = 0; i < vol->cell_count; i++)
  {
    float wx, wy, wz;
    cell_world_pos(&probe, &vol->cells[i], &wx, &wy, &wz);
    const int x = (int)floorf(wx + 1e-4f);
    const int y = (int)floorf(wy + 1e-4f);
    const int z = (int)floorf(wz + 1e-4f);
    if (solid_at(world, x, y, z))
      return true;
  }
  return false;
}

static bool volume_cell_covers(const DebrisVolume *vol, int x, int y, int z)
{
  for (int i = 0; i < vol->cell_count; i++)
  {
    float wx, wy, wz;
    cell_world_pos(vol, &vol->cells[i], &wx, &wy, &wz);
    const int cx = (int)floorf(wx + 1e-4f);
    const int cy = (int)floorf(wy + 1e-4f);
    const int cz = (int)floorf(wz + 1e-4f);
    if (cx == x && cy == y && cz == z)
      return true;
  }
  return false;
}

static bool volume_supported_world(World *world, const DebrisVolume *vol)
{
  for (int i = 0; i < vol->cell_count; i++)
  {
    const DebrisVolumeCell *c = &vol->cells[i];
    float wx, wy, wz;
    cell_world_pos(vol, c, &wx, &wy, &wz);
    const int x = (int)floorf(wx + 1e-4f);
    const int y = (int)floorf(wy + 1e-4f);
    const int z = (int)floorf(wz + 1e-4f);
    if (solid_at(world, x, y, z - 1))
      return true;
  }
  return false;
}

static bool volume_supported_by_volume(const DebrisVolumeSystem *sys, int self, World *world)
{
  const DebrisVolume *vol = &sys->items[self];
  for (int i = 0; i < vol->cell_count; i++)
  {
    const DebrisVolumeCell *c = &vol->cells[i];
    float wx, wy, wz;
    cell_world_pos(vol, c, &wx, &wy, &wz);
    const int x = (int)floorf(wx + 1e-4f);
    const int y = (int)floorf(wy + 1e-4f);
    const int z = (int)floorf(wz + 1e-4f);
    for (int j = 0; j < DEBRIS_VOLUME_MAX; j++)
    {
      if (j == self || !sys->items[j].active || sys->items[j].home != world)
        continue;
      if (volume_cell_covers(&sys->items[j], x, y, z - 1))
        return true;
    }
  }
  return false;
}

static bool volume_has_volume_above(const DebrisVolumeSystem *sys, int self, World *world)
{
  const DebrisVolume *vol = &sys->items[self];
  for (int i = 0; i < vol->cell_count; i++)
  {
    const DebrisVolumeCell *c = &vol->cells[i];
    float wx, wy, wz;
    cell_world_pos(vol, c, &wx, &wy, &wz);
    const int x = (int)floorf(wx + 1e-4f);
    const int y = (int)floorf(wy + 1e-4f);
    const int z = (int)floorf(wz + 1e-4f);
    for (int j = 0; j < DEBRIS_VOLUME_MAX; j++)
    {
      if (j == self || !sys->items[j].active || sys->items[j].home != world)
        continue;
      if (volume_cell_covers(&sys->items[j], x, y, z + 1))
        return true;
    }
  }
  return false;
}

static bool try_place_cell(World *world, int x, int y, int z, VoxelType type, uint8_t damage)
{
  if (!world_pos_in_bounds_fast(world, x, y, z))
    return false;
  if (solid_at(world, x, y, z))
    return false;
  // Refuse embedding solids inside live actors (Devlog #28 place-vs-body). Relocate ring still runs.
  if (world_actor_blocks_cell(world, x, y, z))
    return false;
  world_set_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z, type);
  Voxel *dst = world_voxel_ptr_fast(world, x, y, z);
  if (dst)
  {
    voxel_set_damage(dst, damage);
    voxel_set_momentum(dst, 0.0f, 0.0f, 0.0f);
  }
  return true;
}

static void volume_writeback(DebrisVolume *vol)
{
  World *world = vol->home;
  if (!world)
  {
    vol->active = false;
    return;
  }

  for (int i = 0; i < vol->cell_count; i++)
  {
    const DebrisVolumeCell *c = &vol->cells[i];
    float wx, wy, wz;
    cell_world_pos(vol, c, &wx, &wy, &wz);
    int x = (int)floorf(wx + 0.5f);
    int y = (int)floorf(wy + 0.5f);
    int z = (int)floorf(wz + 0.5f);

    if (try_place_cell(world, x, y, z, c->type, c->damage))
      continue;
    // Prefer above, then a small XY ring, so stacked writebacks do not silently drop cells.
    bool placed = false;
    for (int dz = 1; dz <= 3 && !placed; dz++)
      placed = try_place_cell(world, x, y, z + dz, c->type, c->damage);
    for (int r = 1; r <= 2 && !placed; r++)
      for (int dy = -r; dy <= r && !placed; dy++)
        for (int dx = -r; dx <= r && !placed; dx++)
          placed = try_place_cell(world, x + dx, y + dy, z, c->type, c->damage);
  }

  world->voxel_revision++;
  vol->active = false;
}

static void volume_aabb(const DebrisVolume *vol, float *minx, float *miny, float *minz, float *maxx,
                        float *maxy, float *maxz)
{
  if (vol->cell_count <= 0)
  {
    *minx = vol->x;
    *miny = vol->y;
    *minz = vol->z;
    *maxx = vol->x + 1.0f;
    *maxy = vol->y + 1.0f;
    *maxz = vol->z + 1.0f;
    return;
  }
  float wx, wy, wz;
  cell_world_pos(vol, &vol->cells[0], &wx, &wy, &wz);
  *minx = wx;
  *miny = wy;
  *minz = wz;
  *maxx = wx + 1.0f;
  *maxy = wy + 1.0f;
  *maxz = wz + 1.0f;
  for (int i = 0; i < vol->cell_count; i++)
  {
    cell_world_pos(vol, &vol->cells[i], &wx, &wy, &wz);
    if (wx < *minx)
      *minx = wx;
    if (wy < *miny)
      *miny = wy;
    if (wz < *minz)
      *minz = wz;
    if (wx + 1.0f > *maxx)
      *maxx = wx + 1.0f;
    if (wy + 1.0f > *maxy)
      *maxy = wy + 1.0f;
    if (wz + 1.0f > *maxz)
      *maxz = wz + 1.0f;
  }
}

static bool aabb_overlap(float aminx, float aminy, float aminz, float amaxx, float amaxy, float amaxz,
                         float bminx, float bminy, float bminz, float bmaxx, float bmaxy, float bmaxz)
{
  return aminx < bmaxx && amaxx > bminx && aminy < bmaxy && amaxy > bminy && aminz < bmaxz &&
         amaxz > bminz;
}

// Unit-cube SAT with rounded-face normals (Devlog #26). Face contacts stay axis-aligned; edges and
// corners blend the two/three shallowest axes so stacked rubble does not catch on sharp cube corners.
static bool unit_cube_contact(float ax, float ay, float az, float bx, float by, float bz, float *nx,
                              float *ny, float *nz, float *pen, int *axis)
{
  const float dx = (ax + 0.5f) - (bx + 0.5f);
  const float dy = (ay + 0.5f) - (by + 0.5f);
  const float dz = (az + 0.5f) - (bz + 0.5f);
  const float ox = 1.0f - fabsf(dx);
  const float oy = 1.0f - fabsf(dy);
  const float oz = 1.0f - fabsf(dz);
  if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f)
    return false;

  const float sx = (dx < 0.0f) ? -1.0f : 1.0f;
  const float sy = (dy < 0.0f) ? -1.0f : 1.0f;
  const float sz = (dz < 0.0f) ? -1.0f : 1.0f;
  const float round_eps = 0.18f;

  // Corner: all three overlaps within epsilon of the shallowest.
  const float omin = fminf(ox, fminf(oy, oz));
  if (ox - omin < round_eps && oy - omin < round_eps && oz - omin < round_eps)
  {
    *nx = sx;
    *ny = sy;
    *nz = sz;
    const float len = sqrtf(3.0f);
    *nx /= len;
    *ny /= len;
    *nz /= len;
    *pen = omin;
    *axis = (ox <= oy && ox <= oz) ? 0 : (oy <= oz) ? 1 : 2;
    return true;
  }
  // Edge: two shallow axes within epsilon.
  if (ox <= oz && oy <= oz && fabsf(ox - oy) < round_eps)
  {
    *nx = sx;
    *ny = sy;
    *nz = 0.0f;
    const float len = sqrtf(2.0f);
    *nx /= len;
    *ny /= len;
    *pen = fminf(ox, oy);
    *axis = (ox <= oy) ? 0 : 1;
    return true;
  }
  if (ox <= oy && oz <= oy && fabsf(ox - oz) < round_eps)
  {
    *nx = sx;
    *ny = 0.0f;
    *nz = sz;
    const float len = sqrtf(2.0f);
    *nx /= len;
    *nz /= len;
    *pen = fminf(ox, oz);
    *axis = (ox <= oz) ? 0 : 2;
    return true;
  }
  if (oy <= ox && oz <= ox && fabsf(oy - oz) < round_eps)
  {
    *nx = 0.0f;
    *ny = sy;
    *nz = sz;
    const float len = sqrtf(2.0f);
    *ny /= len;
    *nz /= len;
    *pen = fminf(oy, oz);
    *axis = (oy <= oz) ? 1 : 2;
    return true;
  }

  if (ox <= oy && ox <= oz)
  {
    *nx = sx;
    *ny = 0.0f;
    *nz = 0.0f;
    *pen = ox;
    *axis = 0;
  }
  else if (oy <= oz)
  {
    *nx = 0.0f;
    *ny = sy;
    *nz = 0.0f;
    *pen = oy;
    *axis = 1;
  }
  else
  {
    *nx = 0.0f;
    *ny = 0.0f;
    *nz = sz;
    *pen = oz;
    *axis = 2;
  }
  return true;
}

static float find_warm_impulse(const DebrisVolumeSystem *sys, uint8_t a, uint8_t b, int axis)
{
  for (int i = 0; i < sys->contact_count; i++)
  {
    const DebrisVolumeContact *c = &sys->contacts[i];
    if (!c->live)
      continue;
    if (((c->a == a && c->b == b) || (c->a == b && c->b == a)) && c->axis == axis)
      return c->impulse_n;
  }
  return 0.0f;
}

static float find_warm_friction(const DebrisVolumeSystem *sys, uint8_t a, uint8_t b, int axis)
{
  for (int i = 0; i < sys->contact_count; i++)
  {
    const DebrisVolumeContact *c = &sys->contacts[i];
    if (!c->live)
      continue;
    if (((c->a == a && c->b == b) || (c->a == b && c->b == a)) && c->axis == axis)
      return c->impulse_t;
  }
  return 0.0f;
}

// Spatial hash for unit-cube cell pairs inside an overlapping AABB pair. Avoids O(Na·Nb) when
// two large volumes barely graze each other.
#define CONTACT_HASH_BUCKETS 512
#define CONTACT_HASH_ENTRIES DEBRIS_VOLUME_CELL_CAP

typedef struct
{
  int16_t next;
  int16_t cell;
  int16_t gx, gy, gz;
  float wx, wy, wz;
} ContactHashEntry;

static int16_t s_contact_hash_head[CONTACT_HASH_BUCKETS];
static ContactHashEntry s_contact_hash_ents[CONTACT_HASH_ENTRIES];
static int s_contact_hash_n;

static uint32_t contact_hash_key(int gx, int gy, int gz)
{
  const uint32_t h = (uint32_t)gx * 73856093u ^ (uint32_t)gy * 19349663u ^ (uint32_t)gz * 83492791u;
  return h & (CONTACT_HASH_BUCKETS - 1u);
}

static void contact_hash_clear(void)
{
  for (int i = 0; i < CONTACT_HASH_BUCKETS; i++)
    s_contact_hash_head[i] = -1;
  s_contact_hash_n = 0;
}

static void contact_hash_insert(int cell_i, float wx, float wy, float wz)
{
  if (s_contact_hash_n >= CONTACT_HASH_ENTRIES)
    return;
  const int gx = (int)floorf(wx);
  const int gy = (int)floorf(wy);
  const int gz = (int)floorf(wz);
  const uint32_t bucket = contact_hash_key(gx, gy, gz);
  const int slot = s_contact_hash_n++;
  s_contact_hash_ents[slot].next = s_contact_hash_head[bucket];
  s_contact_hash_ents[slot].cell = (int16_t)cell_i;
  s_contact_hash_ents[slot].gx = (int16_t)gx;
  s_contact_hash_ents[slot].gy = (int16_t)gy;
  s_contact_hash_ents[slot].gz = (int16_t)gz;
  s_contact_hash_ents[slot].wx = wx;
  s_contact_hash_ents[slot].wy = wy;
  s_contact_hash_ents[slot].wz = wz;
  s_contact_hash_head[bucket] = (int16_t)slot;
}

static void collect_volume_contacts(DebrisVolumeSystem *sys, World *world)
{
  DebrisVolumeContact fresh[DEBRIS_VOLUME_CONTACT_CAP];
  int fresh_n = 0;

  float aabb[DEBRIS_VOLUME_MAX][6];
  bool has_aabb[DEBRIS_VOLUME_MAX];
  memset(has_aabb, 0, sizeof(has_aabb));

  for (int i = 0; i < DEBRIS_VOLUME_MAX; i++)
  {
    if (!sys->items[i].active || sys->items[i].home != world)
      continue;
    if (sys->items[i].cell_count <= 0)
      continue;
    volume_aabb(&sys->items[i], &aabb[i][0], &aabb[i][1], &aabb[i][2], &aabb[i][3], &aabb[i][4],
                &aabb[i][5]);
    has_aabb[i] = true;
  }

  for (int a = 0; a < DEBRIS_VOLUME_MAX; a++)
  {
    if (!has_aabb[a])
      continue;
    for (int b = a + 1; b < DEBRIS_VOLUME_MAX; b++)
    {
      if (!has_aabb[b])
        continue;
      if (!aabb_overlap(aabb[a][0], aabb[a][1], aabb[a][2], aabb[a][3], aabb[a][4], aabb[a][5],
                        aabb[b][0], aabb[b][1], aabb[b][2], aabb[b][3], aabb[b][4], aabb[b][5]))
        continue;

      const DebrisVolume *va = &sys->items[a];
      const DebrisVolume *vb = &sys->items[b];
      // Hash the denser volume; probe with the other.
      const bool hash_b = vb->cell_count >= va->cell_count;
      const DebrisVolume *hashed = hash_b ? vb : va;
      const DebrisVolume *probe = hash_b ? va : vb;
      contact_hash_clear();
      for (int i = 0; i < hashed->cell_count; i++)
      {
        float wx, wy, wz;
        cell_world_pos(hashed, &hashed->cells[i], &wx, &wy, &wz);
        contact_hash_insert(i, wx, wy, wz);
      }

      // One warm-started contact per separating axis between this pair.
      uint8_t axes_mask = 0;
      for (int ip = 0; ip < probe->cell_count && fresh_n < DEBRIS_VOLUME_CONTACT_CAP; ip++)
      {
        if (axes_mask == 0x7)
          break;
        float px, py, pz;
        cell_world_pos(probe, &probe->cells[ip], &px, &py, &pz);
        const int pgx = (int)floorf(px);
        const int pgy = (int)floorf(py);
        const int pgz = (int)floorf(pz);
        for (int dz = -1; dz <= 1 && fresh_n < DEBRIS_VOLUME_CONTACT_CAP; dz++)
        {
          for (int dy = -1; dy <= 1 && fresh_n < DEBRIS_VOLUME_CONTACT_CAP; dy++)
          {
            for (int dx = -1; dx <= 1 && fresh_n < DEBRIS_VOLUME_CONTACT_CAP; dx++)
            {
              const uint32_t bucket = contact_hash_key(pgx + dx, pgy + dy, pgz + dz);
              for (int16_t slot = s_contact_hash_head[bucket]; slot >= 0;
                   slot = s_contact_hash_ents[slot].next)
              {
                const ContactHashEntry *e = &s_contact_hash_ents[slot];
                if (e->gx != (int16_t)(pgx + dx) || e->gy != (int16_t)(pgy + dy) ||
                    e->gz != (int16_t)(pgz + dz))
                  continue;
                float nx, ny, nz, pen;
                int axis = 0;
                // unit_cube_contact expects (a,b) in volume-index order for normal sign consistency.
                const float ax = hash_b ? px : e->wx;
                const float ay = hash_b ? py : e->wy;
                const float az = hash_b ? pz : e->wz;
                const float bx = hash_b ? e->wx : px;
                const float by = hash_b ? e->wy : py;
                const float bz = hash_b ? e->wz : pz;
                if (!unit_cube_contact(ax, ay, az, bx, by, bz, &nx, &ny, &nz, &pen, &axis))
                  continue;
                if (axes_mask & (uint8_t)(1u << axis))
                  continue;
                axes_mask |= (uint8_t)(1u << axis);

                DebrisVolumeContact *c = &fresh[fresh_n++];
                c->a = (uint8_t)a;
                c->b = (uint8_t)b;
                c->axis = (int8_t)axis;
                c->nx = nx;
                c->ny = ny;
                c->nz = nz;
                c->impulse_n = find_warm_impulse(sys, (uint8_t)a, (uint8_t)b, axis);
                c->impulse_t = find_warm_friction(sys, (uint8_t)a, (uint8_t)b, axis);
                c->live = true;
                (void)pen;
                if (axes_mask == 0x7)
                  goto pair_done;
              }
            }
          }
        }
      }
    pair_done:;
    }
  }

  sys->contact_count = fresh_n;
  memcpy(sys->contacts, fresh, (size_t)fresh_n * sizeof(DebrisVolumeContact));
}

static void solve_volume_contacts(DebrisVolumeSystem *sys, World *world, float dt)
{
  if (dt < 1e-6f)
    dt = 1e-6f;
  const float baumgarte = 0.2f;

  float aabb_min[DEBRIS_VOLUME_MAX][3];
  float aabb_max[DEBRIS_VOLUME_MAX][3];
  bool aabb_ok[DEBRIS_VOLUME_MAX];
  for (int vi = 0; vi < DEBRIS_VOLUME_MAX; vi++)
  {
    DebrisVolume *v = &sys->items[vi];
    if (!v->active || v->home != world)
    {
      aabb_ok[vi] = false;
      continue;
    }
    volume_aabb(v, &aabb_min[vi][0], &aabb_min[vi][1], &aabb_min[vi][2], &aabb_max[vi][0],
                &aabb_max[vi][1], &aabb_max[vi][2]);
    aabb_ok[vi] = true;
  }

  for (int iter = 0; iter < DEBRIS_VOLUME_TGS_ITERS; iter++)
  {
    for (int i = 0; i < sys->contact_count; i++)
    {
      DebrisVolumeContact *c = &sys->contacts[i];
      if (!c->live)
        continue;
      DebrisVolume *va = &sys->items[c->a];
      DebrisVolume *vb = &sys->items[c->b];
      if (!va->active || !vb->active || va->home != world || vb->home != world || !aabb_ok[c->a] ||
          !aabb_ok[c->b])
      {
        c->live = false;
        continue;
      }

      // Penetration from poses at solve start (TGS only updates velocities here).
      float pen = 0.0f;
      const float *amin = aabb_min[c->a];
      const float *amax = aabb_max[c->a];
      const float *bmin = aabb_min[c->b];
      const float *bmax = aabb_max[c->b];
      const float overlap[3] = {
          fminf(amax[0], bmax[0]) - fmaxf(amin[0], bmin[0]),
          fminf(amax[1], bmax[1]) - fmaxf(amin[1], bmin[1]),
          fminf(amax[2], bmax[2]) - fmaxf(amin[2], bmin[2]),
      };
      if (overlap[0] <= 0.0f || overlap[1] <= 0.0f || overlap[2] <= 0.0f)
      {
        c->live = false;
        continue;
      }
      pen = (c->axis == 0) ? overlap[0] : (c->axis == 1) ? overlap[1] : overlap[2];
      if (c->axis == 0)
      {
        const float ac = 0.5f * (amin[0] + amax[0]);
        const float bc = 0.5f * (bmin[0] + bmax[0]);
        c->nx = (ac >= bc) ? 1.0f : -1.0f;
        c->ny = 0.0f;
        c->nz = 0.0f;
      }
      else if (c->axis == 1)
      {
        const float ac = 0.5f * (amin[1] + amax[1]);
        const float bc = 0.5f * (bmin[1] + bmax[1]);
        c->nx = 0.0f;
        c->ny = (ac >= bc) ? 1.0f : -1.0f;
        c->nz = 0.0f;
      }
      else
      {
        const float ac = 0.5f * (amin[2] + amax[2]);
        const float bc = 0.5f * (bmin[2] + bmax[2]);
        c->nx = 0.0f;
        c->ny = 0.0f;
        c->nz = (ac >= bc) ? 1.0f : -1.0f;
      }

      const float rel_v =
          (va->vx - vb->vx) * c->nx + (va->vy - vb->vy) * c->ny + (va->vz - vb->vz) * c->nz;
      const float bias = -(baumgarte * pen) / dt;
      const float inv_m = va->inv_mass + vb->inv_mass;
      if (inv_m < 1e-8f)
        continue;

      // Mild restitution on hard impacts only so stacks still settle (Devlog #26 materials).
      float bounce = 0.0f;
      if (rel_v < -0.5f)
      {
        const float e = 0.5f * (va->restitution + vb->restitution);
        bounce = e * (-rel_v);
      }
      float lambda = -(rel_v + bias - bounce) / inv_m;
      const float old = c->impulse_n;
      c->impulse_n = fmaxf(0.0f, old + lambda);
      lambda = c->impulse_n - old;

      va->vx += lambda * c->nx * va->inv_mass;
      va->vy += lambda * c->ny * va->inv_mass;
      va->vz += lambda * c->nz * va->inv_mass;
      vb->vx -= lambda * c->nx * vb->inv_mass;
      vb->vy -= lambda * c->ny * vb->inv_mass;
      vb->vz -= lambda * c->nz * vb->inv_mass;

      // Coulomb friction in the contact plane. Tangential impulse also applies multi-axis torque
      // about each volume's COM (τ = r × J).
      if (c->impulse_n > 0.0f)
      {
        const float rax = 0.5f * (amin[0] + amax[0]) - (va->x + va->com_lx);
        const float ray = 0.5f * (amin[1] + amax[1]) - (va->y + va->com_ly);
        const float raz = 0.5f * (amin[2] + amax[2]) - (va->z + va->com_lz);
        const float rbx = 0.5f * (bmin[0] + bmax[0]) - (vb->x + vb->com_lx);
        const float rby = 0.5f * (bmin[1] + bmax[1]) - (vb->y + vb->com_ly);
        const float rbz = 0.5f * (bmin[2] + bmax[2]) - (vb->z + vb->com_lz);

        const float avx = va->vx + (va->wy * raz - va->wz * ray);
        const float avy = va->vy + (va->wz * rax - va->wx * raz);
        const float avz = va->vz + (va->wx * ray - va->wy * rax);
        const float bvx = vb->vx + (vb->wy * rbz - vb->wz * rby);
        const float bvy = vb->vy + (vb->wz * rbx - vb->wx * rbz);
        const float bvz = vb->vz + (vb->wx * rby - vb->wy * rbx);
        float rvx = avx - bvx;
        float rvy = avy - bvy;
        float rvz = avz - bvz;
        const float rn = rvx * c->nx + rvy * c->ny + rvz * c->nz;
        rvx -= rn * c->nx;
        rvy -= rn * c->ny;
        rvz -= rn * c->nz;
        const float tspeed = sqrtf(rvx * rvx + rvy * rvy + rvz * rvz);
        if (tspeed > 1e-5f)
        {
          const float ux = rvx / tspeed;
          const float uy = rvy / tspeed;
          const float uz = rvz / tspeed;
          float lambda_t = -tspeed / inv_m;
          float mu = 0.5f * (va->friction + vb->friction);
          if (mu < 0.05f)
            mu = DEBRIS_VOLUME_FRICTION;
          const float max_f = mu * c->impulse_n;
          const float old_t = c->impulse_t;
          c->impulse_t = fmaxf(-max_f, fminf(max_f, old_t + lambda_t));
          lambda_t = c->impulse_t - old_t;

          const float fx = lambda_t * ux;
          const float fy = lambda_t * uy;
          const float fz = lambda_t * uz;
          va->vx += fx * va->inv_mass;
          va->vy += fy * va->inv_mass;
          va->vz += fz * va->inv_mass;
          vb->vx -= fx * vb->inv_mass;
          vb->vy -= fy * vb->inv_mass;
          vb->vz -= fz * vb->inv_mass;

          va->wx += (ray * fz - raz * fy) * va->inv_ix;
          va->wy += (raz * fx - rax * fz) * va->inv_iy;
          va->wz += (rax * fy - ray * fx) * va->inv_iz;
          vb->wx -= (rby * fz - rbz * fy) * vb->inv_ix;
          vb->wy -= (rbz * fx - rbx * fz) * vb->inv_iy;
          vb->wz -= (rbx * fy - rby * fx) * vb->inv_iz;
        }
      }
    }
  }
}

// Average world-space centre of cells that have a solid neighbour along (sx,sy,sz).
static int contact_centroid(World *world, const DebrisVolume *vol, int sx, int sy, int sz,
                            float *ox, float *oy, float *oz)
{
  float sx_sum = 0.0f, sy_sum = 0.0f, sz_sum = 0.0f;
  int n = 0;
  for (int i = 0; i < vol->cell_count; i++)
  {
    float wx, wy, wz;
    cell_world_pos(vol, &vol->cells[i], &wx, &wy, &wz);
    const int x = (int)floorf(wx + 1e-4f);
    const int y = (int)floorf(wy + 1e-4f);
    const int z = (int)floorf(wz + 1e-4f);
    if (!solid_at(world, x + sx, y + sy, z + sz))
      continue;
    sx_sum += wx + 0.5f;
    sy_sum += wy + 0.5f;
    sz_sum += wz + 0.5f;
    n++;
  }
  if (n <= 0)
    return 0;
  *ox = sx_sum / (float)n;
  *oy = sy_sum / (float)n;
  *oz = sz_sum / (float)n;
  return n;
}

// Apply torque from stopping linear velocity along one axis at a world contact centroid.
static void torque_from_axis_stop(DebrisVolume *vol, float hit_x, float hit_y, float hit_z,
                                  int axis, float v_along)
{
  const float comx = vol->x + vol->com_lx;
  const float comy = vol->y + vol->com_ly;
  const float comz = vol->z + vol->com_lz;
  const float rx = hit_x - comx;
  const float ry = hit_y - comy;
  const float rz = hit_z - comz;
  // Impulse that cancels this axis of COM velocity (mass = 1/inv_mass).
  const float j = -v_along / vol->inv_mass;
  const float jx = (axis == 0) ? j : 0.0f;
  const float jy = (axis == 1) ? j : 0.0f;
  const float jz = (axis == 2) ? j : 0.0f;
  vol->wx += (ry * jz - rz * jy) * vol->inv_ix;
  vol->wy += (rz * jx - rx * jz) * vol->inv_iy;
  vol->wz += (rx * jy - ry * jx) * vol->inv_iz;
}

static void try_orient(DebrisVolume *vol, World *world, float *angle, float new_angle, float *w)
{
  const float old = *angle;
  *angle = new_angle;
  if (volume_overlaps_world(world, vol, vol->x, vol->y, vol->z))
  {
    *angle = old;
    *w *= 0.35f;
  }
}

static void integrate_vs_world(DebrisVolume *vol, World *world, float dt, DebrisPendingImpact *impact)
{
  if (impact)
  {
    impact->speed = 0.0f;
    impact->hx = vol->x + vol->com_lx;
    impact->hy = vol->y + vol->com_ly;
    impact->hz = vol->z + vol->com_lz;
  }

  const float nx = vol->x + vol->vx * dt;
  const float ny = vol->y + vol->vy * dt;
  const float nz = vol->z + vol->vz * dt;

  // Integrate yaw / pitch / roll independently so a blocked pitch does not kill yaw tumble.
  try_orient(vol, world, &vol->yaw, vol->yaw + vol->wz * dt, &vol->wz);
  try_orient(vol, world, &vol->pitch, vol->pitch + vol->wy * dt, &vol->wy);
  try_orient(vol, world, &vol->roll, vol->roll + vol->wx * dt, &vol->wx);

  if (!volume_overlaps_world(world, vol, vol->x, vol->y, nz))
    vol->z = nz;
  else
  {
    const float impact_vz = vol->vz;
    float hx = vol->x + vol->com_lx;
    float hy = vol->y + vol->com_ly;
    float hz = vol->z;
    const int n_contact = contact_centroid(world, vol, 0, 0, -1, &hx, &hy, &hz);
    // Only hard landings inject tumble torque — resting frames still accrue ~g·dt of vz.
    if (n_contact > 0 && impact_vz < -1.5f)
      torque_from_axis_stop(vol, hx, hy, hz, 2, impact_vz);
    if (impact && impact_vz < -1.5f)
    {
      const float s = -impact_vz;
      if (s > impact->speed)
      {
        impact->speed = s;
        impact->hx = hx;
        impact->hy = hy;
        impact->hz = hz;
      }
    }

    float bounce = 0.0f;
    if (impact_vz < -3.0f && vol->restitution > 0.05f)
    {
      bounce = -impact_vz * vol->restitution;
      if (bounce > 1.25f)
        bounce = 1.25f;
    }
    vol->vz = bounce;

    float probe = vol->z;
    for (int s = 0; s < 8; s++)
    {
      const float try_z = probe - 0.125f;
      if (volume_overlaps_world(world, vol, vol->x, vol->y, try_z))
        break;
      probe = try_z;
    }
    vol->z = probe;

    // Ground friction: heavier piles scrub horizontal speed faster (μ scaled by mass).
    const float hspeed = sqrtf(vol->vx * vol->vx + vol->vy * vol->vy);
    if (hspeed > 1e-4f)
    {
      const float ux = vol->vx / hspeed;
      const float uy = vol->vy / hspeed;
      float mu = vol->friction > 0.05f ? vol->friction : DEBRIS_VOLUME_FRICTION;
      const float heavy = volume_mass_scale(vol);
      const float dv = fminf(hspeed, mu * (3.0f + 2.8f * heavy));
      vol->vx -= ux * dv;
      vol->vy -= uy * dv;
      const float mass = 1.0f / vol->inv_mass;
      const float fx = -ux * dv * mass;
      const float fy = -uy * dv * mass;
      const float comx = vol->x + vol->com_lx;
      const float comy = vol->y + vol->com_ly;
      const float comz = vol->z + vol->com_lz;
      float rx = hx - comx;
      float ry = hy - comy;
      float rz = hz - comz;
      if (n_contact <= 0)
      {
        // Fallback: upright base under COM (preserves tip tests when no neighbour sample).
        rx = 0.0f;
        ry = 0.0f;
        rz = -vol->com_lz;
      }
      (void)comz;
      vol->wx += (ry * 0.0f - rz * fy) * vol->inv_ix;
      vol->wy += (rz * fx - rx * 0.0f) * vol->inv_iy;
      vol->wz += (rx * fy - ry * fx) * vol->inv_iz;
    }

    // Gravity tip when support is offset from the COM (overhang / already tilted).
    if (n_contact > 0)
    {
      const float comx = vol->x + vol->com_lx;
      const float comy = vol->y + vol->com_ly;
      const float ox = comx - hx;
      const float oy = comy - hy;
      const float o2 = ox * ox + oy * oy;
      const bool already_moving =
          hspeed > 0.15f || fabsf(vol->wx) > 0.08f || fabsf(vol->wy) > 0.08f;
      if (o2 > 0.04f && already_moving)
      {
        const float mass = 1.0f / vol->inv_mass;
        const float tip = 5.0f * mass * dt;
        vol->wx += oy * tip * vol->inv_ix;
        vol->wy += -ox * tip * vol->inv_iy;
      }
    }

    // Settle: once on the ground and slow, kill residual tumble so writeback can fire.
    {
      const float speed = fabsf(vol->vx) + fabsf(vol->vy) + fabsf(vol->vz);
      if (speed < 0.35f)
      {
        vol->wx *= 0.78f;
        vol->wy *= 0.78f;
        vol->wz *= 0.72f;
        if (speed < 0.12f)
        {
          if (fabsf(vol->wx) < 0.18f)
            vol->wx = 0.0f;
          if (fabsf(vol->wy) < 0.18f)
            vol->wy = 0.0f;
          if (fabsf(vol->wz) < 0.18f)
            vol->wz = 0.0f;
        }
      }
      else
      {
        vol->wx *= 0.92f;
        vol->wy *= 0.92f;
        vol->wz *= 0.85f;
      }
    }
  }

  if (!volume_overlaps_world(world, vol, nx, vol->y, vol->z))
    vol->x = nx;
  else
  {
    const float impact_vx = vol->vx;
    const int sx = (impact_vx >= 0.0f) ? 1 : -1;
    float hx = vol->x + vol->com_lx;
    float hy = vol->y + vol->com_ly;
    float hz = vol->z + vol->com_lz;
    if (fabsf(impact_vx) > 0.4f && contact_centroid(world, vol, sx, 0, 0, &hx, &hy, &hz) > 0)
      torque_from_axis_stop(vol, hx, hy, hz, 0, impact_vx);
    if (impact && fabsf(impact_vx) > 0.4f)
    {
      const float s = fabsf(impact_vx);
      if (s > impact->speed)
      {
        impact->speed = s;
        impact->hx = hx;
        impact->hy = hy;
        impact->hz = hz;
      }
    }
    vol->vx = 0.0f;
  }

  if (!volume_overlaps_world(world, vol, vol->x, ny, vol->z))
    vol->y = ny;
  else
  {
    const float impact_vy = vol->vy;
    const int sy = (impact_vy >= 0.0f) ? 1 : -1;
    float hx = vol->x + vol->com_lx;
    float hy = vol->y + vol->com_ly;
    float hz = vol->z + vol->com_lz;
    if (fabsf(impact_vy) > 0.4f && contact_centroid(world, vol, 0, sy, 0, &hx, &hy, &hz) > 0)
      torque_from_axis_stop(vol, hx, hy, hz, 1, impact_vy);
    if (impact && fabsf(impact_vy) > 0.4f)
    {
      const float s = fabsf(impact_vy);
      if (s > impact->speed)
      {
        impact->speed = s;
        impact->hx = hx;
        impact->hy = hy;
        impact->hz = hz;
      }
    }
    vol->vy = 0.0f;
  }
}

static uint32_t shatter_hash(uint32_t seed, int x, int y, int z)
{
  uint32_t h = seed ^ (uint32_t)(x * 374761393 + y * 668265263 + z * 2147483647);
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return h;
}

static float shatter_speed_threshold(const DebrisVolume *vol)
{
  // Fragile materials break on gentler hits; stone needs a hard slam; foliage almost never.
  if (vol->restitution >= 0.20f)
    return 6.5f; // glass / crystal / ice
  if (vol->friction >= 0.55f)
    return 22.0f; // leaves / bush dominate
  if (vol->friction <= 0.22f)
    return 7.5f; // ice-like average
  return 15.0f; // stone / wood
}

static bool local_stamp_deletes(int dx, int dy, int dz, float dir_x, float dir_y, float dir_z,
                                float speed_vox_s, uint32_t seed, int icx, int icy, int icz,
                                VoxelType type)
{
  if (!voxel_is_destructible(type))
    return false;

  int radius = 1;
  if (speed_vox_s >= 18.0f)
    radius = 2;
  if (speed_vox_s >= 28.0f)
    radius = 3;
  if (radius > FRACTURE_STAMP_RADIUS_MAX)
    radius = FRACTURE_STAMP_RADIUS_MAX;

  const float fx = (float)dx + 0.5f;
  const float fy = (float)dy + 0.5f;
  const float fz = (float)dz + 0.5f;
  const float r2 = fx * fx + fy * fy + fz * fz;
  const float rad = (float)radius + 0.75f;
  if (r2 > rad * rad)
    return false;

  float len = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
  if (len < 1e-5f)
  {
    dir_x = 0.0f;
    dir_y = 0.0f;
    dir_z = 1.0f;
    len = 1.0f;
  }
  dir_x /= len;
  dir_y /= len;
  dir_z /= len;

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

  const float along = fabsf(fx * dir_x + fy * dir_y + fz * dir_z);
  const float across_p = fabsf(fx * px + fy * py + fz * pz);
  const float across_q = fabsf(fx * qx + fy * qy + fz * qz);
  const float plane = fminf(along, fminf(across_p, across_q));

  const int nfeat = (speed_vox_s >= 24.0f) ? 6 : 4;
  float f1 = 1e9f;
  for (int i = 0; i < nfeat; i++)
  {
    const uint32_t h = shatter_hash(seed ^ (uint32_t)(i * 0x9e3779b9u), icx, icy, icz);
    const float u = ((float)(h & 0xFFu) / 255.0f) * 2.0f - 1.0f;
    const float v = ((float)((h >> 8) & 0xFFu) / 255.0f) * 2.0f - 1.0f;
    const float w = ((float)((h >> 16) & 0xFFu) / 255.0f) * 2.0f - 1.0f;
    const float scale = (float)radius * 0.85f;
    const float ex = fx - u * scale;
    const float ey = fy - v * scale;
    const float ez = fz - w * scale;
    const float d2 = ex * ex + ey * ey + ez * ez;
    if (d2 < f1)
      f1 = d2;
  }
  const float worley = sqrtf(f1) / rad;

  float mass = voxel_type_mass_kg(type);
  if (mass < 1.0f)
    mass = 1.0f;
  if (mass > 40.0f)
    mass = 40.0f;
  // Slightly more aggressive than world stamps so secondary hits visibly shard.
  const float chunk_thresh = 0.28f + 0.22f * (1.0f - mass / 40.0f);

  const bool impact = (dx == 0 && dy == 0 && dz == 0);
  const bool cut = plane < 0.48f;
  const bool chunk = worley < chunk_thresh;
  return impact || cut || chunk;
}

static int shatter_uf_find(int *parent, int i)
{
  while (parent[i] != i)
  {
    parent[i] = parent[parent[i]];
    i = parent[i];
  }
  return i;
}

// Stamp the volume in local cell space, drop deleted cells, and split survivors into connected
// debris volumes. Returns how many cells were removed from the source volume.
static int shatter_debris_volume(DebrisVolumeSystem *sys, DebrisVolume *vol, float speed,
                                 float hit_x, float hit_y, float hit_z, uint32_t seed)
{
  if (!sys || !vol || !vol->active || vol->shatter_cool > 0)
    return 0;
  if (vol->cell_count < DEBRIS_SHATTER_MIN_CELLS)
    return 0;
  if (speed < shatter_speed_threshold(vol))
    return 0;

  int best = 0;
  float best_d = 1e30f;
  for (int i = 0; i < vol->cell_count; i++)
  {
    float wx, wy, wz;
    cell_world_pos(vol, &vol->cells[i], &wx, &wy, &wz);
    const float dx = (wx + 0.5f) - hit_x;
    const float dy = (wy + 0.5f) - hit_y;
    const float dz = (wz + 0.5f) - hit_z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < best_d)
    {
      best_d = d2;
      best = i;
    }
  }

  const int icx = vol->cells[best].lx;
  const int icy = vol->cells[best].ly;
  const int icz = vol->cells[best].lz;

  // Impact direction: from hit toward COM (volume is "above" the contact for ground hits).
  float dir_x = (vol->x + vol->com_lx) - hit_x;
  float dir_y = (vol->y + vol->com_ly) - hit_y;
  float dir_z = (vol->z + vol->com_lz) - hit_z;
  if (dir_x * dir_x + dir_y * dir_y + dir_z * dir_z < 1e-6f)
  {
    dir_x = 0.0f;
    dir_y = 0.0f;
    dir_z = 1.0f;
  }

  uint8_t keep[DEBRIS_VOLUME_CELL_CAP];
  int deleted = 0;
  for (int i = 0; i < vol->cell_count; i++)
  {
    const DebrisVolumeCell *c = &vol->cells[i];
    const int dx = (int)c->lx - icx;
    const int dy = (int)c->ly - icy;
    const int dz = (int)c->lz - icz;
    if (local_stamp_deletes(dx, dy, dz, dir_x, dir_y, dir_z, speed, seed, icx, icy, icz, c->type))
    {
      // Coat scrapes stay as a thinned type inside the volume when break leaves a host.
      const VoxelType left = voxel_type_after_break(c->type);
      if (left != VOXEL_AIR && left != c->type)
      {
        vol->cells[i].type = left;
        vol->cells[i].damage = 0;
        keep[i] = 1;
      }
      else
      {
        keep[i] = 0;
        deleted++;
      }
    }
    else
    {
      keep[i] = 1;
    }
  }
  if (deleted <= 0)
    return 0;

  DebrisVolumeCell survivors[DEBRIS_VOLUME_CELL_CAP];
  int nsurv = 0;
  for (int i = 0; i < vol->cell_count; i++)
    if (keep[i])
      survivors[nsurv++] = vol->cells[i];

  vol->shatter_cool = DEBRIS_SHATTER_COOL_FRAMES;
  vol->vx *= 0.55f;
  vol->vy *= 0.55f;
  vol->vz *= 0.35f;
  vol->wx *= 0.6f;
  vol->wy *= 0.6f;
  vol->wz *= 0.6f;
  vol->rest_ticks = 0;

  if (nsurv <= 0)
  {
    vol->active = false;
    vol->cell_count = 0;
    return deleted;
  }

  int parent[DEBRIS_VOLUME_CELL_CAP];
  int size[DEBRIS_VOLUME_CELL_CAP];
  for (int i = 0; i < nsurv; i++)
  {
    parent[i] = i;
    size[i] = 1;
  }
  for (int i = 0; i < nsurv; i++)
    for (int j = i + 1; j < nsurv; j++)
    {
      const int adx = abs((int)survivors[i].lx - (int)survivors[j].lx);
      const int ady = abs((int)survivors[i].ly - (int)survivors[j].ly);
      const int adz = abs((int)survivors[i].lz - (int)survivors[j].lz);
      if (adx + ady + adz != 1)
        continue;
      int a = shatter_uf_find(parent, i);
      int b = shatter_uf_find(parent, j);
      if (a == b)
        continue;
      if (size[a] < size[b])
      {
        int t = a;
        a = b;
        b = t;
      }
      parent[b] = a;
      size[a] += size[b];
    }

  int root_of[DEBRIS_VOLUME_CELL_CAP];
  int nroots = 0;
  int roots[DEBRIS_VOLUME_CELL_CAP];
  int root_size[DEBRIS_VOLUME_CELL_CAP];
  for (int i = 0; i < nsurv; i++)
  {
    const int r = shatter_uf_find(parent, i);
    root_of[i] = r;
    int found = -1;
    for (int k = 0; k < nroots; k++)
      if (roots[k] == r)
      {
        found = k;
        break;
      }
    if (found < 0)
    {
      roots[nroots] = r;
      root_size[nroots] = 0;
      found = nroots++;
    }
    root_size[found]++;
  }

  int best_root = 0;
  for (int k = 1; k < nroots; k++)
    if (root_size[k] > root_size[best_root])
      best_root = k;

  // Rebuild the source volume as the largest connected remnant.
  {
    int written = 0;
    float fric_sum = 0.0f, rest_sum = 0.0f;
    for (int i = 0; i < nsurv; i++)
      if (root_of[i] == roots[best_root])
      {
        vol->cells[written++] = survivors[i];
        fric_sum += material_friction(survivors[i].type);
        rest_sum += material_restitution(survivors[i].type);
      }
    vol->cell_count = written;
    if (written > 0)
    {
      vol->friction = fric_sum / (float)written;
      vol->restitution = rest_sum / (float)written;
      rebuild_mass_props(vol);
    }
    else
    {
      vol->active = false;
    }
  }

  // Spawn a new volume per remaining component (pool permitting).
  for (int k = 0; k < nroots; k++)
  {
    if (k == best_root)
      continue;
    DebrisVolume *child = alloc_volume(sys);
    if (!child)
      break;
    memset(child, 0, sizeof(*child));
    child->home = vol->home;
    child->x = vol->x;
    child->y = vol->y;
    child->z = vol->z;
    child->yaw = vol->yaw;
    child->pitch = vol->pitch;
    child->roll = vol->roll;
    child->vx = vol->vx;
    child->vy = vol->vy;
    child->vz = vol->vz;
    child->wx = vol->wx;
    child->wy = vol->wy;
    child->wz = vol->wz;
    child->shatter_cool = DEBRIS_SHATTER_COOL_FRAMES;

    int written = 0;
    float fric_sum = 0.0f, rest_sum = 0.0f;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    for (int i = 0; i < nsurv; i++)
    {
      if (root_of[i] != roots[k])
        continue;
      child->cells[written] = survivors[i];
      fric_sum += material_friction(survivors[i].type);
      rest_sum += material_restitution(survivors[i].type);
      sx += (float)survivors[i].lx + 0.5f;
      sy += (float)survivors[i].ly + 0.5f;
      sz += (float)survivors[i].lz + 0.5f;
      written++;
    }
    if (written <= 0)
    {
      memset(child, 0, sizeof(*child));
      continue;
    }
    child->cell_count = written;
    child->friction = fric_sum / (float)written;
    child->restitution = rest_sum / (float)written;
    rebuild_mass_props(child);
    child->active = true;

    // Kick shard away from the impact so pieces separate visibly.
    const float scx = sx / (float)written;
    const float scy = sy / (float)written;
    const float scz = sz / (float)written;
    float ox = scx - ((float)icx + 0.5f);
    float oy = scy - ((float)icy + 0.5f);
    float oz = scz - ((float)icz + 0.5f);
    float olen = sqrtf(ox * ox + oy * oy + oz * oz);
    if (olen < 1e-3f)
    {
      ox = 0.15f * (float)((k % 3) - 1);
      oy = 0.15f * (float)(((k / 3) % 3) - 1);
      oz = 0.35f;
      olen = sqrtf(ox * ox + oy * oy + oz * oz);
    }
    ox /= olen;
    oy /= olen;
    oz /= olen;
    const float kick = fminf(speed * 0.25f, 6.0f);
    voxel_debris_volume_apply_impulse(child, ox * kick, oy * kick, oz * kick + 1.2f,
                                      hit_x, hit_y, hit_z);
  }

  return deleted;
}

static void queue_impact(DebrisPendingImpact *slots, int *n, int cap, int vol_index,
                         float speed, float hx, float hy, float hz, int *indices)
{
  if (speed <= 0.0f || *n >= cap)
    return;
  // Keep the hardest hit per volume index.
  for (int i = 0; i < *n; i++)
    if (indices[i] == vol_index)
    {
      if (speed > slots[i].speed)
      {
        slots[i].speed = speed;
        slots[i].hx = hx;
        slots[i].hy = hy;
        slots[i].hz = hz;
      }
      return;
    }
  indices[*n] = vol_index;
  slots[*n].speed = speed;
  slots[*n].hx = hx;
  slots[*n].hy = hy;
  slots[*n].hz = hz;
  (*n)++;
}

void voxel_debris_volume_step(DebrisVolumeSystem *sys, World *world, float dt, float gravity)
{
  if (!sys || !world || dt <= 0.0f)
    return;
  if (gravity < 0.0f)
    gravity = 0.0f;

  DebrisPendingImpact pending[DEBRIS_VOLUME_MAX];
  int pending_idx[DEBRIS_VOLUME_MAX];
  int pending_n = 0;

  // 1. Forces + shatter cooldown tick
  for (int i = 0; i < DEBRIS_VOLUME_MAX; i++)
  {
    DebrisVolume *vol = &sys->items[i];
    if (!vol->active || vol->home != world)
      continue;
    if (vol->shatter_cool > 0)
      vol->shatter_cool--;
    vol->vz -= gravity * dt;
    // Massive piles: lower terminal fall speed and stronger air drag on slide/spin so they
    // feel planted instead of skating like light foliage scraps of the same shape.
    const float heavy = volume_mass_scale(vol);
    const float vmax = 24.0f / sqrtf(heavy);
    if (vol->vz < -vmax)
      vol->vz = -vmax;
    const float lin = expf(-0.65f * heavy * dt);
    const float ang = expf(-1.05f * heavy * dt);
    vol->vx *= lin;
    vol->vy *= lin;
    vol->wx *= ang;
    vol->wy *= ang;
    vol->wz *= ang;
  }

  // 2. Volume–volume contacts (reuse warm impulses) then TGS velocity solve
  collect_volume_contacts(sys, world);

  // Hard approaching contacts can secondary-fracture both bodies (pre-solve relative speed).
  for (int ci = 0; ci < sys->contact_count; ci++)
  {
    DebrisVolumeContact *c = &sys->contacts[ci];
    if (!c->live)
      continue;
    DebrisVolume *va = &sys->items[c->a];
    DebrisVolume *vb = &sys->items[c->b];
    if (!va->active || !vb->active)
      continue;
    const float rvx = va->vx - vb->vx;
    const float rvy = va->vy - vb->vy;
    const float rvz = va->vz - vb->vz;
    const float approach = -(rvx * c->nx + rvy * c->ny + rvz * c->nz);
    if (approach < 4.0f)
      continue;
    float hx = 0.5f * (va->x + va->com_lx + vb->x + vb->com_lx);
    float hy = 0.5f * (va->y + va->com_ly + vb->y + vb->com_ly);
    float hz = 0.5f * (va->z + va->com_lz + vb->z + vb->com_lz);
    queue_impact(pending, &pending_n, DEBRIS_VOLUME_MAX, c->a, approach, hx, hy, hz, pending_idx);
    queue_impact(pending, &pending_n, DEBRIS_VOLUME_MAX, c->b, approach, hx, hy, hz, pending_idx);
  }

  solve_volume_contacts(sys, world, dt);

  // 3. Integrate against static world occupancy
  for (int i = 0; i < DEBRIS_VOLUME_MAX; i++)
  {
    DebrisVolume *vol = &sys->items[i];
    if (!vol->active || vol->home != world)
      continue;
    DebrisPendingImpact impact;
    integrate_vs_world(vol, world, dt, &impact);
    if (impact.speed > 0.0f)
      queue_impact(pending, &pending_n, DEBRIS_VOLUME_MAX, i, impact.speed, impact.hx, impact.hy,
                   impact.hz, pending_idx);
  }

  // 3b. Secondary shatter — hardest impacts first, budgeted
  for (int pass = 0; pass < DEBRIS_SHATTER_MAX_PER_STEP; pass++)
  {
    int best = -1;
    float best_speed = 0.0f;
    for (int i = 0; i < pending_n; i++)
    {
      const int vi = pending_idx[i];
      if (vi < 0 || !sys->items[vi].active || sys->items[vi].home != world)
        continue;
      if (pending[i].speed > best_speed)
      {
        best_speed = pending[i].speed;
        best = i;
      }
    }
    if (best < 0)
      break;
    const int vi = pending_idx[best];
    const uint32_t seed =
        (uint32_t)vi * 2654435761u ^ (uint32_t)(pending[best].hx * 12.0f) ^
        (uint32_t)(pending[best].hy * 17.0f) ^ (uint32_t)(sys->items[vi].cell_count << 8);
    shatter_debris_volume(sys, &sys->items[vi], pending[best].speed, pending[best].hx,
                          pending[best].hy, pending[best].hz, seed);
    pending_idx[best] = -1;
  }

  // 4. Position-level separation for residual volume overlaps (one pass, Z preferred)
  for (int a = 0; a < DEBRIS_VOLUME_MAX; a++)
  {
    if (!sys->items[a].active || sys->items[a].home != world)
      continue;
    for (int b = a + 1; b < DEBRIS_VOLUME_MAX; b++)
    {
      if (!sys->items[b].active || sys->items[b].home != world)
        continue;
      float amin[3], amax[3], bmin[3], bmax[3];
      volume_aabb(&sys->items[a], &amin[0], &amin[1], &amin[2], &amax[0], &amax[1], &amax[2]);
      volume_aabb(&sys->items[b], &bmin[0], &bmin[1], &bmin[2], &bmax[0], &bmax[1], &bmax[2]);
      const float ox = fminf(amax[0], bmax[0]) - fmaxf(amin[0], bmin[0]);
      const float oy = fminf(amax[1], bmax[1]) - fmaxf(amin[1], bmin[1]);
      const float oz = fminf(amax[2], bmax[2]) - fmaxf(amin[2], bmin[2]);
      if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f)
        continue;
      DebrisVolume *va = &sys->items[a];
      DebrisVolume *vb = &sys->items[b];
      const float w_a = va->inv_mass / (va->inv_mass + vb->inv_mass);
      const float w_b = 1.0f - w_a;
      if (oz <= ox && oz <= oy)
      {
        const float ac = 0.5f * (amin[2] + amax[2]);
        const float bc = 0.5f * (bmin[2] + bmax[2]);
        const float s = (ac >= bc) ? 1.0f : -1.0f;
        va->z += s * oz * w_a;
        vb->z -= s * oz * w_b;
        if (s > 0.0f)
        {
          if (va->vz < 0.0f)
            va->vz = 0.0f;
          if (vb->vz > 0.0f)
            vb->vz = 0.0f;
        }
        else
        {
          if (vb->vz < 0.0f)
            vb->vz = 0.0f;
          if (va->vz > 0.0f)
            va->vz = 0.0f;
        }
      }
      else if (ox <= oy)
      {
        const float ac = 0.5f * (amin[0] + amax[0]);
        const float bc = 0.5f * (bmin[0] + bmax[0]);
        const float s = (ac >= bc) ? 1.0f : -1.0f;
        va->x += s * ox * w_a;
        vb->x -= s * ox * w_b;
      }
      else
      {
        const float ac = 0.5f * (amin[1] + amax[1]);
        const float bc = 0.5f * (bmin[1] + bmax[1]);
        const float s = (ac >= bc) ? 1.0f : -1.0f;
        va->y += s * oy * w_a;
        vb->y -= s * oy * w_b;
      }
    }
  }

  // 5. Writeback bottoms first when resting on world and nothing stacked on top
  for (int pass = 0; pass < DEBRIS_VOLUME_MAX; pass++)
  {
    int best = -1;
    float best_z = 1e30f;
    for (int i = 0; i < DEBRIS_VOLUME_MAX; i++)
    {
      DebrisVolume *vol = &sys->items[i];
      if (!vol->active || vol->home != world)
        continue;
      const bool supported = volume_supported_world(world, vol) ||
                             volume_supported_by_volume(sys, i, world);
      if (!supported || fabsf(vol->vz) >= 0.08f || fabsf(vol->vx) >= 0.08f ||
          fabsf(vol->vy) >= 0.08f || fabsf(vol->wx) >= 0.15f || fabsf(vol->wy) >= 0.15f ||
          fabsf(vol->wz) >= 0.15f)
      {
        vol->rest_ticks = 0;
        continue;
      }
      vol->rest_ticks++;
      if (vol->rest_ticks < 3)
        continue;
      if (volume_has_volume_above(sys, i, world))
        continue;
      if (!volume_supported_world(world, vol) && volume_supported_by_volume(sys, i, world))
        continue; // wait until the support writes back into the world
      if (vol->z < best_z)
      {
        best_z = vol->z;
        best = i;
      }
    }
    if (best < 0)
      break;
    volume_writeback(&sys->items[best]);
  }
}

int voxel_debris_volume_gather(const DebrisVolumeSystem *sys, const World *world,
                               const DebrisVolume **out, int cap)
{
  if (!sys || !out || cap <= 0)
    return 0;
  int n = 0;
  for (int i = 0; i < DEBRIS_VOLUME_MAX && n < cap; i++)
  {
    if (!sys->items[i].active)
      continue;
    if (world && sys->items[i].home != world)
      continue;
    out[n++] = &sys->items[i];
  }
  return n;
}
