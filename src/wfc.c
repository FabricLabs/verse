#include "wfc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline uint32_t pcg32(uint32_t* state) {
  uint32_t x = *state;
  uint32_t count = x >> 28; // 4 most significant bits for rotation count
  *state = x * 747796405u + 2891336453u;
  uint32_t word = ((*state >> ((x >> 22) + 1)) ^ *state) * 277803737u;
  word ^= word >> 22;
  return word;
}

bool wfc_model_build(WfcModel* model, const WfcTile* tiles, int tile_count) {
  if (!model || !tiles || tile_count <= 0) return false;
  memset(model, 0, sizeof(*model));
  model->tiles = tiles;
  model->tile_count = tile_count;
  for (int d = 0; d < 4; d++) {
    model->allowed[d] = (bool*)calloc((size_t)tile_count * (size_t)tile_count, sizeof(bool));
    if (!model->allowed[d]) return false;
  }
  for (int a = 0; a < tile_count; a++) {
    for (int b = 0; b < tile_count; b++) {
      // North edge of a must equal South edge of b to place b north of a, etc.
      bool n_ok = tiles[a].edge[WFC_DIR_NORTH] == tiles[b].edge[WFC_DIR_SOUTH];
      bool s_ok = tiles[a].edge[WFC_DIR_SOUTH] == tiles[b].edge[WFC_DIR_NORTH];
      bool e_ok = tiles[a].edge[WFC_DIR_EAST] == tiles[b].edge[WFC_DIR_WEST];
      bool w_ok = tiles[a].edge[WFC_DIR_WEST] == tiles[b].edge[WFC_DIR_EAST];
      model->allowed[WFC_DIR_NORTH][a * tile_count + b] = n_ok;
      model->allowed[WFC_DIR_SOUTH][a * tile_count + b] = s_ok;
      model->allowed[WFC_DIR_EAST][a * tile_count + b] = e_ok;
      model->allowed[WFC_DIR_WEST][a * tile_count + b] = w_ok;
    }
  }
  return true;
}

void wfc_model_free(WfcModel* model) {
  if (!model) return;
  for (int d = 0; d < 4; d++) {
    if (model->allowed[d]) free(model->allowed[d]);
    model->allowed[d] = NULL;
  }
  model->tiles = NULL;
  model->tile_count = 0;
}

typedef struct {
  // Bitset of possible tiles per cell; 1 means allowed.
  unsigned long* bits; // size = ceil(tile_count / (8*sizeof(unsigned long))) per cell
  int words_per_cell;
} Domain;

static inline void domain_init(Domain* dom, int cells, int tile_count) {
  int bits = tile_count;
  int word_bits = (int)(sizeof(unsigned long) * 8);
  dom->words_per_cell = (bits + word_bits - 1) / word_bits;
  dom->bits = (unsigned long*)malloc((size_t)cells * (size_t)dom->words_per_cell * sizeof(unsigned long));
}

static inline void domain_free(Domain* dom) { if (dom && dom->bits) free(dom->bits); }

static inline void domain_set_all(Domain* dom, int idx, int tile_count) {
  for (int w = 0; w < dom->words_per_cell; w++) dom->bits[idx * dom->words_per_cell + w] = ~0UL;
  // clear extra bits beyond tile_count
  int excess = dom->words_per_cell * (int)(sizeof(unsigned long) * 8) - tile_count;
  if (excess > 0) dom->bits[idx * dom->words_per_cell + dom->words_per_cell - 1] >>= excess, dom->bits[idx * dom->words_per_cell + dom->words_per_cell - 1] <<= excess;
}

static inline int domain_count(const Domain* dom, int idx) {
  int c = 0;
  for (int w = 0; w < dom->words_per_cell; w++) c += __builtin_popcountl(dom->bits[idx * dom->words_per_cell + w]);
  return c;
}

static inline int domain_first(const Domain* dom, int idx) {
  for (int w = 0; w < dom->words_per_cell; w++) {
    unsigned long v = dom->bits[idx * dom->words_per_cell + w];
    if (v) return w * (int)(sizeof(unsigned long) * 8) + __builtin_ctzl(v);
  }
  return -1;
}

static inline bool domain_has(const Domain* dom, int idx, int t) {
  int w = t / (int)(sizeof(unsigned long) * 8);
  int b = t % (int)(sizeof(unsigned long) * 8);
  return (dom->bits[idx * dom->words_per_cell + w] >> b) & 1UL;
}

static inline void domain_clear(Domain* dom, int idx, int t) {
  int w = t / (int)(sizeof(unsigned long) * 8);
  int b = t % (int)(sizeof(unsigned long) * 8);
  dom->bits[idx * dom->words_per_cell + w] &= ~(1UL << b);
}

static inline float tile_weight(const WfcTile* tiles, int t) {
  float w = tiles[t].weight;
  return (w > 0.0f) ? w : 1.0f;
}

static int pick_weighted(const WfcTile* tiles, uint32_t* rng, const Domain* dom, int idx, int tile_count) {
  // compute total weight
  float total = 0.0f;
  for (int t = 0; t < tile_count; t++) if (domain_has(dom, idx, t)) total += tile_weight(tiles, t);
  if (total <= 0.0f) return -1;
  float r = (pcg32(rng) / (float)UINT32_MAX) * total;
  for (int t = 0; t < tile_count; t++) if (domain_has(dom, idx, t)) {
    float w = tile_weight(tiles, t);
    if (r <= w) return t;
    r -= w;
  }
  return domain_first(dom, idx);
}

