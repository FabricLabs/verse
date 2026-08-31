#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "window.h"

// Define the missing global variable
int g_show_exit_prompt = 0;

int main() {
    printf("=== Exit Prompt Test ===\n");

    // Initialize window system
    if (!window_init("Exit Prompt Test", 256, 240)) {
        printf("✗ Failed to initialize window system\n");
        return 1;
    }

    printf("✓ Window system initialized\n");
    printf("✓ Window created in fullscreen mode with optimal scaling\n");

    // Set up button callback
    window_set_button_callback(NULL); // No callback needed for this test

    printf("\nInstructions:\n");
    printf("1. The window should open in fullscreen mode\n");
    printf("2. The exit confirmation dialog should appear centered on screen\n");
    printf("3. Both 'Cancel' and 'Exit' buttons should be visible and clickable\n");
    printf("4. The dialog should be properly positioned and not run off screen\n");
    printf("5. The test will run for 5 seconds then exit\n");
    printf("\nPress Enter to continue...\n");
    getchar();

    // Main loop
    int running = 1;
    int frame_count = 0;
    while (running && frame_count < 300) { // Run for 5 seconds
        frame_count++;

        // Clear the screen
        window_clear();

        // Render some text to show the window is working
        window_render_text("Exit Prompt Test", 100, 100, (SDL_Color){255, 255, 0, 255});
        window_render_text("The exit dialog should appear centered", 50, 150, (SDL_Color){255, 255, 255, 255});

        // Always render exit prompt for testing
        window_render_exit_prompt();

        // Present the frame
        window_present();

        // Small delay
        usleep(16667); // ~60 FPS
    }

    // Cleanup
    window_cleanup();
    printf("✓ Exit prompt test completed\n");

    return 0;
}
