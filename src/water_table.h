#ifndef WATER_TABLE_H
#define WATER_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Groundwater saturation in permeable solids.
//
// Open VOXEL_WATER only moves through air (fluid_sim). A water table needs the pore space of
// stone, sand, and soil to hold moisture without turning those cells into free fluid — otherwise
// terrain collapses into underground lakes. Saturated solids keep their type and store wetness in
// the quantity field (0..WATER_TABLE_WETNESS_MAX), the same channel the renderer already blue-shifts.
//
// Wilderness generation builds the table after mountain rivulets settle: free water and climate
// moisture seed a hydrostatic column, then wetness diffuses through permeable neighbours so
// aquifers connect under ridges. Runtime transfers from standing water seep one step into the bed.

#define WATER_TABLE_WETNESS_MAX 6

// Relative hydraulic conductivity 0..8. Zero means impermeable (bedrock, ores, wood, fluids).
uint8_t water_table_permeability(VoxelType type);
bool water_table_is_permeable(VoxelType type);

// Raise wetness on a permeable solid toward WATER_TABLE_WETNESS_MAX. No-op on fluids / impermeable.
// Returns true when the cell's wetness increased.
bool water_table_wet(Voxel *v, uint8_t target);

// Wilderness post-weather pass. `tops` is column solid surface z (or -1), same layout as erosion.
// Safe no-op on a null world. Deterministic for a given salt and voxel field.
void water_table_build(World *world, const int *tops, uint32_t salt);

// After water moves from (ax,ay,az) → (bx,by,bz), charge permeable solids under / beside the path.
void water_table_on_water_transfer(World *world, int ax, int ay, int az, int bx, int by, int bz,
                                   int flow_amount);

// One seep step from a water cell into permeable neighbours (down first, then lateral).
void water_table_seep_from(World *world, int x, int y, int z);

#endif // WATER_TABLE_H
