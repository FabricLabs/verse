#include <inttypes.h>
#include "universe.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "wfc3d.h"
#include "world_entropy_generator.h"
#include "settlement.h"

static uint64_t mix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

static size_t hash_xyz(uint64_t x, uint64_t y, uint64_t z, size_t cap) {
  uint64_t h = mix64(x) ^ mix64(y) ^ mix64(z);
  return (size_t)(h % cap);
}

bool universe_init(Universe* u, const char* seed_hex, int affinity, uint32_t min_gap) {
  if (!u || !seed_hex) return false;
  memset(u, 0, sizeof(*u));
  strncpy(u->seed, seed_hex, 64); u->seed[64] = '\0';
  u->affinity = affinity;
  u->min_gap = min_gap;
  u->log = NULL;
  u->vector_clock = 0;
  u->history_event_count = 0;
  u->capacity = 1024; // initial
  u->entries = (struct Entry*)calloc(u->capacity, sizeof(*u->entries));
  if (!u->entries) return false;
  // Ensure storage dir and GLOBAL.lock exist
  const char* store_dir = "./stores/verse";
  struct stat st = {0};
  if (stat(store_dir, &st) == -1) {
    mkdir(store_dir, 0755);
  }
  // Initialize GLOBAL.lock with current unix time
  char lock_path[256];
  snprintf(lock_path, sizeof(lock_path), "%s/%s", store_dir, "GLOBAL.lock");
  FILE* lf = fopen(lock_path, "wb");
  if (lf) {
    time_t now = time(NULL);
    fprintf(lf, "%ld", (long)now);
    fclose(lf);
  }

  // record GENESIS and seed event (two events)
  // TODO: create a JSON patch for GENESIS
  universe_append_event(u, "GENESIS", NULL);
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"seed\":\"%s\"}", u->seed);
  universe_append_event(u, "SET_SEED", buf);

  chronicle_init(&u->chronicle);
  u->chronicle_ready = chronicle_generate(&u->chronicle, u->seed, CHRONICLE_YEARS_DEFAULT);
  return true;
}

void universe_free(Universe* u) {
  if (!u) return;
  free(u->entries); u->entries = NULL; u->capacity = u->count = 0;
  if (u->log) { free(u->log); u->log = NULL; }
}

static struct Entry* find_slot(Universe* u, uint64_t x, uint64_t y, uint64_t z) {
  size_t idx = hash_xyz(x,y,z,u->capacity);
  for (size_t i = 0; i < u->capacity; i++) {
    size_t p = (idx + i) % u->capacity;
    if (!u->entries[p].used) return &u->entries[p];
    if (u->entries[p].used && u->entries[p].x == x && u->entries[p].y == y && u->entries[p].z == z) return &u->entries[p];
  }
  return NULL;
}

static bool grow(Universe* u) {
  size_t newcap = u->capacity * 2;
  struct Entry* n = (struct Entry*)calloc(newcap, sizeof(*n));
  if (!n) return false;
  struct Entry* old = u->entries; size_t oldcap = u->capacity;
  u->entries = n; u->capacity = newcap; u->count = 0;
  for (size_t i = 0; i < oldcap; i++) {
    if (old[i].used) {
      struct Entry* s = find_slot(u, old[i].x, old[i].y, old[i].z);
      if (s) { *s = old[i]; }
    }
  }
  free(old);
  return true;
}

bool universe_has(const Universe* u, uint64_t x, uint64_t y, uint64_t z) {
  if (!u || !u->entries) return false;
  size_t idx = hash_xyz(x,y,z,u->capacity);
  for (size_t i = 0; i < u->capacity; i++) {
    size_t p = (idx + i) % u->capacity;
    if (!u->entries[p].used) return false;
    if (u->entries[p].x == x && u->entries[p].y == y && u->entries[p].z == z) return true;
  }
  return false;
}

World* universe_get(const Universe* u, uint64_t x, uint64_t y, uint64_t z) {
  if (!u || !u->entries) return NULL;
  size_t idx = hash_xyz(x,y,z,u->capacity);
  for (size_t i = 0; i < u->capacity; i++) {
    size_t p = (idx + i) % u->capacity;
    if (!u->entries[p].used) return NULL;
    if (u->entries[p].x == x && u->entries[p].y == y && u->entries[p].z == z) return u->entries[p].w;
  }
  return NULL;
}

