// main.c
// Implements command line interface for VERSE.

// Standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>

// Project libraries
#include "world.h"
#include "server.h"
#include "engine.h"
#include "player.h"
#include "constants.h"
#include "mud_telnet.h"

// Function to print a cross-section of the world at a specific Y level
void print_world_slice(World* world, uint32_t y) {
  if (!world || y >= world->height) {
    printf("Invalid world or Y coordinate\n");
    return;
  }

  printf("World slice at y=%u:\n", y);

  // Print Z coordinates on top
  printf("  ");
  for (uint32_t z = 0; z < world->depth; z++) {
    printf("%c", (z % 10) + '0');
  }
  printf("\n");

  for (uint32_t x = 0; x < world->width; x++) {
    // Print X coordinate on the left
    printf("%3u ", x);

    for (uint32_t z = 0; z < world->depth; z++) {
      Voxel* voxel = world_get_voxel(world, x, y, z);
      char symbol = ' ';

      if (voxel) {
        switch (voxel->type) {
          case VOXEL_AIR:    symbol = ' '; break;
          case VOXEL_SOIL:   symbol = '#'; break;
          case VOXEL_GRASS:  symbol = '^'; break;
          case VOXEL_STONE:  symbol = '@'; break;
          case VOXEL_WATER:  symbol = '~'; break;
          case VOXEL_WOOD:   symbol = '|'; break;
          case VOXEL_LEAVES: symbol = '*'; break;
          case VOXEL_SAND:   symbol = '.'; break;
          case VOXEL_WORLD:  symbol = 'W'; break; // Special character for nested worlds
          default:           symbol = '?'; break;
        }
      }

      printf("%c", symbol);
    }

    printf("\n");
  }
}

// Function to save world to a file
void save_world_to_file(World* world, const char* filename) {
  FILE* file = fopen(filename, "w");
  if (file) {
    char* serialized = world_serialize(world);
    if (serialized) {
      fprintf(file, "%s", serialized);
      free(serialized);
      printf("World saved to %s\n", filename);
    } else {
      printf("Failed to serialize world\n");
    }
    fclose(file);
  } else {
    printf("Failed to open file for writing: %s\n", filename);
  }
}

// Function to load world from a file
World* load_world_from_file(const char* filename) {
  FILE* file = fopen(filename, "r");
  if (!file) {
    printf("Failed to open file: %s\n", filename);
    return NULL;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  rewind(file);

  // Allocate buffer
  char* buffer = (char*)malloc(file_size + 1);
  if (!buffer) {
    fclose(file);
    printf("Memory allocation failed\n");
    return NULL;
  }

  // Read file
  size_t read_size = fread(buffer, 1, file_size, file);
  buffer[read_size] = '\0';
  fclose(file);

  // Deserialize
  World* world = world_deserialize(buffer);
  free(buffer);

  if (!world) {
    printf("Failed to deserialize world from file\n");
  } else {
    printf("World loaded from %s\n", filename);
  }

  return world;
}

// Function to navigate through world slices
void navigate_world(World* world, const char* title) {
  uint32_t current_slice = world->height / 2;
  char cmd[10];
  char input[256];

  while (1) {
    system("clear");
    printf("Exploring %s\n", title);
    printf("Navigation commands:\n");
    printf("  up    - Move up one slice\n");
    printf("  down  - Move down one slice\n");
    printf("  save  - Save world to file\n");
    printf("  menu  - Return to main menu\n\n");
    printf("Legend:\n");
    printf("  ' ' - Air\n");
    printf("  '#' - Dirt\n");
    printf("  '^' - Grass\n");
    printf("  '@' - Stone\n");
    printf("  '~' - Water\n");
    printf("  '|' - Wood\n");
    printf("  '*' - Leaves\n");
    printf("  '.' - Sand\n");
    printf("  'W' - Nested World\n\n");
    printf("Current slice: %u/%u\n", current_slice, world->height - 1);
    print_world_slice(world, current_slice);
    printf("\nEnter command: ");
    fflush(stdout);

    if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
      break;
    }

    // Remove newline
    cmd[strcspn(cmd, "\n")] = 0;

    if (strcmp(cmd, "up") == 0 && current_slice < world->height - 1) {
      current_slice++;
    } else if (strcmp(cmd, "down") == 0 && current_slice > 0) {
      current_slice--;
    } else if (strcmp(cmd, "save") == 0) {
      printf("Enter filename to save (or press Enter for default.world): ");
      fflush(stdout);

      if (fgets(input, sizeof(input), stdin) == NULL || input[0] == '\n') {
        strcpy(input, "default.world");
      } else {
        // Remove newline
        input[strcspn(input, "\n")] = 0;
      }

      save_world_to_file(world, input);
      printf("Press Enter to continue...");
      fflush(stdout);
      getchar();
    } else if (strcmp(cmd, "menu") == 0) {
      break;
    }
  }
}

