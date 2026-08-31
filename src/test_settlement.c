// Headless checks for settlement scale selection and basic building stamps.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "settlement.h"
#include "universe.h"
#include "world.h"
#include "constants.h"
#include "mob_ai.h"
#include "voxel_shape.h"

static int failures = 0;

static void report(const char *name, bool ok)
{
  printf("  %s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok)
    failures++;
}

static void test_scale_range_and_adjacency(void)
{
  printf("\n-- scale range and adjacency --\n");
  int hist[10] = {0};
  int adjacent = 0;
  int out_of_range = 0;
  const int R = 200;
  for (int gy = -R; gy <= R; gy++)
  {
    for (int gx = -R; gx <= R; gx++)
    {
      const int s = universe_settlement_scale(gx, gy);
      if (s < 0 || s > 9)
      {
        out_of_range++;
        continue;
      }
      hist[s]++;
      if (s == 0)
        continue;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
          if (dx == 0 && dy == 0)
            continue;
          if (universe_settlement_scale(gx + dx, gy + dy) > 0)
            adjacent++;
        }
    }
  }
  printf("       histogram:");
  for (int i = 0; i < 10; i++)
    printf(" %d=%d", i, hist[i]);
  printf("\n");
  report("scale always in 0..9", out_of_range == 0);
  report("some cells have no settlement", hist[0] > 0);
  report("some cells have a settlement", hist[0] < (2 * R + 1) * (2 * R + 1));
  report("single-building scale exists", hist[1] > 0);
  report("no adjacent settlements", adjacent == 0);
  report("home-drop (0,0) is always scale 1", universe_settlement_scale(0, 0) == 1);
  report("home-drop neighbors stay empty",
         universe_settlement_scale(1, 0) == 0 && universe_settlement_scale(-1, 0) == 0 &&
             universe_settlement_scale(0, 1) == 0 && universe_settlement_scale(0, -1) == 0 &&
             universe_settlement_scale(1, 1) == 0 && universe_settlement_scale(-1, -1) == 0);
  // Settlements prefer flat basins (Voronoi sites with flatness bias).
  {
    int flat_ok = 0, settled = 0;
    for (int gy = -R; gy <= R; gy++)
      for (int gx = -R; gx <= R; gx++)
      {
        if (universe_settlement_scale(gx, gy) <= 0)
          continue;
        if (gx == 0 && gy == 0)
          continue;
        settled++;
        if (universe_settlement_flatness(gx, gy) >= 0.35f)
          flat_ok++;
      }
    printf("       flat settlements=%d/%d\n", flat_ok, settled);
    report("settlements sit on flat basins", settled == 0 || flat_ok * 2 >= settled);
  }
  report("building_count(1) is 1", settlement_building_count(1) == 1);
  report("building_count(5) is 25", settlement_building_count(5) == 25);
  report("building_count(9) is 81", settlement_building_count(9) == 81);
}

static void test_stamp_places_buildings(void)
{
  printf("\n-- stamp basic buildings --\n");
  World *w = world_create(64, 64, 32);
  if (!w)
  {
    report("world created", false);
    return;
  }
  // Flat stone ground for a fast stamp without full wilderness gen.
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      world_set_voxel(w, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
    }
  world_refresh_occupancy_bitfield(w);

  report("scale-1 stamp succeeds", settlement_stamp(w, 1, "settlement_test"));
  int wood_or_brick = 0;
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
      {
        VoxelType t = w->voxels[(size_t)z * w->width * w->height + (size_t)y * w->width + x].type;
        if (t == VOXEL_WOOD_OAK || t == VOXEL_BRICK || t == VOXEL_WOOD_PINE)
          wood_or_brick++;
      }
  printf("       building voxels=%d\n", wood_or_brick);
  report("stamp placed building materials", wood_or_brick > 20);

  // Decide-cell: home-drop and other settlement cells map to WFC_TOWN
  WorldGenerationType home_type;
  VoxelType home_fill;
  universe_wfc_decide_cell(UNIVERSE_GEN_GAMEWORLD, 0, 0, 0, &home_type, &home_fill);
  report("decide_cell marks home-drop as WFC_TOWN", home_type == WORLD_TYPE_WFC_TOWN);

  world_destroy(w);
}

static void test_home_drop_shack_anchor(void)
{
  printf("\n-- home-drop hunter's shack anchor --\n");
  World *w = world_create(64, 64, 32);
  if (!w)
  {
    report("world created", false);
    return;
  }
  w->universe_x = 0;
  w->universe_y = 0;
  w->universe_z = 0;
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      world_set_voxel(w, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
    }
  world_refresh_occupancy_bitfield(w);

  report("home-drop stamp succeeds", settlement_stamp(w, 1, "home_drop_shack"));
  report("home-drop records an anchor", settlement_has_anchor(w));
  int ax = -1, ay = -1, az = -1;
  settlement_anchor(w, &ax, &ay, &az);
  printf("       anchor=(%d,%d,%d)\n", ax, ay, az);
  report("anchor is near world center", ax > 20 && ax < 44 && ay > 20 && ay < 50);
  report("home-drop records a note", settlement_has_note(w));
  int nx = -1, ny = -1, nz = -1;
  settlement_note(w, &nx, &ny, &nz);
  printf("       note=(%d,%d,%d)\n", nx, ny, nz);
  report("note is near the shack anchor",
         abs(nx - ax) <= 3 && abs(ny - ay) <= 3 && nz >= az);

  world_destroy(w);
}

