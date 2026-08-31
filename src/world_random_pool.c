/*
 * world_random_pool.c - Integration implementation
 */

#include "world_random_pool.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

static struct {
    bool initialized;
    bool deterministic_mode;
    char current_seed[256];

    // Statistics
    uint64_t pool_hits;
    uint64_t pool_misses;
    uint64_t fallback_calls;
    uint64_t total_latency_pool_us;
    uint64_t total_latency_fallback_us;
} g_wrp_state = {0};

// Time measurement helper
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Initialize integration
int world_random_pool_init(void) {
    if (g_wrp_state.initialized) {
        return 0;
    }

    memset(&g_wrp_state, 0, sizeof(g_wrp_state));
    g_wrp_state.initialized = true;

    printf("[WORLD_RANDOM_POOL] Integration initialized\n");
    return 0;
}

// Shutdown integration
void world_random_pool_shutdown(void) {
    if (!g_wrp_state.initialized) {
        return;
    }

    printf("[WORLD_RANDOM_POOL] Stats: %llu pool hits, %llu misses, %llu fallbacks\n",
           g_wrp_state.pool_hits, g_wrp_state.pool_misses, g_wrp_state.fallback_calls);

    g_wrp_state.initialized = false;
}

// Get random uint32_t from pool
uint32_t world_pool_rand(void) {
    if (!g_wrp_state.initialized) {
        return (uint32_t)rand();
    }

    uint64_t start_time = get_time_ns();
    uint32_t value;

    random_pool_error_t err = random_pool_get_uint32(&value);
    if (err == RANDOM_POOL_SUCCESS) {
        g_wrp_state.pool_hits++;
        g_wrp_state.total_latency_pool_us += (get_time_ns() - start_time) / 1000;
        return value;
    }

    // Fallback to standard rand()
    g_wrp_state.pool_misses++;
    g_wrp_state.fallback_calls++;
    value = (uint32_t)rand();
    g_wrp_state.total_latency_fallback_us += (get_time_ns() - start_time) / 1000;

    return value;
}

// Get random number in range
uint32_t world_pool_rand_range(uint32_t range) {
    if (range == 0) {
        return 0;
    }

    if (!g_wrp_state.initialized) {
        return (uint32_t)(rand() % range);
    }

    uint64_t start_time = get_time_ns();
    uint32_t value;

    random_pool_error_t err = random_pool_get_range(range, &value);
    if (err == RANDOM_POOL_SUCCESS) {
        g_wrp_state.pool_hits++;
        g_wrp_state.total_latency_pool_us += (get_time_ns() - start_time) / 1000;
        return value;
    }

    // Fallback
    g_wrp_state.pool_misses++;
    g_wrp_state.fallback_calls++;
    value = (uint32_t)(rand() % range);
    g_wrp_state.total_latency_fallback_us += (get_time_ns() - start_time) / 1000;

    return value;
}

// Get random float [0, 1)
float world_pool_randf(void) {
    if (!g_wrp_state.initialized) {
        return (float)rand() / (float)RAND_MAX;
    }

    uint64_t start_time = get_time_ns();
    float value;

    random_pool_error_t err = random_pool_get_float(&value);
    if (err == RANDOM_POOL_SUCCESS) {
        g_wrp_state.pool_hits++;
        g_wrp_state.total_latency_pool_us += (get_time_ns() - start_time) / 1000;
        return value;
    }

    // Fallback
    g_wrp_state.pool_misses++;
    g_wrp_state.fallback_calls++;
    value = (float)rand() / (float)RAND_MAX;
    g_wrp_state.total_latency_fallback_us += (get_time_ns() - start_time) / 1000;

    return value;
}

// Get random double [0, 1)
double world_pool_randd(void) {
    if (!g_wrp_state.initialized) {
        return (double)rand() / (double)RAND_MAX;
    }

    uint64_t start_time = get_time_ns();
    double value;

    random_pool_error_t err = random_pool_get_double(&value);
    if (err == RANDOM_POOL_SUCCESS) {
        g_wrp_state.pool_hits++;
        g_wrp_state.total_latency_pool_us += (get_time_ns() - start_time) / 1000;
        return value;
    }

    // Fallback
    g_wrp_state.pool_misses++;
    g_wrp_state.fallback_calls++;
    value = (double)rand() / (double)RAND_MAX;
    g_wrp_state.total_latency_fallback_us += (get_time_ns() - start_time) / 1000;

    return value;
}

// Seed for deterministic generation
void world_pool_seed(const char* seed) {
    if (!g_wrp_state.initialized || !seed) {
        return;
    }

    strncpy(g_wrp_state.current_seed, seed, sizeof(g_wrp_state.current_seed) - 1);
    g_wrp_state.current_seed[sizeof(g_wrp_state.current_seed) - 1] = '\0';
    g_wrp_state.deterministic_mode = true;

    // Seed the pool deterministically
    random_pool_seed_deterministic(seed);

    // Also seed standard rand() for fallback
    // seed_rand_with_world_seed(seed);
    // Simple fallback seeding
    unsigned int simple_seed = 0;
    for (const char* p = seed; *p; p++) {
        simple_seed = simple_seed * 31 + (unsigned int)*p;
    }
    srand(simple_seed);

    printf("[WORLD_RANDOM_POOL] Switched to deterministic mode with seed: %s\n", seed);
}

// Check deterministic mode
bool world_pool_is_deterministic(void) {
    return g_wrp_state.deterministic_mode;
}

// Reset to non-deterministic
void world_pool_reset_deterministic(void) {
    if (!g_wrp_state.initialized) {
        return;
    }

    g_wrp_state.deterministic_mode = false;
    g_wrp_state.current_seed[0] = '\0';

    // Force pool refill with system entropy
    random_pool_force_refill();

    printf("[WORLD_RANDOM_POOL] Reset to non-deterministic mode\n");
}

// Drop-in replacement for seeded_rand_range
uint32_t seeded_rand_range_pooled(uint32_t range) {
    return world_pool_rand_range(range);
}

// Get statistics
void world_random_pool_get_stats(world_random_pool_stats_t* stats) {
    if (!stats || !g_wrp_state.initialized) {
        return;
    }

    stats->pool_hits = g_wrp_state.pool_hits;
    stats->pool_misses = g_wrp_state.pool_misses;
    stats->fallback_calls = g_wrp_state.fallback_calls;

    if (g_wrp_state.pool_hits > 0) {
        stats->average_pool_latency_us =
            (double)g_wrp_state.total_latency_pool_us / (double)g_wrp_state.pool_hits;
    } else {
        stats->average_pool_latency_us = 0.0;
    }

    if (g_wrp_state.fallback_calls > 0) {
        stats->average_fallback_latency_us =
            (double)g_wrp_state.total_latency_fallback_us / (double)g_wrp_state.fallback_calls;
    } else {
        stats->average_fallback_latency_us = 0.0;
    }
}
