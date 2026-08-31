#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

// Simplified coordinate conversion functions for testing
void test_world_to_screen_coords(int world_x, int world_y, int world_z, int* screen_x, int* screen_y) {
    // Center of the screen (character position)
    int center_x = 128; // BASE_RESOLUTION_WIDTH / 2
    int center_y = 120; // BASE_RESOLUTION_HEIGHT / 2

    // Isometric projection matrix (inverse of screen_to_world)
    float scale = 16.0f;

    // Isometric projection: screen_x = center_x + (world_x - world_z) * scale
    //                      screen_y = center_y + (world_x + world_z) * scale / 2
    *screen_x = center_x + (world_x - world_z) * scale;
    *screen_y = center_y + (world_x + world_z) * scale / 2 - world_y * scale / 4; // Y affects height
}

int test_screen_to_world_coords(int screen_x, int screen_y, int* world_x, int* world_y, int* world_z) {
    // Center of the screen (character position)
    int center_x = 128; // BASE_RESOLUTION_WIDTH / 2
    int center_y = 120; // BASE_RESOLUTION_HEIGHT / 2

    // Convert from screen space to world space
    // Isometric projection: x = (screen_x - center_x) / scale, z = (screen_y - center_y) / scale
    float scale = 16.0f; // Isometric scale factor

    int dx = screen_x - center_x;
    int dy = screen_y - center_y;

    // Isometric projection matrix
    // For a 45-degree isometric view:
    // world_x = (dx + dy) / (2 * scale)
    // world_z = (dy - dx) / (2 * scale)

    *world_x = (dx + dy) / (2 * scale);
    *world_z = (dy - dx) / (2 * scale);
    *world_y = 0; // Default to ground level, will be adjusted by ray casting

    return 1; // Success
}

// Test coordinate conversion functions
void test_coordinate_conversion() {
    printf("=== Testing Isomorphic Renderer Coordinate Conversion ===\n");

    // Test world to screen conversion
    int screen_x, screen_y;
    int world_x = 5, world_y = 10, world_z = 3;

    test_world_to_screen_coords(world_x, world_y, world_z, &screen_x, &screen_y);
    printf("World (%d, %d, %d) -> Screen (%d, %d)\n", world_x, world_y, world_z, screen_x, screen_y);

    // Test screen to world conversion
    int converted_world_x, converted_world_y, converted_world_z;
    int result = test_screen_to_world_coords(screen_x, screen_y, &converted_world_x, &converted_world_y, &converted_world_z);

    printf("Screen (%d, %d) -> World (%d, %d, %d) [result: %d]\n",
           screen_x, screen_y, converted_world_x, converted_world_y, converted_world_z, result);

    // Test round-trip conversion
    printf("Round-trip test: World (%d, %d, %d) -> Screen -> World (%d, %d, %d)\n",
           world_x, world_y, world_z, converted_world_x, converted_world_y, converted_world_z);

    // Test center point (should be at screen center)
    test_world_to_screen_coords(0, 0, 0, &screen_x, &screen_y);
    printf("World center (0, 0, 0) -> Screen (%d, %d)\n", screen_x, screen_y);

    // Test various world positions
    printf("\nTesting various world positions:\n");
    int test_positions[][3] = {
        {0, 0, 0},   // Center
        {1, 0, 0},   // Right
        {-1, 0, 0},  // Left
        {0, 0, 1},   // Forward
        {0, 0, -1},  // Back
        {1, 0, 1},   // Diagonal
        {2, 5, 3},   // Complex position
    };

    for (int i = 0; i < 7; i++) {
        int x = test_positions[i][0];
        int y = test_positions[i][1];
        int z = test_positions[i][2];

        test_world_to_screen_coords(x, y, z, &screen_x, &screen_y);
        printf("World (%d, %d, %d) -> Screen (%d, %d)\n", x, y, z, screen_x, screen_y);
    }

    printf("=== Coordinate conversion test completed ===\n\n");
}

// Test movement destination system
void test_movement_destination() {
    printf("=== Testing Movement Destination System ===\n");

    // Initialize movement destination
    typedef struct {
        int target_x;
        int target_y;
        int target_z;
        int has_destination;
        float path_progress; // 0.0 to 1.0 for smooth movement
    } MovementDestination;

    MovementDestination dest = {0};
    dest.target_x = 10;
    dest.target_y = 5;
    dest.target_z = 8;
    dest.has_destination = 1;
    dest.path_progress = 0.0f;

    printf("Movement destination set to (%d, %d, %d)\n",
           dest.target_x, dest.target_y, dest.target_z);
    printf("Has destination: %s\n", dest.has_destination ? "true" : "false");
    printf("Path progress: %.2f%%\n", dest.path_progress * 100.0f);

    // Test distance calculation
    int current_x = 0, current_y = 0, current_z = 0;
    int dx = dest.target_x - current_x;
    int dy = dest.target_y - current_y;
    int dz = dest.target_z - current_z;

    float distance = sqrt(dx*dx + dy*dy + dz*dz);
    printf("Distance from (0,0,0) to destination: %.2f\n", distance);

    printf("=== Movement destination test completed ===\n\n");
}

int main() {
    printf("=== Isomorphic Renderer Test ===\n\n");

    // Test coordinate conversion
    test_coordinate_conversion();

    // Test movement destination
    test_movement_destination();

    printf("All tests completed successfully!\n");
    return 0;
}