static void test_inter_settlement_roads(void)
{
  printf("\n-- inter-settlement roads (hub tree + mesh) --\n");

  int hub_x = 0, hub_y = 0;
  report("main hub exists near origin", universe_main_hub(&hub_x, &hub_y));
  printf("       main hub=(%d,%d) scale=%d\n", hub_x, hub_y,
         universe_settlement_scale(hub_x, hub_y));
  report("main hub is on a road network cell",
         universe_settlement_road(hub_x, hub_y, NULL, NULL, NULL, NULL, NULL) ||
             universe_settlement_scale(hub_x, hub_y) >= 5);

  // Locate a linked settlement and its parent so we can assert the corridor.
  int node_x = 0, node_y = 0, peer_x = 0, peer_y = 0;
  bool found_link = false;
  for (int gy = -60; gy <= 60 && !found_link; gy++)
  {
    for (int gx = -60; gx <= 60; gx++)
    {
      const int s = universe_settlement_scale(gx, gy);
      if (s < 3)
        continue;
      if (gx == hub_x && gy == hub_y)
        continue;
      int ax = 0, ay = 0, bx = 0, by = 0;
      if (!universe_settlement_road(gx, gy, NULL, &ax, &ay, &bx, &by))
        continue;
      node_x = gx;
      node_y = gy;
      peer_x = (ax == gx && ay == gy) ? bx : ax;
      peer_y = (ax == gx && ay == gy) ? by : ay;
      found_link = true;
      break;
    }
  }
  report("found a scale>=3 settlement on a road", found_link);
  if (!found_link)
    return;
  printf("       node=(%d,%d) scale=%d peer=(%d,%d)\n", node_x, node_y,
         universe_settlement_scale(node_x, node_y), peer_x, peer_y);

  SettlementRoadType ht = SETTLEMENT_ROAD_NONE;
  report("node cell is on a road", universe_settlement_road(node_x, node_y, &ht, NULL, NULL, NULL, NULL));
  report("trade route aliases road graph",
         universe_settlement_trade_route(node_x, node_y, NULL, NULL, NULL, NULL, NULL));

  // Corridor cells along the terrain-aware route (may leave the straight AABB).
  int mid_hits = 0;
  const int steps = (node_x > peer_x ? node_x - peer_x : peer_x - node_x) +
                    (node_y > peer_y ? node_y - peer_y : peer_y - node_y);
  const int pad = 12;
  const int minx = (node_x < peer_x ? node_x : peer_x) - pad;
  const int maxx = (node_x > peer_x ? node_x : peer_x) + pad;
  const int miny = (node_y < peer_y ? node_y : peer_y) - pad;
  const int maxy = (node_y > peer_y ? node_y : peer_y) + pad;
  for (int gy = miny; gy <= maxy; gy++)
    for (int gx = minx; gx <= maxx; gx++)
    {
      int ax = 0, ay = 0, bx = 0, by = 0;
      if (!universe_settlement_road(gx, gy, NULL, &ax, &ay, &bx, &by))
        continue;
      if ((ax == node_x && ay == node_y && bx == peer_x && by == peer_y) ||
          (ax == peer_x && ay == peer_y && bx == node_x && by == node_y))
        mid_hits++;
    }
  printf("       corridor cells with road=%d (manhattan=%d)\n", mid_hits, steps);
  report("road covers more than just the two endpoints", mid_hits >= (steps > 0 ? 2 : 1));

  // Scale 1–2 never seed the road graph as endpoints (except via corridor cross).
  int tiny_as_endpoint = 0;
  for (int gy = -40; gy <= 40; gy++)
    for (int gx = -40; gx <= 40; gx++)
    {
      const int s = universe_settlement_scale(gx, gy);
      if (s < 1 || s > 2)
        continue;
      int ax = 0, ay = 0, bx = 0, by = 0;
      if (!universe_settlement_road(gx, gy, NULL, &ax, &ay, &bx, &by))
        continue;
      if ((ax == gx && ay == gy) || (bx == gx && by == gy))
        tiny_as_endpoint++;
    }
  report("scale-1/2 settlements are not road endpoints", tiny_as_endpoint == 0);

  report("path..highway names resolve",
         strcmp(settlement_road_type_name(SETTLEMENT_ROAD_PATH), "path") == 0 &&
             strcmp(settlement_road_type_name(SETTLEMENT_ROAD_DIRT), "dirt") == 0 &&
             strcmp(settlement_road_type_name(SETTLEMENT_ROAD_GRAVEL), "gravel") == 0 &&
             strcmp(settlement_road_type_name(SETTLEMENT_ROAD_COBBLE), "cobble") == 0 &&
             strcmp(settlement_road_type_name(SETTLEMENT_ROAD_HIGHWAY), "highway") == 0);
  report("road voxels differ by grade",
         settlement_road_voxel(SETTLEMENT_ROAD_PATH) == VOXEL_SOIL &&
             settlement_road_voxel(SETTLEMENT_ROAD_DIRT) == VOXEL_SOIL_LOAM &&
             settlement_road_voxel(SETTLEMENT_ROAD_GRAVEL) == VOXEL_GRAVEL &&
             settlement_road_voxel(SETTLEMENT_ROAD_COBBLE) == VOXEL_COBBLE &&
             settlement_road_voxel(SETTLEMENT_ROAD_HIGHWAY) == VOXEL_BRICK);
  report("widths span 1..5",
         settlement_road_width(SETTLEMENT_ROAD_PATH) == 1 &&
             settlement_road_width(SETTLEMENT_ROAD_DIRT) == 2 &&
             settlement_road_width(SETTLEMENT_ROAD_GRAVEL) == 3 &&
             settlement_road_width(SETTLEMENT_ROAD_COBBLE) == 4 &&
             settlement_road_width(SETTLEMENT_ROAD_HIGHWAY) == 5);
  report("thicknesses span 1..5",
         settlement_road_thickness(SETTLEMENT_ROAD_PATH) == 1 &&
             settlement_road_thickness(SETTLEMENT_ROAD_HIGHWAY) == 5);
  report("highway is wider than path",
         settlement_road_width(SETTLEMENT_ROAD_HIGHWAY) >
             settlement_road_width(SETTLEMENT_ROAD_PATH));

  // Stamp pavement onto a flat world sitting on the corridor cell.
  World *w = world_create(48, 48, 24);
  if (!w)
  {
    report("road stamp world created", false);
    return;
  }
  w->universe_x = (uint64_t)(int64_t)node_x;
  w->universe_y = (uint64_t)(int64_t)node_y;
  w->universe_z = 0;
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      world_set_voxel(w, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
    }
  world_refresh_occupancy_bitfield(w);
  w->settlement_has_anchor = true;
  w->settlement_anchor_x = 24;
  w->settlement_anchor_y = 24;
  w->settlement_anchor_z = 3;
  w->settlement_scale = universe_settlement_scale(node_x, node_y);

  report("road stamp succeeds on linked cell", settlement_stamp_roads(w));
  int soil = 0, gravel = 0, cobble = 0, brick = 0, loam = 0;
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      for (uint32_t z = 0; z < w->depth; z++)
      {
        VoxelType t = w->voxels[(size_t)z * w->width * w->height + (size_t)y * w->width + x].type;
        if (t == VOXEL_SOIL)
          soil++;
        else if (t == VOXEL_SOIL_LOAM)
          loam++;
        else if (t == VOXEL_GRAVEL)
          gravel++;
        else if (t == VOXEL_COBBLE)
          cobble++;
        else if (t == VOXEL_BRICK)
          brick++;
      }
    }
  printf("       paved soil=%d loam=%d gravel=%d cobble=%d brick=%d (type=%s)\n", soil, loam,
         gravel, cobble, brick, settlement_road_type_name(ht));
  report("stamp placed road pavement voxels", soil + loam + gravel + cobble + brick > 10);
  if (ht == SETTLEMENT_ROAD_HIGHWAY)
    report("highway stamp uses brick", brick > 0);
  else if (ht == SETTLEMENT_ROAD_COBBLE)
    report("cobble stamp uses cobble", cobble > 0);
  else if (ht == SETTLEMENT_ROAD_GRAVEL)
    report("gravel stamp uses gravel", gravel > 0);
  else if (ht == SETTLEMENT_ROAD_DIRT)
    report("dirt stamp uses loam", loam > 0);
  else if (ht == SETTLEMENT_ROAD_PATH)
    report("path stamp uses soil", soil > 0);

  // Off-network cell stamps nothing.
  bool found_off = false;
  for (int gy = -30; gy <= 30 && !found_off; gy++)
  {
    for (int gx = -30; gx <= 30; gx++)
    {
      if (universe_settlement_scale(gx, gy) > 0)
        continue;
      if (universe_settlement_road(gx, gy, NULL, NULL, NULL, NULL, NULL))
        continue;
      w->universe_x = (uint64_t)(int64_t)gx;
      w->universe_y = (uint64_t)(int64_t)gy;
      found_off = true;
      break;
    }
  }
  report("found off-network wilderness cell", found_off);
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
      for (uint32_t z = 3; z < w->depth; z++)
        world_set_voxel(w, x, y, z, VOXEL_AIR);
    }
  report("off-network cell has no road stamp", !settlement_stamp_roads(w));

  world_destroy(w);
}

