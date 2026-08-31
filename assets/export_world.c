#include "../src/world.h"
#include <stdio.h>

int main() {
  // Create a small world (32x32x32)
  World* world = world_create(32, 32, 32);
  if (!world) {
    printf("Failed to create world\n");
    return 1;
  }

  // Generate a sample world with floating island
  world_generate(world, "sample_world");

  // Serialize the world to a string
  char* serialized = world_serialize(world);
  if (!serialized) {
    printf("Failed to serialize world\n");
    world_destroy(world);
    return 1;
  }

  // Save to a file
  FILE* file = fopen("sample_world.txt", "w");
  if (!file) {
      printf("Failed to open file for writing\n");
      free(serialized);
      world_destroy(world);
      return 1;
  }

  fprintf(file, "%s", serialized);
  fclose(file);

  printf("World exported to sample_world.txt\n");

  // Clean up
  free(serialized);
  world_destroy(world);

  return 0;
}