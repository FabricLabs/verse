#include "wfc3d.h"
#include <stdlib.h>
#include <string.h>

bool wfc3_model_build(Wfc3Model *model, const Wfc3Tile *tiles, int tile_count)
{
  if (!model || !tiles || tile_count <= 0)
    return false;
  memset(model, 0, sizeof(*model));
  model->tiles = tiles;
  model->tile_count = tile_count;
  for (int d = 0; d < 6; d++)
  {
    model->allowed[d] = (bool *)calloc((size_t)tile_count * (size_t)tile_count, sizeof(bool));
    if (!model->allowed[d])
      return false;
  }
  for (int a = 0; a < tile_count; a++)
  {
    for (int b = 0; b < tile_count; b++)
    {
      bool n_ok = tiles[a].edge[WFC3_DIR_NORTH] == tiles[b].edge[WFC3_DIR_SOUTH];
      bool s_ok = tiles[a].edge[WFC3_DIR_SOUTH] == tiles[b].edge[WFC3_DIR_NORTH];
      bool e_ok = tiles[a].edge[WFC3_DIR_EAST] == tiles[b].edge[WFC3_DIR_WEST];
      bool w_ok = tiles[a].edge[WFC3_DIR_WEST] == tiles[b].edge[WFC3_DIR_EAST];
      bool u_ok = tiles[a].edge[WFC3_DIR_UP] == tiles[b].edge[WFC3_DIR_DOWN];
      bool d_ok = tiles[a].edge[WFC3_DIR_DOWN] == tiles[b].edge[WFC3_DIR_UP];
      model->allowed[WFC3_DIR_NORTH][a * tile_count + b] = n_ok;
      model->allowed[WFC3_DIR_SOUTH][a * tile_count + b] = s_ok;
      model->allowed[WFC3_DIR_EAST][a * tile_count + b] = e_ok;
      model->allowed[WFC3_DIR_WEST][a * tile_count + b] = w_ok;
      model->allowed[WFC3_DIR_UP][a * tile_count + b] = u_ok;
      model->allowed[WFC3_DIR_DOWN][a * tile_count + b] = d_ok;
    }
  }
  return true;
}

void wfc3_model_free(Wfc3Model *model)
{
  if (!model)
    return;
  for (int d = 0; d < 6; d++)
  {
    if (model->allowed[d])
      free(model->allowed[d]);
    model->allowed[d] = NULL;
  }
  model->tiles = NULL;
  model->tile_count = 0;
}

