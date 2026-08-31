/*
 * random_pool.c - Pre-generated random number pool implementation
 */

#include "random_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>

#ifdef __APPLE__
#include <Security/Security.h>
#endif

#ifdef __linux__
#include <sys/random.h>
#endif

// Pool state structure
typedef struct {
    uint8_t* buffer;
    size_t size;
    size_t read_pos;
    size_t write_pos;
    size_t available;

    pthread_mutex_t mutex;
    pthread_cond_t refill_cond;
    pthread_t refill_thread;

    bool initialized;
    bool shutdown_requested;
    bool is_refilling;

    // Statistics
    uint64_t bytes_consumed;
    uint64_t bytes_generated;
    uint64_t refill_count;
    uint64_t refill_time_us;

    // Custom entropy source (optional)
    random_pool_entropy_func entropy_func;
} random_pool_state_t;

static random_pool_state_t g_pool = {0};

// Forward declarations
static void* refill_thread_func(void* arg);
static int get_system_entropy(uint8_t* buffer, size_t length);
static uint64_t get_time_us(void);

// Initialize the random pool
random_pool_error_t random_pool_init(void) {
    if (g_pool.initialized) {
        return RANDOM_POOL_ERROR_ALREADY_INITIALIZED;
    }

    // Allocate pool buffer
    g_pool.buffer = calloc(1, RANDOM_POOL_SIZE);
    if (!g_pool.buffer) {
        return RANDOM_POOL_ERROR_ALLOCATION_FAILED;
    }

    g_pool.size = RANDOM_POOL_SIZE;
    g_pool.read_pos = 0;
    g_pool.write_pos = 0;
    g_pool.available = 0;

    // Initialize synchronization
    if (pthread_mutex_init(&g_pool.mutex, NULL) != 0) {
        free(g_pool.buffer);
        g_pool.buffer = NULL;
        return RANDOM_POOL_ERROR_MUTEX_FAILED;
    }

    if (pthread_cond_init(&g_pool.refill_cond, NULL) != 0) {
        pthread_mutex_destroy(&g_pool.mutex);
        free(g_pool.buffer);
        g_pool.buffer = NULL;
        return RANDOM_POOL_ERROR_MUTEX_FAILED;
    }

    g_pool.initialized = true;
    g_pool.shutdown_requested = false;

    // Initial fill
    uint64_t start_time = get_time_us();
    int result = get_system_entropy(g_pool.buffer, g_pool.size);
    if (result != 0) {
        pthread_cond_destroy(&g_pool.refill_cond);
        pthread_mutex_destroy(&g_pool.mutex);
        free(g_pool.buffer);
        g_pool.buffer = NULL;
        g_pool.initialized = false;
        return RANDOM_POOL_ERROR_ENTROPY_SOURCE_FAILED;
    }
    g_pool.available = g_pool.size;
    g_pool.bytes_generated = g_pool.size;
    g_pool.refill_time_us = get_time_us() - start_time;

    // Start refill thread
    if (pthread_create(&g_pool.refill_thread, NULL, refill_thread_func, NULL) != 0) {
        pthread_cond_destroy(&g_pool.refill_cond);
        pthread_mutex_destroy(&g_pool.mutex);
        free(g_pool.buffer);
        g_pool.buffer = NULL;
        g_pool.initialized = false;
        return RANDOM_POOL_ERROR_THREAD_CREATION_FAILED;
    }

    printf("[RANDOM_POOL] Initialized with %zu bytes, initial fill took %llu us\n",
           g_pool.size, g_pool.refill_time_us);

    return RANDOM_POOL_SUCCESS;
}

// Shutdown the random pool
void random_pool_shutdown(void) {
    if (!g_pool.initialized) {
        return;
    }

    // Signal shutdown
    pthread_mutex_lock(&g_pool.mutex);
    g_pool.shutdown_requested = true;
    pthread_cond_signal(&g_pool.refill_cond);
    pthread_mutex_unlock(&g_pool.mutex);

    // Wait for thread to exit
    pthread_join(g_pool.refill_thread, NULL);

    // Cleanup
    pthread_cond_destroy(&g_pool.refill_cond);
    pthread_mutex_destroy(&g_pool.mutex);

    if (g_pool.buffer) {
        // Clear buffer before freeing for security
        memset(g_pool.buffer, 0, g_pool.size);
        free(g_pool.buffer);
        g_pool.buffer = NULL;
    }

    g_pool.initialized = false;

    printf("[RANDOM_POOL] Shutdown complete. Generated %llu bytes, consumed %llu bytes\n",
           g_pool.bytes_generated, g_pool.bytes_consumed);
}