// Function to validate if the player can move to a given position (must be within bounds and on grass)
bool is_valid_player_move(World* world, uint32_t x, uint32_t y, uint32_t z) {
  if (!world_is_position_valid(world, x, y, z)) return false;
  Voxel* voxel = world_get_voxel(world, x, y, z);
  return voxel && voxel->type == VOXEL_GRASS;
}

// Print world slice with player position (if player is on this slice)
void print_world_slice_with_player(World* world, uint32_t y, uint32_t player_x, uint32_t player_z, bool show_player) {
  if (!world || y >= world->height) {
    printf("Invalid world or Y coordinate\n");
    return;
  }

  printf("World slice at y=%u:\n", y);
  printf("  ");
  for (uint32_t z = 0; z < world->depth; z++) {
    printf("%c", (z % 10) + '0');
  }
  printf("\n");

  for (uint32_t x = 0; x < world->width; x++) {
    printf("%3u ", x);
    for (uint32_t z = 0; z < world->depth; z++) {
      char symbol = ' ';
      if (show_player && x == player_x && z == player_z) {
        symbol = '@'; // Player symbol
      } else {
        Voxel* voxel = world_get_voxel(world, x, y, z);
        if (voxel) {
          switch (voxel->type) {
            case VOXEL_AIR:    symbol = ' '; break;
            case VOXEL_SOIL:   symbol = '#'; break;
            case VOXEL_GRASS:  symbol = '^'; break;
            case VOXEL_STONE:  symbol = '@'; break;
            case VOXEL_WATER:  symbol = '~'; break;
            case VOXEL_WOOD:   symbol = '|'; break;
            case VOXEL_LEAVES: symbol = '*'; break;
            case VOXEL_SAND:   symbol = '.'; break;
            case VOXEL_WORLD:  symbol = 'W'; break;
            default:           symbol = '?'; break;
          }
        }
      }
      printf("%c", symbol);
    }
    printf("\n");
  }
}

// Play mode: player can move on grass tiles using WASD
void play_mode(const char* seed, uint32_t size) {
  Engine* engine = engine_create();
  if (!engine) {
    printf("Failed to create engine.\n");
    return;
  }

  // Create or load player state
  PlayerState* player_state = load_player_state(VERSE_PLAYER_SAVE);
  if (!player_state) {
    player_state = player_state_create();
    // Set initial world seed for new players
    strncpy(player_state->current_world_id, seed, sizeof(player_state->current_world_id) - 1);
    player_state->current_world_id[sizeof(player_state->current_world_id) - 1] = '\0';
  }

  // Create the main world if it doesn't exist
  World* world = world_create(size, size, size);
  if (!world) {
    printf("Failed to create world\n");
    player_state_destroy(player_state);
    engine_destroy(engine);
    return;
  }
  world_generate(world, player_state->current_world_id);

  // Add the world to the engine with seed as ID
  if (!engine_add_world_entry(engine, player_state->current_world_id, world)) {
    printf("Failed to add world to engine\n");
    world_destroy(world);
    player_state_destroy(player_state);
    engine_destroy(engine);
    return;
  }

  // Set as center world ID
  strncpy(engine->center_world_id, player_state->current_world_id, sizeof(engine->center_world_id) - 1);
  engine->center_world_id[sizeof(engine->center_world_id) - 1] = '\0';

  // Create player actor at saved position or center if new
  Actor* player;
  if (player_state->x == 0 && player_state->y == 0 && player_state->z == 0) {
    uint32_t px = size / 2, pz = size / 2, py = 0;
    // Find highest grass tile at (px, *, pz)
    for (int y = size - 1; y >= 0; y--) {
      Voxel* v = world_get_voxel(world, px, y, pz);
      if (v && v->type == VOXEL_GRASS) {
        py = y;
        break;
      }
    }
    player = engine_add_actor(engine, "Player", px, py, pz, player_state->current_world_id);
    player_state->x = px;
    player_state->y = py;
    player_state->z = pz;
  } else {
    player = engine_add_actor(engine, "Player",
      player_state->x, player_state->y, player_state->z,
      player_state->current_world_id);
  }

  if (!player) {
    printf("Failed to add player actor\n");
    engine_destroy(engine);
    return;
  }

  int current_slice = player_state->y;
  char cmd[16];
  while (1) {
    system("clear");
    printf("VERSE - PLAY MODE\n");
    printf("Move with WASD, up/down to change slice, save, or menu.\n");
    printf("Legend: '@' = Player, '^' = Grass, '#' = Dirt, etc.\n");
    printf("Current slice: %d/%d\n", current_slice, world->height - 1);
    print_world_slice_with_player(world, current_slice, (uint32_t)player->x, (uint32_t)player->z, player->y == current_slice);
    printf("\nCommands: w/a/s/d = move, up/down = slice, save, menu\n");
    printf("Enter command: ");
    fflush(stdout);
    if (fgets(cmd, sizeof(cmd), stdin) == NULL) break;
    cmd[strcspn(cmd, "\n")] = 0;
    if (strcmp(cmd, "up") == 0 && current_slice < (int)world->height - 1) {
      current_slice++;
    } else if (strcmp(cmd, "down") == 0 && current_slice > 0) {
      current_slice--;
    } else if (strcmp(cmd, "save") == 0) {
      // Update player state before saving
      player_state->x = player->x;
      player_state->y = player->y;
      player_state->z = player->z;

      // Save player state
      save_player_state(player_state, VERSE_PLAYER_SAVE);

      // Compute and save neighboring worlds
      engine_compute_neighbors(engine, player_state->current_world_id);
      engine_save_neighbors(engine, player_state->current_world_id);

      printf("Game saved! Press Enter to continue...");
      fflush(stdout);
      getchar();
    } else if (strcmp(cmd, "menu") == 0) {
      break;
    } else if (strcmp(cmd, "w") == 0 || strcmp(cmd, "a") == 0 || strcmp(cmd, "s") == 0 || strcmp(cmd, "d") == 0) {
      int dx = 0, dz = 0;
      if (strcmp(cmd, "w") == 0) dz = -1;
      if (strcmp(cmd, "s") == 0) dz = 1;
      if (strcmp(cmd, "a") == 0) dx = -1;
      if (strcmp(cmd, "d") == 0) dx = 1;
      uint32_t nx = (uint32_t)player->x + dx;
      uint32_t nz = (uint32_t)player->z + dz;
      uint32_t ny = (uint32_t)player->y;
      if (is_valid_player_move(world, nx, ny, nz)) {
        player->x = nx;
        player->z = nz;
      } else {
        printf("Cannot move there! Only grass tiles are walkable. Press Enter...");
        fflush(stdout);
        getchar();
      }
    }
  }

  // Clean up
  player_state_destroy(player_state);
  engine_destroy(engine);

  printf("Thanks for playing!\n");
}

