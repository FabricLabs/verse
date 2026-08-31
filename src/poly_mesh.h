#ifndef POLY_MESH_H
#define POLY_MESH_H

#include <stdbool.h>
#include <stdint.h>

// Packed vertex-frame mesh baked from a skinned glTF by scripts/bake_gltf_vmesh.py.
// Plain C, no SDL, so a headless test can load a .vmesh without a window.

#define POLY_MESH_NAME_LEN 32
#define POLY_MESH_CLIP_NAME_LEN 32

typedef struct
{
  char name[POLY_MESH_CLIP_NAME_LEN];
  uint32_t frame_count;
  float duration;
  int looping;
  const float *xyz; // frame_count * vertex_count * 3, owned by the parent mesh
} PolyMeshClip;

typedef struct
{
  char name[POLY_MESH_NAME_LEN];
  uint32_t vertex_count;
  uint32_t index_count;
  float height;
  // Footprint center of rest pose (XY) and sole (Z). Sampled verts are returned relative to this
  // so yaw/roll orbit the body, not a corner of the bake AABB.
  float pivot_x, pivot_y, pivot_z;
  uint8_t *colors;   // vertex_count * 3
  uint16_t *indices; // index_count
  PolyMeshClip *clips;
  int clip_count;
  float *frames; // all clip xyz, clips[i].xyz points in
  bool loaded;
} PolyMesh;

bool poly_mesh_init(const char *models_dir); // registers dir only; .vmesh load on first get
void poly_mesh_shutdown(void);

const PolyMesh *poly_mesh_get(const char *name); // loads that mesh on first call
const PolyMeshClip *poly_mesh_find_clip(const PolyMesh *mesh, const char *clip_name);
// Exact name match only (unlike find_clip, which falls back to clips[0]).
bool poly_mesh_has_clip(const PolyMesh *mesh, const char *clip_name);

// Interpolate the named clip at `time` seconds into xyz_out (vertex_count * 3).
// Unknown clips fall back to the first. Returns false if the mesh is empty.
bool poly_mesh_sample(const PolyMesh *mesh, const char *clip_name, float time, float *xyz_out);

// Distance-based draw LOD for software-rasterised actors (fill cost dominates).
//   0 → too far: skip the mesh (caller may draw a sphere impostor)
//   1 → full triangle set
//   2 / 4 → draw every Nth triangle (index step = 3*N)
int poly_mesh_lod_stride(float dist);

// Same as sample, but when `nearest` is true skips the lerp (picks one baked frame).
bool poly_mesh_sample_ex(const PolyMesh *mesh, const char *clip_name, float time, float *xyz_out,
                         bool nearest);

#endif // POLY_MESH_H
