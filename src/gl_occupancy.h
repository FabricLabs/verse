#ifndef VERSE_GL_OCCUPANCY_H
#define VERSE_GL_OCCUPANCY_H

#include <stdbool.h>
#include <stdint.h>

#include "gpu_voxel_buffer.h"

// GL_TEXTURE_3D occupancy for a future fullscreen GPU lighting pass. Live FP lighting is still
// CPU/ShadowWorld (fixed-function SDL GL has no deferred shader path). Upload is gated behind
// FP_GPU_OCCUPANCY=1 / fp_renderer_set_gpu_occupancy. A GLSL consumer would:
//   reconstruct world pos from depth, DDA the R8 volume toward the sun + hemi, write irradiance.
// Requires a current OpenGL context. Safe to call when none exists — returns false.

typedef struct GlOccupancyTexture {
  uint32_t tex; // GLuint
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint64_t revision; // last uploaded world->voxel_revision (caller-owned stamp)
} GlOccupancyTexture;

void gl_occupancy_init(GlOccupancyTexture *tex);
void gl_occupancy_destroy(GlOccupancyTexture *tex);

// Expand bit-packed occupancy to R8 (0/255) and upload as GL_TEXTURE_3D NEAREST. Creates the
// texture on first success. Returns false if buf is empty or no GL context / GL_TEXTURE_3D.
bool gl_occupancy_upload(GlOccupancyTexture *tex, const GpuVoxelBuffer *buf, uint64_t revision);

// Bind to texture unit (0..N). No-op if tex is empty.
void gl_occupancy_bind(const GlOccupancyTexture *tex, unsigned unit);

// Lightweight consumer: binds the occupancy volume and leaves it on unit 0 for a subsequent
// fullscreen pass / shader. Returns true when a texture is live. Fixed-function SDL has no
// deferred shader yet; this still proves the upload path and keeps the sampler warm.
bool gl_occupancy_consume_bind(const GlOccupancyTexture *tex);

#endif // VERSE_GL_OCCUPANCY_H
