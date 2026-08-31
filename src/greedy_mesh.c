#include "greedy_mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
  float x, y, z;
} Vec3;

static void append_str(char** buf, size_t* cap, size_t* len, const char* s) {
  size_t sl = strlen(s);
  if (*len + sl + 1 > *cap) {
    size_t nc = (*cap == 0) ? 4096 : (*cap * 2);
    while (nc < *len + sl + 1) nc *= 2;
    char* nb = (char*)realloc(*buf, nc);
    if (!nb) return; *buf = nb; *cap = nc;
  }
  memcpy(*buf + *len, s, sl); *len += sl; (*buf)[*len] = '\0';
}

static void append_fmt(char** buf, size_t* cap, size_t* len, const char* fmt, ...) {
  va_list ap; va_start(ap, fmt);
  char tmp[256]; int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n <= 0) return; if ((size_t)n >= sizeof(tmp)) { n = (int)sizeof(tmp) - 1; }
  tmp[n] = '\0'; append_str(buf, cap, len, tmp);
}

// Axis-aligned face directions
static const int DIRS[6][3] = {
  { 1, 0, 0 }, {-1, 0, 0 },
  { 0, 1, 0 }, { 0,-1, 0 },
  { 0, 0, 1 }, { 0, 0,-1 }
};

// Greedy mesh per voxel type: for each axis, build maximal rectangles for visible faces
bool greedy_mesh_export_obj_per_voxel_type(
    const World* world,
    const char* out_base_path,
    int* out_model_count,
    size_t* out_total_quads) {
  if (!world || !out_base_path) return false;
  int models = 0; size_t total_quads = 0;

  // Track which voxel types are present
  bool present[VOXEL_COUNT]; memset(present, 0, sizeof(present));
  for (uint32_t z = 0; z < world->depth; z++)
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++) {
        const Voxel* v = world_get_voxel((World*)world, x,y,z);
        if (v && v->type != VOXEL_AIR && v->type < VOXEL_COUNT) present[v->type] = true;
      }

  for (int t = 0; t < (int)VOXEL_COUNT; t++) {
    if (!present[t]) continue;
    const char* tname = world_voxel_type_name((VoxelType)t);
    char out_path[1024];
    snprintf(out_path, sizeof(out_path), "%s.%s.obj", out_base_path, tname);
    FILE* check = fopen(out_path, "rb");
    if (check) { fclose(check); continue; }

    char* obj = NULL; size_t cap = 0, len = 0;
    append_str(&obj, &cap, &len, "# Greedy mesh export\n");
    size_t vertex_base = 1; // OBJ 1-based
    size_t quad_count = 0;

    // For each axis (0=x,1=y,2=z), sweep and merge quads
    for (int axis = 0; axis < 3; axis++) {
      int u = (axis + 1) % 3;
      int vA = (axis + 2) % 3;
      uint32_t dims[3] = { world->width, world->height, world->depth };
      uint32_t A = dims[axis], U = dims[u], V = dims[vA];

      // For each layer along axis
      for (uint32_t a = 0; a <= A; a++) {
        // Build a visibility mask for faces at this interface (between a-1 and a)
        // mask[U][V]: 1 if face exists and belongs to type t; also track normal sign
        uint8_t* mask = (uint8_t*)calloc((size_t)U * (size_t)V, 1);
        int normal_sign = 0; // +1 for forward, -1 for backward; we'll generate both in one go
        // Fill mask for forward faces at plane a (face normal +axis)
        for (uint32_t uu = 0; uu < U; uu++) {
          for (uint32_t vv = 0; vv < V; vv++) {
            int xyz0[3] = {0,0,0}; int xyz1[3] = {0,0,0}; xyz0[axis]=(int)a-1; xyz1[axis]=(int)a;
            xyz0[u]=(int)uu; xyz0[vA]=(int)vv; xyz1[u]=(int)uu; xyz1[vA]=(int)vv;
            const Voxel* left = (xyz0[axis] >= 0) && world_is_position_valid((World*)world, xyz0[0], xyz0[1], xyz0[2])
                                ? world_get_voxel((World*)world, xyz0[0], xyz0[1], xyz0[2]) : NULL;
            const Voxel* right= (xyz1[axis] < (int)dims[axis]) && world_is_position_valid((World*)world, xyz1[0], xyz1[1], xyz1[2])
                                ? world_get_voxel((World*)world, xyz1[0], xyz1[1], xyz1[2]) : NULL;
            bool left_solid = left && left->type != VOXEL_AIR;
            bool right_solid= right && right->type != VOXEL_AIR;
            VoxelType left_t = left_solid ? left->type : VOXEL_AIR;
            VoxelType right_t= right_solid? right->type: VOXEL_AIR;
            // A face exists if one side solid of type t and the other side not the same type
            bool face_fwd = (left_t == (VoxelType)t) && (!right_solid || right_t != (VoxelType)t);
            bool face_bwd = (right_t == (VoxelType)t) && (!left_solid || left_t != (VoxelType)t);
            if (face_fwd || face_bwd) mask[uu*V + vv] = (uint8_t)(face_fwd ? 1 : 2);
          }
        }

        // Greedy merge rectangles in mask for forward and backward separately
        for (int pass = 1; pass <= 2; pass++) {
          uint8_t wanted = (uint8_t)pass;
          for (uint32_t uu = 0; uu < U; ) {
            for (uint32_t vv = 0; vv < V; ) {
              if (mask[uu*V + vv] != wanted) { vv++; continue; }
              // Determine max width
              uint32_t w = 1;
              while (vv + w < V && mask[uu*V + (vv + w)] == wanted) w++;
              // Determine max height
              uint32_t h = 1; bool expand = true;
              while (uu + h < U && expand) {
                for (uint32_t k = 0; k < w; k++) {
                  if (mask[(uu + h)*V + (vv + k)] != wanted) { expand = false; break; }
                }
                if (expand) h++;
              }
              // Emit quad (uu..uu+h, vv..vv+w) on plane a with orientation
              int sign = (wanted == 1) ? +1 : -1;
              // Compute 4 corners in xyz
              int xyz[4][3];
              for (int c = 0; c < 4; c++) { xyz[c][0]=xyz[c][1]=xyz[c][2]=0; }
              // Base corner
              xyz[0][axis] = (int)a - (sign > 0 ? 0 : 1);
              xyz[0][u] = (int)uu; xyz[0][vA] = (int)vv;
              xyz[1][axis] = xyz[0][axis]; xyz[1][u] = (int)uu + (int)h; xyz[1][vA] = (int)vv;
              xyz[2][axis] = xyz[0][axis]; xyz[2][u] = (int)uu + (int)h; xyz[2][vA] = (int)vv + (int)w;
              xyz[3][axis] = xyz[0][axis]; xyz[3][u] = (int)uu;           xyz[3][vA] = (int)vv + (int)w;

              // Convert to float vertices and write to OBJ
              for (int c = 0; c < 4; c++) {
                append_fmt(&obj, &cap, &len, "v %d %d %d\n", xyz[c][0], xyz[c][1], xyz[c][2]);
              }
              // Face order: ensure correct winding based on sign and axis
              if (sign > 0) {
                append_fmt(&obj, &cap, &len, "f %zu %zu %zu %zu\n", vertex_base, vertex_base+1, vertex_base+2, vertex_base+3);
              } else {
                append_fmt(&obj, &cap, &len, "f %zu %zu %zu %zu\n", vertex_base+3, vertex_base+2, vertex_base+1, vertex_base);
              }
              vertex_base += 4; quad_count++;

              // Clear mask region
              for (uint32_t ru = 0; ru < h; ru++) {
                for (uint32_t rv = 0; rv < w; rv++) {
                  mask[(uu + ru)*V + (vv + rv)] = 0;
                }
              }
              vv += w;
            }
            uu++;
          }
        }

        free(mask);
      }
    }

    // Write file
    FILE* out = fopen(out_path, "wb");
    if (out && obj) { fwrite(obj, 1, len, out); fclose(out); models++; total_quads += quad_count; }
    if (obj) free(obj);
  }

  if (out_model_count) *out_model_count = models;
  if (out_total_quads) *out_total_quads = total_quads;
  return true;
}


