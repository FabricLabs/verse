# World.c Cleanup Plan

## Overview

The world.c file currently contains 7,794 lines with many functions already extracted to modular files. This document outlines what needs to be removed and what should remain.

## Functions to REMOVE (already extracted)

### From world_core.c:
- `World* world_create(...)` - Lines ~3800
- `void world_destroy(World* world)` - Line 3878
- `World* world_create_empty(...)` - Variant of world_create

### From world_generation_*.c:
- `void world_generate(World* world, const char* seed)` - Line 4537
- `void world_generate_with_type(...)` - Line 5305
- `void world_generate_with_type_and_fill(...)` - Line 5415
- `static void world_generate_home(...)` - Line 5857
- `static void world_generate_farm(...)` - Line 6002
- `static void world_generate_random(...)` - Line 6066
- `static void world_generate_wilderness(...)` - Line 6208
- `static void world_generate_underworld(...)` - Line 311
- `static void world_generate_cloud(...)` - Line 400
- `static void world_generate_arena(...)` - Line 5974
- `static void world_generate_scoured(...)` - Line 6318
- `static void world_generate_labyrinth_square(...)` - Line 6327

### From world_voxel.c:
- `Voxel* world_get_voxel(...)` - Multiple locations
- `bool world_set_voxel(...)` - Line 4356
- `void world_voxel_type_color(...)` - Line 3919
- `int world_height_at(...)` - Replaced by world_height_at_fast

### From world_serialize.c:
- `bool world_save(...)` - Line 4824
- `World* world_load(...)` - Line 4851 (returns bool in world.c, World* in serialize)

### From world_physics.c:
- `void world_step_actors(...)` - Line 5201
- `void world_step_fluids(...)` - Line 6819
- Temperature stepping functions

### From world_noise.c:
- `double perlin_noise(...)` - Line 2913
- `static void init_perlin_noise(...)` - Line 2865
- Perlin permutation tables

## Functions to KEEP in world.c

### Universe Integration:
- `void world_set_universe_noise_seed(...)` - Line 47
- `static inline float universe_z_bias()` - Line 19
- Global perlin seed management
- NoiseWarpParams structures

### RNG Functions:
- `void world_rng_seed(...)` - Line 489
- `int world_rng_range(...)` - Line 520
- `static uint32_t world_rng_next(...)` - Internal RNG

### Pathfinding:
- `bool world_find_path_2d(...)` - Line 527

### Save/Load Variants:
- `bool world_save_by_seed(...)` - Line 4949
- `bool world_load_by_seed(...)` - Line 5006
- `bool world_exists_by_seed(...)` - Line 5033
- `bool world_save_with_universe(...)` - Line 5069
- `World* world_load_with_universe(...)` - Universe-aware loading

### Rendering/Visualization:
- `bool world_face_dimensions(...)` - Line 3138
- `bool world_face_to_rgb24(...)` - Line 3169
- `bool world_get_slice_voxels(...)` - Line 3268
- `bool world_copy_subvolume(...)` - Line 3376
- `bool world_raycast_first_hit(...)` - Line 3479

### World Operations:
- `bool world_autocrop(...)` - Line 4398
- `bool world_draw_sphere(...)` - Line 4474
- `void world_blend_adjacent_face(...)` - Line 7380
- `int world_find_lowest_connected_plane(...)` - Line 6606

### Condition System:
- `bool world_add_voxel_condition(...)` - Line 4573
- `bool world_remove_voxel_condition(...)` - Line 4586
- `bool world_has_voxel_condition(...)` - Line 4599
- `void world_clear_voxel_conditions(...)` - Line 4611

### Evolution/Epoch:
- `bool world_advance_epoch(...)` - Line 4619

### Logging:
- `bool world_set_log(...)` - Line 5242
- `bool world_append_log(...)` - Line 5270

### Springs/Water:
- `static void world_generate_spring_water(...)` - Line 7195
- `void world_update_springs(...)` - Line 6557

### Scoring/Level:
- `void world_update_score_and_level(...)` - Line 7704
- `void world_set_base_level(...)` - Line 7767
- `void world_increment_history_event(...)` - Line 7775
- `void world_add_unique_player(...)` - Line 7783

### Field Sampling (many noise-based functions):
- `float world_sample_magma_field(...)` - Line 2391
- `int world_magma_seed_at(...)` - Line 2398
- `float world_sample_occupancy_noise(...)` - Line 2633
- `float world_sample_rarity_column(...)` - Line 2641
- `float world_sample_stone_field(...)` - Line 2654
- Various ore and material generation functions

### Utility:
- `bool world_is_position_valid(...)` - Line 3891
- `float world_get_gravity(...)` - Line 5193
- `int world_height_at_fast(...)` - Line 81 (optimized version)
- `void world_generate_solid_fill(...)` - Line 300 (simple utility)

## Implementation Strategy

1. **Create new world.c** with:
   - Include all modular headers
   - Keep only functions listed above
   - Remove all duplicate implementations

2. **Update function signatures** where needed:
   - Some functions may need to use types from world_internal.h
   - Ensure proper includes for all dependencies

3. **Test compilation** with existing code
   - All verse_client.c dependencies should still work
   - All test programs should compile

4. **File size reduction**:
   - From: 7,794 lines
   - Expected: ~2,000-3,000 lines (65-70% reduction)
