#ifndef WFC3D_H
#define WFC3D_H

#include <stdbool.h>
#include <stdint.h>

// 3D WFC directions
typedef enum {
  WFC3_DIR_NORTH = 0,
  WFC3_DIR_EAST  = 1,
  WFC3_DIR_SOUTH = 2,
  WFC3_DIR_WEST  = 3,
  WFC3_DIR_UP    = 4,
  WFC3_DIR_DOWN  = 5
} Wfc3Dir;

// 3D tile with socket edges for 6 directions
typedef struct {
  const char* name;     // human-readable
  uint8_t edge[6];      // socket id per direction; equality means compatible
  float weight;         // optional weight (>=0). 0 => treat as 1
} Wfc3Tile;

// 3D model: precomputed allowed adjacency per direction
typedef struct {
  const Wfc3Tile* tiles;
  int tile_count;
  // allowed[dir][a * tile_count + b] => b is allowed neighbor of a in 'dir'
  bool* allowed[6];
} Wfc3Model;

// Build adjacency with equality matching on socket ids
bool wfc3_model_build(Wfc3Model* model, const Wfc3Tile* tiles, int tile_count);
void wfc3_model_free(Wfc3Model* model);

// Convenience: build the "gameworld" tileset/model capturing vertical stack rules:
// - Flat plane of wilderness horizontally
// - Above wilderness: cloud
// - Below wilderness: bedrock (upper)
// - Below bedrock(upper): underworld
// - Below underworld: bedrock (lower)
// Horizontal adjacency is restricted to same-type tiles within each layer.
void wfc3_build_gameworld_model(Wfc3Model* model);

#endif // WFC3D_H


