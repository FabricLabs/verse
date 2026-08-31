// tests/world_determinism.c
// Determinism test: generate 10 worlds from the same generator using a base seed
// and verify the serialized outputs are identical across two passes.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "../src/world.h"
#include "../src/constants.h"

// Simple 64-bit FNV-1a hash for strings
static uint64_t fnv1a64(const char* data) {
    const uint64_t FNV_OFFSET = 1469598103934665603ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;
    uint64_t hash = FNV_OFFSET;
    for (const unsigned char* p = (const unsigned char*)data; *p; ++p) {
        hash ^= (uint64_t)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}

static void make_seed(char* buf, size_t buf_size, const char* base, int index) {
    snprintf(buf, buf_size, "%s_%d", base, index);
}

int main(int argc, char** argv) {
    const char* base_seed = (argc > 1) ? argv[1] : "foo";
    const char* second_seed = (argc > 2) ? argv[2] : "bar";
    const int world_count = 10;

    uint64_t hashes_pass1[world_count];
    uint64_t hashes_pass2[world_count];
    uint64_t hashes_second[world_count];

    // Pass 1: generate 10 worlds and record hashes
    for (int i = 0; i < world_count; i++) {
        char seed[256];
        make_seed(seed, sizeof(seed), base_seed, i);

        World* w = world_create(WORLD_SIZE_CUBE);
        if (!w) {
            fprintf(stderr, "Failed to create world %d\n", i);
            return 2;
        }

        world_generate(w, seed);

        char* serialized = world_serialize(w);
        if (!serialized) {
            fprintf(stderr, "Failed to serialize world %d\n", i);
            world_destroy(w);
            return 3;
        }

        hashes_pass1[i] = fnv1a64(serialized);
        free(serialized);
        world_destroy(w);
    }

    // Pass 2: regenerate and compare
    for (int i = 0; i < world_count; i++) {
        char seed[256];
        make_seed(seed, sizeof(seed), base_seed, i);

        World* w = world_create(WORLD_SIZE_CUBE);
        if (!w) {
            fprintf(stderr, "Failed to create world %d (pass 2)\n", i);
            return 4;
        }

        world_generate(w, seed);

        char* serialized = world_serialize(w);
        if (!serialized) {
            fprintf(stderr, "Failed to serialize world %d (pass 2)\n", i);
            world_destroy(w);
            return 5;
        }

        hashes_pass2[i] = fnv1a64(serialized);
        free(serialized);
        world_destroy(w);
    }

    // Validate and print results for first seed
    int failures = 0;
    printf("Determinism check for base seed '%s' (size %dx%dx%d)\n",
           base_seed, WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z);
    for (int i = 0; i < world_count; i++) {
        if (hashes_pass1[i] != hashes_pass2[i]) {
            fprintf(stderr, "Mismatch at index %d: %016llx vs %016llx\n",
                    i, (unsigned long long)hashes_pass1[i], (unsigned long long)hashes_pass2[i]);
            failures++;
        }
        printf("%2d: %016llx\n", i, (unsigned long long)hashes_pass1[i]);
    }

    if (failures) {
        fprintf(stderr, "Determinism test FAILED (%d mismatches)\n", failures);
        return 1;
    }

    printf("Determinism test PASSED (10/10 worlds match)\n");

    // Second seed generation: ensure it does not match first seed outputs
    for (int i = 0; i < world_count; i++) {
        char seed[256];
        make_seed(seed, sizeof(seed), second_seed, i);

        World* w = world_create(WORLD_SIZE_CUBE);
        if (!w) {
            fprintf(stderr, "Failed to create world %d (second seed)\n", i);
            return 6;
        }

        world_generate(w, seed);

        char* serialized = world_serialize(w);
        if (!serialized) {
            fprintf(stderr, "Failed to serialize world %d (second seed)\n", i);
            world_destroy(w);
            return 7;
        }

        hashes_second[i] = fnv1a64(serialized);
        free(serialized);
        world_destroy(w);
    }

    // Compare first vs second seed outputs
    int equal_positions = 0;
    printf("\nCross-seed difference check: base '%s' vs second '%s'\n", base_seed, second_seed);
    for (int i = 0; i < world_count; i++) {
        printf("%2d: %016llx  vs  %016llx%s\n", i,
               (unsigned long long)hashes_pass1[i],
               (unsigned long long)hashes_second[i],
               (hashes_pass1[i] == hashes_second[i]) ? "  (EQUAL)" : "");
        if (hashes_pass1[i] == hashes_second[i]) equal_positions++;
    }

    if (equal_positions == world_count) {
        fprintf(stderr, "Cross-seed test FAILED: all %d worlds are identical across seeds\n", world_count);
        return 8;
    }

    printf("Cross-seed test PASSED: %d/%d positions differ (expected some differences)\n",
           world_count - equal_positions, world_count);
    return 0;
}