// Get random bytes from the pool
random_pool_error_t random_pool_get_bytes(uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) {
        return RANDOM_POOL_ERROR_INVALID_PARAMETER;
    }

    if (!g_pool.initialized) {
        return RANDOM_POOL_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_pool.mutex);

    // Check if we have enough bytes
    if (g_pool.available < length) {
        pthread_mutex_unlock(&g_pool.mutex);
        return RANDOM_POOL_ERROR_INSUFFICIENT_ENTROPY;
    }

    // Copy bytes from pool
    size_t bytes_copied = 0;
    while (bytes_copied < length) {
        size_t chunk_size = length - bytes_copied;
        size_t until_wrap = g_pool.size - g_pool.read_pos;
        if (chunk_size > until_wrap) {
            chunk_size = until_wrap;
        }

        memcpy(buffer + bytes_copied, g_pool.buffer + g_pool.read_pos, chunk_size);
        g_pool.read_pos = (g_pool.read_pos + chunk_size) % g_pool.size;
        bytes_copied += chunk_size;
    }

    g_pool.available -= length;
    g_pool.bytes_consumed += length;

    // Check if we need to trigger refill
    if (g_pool.available < RANDOM_POOL_LOW_WATERMARK && !g_pool.is_refilling) {
        pthread_cond_signal(&g_pool.refill_cond);
    }

    pthread_mutex_unlock(&g_pool.mutex);

    return RANDOM_POOL_SUCCESS;
}

// Get a random 32-bit integer
random_pool_error_t random_pool_get_uint32(uint32_t* value) {
    return random_pool_get_bytes((uint8_t*)value, sizeof(uint32_t));
}

// Get a random 64-bit integer
random_pool_error_t random_pool_get_uint64(uint64_t* value) {
    return random_pool_get_bytes((uint8_t*)value, sizeof(uint64_t));
}

// Get a random float in [0, 1)
random_pool_error_t random_pool_get_float(float* value) {
    if (!value) {
        return RANDOM_POOL_ERROR_INVALID_PARAMETER;
    }

    uint32_t bits;
    random_pool_error_t err = random_pool_get_uint32(&bits);
    if (err != RANDOM_POOL_SUCCESS) {
        return err;
    }

    // Convert to float in [0, 1) using 24 bits of precision
    *value = (bits >> 8) * (1.0f / (1 << 24));
    return RANDOM_POOL_SUCCESS;
}

// Get a random double in [0, 1)
random_pool_error_t random_pool_get_double(double* value) {
    if (!value) {
        return RANDOM_POOL_ERROR_INVALID_PARAMETER;
    }

    uint64_t bits;
    random_pool_error_t err = random_pool_get_uint64(&bits);
    if (err != RANDOM_POOL_SUCCESS) {
        return err;
    }

    // Convert to double in [0, 1) using 53 bits of precision
    *value = (bits >> 11) * (1.0 / (1ULL << 53));
    return RANDOM_POOL_SUCCESS;
}

// Get a random integer in range [0, max) using rejection sampling
random_pool_error_t random_pool_get_range(uint32_t max, uint32_t* value) {
    if (!value || max == 0) {
        return RANDOM_POOL_ERROR_INVALID_PARAMETER;
    }

    if (max == 1) {
        *value = 0;
        return RANDOM_POOL_SUCCESS;
    }

    // Use rejection sampling to avoid modulo bias
    uint32_t threshold = (uint32_t)(-max) % max;
    uint32_t candidate;

    do {
        random_pool_error_t err = random_pool_get_uint32(&candidate);
        if (err != RANDOM_POOL_SUCCESS) {
            return err;
        }
    } while (candidate < threshold);

    *value = candidate % max;
    return RANDOM_POOL_SUCCESS;
}

