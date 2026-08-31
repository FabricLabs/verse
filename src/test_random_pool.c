/*
 * test_random_pool.c - Test program for the random pool system
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include "random_pool.h"
#include "world_random_pool.h"

// Test basic pool functionality
void test_basic_pool(void) {
    printf("\n=== Testing Basic Pool Functionality ===\n");

    // Get some random bytes
    uint8_t buffer[32];
    random_pool_error_t err = random_pool_get_bytes(buffer, sizeof(buffer));
    assert(err == RANDOM_POOL_SUCCESS);

    printf("Got 32 random bytes: ");
    for (int i = 0; i < 8; i++) {
        printf("%02x", buffer[i]);
    }
    printf("...\n");

    // Get random integers
    uint32_t u32;
    err = random_pool_get_uint32(&u32);
    assert(err == RANDOM_POOL_SUCCESS);
    printf("Random uint32: %u\n", u32);

    uint64_t u64;
    err = random_pool_get_uint64(&u64);
    assert(err == RANDOM_POOL_SUCCESS);
    printf("Random uint64: %llu\n", u64);

    // Get random floats
    float f;
    err = random_pool_get_float(&f);
    assert(err == RANDOM_POOL_SUCCESS);
    assert(f >= 0.0f && f < 1.0f);
    printf("Random float: %f\n", f);

    double d;
    err = random_pool_get_double(&d);
    assert(err == RANDOM_POOL_SUCCESS);
    assert(d >= 0.0 && d < 1.0);
    printf("Random double: %f\n", d);

    printf("✓ Basic pool tests passed\n");
}

// Test range generation
void test_range_generation(void) {
    printf("\n=== Testing Range Generation ===\n");

    // Test various ranges
    uint32_t ranges[] = {10, 100, 1000, 65536, 1000000};

    for (int i = 0; i < sizeof(ranges)/sizeof(ranges[0]); i++) {
        uint32_t max = ranges[i];
        uint32_t min_val = max, max_val = 0;
        double sum = 0;
        int samples = 10000;

                for (int j = 0; j < samples; j++) {
            uint32_t value;
            random_pool_error_t err = random_pool_get_range(max, &value);
            if (err == RANDOM_POOL_ERROR_INSUFFICIENT_ENTROPY) {
                // Wait for refill and retry
                usleep(1000); // 1ms
                j--; // Retry this iteration
                continue;
            }
            assert(err == RANDOM_POOL_SUCCESS);
            assert(value < max);

            if (value < min_val) min_val = value;
            if (value > max_val) max_val = value;
            sum += value;
        }

        double avg = sum / samples;
        double expected = (max - 1) / 2.0;
        double deviation = (avg - expected) / expected * 100.0;

        printf("Range [0, %u): min=%u, max=%u, avg=%.2f (expected %.2f, deviation %.2f%%)\n",
               max, min_val, max_val, avg, expected, deviation);

        // Check reasonable distribution (within 5% of expected average)
        assert(deviation > -5.0 && deviation < 5.0);
    }

    printf("✓ Range generation tests passed\n");
}

// Test pool refilling
void test_pool_refilling(void) {
    printf("\n=== Testing Pool Refilling ===\n");

    random_pool_stats_t stats_before, stats_after;
    random_pool_get_stats(&stats_before);

    printf("Initial stats: available=%u/%u, refills=%llu\n",
           stats_before.current_available, stats_before.pool_size,
           stats_before.refill_count);

    // Consume a lot of random data to trigger refill
    size_t total_consumed = 0;
    uint8_t buffer[1024];

    while (total_consumed < RANDOM_POOL_SIZE * 2) {
        random_pool_error_t err = random_pool_get_bytes(buffer, sizeof(buffer));
        if (err == RANDOM_POOL_SUCCESS) {
            total_consumed += sizeof(buffer);
        } else {
            // Pool temporarily exhausted, wait a bit
            usleep(10000); // 10ms
        }
    }

    // Give refill thread time to work
    sleep(1);

    random_pool_get_stats(&stats_after);

    printf("After consuming %zu bytes:\n", total_consumed);
    printf("  Available: %u/%u\n", stats_after.current_available, stats_after.pool_size);
    printf("  Refills: %llu (was %llu)\n", stats_after.refill_count, stats_before.refill_count);
    printf("  Total generated: %llu bytes\n", stats_after.bytes_generated);
    printf("  Refill time: %llu us\n", stats_after.refill_time_us);

    // Should have triggered at least one refill
    assert(stats_after.refill_count > stats_before.refill_count);

    printf("✓ Pool refilling tests passed\n");
}

// Test deterministic mode
void test_deterministic_mode(void) {
    printf("\n=== Testing Deterministic Mode ===\n");

    const char* test_seed = "test_deterministic_12345";

    // Seed the pool
    random_pool_error_t err = random_pool_seed_deterministic(test_seed);
    assert(err == RANDOM_POOL_SUCCESS);

    // Get some values
    uint32_t values1[10];
    for (int i = 0; i < 10; i++) {
        err = random_pool_get_uint32(&values1[i]);
        assert(err == RANDOM_POOL_SUCCESS);
    }

    // Re-seed with same seed
    err = random_pool_seed_deterministic(test_seed);
    assert(err == RANDOM_POOL_SUCCESS);

    // Should get same values
    uint32_t values2[10];
    for (int i = 0; i < 10; i++) {
        err = random_pool_get_uint32(&values2[i]);
        assert(err == RANDOM_POOL_SUCCESS);
        assert(values1[i] == values2[i]);
    }

    printf("Deterministic generation verified: ");
    for (int i = 0; i < 5; i++) {
        printf("%u ", values1[i]);
    }
    printf("...\n");

    // Reset to non-deterministic
    err = random_pool_force_refill();
    assert(err == RANDOM_POOL_SUCCESS);

    printf("✓ Deterministic mode tests passed\n");
}

// Test world integration
void test_world_integration(void) {
    printf("\n=== Testing World Integration ===\n");

    // Initialize integration
    int result = world_random_pool_init();
    assert(result == 0);

    // Test pooled functions
    printf("Testing pooled random functions:\n");

    for (int i = 0; i < 10; i++) {
        uint32_t val = world_pool_rand();
        printf("  world_pool_rand(): %u\n", val);
    }

    printf("\nRange tests:\n");
    for (int i = 0; i < 5; i++) {
        uint32_t val = world_pool_rand_range(100);
        assert(val < 100);
        printf("  world_pool_rand_range(100): %u\n", val);
    }

    printf("\nFloat tests:\n");
    for (int i = 0; i < 5; i++) {
        float f = world_pool_randf();
        assert(f >= 0.0f && f < 1.0f);
        printf("  world_pool_randf(): %f\n", f);
    }

    // Test deterministic mode
    printf("\nTesting deterministic world generation:\n");
    world_pool_seed("test_world_seed");
    assert(world_pool_is_deterministic());

    uint32_t det_values[5];
    for (int i = 0; i < 5; i++) {
        det_values[i] = world_pool_rand_range(1000);
    }

    // Re-seed and verify same values
    world_pool_seed("test_world_seed");
    for (int i = 0; i < 5; i++) {
        uint32_t val = world_pool_rand_range(1000);
        assert(val == det_values[i]);
    }

    printf("Deterministic world generation verified\n");

    // Get stats
    world_random_pool_stats_t wrp_stats;
    world_random_pool_get_stats(&wrp_stats);

    printf("\nWorld integration stats:\n");
    printf("  Pool hits: %llu\n", wrp_stats.pool_hits);
    printf("  Pool misses: %llu\n", wrp_stats.pool_misses);
    printf("  Fallback calls: %llu\n", wrp_stats.fallback_calls);
    printf("  Avg pool latency: %.2f us\n", wrp_stats.average_pool_latency_us);
    printf("  Avg fallback latency: %.2f us\n", wrp_stats.average_fallback_latency_us);

    world_random_pool_shutdown();

    printf("✓ World integration tests passed\n");
}

// Performance benchmark
void benchmark_performance(void) {
    printf("\n=== Performance Benchmark ===\n");

    const int iterations = 1000000;
    clock_t start, end;
    double cpu_time_used;

    // Benchmark standard rand()
    start = clock();
    uint32_t sum1 = 0;
    for (int i = 0; i < iterations; i++) {
        sum1 += (uint32_t)rand();
    }
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    double rand_per_sec = iterations / cpu_time_used;
    printf("Standard rand(): %.2f million/sec\n", rand_per_sec / 1000000.0);

    // Benchmark pool
    start = clock();
    uint32_t sum2 = 0;
    for (int i = 0; i < iterations; i++) {
        uint32_t val;
        if (random_pool_get_uint32(&val) == RANDOM_POOL_SUCCESS) {
            sum2 += val;
        }
    }
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    double pool_per_sec = iterations / cpu_time_used;
    printf("Random pool: %.2f million/sec\n", pool_per_sec / 1000000.0);

    printf("Pool speedup: %.2fx\n", pool_per_sec / rand_per_sec);

    // Prevent optimization
    printf("(Sums: %u, %u)\n", sum1, sum2);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== Random Pool Test Suite ===\n");

    // Initialize pool
    random_pool_error_t err = random_pool_init();
    if (err != RANDOM_POOL_SUCCESS) {
        fprintf(stderr, "Failed to initialize random pool: %d\n", err);
        return 1;
    }

    // Run tests
    test_basic_pool();
    test_range_generation();
    test_pool_refilling();
    test_deterministic_mode();
    test_world_integration();
    benchmark_performance();

    // Get final stats
    random_pool_stats_t stats;
    random_pool_get_stats(&stats);

    printf("\n=== Final Pool Statistics ===\n");
    printf("Total bytes generated: %llu\n", stats.bytes_generated);
    printf("Total bytes consumed: %llu\n", stats.bytes_consumed);
    printf("Total refills: %llu\n", stats.refill_count);
    printf("Average refill time: %llu us\n", stats.refill_time_us);

    // Shutdown
    random_pool_shutdown();

    printf("\n✅ All tests passed!\n");

    return 0;
}
