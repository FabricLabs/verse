#ifndef WORLD_ENTROPY_GENERATOR_H
#define WORLD_ENTROPY_GENERATOR_H

#include "entropy_field.h"
#include "universe_coords.h"
#include "world.h"

// New world generation system using universe-wide entropy fields
// This replaces the old noise offset system with seamless, boundary-free generation

// Initialize the entropy field system for world generation
void world_entropy_generator_init(uint32_t universe_seed);

// Generate terrain using entropy field sampling
void world_generate_terrain_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z);

// Generate ore deposits using entropy field sampling
void world_generate_ore_deposits_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z);

// Generate vegetation using entropy field sampling
void world_generate_vegetation_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z);

// Generate structures using entropy field sampling
void world_generate_structures_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z);

// Sample terrain height at a specific world position
float world_sample_terrain_height_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                         uint32_t local_x, uint32_t local_y);

// Sample ore density at a specific world position
float world_sample_ore_density_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                      uint32_t local_x, uint32_t local_y, uint32_t local_z);

// Sample vegetation density at a specific world position
float world_sample_vegetation_density_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                             uint32_t local_x, uint32_t local_y);

// Sample structure placement probability at a specific world position
float world_sample_structure_probability_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z,
                                                uint32_t local_x, uint32_t local_y);

// Composite generation function that uses all entropy fields
void world_generate_composite_entropy(World *world, int32_t world_x, int32_t world_y, int32_t world_z);

// Get the current entropy field configuration
const EntropyField* world_entropy_get_terrain_field(void);
const EntropyField* world_entropy_get_ore_field(void);
const EntropyField* world_entropy_get_vegetation_field(void);
const EntropyField* world_entropy_get_structure_field(void);

// Update entropy field parameters (for runtime tuning)
void world_entropy_update_terrain_field(const EntropyField *field);
void world_entropy_update_ore_field(const EntropyField *field);
void world_entropy_update_vegetation_field(const EntropyField *field);
void world_entropy_update_structure_field(const EntropyField *field);

#endif // WORLD_ENTROPY_GENERATOR_H
