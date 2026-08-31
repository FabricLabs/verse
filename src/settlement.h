#ifndef SETTLEMENT_H
#define SETTLEMENT_H

#include <stdint.h>
#include <stdbool.h>

#include "world.h"

// Wilderness-plane settlement scale for a universe cell (gx, gy) on z=0.
//   0 = none (ordinary wilderness)
//   1 = single building
//   5 = proper village / town
//   9 = fortress city
// Placement uses jittered Voronoi sites with force-directed spacing, biased toward
// flat terrain so larger settlements claim larger flat basins. Guarantees no two
// settlements share an edge or corner (Chebyshev adjacency). The cell under home
// (0,0) is always scale 1.
int universe_settlement_scale(int gx, int gy);

// Deterministic flatness score in [0,1] for a wilderness cell (higher = flatter).
float universe_settlement_flatness(int gx, int gy);

// Nearest cell with exact `scale` within `max_manhattan` of (from_gx, from_gy).
// Tie-break: closer Manhattan, then lexicographically smaller (gx,gy). Skips the origin cell.
bool universe_nearest_settlement_of_scale(int from_gx, int from_gy, int scale, int max_manhattan,
                                          int *out_gx, int *out_gy);

// Primary trade / road hub near the origin (highest-scale settlement in range).
// Home-drop never qualifies. Returns false if none found.
bool universe_main_hub(int *out_gx, int *out_gy);

// How many basic buildings a given scale places (scale^2, clamped).
int settlement_building_count(int scale);

// Short label for map waypoints / toasts ("Hut", "Village", "Town", …).
const char *settlement_scale_label(int scale);

// Building roles used when stamping a settlement. Each role maps to a procedural complexity.
typedef enum {
  SETTLEMENT_BLDG_HUT = 0,
  SETTLEMENT_BLDG_COTTAGE,
  SETTLEMENT_BLDG_HOUSE,
  SETTLEMENT_BLDG_SHOP,   // shopkeeper workplace
  SETTLEMENT_BLDG_TOWER,  // guard post
  SETTLEMENT_BLDG_HALL,   // town hall / healer
  SETTLEMENT_BLDG_MANOR,  // residence + civic
  SETTLEMENT_BLDG_CASTLE, // fortress / guard HQ
  SETTLEMENT_BLDG_BASIC,  // procedural box fallback
  SETTLEMENT_BLDG_COUNT
} SettlementBuildingType;

// Procedural building complexity. Footprint may clamp the request downward
// (e.g. multi-level needs room for stairs; multi-room needs a partition).
typedef enum {
  SETTLEMENT_PROC_ONE_ROOM = 0,   // hollow shell, exterior door, windows, pitched roof
  SETTLEMENT_PROC_MULTI_ROOM,     // interior partitions with doorways
  SETTLEMENT_PROC_MULTI_LEVEL,    // stacked stories + stairwell
  SETTLEMENT_PROC_COUNT
} SettlementProcComplexity;

const char *settlement_building_type_name(SettlementBuildingType type);
const char *settlement_proc_complexity_name(SettlementProcComplexity complexity);

// Default procedural complexity for a building role.
SettlementProcComplexity settlement_proc_complexity_for_type(SettlementBuildingType type);

// Primary villager occupation this building serves (matches VillagerProfession values).
// Residences → farmer/shepherd; shop → merchant/baker/miller; tower → guard; hall → healer; etc.
uint8_t settlement_occupation_for_building(SettlementBuildingType type, uint32_t *rng);
const char *settlement_occupation_label(uint8_t occupation);

// Pick a building role for the Nth structure in a settlement of the given scale.
SettlementBuildingType settlement_pick_building_type(int scale, int index, int total, uint32_t *rng);

// Stamp a procedural building with SW corner at (ox, oy). Uses construction voxels
// (stone foundations, walls, thatch/roof-tile roofs, glass windows, door panels) and
// occupation furnishings (beds, crates, barrels, counters). Returns floor z, or -1.
// When type is not BASIC, records the building on world->settlement_placed.
int settlement_place_procedural(World *world, int ox, int oy, int bw, int bd,
                                SettlementProcComplexity complexity, uint32_t *rng);
int settlement_place_procedural_typed(World *world, int ox, int oy, int bw, int bd,
                                      SettlementBuildingType type, uint32_t *rng);

// True when a stamp recorded an objective anchor (building center).
bool settlement_has_anchor(const World *world);
bool settlement_anchor(const World *world, int *out_x, int *out_y, int *out_z);

// Hunter's note prop inside the home-drop shack (objective for the follow-up quest).
bool settlement_has_note(const World *world);
bool settlement_note(const World *world, int *out_x, int *out_y, int *out_z);

// Inter-settlement roads and trade corridors. Settlements of scale >= 3 link toward
// the main hub; hubs (scale >= 6) also form a peer mesh. Corridors are A*-routed to
// prefer gentle grades and avoid water / lava / forest proxies (and, when stamping,
// weave around real trees, fluids, magma, and steep columns). Grade follows the
// weaker endpoint: path → dirt → gravel → cobble → highway (widths and thicknesses 1..5).
typedef enum {
  SETTLEMENT_ROAD_NONE = 0,
  SETTLEMENT_ROAD_PATH,     // width 1, thick 1
  SETTLEMENT_ROAD_DIRT,     // width 2, thick 2
  SETTLEMENT_ROAD_GRAVEL,   // width 3, thick 3
  SETTLEMENT_ROAD_COBBLE,   // width 4, thick 4
  SETTLEMENT_ROAD_HIGHWAY,  // width 5, thick 5
  SETTLEMENT_ROAD_TYPE_COUNT
} SettlementRoadType;

const char *settlement_road_type_name(SettlementRoadType type);
VoxelType settlement_road_voxel(SettlementRoadType type);
int settlement_road_width(SettlementRoadType type);       // 1..5 voxels
int settlement_road_thickness(SettlementRoadType type);   // 1..5 layers below surface
int settlement_road_half_width(SettlementRoadType type);  // width/2 (paint radius)

// True when (gx,gy) lies on a road between two settlements. Optionally returns
// the canonical edge endpoints and the pavement grade for that crossing.
bool universe_settlement_road(int gx, int gy, SettlementRoadType *out_type,
                              int *out_from_gx, int *out_from_gy,
                              int *out_to_gx, int *out_to_gy);

// Trade corridors follow the same graph as roads (living-world caravan routes).
bool universe_settlement_trade_route(int gx, int gy, SettlementRoadType *out_type,
                                     int *out_from_gx, int *out_from_gy,
                                     int *out_to_gx, int *out_to_gy);

// True when settlements at A and B are directly linked by a road/trade edge.
bool universe_settlements_linked(int ax, int ay, int bx, int by, SettlementRoadType *out_type);

// Stamp a settlement of the given scale onto an already-generated wilderness surface.
// All structures are procedural (one-room / multi-room / multi-level) using construction voxels
// including textured doors and pitched-roof tiles. Prefab .vbuild files are not used.
// When scale >= 5, adds a plaza; when scale >= 4, adds a perimeter (wood fence → wattle →
// stone curtain → fortress ramparts at scale 9).
// The home-drop cell (0,0) places a fixed "hunter's shack" and always records an anchor.
// Layout is centered on the flattest basin found in the cell.
bool settlement_stamp(World *world, int scale, const char *seed);

// Pave roads across a wilderness / settlement world from the universe road graph.
// Safe to call more than once; skips columns occupied by building walls.
bool settlement_stamp_roads(World *world);

#endif // SETTLEMENT_H
