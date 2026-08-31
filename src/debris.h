#ifndef VERSE_DEBRIS_H
#define VERSE_DEBRIS_H

#include <stdbool.h>
#include <stdint.h>

#include "item.h"
#include "voxel.h"
#include "voxel_combat.h"
#include "world.h"

// Broken voxels scatter as 4x4x4 subcomponent chunks. Each active piece is one such chunk visually
// (edge length VOXEL_DROP_EDGE / MATERIAL_WORLD_SIZE of a parent voxel) and carries one or more
 // piece-units toward the fractional inventory total of VOXEL_PIECES_PER_BLOCK.

#define DEBRIS_MAX 256

// How many distinct flying chunks a destroyed voxel usually produces. Their piece counts sum to a
// full block (or less when the material does not drop).
#define DEBRIS_BURST_COUNT 32

#define DEBRIS_PICKUP_RADIUS 1.35f
#define DEBRIS_SIZE ((float)VOXEL_DROP_EDGE / (float)MATERIAL_WORLD_SIZE)

typedef struct
{
  bool active;
  float x, y, z;
  float vx, vy, vz;
  VoxelType type;
  ItemId item;
  uint32_t pieces; // inventory units this entity contributes on pickup
  // Gear instance fields (ignored for stackables / voxel debris).
  uint16_t durability;
  uint16_t durability_max;
  uint8_t quality;
  uint8_t material;
  uint8_t r, g, b;
  float life; // seconds before despawn if never collected
  bool settled;
  // Inventory / loot drops render the item icon instead of a tinted cube.
  bool icon_sprite;
} DebrisPiece;

typedef struct
{
  DebrisPiece items[DEBRIS_MAX];
  uint32_t spawned_total;
  uint32_t dropped_total; // refused because the pool was full
} DebrisSystem;

void debris_system_reset(DebrisSystem *sys);

// Scatter pieces from a destroyed voxel at its centre. `total_pieces` should normally be
 // VOXEL_PIECES_PER_BLOCK for a full block. Returns how many entities were created.
int debris_spawn_from_voxel(DebrisSystem *sys, World *world, float x, float y, float z,
                            VoxelType type, uint32_t total_pieces, uint32_t seed);

// Drop a full inventory stack near (x,y,z) as a single icon sprite. Returns true on spawn.
bool debris_spawn_item_stack(DebrisSystem *sys, World *world, float x, float y, float z,
                             const ItemStack *stack, uint32_t seed);

void debris_system_step(DebrisSystem *sys, World *world, float dt, float gravity);

// Collect pieces near (px,py,pz) into `inv`. Returns pieces collected this call.
uint32_t debris_try_pickup(DebrisSystem *sys, Inventory *inv, float px, float py, float pz,
                           float radius);

int debris_active_count(const DebrisSystem *sys);

#endif // VERSE_DEBRIS_H
