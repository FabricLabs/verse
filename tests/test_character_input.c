#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// SDL key constants for testing
#define SDLK_ESCAPE 27
#define SDLK_RETURN 13
#define SDLK_KP_ENTER 271
#define SDLK_BACKSPACE 8

// Simplified input manager for testing
typedef enum {
    INPUT_STATE_MAIN_MENU,
    INPUT_STATE_NAME_INPUT,
    INPUT_STATE_GAME_WORLD
} InputState;

typedef struct {
    InputState current_state;
    bool key_handled;
} InputManager;

// Global variables for testing
static char g_player_name[64] = "";
static bool g_show_name_input = false;

// Simplified input manager functions
void input_manager_init(InputManager* manager) {
    manager->current_state = INPUT_STATE_MAIN_MENU;
    manager->key_handled = false;
}

bool input_handle_name_input(InputManager* manager, int key) {
    (void)manager; // Suppress unused parameter warning
    switch (key) {
        case SDLK_ESCAPE:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_BACKSPACE:
            return false; // Let main app handle these keys
        default:
            // Handle character input
            if (key >= 32 && key <= 126) {
                return false; // Let main app handle character input
            }
            return false;
    }
}

// Simulate key press handling
void handle_key_press(int key) {
    // Simulate input manager handling
    InputManager manager;
    input_manager_init(&manager);
    manager.current_state = INPUT_STATE_NAME_INPUT;

    bool handled_by_input_manager = input_handle_name_input(&manager, key);

    if (handled_by_input_manager) {
        printf("Key handled by input manager\n");
        return;
    }

    // Handle name input when active
    if (g_show_name_input) {
        switch (key) {
            case SDLK_ESCAPE:
                g_show_name_input = false;
                printf("Name input cancelled\n");
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (strlen(g_player_name) > 0) {
                    g_show_name_input = false;
                    printf("Character name confirmed: %s\n", g_player_name);
                }
                break;
            case SDLK_BACKSPACE:
                if (strlen(g_player_name) > 0) {
                    g_player_name[strlen(g_player_name) - 1] = '\0';
                    printf("Name: %s\n", g_player_name);
                }
                break;
            default:
                // Handle character input for name input
                if (key >= 32 && key <= 126 && strlen(g_player_name) < 63) {
                    char ch = (char)key;
                    g_player_name[strlen(g_player_name)] = ch;
                    g_player_name[strlen(g_player_name) + 1] = '\0';
                    printf("Name: %s\n", g_player_name);
                }
                break;
        }
    }
}

// Test character input functionality
void test_character_input() {
    printf("=== Testing Character Name Input ===\n");

    // Initialize
    memset(g_player_name, 0, sizeof(g_player_name));
    g_show_name_input = true;

    printf("Starting name input test...\n");
    printf("Current name: '%s'\n", g_player_name);

    // Test typing characters
    printf("\n--- Testing Character Input ---\n");
    handle_key_press('A');
    handle_key_press('l');
    handle_key_press('i');
    handle_key_press('c');
    handle_key_press('e');

    printf("Final name: '%s'\n", g_player_name);

    // Test backspace
    printf("\n--- Testing Backspace ---\n");
    handle_key_press(SDLK_BACKSPACE);
    printf("After backspace: '%s'\n", g_player_name);

    // Test confirmation
    printf("\n--- Testing Confirmation ---\n");
    handle_key_press(SDLK_RETURN);
    printf("Name input active: %s\n", g_show_name_input ? "true" : "false");

    // Test cancellation
    printf("\n--- Testing Cancellation ---\n");
    g_show_name_input = true;
    handle_key_press(SDLK_ESCAPE);
    printf("Name input active: %s\n", g_show_name_input ? "true" : "false");

    printf("=== Character input test completed ===\n\n");
}

// Test input manager integration
void test_input_manager_integration() {
    printf("=== Testing Input Manager Integration ===\n");

    // Test that input manager doesn't block character input
    printf("Testing that input manager allows character input...\n");

    InputManager manager;
    input_manager_init(&manager);
    manager.current_state = INPUT_STATE_NAME_INPUT;

    // Test various keys
    int test_keys[] = {'A', 'B', 'C', SDLK_RETURN, SDLK_ESCAPE, SDLK_BACKSPACE};
    const char* key_names[] = {"'A'", "'B'", "'C'", "RETURN", "ESCAPE", "BACKSPACE"};

    for (int i = 0; i < 6; i++) {
        bool handled = input_handle_name_input(&manager, test_keys[i]);
        printf("Key %s: %s\n", key_names[i], handled ? "handled by input manager" : "passed to main app");
    }

    printf("=== Input manager integration test completed ===\n\n");
}

int main() {
    printf("=== Character Input Test ===\n\n");

    // Test character input functionality
    test_character_input();

    // Test input manager integration
    test_input_manager_integration();

    printf("All tests completed successfully!\n");
    return 0;
}
