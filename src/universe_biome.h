#ifndef UNIVERSE_BIOME_H
#define UNIVERSE_BIOME_H

#include <stdint.h>
#include "world.h"

// Universe-continuous climate / biome field for wilderness (and any world with
// universe_x/y/z set). Sampled at absolute coordinates so adjacent cells agree.

typedef enum {
  UNIVERSE_BIOME_TEMPERATE = 0,
  UNIVERSE_BIOME_GRASSLAND,
  UNIVERSE_BIOME_BOREAL,
  UNIVERSE_BIOME_DESERT,
  UNIVERSE_BIOME_WETLAND,
  UNIVERSE_BIOME_ALPINE,
  UNIVERSE_BIOME_TROPICAL,
  UNIVERSE_BIOME_VOLCANIC,
  UNIVERSE_BIOME_COUNT
} UniverseBiomeId;

typedef struct {
  float temperature; // 0 cold .. 1 hot
  float moisture;    // 0 arid .. 1 wet
  float elevation;   // 0 lowland .. 1 highland (noise proxy or height ratio)
  float volcanic;    // 0 none .. 1 strong volcanic province
} UniverseClimate;

typedef struct {
  UniverseBiomeId primary;
  float weights[UNIVERSE_BIOME_COUNT]; // soft blend; sum ~= 1
} UniverseBiomeSample;

// Climate axes at local (x,y). elevation_override < 0 uses a large-scale height proxy.
UniverseClimate universe_climate_sample(const World *world, uint32_t x, uint32_t y,
                                        float elevation_override);

UniverseBiomeSample universe_biome_classify(const UniverseClimate *climate);

// Convenience: climate + classify at a column.
UniverseBiomeSample universe_biome_at(const World *world, uint32_t x, uint32_t y,
                                      float elevation_override);

UniverseBiomeId universe_biome_primary_at(const World *world, uint32_t x, uint32_t y);

// Rain intensity hint in [0,1] from biome soft weights (for particle FX).
float universe_biome_rain_intensity(const UniverseBiomeSample *sample);

const char *universe_biome_name(UniverseBiomeId id);

// Drive light/heavy rain particle FX from climate. Safe on any world type;
// non-wilderness clears climate-driven rain (leaves console heavy rain alone if already on).
void universe_biome_apply_ambient_weather(World *world);

#endif // UNIVERSE_BIOME_H
