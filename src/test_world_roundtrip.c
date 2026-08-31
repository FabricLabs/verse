#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "world_bulk_ops.h"

// Serialization fidelity test.
//
// The legacy per-voxel stream in the save format stores one hex digit per voxel, which is
// only the low 4 bits of the type, and carries no condition_mask or data8 at all. The VOX2
// section added alongside it restores those. This test pins that down so the loss cannot
// come back, and it checks the whole grid rather than only the voxels it plants.

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "✓" : "✗", what);
    if (!ok) failures++;
}

int main(void)
{
    const uint32_t SIZE = 32;

    printf("World Serialization Round-Trip Test\n");
    printf("===================================\n\n");

    World *original = world_create(SIZE, SIZE, SIZE);
    if (!original) {
        printf("Failed to create world\n");
        return 1;
    }
    world_generate_with_type(original, "roundtrip_seed", WORLD_TYPE_WILDERNESS);

    const size_t count = (size_t)SIZE * SIZE * SIZE;

    // Plant state the legacy stream provably cannot represent.
    original->voxels[100].type = VOXEL_ORE_PLATINUM; // type >= 16
    original->voxels[200].type = VOXEL_CRYSTAL_BLUE;
    original->voxels[300].condition_mask = 0x8000000000000001ULL; // lowest and highest bit
    original->voxels[301].condition_mask = 0x00000000000000FFULL;
    original->voxels[400].data8 = 0x0102030405060708ULL; // all eight packed fields

    VoxelType *types = malloc(count * sizeof(VoxelType));
    if (!types) {
        world_destroy(original);
        return 1;
    }
    for (size_t i = 0; i < count; i++)
        types[i] = original->voxels[i].type;

    char *data = world_serialize(original);
    if (!data) {
        printf("✗ world_serialize returned NULL\n");
        free(types);
        world_destroy(original);
        return 1;
    }
    printf("Serialized %zu voxels into %zu bytes\n\n", count, strlen(data));

    World *restored = world_deserialize(data);
    if (!restored) {
        printf("✗ world_deserialize returned NULL\n");
        free(data);
        free(types);
        world_destroy(original);
        return 1;
    }

    printf("Fidelity:\n");
    check(restored->width == SIZE && restored->height == SIZE && restored->depth == SIZE,
          "dimensions preserved");

    size_t type_mismatch = 0;
    for (size_t i = 0; i < count; i++)
        if (restored->voxels[i].type != types[i]) type_mismatch++;
    if (type_mismatch)
        printf("  (%zu of %zu voxel types differ)\n", type_mismatch, count);
    check(type_mismatch == 0, "every voxel type preserved");

    check(restored->voxels[100].type == VOXEL_ORE_PLATINUM,
          "type >= 16 preserved (VOXEL_ORE_PLATINUM)");
    check(restored->voxels[200].type == VOXEL_CRYSTAL_BLUE,
          "type >= 16 preserved (VOXEL_CRYSTAL_BLUE)");
    check(restored->voxels[300].condition_mask == 0x8000000000000001ULL,
          "condition_mask preserved across the full 64 bits");
    check(restored->voxels[301].condition_mask == 0x00000000000000FFULL,
          "condition_mask preserved for low-byte conditions");
    check(restored->voxels[400].data8 == 0x0102030405060708ULL,
          "data8 packed fields preserved");

    size_t stray_conditions = 0, stray_data = 0;
    for (size_t i = 0; i < count; i++) {
        if (restored->voxels[i].condition_mask != original->voxels[i].condition_mask)
            stray_conditions++;
        if (restored->voxels[i].data8 != original->voxels[i].data8)
            stray_data++;
    }
    check(stray_conditions == 0, "no spurious condition masks introduced");
    check(stray_data == 0, "no spurious data8 values introduced");

    // Metadata block (separate from VOX2) must survive too.
    check(strcmp(restored->seed_id, original->seed_id) == 0, "seed_id preserved");
    check(restored->generation_type == original->generation_type, "generation_type preserved");

    // A save without a VOX2 section must still load: truncating at the section boundary
    // reproduces exactly what pre-VOX2 files look like on disk.
    char *legacy = strdup(data);
    if (legacy) {
        char *cut = strstr(legacy, "\nVOX2\n");
        if (cut) *cut = '\0';
        World *old_style = world_deserialize(legacy);
        check(old_style != NULL, "a save with no VOX2 section still loads");
        if (old_style) {
            check(old_style->width == SIZE && old_style->generation_type == original->generation_type,
                  "pre-VOX2 save keeps its dimensions and metadata");
            world_destroy(old_style);
        }
        free(legacy);
    }

    // The in-memory round-trip above proves the format. Players hit the disk path, so
    // check that world_save/world_load carry the same state through a file.
    if (world_save(original, "test_world_roundtrip.world")) {
        World *from_disk = world_create(SIZE, SIZE, SIZE);
        if (from_disk && world_load(from_disk, "test_world_roundtrip.world")) {
            size_t disk_mismatch = 0;
            for (size_t i = 0; i < count; i++)
                if (from_disk->voxels[i].type != types[i]) disk_mismatch++;
            check(disk_mismatch == 0, "every voxel type survives a disk round-trip");
            check(from_disk->voxels[100].type == VOXEL_ORE_PLATINUM,
                  "type >= 16 survives a disk round-trip");
            check(from_disk->voxels[300].condition_mask == 0x8000000000000001ULL,
                  "condition_mask survives a disk round-trip");
            check(from_disk->voxels[400].data8 == 0x0102030405060708ULL,
                  "data8 survives a disk round-trip");
        } else {
            check(0, "world_load read back the saved file");
        }
        if (from_disk) world_destroy(from_disk);
        remove("test_world_roundtrip.world");
    } else {
        check(0, "world_save wrote the file");
    }

    free(types);
    free(data);
    world_destroy(restored);
    world_destroy(original);

    printf("\n");
    if (failures == 0) {
        printf("=== Serialization is lossless ===\n");
        return 0;
    }
    printf("=== %d fidelity check(s) FAILED ===\n", failures);
    return 1;
}
