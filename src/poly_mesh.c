#include "poly_mesh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Kenney Cube Pets + Gobkit minions + prior Quaternius/OGA meshes. Lazy-loaded on get.
#define POLY_MESH_SLOT_COUNT 28

static const char *g_files[POLY_MESH_SLOT_COUNT] = {
    "goleling.vmesh",
    "pigeon.vmesh",
    "sheep.vmesh",
    "chick.vmesh",
    "bat.vmesh",
    "flesh_walker.vmesh",
    // Kenney Cube Pets (CC0)
    "bunny.vmesh",
    "cat.vmesh",
    "caterpillar.vmesh",
    "cow.vmesh",
    "dog.vmesh",
    "elephant.vmesh",
    "fish.vmesh",
    "giraffe.vmesh",
    "hog.vmesh",
    "lion.vmesh",
    "monkey.vmesh",
    "parrot.vmesh",
    "pig.vmesh",
    "tiger.vmesh",
    // Gobkit Free Minions (CC0) — camp NPC bodies
    "minion-a01.vmesh",
    "minion-a02.vmesh",
    "minion-b01.vmesh",
    "minion-b02.vmesh",
    "minion-c01.vmesh",
    "minion-c02.vmesh",
    "minion-d01.vmesh",
    "minion-d02.vmesh",
};

static const char *g_names[POLY_MESH_SLOT_COUNT] = {
    "goleling",
    "pigeon",
    "sheep",
    "chick",
    "bat",
    "flesh_walker",
    "bunny",
    "cat",
    "caterpillar",
    "cow",
    "dog",
    "elephant",
    "fish",
    "giraffe",
    "hog",
    "lion",
    "monkey",
    "parrot",
    "pig",
    "tiger",
    "minion-a01",
    "minion-a02",
    "minion-b01",
    "minion-b02",
    "minion-c01",
    "minion-c02",
    "minion-d01",
    "minion-d02",
};

static PolyMesh g_meshes[POLY_MESH_SLOT_COUNT];
static bool g_slot_attempted[POLY_MESH_SLOT_COUNT];
static char g_models_dir[512];
static bool g_inited = false;

static bool read_exactly(FILE *f, void *dst, size_t n)
{
  return fread(dst, 1, n, f) == n;
}

