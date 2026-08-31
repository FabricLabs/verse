#ifndef LIGHTNING_PATH_H
#define LIGHTNING_PATH_H

#include <stdint.h>

// Jagged 3D polylines for volcano conduits and storm bolts.
//
// Both features need the same visual grammar: a mostly-directed stroke that takes short lateral
// jags rather than a straight Bresenham line. One seeded generator keeps a vent and a lightning
// strike looking related when they share a world.

#define LIGHTNING_PATH_MAX_POINTS 512

typedef struct
{
  int16_t x, y, z;
} LightningPathPoint;

// Fill out[0..*out_count) with a jagged path from (x0,y0,z0) to (x1,y1,z1).
// Returns the number of points written (at least 2 on success, 0 on bad args).
// Deterministic for a given seed and endpoints. Lateral jitter scales with segment length.
int lightning_path_generate(uint32_t seed, int x0, int y0, int z0, int x1, int y1, int z1,
                            LightningPathPoint *out, int max_out);

#endif // LIGHTNING_PATH_H
