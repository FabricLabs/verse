#ifndef FIRE_SIM_H
#define FIRE_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel.h"
#include "world.h"

// Fire lives on the voxel condition mask (BURNING_LOW / MEDIUM / HIGH) and is put out by water
// (adjacent VOXEL_WATER, a water cell, or the WET condition). Dry flammable surfaces catch from
// contact with a burning neighbour or a fireball impact.

bool voxel_type_is_flammable(VoxelType type);

// True when the voxel carries any BURNING_* bit.
bool fire_voxel_is_burning(const Voxel *voxel);

// True when the voxel carries WET, or is itself a water/ice fluid cell.
bool fire_voxel_is_wet(const Voxel *voxel);

// A surface that can catch: flammable type, dry, and not already burning.
bool fire_can_ignite_at(const World *world, int x, int y, int z);

// Light a dry flammable cell (or re-light a candle/campfire). intensity is one of
// "BURNING_LOW", "BURNING_MEDIUM", "BURNING_HIGH". Returns true if the cell is burning after.
bool fire_ignite_at(World *world, int x, int y, int z, const char *intensity);

// Strip every BURNING_* bit. Returns true if anything was cleared.
bool fire_extinguish_at(World *world, int x, int y, int z);

// Extinguish if this cell is wet / water, or has a water neighbour. Returns true if extinguished.
bool fire_try_extinguish_with_water(World *world, int x, int y, int z);

// One simulation tick: put out fires that touch water, spread to dry neighbours, consume soft fuel.
void fire_sim_step(World *world, float dt_seconds);

#endif // FIRE_SIM_H
