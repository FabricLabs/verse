#include "entropy_field.h"
#include <math.h>
#include <string.h>

// Forward declaration for Perlin noise (from noise-c library)
double perlin_noise(double x, double y, double z);

// Default entropy field parameters optimized for terrain generation
EntropyField entropy_field_default(uint32_t universe_seed)
{
    EntropyField field = {
        .universe_seed = universe_seed,
        .layer1_scale = 0.001f,      // Large-scale continental features
        .layer2_scale = 0.01f,       // Medium-scale regional features
        .layer3_scale = 0.1f,        // Small-scale local features
        .layer1_amplitude = 0.5f,    // Primary influence
        .layer2_amplitude = 0.3f,    // Secondary influence
        .layer3_amplitude = 0.2f,    // Detail influence
        .use_domain_warping = true,
        .warp_amplitude = 2.0f,      // Moderate domain warping
        .warp_scale = 0.005f         // Low-frequency warping
    };
    return field;
}

// Custom entropy field with user-defined parameters
EntropyField entropy_field_custom(uint32_t universe_seed,
                                 float layer1_scale, float layer1_amplitude,
                                 float layer2_scale, float layer2_amplitude,
                                 float layer3_scale, float layer3_amplitude,
                                 bool use_domain_warping,
                                 float warp_amplitude, float warp_scale)
{
    EntropyField field = {
        .universe_seed = universe_seed,
        .layer1_scale = layer1_scale,
        .layer2_scale = layer2_scale,
        .layer3_scale = layer3_scale,
        .layer1_amplitude = layer1_amplitude,
        .layer2_amplitude = layer2_amplitude,
        .layer3_amplitude = layer3_amplitude,
        .use_domain_warping = use_domain_warping,
        .warp_amplitude = warp_amplitude,
        .warp_scale = warp_scale
    };
    return field;
}

// Domain warping to break up repetition and create natural variation
static void apply_domain_warping(float *x, float *y, float *z,
                                float amplitude, float scale, uint32_t seed)
{
    // Use different irrational multipliers to avoid resonance
    const float mult1 = 1.318f;  // Golden ratio approximation
    const float mult2 = 0.618f;  // Golden ratio conjugate
    const float mult3 = 2.718f;  // Euler's number approximation

    // Sample warping field at different frequencies
    float warp_x = (float)perlin_noise(*x * scale, *y * scale, *z * scale + seed);
    float warp_y = (float)perlin_noise(*x * scale * mult1, *y * scale * mult1, *z * scale + seed + 37.7f);
    float warp_z = (float)perlin_noise(*x * scale * mult2, *y * scale * mult2, *z * scale + seed + 19.1f);

    // Apply warping (convert from [0,1] to [-1,1] range)
    *x += amplitude * (warp_x - 0.5f) * 2.0f;
    *y += amplitude * (warp_y - 0.5f) * 2.0f;
    *z += amplitude * (warp_z - 0.5f) * 2.0f;
}

// Core entropy field sampling with three-phase noise generation
float entropy_field_sample(const EntropyField *field,
                          float universe_x, float universe_y, float universe_z)
{
    if (!field) return 0.0f;

    float x = universe_x;
    float y = universe_y;
    float z = universe_z;

    // Apply domain warping if enabled
    if (field->use_domain_warping) {
        apply_domain_warping(&x, &y, &z, field->warp_amplitude, field->warp_scale, field->universe_seed);
    }

    // Phase 1: Large-scale structure (continental features)
    float layer1 = (float)perlin_noise(x * field->layer1_scale,
                                       y * field->layer1_scale,
                                       z * field->layer1_scale + field->universe_seed * 0.0001f);

    // Phase 2: Medium-scale detail (regional features)
    float layer2 = (float)perlin_noise(x * field->layer2_scale,
                                       y * field->layer2_scale,
                                       z * field->layer2_scale + field->universe_seed * 0.0002f);

    // Phase 3: Fine-scale variation (local features)
    float layer3 = (float)perlin_noise(x * field->layer3_scale,
                                       y * field->layer3_scale,
                                       z * field->layer3_scale + field->universe_seed * 0.0003f);

    // Combine layers with weights and normalize to [0,1] range
    float result = field->layer1_amplitude * layer1 +
                   field->layer2_amplitude * layer2 +
                   field->layer3_amplitude * layer3;

    // Normalize to [0,1] range
    result = (result + 1.0f) * 0.5f;

    // Clamp to valid range
    if (result < 0.0f) result = 0.0f;
    if (result > 1.0f) result = 1.0f;

    return result;
}

