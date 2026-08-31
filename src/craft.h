#ifndef VERSE_CRAFT_H
#define VERSE_CRAFT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "item.h"
#include "voxel.h"

struct World;

// Workstations. HAND is pocket crafting / the recipe book; the others are world voxels.
typedef enum {
    CRAFT_STATION_HAND = 0,
    CRAFT_STATION_TABLE,
    CRAFT_STATION_ANVIL,
    CRAFT_STATION_FORGE,
    CRAFT_STATION_COUNT
} CraftStation;

#define CRAFT_RECIPE_INPUT_MAX 4

typedef struct {
    ItemId id;
    uint32_t pieces;
} CraftIngredient;

typedef struct {
    const char *name;
    const char *blurb;
    CraftStation station;
    CraftIngredient inputs[CRAFT_RECIPE_INPUT_MAX];
    uint8_t input_count;
    ItemId output_id;
    uint32_t output_pieces;
    bool output_gear;
    ItemMaterial output_material; // ITEM_MAT_NONE → item default
    uint8_t output_quality;       // 0 → 60
} CraftRecipe;

typedef struct {
    bool active;
    CraftStation station;
    bool book_mode; // true = recipe book (browse all); false = station-filtered
    int selected;   // index into the filtered view
    int scroll;
} CraftSession;

void craft_session_clear(CraftSession *session);
void craft_session_open(CraftSession *session, CraftStation station, bool book_mode);

const char *craft_station_name(CraftStation station);
CraftStation craft_station_from_voxel(VoxelType type);
bool craft_voxel_is_station(VoxelType type);

int craft_recipe_count(void);
const CraftRecipe *craft_recipe_at(int index);

// Fill out_indices with recipe catalog indices visible for this session. Returns count.
int craft_session_list_recipes(const CraftSession *session, int *out_indices, int max_out);

bool craft_can_craft(const Inventory *inv, const CraftRecipe *recipe);
bool craft_apply(Inventory *inv, const CraftRecipe *recipe, char *status, size_t status_sz);

// Nearest craft-station voxel within range.
bool craft_find_nearby_station(struct World *world, float x, float y, float z, float range,
                               CraftStation *out_station, int *out_vx, int *out_vy, int *out_vz);

#endif // VERSE_CRAFT_H
