#ifndef WORLD_PHYSICS_H
#define WORLD_PHYSICS_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration
typedef struct World World;

// Core physics functions
float world_get_gravity(const World* world);
void world_step_actors(World* world, float dt_seconds);
void world_step_fluids(World* world, int max_cells);
void world_step_temperature(World* world, int max_cells);

// Entity physics
void world_apply_entity_gravity(World* world, float* entity_x, float* entity_y, float* entity_z,
                               float* velocity_z, float dt_seconds, bool is_flying);
bool world_check_collision(World* world, float x, float y, float z);
int world_get_terrain_height(World* world, int x, int y);

// Spring water mechanics
void world_push_spring_water(World* world, uint32_t x, uint32_t y, uint32_t z);

// Fluid simulation (compatibility)
bool world_simulate_water_flow(World* world, uint32_t iterations);
bool world_simulate_magma_flow(World* world, uint32_t iterations);
void world_update_fluid_pressures(World* world);

// Gravity and falling blocks
bool world_simulate_gravity(World* world);
bool world_update_falling_blocks(World* world);

// Temperature and heat transfer
void world_simulate_heat_transfer(World* world, float delta_time);
float world_get_temperature_at(World* world, uint32_t x, uint32_t y, uint32_t z);
void world_set_temperature_at(World* world, uint32_t x, uint32_t y, uint32_t z, float temp);

// Magma field sampling
float world_sample_magma_field(World* world, int x, int y, int z);
int world_magma_seed_at(World* world, int x, int y, int z);

// Occupancy and density
void world_update_occupancy(World* world);
bool world_refresh_occupancy_bitfield(World* world);
float world_sample_occupancy_noise(World* world, int x, int y, int z);

// Stone field and rarity
float world_sample_stone_field(World* world, int x, int y, int z);
float world_sample_rarity_column(World* world, int x, int y);

// Physics constants
#define WORLD_PHYSICS_WATER_FLOW_RATE 0.8f
#define WORLD_PHYSICS_MAGMA_FLOW_RATE 0.3f
#define WORLD_PHYSICS_HEAT_TRANSFER_RATE 0.1f

#endif // WORLD_PHYSICS_H