static bool load_vmesh(PolyMesh *slot, const char *path, const char *name)
{
  FILE *f = fopen(path, "rb");
  if (!f)
  {
    fprintf(stderr, "[poly_mesh] fopen failed: %s\n", path);
    return false;
  }

  char magic[8];
  uint32_t nverts = 0, nidx = 0, nclips = 0;
  float height = 0.0f;
  if (!read_exactly(f, magic, 8) || memcmp(magic, "VMESH1", 6) != 0)
  {
    fprintf(stderr, "[poly_mesh] bad magic in %s\n", path);
    fclose(f);
    return false;
  }
  if (!read_exactly(f, &nverts, 4) || !read_exactly(f, &nidx, 4) ||
      !read_exactly(f, &height, 4) || !read_exactly(f, &nclips, 4))
  {
    fprintf(stderr, "[poly_mesh] truncated header: %s\n", path);
    fclose(f);
    return false;
  }
  if (nverts == 0 || nidx == 0 || nclips == 0 || nverts > 20000 || nidx > 60000 ||
      nclips > 32)
  {
    fprintf(stderr, "[poly_mesh] implausible header %u %u %u: %s\n", nverts, nidx, nclips, path);
    fclose(f);
    return false;
  }

  uint8_t *colors = (uint8_t *)malloc(nverts * 3);
  uint16_t *indices = (uint16_t *)malloc(nidx * sizeof(uint16_t));
  PolyMeshClip *clips = (PolyMeshClip *)calloc(nclips, sizeof(PolyMeshClip));
  if (!colors || !indices || !clips)
  {
    fprintf(stderr, "[poly_mesh] out of memory: %s\n", path);
    free(colors);
    free(indices);
    free(clips);
    fclose(f);
    return false;
  }

  if (!read_exactly(f, colors, nverts * 3) ||
      !read_exactly(f, indices, nidx * sizeof(uint16_t)))
  {
    fprintf(stderr, "[poly_mesh] truncated colours/indices: %s\n", path);
    free(colors);
    free(indices);
    free(clips);
    fclose(f);
    return false;
  }

  size_t total_floats = 0;
  float *frames = NULL;
  for (uint32_t i = 0; i < nclips; i++)
  {
    char clip_name[POLY_MESH_CLIP_NAME_LEN];
    uint32_t frame_count = 0, loop = 0;
    float duration = 0.0f;
    if (!read_exactly(f, clip_name, POLY_MESH_CLIP_NAME_LEN) ||
        !read_exactly(f, &frame_count, 4) ||
        !read_exactly(f, &duration, 4) ||
        !read_exactly(f, &loop, 4) ||
        frame_count == 0 || frame_count > 256)
    {
      fprintf(stderr, "[poly_mesh] bad clip %u in %s\n", i, path);
      free(colors);
      free(indices);
      free(clips);
      free(frames);
      fclose(f);
      return false;
    }
    clip_name[POLY_MESH_CLIP_NAME_LEN - 1] = '\0';
    const size_t nfloats = (size_t)frame_count * (size_t)nverts * 3u;
    float *grown = (float *)realloc(frames, (total_floats + nfloats) * sizeof(float));
    if (!grown || !read_exactly(f, grown + total_floats, nfloats * sizeof(float)))
    {
      fprintf(stderr, "[poly_mesh] truncated frames on clip %u: %s\n", i, path);
      free(colors);
      free(indices);
      free(clips);
      free(grown ? grown : frames);
      fclose(f);
      return false;
    }
    frames = grown;
    memcpy(clips[i].name, clip_name, POLY_MESH_CLIP_NAME_LEN);
    clips[i].frame_count = frame_count;
    clips[i].duration = duration > 0.01f ? duration : 1.0f;
    const int locomotion = strcmp(clip_name, "Flying_Idle") == 0 ||
                           strcmp(clip_name, "Fast_Flying") == 0 ||
                           strcmp(clip_name, "Idle") == 0 ||
                           strcmp(clip_name, "Walk") == 0 ||
                           strcmp(clip_name, "Run") == 0 ||
                           strcmp(clip_name, "Flying") == 0;
    clips[i].looping = locomotion ? 1 : 0;
    (void)loop;
    clips[i].xyz = NULL; // filled after the loop; pointer rebase
    total_floats += nfloats;
  }
  fclose(f);

  size_t offset = 0;
  for (uint32_t i = 0; i < nclips; i++)
  {
    clips[i].xyz = frames + offset;
    offset += (size_t)clips[i].frame_count * (size_t)nverts * 3u;
  }

  memset(slot, 0, sizeof(*slot));
  strncpy(slot->name, name, POLY_MESH_NAME_LEN - 1);
  slot->vertex_count = nverts;
  slot->index_count = nidx;
  slot->height = height;
  slot->colors = colors;
  slot->indices = indices;
  slot->clips = clips;
  slot->clip_count = (int)nclips;
  slot->frames = frames;
  slot->loaded = true;

  // Older bakes pinned the AABB min corner at the origin. Yaw then spun the whole creature
  // around that corner. Prefer a calm rest clip: XY = footprint centre, Z = sole.
  {
    const PolyMeshClip *rest = NULL;
    static const char *rest_names[] = {"Flying_Idle", "Idle", "Walk", "Flying", NULL};
    for (int n = 0; rest_names[n]; n++)
    {
      for (int c = 0; c < slot->clip_count; c++)
      {
        if (strcmp(slot->clips[c].name, rest_names[n]) == 0)
        {
          rest = &slot->clips[c];
          break;
        }
      }
      if (rest)
        break;
    }
    if (!rest)
      rest = &slot->clips[0];
    const float *xyz = rest->xyz;
    float min_x = xyz[0], max_x = xyz[0];
    float min_y = xyz[1], max_y = xyz[1];
    float min_z = xyz[2];
    for (uint32_t v = 1; v < nverts; v++)
    {
      const float x = xyz[v * 3];
      const float y = xyz[v * 3 + 1];
      const float z = xyz[v * 3 + 2];
      if (x < min_x) min_x = x;
      if (x > max_x) max_x = x;
      if (y < min_y) min_y = y;
      if (y > max_y) max_y = y;
      if (z < min_z) min_z = z;
    }
    slot->pivot_x = 0.5f * (min_x + max_x);
    slot->pivot_y = 0.5f * (min_y + max_y);
    slot->pivot_z = min_z;
  }
  return true;
}

bool poly_mesh_init(const char *models_dir)
{
  // Register the directory only. Individual .vmesh files load on first poly_mesh_get so entering
  // a world with one sheep does not pay for goleling+pigeon (~6 MB) on the same hitch.
  if (g_inited)
    return true;

  const char *dir = (models_dir && models_dir[0]) ? models_dir : "models/poly";
  strncpy(g_models_dir, dir, sizeof(g_models_dir) - 1);
  g_models_dir[sizeof(g_models_dir) - 1] = '\0';
  memset(g_meshes, 0, sizeof(g_meshes));
  memset(g_slot_attempted, 0, sizeof(g_slot_attempted));
  g_inited = true;
  return true;
}