static void test_nearest_scale_five(void)
{
  printf("\n-- nearest size-5 settlement --\n");
  int gx = 0, gy = 0;
  report("finds a scale-5 near home-drop",
         universe_nearest_settlement_of_scale(0, 0, 5, 96, &gx, &gy));
  if (universe_nearest_settlement_of_scale(0, 0, 5, 96, &gx, &gy))
  {
    printf("       nearest size-5 at (%d,%d) manhattan=%d\n", gx, gy,
           (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy));
    report("returned cell is exactly scale 5", universe_settlement_scale(gx, gy) == 5);
    report("is not the home-drop", !(gx == 0 && gy == 0));
  }
  report("tiny radius can miss", !universe_nearest_settlement_of_scale(0, 0, 5, 1, &gx, &gy) ||
                                     universe_settlement_scale(gx, gy) == 5);
  report("rejects invalid scale", !universe_nearest_settlement_of_scale(0, 0, 0, 40, &gx, &gy));
}

static void test_settlement_labels_and_fauna_policy(void)
{
  printf("\n-- settlement labels --\n");
  report("scale 1 is Hut", strcmp(settlement_scale_label(1), "Hut") == 0);
  report("scale 5 is Village", strcmp(settlement_scale_label(5), "Village") == 0);
  report("scale 7 is Town", strcmp(settlement_scale_label(7), "Town") == 0);
  report("scale 9 is Fortress", strcmp(settlement_scale_label(9), "Fortress") == 0);
}

