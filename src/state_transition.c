#include "state_transition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <math.h>

// Utility functions
uint64_t get_current_time_microseconds()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

char *create_json_path(const char *format, ...)
{
  va_list args;
  va_start(args, format);

  // First pass: calculate required size
  va_list args_copy;
  va_copy(args_copy, args);
  int size = vsnprintf(NULL, 0, format, args_copy);
  va_end(args_copy);

  if (size < 0)
  {
    va_end(args);
    return NULL;
  }

  // Allocate and format
  char *path = malloc(size + 1);
  if (!path)
  {
    va_end(args);
    return NULL;
  }

  vsnprintf(path, size + 1, format, args);
  va_end(args);
  return path;
}

char *voxel_type_to_string(VoxelType type)
{
  switch (type)
  {
  case VOXEL_AIR:
    return "air";
  case VOXEL_SOIL:
    return "dirt";
  case VOXEL_GRASS:
    return "grass";
  case VOXEL_STONE:
    return "stone";
  case VOXEL_WATER:
    return "water";
  case VOXEL_WOOD:
    return "wood";
  case VOXEL_LEAVES:
    return "leaves";
  case VOXEL_SAND:
    return "sand";
  default:
    return "unknown";
  }
}

VoxelType string_to_voxel_type(const char *str)
{
  if (strcmp(str, "air") == 0)
    return VOXEL_AIR;
  if (strcmp(str, "dirt") == 0)
    return VOXEL_SOIL;
  if (strcmp(str, "grass") == 0)
    return VOXEL_GRASS;
  if (strcmp(str, "stone") == 0)
    return VOXEL_STONE;
  if (strcmp(str, "water") == 0)
    return VOXEL_WATER;
  if (strcmp(str, "wood") == 0)
    return VOXEL_WOOD;
  if (strcmp(str, "leaves") == 0)
    return VOXEL_LEAVES;
  if (strcmp(str, "sand") == 0)
    return VOXEL_SAND;
  return VOXEL_AIR;
}

// Vector clock management
void vector_clock_init(VectorClock *clock)
{
  clock->current_tick = 0;
  clock->last_tick_time = get_current_time_microseconds();
  clock->frame_count = 0;
  clock->tick_ready = false;
}

void vector_clock_update(VectorClock *clock, uint64_t current_time)
{
  uint64_t time_since_last_tick = current_time - clock->last_tick_time;

  if (time_since_last_tick >= VECTOR_CLOCK_INTERVAL)
  {
    clock->current_tick++;
    clock->last_tick_time = current_time;
    clock->tick_ready = true;
  }
  else
  {
    clock->tick_ready = false;
  }

  clock->frame_count++;
}

bool vector_clock_is_tick_ready(VectorClock *clock)
{
  return clock->tick_ready;
}

uint64_t vector_clock_get_current_tick(VectorClock *clock)
{
  return clock->current_tick;
}

// Performance monitoring
void performance_monitor_init(PerformanceMonitor *monitor)
{
  monitor->frame_count = 0;
  monitor->total_frame_time = 0;
  monitor->last_frame_time = 0;
  monitor->start_time = get_current_time_microseconds();
  monitor->average_fps = 0.0;
  monitor->current_fps = 0.0;
  monitor->performance_warning_issued = false;
}

void performance_monitor_start_frame(PerformanceMonitor *monitor)
{
  monitor->last_frame_time = get_current_time_microseconds();
}

void performance_monitor_end_frame(PerformanceMonitor *monitor)
{
  uint64_t current_time = get_current_time_microseconds();
  uint64_t frame_time = current_time - monitor->last_frame_time;

  monitor->frame_count++;
  monitor->total_frame_time += frame_time;

  // Calculate current FPS
  if (frame_time > 0)
  {
    monitor->current_fps = 1000000.0 / frame_time;
  }

  // Calculate average FPS
  uint64_t total_time = current_time - monitor->start_time;
  if (total_time > 0)
  {
    monitor->average_fps = (monitor->frame_count * 1000000.0) / total_time;
  }
}

double performance_monitor_get_average_fps(PerformanceMonitor *monitor)
{
  return monitor->average_fps;
}

double performance_monitor_get_current_fps(PerformanceMonitor *monitor)
{
  return monitor->current_fps;
}