// Get pool statistics
random_pool_error_t random_pool_get_stats(random_pool_stats_t* stats) {
    if (!stats) {
        return RANDOM_POOL_ERROR_INVALID_PARAMETER;
    }

    if (!g_pool.initialized) {
        return RANDOM_POOL_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_pool.mutex);

    stats->bytes_consumed = g_pool.bytes_consumed;
    stats->bytes_generated = g_pool.bytes_generated;
    stats->refill_count = g_pool.refill_count;
    stats->refill_time_us = g_pool.refill_time_us;
    stats->current_available = g_pool.available;
    stats->pool_size = g_pool.size;
    stats->is_refilling = g_pool.is_refilling;

    pthread_mutex_unlock(&g_pool.mutex);

    return RANDOM_POOL_SUCCESS;
}

// Force immediate refill
random_pool_error_t random_pool_force_refill(void) {
    if (!g_pool.initialized) {
        return RANDOM_POOL_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_pool.mutex);

    uint64_t start_time = get_time_us();
    g_pool.is_refilling = true;

    // Refill the empty portion of the buffer
    size_t to_fill = g_pool.size - g_pool.available;
    if (to_fill > 0) {
        uint8_t* temp_buffer = malloc(to_fill);
        if (!temp_buffer) {
            g_pool.is_refilling = false;
            pthread_mutex_unlock(&g_pool.mutex);
            return RANDOM_POOL_ERROR_ALLOCATION_FAILED;
        }

        pthread_mutex_unlock(&g_pool.mutex);

        // Get entropy without holding lock
        int result = get_system_entropy(temp_buffer, to_fill);

        pthread_mutex_lock(&g_pool.mutex);

        if (result == 0) {
            // Copy to pool buffer
            size_t bytes_copied = 0;
            while (bytes_copied < to_fill) {
                size_t chunk_size = to_fill - bytes_copied;
                size_t until_wrap = g_pool.size - g_pool.write_pos;
                if (chunk_size > until_wrap) {
                    chunk_size = until_wrap;
                }

                memcpy(g_pool.buffer + g_pool.write_pos, temp_buffer + bytes_copied, chunk_size);
                g_pool.write_pos = (g_pool.write_pos + chunk_size) % g_pool.size;
                bytes_copied += chunk_size;
            }

            g_pool.available += to_fill;
            g_pool.bytes_generated += to_fill;
            g_pool.refill_count++;
            g_pool.refill_time_us = get_time_us() - start_time;
        }

        // Clear and free temp buffer
        memset(temp_buffer, 0, to_fill);
        free(temp_buffer);
    }

    g_pool.is_refilling = false;
    pthread_mutex_unlock(&g_pool.mutex);

    return RANDOM_POOL_SUCCESS;
}

// Check if pool needs refilling
bool random_pool_needs_refill(void) {
    if (!g_pool.initialized) {
        return false;
    }

    pthread_mutex_lock(&g_pool.mutex);
    bool needs_refill = g_pool.available < RANDOM_POOL_LOW_WATERMARK;
    pthread_mutex_unlock(&g_pool.mutex);

    return needs_refill;
}

// Set custom entropy source
void random_pool_set_entropy_source(random_pool_entropy_func func) {
    pthread_mutex_lock(&g_pool.mutex);
    g_pool.entropy_func = func;
    pthread_mutex_unlock(&g_pool.mutex);
}

// Seed pool deterministically (for testing/reproduction)
random_pool_error_t random_pool_seed_deterministic(const char* seed) {
    if (!seed) {
        return RANDOM_POOL_ERROR_INVALID_PARAMETER;
    }

    if (!g_pool.initialized) {
        return RANDOM_POOL_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_pool.mutex);

    // Simple deterministic fill based on seed
    // This is NOT cryptographically secure - only for testing!
    uint64_t hash = 5381;
    for (const char* p = seed; *p; p++) {
        hash = ((hash << 5) + hash) + (uint64_t)*p;
    }

    // Fill buffer with pseudo-random data
    for (size_t i = 0; i < g_pool.size; i += 8) {
        hash = hash * 6364136223846793005ULL + 1442695040888963407ULL;
        size_t copy_size = (g_pool.size - i) < 8 ? (g_pool.size - i) : 8;
        memcpy(g_pool.buffer + i, &hash, copy_size);
    }

    g_pool.read_pos = 0;
    g_pool.write_pos = 0;
    g_pool.available = g_pool.size;

    pthread_mutex_unlock(&g_pool.mutex);

    printf("[RANDOM_POOL] Seeded deterministically with seed: %s\n", seed);

    return RANDOM_POOL_SUCCESS;
}