static void test_building_type_mix(void)
{
  printf("\n-- building type mix --\n");
  report("hut name", strcmp(settlement_building_type_name(SETTLEMENT_BLDG_HUT), "hut") == 0);
  report("manor name", strcmp(settlement_building_type_name(SETTLEMENT_BLDG_MANOR), "manor") == 0);
  report("castle name", strcmp(settlement_building_type_name(SETTLEMENT_BLDG_CASTLE), "castle") == 0);

  report("scale-1 picks hut",
         settlement_pick_building_type(1, 0, 1, NULL) == SETTLEMENT_BLDG_HUT);
  report("scale-5 landmark is hall",
         settlement_pick_building_type(5, 0, 25, NULL) == SETTLEMENT_BLDG_HALL);
  report("scale-6 landmark is manor",
         settlement_pick_building_type(6, 0, 36, NULL) == SETTLEMENT_BLDG_MANOR);
  report("scale-8 landmark is castle",
         settlement_pick_building_type(8, 0, 64, NULL) == SETTLEMENT_BLDG_CASTLE);
  report("scale-7 second is tower",
         settlement_pick_building_type(7, 1, 49, NULL) == SETTLEMENT_BLDG_TOWER);

  // Village stamp away from home-drop should place construction materials.
  World *w = world_create(96, 96, 48);
  if (!w)
  {
    report("village world created", false);
    return;
  }
  w->universe_x = 10;
  w->universe_y = -4;
  w->universe_z = 0;
  for (uint32_t y = 0; y < w->height; y++)
    for (uint32_t x = 0; x < w->width; x++)
    {
      world_set_voxel(w, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
    }
  world_refresh_occupancy_bitfield(w);

  report("village stamp succeeds", settlement_stamp(w, 5, "village_types"));
  int thatch = 0, plaster = 0, wood = 0, brick = 0, glass = 0, plank = 0, doors = 0, roof_tile = 0;
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
      {
        VoxelType t = w->voxels[(size_t)z * w->width * w->height + (size_t)y * w->width + x].type;
        if (t == VOXEL_THATCH || t == VOXEL_THATCH_MIRROR)
          thatch++;
        else if (t == VOXEL_PLASTER)
          plaster++;
        else if (t == VOXEL_WOOD_OAK || t == VOXEL_WOOD_PINE || t == VOXEL_WOOD)
          wood++;
        else if (t == VOXEL_BRICK || t == VOXEL_TERRACOTTA || t == VOXEL_ADOBE)
          brick++;
        else if (t == VOXEL_GLASS || t == VOXEL_GLASS_NS)
          glass++;
        else if (t == VOXEL_PLANK)
          plank++;
        else if (t == VOXEL_DOOR || t == VOXEL_DOOR_NS)
          doors++;
        else if (t == VOXEL_ROOF_TILE || t == VOXEL_ROOF_TILE_MIRROR)
          roof_tile++;
      }
  printf("       thatch=%d roof_tile=%d plaster=%d wood=%d brick/terra/adobe=%d glass=%d plank=%d "
         "doors=%d\n",
         thatch, roof_tile, plaster, wood, brick, glass, plank, doors);
  report("village uses construction materials", thatch + plaster + wood + brick + roof_tile > 200);
  report("village has textured roofs", thatch > 0 || roof_tile > 0);
  report("village has glass windows", glass > 0);
  report("village has door voxels", doors > 0);
  world_destroy(w);
}

static VoxelType flat_voxel_at(const World *w, int x, int y, int z)
{
  return w->voxels[(size_t)z * w->width * w->height + (size_t)y * w->width + (size_t)x].type;
}

static World *make_flat_world(uint32_t w, uint32_t h, uint32_t d)
{
  World *world = world_create(w, h, d);
  if (!world)
    return NULL;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++)
    {
      world_set_voxel(world, x, y, 0, VOXEL_BEDROCK);
      world_set_voxel(world, x, y, 1, VOXEL_STONE);
      world_set_voxel(world, x, y, 2, VOXEL_GRASS);
    }
  world_refresh_occupancy_bitfield(world);
  return world;
}

static int count_type_in_box(const World *w, int x0, int y0, int z0, int x1, int y1, int z1,
                             VoxelType t)
{
  int n = 0;
  for (int z = z0; z <= z1; z++)
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++)
        if (flat_voxel_at(w, x, y, z) == t)
          n++;
  return n;
}

