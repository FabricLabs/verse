#ifndef ENTROPY_FIELD_H
#define ENTROPY_FIELD_H

#include <stdint.h>
#include <stdbool.h>

// Universe-wide entropy field system
// This provides a continuous, seamless noise field across all worlds
// with no boundary artifacts or discontinuities

typedef struct EntropyField {
    uint32_t universe_seed;           // Global seed for all noise generation
    float layer1_scale;              // Large-scale features (e.g., 0.001)
    float layer2_scale;              // Medium-scale features (e.g., 0.01)
    float layer3_scale;              // Small-scale features (e.g., 0.1)
    float layer1_amplitude;          // Weight for layer 1
    float layer2_amplitude;          // Weight for layer 2
    float layer3_amplitude;          // Weight for layer 3
    bool use_domain_warping;         // Enable domain warping for natural variation
    float warp_amplitude;            // Domain warp strength
    float warp_scale;                // Domain warp frequency
} EntropyField;

// Initialize entropy field with default parameters
EntropyField entropy_field_default(uint32_t universe_seed);

// Initialize entropy field with custom parameters
EntropyField entropy_field_custom(uint32_t universe_seed,
                                 float layer1_scale, float layer1_amplitude,
                                 float layer2_scale, float layer2_amplitude,
                                 float layer3_scale, float layer3_amplitude,
                                 bool use_domain_warping,
                                 float warp_amplitude, float warp_scale);

// Sample the entropy field at a specific universe coordinate
float entropy_field_sample(const EntropyField *field,
                          float universe_x, float universe_y, float universe_z);

// Sample the entropy field in 2D (useful for height maps, etc.)
float entropy_field_sample_2d(const EntropyField *field,
                             float universe_x, float universe_y);

// Add two entropy fields together (for composition)
void entropy_field_add_fields(EntropyField *result,
                            const EntropyField *field1,
                            const EntropyField *field2);

// Multiply entropy field by a scalar factor
void entropy_field_scale(EntropyField *field, float factor);

// Create a specialized entropy field for specific use cases
EntropyField entropy_field_terrain(uint32_t universe_seed);
EntropyField entropy_field_ore_deposits(uint32_t universe_seed);
EntropyField entropy_field_vegetation(uint32_t universe_seed);
EntropyField entropy_field_structures(uint32_t universe_seed);

// Domain warping utilities (can be used independently)
void entropy_field_warp_coordinates(float *x, float *y, float *z,
                                   float amplitude, float scale, uint32_t seed);

// Noise layer sampling (for advanced composition)
float entropy_field_sample_layer(const EntropyField *field, int layer_index,
                               float universe_x, float universe_y, float universe_z);

#endif // ENTROPY_FIELD_H