// 2D sampling for height maps and surface features
float entropy_field_sample_2d(const EntropyField *field,
                             float universe_x, float universe_y)
{
    if (!field) return 0.0f;

    float x = universe_x;
    float y = universe_y;
    float z = 0.0f; // Use a fixed Z for 2D sampling

    // Apply domain warping if enabled
    if (field->use_domain_warping) {
        apply_domain_warping(&x, &y, &z, field->warp_amplitude, field->warp_scale, field->universe_seed);
    }

    // Sample only X and Y layers
    float layer1 = (float)perlin_noise(x * field->layer1_scale,
                                       y * field->layer1_scale,
                                       field->universe_seed * 0.0001f);

    float layer2 = (float)perlin_noise(x * field->layer2_scale,
                                       y * field->layer2_scale,
                                       field->universe_seed * 0.0002f);

    float layer3 = (float)perlin_noise(x * field->layer3_scale,
                                       y * field->layer3_scale,
                                       field->universe_seed * 0.0003f);

    // Combine and normalize
    float result = field->layer1_amplitude * layer1 +
                   field->layer2_amplitude * layer2 +
                   field->layer3_amplitude * layer3;

    result = (result + 1.0f) * 0.5f;

    if (result < 0.0f) result = 0.0f;
    if (result > 1.0f) result = 1.0f;

    return result;
}

// Add two entropy fields together for composition
void entropy_field_add_fields(EntropyField *result,
                            const EntropyField *field1,
                            const EntropyField *field2)
{
    if (!result || !field1 || !field2) return;

    // Use the first field's seed
    result->universe_seed = field1->universe_seed;

    // Average the scales and amplitudes
    result->layer1_scale = (field1->layer1_scale + field2->layer1_scale) * 0.5f;
    result->layer2_scale = (field1->layer2_scale + field2->layer2_scale) * 0.5f;
    result->layer3_scale = (field1->layer3_scale + field2->layer3_scale) * 0.5f;

    result->layer1_amplitude = (field1->layer1_amplitude + field2->layer1_amplitude) * 0.5f;
    result->layer2_amplitude = (field1->layer2_amplitude + field2->layer2_amplitude) * 0.5f;
    result->layer3_amplitude = (field1->layer3_amplitude + field2->layer3_amplitude) * 0.5f;

    // Use the first field's warping settings
    result->use_domain_warping = field1->use_domain_warping;
    result->warp_amplitude = field1->warp_amplitude;
    result->warp_scale = field1->warp_scale;
}

// Scale entropy field by a factor
void entropy_field_scale(EntropyField *field, float factor)
{
    if (!field) return;

    field->layer1_amplitude *= factor;
    field->layer2_amplitude *= factor;
    field->layer3_amplitude *= factor;
}

// Specialized entropy field for terrain generation
EntropyField entropy_field_terrain(uint32_t universe_seed)
{
    EntropyField field = {
        .universe_seed = universe_seed,
        .layer1_scale = 0.0005f,     // Very large-scale continental features
        .layer2_scale = 0.005f,      // Large-scale regional features
        .layer3_scale = 0.05f,       // Medium-scale local features
        .layer1_amplitude = 0.6f,    // Strong continental influence
        .layer2_amplitude = 0.3f,    // Moderate regional influence
        .layer3_amplitude = 0.1f,    // Light local detail
        .use_domain_warping = true,
        .warp_amplitude = 3.0f,      // Strong domain warping for natural terrain
        .warp_scale = 0.003f         // Low-frequency warping
    };
    return field;
}