static void test_procedural_buildings(void)
{
  printf("\n-- procedural building tiers --\n");
  report("one_room name",
         strcmp(settlement_proc_complexity_name(SETTLEMENT_PROC_ONE_ROOM), "one_room") == 0);
  report("multi_room name",
         strcmp(settlement_proc_complexity_name(SETTLEMENT_PROC_MULTI_ROOM), "multi_room") == 0);
  report("multi_level name",
         strcmp(settlement_proc_complexity_name(SETTLEMENT_PROC_MULTI_LEVEL), "multi_level") == 0);
  report("hut → one_room",
         settlement_proc_complexity_for_type(SETTLEMENT_BLDG_HUT) == SETTLEMENT_PROC_ONE_ROOM);
  report("house → multi_room",
         settlement_proc_complexity_for_type(SETTLEMENT_BLDG_HOUSE) == SETTLEMENT_PROC_MULTI_ROOM);
  report("tower → multi_level",
         settlement_proc_complexity_for_type(SETTLEMENT_BLDG_TOWER) == SETTLEMENT_PROC_MULTI_LEVEL);

  World *w = make_flat_world(48, 48, 40);
  if (!w)
  {
    report("proc world created", false);
    return;
  }

  uint32_t rng = 0xBEEFu;

  // --- One-room ---
  const int ox1 = 4, oy1 = 4, bw1 = 6, bd1 = 6;
  const int g1 =
      settlement_place_procedural(w, ox1, oy1, bw1, bd1, SETTLEMENT_PROC_ONE_ROOM, &rng);
  report("one-room places", g1 >= 0);
  int walls = 0, glass = 0, roof = 0, floor = 0, doors = 0;
  for (int dy = 0; dy < bd1; dy++)
    for (int dx = 0; dx < bw1; dx++)
    {
      for (int z = g1; z < g1 + 12 && z < (int)w->depth; z++)
      {
        VoxelType t = flat_voxel_at(w, ox1 + dx, oy1 + dy, z);
        if (t == VOXEL_WOOD_OAK || t == VOXEL_BRICK || t == VOXEL_ADOBE || t == VOXEL_PLASTER)
          walls++;
        else if (t == VOXEL_GLASS || t == VOXEL_GLASS_NS)
          glass++;
        else if (t == VOXEL_THATCH || t == VOXEL_THATCH_MIRROR || t == VOXEL_ROOF_TILE ||
                 t == VOXEL_ROOF_TILE_MIRROR)
          roof++;
        else if (t == VOXEL_PLANK || t == VOXEL_COBBLE)
          floor++;
        else if (t == VOXEL_DOOR || t == VOXEL_DOOR_NS)
          doors++;
      }
    }
  printf("       one-room walls=%d glass=%d roof=%d floor=%d doors=%d\n", walls, glass, roof,
         floor, doors);
  report("one-room has walls", walls > 40);
  report("one-room has glass windows", glass > 0);
  report("one-room has pitched roof (thatch/tile)", roof > 10);
  report("one-room has floor", floor > 5);
  report("one-room has door voxels", doors >= 2);
  report("doors do not block movement",
         !world_voxel_type_blocks_movement(VOXEL_DOOR) &&
             !world_voxel_type_blocks_movement(VOXEL_DOOR_NS));

  // --- Multi-room ---
  // Clear a fresh patch
  for (uint32_t y = 16; y < 32; y++)
    for (uint32_t x = 16; x < 32; x++)
    {
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
      for (uint32_t z = 3; z < w->depth; z++)
        world_set_voxel(w, x, y, z, VOXEL_AIR);
    }
  const int ox2 = 16, oy2 = 16, bw2 = 9, bd2 = 9;
  const int g2 =
      settlement_place_procedural(w, ox2, oy2, bw2, bd2, SETTLEMENT_PROC_MULTI_ROOM, &rng);
  report("multi-room places", g2 >= 0);
  // Interior partition: count wall voxels and door voxels strictly inside the footprint
  int interior_walls = 0;
  int interior_doors = 0;
  for (int dy = 1; dy < bd2 - 1; dy++)
    for (int dx = 1; dx < bw2 - 1; dx++)
    {
      VoxelType t = flat_voxel_at(w, ox2 + dx, oy2 + dy, g2 + 1);
      if (t == VOXEL_WOOD_OAK || t == VOXEL_BRICK || t == VOXEL_ADOBE || t == VOXEL_PLASTER)
        interior_walls++;
      if (t == VOXEL_DOOR || t == VOXEL_DOOR_NS)
        interior_doors++;
    }
  printf("       multi-room interior_walls=%d interior_doors=%d\n", interior_walls, interior_doors);
  report("multi-room has interior partition walls", interior_walls >= 3);
  report("multi-room has interior door voxels", interior_doors >= 1);

  // --- Multi-level ---
  for (uint32_t y = 4; y < 20; y++)
    for (uint32_t x = 28; x < 44; x++)
    {
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
      for (uint32_t z = 3; z < w->depth; z++)
        world_set_voxel(w, x, y, z, VOXEL_AIR);
    }
  const int ox3 = 28, oy3 = 4, bw3 = 8, bd3 = 8;
  const int g3 =
      settlement_place_procedural(w, ox3, oy3, bw3, bd3, SETTLEMENT_PROC_MULTI_LEVEL, &rng);
  report("multi-level places", g3 >= 0);
  // Upper floor slab around story height 4
  int upper_floor = count_type_in_box(w, ox3 + 1, oy3 + 1, g3 + 4, ox3 + bw3 - 2, oy3 + bd3 - 2,
                                      g3 + 4, VOXEL_PLANK) +
                    count_type_in_box(w, ox3 + 1, oy3 + 1, g3 + 4, ox3 + bw3 - 2, oy3 + bd3 - 2,
                                      g3 + 4, VOXEL_COBBLE);
  // Stairs: sparse stair wedges mid-story (not floor slabs at g or g+4).
  int stair_treads = 0;
  for (int z = g3 + 2; z <= g3 + 3; z++)
    stair_treads += count_type_in_box(w, ox3 + 1, oy3 + 1, z, ox3 + bw3 - 2, oy3 + bd3 - 2, z,
                                      VOXEL_STAIR) +
                    count_type_in_box(w, ox3 + 1, oy3 + 1, z, ox3 + bw3 - 2, oy3 + bd3 - 2, z,
                                      VOXEL_PLANK);
  // Tall walls above first story
  int tall_walls = 0;
  for (int z = g3 + 5; z <= g3 + 8; z++)
    for (int dy = 0; dy < bd3; dy++)
      for (int dx = 0; dx < bw3; dx++)
      {
        VoxelType t = flat_voxel_at(w, ox3 + dx, oy3 + dy, z);
        if (t == VOXEL_WOOD_OAK || t == VOXEL_BRICK || t == VOXEL_ADOBE || t == VOXEL_PLASTER)
          tall_walls++;
      }
  printf("       multi-level upper_floor=%d stair_treads=%d tall_walls=%d\n", upper_floor,
         stair_treads, tall_walls);
  report("multi-level has upper floor slab", upper_floor >= 8);
  report("multi-level has stair treads", stair_treads >= 2);
  report("multi-level has second-story walls", tall_walls > 20);

  // Tiny footprint clamps multi-level down to one-room.
  for (uint32_t y = 30; y < 36; y++)
    for (uint32_t x = 4; x < 10; x++)
    {
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
      for (uint32_t z = 3; z < w->depth; z++)
        world_set_voxel(w, x, y, z, VOXEL_AIR);
    }
  const int g_small =
      settlement_place_procedural(w, 4, 30, 4, 4, SETTLEMENT_PROC_MULTI_LEVEL, &rng);
  report("tiny footprint still places (clamped)", g_small >= 0);
  int tiny_upper = count_type_in_box(w, 5, 31, g_small + 4, 6, 32, g_small + 4, VOXEL_PLANK);
  report("tiny footprint does not build a second story", tiny_upper == 0);

  // --- Roof sits on wall plate (no floating gap) ---
  for (uint32_t y = 4; y < 16; y++)
    for (uint32_t x = 4; x < 16; x++)
    {
      world_set_voxel(w, x, y, 1, VOXEL_STONE);
      world_set_voxel(w, x, y, 2, VOXEL_GRASS);
      for (uint32_t z = 3; z < w->depth; z++)
        world_set_voxel(w, x, y, z, VOXEL_AIR);
    }
  uint32_t rng2 = 0xCAFEu;
  const int gh = settlement_place_procedural_typed(w, 4, 4, 8, 8, SETTLEMENT_BLDG_HALL, &rng2);
  report("hall places", gh >= 0);
  // On an eave edge cell, the voxel below the lowest roof cell must be wall or roof (no air gap).
  int float_gaps = 0;
  for (int dx = 0; dx < 8; dx++)
  {
    VoxelType edge = flat_voxel_at(w, 4 + dx, 4, gh + 1);
    // scan column for first roof material
    for (int z = gh + 1; z < gh + 16 && z < (int)w->depth; z++)
    {
      VoxelType t = flat_voxel_at(w, 4 + dx, 4, z);
      if (t == VOXEL_THATCH || t == VOXEL_THATCH_MIRROR || t == VOXEL_ROOF_TILE ||
          t == VOXEL_ROOF_TILE_MIRROR)
      {
        VoxelType below = flat_voxel_at(w, 4 + dx, 4, z - 1);
        if (below == VOXEL_AIR)
          float_gaps++;
        break;
      }
      (void)edge;
    }
  }
  printf("       floating roof gaps on south eave=%d\n", float_gaps);
  report("roof does not float above walls", float_gaps == 0);

  // Hall/tower want stone foundations.
  int foundation_stone = 0;
  for (int dy = 0; dy < 8; dy++)
    for (int dx = 0; dx < 8; dx++)
      if (flat_voxel_at(w, 4 + dx, 4 + dy, 2) == VOXEL_STONE ||
          flat_voxel_at(w, 4 + dx, 4 + dy, 2) == VOXEL_COBBLE)
        foundation_stone++;
  // Surface was grass at z=2; after stamp, grade/plinth uses stone.
  printf("       foundation stone/cobble cells at grade=%d\n", foundation_stone);
  report("hall has stone foundation footprint", foundation_stone >= 8);

  // Raised halls get 45° skirt wedges (same stone type, shape×orient modifier).
  int wedge_skirts = 0;
  for (int dx = -1; dx <= 8; dx++)
    for (int dy = -1; dy <= 8; dy++)
    {
      if (dx >= 0 && dx < 8 && dy >= 0 && dy < 8)
        continue;
      if (4 + dx < 0 || 4 + dy < 0)
        continue;
      const Voxel *v = world_get_voxel(w, (uint32_t)(4 + dx), (uint32_t)(4 + dy), 2);
      if (v && (v->type == VOXEL_STONE || v->type == VOXEL_COBBLE) &&
          voxel_get_shape(v) == VOXEL_SHAPE_WEDGE)
        wedge_skirts++;
    }
  printf("       foundation wedge skirts=%d\n", wedge_skirts);
  report("hall plinth has shape×orient wedge skirts", wedge_skirts >= 4);

  // Door faces the town center / pavement: hall south of plaza should open northward.
  {
    for (uint32_t y = 20; y < 36; y++)
      for (uint32_t x = 20; x < 36; x++)
      {
        world_set_voxel(w, x, y, 1, VOXEL_STONE);
        world_set_voxel(w, x, y, 2, VOXEL_GRASS);
        for (uint32_t z = 3; z < w->depth; z++)
          world_set_voxel(w, x, y, z, VOXEL_AIR);
      }
    w->settlement_has_town_center = true;
    w->settlement_town_cx = 28;
    w->settlement_town_cy = 22; // plaza south of the building
    // Plaza pavement south of the footprint.
    for (int dx = 0; dx < 6; dx++)
      world_set_voxel(w, (uint32_t)(22 + dx), 22, 2, VOXEL_STONE);
    uint32_t rng3 = 0xD00Fu;
    const int gf = settlement_place_procedural_typed(w, 22, 26, 8, 8, SETTLEMENT_BLDG_HOUSE, &rng3);
    report("town-facing house places", gf >= 0);
    int north_doors = 0, south_doors = 0;
    for (int z = gf + 1; z <= gf + 2 && z < (int)w->depth; z++)
    {
      if (flat_voxel_at(w, 22 + 4, 26, z) == VOXEL_DOOR ||
          flat_voxel_at(w, 22 + 4, 26, z) == VOXEL_DOOR_NS)
        south_doors++;
      if (flat_voxel_at(w, 22 + 4, 26 + 7, z) == VOXEL_DOOR ||
          flat_voxel_at(w, 22 + 4, 26 + 7, z) == VOXEL_DOOR_NS)
        north_doors++;
    }
    printf("       doors toward plaza: north_wall=%d south_wall=%d\n", north_doors, south_doors);
    report("door opens toward town/pavement (south)", south_doors >= 1 && north_doors == 0);
  }

  // Occupation mapping + furnishings.
  report("tower occupation is guard",
         settlement_occupation_for_building(SETTLEMENT_BLDG_TOWER, NULL) ==
             (uint8_t)VILLAGER_JOB_GUARD);
  report("shop occupation label resolves",
         settlement_occupation_label(
             settlement_occupation_for_building(SETTLEMENT_BLDG_SHOP, &rng2)) != NULL);

  int props = 0;
  for (uint32_t z = 0; z < w->depth; z++)
    for (uint32_t y = 4; y < 12; y++)
      for (uint32_t x = 4; x < 12; x++)
      {
        VoxelType t = flat_voxel_at(w, (int)x, (int)y, (int)z);
        if (t == VOXEL_BED || t == VOXEL_CRATE || t == VOXEL_BARREL || t == VOXEL_PAPER ||
            t == VOXEL_CERAMIC || t == VOXEL_IRON || t == VOXEL_CANDLE || t == VOXEL_TABLE ||
            t == VOXEL_CHAIR || t == VOXEL_CHEST)
          props++;
      }
  printf("       occupation prop voxels=%d (placed=%d)\n", props, w->settlement_placed_count);
  report("hall recorded a placed building", w->settlement_placed_count >= 1);
  report("hall has occupation furnishings", props >= 3);

  world_destroy(w);
}

