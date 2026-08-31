/*
 * world_random_pool.h - Integration of random pool with world generation
 *
 * Provides drop-in replacements for existing random functions that use
 * the pre-generated pool for better performance.
 */

#ifndef WORLD_RANDOM_POOL_H
#define WORLD_RANDOM_POOL_H

#include <stdint.h>
#include "random_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize world random pool integration
// Call this once at startup after random_pool_init()
int world_random_pool_init(void);

// Shutdown world random pool integration
void world_random_pool_shutdown(void);

// Fast random number generation using the pool
// These functions fall back to traditional methods if pool is exhausted

// Get a random uint32_t
uint32_t world_pool_rand(void);

// Get a random number in range [0, range)
uint32_t world_pool_rand_range(uint32_t range);

// Get a random float in [0, 1)
float world_pool_randf(void);

// Get a random double in [0, 1)
double world_pool_randd(void);

// Seed-based random (for compatibility with seeded generation)
// This switches to deterministic mode for reproducible worlds
void world_pool_seed(const char* seed);

// Check if we're in deterministic mode
bool world_pool_is_deterministic(void);

// Reset to non-deterministic mode
void world_pool_reset_deterministic(void);

// Integration with existing seeded_rand_range function
// This can be used as a drop-in replacement
uint32_t seeded_rand_range_pooled(uint32_t range);

// Performance statistics
typedef struct {
    uint64_t pool_hits;
    uint64_t pool_misses;
    uint64_t fallback_calls;
    double average_pool_latency_us;
    double average_fallback_latency_us;
} world_random_pool_stats_t;

void world_random_pool_get_stats(world_random_pool_stats_t* stats);

#ifdef __cplusplus
}
#endif

#endif // WORLD_RANDOM_POOL_H
