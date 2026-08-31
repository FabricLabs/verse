#ifndef VOLCANO_H
#define VOLCANO_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Wilderness volcanoes: a lightning-shaped magma conduit from bedrock to the surface.
//
// Occasional wilderness cells (biased toward volcanic climate) stamp one vent. Columns along the
// path are registered as magma hotspots so fluid_sim keeps the conduit molten. A rare pump event
// spills fresh lava at the crater.

#define WORLD_MAGMA_VENT_COLUMN_MAX 256

// Register a column as a sustained magma vent (idempotent). Used by the volcano stamp so conduit
// cells survive fluid_sim's cool-to-basalt pass.
bool world_register_magma_vent_column(World *world, int x, int y);

// True when (x,y) was registered as a vent column (not the noise hotspot test).
bool world_has_magma_vent_column(const World *world, int x, int y);

// Clear vent registrations (world destroy / regenerate).
void world_clear_magma_vent_columns(World *world);

// Post-surface wilderness stamp. `tops` is column solid surface z (or -1), same layout as erosion.
// Returns true when a volcano was placed. Safe no-op on towns / small worlds / null tops.
bool wilderness_stamp_volcano(World *world, const int *tops, uint32_t salt);

// Rare pump: place a dollop of magma at the crater and wake fluid_sim. Returns true if lava spilled.
bool volcano_try_pump(World *world, uint32_t tick_salt);

#endif // VOLCANO_H