bool performance_monitor_check_warning(PerformanceMonitor *monitor)
{
  if (monitor->current_fps < WARNING_FPS_THRESHOLD && !monitor->performance_warning_issued)
  {
    printf("⚠️  PERFORMANCE WARNING: Current FPS (%.1f) below threshold (%d)\n",
           monitor->current_fps, WARNING_FPS_THRESHOLD);
    printf("   Target FPS: %d, Current: %.1f, Average: %.1f\n",
           TARGET_FPS, monitor->current_fps, monitor->average_fps);
    monitor->performance_warning_issued = true;
    return true;
  }
  else if (monitor->current_fps >= WARNING_FPS_THRESHOLD)
  {
    monitor->performance_warning_issued = false;
  }
  return false;
}

// JSONPatch operations
JSONPatchOperation *jsonpatch_operation_create(JSONPatchOp op, const char *path, const char *value)
{
  JSONPatchOperation *operation = malloc(sizeof(JSONPatchOperation));
  if (!operation)
    return NULL;

  operation->op = op;
  operation->path = path ? strdup(path) : NULL;
  operation->value = value ? strdup(value) : NULL;
  operation->from = NULL;

  return operation;
}

void jsonpatch_operation_destroy(JSONPatchOperation *operation)
{
  if (!operation)
    return;

  free(operation->path);
  free(operation->value);
  free(operation->from);
  free(operation);
}

char *jsonpatch_operation_to_json(JSONPatchOperation *operation)
{
  if (!operation)
    return NULL;

  const char *op_strings[] = {"add", "remove", "replace", "move", "copy", "test"};
  const char *op_string = op_strings[operation->op];

  // Calculate required size
  int size = snprintf(NULL, 0,
                      "{\"op\":\"%s\",\"path\":\"%s\"",
                      op_string, operation->path ? operation->path : "");

  if (operation->value)
  {
    size += snprintf(NULL, 0, ",\"value\":\"%s\"", operation->value);
  }

  if (operation->from)
  {
    size += snprintf(NULL, 0, ",\"from\":\"%s\"", operation->from);
  }

  size += 2; // For closing brace and null terminator

  char *json = malloc(size);
  if (!json)
    return NULL;

  int written = snprintf(json, size,
                         "{\"op\":\"%s\",\"path\":\"%s\"",
                         op_string, operation->path ? operation->path : "");

  if (operation->value)
  {
    written += snprintf(json + written, size - written,
                        ",\"value\":\"%s\"", operation->value);
  }

  if (operation->from)
  {
    written += snprintf(json + written, size - written,
                        ",\"from\":\"%s\"", operation->from);
  }

  strcat(json, "}");
  return json;
}

char *jsonpatch_operations_to_json(JSONPatchOperation *operations, int count)
{
  if (!operations || count <= 0)
    return strdup("[]");

  // Calculate total size needed
  int total_size = 2; // For "[]"
  for (int i = 0; i < count; i++)
  {
    char *op_json = jsonpatch_operation_to_json(&operations[i]);
    if (op_json)
    {
      total_size += strlen(op_json) + 1; // +1 for comma
      free(op_json);
    }
  }

  char *json = malloc(total_size);
  if (!json)
    return NULL;

  strcpy(json, "[");
  int pos = 1;

  for (int i = 0; i < count; i++)
  {
    char *op_json = jsonpatch_operation_to_json(&operations[i]);
    if (op_json)
    {
      strcpy(json + pos, op_json);
      pos += strlen(op_json);
      free(op_json);

      if (i < count - 1)
      {
        strcpy(json + pos, ",");
        pos++;
      }
    }
  }

  strcpy(json + pos, "]");
  return json;
}

// State transition management
StateTransition *state_transition_create(uint64_t vector_clock, const char *world_seed)
{
  StateTransition *transition = malloc(sizeof(StateTransition));
  if (!transition)
    return NULL;

  transition->vector_clock = vector_clock;
  transition->timestamp = get_current_time_microseconds();
  transition->world_seed = world_seed ? strdup(world_seed) : NULL;
  transition->operations = NULL;
  transition->operation_count = 0;
  transition->operation_capacity = 0;

  return transition;
}

