/*
 * verse_client_modular.c - Main entry point for the modular verse client
 *
 * This is a simplified version that uses all the modular components.
 */

#include <stdio.h>
#include <stdlib.h>
#include "client_init.h"
#include "client_game_loop.h"

int main(int argc, char* argv[]) {
    // Initialize all client systems
    if (!client_init(argc, argv)) {
        fprintf(stderr, "Failed to initialize client\n");
        return 1;
    }

    // Run the main game loop
    game_loop();

    // Cleanup
    client_shutdown();

    printf("Verse client exited cleanly\n");
    return 0;
}