// Simple menu system (no fancy terminal handling)
void explore_mode(const char* seed, uint32_t size) {
  Engine* engine = engine_create();
  if (!engine) {
    printf("Failed to create engine.\n");
    return;
  }

  World* world = NULL;
  int choice = 0;
  char input[256];

  while (1) {
    // Display menu
    system("clear");
    printf("VERSE\n");
    printf("===================\n\n");
    printf("Current seed: %s\n", seed);
    printf("World size: %u\n\n", size);
    printf("Menu options:\n");
    printf("1. Start New World\n");
    printf("2. Load From File\n");
    printf("3. Play Mode\n");
    printf("4. Quit\n\n");
    printf("Enter your choice (1-4): ");
    fflush(stdout);

    // Get user choice
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break; // Error, exit
    }

    // Parse choice
    choice = atoi(input);

    if (choice == 1) {
      // Start New World
      printf("Initializing new world (please wait)...\n");

      // Clean up previous world if it exists
      if (world) {
        world_destroy(world);
        world = NULL;
      }

      // Create a new world
      world = world_create(size, size, size);

      if (!world) {
        printf("Failed to create world\n");
        printf("Press Enter to continue...");
        fflush(stdout);
        getchar();
        continue;
      }

      // Generate the world
      printf("Generating world with seed: %s...\n", seed);
      fflush(stdout);
      world_generate(world, seed);

      // Explorer mode - navigate through the world
      char title[256];
      snprintf(title, sizeof(title), "world with seed: %s", seed);
      navigate_world(world, title);

    } else if (choice == 2) {
      // Load From File
      printf("Enter seed to load (or press Enter for default): ");
      fflush(stdout);

      char load_seed[256];
      if (fgets(input, sizeof(input), stdin) == NULL || input[0] == '\n') {
        strncpy(load_seed, "default", sizeof(load_seed) - 1);
      } else {
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        strncpy(load_seed, input, sizeof(load_seed) - 1);
      }
      load_seed[sizeof(load_seed) - 1] = '\0';

      char world_path[256];
      construct_world_filename(world_path, sizeof(world_path), load_seed);

      // Clean up previous world if it exists
      if (world) {
        world_destroy(world);
        world = NULL;
      }

      // Load the world
      world = load_world_from_file(world_path);

      if (world) {
        // Explorer mode - navigate through the world
        char title[256];
        snprintf(title, sizeof(title), "world with seed: %s", load_seed);
        navigate_world(world, title);
      } else {
        printf("Failed to load world. Press Enter to continue...");
        fflush(stdout);
        getchar();
      }

    } else if (choice == 3) {
      // Play Mode
      play_mode(seed, size);

    } else if (choice == 4) {
      // Quit
      break;
    } else {
      printf("Invalid choice. Press Enter to continue...");
      fflush(stdout);
      getchar();
    }
  }

  // Clean up
  if (world) {
    world_destroy(world);
  }
  engine_destroy(engine);
  printf("Thanks for playing!\n");
}

