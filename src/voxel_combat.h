#ifndef VERSE_VOXEL_COMBAT_H
#define VERSE_VOXEL_COMBAT_H

#include <stdbool.h>
#include <stdint.h>

#include "item.h"
#include "material_worlds.h"
#include "voxel.h"

// A parent voxel is MATERIAL_WORLD_SIZE^3 subcomponents. Drops are VOXEL_DROP_EDGE^3 chunks of that
// lattice, so one full block yields (32/4)^3 = 512 pieces that inventory accumulates fractionally.
#define VOXEL_DROP_EDGE 4
#define VOXEL_PIECES_PER_BLOCK                                                       \
  ((MATERIAL_WORLD_SIZE / VOXEL_DROP_EDGE) * (MATERIAL_WORLD_SIZE / VOXEL_DROP_EDGE) * \
   (MATERIAL_WORLD_SIZE / VOXEL_DROP_EDGE))

// How much hit-points a voxel can take before it is destroyed. Fireball damage is applied against
// this, and VOXEL_FIELD_DAMAGE stores the accumulated amount scaled into 0..255.
uint8_t voxel_durability(VoxelType type);

// Bedrock and fluids are not breakable by combat.
bool voxel_is_destructible(VoxelType type);

// Convert an impact speed (voxels/s) into hit-points. Calibrated so stone at 14 vox/s deals 18 HP
// (the spirit's fireball); lighter materials take more, heavier ones less. Frame-rate independent:
// the same leftover speed always yields the same damage.
uint8_t voxel_impulse_damage(VoxelType type, float speed_vox_s);

// Apply that impulse as damage. Returns true if the voxel should be destroyed.
bool voxel_apply_impulse(Voxel *voxel, float speed_vox_s);

// Map a broken voxel to the item its pieces become. ITEM_NONE means it yields nothing.
ItemId voxel_type_to_drop_item(VoxelType type);

// Darken a sampled surface texel when the parent voxel is damaged. `seed` should be stable per
// voxel (e.g. a hash of its world coordinates) so cracks do not swim as the camera moves.
void voxel_crack_modulate(uint8_t damage, uint32_t seed, float u, float v,
                          uint8_t *r, uint8_t *g, uint8_t *b);

// Bake a cracked 32x32 ARGB face into `out` (MATERIAL_FACE_SIZE^2). Uses the material template when
// one exists; otherwise fills with the flat `base_*` colour. Returns false only on bad args.
bool voxel_bake_cracked_face(VoxelType type, MaterialFace face, uint8_t damage, uint32_t seed,
                             uint8_t base_r, uint8_t base_g, uint8_t base_b, uint32_t *out);

uint32_t voxel_crack_seed(int x, int y, int z);

#endif // VERSE_VOXEL_COMBAT_H
