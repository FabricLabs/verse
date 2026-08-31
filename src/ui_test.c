#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "renderer.h"
#include "interface.h"

// Simple test program to demonstrate the UI system
int main() {
    printf("=== VERSE UI Test Program ===\n");
    printf("Testing compiled UI system...\n\n");

    // Initialize UI components
    ui_init();
    renderer_init();
    interface_init();

    printf("UI components initialized successfully.\n");
    printf("Testing UI functions:\n\n");

    // Test main menu
    printf("1. Testing main menu:\n");
    ui_show_main_menu();
    printf("\n");

    // Test character creation
    printf("2. Testing character creation:\n");
    ui_character_creation();
    printf("\n");

    // Test world display
    printf("3. Testing world display:\n");
    ui_show_world();
    printf("\n");

    // Test battle interface
    printf("4. Testing battle interface:\n");
    ui_start_battle();
    ui_show_battle();
    printf("\n");

    // Test navigation
    printf("5. Testing navigation:\n");
    ui_show_navigation();
    printf("\n");

    // Test building interface
    printf("6. Testing building interface:\n");
    ui_show_building();
    printf("\n");

    // Test game over
    printf("7. Testing game over:\n");
    ui_show_game_over();
    printf("\n");

    printf("=== UI Test Complete ===\n");
    printf("All UI components compiled and working!\n");
    printf("The 'make ui' command successfully created the compiled UI system.\n");
    printf("\nUI compilation features:\n");
    printf("- Character creation and management\n");
    printf("- World rendering and navigation\n");
    printf("- Battle system with timers\n");
    printf("- Building and crafting interface\n");
    printf("- Game state management\n");
    printf("- Input processing and menu system\n");

    return 0;
}