// Background refill thread
static void* refill_thread_func(void* arg) {
    (void)arg;

    printf("[RANDOM_POOL] Refill thread started\n");

    while (1) {
        pthread_mutex_lock(&g_pool.mutex);

        // Wait for refill signal or shutdown
        while (!g_pool.shutdown_requested &&
               g_pool.available >= RANDOM_POOL_LOW_WATERMARK) {
            pthread_cond_wait(&g_pool.refill_cond, &g_pool.mutex);
        }

        if (g_pool.shutdown_requested) {
            pthread_mutex_unlock(&g_pool.mutex);
            break;
        }

        // Calculate how much to refill
        size_t to_fill = g_pool.size - g_pool.available;
        if (to_fill > RANDOM_POOL_REFILL_CHUNK) {
            to_fill = RANDOM_POOL_REFILL_CHUNK;
        }

        g_pool.is_refilling = true;
        uint64_t start_time = get_time_us();

        pthread_mutex_unlock(&g_pool.mutex);

        // Allocate temporary buffer for refill
        uint8_t* temp_buffer = malloc(to_fill);
        if (temp_buffer) {
            // Get entropy without holding lock
            int result = get_system_entropy(temp_buffer, to_fill);

            pthread_mutex_lock(&g_pool.mutex);

            if (result == 0) {
                // Copy to pool buffer
                size_t bytes_copied = 0;
                while (bytes_copied < to_fill && !g_pool.shutdown_requested) {
                    size_t chunk_size = to_fill - bytes_copied;
                    size_t until_wrap = g_pool.size - g_pool.write_pos;
                    if (chunk_size > until_wrap) {
                        chunk_size = until_wrap;
                    }

                    memcpy(g_pool.buffer + g_pool.write_pos, temp_buffer + bytes_copied, chunk_size);
                    g_pool.write_pos = (g_pool.write_pos + chunk_size) % g_pool.size;
                    bytes_copied += chunk_size;
                }

                g_pool.available += bytes_copied;
                g_pool.bytes_generated += bytes_copied;
                g_pool.refill_count++;
                g_pool.refill_time_us = get_time_us() - start_time;

                printf("[RANDOM_POOL] Refilled %zu bytes in %llu us (total: %llu bytes)\n",
                       bytes_copied, g_pool.refill_time_us, g_pool.bytes_generated);
            }

            g_pool.is_refilling = false;
            pthread_mutex_unlock(&g_pool.mutex);

            // Clear and free temp buffer
            memset(temp_buffer, 0, to_fill);
            free(temp_buffer);
        } else {
            pthread_mutex_lock(&g_pool.mutex);
            g_pool.is_refilling = false;
            pthread_mutex_unlock(&g_pool.mutex);
        }
    }

    printf("[RANDOM_POOL] Refill thread exiting\n");
    return NULL;
}

// Get system entropy
static int get_system_entropy(uint8_t* buffer, size_t length) {
    // Use custom entropy source if available
    if (g_pool.entropy_func) {
        return g_pool.entropy_func(buffer, length);
    }

#ifdef __APPLE__
    // macOS: Use Security framework
    OSStatus status = SecRandomCopyBytes(kSecRandomDefault, length, buffer);
    if (status == errSecSuccess) {
        return 0;
    }
#endif

#ifdef __linux__
    // Linux: Try getrandom() first
    while (length > 0) {
        ssize_t result = getrandom(buffer, length, 0);
        if (result > 0) {
            buffer += result;
            length -= result;
        } else if (errno != EINTR) {
            break;
        }
    }
    if (length == 0) {
        return 0;
    }
#endif

    // Fallback: Read from /dev/urandom
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    size_t bytes_read = 0;
    while (bytes_read < length) {
        ssize_t result = read(fd, buffer + bytes_read, length - bytes_read);
        if (result > 0) {
            bytes_read += result;
        } else if (result < 0 && errno != EINTR) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}

// Get current time in microseconds
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}
