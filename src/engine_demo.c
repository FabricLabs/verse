#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include "world.h"
#include "engine.h"

// Function to print a cross-section of the world at a specific Y level
void print_world_slice(World *world, uint32_t y)
{
  if (!world || y >= world->height)
  {
    printf("Invalid world or Y coordinate\n");
    return;
  }

  printf("World slice at y=%u:\n", y);

  // Print Z coordinates on top
  printf("  ");
  for (uint32_t z = 0; z < world->depth; z++)
  {
    printf("%c", (z % 10) + '0');
  }
  printf("\n");

  for (uint32_t x = 0; x < world->width; x++)
  {
    // Print X coordinate on the left
    printf("%3u ", x);

    for (uint32_t z = 0; z < world->depth; z++)
    {
      Voxel *voxel = world_get_voxel(world, x, y, z);
      char symbol = ' ';

      if (voxel)
      {
        switch (voxel->type)
        {
        case VOXEL_AIR:
          symbol = ' ';
          break;
        case VOXEL_SOIL:
          symbol = '#';
          break;
        case VOXEL_GRASS:
          symbol = '^';
          break;
        case VOXEL_STONE:
          symbol = '@';
          break;
        case VOXEL_WATER:
          symbol = '~';
          break;
        case VOXEL_WOOD:
          symbol = '|';
          break;
        case VOXEL_LEAVES:
          symbol = '*';
          break;
        case VOXEL_SAND:
          symbol = '.';
          break;
        case VOXEL_WORLD:
          symbol = 'W';
          break; // Special character for nested worlds
        default:
          symbol = '?';
          break;
        }
      }

      printf("%c", symbol);
    }

    printf("\n");
  }
}

// Generate test worlds in the surrounding directions
void generate_test_worlds(const char *base_name, uint32_t size)
{
  // Create all 9 worlds (center + 8 surrounding)
  char filename[256];

  // Center world
  sprintf(filename, "%s.world", base_name);
  printf("Generating center world: %s\n", filename);
  World *world = world_create(size, size, size);
  if (world)
  {
    world_generate(world, "center");
    char *serialized = world_serialize(world);
    if (serialized)
    {
      FILE *file = fopen(filename, "w");
      if (file)
      {
        fprintf(file, "%s", serialized);
        fclose(file);
      }
      free(serialized);
    }
    world_destroy(world);
  }

  // Generate the 8 surrounding worlds with different seeds
  const char *directions[] = {
      "north", "northeast", "east", "southeast",
      "south", "southwest", "west", "northwest"};

  for (int i = 0; i < 8; i++)
  {
    sprintf(filename, "%s_%s.world", base_name, directions[i]);
    printf("Generating %s world: %s\n", directions[i], filename);

    World *dir_world = world_create(size, size, size);
    if (dir_world)
    {
      world_generate(dir_world, directions[i]); // Use direction as seed
      char *serialized = world_serialize(dir_world);
      if (serialized)
      {
        FILE *file = fopen(filename, "w");
        if (file)
        {
          fprintf(file, "%s", serialized);
          fclose(file);
        }
        free(serialized);
      }
      world_destroy(dir_world);
    }
  }
}

// Function to add some test actors to the engine
void add_test_actors(Engine *engine)
{
  // Get center world dimensions
  World *center_world = engine_get_world(engine, DIR_CENTER);
  if (!center_world)
    return;

  uint32_t width = center_world->width;
  uint32_t height = center_world->height;
  uint32_t depth = center_world->depth;

  // Add a stationary actor in the center world
  Actor *center_actor = engine_add_actor(engine, "Center", width / 2, height / 2, depth / 2, DIR_CENTER);

  // Add actors along the edges that will move to neighboring worlds
  // North edge moving north
  Actor *north_actor = engine_add_actor(engine, "North", width / 2, height / 2, 2, DIR_CENTER);
  if (north_actor)
  {
    north_actor->velocity_z = -2.0; // Moving north (negative z)
  }

  // East edge moving east
  Actor *east_actor = engine_add_actor(engine, "East", width - 2, height / 2, depth / 2, DIR_CENTER);
  if (east_actor)
  {
    east_actor->velocity_x = 2.0; // Moving east (positive x)
  }

  // South edge moving south
  Actor *south_actor = engine_add_actor(engine, "South", width / 2, height / 2, depth - 2, DIR_CENTER);
  if (south_actor)
  {
    south_actor->velocity_z = 2.0; // Moving south (positive z)
  }

  // West edge moving west
  Actor *west_actor = engine_add_actor(engine, "West", 2, height / 2, depth / 2, DIR_CENTER);
  if (west_actor)
  {
    west_actor->velocity_x = -2.0; // Moving west (negative x)
  }

  // Diagonal movements
  Actor *ne_actor = engine_add_actor(engine, "Northeast", width - 2, height / 2, 2, DIR_CENTER);
  if (ne_actor)
  {
    ne_actor->velocity_x = 1.5;
    ne_actor->velocity_z = -1.5;
  }

  // Add a bouncing actor
  Actor *bounce_actor = engine_add_actor(engine, "Bounce", width / 4, height / 2, depth / 4, DIR_CENTER);
  if (bounce_actor)
  {
    bounce_actor->velocity_x = 3.0;
    bounce_actor->velocity_z = 2.0;
  }
}

