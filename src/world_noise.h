#ifndef WORLD_NOISE_H
#define WORLD_NOISE_H

#include <stdbool.h>

// Noise generation functions for procedural content

// Perlin noise functions
double perlin_noise(double w, double x, double y, double z);
double perlin_noise_3d(double x, double y, double z);
double perlin_noise_2d(double x, double y);

// Simplex noise functions
double simplex_noise(double w, double x, double y, double z);
double simplex_noise_3d(double x, double y, double z);
double simplex_noise_2d(double x, double y);

// Fractal noise functions
double fractal_noise_3d(double x, double y, double z, int octaves, double persistence);
double fractal_noise_2d(double x, double y, int octaves, double persistence);

// Utility functions
void noise_init_seed(unsigned int seed);
double noise_normalize(double value, double min, double max);

// Universe-wide noise seed management
void world_set_universe_noise_seed(const char *base_seed);
bool world_has_universe_noise_seed(void);

#endif // WORLD_NOISE_H