bool universe_place(Universe* u, uint64_t x, uint64_t y, uint64_t z, World* w) {
  if (!u || !u->entries) return false;
  if (universe_has(u, x,y,z)) return false; // occupied
  if ((u->count + 1) * 2 > u->capacity) if (!grow(u)) return false;
  struct Entry* s = find_slot(u, x,y,z);
  if (!s) return false;
  s->x = x; s->y = y; s->z = z; s->w = w; s->used = true; u->count++;
  // log placement
  char patch[256];
  snprintf(patch, sizeof(patch), "{\"op\":\"add\",\"path\":\"/worlds/%llu_%llu_%llu\",\"value\":{\"seed_id\":\"%s\"}}",
           (unsigned long long)x, (unsigned long long)y, (unsigned long long)z, w ? w->seed_id : "");
  universe_append_event(u, "PLACE_WORLD", patch);
  return true;
}

bool universe_remove(Universe* u, uint64_t x, uint64_t y, uint64_t z, World** out_world) {
  if (out_world) *out_world = NULL;
  if (!u || !u->entries) return false;

  size_t idx = hash_xyz(x,y,z,u->capacity);
  size_t p = 0;
  bool found = false;
  for (size_t i = 0; i < u->capacity; i++) {
    p = (idx + i) % u->capacity;
    if (!u->entries[p].used) return false;
    if (u->entries[p].x == x && u->entries[p].y == y && u->entries[p].z == z) { found = true; break; }
  }
  if (!found) return false;

  if (out_world) *out_world = u->entries[p].w;

  // Backward-shift deletion. The table probes linearly and has no tombstones, so just clearing the
  // slot would cut the probe chain and hide every entry that hashed before it and landed after it.
  // Lifting out the rest of the cluster and re-placing it keeps every remaining key findable.
  u->entries[p].used = false;
  u->count--;

  size_t j = (p + 1) % u->capacity;
  while (u->entries[j].used) {
    struct Entry moved = u->entries[j];
    u->entries[j].used = false;
    u->count--;
    struct Entry* slot = find_slot(u, moved.x, moved.y, moved.z);
    if (slot) { *slot = moved; u->count++; }
    j = (j + 1) % u->capacity;
  }

  // Deliberately not logged. universe_append_event writes to GLOBAL.log on disk, and eviction is
  // routine housekeeping driven by where the player is standing rather than a change to the world.
  return true;
}

static uint64_t chebyshev(uint64_t ax, uint64_t ay, uint64_t az, uint64_t bx, uint64_t by, uint64_t bz) {
  uint64_t dx = (ax>bx)?(ax-bx):(bx-ax);
  uint64_t dy = (ay>by)?(ay-by):(by-ay);
  uint64_t dz = (az>bz)?(az-bz):(bz-az);
  uint64_t m = dx; if (dy>m) m=dy; if (dz>m) m=dz; return m;
}

bool universe_find_player_wilderness_location(const Universe* u,
                                              uint64_t* out_x, uint64_t* out_y, uint64_t* out_z) {
  if (!u || !out_x || !out_y || !out_z) return false;
  // start near origin and expand rings; step by affinity stride
  uint64_t r = 0;
  for (int ring = 0; ring < 1000; ring++) {
    r = (uint64_t)(ring * (u->affinity > 0 ? (uint64_t)u->affinity : 1));
    for (int dx = -(int)r; dx <= (int)r; dx++) {
      for (int dy = -(int)r; dy <= (int)r; dy++) {
        for (int dz = -(int)r; dz <= (int)r; dz++) {
          // only check shell
          if ((dx==-(int)r || dx==(int)r) || (dy==-(int)r || dy==(int)r) || (dz==-(int)r || dz==(int)r)) {
            uint64_t x = (uint64_t)((int64_t)0 + dx);
            uint64_t y = (uint64_t)((int64_t)0 + dy);
            uint64_t z = (uint64_t)((int64_t)0 + dz);
            if (universe_has(u,x,y,z)) continue;
            // enforce min_gap to all existing
            bool ok = true;
            size_t checked = 0;
            for (size_t i = 0; i < u->capacity; i++) {
              if (u->entries[i].used) {
                checked++;
                if (chebyshev(x,y,z, u->entries[i].x, u->entries[i].y, u->entries[i].z) < u->min_gap) { ok=false; break; }
              }
            }
            if (ok) { *out_x = x; *out_y = y; *out_z = z; return true; }
          }
        }
      }
    }
  }
  return false;
}

bool universe_place_adjacent(Universe* u,
                             uint64_t base_x, uint64_t base_y, uint64_t base_z,
                             int dx, int dy, int dz,
                             World* w) {
  uint64_t x = (uint64_t)((int64_t)base_x + dx);
  uint64_t y = (uint64_t)((int64_t)base_y + dy);
  uint64_t z = (uint64_t)((int64_t)base_z + dz);
  if (universe_has(u, x,y,z)) return false;
  return universe_place(u, x,y,z, w);
}

