#ifndef WORLD_GEN_PROFILE_H
#define WORLD_GEN_PROFILE_H

#include <stdint.h>
#include <stddef.h>

// Opt-in profiler for the world generation pipeline.
//
// Build with -DWORLD_GEN_PROFILE to attribute generation time per phase. Without that
// define, every macro below expands to nothing, so shipping builds carry no timing cost
// at all. That distinction is the point: the instrumentation this replaced called
// gettimeofday() six times per voxel unconditionally, which is ~12 million clock calls
// per 128^3 world, paid by every player on every new game.

typedef enum
{
  WGEN_PHASE_CREATE = 0,   // world_create: allocate and zero the voxel array
  WGEN_PHASE_CLEAR,        // world_clear_to_air
  WGEN_PHASE_BEDROCK,      // apply_bedrock_floor
  WGEN_PHASE_MAGMA,        // apply_unified_magma (plane + halo + basalt)
  WGEN_PHASE_STRATA,       // apply_wilderness_strata, whole call
  WGEN_PHASE_TERRAIN_NOISE,// sample_nine_phase_noise_field (9 simplex octaves)
  WGEN_PHASE_OCCUPANCY,    // sample_occupancy_variation (entropy field)
  WGEN_PHASE_STONE,        // geological layer selection and voxel placement
  WGEN_PHASE_GRAVEL,       // surface slope test and gravel noise
  WGEN_PHASE_WATER,        // sandstone water noise
  WGEN_PHASE_ORE,          // apply_ore_generation
  WGEN_PHASE_CRYSTAL,      // apply_crystal_generation
  WGEN_PHASE_COUNT
} WorldGenPhase;

const char *world_gen_phase_name(WorldGenPhase phase);

#ifdef WORLD_GEN_PROFILE

uint64_t world_gen_profile_now(void);
void world_gen_profile_record(WorldGenPhase phase, uint64_t elapsed_ns);
void world_gen_profile_tick(WorldGenPhase phase);
void world_gen_profile_reset(void);
void world_gen_profile_report(const char *label, size_t voxel_count);
double world_gen_profile_ms(WorldGenPhase phase);
uint64_t world_gen_profile_calls(WorldGenPhase phase);

// Time a region. BEGIN and END must appear in the same scope with the same phase.
#define WGEN_BEGIN(phase) uint64_t wgen_start_##phase = world_gen_profile_now()
#define WGEN_END(phase) world_gen_profile_record(phase, world_gen_profile_now() - wgen_start_##phase)
// Count an event without timing it.
#define WGEN_TICK(phase) world_gen_profile_tick(phase)

#else

#define WGEN_BEGIN(phase) ((void)0)
#define WGEN_END(phase) ((void)0)
#define WGEN_TICK(phase) ((void)0)

#endif // WORLD_GEN_PROFILE

#endif // WORLD_GEN_PROFILE_H
