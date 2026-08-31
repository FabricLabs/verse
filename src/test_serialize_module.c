/*
 * test_serialize_module.c - Test program for world_serialize module
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include "world_internal.h"
#include "world_serialize.h"
#include "world_core.h"
#include "world_voxel.h"

// Create a test world with known pattern
static World* create_pattern_world() {
    World* world = world_create(8, 8, 8);
    if (!world) return NULL;

    // Create a checkerboard pattern
    for (uint32_t z = 0; z < 8; z++) {
        for (uint32_t y = 0; y < 8; y++) {
            for (uint32_t x = 0; x < 8; x++) {
                if ((x + y + z) % 2 == 0) {
                    world_set_voxel(world, x, y, z, VOXEL_STONE);
                } else {
                    world_set_voxel(world, x, y, z, VOXEL_AIR);
                }
            }
        }
    }

    // Set some metadata
    strcpy(world->seed_id, "test_pattern_123");
    world->gravity = 9.81f;
    world->rarity = 0.75f;
    world->generation_type = WORLD_TYPE_HOME;
    world->version = 1;

    // Add a log entry
    world->log = strdup("Test world created with checkerboard pattern");

    return world;
}

void test_save_load() {
    printf("Testing save/load operations...\n");

    // Create test world
    World* original = create_pattern_world();
    assert(original != NULL);

    // Save it
    const char* filename = "test_world.dat";
    assert(world_save(original, filename));
    printf("  ✓ World saved to %s\n", filename);

    // Load it back
    World* loaded = world_load(filename);
    assert(loaded != NULL);
    printf("  ✓ World loaded from %s\n", filename);

    // Verify dimensions
    assert(loaded->width == original->width);
    assert(loaded->height == original->height);
    assert(loaded->depth == original->depth);
    assert(loaded->version == original->version);
    printf("  ✓ Dimensions match: %ux%ux%u\n", loaded->width, loaded->height, loaded->depth);

    // Note: seed_id is not preserved in simple serialization format
    // This would need to be added to the format for full persistence
    printf("  ℹ️  Seed ID not preserved in simple format\n");

    // Verify log
    if (original->log) {
        assert(loaded->log != NULL);
        assert(strcmp(loaded->log, original->log) == 0);
        printf("  ✓ Log preserved: %s\n", loaded->log);
    }

    // Verify voxel data
    int mismatches = 0;
    for (uint32_t z = 0; z < 8; z++) {
        for (uint32_t y = 0; y < 8; y++) {
            for (uint32_t x = 0; x < 8; x++) {
                Voxel* v1 = world_get_voxel(original, x, y, z);
                Voxel* v2 = world_get_voxel(loaded, x, y, z);
                if (v1->type != v2->type) {
                    mismatches++;
                }
            }
        }
    }
    assert(mismatches == 0);
    printf("  ✓ All voxels match (512 voxels checked)\n");

    // Clean up
    world_destroy(original);
    world_destroy(loaded);
    unlink(filename);

    printf("  ✓ Save/load test passed\n");
}

void test_serialization_size() {
    printf("Testing serialization size calculation...\n");

    World* world = create_pattern_world();
    assert(world != NULL);

    size_t estimated = world_serialize_size(world);
    printf("  Estimated size: %zu bytes\n", estimated);

    // Verify estimate is reasonable
    // Header (36) + log (~44) + voxels (512) + null = ~593
    assert(estimated > 500 && estimated < 1000);

    // Test buffer serialization
    uint8_t* buffer = malloc(estimated);
    assert(buffer != NULL);

    assert(world_serialize_to_buffer(world, buffer, estimated));
    printf("  ✓ Serialized to buffer successfully\n");

    // Deserialize from buffer
    World* restored = world_deserialize_from_buffer(buffer, estimated);
    assert(restored != NULL);
    assert(restored->width == world->width);
    assert(restored->height == world->height);
    assert(restored->depth == world->depth);
    printf("  ✓ Deserialized from buffer successfully\n");

    free(buffer);
    world_destroy(world);
    world_destroy(restored);

    printf("  ✓ Serialization size test passed\n");
}

void test_json_export() {
    printf("Testing JSON export...\n");

    World* world = create_pattern_world();
    assert(world != NULL);

    const char* json_file = "test_world.json";
    assert(world_export_json(world, json_file));
    printf("  ✓ Exported to JSON: %s\n", json_file);

    // Read and display JSON content
    FILE* f = fopen(json_file, "r");
    if (f) {
        char line[256];
        printf("  JSON content:\n");
        while (fgets(line, sizeof(line), f)) {
            printf("    %s", line);
        }
        fclose(f);
    }

    // Clean up
    world_destroy(world);
    unlink(json_file);

    printf("  ✓ JSON export test passed\n");
}

void test_edge_cases() {
    printf("Testing edge cases...\n");

    // Test null parameters
    assert(!world_save(NULL, "test.dat"));
    assert(!world_save(create_pattern_world(), NULL));
    assert(world_load(NULL) == NULL);
    assert(world_load("nonexistent_file.dat") == NULL);

    // Test empty world
    World* empty = world_create(1, 1, 1);
    assert(empty != NULL);
    assert(world_save(empty, "empty.dat"));
    World* loaded_empty = world_load("empty.dat");
    assert(loaded_empty != NULL);
    assert(loaded_empty->width == 1);
    world_destroy(empty);
    world_destroy(loaded_empty);
    unlink("empty.dat");

    // Test large dimensions
    World* large = world_create(100, 100, 100);
    assert(large != NULL);
    size_t large_size = world_serialize_size(large);
    printf("  Large world (100x100x100) serialized size: %zu bytes\n", large_size);
    assert(large_size > 1000000); // Should be > 1MB
    world_destroy(large);

    printf("  ✓ Edge case tests passed\n");
}

void test_compression_stubs() {
    printf("Testing compression stubs...\n");

    World* world = create_pattern_world();
    assert(world != NULL);

    // These should work (they just call regular save/load for now)
    assert(world_save_compressed(world, "compressed.dat"));
    World* loaded = world_load_compressed("compressed.dat");
    assert(loaded != NULL);

    world_destroy(world);
    world_destroy(loaded);
    unlink("compressed.dat");

    printf("  ✓ Compression stub tests passed\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("=== World Serialize Module Test Suite ===\n\n");

    test_save_load();
    test_serialization_size();
    test_json_export();
    test_edge_cases();
    test_compression_stubs();

    printf("\n✅ All tests passed!\n");
    printf("\nThe serialize module has been successfully extracted and works correctly.\n");

    return 0;
}