void universe_append_event(Universe* u, const char* type, const char* json_patch) {
  if (!u || !type) return;
  const char* patch = json_patch ? json_patch : "{}";
  char line[512];
  snprintf(line, sizeof(line), "EVENT type=%s data=%s", type, patch);
  size_t add = strlen(line) + 2;
  if (!u->log) {
    u->log = (char*)malloc(add);
    if (!u->log) return;
    strcpy(u->log, line); strcat(u->log, "\n");
  } else {
    size_t cur = strlen(u->log);
    char* nl = (char*)realloc(u->log, cur + add);
    if (!nl) return;
    u->log = nl; strcpy(u->log + cur, line); strcat(u->log + cur, "\n");
  }
  u->history_event_count++;
  u->vector_clock = u->history_event_count;

  // Append to GLOBAL.log as ASCII hex of a binary event record (uppercase), one event per line
  const char* store_dir = "./stores/verse";
  char log_path[256];
  snprintf(log_path, sizeof(log_path), "%s/%s", store_dir, "GLOBAL.log");
  // Update GLOBAL.lock (write unix time), write GLOBAL.log, update lock again, close
  char lock_path[256];
  snprintf(lock_path, sizeof(lock_path), "%s/%s", store_dir, "GLOBAL.lock");
  FILE* lf = fopen(lock_path, "wb");
  if (lf) { time_t now=time(NULL); fprintf(lf, "%ld", (long)now); fclose(lf); }
  FILE* f = fopen(log_path, "ab+");
  if (f) {
    // Binary event format (big-endian):
    // TODO: use u8 for version
    // TODO: use u8 for type_mask
    // u32 version=1 | u64 seq (vector clock) | u32 type_mask | u32 patch_len | patch bytes
    const uint32_t version = 1u;
    const uint64_t seq = (uint64_t)u->history_event_count; // already incremented above
    uint32_t type_mask = 0;
    if (strcmp(type, "GENESIS") == 0) type_mask = UE_GENESIS;
    else if (strcmp(type, "SET_SEED") == 0) type_mask = UE_SET_SEED;
    else if (strcmp(type, "PLACE_WORLD") == 0) type_mask = UE_PLACE_WORLD;
    const char* patch_str = patch;
    uint32_t patch_len = (uint32_t)strlen(patch_str);

    size_t total = 4 + 8 + 4 + 4 + patch_len;
    uint8_t* be = (uint8_t*)malloc(total);
    if (be) {
      size_t o = 0;
      // version
      be[o++] = (uint8_t)((version >> 24) & 0xFF);
      be[o++] = (uint8_t)((version >> 16) & 0xFF);
      be[o++] = (uint8_t)((version >> 8) & 0xFF);
      be[o++] = (uint8_t)(version & 0xFF);
      // seq
      be[o++] = (uint8_t)((seq >> 56) & 0xFF);
      be[o++] = (uint8_t)((seq >> 48) & 0xFF);
      be[o++] = (uint8_t)((seq >> 40) & 0xFF);
      be[o++] = (uint8_t)((seq >> 32) & 0xFF);
      be[o++] = (uint8_t)((seq >> 24) & 0xFF);
      be[o++] = (uint8_t)((seq >> 16) & 0xFF);
      be[o++] = (uint8_t)((seq >> 8) & 0xFF);
      be[o++] = (uint8_t)(seq & 0xFF);
      // type mask
      be[o++] = (uint8_t)((type_mask >> 24) & 0xFF);
      be[o++] = (uint8_t)((type_mask >> 16) & 0xFF);
      be[o++] = (uint8_t)((type_mask >> 8) & 0xFF);
      be[o++] = (uint8_t)(type_mask & 0xFF);
      // patch len
      be[o++] = (uint8_t)((patch_len >> 24) & 0xFF);
      be[o++] = (uint8_t)((patch_len >> 16) & 0xFF);
      be[o++] = (uint8_t)((patch_len >> 8) & 0xFF);
      be[o++] = (uint8_t)(patch_len & 0xFF);
      // patch
      if (patch_len) {
        memcpy(be + o, patch_str, patch_len); o += patch_len;
      }

      // hex-encode the binary record
      for (size_t i = 0; i < total; i++) {
        fprintf(f, "%02X", be[i]);
      }
      fputc('\n', f);
      free(be);
    }
    fclose(f);
  }
  lf = fopen(lock_path, "wb");
  if (lf) { time_t now2=time(NULL); fprintf(lf, "%ld", (long)now2); fclose(lf); }
}