void wfc3_build_gameworld_model(Wfc3Model *model)
{
  // Socket IDs for vertical layering
  // We use distinct codes so only intended neighbors match.
  enum
  {
    S_ANY = 0,
    // Horizontal sockets (top -> bottom)
    S_EMPTY = 1,
    S_HOME  = 2,
    S_CLOUD = 3,
    S_WILD  = 4,
    S_BED_U = 5,
    S_UNDER = 6,
    S_BED_L = 7,
    // Vertical interface sockets (top -> bottom)
    S_SKY         = 8,  // empty up (terminal)
    S_HOME_EMPTY  = 9,  // home up <-> empty down
    S_CLOUD_HOME  = 10, // cloud up <-> home down
    S_WILD_CLOUD  = 11, // wilderness up <-> cloud down
    S_WILD_BEDU   = 12, // wilderness down <-> bedrock-upper up
    S_BEDU_UNDER  = 13, // bedrock-upper down <-> underworld up
    S_UNDER_BEDL  = 14, // underworld down <-> bedrock-lower up
    S_VOID        = 15  // bedrock-lower down (terminal)
  };

  // Define tiles for each layer; enforce horizontal sockets per layer
  static Wfc3Tile tiles[8];
  // Wilderness layer tile
  tiles[0].name = "wilderness";
  tiles[0].edge[WFC3_DIR_NORTH] = S_WILD;
  tiles[0].edge[WFC3_DIR_EAST] = S_WILD;
  tiles[0].edge[WFC3_DIR_SOUTH] = S_WILD;
  tiles[0].edge[WFC3_DIR_WEST] = S_WILD;
  tiles[0].edge[WFC3_DIR_UP] = S_WILD_CLOUD;  // must meet cloud's down
  tiles[0].edge[WFC3_DIR_DOWN] = S_WILD_BEDU; // must meet bedrock-upper up
  tiles[0].weight = 1.0f;

  // Town tile: shares wilderness vertical rules; laterally compatible with wilderness
  tiles[7].name = "town";
  tiles[7].edge[WFC3_DIR_NORTH] = S_WILD;
  tiles[7].edge[WFC3_DIR_EAST] = S_WILD;
  tiles[7].edge[WFC3_DIR_SOUTH] = S_WILD;
  tiles[7].edge[WFC3_DIR_WEST] = S_WILD;
  tiles[7].edge[WFC3_DIR_UP] = S_WILD_CLOUD;   // same as wilderness
  tiles[7].edge[WFC3_DIR_DOWN] = S_WILD_BEDU;  // same as wilderness
  tiles[7].weight = 0.6f; // slightly rarer than wilderness by default

  // Cloud layer tile (sits above wilderness)
  tiles[1].name = "cloud";
  tiles[1].edge[WFC3_DIR_NORTH] = S_CLOUD;
  tiles[1].edge[WFC3_DIR_EAST] = S_CLOUD;
  tiles[1].edge[WFC3_DIR_SOUTH] = S_CLOUD;
  tiles[1].edge[WFC3_DIR_WEST] = S_CLOUD;
  tiles[1].edge[WFC3_DIR_UP] = S_CLOUD_HOME;   // must meet home's down
  tiles[1].edge[WFC3_DIR_DOWN] = S_WILD_CLOUD; // must meet wilderness up
  tiles[1].weight = 1.0f;

  // Home layer tile (sits above clouds)
  tiles[5].name = "home";
  tiles[5].edge[WFC3_DIR_NORTH] = S_HOME;
  tiles[5].edge[WFC3_DIR_EAST] = S_HOME;
  tiles[5].edge[WFC3_DIR_SOUTH] = S_HOME;
  tiles[5].edge[WFC3_DIR_WEST] = S_HOME;
  tiles[5].edge[WFC3_DIR_UP] = S_HOME_EMPTY;   // must meet empty down
  tiles[5].edge[WFC3_DIR_DOWN] = S_CLOUD_HOME; // must meet cloud up
  tiles[5].weight = 1.0f;

  // Empty layer tile (sits above home)
  tiles[6].name = "empty";
  tiles[6].edge[WFC3_DIR_NORTH] = S_EMPTY;
  tiles[6].edge[WFC3_DIR_EAST] = S_EMPTY;
  tiles[6].edge[WFC3_DIR_SOUTH] = S_EMPTY;
  tiles[6].edge[WFC3_DIR_WEST] = S_EMPTY;
  tiles[6].edge[WFC3_DIR_UP] = S_SKY;          // terminal up
  tiles[6].edge[WFC3_DIR_DOWN] = S_HOME_EMPTY; // must meet home up
  tiles[6].weight = 1.0f;

  // Bedrock (upper) tile (immediately below wilderness)
  tiles[2].name = "bedrock_upper";
  tiles[2].edge[WFC3_DIR_NORTH] = S_BED_U;
  tiles[2].edge[WFC3_DIR_EAST] = S_BED_U;
  tiles[2].edge[WFC3_DIR_SOUTH] = S_BED_U;
  tiles[2].edge[WFC3_DIR_WEST] = S_BED_U;
  tiles[2].edge[WFC3_DIR_UP] = S_WILD_BEDU;    // must meet wilderness down
  tiles[2].edge[WFC3_DIR_DOWN] = S_BEDU_UNDER; // must meet underworld up
  tiles[2].weight = 1.0f;

  // Underworld tile (between two bedrock layers)
  tiles[3].name = "underworld";
  tiles[3].edge[WFC3_DIR_NORTH] = S_UNDER;
  tiles[3].edge[WFC3_DIR_EAST] = S_UNDER;
  tiles[3].edge[WFC3_DIR_SOUTH] = S_UNDER;
  tiles[3].edge[WFC3_DIR_WEST] = S_UNDER;
  tiles[3].edge[WFC3_DIR_UP] = S_BEDU_UNDER;   // must meet bedrock-upper down
  tiles[3].edge[WFC3_DIR_DOWN] = S_UNDER_BEDL; // must meet bedrock-lower up
  tiles[3].weight = 1.0f;

  // Bedrock (lower) tile (below underworld)
  tiles[4].name = "bedrock_lower";
  tiles[4].edge[WFC3_DIR_NORTH] = S_BED_L;
  tiles[4].edge[WFC3_DIR_EAST] = S_BED_L;
  tiles[4].edge[WFC3_DIR_SOUTH] = S_BED_L;
  tiles[4].edge[WFC3_DIR_WEST] = S_BED_L;
  tiles[4].edge[WFC3_DIR_UP] = S_UNDER_BEDL; // must meet underworld down
  tiles[4].edge[WFC3_DIR_DOWN] = S_VOID;     // terminal
  tiles[4].weight = 1.0f;

  // Build adjacency from sockets
  wfc3_model_build(model, tiles, 8);

  // Town shares wilderness lateral sockets so it can sit beside wilderness, but two towns must
  // never be adjacent. Clear town↔town on the four horizontal directions after the socket pass.
  if (model && model->allowed[0] && model->tile_count == 8)
  {
    const int town = 7;
    const int n = model->tile_count;
    const int dirs[4] = {WFC3_DIR_NORTH, WFC3_DIR_EAST, WFC3_DIR_SOUTH, WFC3_DIR_WEST};
    for (int i = 0; i < 4; i++)
    {
      model->allowed[dirs[i]][town * n + town] = false;
    }
  }
}