void state_transition_destroy(StateTransition *transition)
{
  if (!transition)
    return;

  if (transition->world_seed) free(transition->world_seed);

  for (int i = 0; i < transition->operation_count; i++)
  {
    // Free individual fields since operations were created directly in the array
    if (transition->operations[i].path) free(transition->operations[i].path);
    if (transition->operations[i].value) free(transition->operations[i].value);
    if (transition->operations[i].from) free(transition->operations[i].from);
  }

  if (transition->operations) free(transition->operations);
  free(transition);
}

bool state_transition_add_operation(StateTransition *transition, JSONPatchOp op,
                                    const char *path, const char *value, const char *from)
{
  if (!transition)
    return false;

  // Expand capacity if needed
  if (transition->operation_count >= transition->operation_capacity)
  {
    int new_capacity = transition->operation_capacity == 0 ? 10 : transition->operation_capacity * 2;
    JSONPatchOperation *new_operations = realloc(transition->operations,
                                                 new_capacity * sizeof(JSONPatchOperation));
    if (!new_operations)
      return false;

    transition->operations = new_operations;
    transition->operation_capacity = new_capacity;
  }

  // Create new operation directly in the array
  JSONPatchOperation *operation = &transition->operations[transition->operation_count];

  operation->op = op;
  operation->path = path ? strdup(path) : NULL;
  operation->value = value ? strdup(value) : NULL;
  operation->from = from ? strdup(from) : NULL;

  transition->operation_count++;
  return true;
}

char *state_transition_to_json(StateTransition *transition)
{
  if (!transition)
    return NULL;

  char *operations_json = jsonpatch_operations_to_json(transition->operations, transition->operation_count);
  if (!operations_json)
    return NULL;

      int size = snprintf(NULL, 0,
        "{\"vector_clock\":%llu,\"timestamp\":%llu,\"world_seed\":\"%s\",\"operations\":%s}",
        transition->vector_clock, transition->timestamp,
        transition->world_seed ? transition->world_seed : "",
        operations_json);

  char *json = malloc(size + 1);
  if (!json)
  {
    free(operations_json);
    return NULL;
  }

      snprintf(json, size + 1,
        "{\"vector_clock\":%llu,\"timestamp\":%llu,\"world_seed\":\"%s\",\"operations\":%s}",
        transition->vector_clock, transition->timestamp,
        transition->world_seed ? transition->world_seed : "",
        operations_json);

  free(operations_json);
  return json;
}

// State transition system management
StateTransitionSystem *state_transition_system_create()
{
  StateTransitionSystem *system = malloc(sizeof(StateTransitionSystem));
  if (!system)
    return NULL;

  system->transitions = NULL;
  system->transition_count = 0;
  system->transition_capacity = 0;
  system->genesis_state = NULL;
  system->initialized = false;

  vector_clock_init(&system->vector_clock);
  performance_monitor_init(&system->performance);

  return system;
}

void state_transition_system_destroy(StateTransitionSystem *system)
{
  if (!system)
    return;

  for (int i = 0; i < system->transition_count; i++)
  {
    state_transition_destroy(&system->transitions[i]);
  }

  free(system->transitions);
  free(system->genesis_state);
  free(system);
}

bool state_transition_system_initialize(StateTransitionSystem *system, const char *world_seed)
{
  if (!system || !world_seed)
    return false;

  system->initialized = true;
  return true;
}

