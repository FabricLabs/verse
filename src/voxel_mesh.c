#include "voxel_mesh.h"
#include "voxel_render_common.h"
#include "voxel_shape.h"
#include "constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Check if debug mode is enabled via environment variable.
//
// Cached deliberately. DEBUG_PRINT appears inside the greedy mesher's innermost loops, so an
// uncached getenv here made those loops observably side-effecting: the compiler could not discard
// them when debugging was off, and building the mesh for a 128^3 world spent most of a second
// doing millions of environment lookups that printed nothing.
static inline bool is_debug_enabled(void)
{
  static int cached = -1;
  if (cached < 0)
  {
    const char *debug_env = getenv("VERSE_DEBUG");
    cached = (debug_env && (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0)) ? 1 : 0;
  }
  return cached == 1;
}

// Debug print macro that respects debug flag
#define DEBUG_PRINT(fmt, ...)     \
  do                              \
  {                               \
    if (is_debug_enabled())       \
      printf(fmt, ##__VA_ARGS__); \
  } while (0)

static inline void voxel_mesh_emit(VoxelMesh *m, VoxelFaceQuad q)
{
  if (m->count >= m->cap)
  {
    int ncap = m->cap ? (m->cap * 2) : 1024;
    VoxelFaceQuad *nq = (VoxelFaceQuad *)realloc(m->quads, (size_t)ncap * sizeof(VoxelFaceQuad));
    if (!nq)
      return;
    m->quads = nq;
    m->cap = ncap;
  }
  m->quads[m->count++] = q;
}

void voxel_mesh_build_top_faces_greedy(const World *w, VoxelMesh *out)
{
  if (!w || !out)
    return;
  const int W = (int)w->width, H = (int)w->height, D = (int)w->depth;
  uint8_t *vis = (uint8_t *)malloc((size_t)W * (size_t)H);
  if (!vis)
    return;
  for (int z = 0; z < D; z++)
  {
    // Build visibility for top faces on this z slice
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
      {
        const Voxel *v = world_voxel_cptr_fast((World *)w, x, y, z);
        const Voxel *above = (z + 1 < D) ? world_voxel_cptr_fast((World *)w, x, y, z + 1) : NULL;
        vis[y * W + x] = (uint8_t)(v && v->type != VOXEL_AIR &&
                                   (!above || voxel_is_transparent_type(above->type)));
      }
    int *used = (int *)calloc((size_t)W * (size_t)H, sizeof(int));
    if (!used)
    {
      free(vis);
      return;
    }
    for (int y = 0; y < H; y++)
    {
      for (int x = 0; x < W; x++)
      {
        if (!vis[y * W + x] || used[y * W + x])
          continue;
        int wlen = 1;
        while (x + wlen < W && vis[y * W + (x + wlen)] && !used[y * W + (x + wlen)])
          wlen++;
        int hlen = 1;
        int ok = 1;
        while (y + hlen < H && ok)
        {
          for (int xx = 0; xx < wlen; xx++)
          {
            if (!vis[(y + hlen) * W + (x + xx)] || used[(y + hlen) * W + (x + xx)])
            {
              ok = 0;
              break;
            }
          }
          if (ok)
            hlen++;
        }
        uint8_t r = 64, g = 64, b = 64;
        VoxelType rep_type = VOXEL_AIR;
        const Voxel *v0 = world_voxel_cptr_fast((World *)w, x, y, z);
        if (v0)
        {
          world_voxel_type_color(v0->type, &r, &g, &b);
          rep_type = v0->type;
        }
        VoxelFaceQuad q = {.x0 = x, .y0 = y, .z0 = z,
                           .x1 = x + wlen - 1, .y1 = y + hlen - 1, .z1 = z,
                           .face = 0, .type = rep_type,
                           .color = (SDL_Color){r, g, b, 255},
                           .damage = v0 ? voxel_get_damage(v0) : 0};
        voxel_mesh_emit(out, q);
        for (int yy = 0; yy < hlen; yy++)
          for (int xx = 0; xx < wlen; xx++)
            used[(y + yy) * W + (x + xx)] = 1;
      }
    }
    free(used);
  }
  free(vis);
}

// Emit quads from a 2D visibility mask using greedy merging.
// `used` is a scratch buffer of at least W*H bytes (caller-owned, contents ignored on entry).
static void greedy_merge_mask_emit(const World *w, int W, int H, uint8_t *mask, uint8_t *used,
                                   int face, int fixed_axis_val, VoxelMesh *out)
{
  DEBUG_PRINT("[GREEDY] Starting greedy merge for face %d at slice %d, mask size %dx%d\n", face, fixed_axis_val, W, H);

  // Track how many quads we generate
  int debug_quad_count = 0;

  memset(used, 0, (size_t)W * (size_t)H);

  // Scan the mask and merge adjacent visible cells into quads
  for (int y = 0; y < H; y++)
  {
    for (int x = 0; x < W; x++)
    {
      if (!mask[y * W + x] || used[y * W + x])
        continue;

      // Find the height of this run first (for better 2x2 merging)
      int hlen = 1;
      bool can_extend_height = true;
      while (can_extend_height && y + hlen < H)
      {
        // Check if the entire current row can extend (we'll determine width later)
        if (!mask[(y + hlen) * W + x] || used[(y + hlen) * W + x])
        {
          can_extend_height = false;
          break;
        }
        if (can_extend_height)
          hlen++;
      }

      // Now find the maximum width we can extend to at this height
      int wlen = 1;
      while (x + wlen < W)
      {
        bool can_extend_width = true;
        for (int dy = 0; dy < hlen; dy++)
        {
          if (!mask[(y + dy) * W + (x + wlen)] || used[(y + dy) * W + (x + wlen)])
          {
            can_extend_width = false;
            break;
          }
        }
        if (can_extend_width)
          wlen++;
        else
          break;
      }

      if (debug_quad_count < 20) // Only show first 20 quads to avoid spam
        DEBUG_PRINT("[GREEDY] Found run at (%d,%d): width=%d, height=%d\n", x, y, wlen, hlen);

      // Create quad
      VoxelFaceQuad q;
      q.face = face;

      // Map mask coordinates to world coordinates based on face direction
      if (face == 0 || face == 1) // z fixed (top/bottom faces) - mask over X-Y plane
      {
        q.x0 = x;
        q.y0 = y;
        q.z0 = fixed_axis_val;
        q.x1 = x + wlen;           // Face spans to next voxel coordinate (exclusive)
        q.y1 = y + hlen;           // Face spans to next voxel coordinate (exclusive)
        q.z1 = fixed_axis_val + 1; // Face spans to next Z coordinate
        DEBUG_PRINT("[GREEDY] Face %d: Z-fixed quad at z=%d: x[%d,%d], y[%d,%d], z[%d,%d]\n",
                    face, fixed_axis_val, q.x0, q.x1, q.y0, q.y1, q.z0, q.z1);
      }
      else if (face == 2 || face == 4) // y fixed (front/back faces) - mask over X-Z plane
      {
        q.x0 = x;
        q.y0 = fixed_axis_val;
        q.z0 = y;                  // y in mask is actually z coordinate
        q.x1 = x + wlen;           // Face spans to next voxel coordinate (exclusive)
        q.y1 = fixed_axis_val + 1; // Face spans to next Y coordinate
        q.z1 = y + hlen;           // Face spans to next voxel coordinate (exclusive)
        DEBUG_PRINT("[GREEDY] Face %d: Y-fixed quad at y=%d: x[%d,%d], z[%d,%d]\n",
                    face, fixed_axis_val, q.x0, q.x1, q.z0, q.z1);
      }
      else // x fixed (left/right faces) - mask over Y-Z plane
      {
        q.x0 = fixed_axis_val;
        q.y0 = x;                  // x in mask is actually y coordinate
        q.z0 = y;                  // y in mask is actually z coordinate
        q.x1 = fixed_axis_val + 1; // Face spans to next X coordinate
        q.y1 = x + wlen;           // Face spans to next voxel coordinate (exclusive)
        q.z1 = y + hlen;           // Face spans to next voxel coordinate (exclusive)
        DEBUG_PRINT("[GREEDY] Face %d: X-fixed quad at x=%d: y[%d,%d], z[%d,%d]\n",
                    face, fixed_axis_val, q.y0, q.y1, q.z0, q.z1);
      }

      // Colour and material come from the voxel at the quad's origin position.
      uint8_t r = 64, g = 64, b = 64;
      q.type = VOXEL_AIR;
      const Voxel *rep = NULL;
      if (face == 0 || face == 1) // z faces
        rep = world_voxel_cptr_fast(w, q.x0, q.y0, fixed_axis_val);
      else if (face == 2 || face == 4) // y faces
        rep = world_voxel_cptr_fast(w, q.x0, fixed_axis_val, q.z0);
      else // x faces
        rep = world_voxel_cptr_fast(w, fixed_axis_val, q.y0, q.z0);
      if (rep)
      {
        world_voxel_type_color(rep->type, &r, &g, &b);
        q.type = rep->type;
        q.damage = voxel_get_damage(rep);
      }
      else
      {
        q.damage = 0;
      }
      q.color = (SDL_Color){r, g, b, 255};

      // Debug: show coordinate mapping details
      if (debug_quad_count < 10)
      {
        DEBUG_PRINT("[GREEDY] Quad %d mapping: face=%d, mask(%d,%d) -> world(%d,%d,%d) to (%d,%d,%d)\n",
                    debug_quad_count, face, x, y, q.x0, q.y0, q.z0, q.x1, q.y1, q.z1);
      }

      // Per-quad mask dumps used to live here. They re-printed the whole slice mask for every
      // emitted quad, which is unusable as output and kept the innermost loops from being
      // optimized away when debugging was off.

      // Add to mesh
      voxel_mesh_emit(out, q);
      debug_quad_count++;

      // Mark this entire area as used so it doesn't get processed again
      for (int dy = 0; dy < hlen; dy++)
      {
        for (int dx = 0; dx < wlen; dx++)
        {
          used[(y + dy) * W + (x + dx)] = 1;
          mask[(y + dy) * W + (x + dx)] = 0; // ensure not re-picked
        }
      }

      // Skip the rest of this run and continue scanning this row
      x += wlen - 1;
    }
  }

  DEBUG_PRINT("[GREEDY] Generated %d quads for face %d slice %d\n", debug_quad_count, face, fixed_axis_val);
}

// Build all faces using greedy merging algorithm
void voxel_mesh_build_all_faces_greedy_ex(const World *w, VoxelMesh *out, bool skip_sparse)
{
  DEBUG_PRINT("[MESH] Starting mesh generation for all faces\n");
  DEBUG_PRINT("[MESH] World dimensions: %dx%dx%d\n", (int)w->width, (int)w->height, (int)w->depth);

  // Clear output
  out->count = 0;
  for (int i = 0; i < 6; i++)
  {
    out->face_start[i] = 0;
    out->face_count[i] = 0;
  }

  // One mask buffer for the whole build. Allocating per slice used to dominate remesh cost on
  // 128³ worlds (6 faces × 128 slices × malloc/free of a 16 KB plane).
  const int max_plane = (int)w->width * (int)w->height;
  const int max_plane_xz = (int)w->width * (int)w->depth;
  const int max_plane_yz = (int)w->height * (int)w->depth;
  int mask_cap = max_plane;
  if (max_plane_xz > mask_cap)
    mask_cap = max_plane_xz;
  if (max_plane_yz > mask_cap)
    mask_cap = max_plane_yz;
  uint8_t *mask = (uint8_t *)malloc((size_t)mask_cap);
  uint8_t *used = (uint8_t *)malloc((size_t)mask_cap);
  if (!mask || !used)
  {
    free(mask);
    free(used);
    return;
  }

  // Process each face direction
  for (int face = 0; face < 6; face++)
  {
    DEBUG_PRINT("[MESH] Processing face direction %d\n", face);
    out->face_start[face] = out->count;

    // Determine which axis is fixed and which plane to process
    int fixed_axis;
    int W, H;

    if (face == 0 || face == 1) // z fixed (top/bottom faces)
    {
      fixed_axis = 2; // Z axis
      W = (int)w->width;
      H = (int)w->height;
      DEBUG_PRINT("[MESH] Z-fixed face: processing X-Y plane (%dx%d)\n", W, H);
    }
    else if (face == 2 || face == 4) // y fixed (front/back faces)
    {
      fixed_axis = 1; // Y axis
      W = (int)w->width;
      H = (int)w->depth;
      DEBUG_PRINT("[MESH] Y-fixed face: processing X-Z plane (%dx%d)\n", W, H);
    }
    else // x fixed (left/right faces)
    {
      fixed_axis = 0; // X axis
      W = (int)w->height;
      H = (int)w->depth;
      DEBUG_PRINT("[MESH] X-fixed face: processing Y-Z plane (%dx%d)\n", W, H);
    }

    // Process each slice along the fixed axis
    // For positive faces: check slices from 0 to world dimension
    // For negative faces: check slices from 0 to world dimension (but the logic will work correctly now)
    for (int slice = 0; slice < (fixed_axis == 0 ? (int)w->width : fixed_axis == 1 ? (int)w->height
                                                                                   : (int)w->depth);
         slice++)
    {
      if (slice % 10 == 0) // Log every 10th slice to avoid spam
        DEBUG_PRINT("[MESH] Processing slice %d/%d for face %d\n", slice,
                    (fixed_axis == 0 ? (int)w->width : fixed_axis == 1 ? (int)w->height
                                                                       : (int)w->depth),
                    face);

      // Initialize mask to all zeros
      memset(mask, 0, (size_t)W * (size_t)H);

      // Debug: print the slice we're processing
      if (slice == 15 || slice == 16)
      { // Only for our test cube coordinates
        DEBUG_PRINT("[MASK] Building mask for face %d, slice %d (W=%d, H=%d)\n", face, slice, W, H);
      }

      for (int y = 0; y < H; y++)
      {
        for (int x = 0; x < W; x++)
        {
          int world_x, world_y, world_z;

          if (face == 0 || face == 1) // z fixed
          {
            world_x = x;
            world_y = y;
            world_z = slice;
          }
          else if (face == 2 || face == 4) // y fixed
          {
            world_x = x;
            world_y = slice;
            world_z = y;
          }
          else // x fixed
          {
            world_x = slice;
            world_y = x;
            world_z = y;
          }

          const Voxel *v = world_voxel_cptr_fast((World *)w, world_x, world_y, world_z);
          const Voxel *nb = NULL;

          // Check neighbor based on face direction
          // For each face, check if voxel at (x,y,slice) has air neighbor in the direction of the face normal
          if (face == 0) // +Z face: check if voxel has air neighbor above
            nb = (world_z + 1 < (int)w->depth) ? world_voxel_cptr_fast((World *)w, world_x, world_y, world_z + 1) : NULL;
          else if (face == 1) // -Z face: check if voxel has air neighbor below
            nb = (world_z - 1 >= 0) ? world_voxel_cptr_fast((World *)w, world_x, world_y, world_z - 1) : NULL;
          else if (face == 2) // +Y face: check if voxel has air neighbor in front
            nb = (world_y + 1 < (int)w->height) ? world_voxel_cptr_fast((World *)w, world_x, world_y + 1, world_z) : NULL;
          else if (face == 4) // -Y face: check if voxel has air neighbor behind
            nb = (world_y - 1 >= 0) ? world_voxel_cptr_fast((World *)w, world_x, world_y - 1, world_z) : NULL;
          else if (face == 3) // +X face: check if voxel has air neighbor to the right
            nb = (world_x + 1 < (int)w->width) ? world_voxel_cptr_fast((World *)w, world_x + 1, world_y, world_z) : NULL;
          else // -X face: check if voxel has air neighbor to the left
            nb = (world_x - 1 >= 0) ? world_voxel_cptr_fast((World *)w, world_x - 1, world_y, world_z) : NULL;

          // Parent-world sparse fittings are drawn as nested 32³ meshes; vegetation keeps its AABB
          // (see world_voxel_omits_parent_aabb). When meshing a template world itself, skip_sparse
          // is false so bush/leaf cells still emit faces.
          bool is_visible = (v && v->type != VOXEL_AIR &&
                             !(skip_sparse && world_voxel_omits_parent_aabb(v)) &&
                             (!nb || voxel_is_transparent_type(nb->type) ||
                              voxel_has_nontrivial_shape(nb)));
          mask[y * W + x] = (uint8_t)is_visible;

          // Debug: show first few voxel visibility checks for each face
          static int debug_voxel_count[6] = {0};
          if (debug_voxel_count[face] < 5 && is_visible)
          {
            DEBUG_PRINT("[MESH] Face %d: visible at (%d,%d,%d): voxel_type=%d, neighbor_type=%d\n",
                        face, world_x, world_y, world_z, v ? v->type : -1, nb ? nb->type : -1);
            debug_voxel_count[face]++;
          }
        }
      }

      // Debug: show mask contents for our test cube
      if (slice == 15 || slice == 16)
      {
        DEBUG_PRINT("[MASK] Face %d, slice %d mask contents:\n", face, slice);
        for (int y = 0; y < H; y++)
        {
          DEBUG_PRINT("[MASK]   Row %d: ", y);
          for (int x = 0; x < W; x++)
          {
            if (x >= 15 && x <= 16 && y >= 15 && y <= 16)
            {
              DEBUG_PRINT("[%d]", mask[y * W + x]);
            }
            else
            {
              DEBUG_PRINT(" .");
            }
          }
          DEBUG_PRINT("\n");
        }
      }

      // Generate quads from mask
      int faces_after = out->count;
      greedy_merge_mask_emit(w, W, H, mask, used, face, slice, out);
      int faces_generated = out->count - faces_after;

      if (faces_generated > 0)
        DEBUG_PRINT("[MESH] Face %d slice %d: generated %d quads\n", face, slice, faces_generated);
    }

    DEBUG_PRINT("[MESH] Face direction %d complete: %d total quads\n", face, out->count);
    out->face_count[face] = out->count - out->face_start[face];
  }

  free(used);
  free(mask);
  DEBUG_PRINT("[MESH] Mesh generation complete: %d total quads\n", out->count);
}

void voxel_mesh_build_all_faces_greedy(const World *w, VoxelMesh *out)
{
  // Preserve full emission by default (mobs, editors, audits). Terrain FP cache passes
  // skip_sparse=true via _ex so sparse parents are drawn as nested silhouette meshes instead.
  voxel_mesh_build_all_faces_greedy_ex(w, out, false);
}

void voxel_mesh_build_all_faces_greedy_lod(const World *w, VoxelMesh *out, int stride)
{
  voxel_mesh_build_all_faces_greedy_lod_ex(w, out, stride, false);
}

// Coarse mid-distance LOD: emit stride×stride faces only on the lattice.
//
// The previous implementation built a full greedy mesh then filtered origins onto the lattice —
// same O(N³) cost as a full remesh, which made ring-1 warm (~80ms for 128³) unusable in-frame.
// Sampling only every `stride` cell is O((N/stride)³) and reads fine at horizon distances.
void voxel_mesh_build_all_faces_greedy_lod_ex(const World *w, VoxelMesh *out, int stride,
                                             bool skip_sparse)
{
  if (!w || !out)
    return;
  if (stride < 2)
  {
    voxel_mesh_build_all_faces_greedy_ex(w, out, skip_sparse);
    return;
  }

  out->count = 0;
  for (int i = 0; i < 6; i++)
  {
    out->face_start[i] = 0;
    out->face_count[i] = 0;
  }

  const int Ww = (int)w->width;
  const int Hh = (int)w->height;
  const int Dd = (int)w->depth;

  for (int face = 0; face < 6; face++)
  {
    out->face_start[face] = out->count;

    int fixed_dim, plane_w, plane_h;
    if (face == 0 || face == 1)
    {
      fixed_dim = Dd;
      plane_w = Ww;
      plane_h = Hh;
    }
    else if (face == 2 || face == 4)
    {
      fixed_dim = Hh;
      plane_w = Ww;
      plane_h = Dd;
    }
    else
    {
      fixed_dim = Ww;
      plane_w = Hh;
      plane_h = Dd;
    }

    for (int slice = 0; slice < fixed_dim; slice += stride)
    {
      for (int y = 0; y < plane_h; y += stride)
      {
        for (int x = 0; x < plane_w; x += stride)
        {
          int world_x, world_y, world_z;
          if (face == 0 || face == 1)
          {
            world_x = x;
            world_y = y;
            world_z = slice;
          }
          else if (face == 2 || face == 4)
          {
            world_x = x;
            world_y = slice;
            world_z = y;
          }
          else
          {
            world_x = slice;
            world_y = x;
            world_z = y;
          }

          const Voxel *v = world_voxel_cptr_fast((World *)w, world_x, world_y, world_z);
          if (!v || v->type == VOXEL_AIR)
            continue;
          if (skip_sparse && world_voxel_omits_parent_aabb(v))
            continue;

          const Voxel *nb = NULL;
          if (face == 0)
            nb = (world_z + 1 < Dd) ? world_voxel_cptr_fast((World *)w, world_x, world_y, world_z + 1)
                                    : NULL;
          else if (face == 1)
            nb = (world_z - 1 >= 0) ? world_voxel_cptr_fast((World *)w, world_x, world_y, world_z - 1)
                                   : NULL;
          else if (face == 2)
            nb = (world_y + 1 < Hh) ? world_voxel_cptr_fast((World *)w, world_x, world_y + 1, world_z)
                                    : NULL;
          else if (face == 4)
            nb = (world_y - 1 >= 0) ? world_voxel_cptr_fast((World *)w, world_x, world_y - 1, world_z)
                                   : NULL;
          else if (face == 3)
            nb = (world_x + 1 < Ww) ? world_voxel_cptr_fast((World *)w, world_x + 1, world_y, world_z)
                                    : NULL;
          else
            nb = (world_x - 1 >= 0) ? world_voxel_cptr_fast((World *)w, world_x - 1, world_y, world_z)
                                   : NULL;

          if (nb && !voxel_is_transparent_type(nb->type) && !voxel_has_nontrivial_shape(nb))
            continue;

          // Expand the face to cover the stride cell (clamped to world bounds).
          const int x1 = (x + stride < plane_w) ? x + stride : plane_w;
          const int y1 = (y + stride < plane_h) ? y + stride : plane_h;

          VoxelFaceQuad q;
          q.face = face;
          q.type = v->type;
          q.damage = voxel_get_damage(v);
          uint8_t r = 64, g = 64, b = 64;
          world_voxel_type_color(v->type, &r, &g, &b);
          q.color = (SDL_Color){r, g, b, 255};

          if (face == 0 || face == 1)
          {
            q.x0 = world_x;
            q.y0 = world_y;
            q.z0 = world_z;
            q.x1 = x1;
            q.y1 = y1;
            q.z1 = world_z + 1;
          }
          else if (face == 2 || face == 4)
          {
            q.x0 = world_x;
            q.y0 = world_y;
            q.z0 = world_z;
            q.x1 = x1;
            q.y1 = world_y + 1;
            q.z1 = y1;
          }
          else
          {
            q.x0 = world_x;
            q.y0 = world_y;
            q.z0 = world_z;
            q.x1 = world_x + 1;
            q.y1 = x1;
            q.z1 = y1;
          }

          voxel_mesh_emit(out, q);
        }
      }
    }

    out->face_count[face] = out->count - out->face_start[face];
  }
}

bool voxel_mesh_face_bucket_visible(int face, float cam_dx, float cam_dy, float cam_dz)
{
  // Face normals: 0=+Z, 1=-Z, 2=+Y, 3=+X, 4=-Y, 5=-X. Visible when the camera is on the
  // outward side (dot(normal, cam_from_chunk) > 0) — Devlog #7 directional cull.
  switch (face)
  {
  case 0:
    return cam_dz > 0.0f;
  case 1:
    return cam_dz < 0.0f;
  case 2:
    return cam_dy > 0.0f;
  case 3:
    return cam_dx > 0.0f;
  case 4:
    return cam_dy < 0.0f;
  case 5:
    return cam_dx < 0.0f;
  default:
    return true;
  }
}

// Copy mesh data from source to destination
void voxel_mesh_copy(VoxelMesh *dest, const VoxelMesh *src)
{
  if (!dest || !src)
    return;

  // Free existing data in destination
  if (dest->quads)
  {
    free(dest->quads);
    dest->quads = NULL;
  }

  // Initialize destination
  dest->count = 0;
  dest->cap = 0;
  for (int i = 0; i < 6; i++)
  {
    dest->face_start[i] = 0;
    dest->face_count[i] = 0;
  }

  // Copy quads from source
  if (src->count > 0)
  {
    dest->quads = malloc(src->count * sizeof(VoxelFaceQuad));
    if (dest->quads)
    {
      memcpy(dest->quads, src->quads, src->count * sizeof(VoxelFaceQuad));
      dest->count = src->count;
      dest->cap = src->count;
      for (int i = 0; i < 6; i++)
      {
        dest->face_start[i] = src->face_start[i];
        dest->face_count[i] = src->face_count[i];
      }
      DEBUG_PRINT("[MESH] Copied %d quads from source mesh\n", src->count);
    }
  }
}

// Export mesh to OBJ format
bool voxel_mesh_export_obj(const VoxelMesh *mesh, const char *filename)
{
  return voxel_mesh_export_obj_with_transform(mesh, filename, false);
}

// Export mesh to OBJ format with optional coordinate transformation
// If y_up is true, applies rotation to transform from VERSE's Z-up to Y-up
bool voxel_mesh_export_obj_with_transform(const VoxelMesh *mesh, const char *filename, bool y_up)
{
  if (!mesh || !filename)
    return false;

  FILE *file = fopen(filename, "w");
  if (!file)
    return false;

  DEBUG_PRINT("[OBJ] Starting OBJ export of %d quads to %s (y_up=%s)\n",
              mesh->count, filename, y_up ? "true" : "false");

  // Write OBJ header with coordinate system info
  fprintf(file, "# Voxel mesh export\n");
  fprintf(file, "# Generated by verse voxel mesh generator\n");
  fprintf(file, "# %d faces\n", mesh->count);
  if (y_up)
  {
    fprintf(file, "# Coordinate system: Y-up (transformed from VERSE's Z-up)\n");
  }
  else
  {
    fprintf(file, "# Coordinate system: Z-up (VERSE native)\n");
  }
  fprintf(file, "# Includes normal vectors for proper face orientation\n");
  fprintf(file, "\n");

  // First pass: collect all unique vertices
  typedef struct
  {
    float x, y, z;
    int index;
  } UniqueVertex;

  // Also collect unique normals
  typedef struct
  {
    float x, y, z;
    int index;
  } UniqueNormal;

  UniqueVertex *unique_vertices = malloc(mesh->count * 4 * sizeof(UniqueVertex));
  UniqueNormal *unique_normals = malloc(mesh->count * sizeof(UniqueNormal));
  if (!unique_vertices || !unique_normals)
  {
    free(unique_vertices);
    free(unique_normals);
    fclose(file);
    return false;
  }

  int unique_count = 0;
  int normal_count = 0;
  DEBUG_PRINT("[OBJ] First pass: collecting unique vertices from %d quads\n", mesh->count);

  // Collect all vertices from all quads
  for (int i = 0; i < mesh->count; i++)
  {
    const VoxelFaceQuad *quad = &mesh->quads[i];
    DEBUG_PRINT("[OBJ] Processing quad %d: face=%d, (%d,%d,%d) to (%d,%d,%d)\n",
                i, quad->face, quad->x0, quad->y0, quad->z0, quad->x1, quad->y1, quad->z1);

    // Generate the normal vector for this face
    float normal[3];

    // Calculate normal from the actual vertex positions of the first triangle
    // We'll use the first three vertices of the quad to calculate the normal
    float corners[4][3];

    if (quad->face == 0) // +Z face (top)
    {
      // Counter-clockwise winding when viewed from outside (+Z direction)
      // Top-left, top-right, bottom-right, bottom-left
      corners[0][0] = (float)quad->x0;
      corners[0][1] = (float)quad->y1;
      corners[0][2] = (float)quad->z1;
      corners[1][0] = (float)quad->x1;
      corners[1][1] = (float)quad->y1;
      corners[1][2] = (float)quad->z1;
      corners[2][0] = (float)quad->x1;
      corners[2][1] = (float)quad->y0;
      corners[2][2] = (float)quad->z1;
      corners[3][0] = (float)quad->x0;
      corners[3][1] = (float)quad->y0;
      corners[3][2] = (float)quad->z1;
    }
    else if (quad->face == 1) // -Z face (bottom)
    {
      // Counter-clockwise winding when viewed from outside (-Z direction)
      // Top-left, top-right, bottom-right, bottom-left
      corners[0][0] = (float)quad->x0;
      corners[0][1] = (float)quad->y1;
      corners[0][2] = (float)quad->z0;
      corners[1][0] = (float)quad->x1;
      corners[1][1] = (float)quad->y1;
      corners[1][2] = (float)quad->z0;
      corners[2][0] = (float)quad->x1;
      corners[2][1] = (float)quad->y0;
      corners[2][2] = (float)quad->z0;
      corners[3][0] = (float)quad->x0;
      corners[3][1] = (float)quad->y0;
      corners[3][2] = (float)quad->z0;
    }
    else if (quad->face == 2) // +Y face (front)
    {
      // Counter-clockwise winding when viewed from outside (+Y direction)
      // Bottom-left, top-left, top-right, bottom-right
      corners[0][0] = (float)quad->x0;
      corners[0][1] = (float)quad->y1;
      corners[0][2] = (float)quad->z0;
      corners[1][0] = (float)quad->x0;
      corners[1][1] = (float)quad->y1;
      corners[1][2] = (float)quad->z1;
      corners[2][0] = (float)quad->x1;
      corners[2][1] = (float)quad->y1;
      corners[2][2] = (float)quad->z1;
      corners[3][0] = (float)quad->x1;
      corners[3][1] = (float)quad->y1;
      corners[3][2] = (float)quad->z0;
    }
    else if (quad->face == 4) // -Y face (back)
    {
      // Counter-clockwise winding when viewed from outside (-Y direction)
      // Bottom-left, bottom-right, top-right, top-left
      corners[0][0] = (float)quad->x0;
      corners[0][1] = (float)quad->y0;
      corners[0][2] = (float)quad->z0;
      corners[1][0] = (float)quad->x1;
      corners[1][1] = (float)quad->y0;
      corners[1][2] = (float)quad->z0;
      corners[2][0] = (float)quad->x1;
      corners[2][1] = (float)quad->y0;
      corners[2][2] = (float)quad->z1;
      corners[3][0] = (float)quad->x0;
      corners[3][1] = (float)quad->y0;
      corners[3][2] = (float)quad->z1;
    }
    else if (quad->face == 3) // +X face (right)
    {
      // Counter-clockwise winding when viewed from outside (+X direction)
      // Bottom-left, top-left, top-right, bottom-right
      corners[0][0] = (float)quad->x1;
      corners[0][1] = (float)quad->y0;
      corners[0][2] = (float)quad->z0;
      corners[1][0] = (float)quad->x1;
      corners[1][1] = (float)quad->y1;
      corners[1][2] = (float)quad->z0;
      corners[2][0] = (float)quad->x1;
      corners[2][1] = (float)quad->y1;
      corners[2][2] = (float)quad->z1;
      corners[3][0] = (float)quad->x1;
      corners[3][1] = (float)quad->y0;
      corners[3][2] = (float)quad->z1;
    }
    else // quad->face == 5, -X face (left)
    {
      // Counter-clockwise winding when viewed from outside (-X direction)
      // Bottom-left, bottom-right, top-right, top-left
      corners[0][0] = (float)quad->x0;
      corners[0][1] = (float)quad->y0;
      corners[0][2] = (float)quad->z0;
      corners[1][0] = (float)quad->x0;
      corners[1][1] = (float)quad->y0;
      corners[1][2] = (float)quad->z1;
      corners[2][0] = (float)quad->x0;
      corners[2][1] = (float)quad->y1;
      corners[2][2] = (float)quad->z1;
      corners[3][0] = (float)quad->x0;
      corners[3][1] = (float)quad->y1;
      corners[3][2] = (float)quad->z0;
    }

    // Calculate normal from the first triangle (corners 0, 1, 2)
    // Vector from corner 0 to corner 1
    float v1[3] = {
        corners[1][0] - corners[0][0],
        corners[1][1] - corners[0][1],
        corners[1][2] - corners[0][2]};

    // Vector from corner 0 to corner 2
    float v2[3] = {
        corners[2][0] - corners[0][0],
        corners[2][1] - corners[0][1],
        corners[2][2] - corners[0][2]};

    // Calculate cross product: normal = v1 × v2
    normal[0] = v1[1] * v2[2] - v1[2] * v2[1];
    normal[1] = v1[2] * v2[0] - v1[0] * v2[2];
    normal[2] = v1[0] * v2[1] - v1[1] * v2[0];

    // Normalize the normal vector
    float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (length > 0.0f)
    {
      normal[0] /= length;
      normal[1] /= length;
      normal[2] /= length;
    }

    // Apply coordinate transformation to normal if needed
    if (y_up)
    {
      // Transform normal from VERSE's Z-up to Y-up: (nx, ny, nz) -> (nx, nz, -ny)
      float temp_x = normal[0];
      float temp_y = normal[2];
      float temp_z = -normal[1];
      normal[0] = temp_x;
      normal[1] = temp_y;
      normal[2] = temp_z;
    }

    // Add normal to unique normals list
    unique_normals[normal_count].x = normal[0];
    unique_normals[normal_count].y = normal[1];
    unique_normals[normal_count].z = normal[2];
    unique_normals[normal_count].index = normal_count;
    normal_count++;

    // Add each corner to unique vertices list
    for (int j = 0; j < 4; j++)
    {
      // Check if this vertex already exists
      int existing_index = -1;
      for (int k = 0; k < unique_count; k++)
      {
        if (fabs(unique_vertices[k].x - corners[j][0]) < 0.001f &&
            fabs(unique_vertices[k].y - corners[j][1]) < 0.001f &&
            fabs(unique_vertices[k].z - corners[j][2]) < 0.001f)
        {
          existing_index = k;
          break;
        }
      }

      if (existing_index >= 0)
      {
        // Vertex already exists, use existing index
        unique_vertices[existing_index].index = existing_index;
        DEBUG_PRINT("[OBJ] Quad %d corner %d: reusing existing vertex %d (%.1f,%.1f,%.1f)\n",
                    i, j, existing_index, corners[j][0], corners[j][1], corners[j][2]);
      }
      else
      {
        // New vertex
        unique_vertices[unique_count].x = corners[j][0];
        unique_vertices[unique_count].y = corners[j][1];
        unique_vertices[unique_count].z = corners[j][2];
        unique_vertices[unique_count].index = unique_count;
        DEBUG_PRINT("[OBJ] Quad %d corner %d: new vertex %d (%.1f,%.1f,%.1f)\n",
                    i, j, unique_count, corners[j][0], corners[j][1], corners[j][2]);
        unique_count++;
      }
    }
  }

  DEBUG_PRINT("[OBJ] First pass complete: %d unique vertices found\n", unique_count);

  // Second pass: write unique vertices
  DEBUG_PRINT("[OBJ] Second pass: writing %d unique vertices\n", unique_count);
  for (int i = 0; i < unique_count; i++)
  {
    float x = unique_vertices[i].x;
    float y = unique_vertices[i].y;
    float z = unique_vertices[i].z;

    if (y_up)
    {
      // Transform from VERSE's Z-up to Y-up: (x, y, z) -> (x, z, -y)
      // This rotates the model so Z becomes Y and Y becomes -Z
      float temp_x = x;
      float temp_y = z;
      float temp_z = -y;
      x = temp_x;
      y = temp_y;
      z = temp_z;
    }

    fprintf(file, "v %.1f %.1f %.1f\n", x, y, z);
  }

  // Write normal vectors
  DEBUG_PRINT("[OBJ] Writing %d normal vectors\n", normal_count);
  for (int i = 0; i < normal_count; i++)
  {
    fprintf(file, "vn %.6f %.6f %.6f\n",
            unique_normals[i].x, unique_normals[i].y, unique_normals[i].z);
  }

  // Write texture coordinates (simple mapping for compatibility)
  DEBUG_PRINT("[OBJ] Writing %d texture coordinates\n", normal_count);
  for (int i = 0; i < normal_count; i++)
  {
    fprintf(file, "vt %.6f %.6f\n", 0.0f, 0.0f);
  }

  // Third pass: write faces with normal indices
  DEBUG_PRINT("[OBJ] Third pass: writing faces\n");
  for (int i = 0; i < mesh->count; i++)
  {
    int vertex_indices[4]; // Declare inside the loop for each quad
    const VoxelFaceQuad *quad = &mesh->quads[i];
    for (int j = 0; j < 4; j++)
    {
      // Generate the corner coordinates again
      float corners[4][3];

      if (quad->face == 0) // +Z face
      {
        if (y_up)
        {
          // For Y-up, reverse the winding order to maintain correct orientation
          // Bottom-left, bottom-right, top-right, top-left (reversed for Y-up)
          corners[0][0] = (float)quad->x0;
          corners[0][1] = (float)quad->y0;
          corners[0][2] = (float)quad->z1;
          corners[1][0] = (float)quad->x1;
          corners[1][1] = (float)quad->y0;
          corners[1][2] = (float)quad->z1;
          corners[2][0] = (float)quad->x1;
          corners[2][1] = (float)quad->y1;
          corners[2][2] = (float)quad->z1;
          corners[3][0] = (float)quad->x0;
          corners[3][1] = (float)quad->y1;
          corners[3][2] = (float)quad->z1;
        }
        else
        {
          // For Z-up, use standard winding order
          // Top-left, top-right, bottom-right, bottom-left
          corners[0][0] = (float)quad->x0;
          corners[0][1] = (float)quad->y1;
          corners[0][2] = (float)quad->z1;
          corners[1][0] = (float)quad->x1;
          corners[1][1] = (float)quad->y1;
          corners[1][2] = (float)quad->z1;
          corners[2][0] = (float)quad->x1;
          corners[2][1] = (float)quad->y0;
          corners[2][2] = (float)quad->z1;
          corners[3][0] = (float)quad->x0;
          corners[3][1] = (float)quad->y0;
          corners[3][2] = (float)quad->z1;
        }
      }
      else if (quad->face == 1) // -Z face
      {
        corners[0][0] = (float)quad->x0;
        corners[0][1] = (float)quad->y1;
        corners[0][2] = (float)quad->z0;
        corners[1][0] = (float)quad->x1;
        corners[1][1] = (float)quad->y1;
        corners[1][2] = (float)quad->z0;
        corners[2][0] = (float)quad->x1;
        corners[2][1] = (float)quad->y0;
        corners[2][2] = (float)quad->z0;
        corners[3][0] = (float)quad->x0;
        corners[3][1] = (float)quad->y0;
        corners[3][2] = (float)quad->z0;
      }
      else if (quad->face == 2) // +Y face
      {
        corners[0][0] = (float)quad->x0;
        corners[0][1] = (float)quad->y1;
        corners[0][2] = (float)quad->z0;
        corners[1][0] = (float)quad->x0;
        corners[1][1] = (float)quad->y1;
        corners[1][2] = (float)quad->z1;
        corners[2][0] = (float)quad->x1;
        corners[2][1] = (float)quad->y1;
        corners[2][2] = (float)quad->z1;
        corners[3][0] = (float)quad->x1;
        corners[3][1] = (float)quad->y1;
        corners[3][2] = (float)quad->z0;
      }
      else if (quad->face == 4) // -Y face
      {
        corners[0][0] = (float)quad->x0;
        corners[0][1] = (float)quad->y0;
        corners[0][2] = (float)quad->z0;
        corners[1][0] = (float)quad->x1;
        corners[1][1] = (float)quad->y0;
        corners[1][2] = (float)quad->z0;
        corners[2][0] = (float)quad->x1;
        corners[2][1] = (float)quad->y0;
        corners[2][2] = (float)quad->z1;
        corners[3][0] = (float)quad->x0;
        corners[3][1] = (float)quad->y0;
        corners[3][2] = (float)quad->z1;
      }
      else if (quad->face == 3) // +X face
      {
        corners[0][0] = (float)quad->x1;
        corners[0][1] = (float)quad->y0;
        corners[0][2] = (float)quad->z0;
        corners[1][0] = (float)quad->x1;
        corners[1][1] = (float)quad->y1;
        corners[1][2] = (float)quad->z0;
        corners[2][0] = (float)quad->x1;
        corners[2][1] = (float)quad->y1;
        corners[2][2] = (float)quad->z1;
        corners[3][0] = (float)quad->x1;
        corners[3][1] = (float)quad->y0;
        corners[3][2] = (float)quad->z1;
      }
      else // quad->face == 5, -X face
      {
        corners[0][0] = (float)quad->x0;
        corners[0][1] = (float)quad->y0;
        corners[0][2] = (float)quad->z0;
        corners[1][0] = (float)quad->x0;
        corners[1][1] = (float)quad->y0;
        corners[1][2] = (float)quad->z1;
        corners[2][0] = (float)quad->x0;
        corners[2][1] = (float)quad->y1;
        corners[2][2] = (float)quad->z1;
        corners[3][0] = (float)quad->x0;
        corners[3][1] = (float)quad->y1;
        corners[3][2] = (float)quad->z0;
      }

      // Find the vertex indices for all 4 corners
      for (int j = 0; j < 4; j++)
      {
        vertex_indices[j] = -1; // Initialize to invalid index
        for (int k = 0; k < unique_count; k++)
        {
          if (fabs(unique_vertices[k].x - corners[j][0]) < 0.001f &&
              fabs(unique_vertices[k].y - corners[j][1]) < 0.001f &&
              fabs(unique_vertices[k].z - corners[j][2]) < 0.001f)
          {
            vertex_indices[j] = k + 1; // OBJ indices are 1-based
            DEBUG_PRINT("[OBJ] Corner %d (%.1f,%.1f,%.1f) -> vertex index %d\n",
                        j, corners[j][0], corners[j][1], corners[j][2], vertex_indices[j]);
            break;
          }
        }

        // Verify we found a valid index
        if (vertex_indices[j] == -1)
        {
          DEBUG_PRINT("[OBJ] ERROR: Could not find vertex index for corner %d (%.1f,%.1f,%.1f)\n",
                      j, corners[j][0], corners[j][1], corners[j][2]);
          return false;
        }
      }
    }

    // Write the face (two triangles)
    // Reconstruct corners for area check with consistent winding order
    // All faces use counter-clockwise winding when viewed from outside the voxel
    float tri_corners[4][3];
    if (quad->face == 0) // +Z face (top)
    {
      // Counter-clockwise when viewed from +Z (above) - looking down
      tri_corners[0][0] = (float)quad->x0;
      tri_corners[0][1] = (float)quad->y1;
      tri_corners[0][2] = (float)quad->z1;
      tri_corners[1][0] = (float)quad->x1;
      tri_corners[1][1] = (float)quad->y1;
      tri_corners[1][2] = (float)quad->z1;
      tri_corners[2][0] = (float)quad->x1;
      tri_corners[2][1] = (float)quad->y0;
      tri_corners[2][2] = (float)quad->z1;
      tri_corners[3][0] = (float)quad->x0;
      tri_corners[3][1] = (float)quad->y0;
      tri_corners[3][2] = (float)quad->z1;
    }
    else if (quad->face == 1) // -Z face (bottom)
    {
      // Counter-clockwise when viewed from -Z (below) - looking up
      tri_corners[0][0] = (float)quad->x0;
      tri_corners[0][1] = (float)quad->y1;
      tri_corners[0][2] = (float)quad->z0;
      tri_corners[1][0] = (float)quad->x1;
      tri_corners[1][1] = (float)quad->y1;
      tri_corners[1][2] = (float)quad->z0;
      tri_corners[2][0] = (float)quad->x1;
      tri_corners[2][1] = (float)quad->y0;
      tri_corners[2][2] = (float)quad->z0;
      tri_corners[3][0] = (float)quad->x0;
      tri_corners[3][1] = (float)quad->y0;
      tri_corners[3][2] = (float)quad->z0;
    }
    else if (quad->face == 2) // +Y face (back)
    {
      // Counter-clockwise when viewed from +Y (behind) - looking forward
      tri_corners[0][0] = (float)quad->x0;
      tri_corners[0][1] = (float)quad->y1;
      tri_corners[0][2] = (float)quad->z0;
      tri_corners[1][0] = (float)quad->x0;
      tri_corners[1][1] = (float)quad->y1;
      tri_corners[1][2] = (float)quad->z1;
      tri_corners[2][0] = (float)quad->x1;
      tri_corners[2][1] = (float)quad->y1;
      tri_corners[2][2] = (float)quad->z1;
      tri_corners[3][0] = (float)quad->x1;
      tri_corners[3][1] = (float)quad->y1;
      tri_corners[3][2] = (float)quad->z0;
    }
    else if (quad->face == 4) // -Y face (front)
    {
      // Counter-clockwise when viewed from -Y (in front) - looking backward
      tri_corners[0][0] = (float)quad->x0;
      tri_corners[0][1] = (float)quad->y0;
      tri_corners[0][2] = (float)quad->z0;
      tri_corners[1][0] = (float)quad->x1;
      tri_corners[1][1] = (float)quad->y0;
      tri_corners[1][2] = (float)quad->z0;
      tri_corners[2][0] = (float)quad->x1;
      tri_corners[2][1] = (float)quad->y0;
      tri_corners[2][2] = (float)quad->z1;
      tri_corners[3][0] = (float)quad->x0;
      tri_corners[3][1] = (float)quad->y0;
      tri_corners[3][2] = (float)quad->z1;
    }
    else if (quad->face == 3) // +X face (right)
    {
      // Counter-clockwise when viewed from +X (right side) - looking left
      tri_corners[0][0] = (float)quad->x1;
      tri_corners[0][1] = (float)quad->y0;
      tri_corners[0][2] = (float)quad->z0;
      tri_corners[1][0] = (float)quad->x1;
      tri_corners[1][1] = (float)quad->y1;
      tri_corners[1][2] = (float)quad->z0;
      tri_corners[2][0] = (float)quad->x1;
      tri_corners[2][1] = (float)quad->y1;
      tri_corners[2][2] = (float)quad->z1;
      tri_corners[3][0] = (float)quad->x1;
      tri_corners[3][1] = (float)quad->y0;
      tri_corners[3][2] = (float)quad->z1;
    }
    else
    { // face 5 (-X face, left)
      // Counter-clockwise when viewed from -X (left side) - looking right
      tri_corners[0][0] = (float)quad->x0;
      tri_corners[0][1] = (float)quad->y0;
      tri_corners[0][2] = (float)quad->z0;
      tri_corners[1][0] = (float)quad->x0;
      tri_corners[1][1] = (float)quad->y0;
      tri_corners[1][2] = (float)quad->z1;
      tri_corners[2][0] = (float)quad->x0;
      tri_corners[2][1] = (float)quad->y1;
      tri_corners[2][2] = (float)quad->z1;
      tri_corners[3][0] = (float)quad->x0;
      tri_corners[3][1] = (float)quad->y1;
      tri_corners[3][2] = (float)quad->z0;
    }
    // Compute triangle areas via cross product magnitude
    // Triangle 1: corners 0, 1, 2
    float ax1 = tri_corners[1][0] - tri_corners[0][0];
    float ay1 = tri_corners[1][1] - tri_corners[0][1];
    float az1 = tri_corners[1][2] - tri_corners[0][2];
    float bx1 = tri_corners[2][0] - tri_corners[0][0];
    float by1 = tri_corners[2][1] - tri_corners[0][1];
    float bz1 = tri_corners[2][2] - tri_corners[0][2];

    // Triangle 2: corners 0, 2, 3
    float ax2 = tri_corners[2][0] - tri_corners[0][0];
    float ay2 = tri_corners[2][1] - tri_corners[0][1];
    float az2 = tri_corners[2][2] - tri_corners[0][2];
    float bx2 = tri_corners[3][0] - tri_corners[0][0];
    float by2 = tri_corners[3][1] - tri_corners[0][1];
    float bz2 = tri_corners[3][2] - tri_corners[0][2];

    // Cross products for each triangle
    float cross1x = ay1 * bz1 - az1 * by1;
    float cross1y = az1 * bx1 - ax1 * bz1;
    float cross1z = ax1 * by1 - ay1 * bx1;
    float cross2x = ay2 * bz2 - az2 * by2;
    float cross2y = az2 * bx2 - ax2 * bz2;
    float cross2z = ax2 * by2 - ay2 * bx2;

    float area1sq = cross1x * cross1x + cross1y * cross1y + cross1z * cross1z;
    float area2sq = cross2x * cross2x + cross2y * cross2y + cross2z * cross2z;

    if (area1sq < 1e-6f || area2sq < 1e-6f)
    {
      // Skip degenerate quad
      DEBUG_PRINT("[OBJ] Skipping degenerate quad %d (zero-area triangle)\n", i);
      continue;
    }
    fprintf(file, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", vertex_indices[0], i + 1, i + 1, vertex_indices[1], i + 1, i + 1, vertex_indices[2], i + 1, i + 1);
    fprintf(file, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", vertex_indices[0], i + 1, i + 1, vertex_indices[2], i + 1, i + 1, vertex_indices[3], i + 1, i + 1);

    DEBUG_PRINT("[OBJ] Face %d: triangles (%d,%d,%d) and (%d,%d,%d)\n",
                i, vertex_indices[0], vertex_indices[1], vertex_indices[2],
                vertex_indices[0], vertex_indices[2], vertex_indices[3]);
  }

  free(unique_vertices);
  free(unique_normals);
  fclose(file);

  DEBUG_PRINT("[OBJ] Export complete: %d vertices, %d triangles\n", unique_count, mesh->count * 2);
  return true;
}
