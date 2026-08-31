#include "lightning_path.h"

#include <stdlib.h>

// Tiny deterministic LCG — same family as other world-local RNGs, kept private so path seeds do
// not collide with world->rng_state.
static uint32_t lightning_rng_next(uint32_t *state)
{
  *state = *state * 1664525u + 1013904223u;
  return *state;
}

static int lightning_rng_signed(uint32_t *state, int amp)
{
  if (amp <= 0)
    return 0;
  const uint32_t r = lightning_rng_next(state);
  return (int)(r % (uint32_t)(2 * amp + 1)) - amp;
}

static int lightning_iabs(int v)
{
  return v < 0 ? -v : v;
}

int lightning_path_generate(uint32_t seed, int x0, int y0, int z0, int x1, int y1, int z1,
                            LightningPathPoint *out, int max_out)
{
  if (!out || max_out < 2)
    return 0;

  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int dz = z1 - z0;
  const int adx = lightning_iabs(dx);
  const int ady = lightning_iabs(dy);
  const int adz = lightning_iabs(dz);
  int major = adx;
  if (ady > major)
    major = ady;
  if (adz > major)
    major = adz;
  if (major < 1)
  {
    out[0] = (LightningPathPoint){(int16_t)x0, (int16_t)y0, (int16_t)z0};
    out[1] = (LightningPathPoint){(int16_t)x1, (int16_t)y1, (int16_t)z1};
    return 2;
  }

  // One sample per major-axis step, capped so a world-tall vent still fits the buffer.
  int steps = major;
  if (steps + 1 > max_out)
    steps = max_out - 1;

  uint32_t rng = seed ? seed : 1u;
  // Jitter grows with path length but stays small enough that a vent still reaches the surface
  // column neighbourhood rather than wandering off into a neighbouring biome.
  int jitter = 1 + major / 16;
  if (jitter > 4)
    jitter = 4;

  int count = 0;
  int px = x0, py = y0, pz = z0;
  out[count++] = (LightningPathPoint){(int16_t)px, (int16_t)py, (int16_t)pz};

  for (int i = 1; i <= steps; i++)
  {
    // Linear interpolation toward the target, then a lateral jag that prefers the two axes that
    // are not the dominant rise (for a vertical vent that means x/y wiggles).
    const float t = (float)i / (float)steps;
    int nx = x0 + (int)((float)dx * t + (dx >= 0 ? 0.5f : -0.5f));
    int ny = y0 + (int)((float)dy * t + (dy >= 0 ? 0.5f : -0.5f));
    int nz = z0 + (int)((float)dz * t + (dz >= 0 ? 0.5f : -0.5f));

    if (i < steps)
    {
      if (adz >= adx && adz >= ady)
      {
        nx += lightning_rng_signed(&rng, jitter);
        ny += lightning_rng_signed(&rng, jitter);
      }
      else if (adx >= ady)
      {
        ny += lightning_rng_signed(&rng, jitter);
        nz += lightning_rng_signed(&rng, jitter);
      }
      else
      {
        nx += lightning_rng_signed(&rng, jitter);
        nz += lightning_rng_signed(&rng, jitter);
      }
    }
    else
    {
      nx = x1;
      ny = y1;
      nz = z1;
    }

    // Fill 26-connected gaps so the conduit / bolt has no holes for fluid or visuals to fall through.
    while (count < max_out && (px != nx || py != ny || pz != nz))
    {
      if (px != nx)
        px += (nx > px) ? 1 : -1;
      else if (py != ny)
        py += (ny > py) ? 1 : -1;
      else
        pz += (nz > pz) ? 1 : -1;
      out[count++] = (LightningPathPoint){(int16_t)px, (int16_t)py, (int16_t)pz};
      if (count >= max_out)
        break;
    }
  }

  // Guarantee the exact endpoint even if jitter rounding drifted.
  if (count > 0)
  {
    out[count - 1].x = (int16_t)x1;
    out[count - 1].y = (int16_t)y1;
    out[count - 1].z = (int16_t)z1;
  }
  return count;
}
