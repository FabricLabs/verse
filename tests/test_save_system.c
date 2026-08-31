#include "character.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    printf("=== Character Save System Test ===\n");

    // Test 1: Log initial save files
    printf("\n1. Initial save files:\n");
    character_log_save_files();

    // Test 2: Create a test save
    printf("\n2. Creating test save...\n");
    const char* test_character = "TestPlayer";
    const char* test_world_seed = "test_seed_12345";

    bool save_success = character_save_default(test_character, 10, 5, 15,
                                             8, 6, 7, 5, 9, 4,  // stats
                                             100, 50, test_world_seed, 0, 0, 0u, NULL);

    if (save_success) {
        printf("✓ Test save created successfully\n");
    } else {
        printf("✗ Failed to create test save\n");
        return 1;
    }

    // Test 3: Log save files after creation
    printf("\n3. Save files after creation:\n");
    character_log_save_files();

    // Test 4: Load the save
    printf("\n4. Loading test save...\n");
    CharacterSave loaded_save;
    bool load_success = character_load_default(&loaded_save);

    if (load_success) {
        printf("✓ Test save loaded successfully\n");
        printf("  Character: %s\n", loaded_save.name);
        printf("  Position: (%d, %d, %d)\n", loaded_save.x, loaded_save.y, loaded_save.z);
        printf("  Stats: STR=%d, DEX=%d, INT=%d, WIS=%d, CON=%d, LUK=%d\n",
               loaded_save.strength, loaded_save.dexterity, loaded_save.intelligence,
               loaded_save.wisdom, loaded_save.constitution, loaded_save.luck);
        printf("  Experience: %d\n", loaded_save.experience_points);
        printf("  Gold: %d\n", loaded_save.gold);
        printf("  World Seed: %s\n", loaded_save.world_seed);
        printf("  Save Timestamp: %s\n", loaded_save.save_timestamp);
    } else {
        printf("✗ Failed to load test save\n");
        return 1;
    }

    // Test 5: Create another save to test timestamped backups
    printf("\n5. Creating another test save...\n");
    save_success = character_save_default(test_character, 20, 10, 25,
                                         9, 7, 8, 6, 10, 5,  // updated stats
                                         150, 75, test_world_seed, 0, 0, 0u, NULL);

    if (save_success) {
        printf("✓ Second test save created successfully\n");
    } else {
        printf("✗ Failed to create second test save\n");
        return 1;
    }

    // Test 6: Log save files after second creation
    printf("\n6. Save files after second creation:\n");
    character_log_save_files();

    // Test 7: Check if default save exists
    printf("\n7. Checking if default save exists...\n");
    if (character_default_save_exists()) {
        printf("✓ Default save exists\n");
    } else {
        printf("✗ Default save does not exist\n");
    }

    printf("\n=== Save System Test Completed Successfully ===\n");
    return 0;
}