// World state tracking
bool state_transition_system_record_world_changes(StateTransitionSystem *system,
                                                  World *world, uint64_t current_time)
{
  if (!system || !world || !system->initialized)
    return false;

  vector_clock_update(&system->vector_clock, current_time);

  if (!vector_clock_is_tick_ready(&system->vector_clock))
  {
    return true; // No tick ready, no transition needed
  }

  // Create new state transition
  StateTransition *transition = state_transition_create(
      vector_clock_get_current_tick(&system->vector_clock),
      world->log ? world->log : "unknown");

  if (!transition)
    return false;

  // Add world properties to transition
  char *gravity_path = create_json_path("/world/properties/gravity");
  char gravity_value[32];
  snprintf(gravity_value, sizeof(gravity_value), "%.2f", world->gravity);

  state_transition_add_operation(transition, JSONPATCH_REPLACE,
                                 gravity_path, gravity_value, NULL);
  free(gravity_path);

  // Add world dimensions
  char *dimensions_path = create_json_path("/world/properties/dimensions");
  char dimensions_value[64];
  snprintf(dimensions_value, sizeof(dimensions_value),
           "{\"width\":%u,\"height\":%u,\"depth\":%u}",
           world->width, world->height, world->depth);

  state_transition_add_operation(transition, JSONPATCH_REPLACE,
                                 dimensions_path, dimensions_value, NULL);
  free(dimensions_path);

  // Add transition to system
  if (system->transition_count >= system->transition_capacity)
  {
    int new_capacity = system->transition_capacity == 0 ? 10 : system->transition_capacity * 2;
    StateTransition *new_transitions = realloc(system->transitions,
                                               new_capacity * sizeof(StateTransition));
    if (!new_transitions)
    {
      state_transition_destroy(transition);
      return false;
    }

    system->transitions = new_transitions;
    system->transition_capacity = new_capacity;
  }

  // Copy transition into array
  system->transitions[system->transition_count] = *transition;

  // Clear the world_seed pointer in the original so it doesn't get freed
  transition->world_seed = NULL;
  transition->operations = NULL;
  transition->operation_count = 0;

  system->transition_count++;

  // Now we can safely destroy the original transition structure
  free(transition);

  return true;
}

bool state_transition_system_record_voxel_change(StateTransitionSystem *system,
                                                 uint32_t x, uint32_t y, uint32_t z,
                                                 VoxelType old_type, VoxelType new_type)
{
  (void)old_type; // Suppress unused parameter warning
  if (!system || !system->initialized)
    return false;

  if (system->transition_count == 0)
    return true; // No current transition

  StateTransition *current_transition = &system->transitions[system->transition_count - 1];

  char *voxel_path = create_json_path("/world/voxels/%u,%u,%u/type", x, y, z);
  char *new_type_str = voxel_type_to_string(new_type);

  bool success = state_transition_add_operation(current_transition, JSONPATCH_REPLACE,
                                                voxel_path, new_type_str, NULL);

  free(voxel_path);
  return success;
}

bool state_transition_system_record_gravity_change(StateTransitionSystem *system,
                                                   float old_gravity, float new_gravity)
{
  (void)old_gravity; // Suppress unused parameter warning
  if (!system || !system->initialized)
    return false;

  if (system->transition_count == 0)
    return true; // No current transition

  StateTransition *current_transition = &system->transitions[system->transition_count - 1];

  char *gravity_path = create_json_path("/world/properties/gravity");
  char gravity_value[32];
  snprintf(gravity_value, sizeof(gravity_value), "%.2f", new_gravity);

  bool success = state_transition_add_operation(current_transition, JSONPATCH_REPLACE,
                                                gravity_path, gravity_value, NULL);

  free(gravity_path);
  return success;
}

// Genesis state management
char *state_transition_system_create_genesis_state(World *world)
{
  if (!world)
    return NULL;

  int size = snprintf(NULL, 0,
                      "{\"world\":{\"properties\":{\"width\":%u,\"height\":%u,\"depth\":%u,\"gravity\":%.2f},\"voxels\":{}}}",
                      world->width, world->height, world->depth, world->gravity);

  char *genesis = malloc(size + 1);
  if (!genesis)
    return NULL;

  snprintf(genesis, size + 1,
           "{\"world\":{\"properties\":{\"width\":%u,\"height\":%u,\"depth\":%u,\"gravity\":%.2f},\"voxels\":{}}}",
           world->width, world->height, world->depth, world->gravity);

  return genesis;
}

bool state_transition_system_set_genesis_state(StateTransitionSystem *system, const char *genesis_state)
{
  if (!system)
    return false;

  free(system->genesis_state);
  system->genesis_state = genesis_state ? strdup(genesis_state) : NULL;
  return true;
}

char *state_transition_system_get_genesis_state(StateTransitionSystem *system)
{
  return system ? system->genesis_state : NULL;
}

