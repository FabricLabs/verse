#include "character.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test function to demonstrate player ID creation and validation
void test_player_id_system() {
    printf("=== VERSE Player ID System Test ===\n");
    printf("Testing BIP 143 compliant double SHA256 player ID generation\n\n");

    // Test data
    const char* character_name = "TestPlayer";
    const char* world_seed = "test_world_seed_12345";
    uint32_t version = 1;

    printf("Creating player ID for:\n");
    printf("  Character: %s\n", character_name);
    printf("  World Seed: %s\n", world_seed);
    printf("  Version: %u\n\n", version);

    // Create player ID
    PlayerId* player_id = player_id_create(character_name, world_seed, version);
    if (!player_id) {
        printf("Failed to create player ID\n");
        return;
    }

    // Convert to hex string
    char* hex_id = player_id_to_hex(player_id);
    if (!hex_id) {
        printf("Failed to convert player ID to hex\n");
        player_id_destroy(player_id);
        return;
    }

    printf("Generated Player ID (hex): %s\n", hex_id);

    // Validate the player ID
    bool is_valid = player_id_validate(player_id);
    printf("Player ID validation: %s\n", is_valid ? "PASS" : "FAIL");

    // Test preimage serialization
    size_t serialized_size;
    uint8_t* serialized = player_id_preimage_serialize(&player_id->preimage, &serialized_size);
    if (serialized) {
        printf("Preimage serialization: SUCCESS (%zu bytes)\n", serialized_size);
        free(serialized);
    } else {
        printf("Preimage serialization: FAILED\n");
    }

    // Test creating from hex
    PlayerId* player_id_from_hex_test = player_id_from_hex(hex_id);
    if (player_id_from_hex_test) {
        char* hex_id_test = player_id_to_hex(player_id_from_hex_test);
        if (hex_id_test) {
            bool hex_match = strcmp(hex_id, hex_id_test) == 0;
            printf("Hex round-trip test: %s\n", hex_match ? "PASS" : "FAIL");
            free(hex_id_test);
        }
        player_id_destroy(player_id_from_hex_test);
    }

    // Test ID equality (same parameters should produce same ID)
    PlayerId* player_id2 = player_id_create(character_name, world_seed, version);
    if (player_id2) {
        bool equality_test = player_id_equals(player_id, player_id2);
        printf("ID equality test (same params): %s\n", equality_test ? "PASS" : "FAIL");
        player_id_destroy(player_id2);
    }

    // Test different character name (should produce different ID)
    PlayerId* player_id3 = player_id_create("DifferentPlayer", world_seed, version);
    if (player_id3) {
        char* hex_id3 = player_id_to_hex(player_id3);
        printf("Different character ID: %s\n", hex_id3 ? hex_id3 : "FAILED");
        bool inequality_test = !player_id_equals(player_id, player_id3);
        printf("ID inequality test (different character): %s\n", inequality_test ? "PASS" : "FAIL");
        if (hex_id3) free(hex_id3);
        player_id_destroy(player_id3);
    }

    // Test different world seed (should produce different ID)
    PlayerId* player_id4 = player_id_create(character_name, "different_world_seed", version);
    if (player_id4) {
        char* hex_id4 = player_id_to_hex(player_id4);
        printf("Different seed ID: %s\n", hex_id4 ? hex_id4 : "FAILED");
        bool inequality_test2 = !player_id_equals(player_id, player_id4);
        printf("ID inequality test (different seed): %s\n", inequality_test2 ? "PASS" : "FAIL");
        if (hex_id4) free(hex_id4);
        player_id_destroy(player_id4);
    }

    // Display preimage details
    printf("\nPreimage Details:\n");
    printf("  Magic: %02x%02x%02x%02x\n",
           player_id->preimage.magic[0], player_id->preimage.magic[1],
           player_id->preimage.magic[2], player_id->preimage.magic[3]);
    printf("  Version: %u\n",
           player_id->preimage.version[0] | (player_id->preimage.version[1] << 8) |
           (player_id->preimage.version[2] << 16) | (player_id->preimage.version[3] << 24));
    printf("  Type: %u\n",
           player_id->preimage.type[0] | (player_id->preimage.type[1] << 8) |
           (player_id->preimage.type[2] << 16) | (player_id->preimage.type[3] << 24));
    printf("  Size: %u\n",
           player_id->preimage.size[0] | (player_id->preimage.size[1] << 8) |
           (player_id->preimage.size[2] << 16) | (player_id->preimage.size[3] << 24));
    printf("  Preimage structure size: %zu bytes\n", sizeof(PlayerIdPreimage));
    printf("  Expected size: %zu bytes\n", (size_t)(4 + 4 + 32 + 32 + 4 + 4 + 32 + 64));

    // Cleanup
    free(hex_id);
    player_id_destroy(player_id);

    printf("\n=== Player ID System Test Complete ===\n");
}