// Generate a secure random seed as a hex string
static bool generate_secure_seed(char* buffer, size_t buffer_size) {
    if (buffer_size < 129) { // Need 128 chars for 64 bytes hex + null terminator
        return false;
    }

    unsigned char random_bytes[64];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return false;
    }

    bool success = false;
    if (read(fd, random_bytes, sizeof(random_bytes)) == sizeof(random_bytes)) {
        // Convert to hex string
        for (int i = 0; i < 64; i++) {
            snprintf(buffer + (i * 2), 3, "%02x", random_bytes[i]);
        }
        success = true;
    }

    close(fd);
    return success;
}

int main(int argc, char *argv[]) {
  printf("VERSE\n");

  // Command line arguments:
  // --seed, -s <seed>   : Specify world generation seed
  // --explore, -e       : Enable exploration mode
  // --size, -z <size>   : Specify world size (8-256, default: 32)
  // --server, -S        : Run as a server
  // --play, -p          : Enable play mode
  // --mud               : Run MUD (telnet) server
  // --port <port>       : Port for server modes (server / mud)

  char seed_buffer[129] = {0}; // 64 bytes hex + null terminator
  const char* seed = NULL;
  bool explore_mode_enabled = false;
  bool server_mode = false;
  bool play_mode_enabled = false;
  bool mud_mode = false;
  uint32_t size = 32; // Default size
  int port = 2323; // Default MUD / server port

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--seed") == 0 || strcmp(argv[i], "-s") == 0) {
      if (i + 1 < argc) {
        seed = argv[i + 1];
        i++; // Skip the next argument
      }
    } else if (strcmp(argv[i], "--explore") == 0 || strcmp(argv[i], "-e") == 0) {
      explore_mode_enabled = true;
    } else if (strcmp(argv[i], "--size") == 0 || strcmp(argv[i], "-z") == 0) {
      if (i + 1 < argc) {
        size = atoi(argv[i + 1]);
        if (size < 8) size = 8; // Enforce minimum size
        if (size > 256) size = 256; // Enforce maximum size
        i++; // Skip the next argument
      }
    } else if (strcmp(argv[i], "--server") == 0 || strcmp(argv[i], "-S") == 0) {
      server_mode = true;
    } else if (strcmp(argv[i], "--play") == 0 || strcmp(argv[i], "-p") == 0) {
      play_mode_enabled = true;
    } else if (strcmp(argv[i], "--mud") == 0) {
      mud_mode = true;
    } else if (strcmp(argv[i], "--port") == 0) {
      if (i + 1 < argc) {
        port = atoi(argv[++i]);
        if (port <= 0 || port > 65535) port = 2323;
      }
    }
  }

  // Generate secure random seed if none provided
  if (!seed) {
    if (!generate_secure_seed(seed_buffer, sizeof(seed_buffer))) {
      printf("Failed to generate secure random seed\n");
      return 1;
    }
    seed = seed_buffer;
  }

  if (mud_mode) {
    printf("Starting MUD (telnet) server on port %d...\n", port);
    // Use provided seed if any, else secure one already generated
    return run_mud_telnet_server(seed, size, port);
  } else if (server_mode) {
    printf("Starting network server...\n");
    return run_server();
  } else if (explore_mode_enabled) {
    explore_mode(seed, size);
  } else if (play_mode_enabled) {
    play_mode(seed, size);
  } else {
    // Standard mode
    printf("Using seed: %s\n", seed);
    printf("World size: %u\n", size);

    // Create a new world with specified size
    World* world = world_create(size, size, size);

    if (!world) {
      printf("Failed to create world\n");
      return 1;
    }

    // Generate a floating island
    printf("Generating world...\n");
    world_generate(world, seed);

    // Print multiple horizontal slices at different levels
    printf("Slices at different heights to show world generation with seed: %s\n", seed);
    print_world_slice(world, size / 2);     // Middle slice (stone)
    print_world_slice(world, size / 2 + 4); // Higher slice (dirt)
    print_world_slice(world, size / 2 + 8); // Near surface (grass and trees)

    // Clean up
    world_destroy(world);
  }

  return 0;
}
