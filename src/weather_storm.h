#ifndef WEATHER_STORM_H
#define WEATHER_STORM_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h"

// Thunderstorm weather: heavy rain plus occasional lightning strikes that share the volcano
// conduit's jagged path grammar and can ignite dry flammables at the impact.
//
// Enable flags live on WorldParticleEffects (see particle_effects.h); this module owns the
// strike / volcano-pump cadence.

// One weather-tick pulse: maybe strike lightning (and pump any local volcano). Call from the
// same cadence as ambient biome rain (~2s). Safe on null worlds.
void weather_storm_tick(World *world, uint32_t tick_salt);

// Force a single strike at (x,y) for tests. Returns true if something was ignited.
bool weather_storm_strike_at(World *world, int x, int y, uint32_t seed);

#endif // WEATHER_STORM_H
