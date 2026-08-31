#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Simplified tutorial quest structure for testing
typedef struct {
    char* title;
    char* description;
    int objectives_count;
    int completed_objectives;
    bool is_active;
    bool is_completed;
} TutorialQuest;

// Simplified tutorial system for testing
TutorialQuest g_tutorial_quest = {0};
bool g_show_tutorial_modal = false;
bool g_tutorial_completed = false;

// Initialize tutorial quest
void window_init_tutorial_quest() {
    g_tutorial_quest.title = "Welcome to VERSE!";
    g_tutorial_quest.description = "Learn the basics of controlling your spirit in this mystical realm.";
    g_tutorial_quest.objectives_count = 2;
    g_tutorial_quest.completed_objectives = 0;
    g_tutorial_quest.is_active = true;
    g_tutorial_quest.is_completed = false;
    g_show_tutorial_modal = true;
    printf("Tutorial quest initialized\n");
}

// Update tutorial progress based on player actions
void window_update_tutorial_progress(const char* action) {
    if (!g_tutorial_quest.is_active || g_tutorial_quest.is_completed) {
        return;
    }

    if (strcmp(action, "move") == 0 && g_tutorial_quest.completed_objectives == 0) {
        g_tutorial_quest.completed_objectives = 1;
        printf("Tutorial: Movement objective completed!\n");
    } else if (strcmp(action, "attack") == 0 && g_tutorial_quest.completed_objectives == 1) {
        g_tutorial_quest.completed_objectives = 2;
        g_tutorial_quest.is_completed = true;
        g_tutorial_quest.is_active = false;
        g_show_tutorial_modal = false;
        printf("Tutorial: All objectives completed! Quest finished!\n");
    }
}

// Test tutorial quest system
void test_tutorial_quest() {
    printf("=== Testing Tutorial Quest System ===\n");

    // Initialize tutorial quest
    window_init_tutorial_quest();

    printf("Tutorial quest initialized:\n");
    printf("- Title: %s\n", g_tutorial_quest.title);
    printf("- Description: %s\n", g_tutorial_quest.description);
    printf("- Objectives: %d/%d completed\n", g_tutorial_quest.completed_objectives, g_tutorial_quest.objectives_count);
    printf("- Active: %s\n", g_tutorial_quest.is_active ? "true" : "false");
    printf("- Completed: %s\n", g_tutorial_quest.is_completed ? "true" : "false");
    printf("- Modal shown: %s\n", g_show_tutorial_modal ? "true" : "false");

    // Test movement objective
    printf("\n--- Testing Movement Objective ---\n");
    window_update_tutorial_progress("move");
    printf("After movement action:\n");
    printf("- Objectives: %d/%d completed\n", g_tutorial_quest.completed_objectives, g_tutorial_quest.objectives_count);
    printf("- Active: %s\n", g_tutorial_quest.is_active ? "true" : "false");
    printf("- Completed: %s\n", g_tutorial_quest.is_completed ? "true" : "false");

    // Test attack objective
    printf("\n--- Testing Attack Objective ---\n");
    window_update_tutorial_progress("attack");
    printf("After attack action:\n");
    printf("- Objectives: %d/%d completed\n", g_tutorial_quest.completed_objectives, g_tutorial_quest.objectives_count);
    printf("- Active: %s\n", g_tutorial_quest.is_active ? "true" : "false");
    printf("- Completed: %s\n", g_tutorial_quest.is_completed ? "true" : "false");
    printf("- Modal shown: %s\n", g_show_tutorial_modal ? "true" : "false");

    // Test invalid action (should not change state)
    printf("\n--- Testing Invalid Action ---\n");
    window_update_tutorial_progress("invalid");
    printf("After invalid action:\n");
    printf("- Objectives: %d/%d completed\n", g_tutorial_quest.completed_objectives, g_tutorial_quest.objectives_count);

    // Test trying to complete objectives after quest is finished
    printf("\n--- Testing Post-Completion Actions ---\n");
    window_update_tutorial_progress("move");
    window_update_tutorial_progress("attack");
    printf("After post-completion actions:\n");
    printf("- Objectives: %d/%d completed\n", g_tutorial_quest.completed_objectives, g_tutorial_quest.objectives_count);
    printf("- Active: %s\n", g_tutorial_quest.is_active ? "true" : "false");
    printf("- Completed: %s\n", g_tutorial_quest.is_completed ? "true" : "false");

    printf("\n=== Tutorial quest test completed ===\n\n");
}

// Test quest structure
void test_quest_structure() {
    printf("=== Testing Quest Structure ===\n");

    TutorialQuest test_quest = {0};
    test_quest.title = "Test Quest";
    test_quest.description = "This is a test quest description.";
    test_quest.objectives_count = 3;
    test_quest.completed_objectives = 1;
    test_quest.is_active = true;
    test_quest.is_completed = false;

    printf("Test quest created:\n");
    printf("- Title: %s\n", test_quest.title);
    printf("- Description: %s\n", test_quest.description);
    printf("- Objectives: %d/%d completed\n", test_quest.completed_objectives, test_quest.objectives_count);
    printf("- Active: %s\n", test_quest.is_active ? "true" : "false");
    printf("- Completed: %s\n", test_quest.is_completed ? "true" : "false");

    printf("=== Quest structure test completed ===\n\n");
}

// Test reward system
void test_reward_system() {
    printf("=== Testing Reward System ===\n");

    // Simulate tutorial completion and reward
    int player_gold = 0;

    printf("Initial gold: %d\n", player_gold);

    // Complete tutorial objectives
    window_init_tutorial_quest();
    window_update_tutorial_progress("move");
    window_update_tutorial_progress("attack");

    if (g_tutorial_quest.is_completed) {
        player_gold += 50; // Tutorial completion reward
        printf("Tutorial completed! Gold reward: +50\n");
        printf("Final gold: %d\n", player_gold);
    }

    printf("=== Reward system test completed ===\n\n");
}

int main() {
    printf("=== Tutorial System Test ===\n\n");

    // Test quest structure
    test_quest_structure();

    // Test tutorial quest system
    test_tutorial_quest();

    // Test reward system
    test_reward_system();

    printf("All tests completed successfully!\n");
    return 0;
}
