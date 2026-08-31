#include "world_gen_profile.h"

#include <stdio.h>
#include <string.h>

static const char *g_phase_names[WGEN_PHASE_COUNT] = {
    "world_create (alloc+zero)",
    "clear to air",
    "bedrock floor",
    "magma (plane+halo+basalt)",
    "strata (total)",
    "  terrain noise (9-phase)",
    "  occupancy sample",
    "  stone placement",
    "  gravel",
    "  water",
    "  ore",
    "  crystal",
};

const char *world_gen_phase_name(WorldGenPhase phase)
{
  if (phase < 0 || phase >= WGEN_PHASE_COUNT)
    return "?";
  return g_phase_names[phase];
}

#ifdef WORLD_GEN_PROFILE

#include <time.h>

static uint64_t g_elapsed_ns[WGEN_PHASE_COUNT];
static uint64_t g_calls[WGEN_PHASE_COUNT];

uint64_t world_gen_profile_now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void world_gen_profile_record(WorldGenPhase phase, uint64_t elapsed_ns)
{
  if (phase < 0 || phase >= WGEN_PHASE_COUNT)
    return;
  g_elapsed_ns[phase] += elapsed_ns;
  g_calls[phase]++;
}

void world_gen_profile_tick(WorldGenPhase phase)
{
  if (phase < 0 || phase >= WGEN_PHASE_COUNT)
    return;
  g_calls[phase]++;
}

void world_gen_profile_reset(void)
{
  memset(g_elapsed_ns, 0, sizeof(g_elapsed_ns));
  memset(g_calls, 0, sizeof(g_calls));
}

double world_gen_profile_ms(WorldGenPhase phase)
{
  if (phase < 0 || phase >= WGEN_PHASE_COUNT)
    return 0.0;
  return (double)g_elapsed_ns[phase] / 1000000.0;
}

uint64_t world_gen_profile_calls(WorldGenPhase phase)
{
  if (phase < 0 || phase >= WGEN_PHASE_COUNT)
    return 0;
  return g_calls[phase];
}

void world_gen_profile_report(const char *label, size_t voxel_count)
{
  // Strata is the parent of the six sub-phases; the top-level stages are siblings.
  // Report against strata so the sub-phase shares are meaningful, and show the
  // unattributed remainder rather than pretending the parts sum to the whole.
  double strata = world_gen_profile_ms(WGEN_PHASE_STRATA);
  double top =
      world_gen_profile_ms(WGEN_PHASE_CREATE) + world_gen_profile_ms(WGEN_PHASE_CLEAR) +
      world_gen_profile_ms(WGEN_PHASE_BEDROCK) + world_gen_profile_ms(WGEN_PHASE_MAGMA) + strata;

  printf("\n=== world generation profile: %s ===\n", label ? label : "");
  printf("%-28s %10s %14s %12s\n", "phase", "ms", "calls", "ns/call");
  printf("%-28s %10s %14s %12s\n", "----------------------------", "----------",
         "--------------", "------------");

  for (int p = 0; p < WGEN_PHASE_COUNT; p++)
  {
    double ms = world_gen_profile_ms((WorldGenPhase)p);
    uint64_t calls = world_gen_profile_calls((WorldGenPhase)p);
    if (ms == 0.0 && calls == 0)
      continue;
    double ns_per_call = calls ? (ms * 1000000.0) / (double)calls : 0.0;
    printf("%-28s %10.1f %14llu %12.1f\n", world_gen_phase_name((WorldGenPhase)p), ms,
           (unsigned long long)calls, ns_per_call);
  }

  double sub = world_gen_profile_ms(WGEN_PHASE_TERRAIN_NOISE) +
               world_gen_profile_ms(WGEN_PHASE_OCCUPANCY) +
               world_gen_profile_ms(WGEN_PHASE_STONE) +
               world_gen_profile_ms(WGEN_PHASE_GRAVEL) +
               world_gen_profile_ms(WGEN_PHASE_WATER) +
               world_gen_profile_ms(WGEN_PHASE_ORE) +
               world_gen_profile_ms(WGEN_PHASE_CRYSTAL);

  printf("\ntop-level total          %10.1f ms\n", top);
  if (strata > 0.0)
  {
    double unattributed = strata - sub;
    printf("strata sub-phases sum    %10.1f ms (%.0f%% of strata)\n", sub, sub / strata * 100.0);
    // The sub-phase timers each carry two clock reads, so their sum can land marginally
    // above the parent's single measurement. Treat anything under a tenth of a percent as
    // noise rather than reporting a negative remainder.
    if (unattributed < -strata * 0.001)
      printf("strata unattributed      %10.1f ms (timers disagree; treat as suspect)\n", unattributed);
    else if (unattributed <= 0.0)
      printf("strata unattributed           <noise> (sub-phase timers account for all of it)\n");
    else
      printf("strata unattributed      %10.1f ms (loop overhead + clock reads)\n", unattributed);
  }
  if (voxel_count)
    printf("per voxel                %10.1f ns  (%zu voxels)\n",
           top * 1000000.0 / (double)voxel_count, voxel_count);
  printf("=========================================================\n");
}

#endif // WORLD_GEN_PROFILE
