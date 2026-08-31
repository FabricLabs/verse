#ifndef WATER_EROSION_H
#define WATER_EROSION_H

#include <stdint.h>

#include "world.h"

// Hydraulic erosion from flowing water.
//
// A solid under continuous lateral flow takes between WATER_EROSION_MIN_MS (soft turf/soil) and
// WATER_EROSION_MAX_MS (granite/basalt) of realtime physics to wear away. Bedrock never erodes.
// Standing water that has finished settling does not abrade: only transfers move material.
//
// Create Water and springs already wake fluid_sim; abrasion is a side effect of those transfers.
// Wilderness generation calls water_erosion_simulate_weather to cut shallow mountain rivulets
// along downhill accumulation paths — not a full-surface storm carve.

// Physics tick length the carve times are calibrated against (matches WORLD_PHYSICS_TICK_MS).
#define WATER_EROSION_TICK_MS 50

// Soft materials (grass coat) under continuous trickle.
#define WATER_EROSION_MIN_MS (10 * 60 * 1000)
// Hard rock (granite / basalt) under the same trickle.
#define WATER_EROSION_MAX_MS (60 * 60 * 1000)

// Multiply abrasion for accelerated geologic passes (world gen). 1 = realtime.
void water_erosion_set_rate_scale(int scale);
int water_erosion_rate_scale(void);

// Abrade the solid at (x,y,z) because `flow_amount` levels of water moved over or against it.
// `step_index` makes sub-threshold wear deterministic across ticks. Returns true if the cell was
// removed or degraded this call.
bool water_erosion_abrade(World *world, int x, int y, int z, int flow_amount,
                          uint64_t step_index);

// After a water transfer from (ax,ay,az) → (bx,by,bz), scour the bed under the path.
void water_erosion_on_water_transfer(World *world, int ax, int ay, int az, int bx, int by, int bz,
                                     int flow_amount, uint64_t step_index);

// Wilderness post-cap weather: trace shallow downhill rivulets on elevated slopes where runoff
// collects, place thin water in sinks, refresh `tops` (column solid surface z, or -1).
// Safe no-op on a null world. Does not blanket-abrade the surface.
void water_erosion_simulate_weather(World *world, uint32_t salt, int *tops);

#endif // WATER_EROSION_H