// Helper: read one hex line and decode to bytes; returns number of bytes decoded or -1 on error
static long decode_hex_line(const char* line, uint8_t** out_buf) {
  size_t len = strlen(line);
  while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
  if (len % 2 != 0) return -1;
  size_t blen = len / 2;
  uint8_t* buf = (uint8_t*)malloc(blen);
  if (!buf) return -1;
  for (size_t i = 0; i < blen; i++) {
    char a = line[2*i], b = line[2*i+1];
    if (!isxdigit((unsigned char)a) || !isxdigit((unsigned char)b)) { free(buf); return -1; }
    unsigned int v;
    if (sscanf(&line[2*i], "%2X", &v) != 1) { free(buf); return -1; }
    buf[i] = (uint8_t)v;
  }
  *out_buf = buf;
  return (long)blen;
}

bool universe_replay_from_global_log(Universe* u, const char* log_path) {
  if (!u || !log_path) return false;
  FILE* f = fopen(log_path, "rb");
  if (!f) return false;
  char* line = NULL; size_t cap = 0; ssize_t n;
  bool ok = true;
  while ((n = getline(&line, &cap, f)) != -1) {
    uint8_t* rec = NULL;
    long rlen = decode_hex_line(line, &rec);
    if (rlen < 0) { ok = false; break; }
    // Parse binary record
    size_t o = 0;
    if ((size_t)rlen < 4+8+4+4) { free(rec); ok=false; break; }
    uint32_t version = (rec[o]<<24) | (rec[o+1]<<16) | (rec[o+2]<<8) | rec[o+3]; o+=4;
    if (version != 1u) { free(rec); ok=false; break; }
    // seq
    uint64_t seq = ((uint64_t)rec[o]<<56) | ((uint64_t)rec[o+1]<<48) | ((uint64_t)rec[o+2]<<40) | ((uint64_t)rec[o+3]<<32)
                 | ((uint64_t)rec[o+4]<<24) | ((uint64_t)rec[o+5]<<16) | ((uint64_t)rec[o+6]<<8) | (uint64_t)rec[o+7];
    o+=8; (void)seq;
    uint32_t type_mask = (rec[o]<<24) | (rec[o+1]<<16) | (rec[o+2]<<8) | rec[o+3]; o+=4;
    uint32_t patch_len = (rec[o]<<24) | (rec[o+1]<<16) | (rec[o+2]<<8) | rec[o+3]; o+=4;
    if ((size_t)rlen < o + patch_len) { free(rec); ok=false; break; }
    char* patch = NULL;
    if (patch_len > 0) {
      patch = (char*)malloc(patch_len + 1);
      if (!patch) { free(rec); ok=false; break; }
      memcpy(patch, rec+o, patch_len); patch[patch_len] = '\0';
    }
    // Map mask back to type string
    const char* type_str = "";
    if (type_mask & UE_GENESIS) type_str = "GENESIS";
    else if (type_mask & UE_SET_SEED) type_str = "SET_SEED";
    else if (type_mask & UE_PLACE_WORLD) type_str = "PLACE_WORLD";
    else { free(rec); if (patch) free(patch); ok=false; break; }
    // Apply event
    universe_append_event(u, type_str, patch ? patch : NULL);
    if (patch) free(patch);
    free(rec);
  }
  if (line) free(line);
  fclose(f);
  return ok;
}




// -----------------------------------------------------------------------------
// Universe WFC-based generator selection
// -----------------------------------------------------------------------------

static inline UniverseGeneratorType universe_parse_generator(const char* name) {
  if (!name) return UNIVERSE_GEN_GAMEWORLD;
  if (strcmp(name, "arena") == 0 || strcmp(name, "ARENA") == 0) return UNIVERSE_GEN_ARENA;
  return UNIVERSE_GEN_GAMEWORLD;
}

// Map universe Z layer (-3..+3) to WFC tile index/name per wfc3_build_gameworld_model()
// gz: -3=bedrock_lower, -2=underworld, -1=bedrock_upper, 0=wilderness, +1=cloud, +2=home, +3=empty
static inline const char* universe_wfc_tile_for_gz(int gz) {
  switch (gz) {
    case 3:  return "empty";
    case 2:  return "home";
    case 1:  return "cloud";
    case 0:  return "wilderness";
    case -1: return "bedrock_upper";
    case -2: return "underworld";
    case -3: return "bedrock_lower";
    default: return "wilderness";
  }
}