// Test character save with player ID
void test_character_save_with_id() {
    printf("\n=== Testing Character Save with Player ID ===\n");

    const char* character_name = "TestCharacter";
    const char* world_seed = "test_save_world_seed";

    // Create a test save
    bool save_result = character_save_game(
        character_name,
        32, 32, 32,  // position
        10, 10, 10, 10, 10, 10,  // stats
        100,  // experience
        50,   // gold
        world_seed,
        0,    // attribute_points
        0,    // skill_points
        0u,   // unlocked_skills
        NULL  // skill_ranks
    );

    if (save_result) {
        printf("Character saved successfully with player ID\n");

        // Try to load the save
        CharacterSave loaded_save;
        bool load_result = character_load_game(character_name, &loaded_save);

        if (load_result) {
            printf("Character loaded successfully\n");

            // Validate the loaded player ID
            bool id_valid = player_id_validate(&loaded_save.player_id);
            printf("Loaded player ID validation: %s\n", id_valid ? "PASS" : "FAIL");

            // Display the loaded player ID
            char* hex_id = player_id_to_hex(&loaded_save.player_id);
            if (hex_id) {
                printf("Loaded Player ID: %s\n", hex_id);
                free(hex_id);
            }
        } else {
            printf("Failed to load character\n");
        }
    } else {
        printf("Failed to save character\n");
    }
}

// Test double SHA256 function
void test_double_sha256() {
    printf("\n=== Testing Double SHA256 Function ===\n");

    const char* test_input = "TestPlayer:test_world_seed_12345";
    uint8_t result[32];

    double_sha256_hash((const uint8_t*)test_input, strlen(test_input), result);

    printf("Input: %s\n", test_input);
    printf("Double SHA256 result: ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", result[i]);
    }
    printf("\n");

    // Test with different input
    const char* test_input2 = "DifferentPlayer:test_world_seed_12345";
    uint8_t result2[32];

    double_sha256_hash((const uint8_t*)test_input2, strlen(test_input2), result2);

    printf("Input2: %s\n", test_input2);
    printf("Double SHA256 result2: ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", result2[i]);
    }
    printf("\n");

    // Check if they're different
    bool different = memcmp(result, result2, 32) != 0;
    printf("Results are different: %s\n", different ? "PASS" : "FAIL");
}

// Test double SHA256 function with actual preimage data
void test_double_sha256_with_preimage() {
    printf("\n=== Testing Double SHA256 with Preimage Data ===\n");

    // Create a test preimage
    uint8_t test_preimage[176];
    memset(test_preimage, 0, 176);

    // Set magic bytes
    test_preimage[0] = 0x3d;
    test_preimage[1] = 0xf3;
    test_preimage[2] = 0xd3;
    test_preimage[3] = 0xc0;

    // Set version (little endian)
    test_preimage[4] = 1;
    test_preimage[5] = 0;
    test_preimage[6] = 0;
    test_preimage[7] = 0;

    // Set type (little endian)
    test_preimage[64] = 1;
    test_preimage[65] = 0;
    test_preimage[66] = 0;
    test_preimage[67] = 0;

    // Set size (little endian)
    test_preimage[68] = 10;
    test_preimage[69] = 0;
    test_preimage[70] = 0;
    test_preimage[71] = 0;

    // Set content hash (32 bytes starting at offset 72)
    const char* test_hash = "b301adeba8146c699558eda6f12c464d16d6c66fc4b8dec2a1e7d723415bd62e";
    for (int i = 0; i < 32; i++) {
        char hex[3] = {test_hash[i*2], test_hash[i*2+1], 0};
        test_preimage[72 + i] = (uint8_t)strtol(hex, NULL, 16);
    }

    uint8_t result[32];
    double_sha256_hash(test_preimage, 176, result);

    printf("Test preimage hash: %s\n", test_hash);
    printf("Double SHA256 result: ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", result[i]);
    }
    printf("\n");

    // Test with different content hash
    const char* test_hash2 = "a19d1b02105b7535d087e2cdfbe5439c1df60af63ff5bce48d49919bab2e92dd";
    for (int i = 0; i < 32; i++) {
        char hex[3] = {test_hash2[i*2], test_hash2[i*2+1], 0};
        test_preimage[72 + i] = (uint8_t)strtol(hex, NULL, 16);
    }

    uint8_t result2[32];
    double_sha256_hash(test_preimage, 176, result2);

    printf("Test preimage hash2: %s\n", test_hash2);
    printf("Double SHA256 result2: ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", result2[i]);
    }
    printf("\n");

    // Check if they're different
    bool different = memcmp(result, result2, 32) != 0;
    printf("Results are different: %s\n", different ? "PASS" : "FAIL");
}

int main() {
    printf("=== VERSE Player ID System ===\n");
    printf("Testing BIP 143 compliant double SHA256 player identification\n\n");

    test_double_sha256();
    test_double_sha256_with_preimage();
    test_player_id_system();
    test_character_save_with_id();

    printf("\nAll tests completed!\n");
    return 0;
}