void poly_mesh_shutdown(void)
{
  for (int i = 0; i < POLY_MESH_SLOT_COUNT; i++)
  {
    free(g_meshes[i].colors);
    free(g_meshes[i].indices);
    free(g_meshes[i].clips);
    free(g_meshes[i].frames);
    memset(&g_meshes[i], 0, sizeof(g_meshes[i]));
  }
  memset(g_slot_attempted, 0, sizeof(g_slot_attempted));
  g_models_dir[0] = '\0';
  g_inited = false;
}

const PolyMesh *poly_mesh_get(const char *name)
{
  if (!name || !name[0])
    return NULL;
  if (!g_inited)
    poly_mesh_init(NULL);

  for (int i = 0; i < POLY_MESH_SLOT_COUNT; i++)
  {
    if (strcmp(g_names[i], name) != 0)
      continue;
    if (!g_slot_attempted[i])
    {
      g_slot_attempted[i] = true;
      char path[1024];
      snprintf(path, sizeof(path), "%s/%s", g_models_dir, g_files[i]);
      if (!load_vmesh(&g_meshes[i], path, g_names[i]))
        fprintf(stderr, "[poly_mesh] missing or unreadable: %s\n", path);
    }
    return g_meshes[i].loaded ? &g_meshes[i] : NULL;
  }
  return NULL;
}

const PolyMeshClip *poly_mesh_find_clip(const PolyMesh *mesh, const char *clip_name)
{
  if (!mesh || mesh->clip_count <= 0)
    return NULL;
  if (clip_name && clip_name[0])
  {
    for (int i = 0; i < mesh->clip_count; i++)
    {
      if (strcmp(mesh->clips[i].name, clip_name) == 0)
        return &mesh->clips[i];
    }
  }
  return &mesh->clips[0];
}

bool poly_mesh_has_clip(const PolyMesh *mesh, const char *clip_name)
{
  if (!mesh || !clip_name || !clip_name[0] || mesh->clip_count <= 0)
    return false;
  for (int i = 0; i < mesh->clip_count; i++)
  {
    if (strcmp(mesh->clips[i].name, clip_name) == 0)
      return true;
  }
  return false;
}

bool poly_mesh_sample(const PolyMesh *mesh, const char *clip_name, float time, float *xyz_out)
{
  return poly_mesh_sample_ex(mesh, clip_name, time, xyz_out, false);
}

int poly_mesh_lod_stride(float dist)
{
  if (dist < 0.0f)
    dist = -dist;
  if (dist > 96.0f)
    return 0;
  if (dist > 40.0f)
    return 4;
  if (dist > 20.0f)
    return 2;
  return 1;
}

bool poly_mesh_sample_ex(const PolyMesh *mesh, const char *clip_name, float time, float *xyz_out,
                         bool nearest)
{
  if (!mesh || !mesh->loaded || !xyz_out || mesh->vertex_count == 0)
    return false;
  const PolyMeshClip *clip = poly_mesh_find_clip(mesh, clip_name);
  if (!clip || clip->frame_count == 0)
    return false;

  float t = time;
  if (clip->looping)
  {
    const float d = clip->duration;
    t = t - d * (float)((int)(t / d));
    if (t < 0.0f)
      t += d;
  }
  else if (t > clip->duration)
    t = clip->duration;
  if (t < 0.0f)
    t = 0.0f;

  const float u = (clip->duration > 1e-4f) ? (t / clip->duration) : 0.0f;
  const float frame_f = u * (float)(clip->frame_count - 1);
  int i0 = (int)frame_f;
  if (i0 < 0)
    i0 = 0;
  if (i0 >= (int)clip->frame_count)
    i0 = (int)clip->frame_count - 1;

  const uint32_t stride = mesh->vertex_count * 3;
  const float *a = clip->xyz + (size_t)i0 * stride;
  if (nearest || clip->frame_count == 1)
  {
    memcpy(xyz_out, a, (size_t)stride * sizeof(float));
  }
  else
  {
    int i1 = i0 + 1;
    if (i1 >= (int)clip->frame_count)
      i1 = (int)clip->frame_count - 1;
    const float frac = frame_f - (float)i0;
    const float *b = clip->xyz + (size_t)i1 * stride;
    for (uint32_t i = 0; i < stride; i++)
      xyz_out[i] = a[i] + (b[i] - a[i]) * frac;
  }

  // Actor-local space: feet at the origin, body centred on XY so yaw orbits the torso.
  {
    const float px = mesh->pivot_x;
    const float py = mesh->pivot_y;
    const float pz = mesh->pivot_z;
    for (uint32_t v = 0; v < mesh->vertex_count; v++)
    {
      xyz_out[v * 3] -= px;
      xyz_out[v * 3 + 1] -= py;
      xyz_out[v * 3 + 2] -= pz;
    }
  }
  return true;
}
