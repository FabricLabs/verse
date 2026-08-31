#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "window.h"
#include "world.h"

// Simple demonstration program for the window system
int main() {
    printf("=== VERSE Window Demo ===\n");
    printf("Initializing window system...\n");

    // Initialize window
    if (!window_init(WINDOW_TITLE, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT)) {
        printf("Failed to initialize window system!\n");
        return 1;
    }

    printf("Window system initialized successfully!\n");
    printf("Creating test world...\n");

    // Create a simple test world
    World* test_world = world_create(64, 64, 16);
    if (!test_world) {
        printf("Failed to create test world!\n");
        window_cleanup();
        return 1;
    }

    // Generate some test terrain
    world_generate(test_world, "demo_seed");

    // Test variables
    int player_x = 32;
    int player_y = 32;
    int player_z = 8;
    const char* player_name = "DemoPlayer";

    printf("Rendering demo screens...\n");
    printf("Window should be visible now.\n");

    // Demo sequence
    for (int i = 0; i < 3; i++) {
        printf("Showing screen %d...\n", i);

        switch (i) {
            case 0:
                window_render_main_menu();
                break;
            case 1:
                window_render_game_world(test_world, player_x, player_y, player_z, player_name);
                break;
            case 2:
                window_render_battle_interface(600, "Enemy: Demo Monster\nHP: 100/100\nLevel: 5");
                break;
        }

        // Wait 3 seconds
        sleep(3);
    }

    printf("Demo completed!\n");
    printf("Cleaning up...\n");

    world_destroy(test_world);
    window_cleanup();

    printf("Window demo completed successfully!\n");
    printf("The window system is working properly.\n");

    return 0;
}