// Decide the world generation type and optional solid fill for a universe cell based on the WFC tileset
// Returns true if a decision was written. For WORLD_TYPE_SOLID, out_fill is used.
bool universe_wfc_decide_cell(UniverseGeneratorType gen,
                              int gx, int gy, int gz,
                              WorldGenerationType* out_type,
                              VoxelType* out_fill)
{
  if (!out_type || !out_fill) return false;
  const char* tile = universe_wfc_tile_for_gz(gz);
  bool is_origin = (gx == 0 && gy == 0 && gz == 0);
  bool is_home_column = (gx == 0 && gy == 0);

  if (strcmp(tile, "empty") == 0) {
    // Sky layer above home: sparse cloud worlds as fully rendered backdrop (not empty air).
    *out_type = WORLD_TYPE_CLOUD; *out_fill = VOXEL_AIR; return true;
  }
  if (strcmp(tile, "home") == 0) {
    // There is one home island and it sits at the origin of its layer. Everything beside it is sky,
    // which is what makes it an island: the layer used to be home worlds edge to edge, so walking to
    // the boundary put the player on another island rather than over open air, and the twenty-six
    // cells around home were not a consistent backdrop to see it against.
    //
    // The layers directly above and below are already cloud, so this one rule is what leaves home
    // surrounded by cloud on all twenty-six sides.
    if (is_home_column) {
      *out_type = WORLD_TYPE_HOME; *out_fill = VOXEL_AIR; return true;
    }
    *out_type = WORLD_TYPE_CLOUD; *out_fill = VOXEL_AIR; return true;
  }
  if (strcmp(tile, "cloud") == 0) {
    *out_type = WORLD_TYPE_CLOUD; *out_fill = VOXEL_AIR; return true;
  }
  if (strcmp(tile, "wilderness") == 0) {
    if (gen == UNIVERSE_GEN_ARENA && is_origin) {
      *out_type = WORLD_TYPE_ARENA; *out_fill = VOXEL_AIR; return true;
    }
    // Occasional settlement tile on the wilderness plane (scale 1..9). Scale 0 stays wilderness.
    // Adjacency is enforced inside universe_settlement_scale so two settlements never share an edge
    // or corner.
    if (gen == UNIVERSE_GEN_GAMEWORLD && universe_settlement_scale(gx, gy) > 0) {
      *out_type = WORLD_TYPE_WFC_TOWN; *out_fill = VOXEL_AIR; return true;
    }
    *out_type = WORLD_TYPE_WILDERNESS; *out_fill = VOXEL_AIR; return true;
  }
  if (strcmp(tile, "bedrock_upper") == 0) {
    *out_type = WORLD_TYPE_SOLID; *out_fill = VOXEL_BEDROCK; return true;
  }
  if (strcmp(tile, "underworld") == 0) {
    *out_type = WORLD_TYPE_UNDERWORLD; *out_fill = VOXEL_AIR; return true;
  }
  if (strcmp(tile, "bedrock_lower") == 0) {
    *out_type = WORLD_TYPE_SOLID; *out_fill = VOXEL_BEDROCK; return true;
  }
  // Fallback
  *out_type = WORLD_TYPE_WILDERNESS; *out_fill = VOXEL_AIR; return true;
}