// Memory and performance analysis
void analyze_world_memory_usage(World *world)
{
  if (!world)
    return;

  size_t voxel_size = sizeof(Voxel);
  size_t total_voxels = (size_t)world->width * world->height * world->depth;
  size_t total_memory = total_voxels * voxel_size;

  printf("🌍 World Memory Analysis:\n");
  printf("   Dimensions: %ux%ux%u\n", world->width, world->height, world->depth);
  printf("   Total voxels: %zu\n", total_voxels);
  printf("   Voxel size: %zu bytes\n", voxel_size);
  printf("   Total memory: %.2f MB\n", total_memory / (1024.0 * 1024.0));
  printf("   Memory per dimension:\n");
  printf("     Width: %.2f MB\n", (world->height * world->depth * voxel_size) / (1024.0 * 1024.0));
  printf("     Height: %.2f MB\n", (world->width * world->depth * voxel_size) / (1024.0 * 1024.0));
  printf("     Depth: %.2f MB\n", (world->width * world->height * voxel_size) / (1024.0 * 1024.0));
}

void analyze_performance_expectations()
{
  printf("⚡ Performance Expectations:\n");
  printf("   Target FPS: %d\n", TARGET_FPS);
  printf("   Target frame time: %.2f ms\n", FRAME_TIME_TARGET / 1000.0);
  printf("   Warning threshold: %d FPS\n", WARNING_FPS_THRESHOLD);
  printf("   Warning frame time: %.2f ms\n", FRAME_TIME_WARNING / 1000.0);
  printf("   Vector clock ticks per second: %d\n", VECTOR_CLOCK_TICKS_PER_SECOND);
  printf("   Vector clock interval: %.2f ms\n", VECTOR_CLOCK_INTERVAL / 1000.0);
}

void estimate_cpu_usage_for_world_size(uint32_t width, uint32_t height, uint32_t depth)
{
  size_t total_voxels = (size_t)width * height * depth;

  // Estimate operations per frame
  size_t voxel_checks_per_frame = total_voxels * 0.1; // Assume 10% of voxels checked per frame
  size_t physics_calculations = total_voxels * 0.05;  // Assume 5% physics calculations
  size_t rendering_operations = total_voxels * 0.3;   // Assume 30% rendering operations

  size_t total_ops_per_frame = voxel_checks_per_frame + physics_calculations + rendering_operations;
  size_t ops_per_second = total_ops_per_frame * TARGET_FPS;

  printf("💻 CPU Usage Estimation for %ux%ux%u world:\n", width, height, depth);
  printf("   Total voxels: %zu\n", total_voxels);
  printf("   Operations per frame: %zu\n", total_ops_per_frame);
  printf("   Operations per second: %zu\n", ops_per_second);
  printf("   Estimated CPU load: %.1f%%\n", (ops_per_second / 1000000.0) * 100); // Assuming 1M ops = 100% CPU
}

// Save/load with state transitions
bool state_transition_system_save_to_file(StateTransitionSystem *system, const char *filename)
{
  if (!system || !filename)
    return false;

  FILE *file = fopen(filename, "w");
  if (!file)
    return false;

  // Save genesis state
  if (system->genesis_state)
  {
    fprintf(file, "GENESIS_STATE:\n%s\n", system->genesis_state);
  }

  // Save transitions
  fprintf(file, "TRANSITIONS:\n");
  for (int i = 0; i < system->transition_count; i++)
  {
    char *transition_json = state_transition_to_json(&system->transitions[i]);
    if (transition_json)
    {
      fprintf(file, "%s\n", transition_json);
      free(transition_json);
    }
  }

  fclose(file);
  return true;
}

StateTransitionSystem *state_transition_system_load_from_file(const char *filename)
{
  if (!filename)
    return NULL;

  FILE *file = fopen(filename, "r");
  if (!file)
    return NULL;

  StateTransitionSystem *system = state_transition_system_create();
  if (!system)
  {
    fclose(file);
    return NULL;
  }

  char line[4096];
  bool in_transitions = false;

  while (fgets(line, sizeof(line), file))
  {
    line[strcspn(line, "\n")] = 0; // Remove newline

    if (strcmp(line, "GENESIS_STATE:") == 0)
    {
      // Read genesis state
      if (fgets(line, sizeof(line), file))
      {
        line[strcspn(line, "\n")] = 0;
        system->genesis_state = strdup(line);
      }
    }
    else if (strcmp(line, "TRANSITIONS:") == 0)
    {
      in_transitions = true;
    }
    else if (in_transitions && strlen(line) > 0)
    {
      // Parse transition JSON (simplified - in real implementation, use proper JSON parser)
      // For now, just count transitions
      system->transition_count++;
    }
  }

  fclose(file);
  return system;
}
