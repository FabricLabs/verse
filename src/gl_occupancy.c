#include "gl_occupancy.h"

#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_opengl.h>

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif

void gl_occupancy_init(GlOccupancyTexture *tex)
{
  if (!tex)
    return;
  memset(tex, 0, sizeof(*tex));
}

void gl_occupancy_destroy(GlOccupancyTexture *tex)
{
  if (!tex)
    return;
  if (tex->tex)
  {
    GLuint id = (GLuint)tex->tex;
    glDeleteTextures(1, &id);
  }
  memset(tex, 0, sizeof(*tex));
}

bool gl_occupancy_upload(GlOccupancyTexture *tex, const GpuVoxelBuffer *buf, uint64_t revision)
{
  if (!tex || !buf || !buf->bits || buf->width == 0 || buf->height == 0 || buf->depth == 0)
    return false;
  if (!SDL_GL_GetCurrentContext())
    return false;

  const uint32_t w = buf->width;
  const uint32_t h = buf->height;
  const uint32_t d = buf->depth;
  const uint64_t total = (uint64_t)w * (uint64_t)h * (uint64_t)d;
  if (total > 64ull * 1024ull * 1024ull) // hard cap 64M cells (~64 MB R8)
    return false;

  uint8_t *r8 = (uint8_t *)malloc((size_t)total);
  if (!r8)
    return false;
  for (uint64_t i = 0; i < total; i++)
    r8[i] = (buf->bits[i >> 3] & (uint8_t)(1u << (i & 7u))) ? 255u : 0u;

  GLuint id = (GLuint)tex->tex;
  if (!id)
  {
    glGenTextures(1, &id);
    if (!id)
    {
      free(r8);
      return false;
    }
    tex->tex = (uint32_t)id;
  }

  glBindTexture(GL_TEXTURE_3D, id);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  // Prefer sized R8 when available; fall back to luminance-style RED upload.
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, (GLsizei)w, (GLsizei)h, (GLsizei)d, 0, GL_RED,
               GL_UNSIGNED_BYTE, r8);
  const GLenum err = glGetError();
  if (err != GL_NO_ERROR)
  {
    // Older macOS OpenGL may reject GL_R8; retry with GL_LUMINANCE if defined.
#ifdef GL_LUMINANCE
    glTexImage3D(GL_TEXTURE_3D, 0, GL_LUMINANCE, (GLsizei)w, (GLsizei)h, (GLsizei)d, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, r8);
#else
    free(r8);
    glBindTexture(GL_TEXTURE_3D, 0);
    return false;
#endif
  }

  free(r8);
  glBindTexture(GL_TEXTURE_3D, 0);
  tex->width = w;
  tex->height = h;
  tex->depth = d;
  tex->revision = revision;
  return true;
}

void gl_occupancy_bind(const GlOccupancyTexture *tex, unsigned unit)
{
  if (!tex || !tex->tex)
    return;
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_3D, (GLuint)tex->tex);
}

bool gl_occupancy_consume_bind(const GlOccupancyTexture *tex)
{
  if (!tex || !tex->tex || !SDL_GL_GetCurrentContext())
    return false;
  gl_occupancy_bind(tex, 0);
  // A future deferred lighting pass samples this unit for sun/AO DDA. Unbind is left to the
  // caller so multi-pass consumers can keep the volume resident across draws.
  return true;
}
