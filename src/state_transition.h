#ifndef STATE_TRANSITION_H
#define STATE_TRANSITION_H

#include "world.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Performance monitoring constants
#define TARGET_FPS 180
#define FRAME_TIME_TARGET (1000000.0 / TARGET_FPS)  // microseconds
#define WARNING_FPS_THRESHOLD 150  // Warn if below this FPS
#define FRAME_TIME_WARNING (1000000.0 / WARNING_FPS_THRESHOLD)

// Vector clock constants
#define VECTOR_CLOCK_TICKS_PER_SECOND 60
#define VECTOR_CLOCK_INTERVAL (1000000 / VECTOR_CLOCK_TICKS_PER_SECOND)  // microseconds

// JSONPatch operation types
typedef enum {
    JSONPATCH_ADD,
    JSONPATCH_REMOVE,
    JSONPATCH_REPLACE,
    JSONPATCH_MOVE,
    JSONPATCH_COPY,
    JSONPATCH_TEST
} JSONPatchOp;

// JSONPatch operation structure
typedef struct {
    JSONPatchOp op;
    char* path;
    char* value;
    char* from;  // For move/copy operations
} JSONPatchOperation;

// State transition structure
typedef struct {
    uint64_t vector_clock;
    uint64_t timestamp;
    char* world_seed;
    JSONPatchOperation* operations;
    int operation_count;
    int operation_capacity;
} StateTransition;

// Performance monitoring structure
typedef struct {
    uint64_t frame_count;
    uint64_t total_frame_time;
    uint64_t last_frame_time;
    uint64_t start_time;
    double average_fps;
    double current_fps;
    bool performance_warning_issued;
} PerformanceMonitor;

// Vector clock structure
typedef struct {
    uint64_t current_tick;
    uint64_t last_tick_time;
    uint64_t frame_count;
    bool tick_ready;
} VectorClock;

// State transition system
typedef struct {
    StateTransition* transitions;
    int transition_count;
    int transition_capacity;
    VectorClock vector_clock;
    PerformanceMonitor performance;
    char* genesis_state;
    bool initialized;
} StateTransitionSystem;

// Function declarations

// State transition system management
StateTransitionSystem* state_transition_system_create();
void state_transition_system_destroy(StateTransitionSystem* system);
bool state_transition_system_initialize(StateTransitionSystem* system, const char* world_seed);

// Vector clock management
void vector_clock_init(VectorClock* clock);
void vector_clock_update(VectorClock* clock, uint64_t current_time);
bool vector_clock_is_tick_ready(VectorClock* clock);
uint64_t vector_clock_get_current_tick(VectorClock* clock);

// Performance monitoring
void performance_monitor_init(PerformanceMonitor* monitor);
void performance_monitor_start_frame(PerformanceMonitor* monitor);
void performance_monitor_end_frame(PerformanceMonitor* monitor);
double performance_monitor_get_average_fps(PerformanceMonitor* monitor);
double performance_monitor_get_current_fps(PerformanceMonitor* monitor);
bool performance_monitor_check_warning(PerformanceMonitor* monitor);

// JSONPatch operations
JSONPatchOperation* jsonpatch_operation_create(JSONPatchOp op, const char* path, const char* value);
void jsonpatch_operation_destroy(JSONPatchOperation* operation);
char* jsonpatch_operation_to_json(JSONPatchOperation* operation);
char* jsonpatch_operations_to_json(JSONPatchOperation* operations, int count);

// State transition management
StateTransition* state_transition_create(uint64_t vector_clock, const char* world_seed);
void state_transition_destroy(StateTransition* transition);
bool state_transition_add_operation(StateTransition* transition, JSONPatchOp op,
                                   const char* path, const char* value, const char* from);
char* state_transition_to_json(StateTransition* transition);

// World state tracking
bool state_transition_system_record_world_changes(StateTransitionSystem* system,
                                                World* world, uint64_t current_time);
bool state_transition_system_record_voxel_change(StateTransitionSystem* system,
                                               uint32_t x, uint32_t y, uint32_t z,
                                               VoxelType old_type, VoxelType new_type);
bool state_transition_system_record_gravity_change(StateTransitionSystem* system,
                                                 float old_gravity, float new_gravity);

// Genesis state management
char* state_transition_system_create_genesis_state(World* world);
bool state_transition_system_set_genesis_state(StateTransitionSystem* system, const char* genesis_state);
char* state_transition_system_get_genesis_state(StateTransitionSystem* system);

// Save/load with state transitions
bool state_transition_system_save_to_file(StateTransitionSystem* system, const char* filename);
StateTransitionSystem* state_transition_system_load_from_file(const char* filename);

// Memory and performance analysis
void analyze_world_memory_usage(World* world);
void analyze_performance_expectations();
void estimate_cpu_usage_for_world_size(uint32_t width, uint32_t height, uint32_t depth);

// Utility functions
uint64_t get_current_time_microseconds();
char* create_json_path(const char* format, ...);
char* voxel_type_to_string(VoxelType type);
VoxelType string_to_voxel_type(const char* str);

#endif // STATE_TRANSITION_H