// Specialized entropy field for ore deposits
EntropyField entropy_field_ore_deposits(uint32_t universe_seed)
{
    EntropyField field = {
        .universe_seed = universe_seed,
        .layer1_scale = 0.002f,      // Large-scale ore regions
        .layer2_scale = 0.02f,       // Medium-scale ore veins
        .layer3_scale = 0.2f,        // Small-scale ore pockets
        .layer1_amplitude = 0.4f,    // Regional ore distribution
        .layer2_amplitude = 0.4f,    // Vein structure
        .layer3_amplitude = 0.2f,    // Local variation
        .use_domain_warping = true,
        .warp_amplitude = 1.5f,      // Moderate warping
        .warp_scale = 0.008f         // Medium-frequency warping
    };
    return field;
}

// Specialized entropy field for vegetation
EntropyField entropy_field_vegetation(uint32_t universe_seed)
{
    EntropyField field = {
        .universe_seed = universe_seed,
        .layer1_scale = 0.01f,       // Large-scale biome features
        .layer2_scale = 0.1f,        // Medium-scale forest patches
        .layer3_scale = 0.5f,        // Small-scale individual trees
        .layer1_amplitude = 0.3f,    // Biome influence
        .layer2_amplitude = 0.4f,    // Forest density
        .layer3_amplitude = 0.3f,    // Local variation
        .use_domain_warping = false, // No warping for vegetation (more predictable)
        .warp_amplitude = 0.0f,
        .warp_scale = 0.0f
    };
    return field;
}

// Specialized entropy field for structures
EntropyField entropy_field_structures(uint32_t universe_seed)
{
    EntropyField field = {
        .universe_seed = universe_seed,
        .layer1_scale = 0.005f,      // Large-scale structure regions
        .layer2_scale = 0.05f,       // Medium-scale structure clusters
        .layer3_scale = 0.3f,        // Small-scale individual structures
        .layer1_amplitude = 0.5f,    // Regional structure density
        .layer2_amplitude = 0.3f,    // Cluster formation
        .layer3_amplitude = 0.2f,    // Local placement
        .use_domain_warping = true,
        .warp_amplitude = 2.5f,      // Strong warping for natural placement
        .warp_scale = 0.006f         // Medium-frequency warping
    };
    return field;
}

// Domain warping utilities for external use
void entropy_field_warp_coordinates(float *x, float *y, float *z,
                                   float amplitude, float scale, uint32_t seed)
{
    if (!x || !y || !z) return;
    apply_domain_warping(x, y, z, amplitude, scale, seed);
}

// Sample individual noise layers for advanced composition
float entropy_field_sample_layer(const EntropyField *field, int layer_index,
                               float universe_x, float universe_y, float universe_z)
{
    if (!field) return 0.0f;

    float x = universe_x;
    float y = universe_y;
    float z = universe_z;

    // Apply domain warping if enabled
    if (field->use_domain_warping) {
        apply_domain_warping(&x, &y, &z, field->warp_amplitude, field->warp_scale, field->universe_seed);
    }

    float scale, seed_offset;
    switch (layer_index) {
        case 1:
            scale = field->layer1_scale;
            seed_offset = field->universe_seed * 0.0001f;
            break;
        case 2:
            scale = field->layer2_scale;
            seed_offset = field->universe_seed * 0.0002f;
            break;
        case 3:
            scale = field->layer3_scale;
            seed_offset = field->universe_seed * 0.0003f;
            break;
        default:
            return 0.0f;
    }

    float result = (float)perlin_noise(x * scale, y * scale, z * scale + seed_offset);
    return (result + 1.0f) * 0.5f; // Normalize to [0,1]
}