bool universe_generate_neighbors_wfc_around(
    Universe* u,
    const char* base_seed,
    uint64_t base_x, uint64_t base_y, uint64_t base_z,
    World* base_world,
    UniverseGeneratorType generator,
    bool include_diagonals,
    bool include_clouds)
{
  if (!u || !base_seed || !base_world) return false;
  // Determine base cell type from the provided base world; fall back to tileset mapping if unknown
  WorldGenerationType base_type = base_world ? base_world->generation_type : WORLD_TYPE_WILDERNESS;
  VoxelType base_fill = VOXEL_AIR;
  if (!base_world)
  {
    (void)universe_wfc_decide_cell(generator, (int)base_x, (int)base_y, (int)base_z, &base_type, &base_fill);
  }
  const int dirs_cardinal[4][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
  const int dirs_diag[4][3] = { {1,1,0}, {-1,1,0}, {1,-1,0}, {-1,-1,0} };
  // Defer cloud generation entirely to WFC callers; do not auto-generate clouds here
  bool gen_clouds = false;
  int made = 0;
  for (int i = 0; i < 4; i++) {
    const int dx = dirs_cardinal[i][0], dy = dirs_cardinal[i][1];
    const uint64_t nx = (uint64_t)((int64_t)base_x + dx);
    const uint64_t ny = (uint64_t)((int64_t)base_y + dy);
    const uint64_t nz = base_z;
    if (!universe_has(u, nx, ny, nz)) {
      World* w = world_create(base_world->width, base_world->height, base_world->depth);
      if (w) {
        WorldCoord coord = { dx, dy, 0 };
        char* seed = world_generate_seed_for_coord(base_seed, coord);
        const char* use_seed = seed ? seed : base_seed;
        // Initialize entropy field system for this world
        world_entropy_generator_init((uint32_t)strtoul(use_seed, NULL, 16));
        WorldGenerationType gtype; VoxelType fill;
        // Determine neighbor type by adjacency rules relative to the base type
        switch (base_type)
        {
          case WORLD_TYPE_HOME:
            // Home must be surrounded by empty
            gtype = WORLD_TYPE_SOLID; fill = VOXEL_AIR;
            break;
          case WORLD_TYPE_WILDERNESS:
          case WORLD_TYPE_WFC_TOWN:
            // Wilderness-plane neighbors are decided by the tileset (wilderness or settlement).
            universe_wfc_decide_cell(generator, (int)((int64_t)base_x + dx),
                                     (int)((int64_t)base_y + dy), (int)base_z, &gtype, &fill);
            break;
          case WORLD_TYPE_CLOUD:
            gtype = WORLD_TYPE_CLOUD; fill = VOXEL_AIR;
            break;
          case WORLD_TYPE_UNDERWORLD:
            gtype = WORLD_TYPE_UNDERWORLD; fill = VOXEL_AIR;
            break;
          default:
            universe_wfc_decide_cell(generator, (int)((int64_t)base_x + dx), (int)((int64_t)base_y + dy), (int)base_z, &gtype, &fill);
            break;
        }
        if (gtype == WORLD_TYPE_SOLID) {
            world_generate_with_type_and_fill(w, use_seed, gtype, fill);
        } else if (gtype == WORLD_TYPE_CLOUD || gtype == WORLD_TYPE_HOME ||
                   gtype == WORLD_TYPE_WILDERNESS || gtype == WORLD_TYPE_WFC_TOWN ||
                   gtype == WORLD_TYPE_UNDERWORLD ||
                   gtype == WORLD_TYPE_FARM || gtype == WORLD_TYPE_ARENA) {
            world_generate_with_type(w, use_seed, gtype);
        } else {
            world_generate_composite_entropy(w, (int32_t)nx, (int32_t)ny, (int32_t)nz);
        }
        world_refresh_occupancy_bitfield(w);
        universe_place(u, nx, ny, nz, w);
        made++;
        if (seed) free(seed);
        if (gen_clouds) {
          World* cw = world_create(base_world->width, base_world->height, base_world->depth);
          if (cw) {
            WorldCoord ccoord = { dx, dy, 1 };
            char* cseed = world_generate_seed_for_coord(base_seed, ccoord);
            const char* use_cseed = cseed ? cseed : base_seed;

            WorldGenerationType cg; VoxelType cf;
            universe_wfc_decide_cell(generator, (int)((int64_t)base_x + dx), (int)((int64_t)base_y + dy), (int)base_z + 1, &cg, &cf);
            if (cg == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(cw, use_cseed, cg, cf);
            else world_generate_with_type(cw, use_cseed, cg);
            world_refresh_occupancy_bitfield(cw);
            universe_place(u, nx, ny, nz + 1, cw);
            if (cseed) free(cseed);
          }
        }
      }
    }
  }
  if (include_diagonals) {
    for (int i = 0; i < 4; i++) {
      const int dx = dirs_diag[i][0], dy = dirs_diag[i][1];
      const uint64_t nx = (uint64_t)((int64_t)base_x + dx);
      const uint64_t ny = (uint64_t)((int64_t)base_y + dy);
      const uint64_t nz = base_z;
      if (!universe_has(u, nx, ny, nz)) {
        World* w = world_create(base_world->width, base_world->height, base_world->depth);
        if (w) {
          WorldCoord coord = { dx, dy, 0 };
          char* seed = world_generate_seed_for_coord(base_seed, coord);
          const char* use_seed = seed ? seed : base_seed;
          // Initialize entropy field system for this world
          world_entropy_generator_init((uint32_t)strtoul(use_seed, NULL, 16));
          WorldGenerationType gtype; VoxelType fill;
          switch (base_type)
          {
            case WORLD_TYPE_HOME:
              gtype = WORLD_TYPE_SOLID; fill = VOXEL_AIR;
              break;
            case WORLD_TYPE_WILDERNESS:
            case WORLD_TYPE_WFC_TOWN:
              universe_wfc_decide_cell(generator, (int)((int64_t)base_x + dx),
                                       (int)((int64_t)base_y + dy), (int)base_z, &gtype, &fill);
              break;
            case WORLD_TYPE_CLOUD:
              gtype = WORLD_TYPE_CLOUD; fill = VOXEL_AIR;
              break;
            case WORLD_TYPE_UNDERWORLD:
              gtype = WORLD_TYPE_UNDERWORLD; fill = VOXEL_AIR;
              break;
            default:
              universe_wfc_decide_cell(generator, (int)((int64_t)base_x + dx), (int)((int64_t)base_y + dy), (int)base_z, &gtype, &fill);
              break;
          }
          if (gtype == WORLD_TYPE_SOLID) {
              world_generate_with_type_and_fill(w, use_seed, gtype, fill);
          } else if (gtype == WORLD_TYPE_CLOUD || gtype == WORLD_TYPE_HOME ||
                     gtype == WORLD_TYPE_WILDERNESS || gtype == WORLD_TYPE_WFC_TOWN ||
                     gtype == WORLD_TYPE_UNDERWORLD ||
                     gtype == WORLD_TYPE_FARM || gtype == WORLD_TYPE_ARENA) {
              world_generate_with_type(w, use_seed, gtype);
          } else {
              world_generate_composite_entropy(w, (int32_t)nx, (int32_t)ny, (int32_t)nz);
          }
          world_refresh_occupancy_bitfield(w);
          universe_place(u, nx, ny, nz, w);
          made++;
          if (seed) free(seed);
          if (gen_clouds) {
            World* cw = world_create(base_world->width, base_world->height, base_world->depth);
            if (cw) {
              WorldCoord ccoord = { dx, dy, 1 };
              char* cseed = world_generate_seed_for_coord(base_seed, ccoord);
              const char* use_cseed = cseed ? cseed : base_seed;

              WorldGenerationType cg; VoxelType cf;
              universe_wfc_decide_cell(generator, (int)((int64_t)base_x + dx), (int)((int64_t)base_y + dy), (int)base_z + 1, &cg, &cf);
              if (cg == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(cw, use_cseed, cg, cf);
              else world_generate_with_type(cw, use_cseed, cg);
              world_refresh_occupancy_bitfield(cw);
              universe_place(u, nx, ny, nz + 1, cw);
              if (cseed) free(cseed);
            }
          }
        }
      }
    }
  }
  if (gen_clouds && !universe_has(u, base_x, base_y, base_z + 1)) {
    World* cw = world_create(base_world->width, base_world->height, base_world->depth);
    if (cw) {
      WorldCoord ccoord = { 0, 0, 1 };
      char* cseed = world_generate_seed_for_coord(base_seed, ccoord);
      const char* use_cseed = cseed ? cseed : base_seed;

      WorldGenerationType cg; VoxelType cf;
      universe_wfc_decide_cell(generator, (int)base_x, (int)base_y, (int)base_z + 1, &cg, &cf);
      if (cg == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(cw, use_cseed, cg, cf);
      else world_generate_with_type(cw, use_cseed, cg);
      world_refresh_occupancy_bitfield(cw);
      universe_place(u, base_x, base_y, base_z + 1, cw);
      if (cseed) free(cseed);
      made++;
    }
  }
  return (made > 0);
}

bool universe_generate_wfc_square_around(
    Universe* u,
    const char* base_seed,
    uint64_t base_x, uint64_t base_y, uint64_t base_z,
    World* base_world,
    UniverseGeneratorType generator,
    int radius,
    bool include_clouds)
{
  if (!u || !base_seed || !base_world || radius <= 0) return false;
  int made = 0;
  for (int dy = -radius; dy <= radius; dy++) {
    for (int dx = -radius; dx <= radius; dx++) {
      if (dx == 0 && dy == 0) continue; // skip base
      uint64_t nx = (uint64_t)((int64_t)base_x + dx);
      uint64_t ny = (uint64_t)((int64_t)base_y + dy);
      uint64_t nz = base_z;
      if (universe_has(u, nx, ny, nz)) continue;
      World* w = world_create(base_world->width, base_world->height, base_world->depth);
      if (!w) continue;
      WorldCoord coord = { dx, dy, 0 };
      char* seed = world_generate_seed_for_coord(base_seed, coord);
      const char* use_seed = seed ? seed : base_seed;
      // Initialize entropy field system for this world
      world_entropy_generator_init((uint32_t)strtoul(use_seed, NULL, 16));
      WorldGenerationType gtype; VoxelType fill;
      universe_wfc_decide_cell(generator, (int)((int64_t)base_x + dx), (int)((int64_t)base_y + dy), (int)base_z, &gtype, &fill);
      // Prefer the typed generator so cloud/home/wilderness layers keep their look.
      // Composite entropy is only a fallback for unknown non-solid types.
      if (gtype == WORLD_TYPE_SOLID) {
          world_generate_with_type_and_fill(w, use_seed, gtype, fill);
      } else if (gtype == WORLD_TYPE_CLOUD || gtype == WORLD_TYPE_HOME ||
                 gtype == WORLD_TYPE_WILDERNESS || gtype == WORLD_TYPE_WFC_TOWN ||
                 gtype == WORLD_TYPE_UNDERWORLD ||
                 gtype == WORLD_TYPE_FARM || gtype == WORLD_TYPE_ARENA) {
          world_generate_with_type(w, use_seed, gtype);
      } else {
          world_generate_composite_entropy(w, (int32_t)nx, (int32_t)ny, (int32_t)nz);
      }
      world_refresh_occupancy_bitfield(w);
      universe_place(u, nx, ny, nz, w);
      made++;
      if (seed) free(seed);
      if (include_clouds) {
        uint64_t nz_up = nz + 1;
        if (!universe_has(u, nx, ny, nz_up)) {
          World* cw = world_create(base_world->width, base_world->height, base_world->depth);
          if (cw) {
            WorldCoord ccoord = { dx, dy, 1 };
            char* cseed = world_generate_seed_for_coord(base_seed, ccoord);
            const char* use_cseed = cseed ? cseed : base_seed;

            WorldGenerationType cg; VoxelType cf;
            universe_wfc_decide_cell(generator, (int)((int64_t)base_x + dx), (int)((int64_t)base_y + dy), (int)base_z + 1, &cg, &cf);
            if (cg == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(cw, use_cseed, cg, cf);
            else world_generate_with_type(cw, use_cseed, cg);
            world_refresh_occupancy_bitfield(cw);
            universe_place(u, nx, ny, nz_up, cw);
            if (cseed) free(cseed);
          }
        }
      }
    }
  }
  // Base cloud
  if (include_clouds) {
    if (!universe_has(u, base_x, base_y, base_z + 1)) {
      World* cw = world_create(base_world->width, base_world->height, base_world->depth);
      if (cw) {
        WorldCoord ccoord = { 0, 0, 1 };
        char* cseed = world_generate_seed_for_coord(base_seed, ccoord);
        const char* use_cseed = cseed ? cseed : base_seed;

        WorldGenerationType cg; VoxelType cf;
        universe_wfc_decide_cell(generator, (int)base_x, (int)base_y, (int)base_z + 1, &cg, &cf);
        if (cg == WORLD_TYPE_SOLID) world_generate_with_type_and_fill(cw, use_cseed, cg, cf);
        else world_generate_with_type(cw, use_cseed, cg);
        world_refresh_occupancy_bitfield(cw);
        universe_place(u, base_x, base_y, base_z + 1, cw);
        if (cseed) free(cseed);
        made++;
      }
    }
  }
  return (made > 0);
}

// ============================================================================
// UNIVERSE CONSISTENCY FUNCTIONS
// ============================================================================

// Create universe context for a standalone world
bool universe_create_for_world(Universe* u, World* w, const char* seed)
{
  if (!u || !w || !seed)
    return false;

  // Initialize the universe with the world's seed
  if (!universe_init(u, seed, 0, 1))
    return false;

  // Place the world at origin (0,0,0)
  if (!universe_place(u, 0, 0, 0, w))
    return false;

  // Set the world's universe context
  w->universe_context = u;
  w->universe_x = 0;
  w->universe_y = 0;
  w->universe_z = 0;

  // Add genesis event
  char genesis_json[256];
  snprintf(genesis_json, sizeof(genesis_json), "{\"type\":\"standalone_world\",\"seed\":\"%s\"}", seed);
  universe_append_event(u, "GENESIS", genesis_json);

  return true;
}

// Ensure seed consistency when adding worlds to universe
bool universe_ensure_seed_consistency(Universe* u, World* w, uint64_t x, uint64_t y, uint64_t z)
{
  if (!u || !w)
    return false;

  // Check if the universe already has a seed
  if (u->seed[0] != '\0') {
    // For now, we'll accept any world in the universe
    // In practice, you might want to implement a more sophisticated seed validation system
    // that can verify the relationship between the universe seed and world seeds
  } else {
    // Universe has no seed yet, set it from the world
    strncpy(u->seed, w->seed_id, sizeof(u->seed) - 1);
    u->seed[sizeof(u->seed) - 1] = '\0';
  }

    // Set the world's universe context
  w->universe_context = u;
  w->universe_x = x;
  w->universe_y = y;
  w->universe_z = z;

  // Actually place the world in the universe
  if (!universe_place(u, x, y, z, w)) {
    return false;
  }

  // Add placement event
  char placement_json[512];
  snprintf(placement_json, sizeof(placement_json),
           "{\"seed\":\"%s\",\"position\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "],\"type\":%d}",
           w->seed_id, x, y, z, w->generation_type);
  universe_append_event(u, "PLACE_WORLD", placement_json);

  return true;
}
