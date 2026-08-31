#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "state_transition.h"
#include "world.h"

int main() {
    printf("=== VERSE State Transition System Test ===\n\n");

    // Analyze performance expectations
    analyze_performance_expectations();
    printf("\n");

    // Test different world sizes
    printf("=== World Size Analysis ===\n");
    uint32_t world_sizes[][3] = {
        {32, 32, 32},   // Small world
        {64, 64, 16},   // Medium world (current default)
        {128, 128, 32}, // Large world
        {256, 256, 64}  // Very large world
    };

    const char* size_names[] = {"Small", "Medium", "Large", "Very Large"};

    for (int i = 0; i < 4; i++) {
        printf("\n%s World (%ux%ux%u):\n",
               size_names[i], world_sizes[i][0], world_sizes[i][1], world_sizes[i][2]);

        // Create test world
        World* world = world_create(world_sizes[i][0], world_sizes[i][1], world_sizes[i][2]);
        if (world) {
            analyze_world_memory_usage(world);
            estimate_cpu_usage_for_world_size(world_sizes[i][0], world_sizes[i][1], world_sizes[i][2]);
            world_destroy(world);
        }
    }

    printf("\n=== State Transition System Test ===\n");

    // Create state transition system
    StateTransitionSystem* system = state_transition_system_create();
    if (!system) {
        printf("Failed to create state transition system!\n");
        return 1;
    }

    // Initialize with a test world seed
    if (!state_transition_system_initialize(system, "test_world_seed")) {
        printf("Failed to initialize state transition system!\n");
        state_transition_system_destroy(system);
        return 1;
    }

    // Create a test world
    World* test_world = world_create(64, 64, 16);
    if (!test_world) {
        printf("Failed to create test world!\n");
        state_transition_system_destroy(system);
        return 1;
    }

    // Set genesis state
    char* genesis_state = state_transition_system_create_genesis_state(test_world);
    if (genesis_state) {
        state_transition_system_set_genesis_state(system, genesis_state);
        printf("Genesis state created:\n%s\n\n", genesis_state);
        free(genesis_state);
    }

    // Simulate game loop with state transitions
    printf("Simulating game loop with state transitions...\n");

    uint64_t start_time = get_current_time_microseconds();
    uint64_t frame_count = 0;

    for (int frame = 0; frame < 300; frame++) { // 5 seconds at 60 FPS
        uint64_t current_time = get_current_time_microseconds();

        // Start performance monitoring
        performance_monitor_start_frame(&system->performance);

        // Simulate some world changes
        if (frame % 60 == 0) { // Every second
            // Change gravity
            float old_gravity = test_world->gravity;
            test_world->gravity = 9.81f + (frame / 60) * 0.1f;
            state_transition_system_record_gravity_change(system, old_gravity, test_world->gravity);

            // Change some voxels
            for (int i = 0; i < 10; i++) {
                uint32_t x = (frame + i) % test_world->width;
                uint32_t y = (frame + i) % test_world->height;
                uint32_t z = (frame + i) % test_world->depth;

                Voxel* voxel = world_get_voxel(test_world, x, y, z);
                if (voxel) {
                    VoxelType old_type = voxel->type;
                    voxel->type = (VoxelType)((frame + i) % VOXEL_COUNT);
                    state_transition_system_record_voxel_change(system, x, y, z, old_type, voxel->type);
                }
            }
        }

        // Record world changes (this will create state transitions at vector clock ticks)
        state_transition_system_record_world_changes(system, test_world, current_time);

        // End performance monitoring
        performance_monitor_end_frame(&system->performance);

        // Check for performance warnings
        performance_monitor_check_warning(&system->performance);

        frame_count++;

        // Simulate frame time (target 60 FPS)
        usleep(16667); // ~60 FPS
    }

    uint64_t end_time = get_current_time_microseconds();
    double total_time = (end_time - start_time) / 1000000.0;
    double actual_fps = frame_count / total_time;

    printf("\n=== Performance Results ===\n");
    printf("Total frames: %llu\n", frame_count);
    printf("Total time: %.2f seconds\n", total_time);
    printf("Actual FPS: %.1f\n", actual_fps);
    printf("Average FPS: %.1f\n", performance_monitor_get_average_fps(&system->performance));
    printf("Current FPS: %.1f\n", performance_monitor_get_current_fps(&system->performance));
    printf("Vector clock ticks: %llu\n", vector_clock_get_current_tick(&system->vector_clock));
    printf("State transitions recorded: %d\n", system->transition_count);

    // Display some state transitions
    printf("\n=== Sample State Transitions ===\n");
    for (int i = 0; i < system->transition_count && i < 3; i++) {
        printf("Transition %d:\n", i + 1);
        char* transition_json = state_transition_to_json(&system->transitions[i]);
        if (transition_json) {
            printf("%s\n", transition_json);
            free(transition_json);
        }
        printf("\n");
    }

    // Save state transitions to file
    printf("Saving state transitions to file...\n");
    if (state_transition_system_save_to_file(system, "test_state_transitions.log")) {
        printf("State transitions saved successfully!\n");
    } else {
        printf("Failed to save state transitions!\n");
    }

    // Cleanup
    world_destroy(test_world);
    state_transition_system_destroy(system);

    printf("\n🎯 State transition system test completed!\n");
    return 0;
}
