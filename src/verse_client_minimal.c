/*
 * verse_client_minimal.c - Minimal modular client demonstration
 *
 * This demonstrates the modular architecture without complex dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

// Simple modular structure demonstration
typedef struct {
    bool initialized;
    char player_name[64];
    int current_screen;
    bool game_started;
} MinimalGameState;

// Client state module
static MinimalGameState g_game_state = {0};

bool client_state_init(void) {
    if (g_game_state.initialized) {
        return true;
    }

    g_game_state.initialized = true;
    g_game_state.current_screen = 0; // Main menu
    g_game_state.game_started = false;
    strcpy(g_game_state.player_name, "Player");

    printf("✓ Client state initialized\n");
    return true;
}

void client_state_shutdown(void) {
    g_game_state.initialized = false;
    printf("✓ Client state shut down\n");
}

MinimalGameState* client_state_get_game(void) {
    return g_game_state.initialized ? &g_game_state : NULL;
}

bool client_state_should_exit(void) {
    return false; // Simple version doesn't handle exit
}

// Client input module
void client_input_init(void) {
    printf("✓ Input system initialized\n");
}

void handle_key_press(int key) {
    MinimalGameState* state = client_state_get_game();
    if (!state) return;

    printf("Key pressed: %d\n", key);

    // Simple key handling
    switch (key) {
    case 'n': // New game
        printf("Starting new game...\n");
        state->game_started = true;
        state->current_screen = 1; // Game screen
        break;
    case 'q': // Quit
        printf("Quitting...\n");
        exit(0);
        break;
    default:
        printf("Unknown key: %c\n", key);
        break;
    }
}

// Client render module
void client_render_init(void) {
    printf("✓ Render system initialized\n");
}

void render_game(void) {
    MinimalGameState* state = client_state_get_game();
    if (!state) return;

    // Simple rendering
    switch (state->current_screen) {
    case 0: // Main menu
        printf("\n=== VERSE - Main Menu ===\n");
        printf("Press 'n' for New Game\n");
        printf("Press 'q' to Quit\n");
        break;
    case 1: // Game screen
        printf("\n=== VERSE - Game World ===\n");
        printf("Player: %s\n", state->player_name);
        printf("Game started: %s\n", state->game_started ? "Yes" : "No");
        printf("Press 'q' to Quit\n");
        break;
    default:
        printf("\n=== Unknown Screen ===\n");
        break;
    }
}

// Client game loop module
void client_game_loop_init(void) {
    printf("✓ Game loop initialized\n");
}

void game_loop(void) {
    MinimalGameState* state = client_state_get_game();
    if (!state) {
        printf("No game state available\n");
        return;
    }

    printf("\n🎮 Starting game loop...\n");
    printf("This is a minimal demonstration of the modular architecture.\n");
    printf("In the full version, this would handle SDL events, rendering, etc.\n\n");

    // Simulate a few game loop iterations
    for (int i = 0; i < 5; i++) {
        printf("--- Frame %d ---\n", i + 1);
        render_game();

        // Simulate some input
        if (i == 1) {
            printf("Simulating 'n' key press...\n");
            handle_key_press('n');
        }

        printf("\n");
    }

    printf("Game loop completed (minimal version)\n");
}

// Client init module
bool client_init(int argc, char* argv[]) {
    printf("🔧 Initializing modular verse client...\n");

    // Initialize subsystems
    if (!client_state_init()) {
        printf("❌ Failed to initialize client state\n");
        return false;
    }

    client_input_init();
    client_render_init();
    client_game_loop_init();

    printf("✅ All systems initialized successfully\n");
    return true;
}

void client_shutdown(void) {
    printf("🔧 Shutting down modular verse client...\n");
    client_state_shutdown();
    printf("✅ Shutdown complete\n");
}

// Main entry point
int main(int argc, char* argv[]) {
    printf("🚀 Verse Modular Client - Minimal Demo\n");
    printf("=====================================\n\n");

    // Initialize
    if (!client_init(argc, argv)) {
        printf("❌ Initialization failed\n");
        return 1;
    }

    // Run the game loop
    game_loop();

    // Cleanup
    client_shutdown();

    printf("\n🎉 Modular client demo completed successfully!\n");
    printf("\nThis demonstrates:\n");
    printf("✓ Modular architecture with separate concerns\n");
    printf("✓ Clean initialization and shutdown\n");
    printf("✓ State management\n");
    printf("✓ Input handling\n");
    printf("✓ Rendering pipeline\n");
    printf("✓ Game loop structure\n");

    return 0;
}