static bool propagate(const WfcModel* model, Domain* dom, int width, int height, int start_idx) {
  int tile_count = model->tile_count;
  int capacity = width * height;
  int* queue = (int*)malloc((size_t)capacity * sizeof(int));
  int qh = 0, qt = 0;
  queue[qt++] = start_idx;
  while (qh < qt) {
    int idx = queue[qh++];
    int x = idx % width;
    int y = idx / width;
    for (int dir = 0; dir < 4; dir++) {
      int nx = x + (dir == WFC_DIR_EAST ? 1 : dir == WFC_DIR_WEST ? -1 : 0);
      int ny = y + (dir == WFC_DIR_SOUTH ? 1 : dir == WFC_DIR_NORTH ? -1 : 0);
      if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
      int nidx = ny * width + nx;
      // Build allowed set for neighbor based on current cell possibilities
      // A neighbor tile b is allowed if exists any a in current that allows b
      unsigned long before_words[16];
      if (dom->words_per_cell > 16) { free(queue); return false; }
      for (int w = 0; w < dom->words_per_cell; w++) before_words[w] = dom->bits[nidx * dom->words_per_cell + w];
      // compute new mask for neighbor
      // initialize to zero
      for (int w = 0; w < dom->words_per_cell; w++) dom->bits[nidx * dom->words_per_cell + w] = 0UL;
      for (int b = 0; b < tile_count; b++) {
        // neighbor candidate b accumulates if any a supports it
        bool allowed_any = false;
        for (int a = 0; a < tile_count && !allowed_any; a++) if (domain_has(dom, idx, a)) {
          if (model->allowed[dir][a * tile_count + b]) allowed_any = true;
        }
        if (allowed_any) {
          int w = b / (int)(sizeof(unsigned long) * 8);
          int bit = b % (int)(sizeof(unsigned long) * 8);
          dom->bits[nidx * dom->words_per_cell + w] |= (1UL << bit);
        }
      }
      // if changed, enqueue neighbor
      bool changed = false;
      for (int w = 0; w < dom->words_per_cell; w++) if (dom->bits[nidx * dom->words_per_cell + w] != before_words[w]) { changed = true; break; }
      if (changed) queue[qt++] = nidx;
    }
  }
  free(queue);
  return true;
}

bool wfc_solve(const WfcModel* model, int width, int height, uint32_t seed, int max_restarts, int* out_grid) {
  if (!model || !out_grid || width <= 0 || height <= 0) return false;
  int cells = width * height;
  uint32_t rng = seed ? seed : 1u;
  for (int attempt = 0; attempt < (max_restarts > 0 ? max_restarts : 1); attempt++) {
    Domain dom = {0};
    domain_init(&dom, cells, model->tile_count);
    for (int i = 0; i < cells; i++) domain_set_all(&dom, i, model->tile_count);
    bool failed = false;
    for (int remaining = cells; remaining > 0; remaining--) {
      // pick cell with minimum entropy (>1 candidates). Simple scan.
      int best_idx = -1;
      int best_count = 1<<30;
      for (int i = 0; i < cells; i++) {
        int c = domain_count(&dom, i);
        if (c > 1 && c < best_count) { best_count = c; best_idx = i; }
      }
      if (best_idx == -1) {
        // all cells fixed or some zero-domain
        // Check for zero-domain
        for (int i = 0; i < cells; i++) if (domain_count(&dom, i) == 0) { failed = true; break; }
        break;
      }
      // collapse: choose weighted tile and set domain to singleton
      int choice = pick_weighted(model->tiles, &rng, &dom, best_idx, model->tile_count);
      if (choice < 0) { failed = true; break; }
      // clear all but choice
      for (int t = 0; t < model->tile_count; t++) if (t != choice && domain_has(&dom, best_idx, t)) domain_clear(&dom, best_idx, t);
      // propagate constraints
      if (!propagate(model, &dom, width, height, best_idx)) { failed = true; break; }
    }
    if (!failed) {
      // materialize
      for (int i = 0; i < cells; i++) {
        int t = domain_first(&dom, i);
        if (t < 0) { failed = true; break; }
        out_grid[i] = t;
      }
      domain_free(&dom);
      if (!failed) return true;
    } else {
      domain_free(&dom);
    }
    // new seed per restart
    rng = pcg32(&rng);
  }
  return false;
}

void wfc_build_default_town_model(WfcModel* model) {
  static WfcTile tiles[6];
  // Edge IDs: 0=grass, 1=road, 2=lot, 3=wall
  tiles[0] = (WfcTile){"grass", {0,0,0,0}, 3.0f};
  tiles[1] = (WfcTile){"road_ns", {1,0,1,0}, 1.5f};
  tiles[2] = (WfcTile){"road_ew", {0,1,0,1}, 1.5f};
  tiles[3] = (WfcTile){"intersection", {1,1,1,1}, 0.8f};
  tiles[4] = (WfcTile){"lot", {2,2,2,2}, 1.2f};
  tiles[5] = (WfcTile){"wall", {3,3,3,3}, 0.6f};
  wfc_model_build(model, tiles, 6);
}