// Helper function to get direction name
const char *get_direction_name(int dir)
{
  switch (dir)
  {
  case DIR_CENTER:
    return "Center";
  case DIR_NORTH:
    return "North";
  case DIR_NORTHEAST:
    return "Northeast";
  case DIR_EAST:
    return "East";
  case DIR_SOUTHEAST:
    return "Southeast";
  case DIR_SOUTH:
    return "South";
  case DIR_SOUTHWEST:
    return "Southwest";
  case DIR_WEST:
    return "West";
  case DIR_NORTHWEST:
    return "Northwest";
  default:
    return "Unknown";
  }
}

// Function to get actor count (since we can't access engine->actor_count directly)
extern size_t engine_get_actor_count(Engine *engine);

// Function to get actor by index (since we can't access engine->actors[i] directly)
extern Actor *engine_get_actor_by_index(Engine *engine, size_t index);

// Main function for the engine demo
int main(int argc, char *argv[])
{
  printf("VERSE Engine Demo\n");
  printf("=================\n\n");

  // Initialize random number generator
  srand(time(NULL));

  // Parse command line arguments
  bool generate_worlds = false;
  const char *base_name = "demo";
  uint32_t world_size = 32;

  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--generate") == 0 || strcmp(argv[i], "-g") == 0)
    {
      generate_worlds = true;
    }
    else if ((strcmp(argv[i], "--name") == 0 || strcmp(argv[i], "-n") == 0) && i + 1 < argc)
    {
      base_name = argv[i + 1];
      i++;
    }
    else if ((strcmp(argv[i], "--size") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc)
    {
      world_size = atoi(argv[i + 1]);
      if (world_size < 8)
        world_size = 8;
      if (world_size > 128)
        world_size = 128;
      i++;
    }
  }

  // Generate test worlds if requested
  if (generate_worlds)
  {
    printf("Generating test worlds with base name '%s' and size %u\n", base_name, world_size);
    generate_test_worlds(base_name, world_size);
  }

  // Create the engine
  Engine *engine = engine_create();
  if (!engine)
  {
    printf("Failed to create engine\n");
    return 1;
  }

  // Load the center world and surrounding worlds
  char center_world_path[256];
  sprintf(center_world_path, "%s.world", base_name);

  printf("Loading worlds starting with center world: %s\n", center_world_path);
  if (!engine_load_worlds(engine, center_world_path))
  {
    printf("Failed to load center world\n");
    engine_destroy(engine);
    return 1;
  }

  // Report on loaded worlds
  for (int dir = 0; dir < DIR_COUNT; dir++)
  {
    World *world = engine_get_world(engine, dir);
    if (world)
    {
      printf("Loaded world: %s (%ux%ux%u)\n",
             get_direction_name(dir), world->width, world->height, world->depth);
    }
  }

  // Add test actors
  add_test_actors(engine);
  printf("Added test actors to the engine\n");

  // Save actors for later loading
  engine_save_actors(engine, "demo_actors.dat");

  // Main simulation loop
  double delta_time = 0.5; // Half a second per update for demo
  int max_steps = 20;

  printf("\nStarting simulation with %d steps\n", max_steps);
  for (int step = 0; step < max_steps; step++)
  {
    printf("\nStep %d:\n", step + 1);

    // Update all actors
    engine_update_all_actors(engine, delta_time);

    // Print actor positions
    size_t actor_count = engine_get_actor_count(engine);
    for (size_t i = 0; i < actor_count; i++)
    {
      Actor *actor = engine_get_actor_by_index(engine, i);
      if (actor)
      {
        printf("Actor %u (%s): pos=(%.1f, %.1f, %.1f) world=%s vel=(%.1f, %.1f, %.1f)\n",
               actor->id, actor->name,
               actor->x, actor->y, actor->z,
               get_direction_name(actor->world_index),
               actor->velocity_x, actor->velocity_y, actor->velocity_z);

        // Bounce actors off world boundaries if they're in a boundary world
        if (actor->world_index != DIR_CENTER)
        {
          // Reverse velocities to send them back to center
          if (actor->velocity_x > 0 &&
              (actor->world_index == DIR_EAST ||
               actor->world_index == DIR_NORTHEAST ||
               actor->world_index == DIR_SOUTHEAST))
          {
            actor->velocity_x = -actor->velocity_x;
          }

          if (actor->velocity_x < 0 &&
              (actor->world_index == DIR_WEST ||
               actor->world_index == DIR_NORTHWEST ||
               actor->world_index == DIR_SOUTHWEST))
          {
            actor->velocity_x = -actor->velocity_x;
          }

          if (actor->velocity_z > 0 &&
              (actor->world_index == DIR_SOUTH ||
               actor->world_index == DIR_SOUTHEAST ||
               actor->world_index == DIR_SOUTHWEST))
          {
            actor->velocity_z = -actor->velocity_z;
          }

          if (actor->velocity_z < 0 &&
              (actor->world_index == DIR_NORTH ||
               actor->world_index == DIR_NORTHEAST ||
               actor->world_index == DIR_NORTHWEST))
          {
            actor->velocity_z = -actor->velocity_z;
          }
        }
        else
        {
          // Randomly change velocity sometimes for actors in center world
          if (strcmp(actor->name, "Bounce") == 0 && rand() % 100 < 20)
          {
            actor->velocity_x = -actor->velocity_x;
            actor->velocity_z = -actor->velocity_z;
          }
        }
      }
    }
  }

  // Save the final state
  printf("\nSaving final world and actor states...\n");
  engine_save_worlds(engine);
  engine_save_actors(engine, "demo_actors_final.dat");

  // Clean up
  engine_destroy(engine);
  printf("\nEngine demo completed successfully!\n");

  return 0;
}
