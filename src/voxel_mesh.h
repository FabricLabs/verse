#ifndef VOXEL_MESH_H
#define VOXEL_MESH_H

#include <SDL2/SDL.h>
#include "world.h"

// Generic face quad describing an axis-aligned face on the voxel lattice.
//
// `type` is the voxel the colour was taken from. A renderer needs it to pick the material template
// for sub-voxel surface detail, and a greedy quad has already lost the individual voxels by the time
// it reaches one, so it is recorded here rather than looked up again. VOXEL_AIR means unknown, which
// a renderer should read as "draw this flat".
typedef struct {
  int x0, y0, z0;
  int x1, y1, z1; // inclusive end on the primary plane
  int face;       // 0: +Z (top), 1: -Z (bottom), 2: +Y, 3: +X, 4: -Y, 5: -X
  VoxelType type;
  SDL_Color color;
  uint8_t damage; // VOXEL_FIELD_DAMAGE of the origin voxel; cracks when > 0
} VoxelFaceQuad;

typedef struct {
  VoxelFaceQuad* quads;
  int count;
  int cap;
  // Devlog #7 face buckets: quads are stored contiguously per face (0..5) so a renderer can
  // skip back-facing buckets with octant/directional cull before submitting.
  int face_start[6];
  int face_count[6];
} VoxelMesh;

static inline void voxel_mesh_init(VoxelMesh* m)
{
  if (!m)
    return;
  m->quads = NULL;
  m->count = 0;
  m->cap = 0;
  for (int i = 0; i < 6; i++)
  {
    m->face_start[i] = 0;
    m->face_count[i] = 0;
  }
}

static inline void voxel_mesh_free(VoxelMesh* m)
{
  if (m && m->quads) free(m->quads);
  if (m) { m->quads = NULL; m->count = 0; m->cap = 0; }
}

// Build greedy-merged top faces (+Z) for all z-layers of world w.
// Emits one quad per maximal rectangle of visible top faces.
// Colors are derived from the representative voxel type at the rectangle origin.
void voxel_mesh_build_top_faces_greedy(const World* w, VoxelMesh* out);

// Build a complete mesh (all 6 face orientations) using greedy merging on each
// face plane. This function populates 'out' with quads for +Z,-Z,+Y,+X,-Y,-X
// wherever a face is visible (solid adjacent to air or out-of-bounds).
// Quads are grouped by face into out->face_start/face_count for bucketed cull.
//
// Parent-world terrain meshing can omit sparse fittings (doors, roofs, stairs, …) via
// skip_sparse=true — those are drawn as nested 32³ silhouette meshes instead. Vegetation keeps
// its parent AABB under skip_sparse (see world_voxel_omits_parent_aabb). Default greedy keeps
// everything (mobs/editors/audits). Pass skip_sparse=false when meshing a material template
// world itself (its cells are often the same sparse VoxelTypes).
void voxel_mesh_build_all_faces_greedy(const World* w, VoxelMesh* out);
void voxel_mesh_build_all_faces_greedy_ex(const World* w, VoxelMesh* out, bool skip_sparse);

// Build a coarser LOD mesh by sampling only the stride lattice (Devlog #9/#16).
// stride=2 keeps ~1/8 of the surface cells; used for mid-distance rings. Much cheaper than a
// full remesh — O((N/stride)³) rather than O(N³).
void voxel_mesh_build_all_faces_greedy_lod(const World* w, VoxelMesh* out, int stride);
void voxel_mesh_build_all_faces_greedy_lod_ex(const World* w, VoxelMesh* out, int stride,
                                             bool skip_sparse);

// True if face bucket `face` can face the camera given a vector from geometry toward the camera.
// Only safe when the *entire* AABB lies in one octant relative to the camera (per-chunk cull).
// Do not apply to a whole-world mesh — that drops the facing sides of voxels past the eye.
bool voxel_mesh_face_bucket_visible(int face, float cam_dx, float cam_dy, float cam_dz);

// Copy mesh data from source to destination
void voxel_mesh_copy(VoxelMesh* dest, const VoxelMesh* src);

// Export mesh to OBJ format
bool voxel_mesh_export_obj(const VoxelMesh* mesh, const char* filename);

// Export mesh to OBJ format with optional coordinate transformation
// If y_up is true, applies rotation to transform from VERSE's Z-up to Y-up
bool voxel_mesh_export_obj_with_transform(const VoxelMesh* mesh, const char* filename, bool y_up);

#endif // VOXEL_MESH_H


