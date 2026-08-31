/*
 * world_noise.c - Noise generation functions for procedural content
 *
 * This module provides Perlin and Simplex noise implementations
 * used throughout the world generation system.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "world_noise.h"
#include "noise-c/src/crypto/sha2/sha256.h"

// Global universe noise seed (shared permutation)
static int g_universe_perm[512];
static int g_universe_perm_12[512];
static bool g_universe_noise_initialized = false;

// Universe-wide noise normalization
#define NOISE_SCALE_4D_ADJUSTED 0.83f

// Permutation tables for noise
static const int p[256] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,
    142,8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,
    203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,
    220,105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,
    132,187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,
    186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,
    59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,
    70,221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180
};

static double fade(double t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

static double lerp(double t, double a, double b) {
    return a + t * (b - a);
}

static double grad(int hash, double x, double y, double z) {
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static double grad4(int hash, double x, double y, double z, double w) {
    int h = hash & 31;
    double u = h < 24 ? x : y;
    double v = h < 16 ? y : z;
    double s = h < 8 ? z : w;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v) + ((h & 4) == 0 ? s : -s);
}

// Initialize noise with seed
void noise_init_seed(unsigned int seed) {
    // Use seed to shuffle permutation table
    srand(seed);
    int perm[256];
    memcpy(perm, p, sizeof(p));

    // Fisher-Yates shuffle
    for (int i = 255; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = perm[i];
        perm[i] = perm[j];
        perm[j] = temp;
    }

    // Update global permutation if universe seed set
    if (g_universe_noise_initialized) {
        for (int i = 0; i < 256; i++) {
            g_universe_perm[i] = perm[i];
            g_universe_perm[256 + i] = perm[i];
            g_universe_perm_12[i] = perm[i] % 12;
            g_universe_perm_12[256 + i] = perm[i] % 12;
        }
    }
}

// Set universe-wide noise seed
void world_set_universe_noise_seed(const char *base_seed) {
    // Hash the base seed to get deterministic values
    uint8_t hash[32];
    sha256_context_t ctx;
    sha256_reset(&ctx);
    sha256_update(&ctx, (const uint8_t*)base_seed, strlen(base_seed));
    sha256_finish(&ctx, hash);

    // Use hash to initialize permutation tables
    for (int i = 0; i < 256; i++) {
        g_universe_perm[i] = p[(hash[i % 32] + i) & 255];
        g_universe_perm[256 + i] = g_universe_perm[i];
        g_universe_perm_12[i] = g_universe_perm[i] % 12;
        g_universe_perm_12[256 + i] = g_universe_perm_12[i];
    }

    g_universe_noise_initialized = true;
}

bool world_has_universe_noise_seed(void) {
    return g_universe_noise_initialized;
}

// 4D Perlin noise
double perlin_noise(double w, double x, double y, double z) {
    if (!g_universe_noise_initialized) {
        world_set_universe_noise_seed("default");
    }

    int X = (int)floor(x) & 255;
    int Y = (int)floor(y) & 255;
    int Z = (int)floor(z) & 255;
    int W = (int)floor(w) & 255;

    x -= floor(x);
    y -= floor(y);
    z -= floor(z);
    w -= floor(w);

    double u = fade(x);
    double v = fade(y);
    double t = fade(z);
    double s = fade(w);

    int A = g_universe_perm[X] + Y;
    int AA = g_universe_perm[A] + Z;
    int AB = g_universe_perm[A + 1] + Z;
    int B = g_universe_perm[X + 1] + Y;
    int BA = g_universe_perm[B] + Z;
    int BB = g_universe_perm[B + 1] + Z;

    int AAA = g_universe_perm[AA] + W;
    int AAB = g_universe_perm[AA + 1] + W;
    int ABA = g_universe_perm[AB] + W;
    int ABB = g_universe_perm[AB + 1] + W;
    int BAA = g_universe_perm[BA] + W;
    int BAB = g_universe_perm[BA + 1] + W;
    int BBA = g_universe_perm[BB] + W;
    int BBB = g_universe_perm[BB + 1] + W;

    return lerp(s,
        lerp(t,
            lerp(v,
                lerp(u,
                    grad4(g_universe_perm[AAA], x, y, z, w),
                    grad4(g_universe_perm[BAA], x - 1, y, z, w)
                ),
                lerp(u,
                    grad4(g_universe_perm[ABA], x, y - 1, z, w),
                    grad4(g_universe_perm[BBA], x - 1, y - 1, z, w)
                )
            ),
            lerp(v,
                lerp(u,
                    grad4(g_universe_perm[AAB], x, y, z - 1, w),
                    grad4(g_universe_perm[BAB], x - 1, y, z - 1, w)
                ),
                lerp(u,
                    grad4(g_universe_perm[ABB], x, y - 1, z - 1, w),
                    grad4(g_universe_perm[BBB], x - 1, y - 1, z - 1, w)
                )
            )
        ),
        lerp(t,
            lerp(v,
                lerp(u,
                    grad4(g_universe_perm[AAA + 1], x, y, z, w - 1),
                    grad4(g_universe_perm[BAA + 1], x - 1, y, z, w - 1)
                ),
                lerp(u,
                    grad4(g_universe_perm[ABA + 1], x, y - 1, z, w - 1),
                    grad4(g_universe_perm[BBA + 1], x - 1, y - 1, z, w - 1)
                )
            ),
            lerp(v,
                lerp(u,
                    grad4(g_universe_perm[AAB + 1], x, y, z - 1, w - 1),
                    grad4(g_universe_perm[BAB + 1], x - 1, y, z - 1, w - 1)
                ),
                lerp(u,
                    grad4(g_universe_perm[ABB + 1], x, y - 1, z - 1, w - 1),
                    grad4(g_universe_perm[BBB + 1], x - 1, y - 1, z - 1, w - 1)
                )
            )
        )
    ) * NOISE_SCALE_4D_ADJUSTED;
}

// 3D Perlin noise
double perlin_noise_3d(double x, double y, double z) {
    return perlin_noise(0.0, x, y, z);
}

// 2D Perlin noise
double perlin_noise_2d(double x, double y) {
    return perlin_noise(0.0, x, y, 0.0);
}

// 4D Simplex noise
double simplex_noise(double w, double x, double y, double z) {
    // Simplex noise implementation
    // This is a placeholder - full implementation would be quite long
    // For now, use Perlin noise as fallback
    return perlin_noise(w, x, y, z);
}

// 3D Simplex noise
double simplex_noise_3d(double x, double y, double z) {
    return simplex_noise(0.0, x, y, z);
}

// 2D Simplex noise
double simplex_noise_2d(double x, double y) {
    return simplex_noise(0.0, x, y, 0.0);
}

// Fractal noise functions
double fractal_noise_3d(double x, double y, double z, int octaves, double persistence) {
    double total = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double maxValue = 0.0;

    for (int i = 0; i < octaves; i++) {
        total += perlin_noise_3d(x * frequency, y * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return total / maxValue;
}

double fractal_noise_2d(double x, double y, int octaves, double persistence) {
    double total = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double maxValue = 0.0;

    for (int i = 0; i < octaves; i++) {
        total += perlin_noise_2d(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return total / maxValue;
}

// Utility function to normalize noise
double noise_normalize(double value, double min, double max) {
    return (value - min) / (max - min);
}
