/*
 * random_pool.h - Pre-generated random number pool with background refill
 *
 * Provides a 64KB pool of pre-generated random numbers with automatic
 * background refilling using a work queue.
 */

#ifndef RANDOM_POOL_H
#define RANDOM_POOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pool configuration
#define RANDOM_POOL_SIZE (64 * 1024)  // 64KB of random data
#define RANDOM_POOL_LOW_WATERMARK (RANDOM_POOL_SIZE / 4)  // Refill when 25% remains
#define RANDOM_POOL_REFILL_CHUNK (16 * 1024)  // Refill in 16KB chunks

// Error codes
typedef enum {
    RANDOM_POOL_SUCCESS = 0,
    RANDOM_POOL_ERROR_INIT_FAILED = -1,
    RANDOM_POOL_ERROR_ALREADY_INITIALIZED = -2,
    RANDOM_POOL_ERROR_NOT_INITIALIZED = -3,
    RANDOM_POOL_ERROR_ALLOCATION_FAILED = -4,
    RANDOM_POOL_ERROR_THREAD_CREATION_FAILED = -5,
    RANDOM_POOL_ERROR_MUTEX_FAILED = -6,
    RANDOM_POOL_ERROR_ENTROPY_SOURCE_FAILED = -7,
    RANDOM_POOL_ERROR_INVALID_PARAMETER = -8,
    RANDOM_POOL_ERROR_INSUFFICIENT_ENTROPY = -9,
} random_pool_error_t;

// Statistics structure
typedef struct {
    uint64_t bytes_consumed;
    uint64_t bytes_generated;
    uint64_t refill_count;
    uint64_t refill_time_us;
    uint32_t current_available;
    uint32_t pool_size;
    bool is_refilling;
} random_pool_stats_t;

// Initialize the random pool and background worker
// This must be called before any other random_pool functions
random_pool_error_t random_pool_init(void);

// Shutdown the random pool and cleanup resources
void random_pool_shutdown(void);

// Get random bytes from the pool
// Returns RANDOM_POOL_SUCCESS on success, error code otherwise
random_pool_error_t random_pool_get_bytes(uint8_t* buffer, size_t length);

// Get a random 32-bit integer from the pool
random_pool_error_t random_pool_get_uint32(uint32_t* value);

// Get a random 64-bit integer from the pool
random_pool_error_t random_pool_get_uint64(uint64_t* value);

// Get a random float in [0, 1) from the pool
random_pool_error_t random_pool_get_float(float* value);

// Get a random double in [0, 1) from the pool
random_pool_error_t random_pool_get_double(double* value);

// Get a random integer in range [0, max) using rejection sampling
random_pool_error_t random_pool_get_range(uint32_t max, uint32_t* value);

// Get current pool statistics
random_pool_error_t random_pool_get_stats(random_pool_stats_t* stats);

// Force an immediate refill (blocking)
// Useful for initialization or after detecting low entropy
random_pool_error_t random_pool_force_refill(void);

// Check if pool needs refilling (non-blocking)
bool random_pool_needs_refill(void);

// Set custom entropy source (optional)
// If not set, will use system entropy sources
typedef int (*random_pool_entropy_func)(uint8_t* buffer, size_t length);
void random_pool_set_entropy_source(random_pool_entropy_func func);

// For integration with existing seeded random functions
// Seeds the pool with deterministic data for reproducible generation
random_pool_error_t random_pool_seed_deterministic(const char* seed);

#ifdef __cplusplus
}
#endif

#endif // RANDOM_POOL_H
