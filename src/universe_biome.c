#include "universe_biome.h"
#include "entropy_field.h"
#include "universe_coords.h"
#include "particle_effects.h"

#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static float sample_axis(const World *world, uint32_t x, uint32_t y, uint32_t seed_off,
                         float layer1_scale)
{
  if (!world)
    return 0.5f;

  UniverseCoord uc = get_world_universe_coords_f(
      (int32_t)world->universe_x, (int32_t)world->universe_y, (int32_t)world->universe_z,
      (float)x, (float)y, 0.0f, world->width, world->height, world->depth);

  EntropyField field = entropy_field_custom(seed_off, layer1_scale, 0.55f, layer1_scale * 8.0f,
                                            0.30f, layer1_scale * 40.0f, 0.15f, true, 3.0f,
                                            layer1_scale * 2.0f);
  return clampf(entropy_field_sample_2d(&field, uc.x, uc.y), 0.0f, 1.0f);
}

UniverseClimate universe_climate_sample(const World *world, uint32_t x, uint32_t y,
                                        float elevation_override)
{
  UniverseClimate c;
  c.temperature = sample_axis(world, x, y, 0x71C3A901u, 0.00055f);
  c.moisture = sample_axis(world, x, y, 0xA3B17E55u, 0.00062f);
  c.volcanic = sample_axis(world, x, y, 0x5EEDF00Du, 0.00040f);
  if (elevation_override >= 0.0f)
    c.elevation = clampf(elevation_override, 0.0f, 1.0f);
  else
    c.elevation = sample_axis(world, x, y, 0xE7E7A710u, 0.00048f);
  return c;
}

// Ideal (temp, moisture, elev) centers for distance scoring.
static const float s_centers[UNIVERSE_BIOME_COUNT][3] = {
    /* temperate */ {0.50f, 0.55f, 0.30f},
    /* grassland */ {0.55f, 0.32f, 0.35f},
    /* boreal    */ {0.22f, 0.48f, 0.42f},
    /* desert    */ {0.78f, 0.12f, 0.28f},
    /* wetland   */ {0.48f, 0.88f, 0.18f},
    /* alpine    */ {0.28f, 0.40f, 0.88f},
    /* tropical  */ {0.88f, 0.78f, 0.22f},
    /* volcanic  */ {0.70f, 0.25f, 0.45f},
};

UniverseBiomeSample universe_biome_classify(const UniverseClimate *climate)
{
  UniverseBiomeSample out;
  memset(&out, 0, sizeof(out));
  if (!climate)
  {
    out.primary = UNIVERSE_BIOME_TEMPERATE;
    out.weights[UNIVERSE_BIOME_TEMPERATE] = 1.0f;
    return out;
  }

  float scores[UNIVERSE_BIOME_COUNT];
  float sum = 0.0f;
  for (int i = 0; i < UNIVERSE_BIOME_COUNT; i++)
  {
    float dt = climate->temperature - s_centers[i][0];
    float dm = climate->moisture - s_centers[i][1];
    float de = climate->elevation - s_centers[i][2];
    float dist2 = dt * dt + dm * dm + de * de;
    // Soft inverse-distance; sharper near centers.
    float s = 1.0f / (0.04f + dist2);
    if (i == UNIVERSE_BIOME_VOLCANIC)
    {
      // Volcanic provinces are gated by their own field more than climate distance.
      float v = climate->volcanic;
      s *= 0.15f + 4.0f * v * v;
    }
    else if (i == UNIVERSE_BIOME_ALPINE && climate->elevation > 0.65f)
      s *= 1.0f + 3.0f * (climate->elevation - 0.65f);
    scores[i] = s;
    sum += s;
  }

  if (sum <= 1e-8f)
  {
    out.primary = UNIVERSE_BIOME_TEMPERATE;
    out.weights[UNIVERSE_BIOME_TEMPERATE] = 1.0f;
    return out;
  }

  float best = -1.0f;
  out.primary = UNIVERSE_BIOME_TEMPERATE;
  for (int i = 0; i < UNIVERSE_BIOME_COUNT; i++)
  {
    out.weights[i] = scores[i] / sum;
    if (out.weights[i] > best)
    {
      best = out.weights[i];
      out.primary = (UniverseBiomeId)i;
    }
  }
  return out;
}

UniverseBiomeSample universe_biome_at(const World *world, uint32_t x, uint32_t y,
                                      float elevation_override)
{
  UniverseClimate c = universe_climate_sample(world, x, y, elevation_override);
  return universe_biome_classify(&c);
}

UniverseBiomeId universe_biome_primary_at(const World *world, uint32_t x, uint32_t y)
{
  return universe_biome_at(world, x, y, -1.0f).primary;
}

float universe_biome_rain_intensity(const UniverseBiomeSample *sample)
{
  if (!sample)
    return 0.0f;
  float wet = sample->weights[UNIVERSE_BIOME_WETLAND] * 1.0f +
              sample->weights[UNIVERSE_BIOME_TEMPERATE] * 0.55f +
              sample->weights[UNIVERSE_BIOME_TROPICAL] * 0.85f +
              sample->weights[UNIVERSE_BIOME_BOREAL] * 0.35f +
              sample->weights[UNIVERSE_BIOME_GRASSLAND] * 0.20f +
              sample->weights[UNIVERSE_BIOME_ALPINE] * 0.25f;
  float dry = sample->weights[UNIVERSE_BIOME_DESERT] +
              sample->weights[UNIVERSE_BIOME_VOLCANIC] * 0.7f;
  return clampf(wet * (1.0f - dry), 0.0f, 1.0f);
}

const char *universe_biome_name(UniverseBiomeId id)
{
  static const char *names[UNIVERSE_BIOME_COUNT] = {
      "temperate_forest", "grassland", "boreal", "desert",
      "wetland",          "alpine",    "tropical", "volcanic"};
  if (id < 0 || id >= UNIVERSE_BIOME_COUNT)
    return "unknown";
  return names[id];
}

void universe_biome_apply_ambient_weather(World *world)
{
  if (!world)
    return;

  // Console / quest-driven meteor storms own the sky; do not fight them with climate rain.
  if (particle_effects_meteor_storm_enabled(world) ||
      particle_effects_meteor_storm_universe_enabled())
    return;

  // Sticky thunderstorm (console / universe) keeps heavy rain while the storm is up.
  if (particle_effects_thunderstorm_enabled(world) ||
      particle_effects_thunderstorm_universe_enabled())
  {
    particle_effects_set_heavy_rain_enabled(world, true);
    particle_effects_set_rain_enabled(world, false);
    return;
  }

  if (world->generation_type != WORLD_TYPE_WILDERNESS)
  {
    // Home/arena/etc. keep their own ambience; do not force rain off (console may own it).
    return;
  }

  uint32_t cx = world->width / 2;
  uint32_t cy = world->height / 2;
  UniverseBiomeSample sample = universe_biome_at(world, cx, cy, -1.0f);
  float rain = universe_biome_rain_intensity(&sample);

  // Thresholds: light rain common in wet biomes; heavy only in wetland/tropical peaks.
  bool want_heavy = rain >= 0.72f;
  bool want_light = rain >= 0.32f;
  particle_effects_set_heavy_rain_enabled(world, want_heavy);
  particle_effects_set_rain_enabled(world, want_light && !want_heavy);
}