static void test_perimeter_fences_and_fortress_ramparts(void)
{
  printf("-- perimeter fences / fortress ramparts --\n");

  report("fence type names resolve",
         strcmp(voxel_type_name(VOXEL_FENCE), "FENCE") == 0 &&
             strcmp(voxel_type_name(VOXEL_FENCE_NS), "FENCE_NS") == 0 &&
             strcmp(voxel_type_name(VOXEL_FENCE_WATTLE), "FENCE_WATTLE") == 0 &&
             strcmp(voxel_type_name(VOXEL_FENCE_IRON), "FENCE_IRON") == 0 &&
             strcmp(voxel_type_name(VOXEL_RAMPART), "RAMPART") == 0 &&
             strcmp(voxel_type_name(VOXEL_PARAPET), "PARAPET") == 0);
  report("fence/rampart voxels keep material gaps",
         world_voxel_type_has_material_gaps(VOXEL_FENCE) &&
             world_voxel_type_has_material_gaps(VOXEL_FENCE_WATTLE) &&
             world_voxel_type_has_material_gaps(VOXEL_FENCE_IRON) &&
             world_voxel_type_has_material_gaps(VOXEL_PARAPET) &&
             !world_voxel_type_has_material_gaps(VOXEL_RAMPART));

  // Scale 5 village: wooden fence perimeter.
  {
    World *w = make_flat_world(96, 96, 40);
    if (!w)
    {
      report("fence world created", false);
      return;
    }
    w->universe_x = 12;
    w->universe_y = -6;
    report("scale-5 stamp succeeds", settlement_stamp(w, 5, "fence_village"));
    int fence = 0, wattle = 0, rampart = 0;
    for (uint32_t z = 0; z < w->depth; z++)
      for (uint32_t y = 0; y < w->height; y++)
        for (uint32_t x = 0; x < w->width; x++)
        {
          VoxelType t = flat_voxel_at(w, (int)x, (int)y, (int)z);
          if (t == VOXEL_FENCE || t == VOXEL_FENCE_NS)
            fence++;
          else if (t == VOXEL_FENCE_WATTLE)
            wattle++;
          else if (t == VOXEL_RAMPART)
            rampart++;
        }
    printf("       scale5 fence=%d wattle=%d rampart=%d\n", fence, wattle, rampart);
    report("village has wooden fence perimeter", fence >= 40);
    report("village has no wattle/rampart", wattle == 0 && rampart == 0);

    // Edge alignment: west/east runs use FENCE; north/south runs use FENCE_NS.
    // Skip corners (they prefer the NS-authored mesh).
    int west_fence = 0, west_ns = 0, south_fence = 0, south_ns = 0;
    int fence_xmin = (int)w->width, fence_xmax = -1, fence_ymin = (int)w->height, fence_ymax = -1;
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
        for (uint32_t z = 3; z < 8 && z < w->depth; z++)
        {
          VoxelType t = flat_voxel_at(w, (int)x, (int)y, (int)z);
          if (t != VOXEL_FENCE && t != VOXEL_FENCE_NS)
            continue;
          if ((int)x < fence_xmin)
            fence_xmin = (int)x;
          if ((int)x > fence_xmax)
            fence_xmax = (int)x;
          if ((int)y < fence_ymin)
            fence_ymin = (int)y;
          if ((int)y > fence_ymax)
            fence_ymax = (int)y;
        }
    for (int y = fence_ymin + 2; y <= fence_ymax - 2; y++)
      for (uint32_t z = 3; z < 8 && z < w->depth; z++)
      {
        VoxelType t = flat_voxel_at(w, fence_xmin, y, (int)z);
        if (t == VOXEL_FENCE)
          west_fence++;
        else if (t == VOXEL_FENCE_NS)
          west_ns++;
      }
    for (int x = fence_xmin + 2; x <= fence_xmax - 2; x++)
      for (uint32_t z = 3; z < 8 && z < w->depth; z++)
      {
        VoxelType t = flat_voxel_at(w, x, fence_ymin, (int)z);
        if (t == VOXEL_FENCE)
          south_fence++;
        else if (t == VOXEL_FENCE_NS)
          south_ns++;
      }
    printf("       orient west(FENCE=%d NS=%d) south(FENCE=%d NS=%d) xmin=%d ymin=%d\n",
           west_fence, west_ns, south_fence, south_ns, fence_xmin, fence_ymin);
    report("west fence run uses VOXEL_FENCE", west_fence > 0 && west_ns == 0);
    report("south fence run uses VOXEL_FENCE_NS", south_ns > 0 && south_fence == 0);
    world_destroy(w);
  }

  // Scale 6 hamlet: wattle fence.
  {
    World *w = make_flat_world(96, 96, 40);
    if (!w)
    {
      report("wattle world created", false);
      return;
    }
    w->universe_x = 14;
    w->universe_y = -8;
    report("scale-6 stamp succeeds", settlement_stamp(w, 6, "wattle_town"));
    int wattle = 0, wood = 0;
    for (uint32_t z = 0; z < w->depth; z++)
      for (uint32_t y = 0; y < w->height; y++)
        for (uint32_t x = 0; x < w->width; x++)
        {
          VoxelType t = flat_voxel_at(w, (int)x, (int)y, (int)z);
          if (t == VOXEL_FENCE_WATTLE)
            wattle++;
          else if (t == VOXEL_FENCE || t == VOXEL_FENCE_NS)
            wood++;
        }
    printf("       scale6 wattle=%d wood_fence=%d\n", wattle, wood);
    report("scale-6 has wattle fence", wattle >= 40);
    report("scale-6 has no wood fence", wood == 0);

    // Wattle uses one mesh: NS runs yaw≈0, EW runs yaw≈π/2, and neighbours share yaw.
    int wattle_xmin = (int)w->width, wattle_xmax = -1, wattle_ymin = (int)w->height,
        wattle_ymax = -1;
    for (uint32_t y = 0; y < w->height; y++)
      for (uint32_t x = 0; x < w->width; x++)
        for (uint32_t z = 3; z < 8 && z < w->depth; z++)
        {
          if (flat_voxel_at(w, (int)x, (int)y, (int)z) != VOXEL_FENCE_WATTLE)
            continue;
          if ((int)x < wattle_xmin)
            wattle_xmin = (int)x;
          if ((int)x > wattle_xmax)
            wattle_xmax = (int)x;
          if ((int)y < wattle_ymin)
            wattle_ymin = (int)y;
          if ((int)y > wattle_ymax)
            wattle_ymax = (int)y;
        }
    float west_yaw = -1.0f, south_yaw = -1.0f;
    int west_yaw_mismatch = 0, south_yaw_mismatch = 0, west_n = 0, south_n = 0;
    for (int y = wattle_ymin + 2; y <= wattle_ymax - 2; y++)
      for (uint32_t z = 3; z < 8 && z < w->depth; z++)
      {
        const Voxel *v = world_get_voxel(w, (uint32_t)wattle_xmin, (uint32_t)y, z);
        if (!v || v->type != VOXEL_FENCE_WATTLE)
          continue;
        const float yaw = voxel_get_yaw_radians(v);
        if (west_yaw < 0.0f)
          west_yaw = yaw;
        else if (fabsf(yaw - west_yaw) > 0.05f)
          west_yaw_mismatch++;
        west_n++;
      }
    for (int x = wattle_xmin + 2; x <= wattle_xmax - 2; x++)
      for (uint32_t z = 3; z < 8 && z < w->depth; z++)
      {
        const Voxel *v = world_get_voxel(w, (uint32_t)x, (uint32_t)wattle_ymin, z);
        if (!v || v->type != VOXEL_FENCE_WATTLE)
          continue;
        const float yaw = voxel_get_yaw_radians(v);
        if (south_yaw < 0.0f)
          south_yaw = yaw;
        else if (fabsf(yaw - south_yaw) > 0.05f)
          south_yaw_mismatch++;
        south_n++;
      }
    printf("       wattle yaw west=%.2f (n=%d mism=%d) south=%.2f (n=%d mism=%d)\n", west_yaw,
           west_n, west_yaw_mismatch, south_yaw, south_n, south_yaw_mismatch);
    report("west wattle yaw is ~0 (NS run)", west_n > 0 && fabsf(west_yaw) < 0.1f);
    report("south wattle yaw is ~π/2 (EW run)",
           south_n > 0 && fabsf(south_yaw - (float)M_PI * 0.5f) < 0.1f);
    report("wattle neighbours share edge yaw", west_yaw_mismatch == 0 && south_yaw_mismatch == 0);
    world_destroy(w);
  }

  // Scale 9 fortress: thick stone walls, ramparts, parapets.
  {
    World *w = make_flat_world(112, 112, 48);
    if (!w)
    {
      report("fortress world created", false);
      return;
    }
    w->universe_x = 20;
    w->universe_y = -10;
    report("scale-9 stamp succeeds", settlement_stamp(w, 9, "fortress_ramparts"));
    int stone_wall = 0, rampart = 0, parapet = 0, iron = 0;
    for (uint32_t z = 0; z < w->depth; z++)
      for (uint32_t y = 0; y < w->height; y++)
        for (uint32_t x = 0; x < w->width; x++)
        {
          VoxelType t = flat_voxel_at(w, (int)x, (int)y, (int)z);
          // Count elevated stone on the outer ring as curtain (above grade).
          if (t == VOXEL_STONE && z >= 4)
            stone_wall++;
          else if (t == VOXEL_RAMPART)
            rampart++;
          else if (t == VOXEL_PARAPET)
            parapet++;
          else if (t == VOXEL_FENCE_IRON)
            iron++;
        }
    printf("       scale9 elevated_stone=%d rampart=%d parapet=%d iron=%d\n", stone_wall, rampart,
           parapet, iron);
    report("fortress has elevated stone walls", stone_wall >= 200);
    report("fortress has rampart walkway", rampart >= 40);
    report("fortress has parapet merlons", parapet >= 20);
    report("fortress has iron gate posts", iron >= 8);
    world_destroy(w);
  }
}

int main(void)
{
  printf("=== Settlement Tests ===\n");
  test_scale_range_and_adjacency();
  test_stamp_places_buildings();
  test_home_drop_shack_anchor();
  test_inter_settlement_roads();
  test_nearest_scale_five();
  test_settlement_labels_and_fauna_policy();
  test_building_type_mix();
  test_procedural_buildings();
  test_perimeter_fences_and_fortress_ramparts();
  printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
