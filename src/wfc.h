#ifndef WFC_H
#define WFC_H

#include <stdbool.h>
#include <stdint.h>

// Cardinal directions
typedef enum {
  WFC_DIR_NORTH = 0,
  WFC_DIR_EAST = 1,
  WFC_DIR_SOUTH = 2,
  WFC_DIR_WEST = 3
} WfcDir;

// Tile definition with simple edge matching (equal-edge IDs match)
typedef struct {
  const char* name;
  // Edge categories for N,E,S,W. Matching requires equality.
  uint8_t edge[4];
  // Optional weight for stochastic choice
  float weight;
} WfcTile;

// WFC model: tileset and precomputed adjacency permission matrix per direction
typedef struct {
  const WfcTile* tiles;
  int tile_count;
  // allowed[dir][a][b] == true if tile a may have neighbor b in direction dir
  bool* allowed[4]; // flattened tile_count x tile_count arrays
} WfcModel;

// Build a model with equality edge-matching (allowed if edges equal). Returns true on success.
bool wfc_model_build(WfcModel* model, const WfcTile* tiles, int tile_count);
void wfc_model_free(WfcModel* model);

// Solve a width x height grid. out_grid must have size width*height and will contain tile indices [0..tile_count-1].
// Returns true on success. Uses seed for random tie breaks.
bool wfc_solve(const WfcModel* model, int width, int height, uint32_t seed, int max_restarts, int* out_grid);

// Convenience: provide a tiny default "town" model with grass, roads, intersections, building lots.
// The model memory is owned by caller; tiles array must live as long as model.
void wfc_build_default_town_model(WfcModel* model);

#endif // WFC_H


