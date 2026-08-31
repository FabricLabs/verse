#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "window.h"
#include "world.h"

// Simple test program to demonstrate the window system
int main() {
    printf("=== VERSE Window Test ===\n");
    printf("Initializing window system...\n");

    // Initialize window
    if (!window_init(WINDOW_TITLE, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT)) {
        printf("Failed to initialize window system!\n");
        return 1;
    }

    printf("Window system initialized successfully!\n");
    printf("Testing window rendering...\n");

    // Create a simple test world
    World* test_world = world_create(64, 64, 16);
    if (!test_world) {
        printf("Failed to create test world!\n");
        window_cleanup();
        return 1;
    }

    // Generate some test terrain
    world_generate(test_world, "test_seed");

    // Test variables
    int player_x = 32;
    int player_y = 32;
    int player_z = 8;
    const char* player_name = "TestPlayer";
    int running = 1;
    int screen = 0; // 0 = main menu, 1 = game world, 2 = battle

    printf("Starting window test loop...\n");
    printf("Press ESC to exit, 1-3 to switch screens\n");

    // Main window loop
    while (running) {
        // Handle events
        running = window_handle_events();

        // Render appropriate screen
        switch (screen) {
            case 0:
                window_render_main_menu();
                break;
            case 1:
                window_render_game_world(test_world, player_x, player_y, player_z, player_name);
                break;
            case 2:
                window_render_battle_interface(600, "Enemy: Test Monster\nHP: 100/100\nLevel: 5");
                break;
        }

        // Small delay to prevent excessive CPU usage
        usleep(16667); // ~60 FPS
    }

    printf("Cleaning up...\n");
    world_destroy(test_world);
    window_cleanup();

    printf("Window test completed successfully!\n");
    return 0;
}
