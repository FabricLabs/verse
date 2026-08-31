#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "engine.h"
#include "world.h"

// Simple world file for testing
static const char* TEST_WORLD_DATA = "{ \"width\": 10, \"height\": 5, \"depth\": 10, \"blocks\": [] }";

// World ID for testing
#define TEST_WORLD_ID "test_world"

// Helper function to write a test world file
static void write_test_world_file(const char* filename) {
  FILE* file = fopen(filename, "w");
  if (!file) {
    fprintf(stderr, "Failed to create test world file: %s\n", filename);
    exit(EXIT_FAILURE);
  }

  fprintf(file, "%s", TEST_WORLD_DATA);
  fclose(file);
}

// Helper function to clean up test world files
static void cleanup_test_files() {
  remove("test_world.world");
  remove("test_actors.dat");
}

// Test engine creation and destruction
static void test_engine_create_destroy() {
  printf("Testing engine creation and destruction...\n");

  Engine* engine = engine_create();
  assert(engine != NULL);
  assert(engine->actors != NULL);
  assert(engine->actor_count == 0);
  assert(engine->actor_capacity > 0);
  assert(engine->next_actor_id == 1);
  assert(engine->world_table != NULL);
  assert(engine->world_count == 0);
  assert(engine->world_table_size > 0);

  engine_destroy(engine);
  printf("PASS: Engine creation and destruction\n");
}

// Test loading a single world
static void test_engine_load_world() {
  printf("Testing single world loading...\n");

  // Create test world file
  write_test_world_file("test_world.world");

  Engine* engine = engine_create();
  assert(engine != NULL);

  // Load the world
  bool result = engine_load_world(engine, "test_world.world", TEST_WORLD_ID);
  assert(result);

  // Check that the world was loaded
  World* world = engine_get_world(engine, TEST_WORLD_ID);
  assert(world != NULL);

  // Set it as center world
  strncpy(engine->center_world_id, TEST_WORLD_ID, sizeof(engine->center_world_id) - 1);
  engine->center_world_id[sizeof(engine->center_world_id) - 1] = '\0';

  // We should have 1 world
  assert(engine->world_count == 1);

  engine_destroy(engine);
  printf("PASS: Single world loading\n");
}

// Test actor management
static void test_actor_management() {
    printf("Testing actor management...\n");

    // Create test world file
    write_test_world_file("test_world.world");

    Engine* engine = engine_create();
    assert(engine != NULL);

    // Load the world
    bool result = engine_load_world(engine, "test_world.world", TEST_WORLD_ID);
    assert(result);
    strncpy(engine->center_world_id, TEST_WORLD_ID, sizeof(engine->center_world_id) - 1);
    engine->center_world_id[sizeof(engine->center_world_id) - 1] = '\0';

    // Test adding actors
    Actor* actor1 = engine_add_actor(engine, "Player", 1.0, 2.0, 3.0, TEST_WORLD_ID);
    assert(actor1 != NULL);
    assert(engine->actor_count == 1);
    assert(actor1->id == 1);
    assert(strcmp(actor1->name, "Player") == 0);
    assert(actor1->x == 1.0 && actor1->y == 2.0 && actor1->z == 3.0);
    assert(strcmp(actor1->world_id, TEST_WORLD_ID) == 0);

    Actor* actor2 = engine_add_actor(engine, "NPC", 4.0, 5.0, 6.0, TEST_WORLD_ID);
    assert(actor2 != NULL);
    assert(engine->actor_count == 2);
    assert(actor2->id == 2);

    // Test finding actors
    Actor* found = engine_find_actor(engine, 1);
    assert(found == actor1);

    found = engine_find_actor(engine, 2);
    assert(found == actor2);

    found = engine_find_actor(engine, 999); // Non-existent ID
    assert(found == NULL);

    // Test getting actor by index
    Actor* by_index = engine_get_actor_by_index(engine, 0);
    assert(by_index == actor1);

    by_index = engine_get_actor_by_index(engine, 1);
    assert(by_index == actor2);

    // Test removing actors
    result = engine_remove_actor(engine, 1);
    assert(result);
    assert(engine->actor_count == 1);

    found = engine_find_actor(engine, 1);
    assert(found == NULL);

    // The second actor should now be at index 0
    by_index = engine_get_actor_by_index(engine, 0);
    assert(by_index->id == actor2->id);

    engine_destroy(engine);
    printf("PASS: Actor management\n");
}

// Test actor movement within a single world
static void test_actor_movement() {
  printf("Testing actor movement within world...\n");

  // Create test world file
  write_test_world_file("test_world.world");

  Engine* engine = engine_create();
  assert(engine != NULL);

  // Load the world
  bool result = engine_load_world(engine, "test_world.world", TEST_WORLD_ID);
  assert(result);
  strncpy(engine->center_world_id, TEST_WORLD_ID, sizeof(engine->center_world_id) - 1);
  engine->center_world_id[sizeof(engine->center_world_id) - 1] = '\0';

  // Add a player in the middle of the world
  Actor* player = engine_add_actor(engine, "Player", 5.0, 2.0, 5.0, TEST_WORLD_ID);
  assert(player != NULL);

  // Test basic movement
  // Note: we don't make specific assertions about exact positions
  // since different engines might implement physics differently

  // Try moving in X direction
  player->velocity_x = 1.0;
  player->velocity_y = 0.0;
  player->velocity_z = 0.0;

  // Store initial position
  float initial_x = player->x;

  // Update position
  engine_update_actor(engine, player, 0.5);

  // Player should have moved in X direction
  assert(player->x != initial_x);

  // Verify player is still within world bounds
  assert(player->x >= 0.0 && player->x < 10.0);
  assert(player->y >= 0.0 && player->y < 5.0);
  assert(player->z >= 0.0 && player->z < 10.0);

  // Test Z movement
  player->velocity_x = 0.0;
  player->velocity_z = 1.0;

  // Store initial position
  float initial_z = player->z;

  // Update position
  engine_update_actor(engine, player, 0.5);

  // Player should have moved in Z direction
  assert(player->z != initial_z);

  // Verify player is still within world bounds
  assert(player->x >= 0.0 && player->x < 10.0);
  assert(player->y >= 0.0 && player->y < 5.0);
  assert(player->z >= 0.0 && player->z < 10.0);

  engine_destroy(engine);
  printf("PASS: Actor movement within world\n");
}

// Test actor collision with world boundaries
static void test_actor_boundaries() {
  printf("Testing actor collision with world boundaries...\n");

  // Create test world file
  write_test_world_file("test_world.world");

  Engine* engine = engine_create();
  assert(engine != NULL);

  // Load the world
  bool result = engine_load_world(engine, "test_world.world", TEST_WORLD_ID);
  assert(result);
  strncpy(engine->center_world_id, TEST_WORLD_ID, sizeof(engine->center_world_id) - 1);
  engine->center_world_id[sizeof(engine->center_world_id) - 1] = '\0';

  World* world = engine_get_world(engine, TEST_WORLD_ID);
  assert(world != NULL);

  // Add a player near the edge
  Actor* player = engine_add_actor(engine, "Player", 9.0, 2.0, 9.0, TEST_WORLD_ID);
  assert(player != NULL);

  // Try to move beyond world width
  player->velocity_x = 2.0;
  player->velocity_y = 0.0;
  player->velocity_z = 0.0;

  // Update the player
  engine_update_actor(engine, player, 1.0);

  // Check that the player is still in the same world
  assert(strcmp(player->world_id, TEST_WORLD_ID) == 0);

  // Verify player is still within valid bounds
  assert(player->x >= 0.0 && player->x < 10.0);
  assert(player->y >= 0.0 && player->y < 5.0);
  assert(player->z >= 0.0 && player->z < 10.0);

  engine_destroy(engine);
  printf("PASS: Actor collision with world boundaries\n");
}

// Test saving and loading actors
static void test_actor_serialization() {
  printf("Testing actor serialization...\n");

  // Create test world file
  write_test_world_file("test_world.world");

  Engine* engine = engine_create();
  assert(engine != NULL);

  // Load the world
  bool result = engine_load_world(engine, "test_world.world", TEST_WORLD_ID);
  assert(result);
  strncpy(engine->center_world_id, TEST_WORLD_ID, sizeof(engine->center_world_id) - 1);
  engine->center_world_id[sizeof(engine->center_world_id) - 1] = '\0';

  // Add some actors
  Actor* actor1 = engine_add_actor(engine, "Player", 1.0, 2.0, 3.0, TEST_WORLD_ID);
  assert(actor1 != NULL);

  Actor* actor2 = engine_add_actor(engine, "NPC", 4.0, 5.0, 6.0, TEST_WORLD_ID);
  assert(actor2 != NULL);

  // Save actors to file
  result = engine_save_actors(engine, "test_actors.dat");
  assert(result);

  // Create a new engine
  Engine* engine2 = engine_create();
  assert(engine2 != NULL);

  // Load the world
  result = engine_load_world(engine2, "test_world.world", TEST_WORLD_ID);
  assert(result);
  strncpy(engine2->center_world_id, TEST_WORLD_ID, sizeof(engine2->center_world_id) - 1);
  engine2->center_world_id[sizeof(engine2->center_world_id) - 1] = '\0';

  // Load actors from file
  result = engine_load_actors(engine2, "test_actors.dat");
  assert(result);
  assert(engine2->actor_count == 2);

  // Check that the actors were loaded correctly
  Actor* loaded1 = engine_find_actor(engine2, 1);
  assert(loaded1 != NULL);
  assert(strcmp(loaded1->name, "Player") == 0);
  assert(loaded1->x == 1.0 && loaded1->y == 2.0 && loaded1->z == 3.0);
  assert(strcmp(loaded1->world_id, TEST_WORLD_ID) == 0);

  Actor* loaded2 = engine_find_actor(engine2, 2);
  assert(loaded2 != NULL);
  assert(strcmp(loaded2->name, "NPC") == 0);
  assert(loaded2->x == 4.0 && loaded2->y == 5.0 && loaded2->z == 6.0);
  assert(strcmp(loaded2->world_id, TEST_WORLD_ID) == 0);

  // Check that next_actor_id was set correctly
  assert(engine2->next_actor_id == 3);

  engine_destroy(engine);
  engine_destroy(engine2);
  printf("PASS: Actor serialization\n");
}

// Main test function
int main() {
  printf("Running simplified engine tests (single world)...\n");

  test_engine_create_destroy();
  test_engine_load_world();
  test_actor_management();
  test_actor_movement();
  test_actor_boundaries();
  test_actor_serialization();

  // Clean up test files
  cleanup_test_files();

  printf("All simplified tests PASSED!\n");
  return 0;
} 