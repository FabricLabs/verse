#include "window.h"
#include "world.h"
#include "character.h"
#include "background_music.h"
#include "world_transition.h"
#include "world_spawn.h"
#include "input_manager.h"
#include "item.h"
#include "currency.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <jansson.h>

// Global state for the interactive test
static int g_player_x = 32;
static int g_player_y = 8;  // Height (vertical position)
static int g_player_z = 32; // Depth (forward/backward position)
static int g_screen = 0;
static int g_running = 1;
int g_show_exit_prompt = 0;  // Track if exit prompt is shown
static int g_show_help_modal = 0;   // Track if help modal is shown
static int g_show_name_input = 0;   // Track if name input is shown
static int g_show_chapter = 0;      // Track if chapter is shown
static float g_chapter_fade_alpha = 0.0f;  // Chapter fade alpha (0.0 to 1.0)
static Uint32 g_chapter_fade_start_time = 0;  // When chapter fade started
static int g_chapter_current_char = 0;  // Current character in chapter text
static int g_chapter_total_chars = 0;   // Total characters in chapter text
static Uint32 g_chapter_last_text_update = 0; // Last chapter text update time
static bool g_chapter_text_complete = false;  // Whether chapter text has finished streaming
static int g_chapter_current_page = 0;  // Current chapter page
static int g_chapter_total_pages = 0;   // Total chapter pages
static int g_show_scene = 0;        // Track if scene is shown
static int g_show_quest = 0;        // Track if quest screen is shown
static int g_show_new_game_warning = 0; // Track if new game warning is shown
static int g_show_loading = 0;      // Track if loading screen is shown
static int g_settings_screen = 5;   // Settings screen ID
static char g_player_name[64] = ""; // Player character name
static int g_first_time = 1;        // Track if this is first time playing

// Startup sequence state
static int g_startup_state = 0;      // 0=black, 1=fade_in_title, 2=show_title, 3=fade_out_title, 4=main_menu
static Uint32 g_startup_start_time = 0;  // When startup sequence began
static float g_title_fade_alpha = 0.0f;  // Title fade alpha (0.0 to 1.0)
static const int TITLE_SHOW_DURATION = 5000;  // 5 seconds to show title
static const int TITLE_FADE_DURATION = 1000;  // 1 second fade in/out

// Loading state
static GameWorlds* g_game_worlds = NULL;
static int g_loading_current = 0;
static int g_loading_total = 0;
static char g_loading_message[256] = "";
static Uint32 g_loading_start_time = 0;
static Uint32 g_saving_start_time = 0;
static bool g_is_saving = false;

// Scene management
typedef struct
{
  char *type;
  char *content;
  char *quest_name;
  char *quest_description;
  int objectives_count;
  int completed_objectives;
} SceneElement;

typedef struct
{
  char *name;
  char *description;
  char *world;
  SceneElement *sequence;
  int sequence_count;
  int current_element;
  int current_char;
  int total_chars;
} Scene;

static Scene *g_current_scene = NULL;
static int g_scene_auto_advance = 0;  // Auto-advance character filtering
static int g_text_speed = 1;          // 0=Slow, 1=Medium, 2=Fast
static int g_text_streaming = 1;      // 1=streaming, 0=skip to end
static Uint32 g_last_text_update = 0; // Last text update time
static char g_status_message[256] = "Ready for input...";
static World *g_main_menu_world = NULL;
static World *g_game_world = NULL;
static int g_game_started = 0;
static int g_falling = 0;      // Track if player is falling
static int g_fall_start_y = 0; // Y position when fall started

// Forward declarations
World *create_main_menu_world();
World *create_game_world();
const char* get_chapter_content(int page, const char* player_name);

// Character attributes (immortal soul retains permanent attributes)
static int g_strength = 5;
static int g_dexterity = 5;
static int g_intelligence = 5;
static int g_wisdom = 5;
static int g_constitution = 5;
static int g_luck = 5;
static int g_experience_points = 0;

// Inventory items
static int g_gold = 0;
static int g_stone_blocks = 0;
static int g_wood_blocks = 0;
static int g_food_items = 0;
static int g_potions = 0;
static int g_weapons = 0;
static int g_armor = 0;

// Background music system
static BackgroundMusicSystem* g_background_music = NULL;
static InputManager g_input_manager;

// Save/load functionality
static void save_player_game() {
    if (strlen(g_player_name) > 0) {
        // Get current world seed
        char world_seed[65] = "";
        if (g_game_world) {
            // For now, use a default seed - in a real implementation,
            // we'd get the actual world seed from the world
            strcpy(world_seed, "default_game_seed");
        } else {
            strcpy(world_seed, "main_menu_seed");
        }

        // Save to character-specific file
        bool character_saved = character_save_game(g_player_name, g_player_x, g_player_y, g_player_z,
                               g_strength, g_dexterity, g_intelligence,
                               g_wisdom, g_constitution, g_luck,
                               g_experience_points, g_gold, world_seed, 0, 0, 0u, NULL);

        // Always save to default.save as well
        bool default_saved = character_save_default(g_player_name, g_player_x, g_player_y, g_player_z,
                               g_strength, g_dexterity, g_intelligence,
                               g_wisdom, g_constitution, g_luck,
                               g_experience_points, g_gold, world_seed, 0, 0, 0u, NULL);

        if (character_saved && default_saved) {
            snprintf(g_status_message, sizeof(g_status_message),
                    "Game saved successfully for %s (and to default.save)", g_player_name);
        } else if (character_saved) {
            snprintf(g_status_message, sizeof(g_status_message),
                    "Game saved for %s (default.save failed)", g_player_name);
        } else if (default_saved) {
            snprintf(g_status_message, sizeof(g_status_message),
                    "Game saved to default.save only");
        } else {
            snprintf(g_status_message, sizeof(g_status_message),
                    "Failed to save game for %s", g_player_name);
        }
    }
}

static bool load_player_game(const char* character_name) {
    CharacterSave save;
    if (character_load_game(character_name, &save)) {
        // Load character data
        strcpy(g_player_name, save.name);
        g_player_x = save.x;
        g_player_y = save.y;
        g_player_z = save.z;
        g_strength = save.strength;
        g_dexterity = save.dexterity;
        g_intelligence = save.intelligence;
        g_wisdom = save.wisdom;
        g_constitution = save.constitution;
        g_luck = save.luck;
        g_experience_points = save.experience_points;
        g_gold = save.gold;

        // Load the world based on the seed
        if (g_game_world) {
            world_destroy(g_game_world);
        }
        // For now, create a new game world - in a real implementation,
        // we'd recreate the world from the seed
        g_game_world = create_game_world();

        g_game_started = 1;
        g_first_time = 0;

        snprintf(g_status_message, sizeof(g_status_message),
                "Game loaded successfully for %s", character_name);
        return true;
    } else {
        snprintf(g_status_message, sizeof(g_status_message),
                "Failed to load game for %s", character_name);
        return false;
    }
}

// Loading progress callback
static void loading_progress_callback(int current, int total, const char* message) {
    g_loading_current = current;
    g_loading_total = total;
    if (message) {
        strncpy(g_loading_message, message, sizeof(g_loading_message) - 1);
        g_loading_message[sizeof(g_loading_message) - 1] = '\0';
    }

    // Render the loading bar immediately
    window_clear();
    window_render_main_menu(); // Show main menu as background
    window_render_loading_bar(current, total, message);
    window_present();

    // Small delay to show progress
    usleep(50000); // 50ms delay
}

// Collision detection functions
bool is_solid_voxel(VoxelType type)
{
  return type == VOXEL_SOIL || type == VOXEL_STONE || type == VOXEL_WOOD ||
         type == VOXEL_LEAVES || type == VOXEL_SAND;
}

bool is_walkable_voxel(VoxelType type)
{
  return type == VOXEL_GRASS || type == VOXEL_AIR;
}

bool can_move_to_position(World *world, int x, int y, int z)
{
  if (!world_is_position_valid(world, x, y, z))
  {
    return false;
  }

  // Check if the target position is solid (collision)
  Voxel *target_voxel = world_get_voxel(world, x, y, z);
  if (target_voxel && is_solid_voxel(target_voxel->type))
  {
    return false;
  }

  // Check if there's solid ground below the target position
  Voxel *ground_voxel = world_get_voxel(world, x, y - 1, z);
  if (!ground_voxel || !is_solid_voxel(ground_voxel->type))
  {
    return false; // Would fall!
  }

  return true;
}

// Find the grass surface level for starting position
int find_grass_surface(World *world, int x, int z)
{
  // Start from the top and work down to find grass
  for (int y = world->height - 1; y >= 0; y--)
  {
    Voxel *voxel = world_get_voxel(world, x, y, z);
    if (voxel && voxel->type == VOXEL_GRASS)
    {
      return y + 1; // Return the position above the grass
    }
  }
  return 8; // Fallback height if no grass found
}

// Scene management functions
Scene *load_scene(const char *scene_name)
{
  char filename[256];
  snprintf(filename, sizeof(filename), "scenes/%s.json", scene_name);

  FILE *file = fopen(filename, "r");
  if (!file)
  {
    printf("Failed to open scene file: %s\n", filename);
    return NULL;
  }

  // Read file content
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *json_content = malloc(file_size + 1);
  fread(json_content, 1, file_size, file);
  json_content[file_size] = '\0';
  fclose(file);

  // Parse JSON with jansson
  json_error_t error;
  json_t *root = json_loads(json_content, 0, &error);
  if (!root)
  {
    printf("Failed to parse scene JSON: %s - %s at position %d\n", filename, error.text, error.position);
    free(json_content);
    return NULL;
  }

  Scene *scene = malloc(sizeof(Scene));
  memset(scene, 0, sizeof(Scene));

  // Parse scene metadata
  json_t *name_obj = json_object_get(root, "name");
  json_t *desc_obj = json_object_get(root, "description");
  json_t *world_obj = json_object_get(root, "world");
  json_t *seq_obj = json_object_get(root, "sequence");

  if (name_obj && json_is_string(name_obj))
  {
    scene->name = strdup(json_string_value(name_obj));
  }
  if (desc_obj && json_is_string(desc_obj))
  {
    scene->description = strdup(json_string_value(desc_obj));
  }
  if (world_obj && json_is_string(world_obj))
  {
    scene->world = strdup(json_string_value(world_obj));
  }

  // Parse sequence
  if (seq_obj && json_is_array(seq_obj))
  {
    size_t array_len = json_array_size(seq_obj);
    scene->sequence = malloc(array_len * sizeof(SceneElement));
    scene->sequence_count = array_len;

    for (size_t i = 0; i < array_len; i++)
    {
      json_t *element = json_array_get(seq_obj, i);
      SceneElement *seq_elem = &scene->sequence[i];
      memset(seq_elem, 0, sizeof(SceneElement));

      json_t *type_obj = json_object_get(element, "type");
      json_t *content_obj = json_object_get(element, "content");
      json_t *quest_name_obj = json_object_get(element, "name");
      json_t *quest_desc_obj = json_object_get(element, "description");
      json_t *objectives_obj = json_object_get(element, "objectives");

      if (type_obj && json_is_string(type_obj))
      {
        seq_elem->type = strdup(json_string_value(type_obj));
      }
      if (content_obj && json_is_string(content_obj))
      {
        seq_elem->content = strdup(json_string_value(content_obj));
      }
      if (quest_name_obj && json_is_string(quest_name_obj))
      {
        seq_elem->quest_name = strdup(json_string_value(quest_name_obj));
      }
      if (quest_desc_obj && json_is_string(quest_desc_obj))
      {
        seq_elem->quest_description = strdup(json_string_value(quest_desc_obj));
      }
      if (objectives_obj && json_is_array(objectives_obj))
      {
        seq_elem->objectives_count = json_array_size(objectives_obj);
      }
    }
  }

  // Initialize scene state
  scene->current_element = 0;
  scene->current_char = 0;
  if (scene->sequence_count > 0 && scene->sequence[0].content)
  {
    scene->total_chars = strlen(scene->sequence[0].content);
  }

  json_decref(root);
  free(json_content);

  printf("Loaded scene: %s with %d elements\n", scene->name, scene->sequence_count);
  return scene;
}

void free_scene(Scene *scene)
{
  if (!scene)
    return;

  for (int i = 0; i < scene->sequence_count; i++)
  {
    free(scene->sequence[i].type);
    free(scene->sequence[i].content);
    free(scene->sequence[i].quest_name);
    free(scene->sequence[i].quest_description);
  }

  free(scene->sequence);
  free(scene->name);
  free(scene->description);
  free(scene->world);
  free(scene);
}

void update_text_streaming()
{
  if (!g_current_scene || !g_text_streaming)
    return;

  SceneElement *current_elem = &g_current_scene->sequence[g_current_scene->current_element];
  if (!current_elem || !current_elem->content)
    return;

  Uint32 current_time = SDL_GetTicks();
  int update_interval;

  // Set update interval based on text speed
  switch (g_text_speed)
  {
  case 0:                  // Slow
    update_interval = 100; // 100ms between characters
    break;
  case 1:                 // Medium
    update_interval = 50; // 50ms between characters
    break;
  case 2:                 // Fast
    update_interval = 20; // 20ms between characters
    break;
  default:
    update_interval = 50;
    break;
  }

  if (current_time - g_last_text_update >= update_interval)
  {
    if (g_current_scene->current_char < g_current_scene->total_chars)
    {
      g_current_scene->current_char++;
      g_last_text_update = current_time;
    }
  }
}

void update_chapter_text_streaming()
{
  if (!g_show_chapter || g_chapter_fade_alpha < 1.0f)
    return;

  Uint32 current_time = SDL_GetTicks();
  int update_interval;

  // Set update interval based on text speed
  switch (g_text_speed)
  {
  case 0:                  // Slow
    update_interval = 100; // 100ms between characters
    break;
  case 1:                 // Medium
    update_interval = 50; // 50ms between characters
    break;
  case 2:                 // Fast
    update_interval = 20; // 20ms between characters
    break;
  default:
    update_interval = 50;
    break;
  }

  if (current_time - g_chapter_last_text_update >= update_interval)
  {
    if (g_chapter_current_char < g_chapter_total_chars)
    {
      g_chapter_current_char++;
      g_chapter_last_text_update = current_time;
    }
    else
    {
      // Text streaming is complete
      g_chapter_text_complete = true;
    }
  }
}

void advance_scene()
{
  if (!g_current_scene)
    return;

  SceneElement *current_elem = &g_current_scene->sequence[g_current_scene->current_element];

  if (g_current_scene->current_char < g_current_scene->total_chars)
  {
    if (g_text_streaming)
    {
      // Streaming mode - advance one character
      g_current_scene->current_char++;
      g_scene_auto_advance = 0;
    }
    else
    {
      // Skip mode - jump to end of current text
      g_current_scene->current_char = g_current_scene->total_chars;
      g_text_streaming = 1; // Re-enable streaming for next element
    }
  }
  else
  {
    // Text complete - advance to next element
    g_current_scene->current_element++;
    g_current_scene->current_char = 0;

    if (g_current_scene->current_element >= g_current_scene->sequence_count)
    {
      // Scene complete
      free_scene(g_current_scene);
      g_current_scene = NULL;
      g_show_scene = 0;
      g_screen = 1; // Return to game world
      snprintf(g_status_message, sizeof(g_status_message), "Scene completed");
    }
    else
    {
      // Setup next element
      SceneElement *next_elem = &g_current_scene->sequence[g_current_scene->current_element];
      if (strcmp(next_elem->type, "text") == 0 && next_elem->content)
      {
        g_current_scene->total_chars = strlen(next_elem->content);
      }
      else if (strcmp(next_elem->type, "quest") == 0)
      {
        // For quest elements, we don't need character counting
        g_current_scene->total_chars = 0;
      }
    }
  }
}

// Create main menu world
World* create_main_menu_world()
{
  const char* seed = "main_menu_seed_verse_2024";

  // Try to load existing world first
  World* world = world_create(64, 64, 16);
  if (world && world_load_by_seed(world, seed)) {
    printf("Loaded existing main menu world from worlds/%s.world\n", seed);
    return world;
  }

  // Create new world if loading failed
  if (world) {
    world_generate(world, seed);
    world_save_by_seed(world, seed);
    printf("Created new main menu world\n");
  }

  return world;
}

// Create game world
World* create_game_world()
{
  const char* seed = "default_game_seed";

  // Try to load existing world first
  World* world = world_create(64, 16, 64);
  if (world && world_load_by_seed(world, seed)) {
    printf("Loaded existing game world from worlds/%s.world\n", seed);
    return world;
  }

  // Create new world if loading failed
  if (world) {
    world_generate(world, seed);
    world_save_by_seed(world, seed);
    printf("Created new game world\n");
  }

  return world;
}

// Find safe starting position on grass surface
void find_safe_starting_position(World *world, int *x, int *y, int *z)
{
  if (!world || !x || !y || !z) {
    printf("Invalid parameters for find_safe_starting_position\n");
    return;
  }

  // Use the new spawn system to find the best spawn position
  SpawnPosition spawn = world_find_best_spawn_position(world);

  if (spawn.is_safe) {
    *x = spawn.x;
    *y = spawn.y;
    *z = spawn.z;
    printf("Found safe spawn position: (%d, %d, %d) - %s\n",
           *x, *y, *z, spawn.spawn_reason);
  } else {
    // Fallback to old method if new system fails
    printf("New spawn system failed, using fallback method\n");
    *x = world->width / 2;
    *z = world->depth / 2;
    *y = find_grass_surface(world, *x, *z);

    // If no grass found at center, search nearby
    if (*y == 8) {
      for (int radius = 1; radius < 10; radius++) {
        for (int dx = -radius; dx <= radius; dx++) {
          for (int dz = -radius; dz <= radius; dz++) {
            int test_x = *x + dx;
            int test_z = *z + dz;

            if (world_is_position_valid(world, test_x, *y, test_z)) {
              int grass_y = find_grass_surface(world, test_x, test_z);
              if (grass_y != 8) {
                *x = test_x;
                *z = test_z;
                *y = grass_y;
                printf("Fallback spawn position: (%d, %d, %d)\n", *x, *y, *z);
                return;
              }
            }
          }
        }
      }
    }
  }

  // Clean up spawn reason
  if (spawn.spawn_reason) {
    free(spawn.spawn_reason);
  }
}

// Check if player should fall and handle falling
void check_and_handle_falling(World *world)
{
  if (!world || !g_game_started)
    return;

  // Check if player should transition to another world
  if (world_transition_should_transition(world, g_player_x, g_player_y, g_player_z))
  {
    // Create transition context
    WorldTransitionContext context = {
      .current_world = world,
      .player_x = g_player_x,
      .player_y = g_player_y,
      .player_z = g_player_z,
      .player = NULL, // We'll need to create a Player object from current state
      .transition_time = world->vector_clock
    };

    // Create a temporary Player object for the transition
    Player* temp_player = player_create(g_player_name, "Player", "temp_world", "temp_seed");
    if (temp_player) {
      // Set player position and stats from current state
      actor_set_position(&temp_player->base, (double)g_player_x, (double)g_player_y, (double)g_player_z);
      temp_player->base.health = 100; // Default health
      temp_player->base.experience = g_experience_points;
      temp_player->gold = g_gold;

      context.player = temp_player;

      // Perform the transition
      WorldTransitionResult result = world_transition_perform(&context);

      if (result.success) {
        // Update global state with new position and world
        g_player_x = result.new_x;
        g_player_y = result.new_y;
        g_player_z = result.new_z;

        // Update the current world pointer (this would need to be stored globally)
        // For now, we'll just update the status message
        snprintf(g_status_message, sizeof(g_status_message), "%s", result.transition_message);
        printf("World transition successful: %s\n", result.transition_message);

        // Reset falling state
        g_falling = 0;
      } else {
        // Transition failed, continue with normal falling
        snprintf(g_status_message, sizeof(g_status_message),
                "Transition failed: %s", result.transition_message);
        printf("World transition failed: %s\n", result.transition_message);
      }

      // Clean up temporary player
      player_destroy(temp_player);

      // Clean up result message
      if (result.transition_message) {
        free(result.transition_message);
      }

      return;
    }
  }

  // Normal falling logic (if no transition occurred)
  // Check if there's solid ground below the player
  Voxel *ground_voxel = world_get_voxel(world, g_player_x, g_player_y - 1, g_player_z);
  bool has_ground = ground_voxel && is_solid_voxel(ground_voxel->type);

  if (!has_ground && !g_falling)
  {
    // Start falling
    g_falling = 1;
    g_fall_start_y = g_player_y;
    snprintf(g_status_message, sizeof(g_status_message),
             "Falling! Gravity: %.2f m/s²", world_get_gravity(world));
    printf("Fall: Started falling from Y=%d\n", g_fall_start_y);
  }
  else if (!has_ground && g_falling)
  {
    // Continue falling
    g_player_y--;
    snprintf(g_status_message, sizeof(g_status_message),
             "Falling... Y=%d (Gravity: %.2f m/s²)", g_player_y, world_get_gravity(world));
    printf("Fall: Falling to Y=%d\n", g_player_y);
  }
  else if (has_ground && g_falling)
  {
    // Land
    int fall_distance = g_fall_start_y - g_player_y;
    g_falling = 0;
    snprintf(g_status_message, sizeof(g_status_message),
             "Landed! Fell %d blocks. Gravity: %.2f m/s²", fall_distance, world_get_gravity(world));
    printf("Fall: Landed after falling %d blocks\n", fall_distance);
  }
}

// Button callback function
void handle_button_click(int button_id)
{
  switch (button_id)
  {
  case BUTTON_CONTINUE:
    {
      // Load the first available save and continue the game
      CharacterSave saves[10];
      int save_count = character_list_saves(saves, 10);
      if (save_count > 0) {
        // Load the first save found
        if (load_player_game(saves[0].name)) {
          g_screen = 1; // Switch to game world
          g_game_started = 1;
          snprintf(g_status_message, sizeof(g_status_message), "Continued game with %s", saves[0].name);
          printf("Button: Continue selected - loaded %s\n", saves[0].name);
        } else {
          snprintf(g_status_message, sizeof(g_status_message), "Failed to load save file");
          printf("Button: Continue failed to load save\n");
        }
      } else {
        snprintf(g_status_message, sizeof(g_status_message), "No save files found");
        printf("Button: Continue - no saves found\n");
      }
    }
    break;
  case BUTTON_NEW_GAME:
    if (!g_game_started)
    {
      // TEMPORARY: Skip warning and start game directly for testing
      printf("Button: New Game selected - starting game directly (bypassing warning)\n");
      g_game_started = 1;
      g_screen = 1; // Switch to game world

      // Create game world and find safe starting position
      g_game_world = create_game_world();
      if (g_game_world)
      {
        find_safe_starting_position(g_game_world, &g_player_x, &g_player_y, &g_player_z);
        snprintf(g_status_message, sizeof(g_status_message), "New game started at (%d, %d, %d)!", g_player_x, g_player_y, g_player_z);
        printf("DEBUG: Game started with world at (%d, %d, %d)\n", g_player_x, g_player_y, g_player_z);
      }
      else
      {
        // Fallback to main menu world if game world creation fails
        find_safe_starting_position(g_main_menu_world, &g_player_x, &g_player_y, &g_player_z);
        snprintf(g_status_message, sizeof(g_status_message), "New game started (fallback) at (%d, %d, %d)!", g_player_x, g_player_y, g_player_z);
      }
    }
    break;
    case BUTTON_LOAD_GAME: {
    // Check for saved games
    CharacterSave saves[10];
    int save_count = character_list_saves(saves, 10);

    if (save_count > 0) {
      // For now, load the first available save
      // In a full implementation, we'd show a list of saves to choose from
      if (load_player_game(saves[0].name)) {
        g_screen = 1; // Switch to game world
        printf("Button: Load Game selected - loaded %s\n", saves[0].name);
      } else {
        snprintf(g_status_message, sizeof(g_status_message), "Failed to load saved game");
        printf("Button: Load Game selected - failed to load\n");
      }
    } else {
      snprintf(g_status_message, sizeof(g_status_message), "No saved games found");
      printf("Button: Load Game selected - no saves found\n");
    }
    break;
  }
  case BUTTON_SETTINGS:
    g_screen = g_settings_screen;
    snprintf(g_status_message, sizeof(g_status_message), "Settings screen opened");
    printf("Button: Settings selected\n");
    break;
  case BUTTON_EXIT:
    if (g_screen == 0)
    { // Only show prompt from main menu
      g_show_exit_prompt = 1;
      snprintf(g_status_message, sizeof(g_status_message), "Exit prompt shown");
      printf("Button: Exit selected - showing prompt\n");
    }
    else
    {
      g_running = 0;
      snprintf(g_status_message, sizeof(g_status_message), "Exiting game...");
      printf("Button: Exit selected - exiting directly\n");
    }
    break;
  case BUTTON_CONFIRM_EXIT:
    // Save the game before exiting
    save_player_game();
    g_running = 0;
    snprintf(g_status_message, sizeof(g_status_message), "Game saved and exit confirmed");
    printf("Button: Confirm exit selected - game saved\n");
    break;
  case BUTTON_CANCEL_EXIT:
    g_show_exit_prompt = 0;
    g_show_name_input = 0;
    snprintf(g_status_message, sizeof(g_status_message), "Action cancelled");
    printf("Button: Cancel selected\n");
    break;
  case BUTTON_CONFIRM_NEW_GAME:
    g_show_new_game_warning = 0;
    // Start world generation process
    g_show_loading = 1;
    g_loading_start_time = SDL_GetTicks();
    g_is_saving = false;

    // Set up the progress callback
    game_worlds_set_progress_callback(loading_progress_callback);

    // Create and generate all worlds
    g_game_worlds = game_worlds_create(g_player_name);
    if (g_game_worlds) {
        game_worlds_generate_all(g_game_worlds, g_player_name);
        g_saving_start_time = SDL_GetTicks();
        g_is_saving = true;
        game_worlds_save_all(g_game_worlds, g_player_name);
    }

    snprintf(g_status_message, sizeof(g_status_message), "World generation started");
    printf("Button: Confirm new game selected - starting world generation\n");
    break;
  case BUTTON_CANCEL_NEW_GAME:
    g_show_new_game_warning = 0;
    snprintf(g_status_message, sizeof(g_status_message), "New game cancelled");
    printf("Button: Cancel new game selected\n");
    break;
  case BUTTON_CLOSE_HELP:
    if (g_show_chapter)
    {
      // Chapter completed - start the scene
      g_show_chapter = 0;
      g_current_scene = load_scene("00000-genesis");
      if (g_current_scene)
      {
        g_show_scene = 1;
        snprintf(g_status_message, sizeof(g_status_message), "Starting scene: %s", g_current_scene->name);
        printf("Button: Chapter completed - starting scene\n");
      }
      else
      {
        // Fallback to game world if scene loading fails
        g_screen = 1;
        g_game_started = 1;
        g_game_world = create_game_world();
        if (g_game_world)
        {
          find_safe_starting_position(g_game_world, &g_player_x, &g_player_y, &g_player_z);
        }
        snprintf(g_status_message, sizeof(g_status_message), "Scene loading failed - starting game");
      }
    }
    else
    {
      g_show_help_modal = 0;
      g_show_scene = 0;
      snprintf(g_status_message, sizeof(g_status_message), "Modal closed");
      printf("Button: Close modal selected\n");
    }
    break;
  case BUTTON_CONFIRM_NAME:
    if (g_show_name_input && strlen(g_player_name) > 0)
    {
      g_show_name_input = 0;
      g_show_chapter = 1; // Show first chapter
      g_chapter_fade_alpha = 0.0f; // Start fade from 0
      g_chapter_fade_start_time = SDL_GetTicks(); // Start fade timer
      g_chapter_current_char = 0; // Start text streaming from beginning
      g_chapter_total_chars = 0; // Will be set when content is generated
      g_chapter_last_text_update = SDL_GetTicks(); // Initialize text update timer
      g_first_time = 0;   // Mark as not first time anymore
      save_player_game(); // Save immediately upon name confirmation
      snprintf(g_status_message, sizeof(g_status_message), "Character name confirmed and saved: %s", g_player_name);
      printf("Button: Confirm name selected - %s (game saved)\n", g_player_name);
      printf("DEBUG: Setting g_show_chapter = 1, g_first_time = 0\n");
    }
    break;
  case BUTTON_RESUME:
    // Return to game world from in-game menu
    g_screen = 1;
    snprintf(g_status_message, sizeof(g_status_message), "Returned to game world");
    printf("Button: Resume selected - returning to game\n");
    break;
  case BUTTON_SAVE_GAME:
    // Save the current game
    save_player_game();
    snprintf(g_status_message, sizeof(g_status_message), "Game saved successfully");
    printf("Button: Save Game selected - game saved\n");
    break;
  case BUTTON_MAIN_MENU:
    // Return to main menu from in-game menu
    g_screen = 0;
    snprintf(g_status_message, sizeof(g_status_message), "Returned to main menu");
    printf("Button: Main Menu selected - returning to main menu\n");
    break;
  default:
    snprintf(g_status_message, sizeof(g_status_message), "Unknown button clicked: %d", button_id);
    break;
  }
  printf("Status: %s\n", g_status_message);
}

// Key callback function
void handle_key_press(int key)
{
  // Use input manager to handle the key press
  if (input_manager_handle_key(&g_input_manager, key)) {
    // Input manager handled the key, check if we need to do anything else
    return;
  }

  // Fall back to original key handling for cases not covered by input manager
  // Prioritize name input when active
  if (g_show_name_input)
  {
    switch (key)
    {
    case SDLK_ESCAPE:
      g_show_name_input = 0;
      snprintf(g_status_message, sizeof(g_status_message), "Name input cancelled");
      break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      if (strlen(g_player_name) > 0)
      {
        g_show_name_input = 0;
        g_show_chapter = 1; // Show first chapter
        g_chapter_fade_alpha = 0.0f; // Start fade from 0
        g_chapter_fade_start_time = SDL_GetTicks(); // Start fade timer
        g_chapter_current_page = 0; // Start with first page
        g_chapter_current_char = 0; // Start text streaming from beginning
        g_chapter_total_chars = 0; // Will be set when content is generated
        g_chapter_text_complete = false; // Reset completion flag
        g_chapter_last_text_update = SDL_GetTicks(); // Initialize text update timer
        g_first_time = 0;   // Mark as not first time anymore
        save_player_game(); // Save immediately upon name confirmation
        snprintf(g_status_message, sizeof(g_status_message), "Character name confirmed and saved: %s", g_player_name);
        printf("Key: Enter pressed - name confirmed and saved: %s\n", g_player_name);
      }
      break;
    case SDLK_BACKSPACE:
      if (strlen(g_player_name) > 0)
      {
        g_player_name[strlen(g_player_name) - 1] = '\0';
        snprintf(g_status_message, sizeof(g_status_message), "Name: %s", g_player_name);
      }
      break;
    default:
      // Handle character input for name input
      if (key >= 32 && key <= 126 && strlen(g_player_name) < 63)
      {
        char ch = (char)key;
        g_player_name[strlen(g_player_name)] = ch;
        g_player_name[strlen(g_player_name) + 1] = '\0';
        snprintf(g_status_message, sizeof(g_status_message), "Name: %s", g_player_name);
      }
      break;
    }
    printf("Input: %s\n", g_status_message);
    return;
  }

  // Handle scene progression
  if (g_show_scene && g_current_scene)
  {
    SceneElement *current_elem = &g_current_scene->sequence[g_current_scene->current_element];

    if (g_current_scene->current_char < g_current_scene->total_chars)
    {
      // Text is still streaming - skip to end
      g_text_streaming = 0; // Disable streaming
      advance_scene();      // This will skip to the end
      snprintf(g_status_message, sizeof(g_status_message), "Text skipped to end");
    }
    else
    {
      // Text is complete - advance to next element
      advance_scene();
      snprintf(g_status_message, sizeof(g_status_message), "Scene advanced to next element");
    }
    printf("Input: %s\n", g_status_message);
    return;
  }

  // Handle chapter text advancement
  if (g_show_chapter)
  {
    if (g_chapter_current_char < g_chapter_total_chars)
    {
      // Text is still streaming - skip to end
      g_chapter_current_char = g_chapter_total_chars;
      g_chapter_text_complete = true;
      snprintf(g_status_message, sizeof(g_status_message), "Chapter text skipped to end");
    }
    else
    {
      // Text is complete - advance to next page
      g_chapter_current_page++;
      g_chapter_current_char = 0;
      g_chapter_total_chars = 0; // Will be set when new content is loaded
      g_chapter_text_complete = false;

      // Check if there are more pages
      const char* next_content = get_chapter_content(g_chapter_current_page, g_player_name);
      if (!next_content) {
          // No more pages - end chapter
          g_show_chapter = 0;
          g_screen = 1; // Return to game world
          snprintf(g_status_message, sizeof(g_status_message), "Chapter completed");
      } else {
          snprintf(g_status_message, sizeof(g_status_message), "Chapter page %d", g_chapter_current_page);
      }
    }
    printf("Input: %s\n", g_status_message);
    return;
  }

  // Handle quest screen dismissal
  if (g_show_quest)
  {
    g_show_quest = 0;
    g_screen = 1; // Return to game world
    snprintf(g_status_message, sizeof(g_status_message), "Quest screen dismissed");
    printf("Input: %s\n", g_status_message);
    return;
  }

  // Global key bindings (work from any screen)
  switch (key)
  {
  case SDLK_q:
    if (g_game_started)
    {
      g_show_quest = 1;
      snprintf(g_status_message, sizeof(g_status_message), "Quests opened");
    }
    else
    {
      snprintf(g_status_message, sizeof(g_status_message), "Start a new game first!");
    }
    break;
  case SDLK_c:
    if (g_game_started)
    {
      g_screen = 3; // Character sheet screen
      snprintf(g_status_message, sizeof(g_status_message), "Character sheet opened");
    }
    else
    {
      snprintf(g_status_message, sizeof(g_status_message), "Start a new game first!");
    }
    break;
  case SDLK_i:
    if (g_game_started)
    {
      g_screen = 4; // Inventory screen
      snprintf(g_status_message, sizeof(g_status_message), "Inventory opened");
    }
    else
    {
      snprintf(g_status_message, sizeof(g_status_message), "Start a new game first!");
    }
    break;
  case SDLK_s:
    if (g_game_started)
    {
      // TODO: Add skills screen (screen 6 or similar)
      snprintf(g_status_message, sizeof(g_status_message), "Skills opened");
    }
    else
    {
      snprintf(g_status_message, sizeof(g_status_message), "Start a new game first!");
    }
    break;
  case SDLK_UP:
    if (g_screen == g_settings_screen)
    {
      window_settings_prev_selection();
      snprintf(g_status_message, sizeof(g_status_message), "Settings selection changed");
    }
    else
    {
      window_prev_selection();
      snprintf(g_status_message, sizeof(g_status_message), "Previous selection");
    }
    break;
  case SDLK_DOWN:
    if (g_screen == g_settings_screen)
    {
      window_settings_next_selection();
      snprintf(g_status_message, sizeof(g_status_message), "Settings selection changed");
    }
    else
    {
      window_next_selection();
      snprintf(g_status_message, sizeof(g_status_message), "Next selection");
    }
    break;
  case SDLK_TAB:
    // If no selection, select first item, otherwise cycle through
    if (window_get_selected_button() == 0) {
      window_next_selection();
    } else {
      window_next_selection();
    }
    snprintf(g_status_message, sizeof(g_status_message), "Tab selection");
    break;
  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    if (g_screen == g_settings_screen)
    {
      // Handle settings selection
      int section = window_get_settings_section();
      int selection = window_get_settings_selection();

      if (section == 0)
      { // Video
        if (selection == 0)
        { // Scale
          int new_scale = (window_get_scale_factor() % MAX_SCALE_FACTOR) + 1;
          window_set_scale_factor(new_scale);
          snprintf(g_status_message, sizeof(g_status_message), "Scale changed to %dx", new_scale);
        }
        else if (selection == 2)
        { // Fullscreen
          window_toggle_fullscreen();
          snprintf(g_status_message, sizeof(g_status_message), "Fullscreen %s", window_is_fullscreen() ? "enabled" : "disabled");
        }
      }
      else if (section == 1)
      { // Sound
        if (selection == 0)
        { // Text speed
          g_text_speed = (g_text_speed + 1) % 3;
          const char *speed_names[] = {"Slow", "Medium", "Fast"};
          snprintf(g_status_message, sizeof(g_status_message), "Text speed changed to %s", speed_names[g_text_speed]);
        }
      }
      else if (section == 2)
      { // Preferences
        if (selection == 0)
        { // Tuning scale
          int new_scale = (window_get_tuning_scale() + 1) % 2;
          window_set_tuning_scale(new_scale);
          const char *tuning_names[] = {"432 Hz", "440 Hz"};
          snprintf(g_status_message, sizeof(g_status_message), "Tuning scale changed to %s", tuning_names[new_scale]);
        }
      }
    }
    else if (g_show_exit_prompt)
    {
      // Confirm exit - save game first
      save_player_game();
      g_running = 0;
      snprintf(g_status_message, sizeof(g_status_message), "Game saved and exit confirmed");
      printf("Key: Enter pressed on exit prompt - game saved and exiting\n");
    }
    else
    {
      // Activate the selected button for menu navigation
      window_activate_selection();
      snprintf(g_status_message, sizeof(g_status_message), "Activated selection");
    }
    break;
  case SDLK_w:
    if (g_game_started && g_screen == 1)
    {
      int new_z = g_player_z - 1;
      if (new_z >= 0)
      {
        World *current_world = g_game_world ? g_game_world : g_main_menu_world;
        if (can_move_to_position(current_world, g_player_x, g_player_y, new_z))
        {
          g_player_z = new_z;
          snprintf(g_status_message, sizeof(g_status_message), "Moved North to (%d, %d, %d)", g_player_x, g_player_y, g_player_z);
        }
        else
        {
          snprintf(g_status_message, sizeof(g_status_message), "You would fall! Cannot move North.");
        }
      }
    }
    break;
  case SDLK_x:
    if (g_game_started && g_screen == 1)
    {
      int new_z = g_player_z + 1;
      if (new_z < 64)
      {
        World *current_world = g_game_world ? g_game_world : g_main_menu_world;
        if (can_move_to_position(current_world, g_player_x, g_player_y, new_z))
        {
          g_player_z = new_z;
          snprintf(g_status_message, sizeof(g_status_message), "Moved South to (%d, %d, %d)", g_player_x, g_player_y, g_player_z);
        }
        else
        {
          snprintf(g_status_message, sizeof(g_status_message), "You would fall! Cannot move South.");
        }
      }
    }
    break;
  case SDLK_a:
    if (g_game_started && g_screen == 1)
    {
      int new_x = g_player_x - 1;
      if (new_x >= 0)
      {
        World *current_world = g_game_world ? g_game_world : g_main_menu_world;
        if (can_move_to_position(current_world, new_x, g_player_y, g_player_z))
        {
          g_player_x = new_x;
          snprintf(g_status_message, sizeof(g_status_message), "Moved West to (%d, %d, %d)", g_player_x, g_player_y, g_player_z);
        }
        else
        {
          snprintf(g_status_message, sizeof(g_status_message), "You would fall! Cannot move West.");
        }
      }
    }
    break;
  case SDLK_d:
    if (g_game_started && g_screen == 1)
    {
      int new_x = g_player_x + 1;
      if (new_x < 64)
      {
        World *current_world = g_game_world ? g_game_world : g_main_menu_world;
        if (can_move_to_position(current_world, new_x, g_player_y, g_player_z))
        {
          g_player_x = new_x;
          snprintf(g_status_message, sizeof(g_status_message), "Moved East to (%d, %d, %d)", g_player_x, g_player_y, g_player_z);
        }
        else
        {
          snprintf(g_status_message, sizeof(g_status_message), "You would fall! Cannot move East.");
        }
      }
    }
    break;
  case SDLK_SPACE:
    if (g_game_started)
    {
      snprintf(g_status_message, sizeof(g_status_message), "Interacting at (%d, %d, %d)", g_player_x, g_player_y, g_player_z);
    }
    break;

  case SDLK_n:
    snprintf(g_status_message, sizeof(g_status_message), "Opening navigation...");
    break;
  case SDLK_b:
    snprintf(g_status_message, sizeof(g_status_message), "Opening building mode...");
    break;
  case SDLK_h:
    if (g_game_started)
    {
      g_show_help_modal = 1;
      snprintf(g_status_message, sizeof(g_status_message), "Help modal opened");
    }
    else
    {
      snprintf(g_status_message, sizeof(g_status_message), "Start a new game first!");
    }
    break;
  case SDLK_ESCAPE:
    if (g_show_exit_prompt || g_show_help_modal || g_show_tutorial_modal || g_show_chapter || g_show_scene || g_show_new_game_warning || g_show_loading)
    {
      // Close any modal
      g_show_exit_prompt = 0;
      g_show_help_modal = 0;
      g_show_tutorial_modal = 0;
      g_show_chapter = 0;
      g_show_scene = 0;
      g_show_new_game_warning = 0;
      g_show_loading = 0;
      if (g_current_scene)
      {
        free_scene(g_current_scene);
        g_current_scene = NULL;
      }
      snprintf(g_status_message, sizeof(g_status_message), "Modal closed");
    }
    else if (g_screen == 3 || g_screen == 4)
    {
      // Return to game world from character sheet or inventory
      g_screen = 1;
      snprintf(g_status_message, sizeof(g_status_message), "Returned to game world");
    }
    else if (g_screen == g_settings_screen)
    {
      // Return to main menu from settings
      g_screen = 0;
      snprintf(g_status_message, sizeof(g_status_message), "Returned to main menu");
    }
    else if (g_screen == 5)
    {
      // Return to game world from in-game menu
      g_screen = 1;
      snprintf(g_status_message, sizeof(g_status_message), "Returned to game world");
    }
    else if (g_screen == 1)
    {
      // Show in-game menu from game world
      g_screen = 5; // In-game menu screen
      snprintf(g_status_message, sizeof(g_status_message), "In-game menu opened");
    }
    else if (g_screen == 0)
    {
      // Show exit prompt from main menu
      g_show_exit_prompt = 1;
      snprintf(g_status_message, sizeof(g_status_message), "Exit prompt shown");
    }
    else
    {
      // Exit program from other screens
      g_running = 0;
      snprintf(g_status_message, sizeof(g_status_message), "Exiting...");
    }
    break;

  default:
    snprintf(g_status_message, sizeof(g_status_message), "Key pressed: %d", key);
    break;
  }
  printf("Input: %s\n", g_status_message);
}

// Mouse callback function
void handle_mouse_click(int x, int y, int button)
{
  // Handle isomorphic mouse input when in game world
  if (g_screen == 1 && g_game_started) {
    // Convert screen coordinates to world coordinates for attack detection
    int world_x, world_y, world_z;
    if (window_screen_to_world_coords(x, y, &world_x, &world_y, &world_z)) {
      // Check if clicking on a weed (for tutorial attack objective)
      World *current_world = g_game_world ? g_game_world : g_main_menu_world;
      if (current_world) {
        Voxel* voxel = world_get_voxel(current_world, world_x, world_y, world_z);
        if (voxel && voxel->type == VOXEL_LEAVES) {
          // Attack the weed
          printf("Attacking weed at (%d, %d, %d)\n", world_x, world_y, world_z);
          window_update_tutorial_progress("attack");
          snprintf(g_status_message, sizeof(g_status_message), "Weed destroyed! +10 gold");
          g_gold += 10; // Reward for attacking
          printf("Mouse: %s\n", g_status_message);
          return;
        }
      }
    }

    window_handle_isomorphic_mouse(x, y, button);
    snprintf(g_status_message, sizeof(g_status_message), "Isomorphic mouse: (%d, %d) button %d", x, y, button);
  } else {
    snprintf(g_status_message, sizeof(g_status_message), "Mouse click at (%d, %d) with button %d", x, y, button);
  }
  printf("Mouse: %s\n", g_status_message);
}

// Window callback function
void handle_window_event(int event_type)
{
  snprintf(g_status_message, sizeof(g_status_message), "Window event: %d", event_type);
  printf("Window: %s\n", g_status_message);
}

// Determine the current input state based on game state
InputState determine_input_state() {
    if (g_show_exit_prompt) {
        return INPUT_STATE_EXIT_CONFIRMATION;
    }
    if (g_show_new_game_warning) {
        return INPUT_STATE_NEW_GAME_WARNING;
    }
    if (g_show_name_input) {
        return INPUT_STATE_NAME_INPUT;
    }
    if (g_show_loading) {
        return INPUT_STATE_LOADING;
    }
    if (g_show_scene) {
        return INPUT_STATE_SCENE;
    }
    if (g_show_quest) {
        return INPUT_STATE_QUEST;
    }
    if (g_show_chapter) {
        return INPUT_STATE_CHAPTER;
    }

    switch (g_screen) {
        case 0: // Main menu
            return INPUT_STATE_MAIN_MENU;
        case 1: // Game world
            return INPUT_STATE_GAME_WORLD;
        case 2: // Settings
            return INPUT_STATE_SETTINGS;
        case 5: // In-game menu
            return INPUT_STATE_IN_GAME_MENU;
        default:
            return INPUT_STATE_MAIN_MENU;
    }
}

// Update player movement toward destination
void update_player_movement() {
    if (g_movement_destination.has_destination && g_game_started) {
        // Calculate direction to destination
        int dx = g_movement_destination.target_x - g_player_x;
        int dy = g_movement_destination.target_y - g_player_y;
        int dz = g_movement_destination.target_z - g_player_z;

        // Check if we've reached the destination
        if (abs(dx) <= 1 && abs(dy) <= 1 && abs(dz) <= 1) {
            // Reached destination
            g_movement_destination.has_destination = false;
            g_movement_destination.path_progress = 1.0f;
            snprintf(g_status_message, sizeof(g_status_message), "Reached destination");
            return;
        }

        // Move toward destination (one step at a time)
        World *current_world = g_game_world ? g_game_world : g_main_menu_world;

        // Prioritize movement: X, then Z, then Y
        if (dx != 0) {
            int new_x = g_player_x + (dx > 0 ? 1 : -1);
            if (can_move_to_position(current_world, new_x, g_player_y, g_player_z)) {
                g_player_x = new_x;
                snprintf(g_status_message, sizeof(g_status_message), "Moving toward destination");
            }
        } else if (dz != 0) {
            int new_z = g_player_z + (dz > 0 ? 1 : -1);
            if (can_move_to_position(current_world, g_player_x, g_player_y, new_z)) {
                g_player_z = new_z;
                snprintf(g_status_message, sizeof(g_status_message), "Moving toward destination");
            }
        } else if (dy != 0) {
            int new_y = g_player_y + (dy > 0 ? 1 : -1);
            if (can_move_to_position(current_world, g_player_x, new_y, g_player_z)) {
                g_player_y = new_y;
                snprintf(g_status_message, sizeof(g_status_message), "Moving toward destination");
            }
        }

        // Update progress
        float total_distance = sqrt(dx*dx + dy*dy + dz*dz);
        float current_distance = sqrt((g_movement_destination.target_x - g_player_x)*(g_movement_destination.target_x - g_player_x) +
                                   (g_movement_destination.target_y - g_player_y)*(g_movement_destination.target_y - g_player_y) +
                                   (g_movement_destination.target_z - g_player_z)*(g_movement_destination.target_z - g_player_z));
        g_movement_destination.path_progress = 1.0f - (current_distance / total_distance);
    }
}

// Get chapter content by page
const char* get_chapter_content(int page, const char* player_name) {
    static char chapter_content[1024];

    switch (page) {
        case 0:
            snprintf(chapter_content, sizeof(chapter_content),
                "In the beginning, there was only darkness.\n\n"
                "Then, from the void, emerged the first spirits - "
                "ethereal beings of pure consciousness, free from "
                "the constraints of physical form.");
            break;
        case 1:
            snprintf(chapter_content, sizeof(chapter_content),
                "You are one of these ancient spirits, awakened "
                "to explore the newly formed worlds.\n\n"
                "Your journey begins in this realm of floating islands, "
                "where you will discover your true nature and the secrets "
                "that lie beyond the veil of reality.");
            break;
        case 2:
            snprintf(chapter_content, sizeof(chapter_content),
                "As a spirit, you are immortal but not invincible.\n\n"
                "You can traverse the worlds, interact with their "
                "inhabitants, and shape the very fabric of existence "
                "itself.");
            break;
        case 3:
            snprintf(chapter_content, sizeof(chapter_content),
                "But beware - not all spirits are benevolent, "
                "and the worlds you explore may hold dangers beyond "
                "your current understanding.\n\n"
                "Your name is %s, and your destiny awaits...",
                player_name);
            break;
        default:
            return NULL; // No more pages
    }

    return chapter_content;
}

// Enhanced interactive test program for the window system
int main()
{
  printf("=== VERSE Interactive Window Test ===\n");
  printf("Initializing window system...\n");

  // Initialize window
  if (!window_init(WINDOW_TITLE, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT))
  {
    printf("Failed to initialize window system!\n");
    return 1;
  }

  // Set up input callbacks
  window_set_key_callback(handle_key_press);
  window_set_mouse_callback(handle_mouse_click);
  window_set_window_callback(handle_window_event);
  window_set_button_callback(handle_button_click);

  // Initialize spawn system
  world_spawn_init();

  // Initialize tutorial quest (only on first load) - DISABLED FOR NOW
  // if (g_first_time) {
  //   window_init_tutorial_quest();
  // }

  printf("Window system initialized successfully!\n");

  // Check for saved games on startup
  printf("Checking for saved games...\n");
  CharacterSave saves[10];
  int save_count = character_list_saves(saves, 10);
  if (save_count > 0) {
    printf("Found %d saved game(s):\n", save_count);
    for (int i = 0; i < save_count; i++) {
      printf("  %s (saved %s)\n", saves[i].name, saves[i].save_timestamp);
    }
  } else {
    printf("No saved games found.\n");
  }

  printf("Creating main menu world...\n");

  // Create main menu world (no spawns, defined seed)
  g_main_menu_world = create_main_menu_world();
  if (!g_main_menu_world)
  {
    printf("Failed to create main menu world!\n");
    window_cleanup();
    return 1;
  }

  const char *player_name = "InteractivePlayer";

  printf("Starting interactive window test...\n");
  printf("Controls:\n");
  printf("  ESC - Exit\n");
  printf("  1-3 - Switch screens (Menu/World/Battle)\n");
  printf("  WASD - Move player (only in game world)\n");
  printf("  Space - Interact\n");
  printf("  I - Inventory\n");
  printf("  C - Character\n");
  printf("  N - Navigation\n");
  printf("  B - Building\n");
  printf("  Mouse - Click menu buttons\n");
  printf("  Enter - Confirm action\n");
  printf("\n");
  printf("IMPORTANT: Click 'New Game' button to start the actual game!\n");
  printf("\n");

  // Initialize background music system
  printf("Initializing background music system...\n");
  g_background_music = background_music_create(44100);
  if (g_background_music) {
    if (background_music_initialize(g_background_music, "assets/melodies")) {
      // Set up audio system
      if (window_setup_audio(g_background_music)) {
        background_music_start(g_background_music);
        printf("Background music system initialized successfully\n");
      } else {
        printf("Failed to set up audio system\n");
      }
    } else {
      printf("Failed to initialize background music system\n");
    }
  } else {
    printf("Failed to create background music system\n");
  }

  // Initialize input manager
  input_manager_init(&g_input_manager);
  printf("Input manager initialized\n");

  // Initialize startup sequence
  g_startup_state = 0;  // Start with black screen
  g_startup_start_time = SDL_GetTicks();
  g_title_fade_alpha = 0.0f;
  printf("Startup sequence initialized\n");

          // Auto-start removed - user should click "New Game" button
  printf("DEBUG: Auto-started new game\n");

  // Main window loop
  while (g_running)
  {
    // Update input manager state
    InputState current_input_state = determine_input_state();
    input_manager_set_state(&g_input_manager, current_input_state);

    // Handle events
    g_running = window_handle_events();

    // Check for falling if game is started
    if (g_game_started && g_screen == 1)
    {
      World *current_world = g_game_world ? g_game_world : g_main_menu_world;
      check_and_handle_falling(current_world);
    }

    // Update text streaming if scene is active
    if (g_show_scene && g_current_scene)
    {
      update_text_streaming();
    }

    // Update chapter text streaming if chapter is active
    if (g_show_chapter)
    {
      update_chapter_text_streaming();
    }

    // Update player movement toward destination
    update_player_movement();

    // Check for tutorial progress
    if (g_movement_destination.has_destination && g_tutorial_quest.is_active) {
        window_update_tutorial_progress("move");
    }

    // Handle startup sequence
    if (g_startup_state < 4) {
        // Startup sequence is active
        Uint32 current_time = SDL_GetTicks();
        Uint32 elapsed_time = current_time - g_startup_start_time;

        switch (g_startup_state) {
            case 0: // Black screen
                window_clear();
                if (elapsed_time >= 500) { // 0.5 second black screen
                    g_startup_state = 1;
                    g_startup_start_time = current_time;
                }
                break;

            case 1: // Fade in title
                if (elapsed_time < TITLE_FADE_DURATION) {
                    g_title_fade_alpha = (float)elapsed_time / (float)TITLE_FADE_DURATION;
                } else {
                    g_title_fade_alpha = 1.0f;
                    g_startup_state = 2;
                    g_startup_start_time = current_time;
                }
                window_render_title_screen(g_title_fade_alpha, 0);
                break;

            case 2: // Show title
                window_render_title_screen(1.0f, 0);
                if (elapsed_time >= TITLE_SHOW_DURATION) {
                    g_startup_state = 3;
                    g_startup_start_time = current_time;
                }
                break;

            case 3: // Fade out title
                if (elapsed_time < TITLE_FADE_DURATION) {
                    g_title_fade_alpha = 1.0f - ((float)elapsed_time / (float)TITLE_FADE_DURATION);
                } else {
                    g_title_fade_alpha = 0.0f;
                    g_startup_state = 4; // Move to main menu
                }
                window_render_title_screen(g_title_fade_alpha, 0);
                break;
        }
    } else {
        // Normal rendering - startup sequence complete
        switch (g_screen)
        {
        case 0:
          window_render_main_menu();
          break;
    case 1:
      if (g_game_started)
      {
        // Use game world if started, otherwise show main menu world
        World *current_world = g_game_world ? g_game_world : g_main_menu_world;
        window_render_isomorphic_world(current_world, g_player_x, g_player_y, g_player_z, player_name);
      }
      else
      {
        // Show main menu world as background
        window_render_isomorphic_world(g_main_menu_world, g_player_x, g_player_y, g_player_z, "Main Menu");
      }
      break;
    case 2:
      window_render_battle_interface(600, "Enemy: Interactive Monster\nHP: 100/100\nLevel: 5");
      break;
    case 3:
      window_render_character_sheet(player_name, g_strength, g_dexterity, g_intelligence,
                                    g_wisdom, g_constitution, g_luck, g_experience_points);
      break;
    case 4:
      {
        static Inventory demo_inv;
        static bool demo_inv_ready = false;
        if (!demo_inv_ready)
        {
          inventory_init(&demo_inv, INVENTORY_DEFAULT_SLOTS);
          if (g_food_items > 0)
            inventory_add(&demo_inv, ITEM_FOOD, (uint16_t)g_food_items);
          if (g_potions > 0)
            inventory_add(&demo_inv, ITEM_POTION, (uint16_t)g_potions);
          if (g_stone_blocks > 0)
            inventory_add(&demo_inv, ITEM_STONE_BLOCK, (uint16_t)g_stone_blocks);
          if (g_wood_blocks > 0)
            inventory_add(&demo_inv, ITEM_WOOD_BLOCK, (uint16_t)g_wood_blocks);
          if (g_weapons > 0)
            inventory_add(&demo_inv, ITEM_WEAPON_CLUB, (uint16_t)g_weapons);
          demo_inv_ready = true;
        }
        Wallet demo_purse;
        wallet_from_copper(&demo_purse, g_gold > 0 ? g_gold : 0);
        window_render_inventory(player_name, &demo_purse, &demo_inv, NULL, NULL);
      }
      break;
    case 5:
      window_render_in_game_menu();
      break;
        }
    }

    // Render modals if needed (overlays all screens) - only after startup sequence
    if (g_startup_state >= 4) {
        if (g_show_exit_prompt)
        {
          window_render_exit_prompt();
        }
    if (g_show_new_game_warning)
    {
      window_render_new_game_warning();
    }
    if (g_show_loading)
    {
      window_render_loading_bar(g_loading_current, g_loading_total, g_loading_message);
    }
    if (g_show_help_modal)
    {
      window_render_help_modal();
    }
    if (g_show_tutorial_modal)
    {
      window_render_tutorial_modal();
    }
    if (g_show_name_input)
    {
      window_render_name_input("Enter your character name:", g_player_name);
    }
    if (g_show_chapter)
    {
      // Update fade alpha based on time
      Uint32 current_time = SDL_GetTicks();
      Uint32 fade_duration = 1000; // 1 second fade-in
      Uint32 elapsed_time = current_time - g_chapter_fade_start_time;

      if (elapsed_time < fade_duration) {
        g_chapter_fade_alpha = (float)elapsed_time / (float)fade_duration;
      } else {
        g_chapter_fade_alpha = 1.0f;
      }

      printf("DEBUG: Rendering chapter page %d with fade alpha %.2f, g_player_name = '%s'\n",
             g_chapter_current_page, g_chapter_fade_alpha, g_player_name);
      const char *chapter_title = "Genesis";

      // Get content for current page
      const char* chapter_content = get_chapter_content(g_chapter_current_page, g_player_name);
      if (!chapter_content) {
          // No more pages - end chapter
          g_show_chapter = 0;
          g_screen = 1; // Return to game world
          continue;
      }

      // Set total characters if not already set
      if (g_chapter_total_chars == 0) {
          g_chapter_total_chars = strlen(chapter_content);
      }

      window_render_chapter_with_fade(chapter_title, chapter_content, g_chapter_fade_alpha, g_chapter_current_char, g_chapter_total_chars);

      // Add arrow if text is complete or being skipped
      if (g_chapter_text_complete || g_chapter_current_char >= g_chapter_total_chars) {
          // Render arrow in bottom right corner
          int arrow_x = 256 - 30;  // Base width - 30
          int arrow_y = 240 - 30;  // Base height - 30
          SDL_Color arrow_color = {255, 255, 255, (Uint8)(255 * g_chapter_fade_alpha)}; // White with fade
          window_render_arrow(arrow_x, arrow_y, arrow_color);
      }
        }
    }
    if (g_show_scene && g_current_scene)
    {
      SceneElement *current_elem = &g_current_scene->sequence[g_current_scene->current_element];

      if (strcmp(current_elem->type, "text") == 0)
      {
        // Render text element with character filtering
        window_render_scene_text_box(current_elem->content,
                                     g_current_scene->current_char,
                                     g_current_scene->total_chars);
      }
      else if (strcmp(current_elem->type, "quest") == 0)
      {
        // Render quest element
        window_render_quest_box(current_elem->quest_name,
                                current_elem->quest_description,
                                current_elem->completed_objectives,
                                current_elem->objectives_count);
      }
    }

    // Present the final rendered frame
    window_present();

    // Handle loading completion
    if (g_show_loading) {
        Uint32 current_time = SDL_GetTicks();

        if (g_is_saving) {
            // We're in the saving phase
            if (current_time - g_saving_start_time >= 3000) { // 3 seconds for saving
                // Complete the loading process
                g_show_loading = 0;
                g_show_chapter = 1; // Show first chapter
                g_chapter_fade_alpha = 0.0f; // Start fade from 0
                g_chapter_fade_start_time = SDL_GetTicks(); // Start fade timer
                g_first_time = 0;   // Mark as not first time anymore

                // Clean up game worlds
                if (g_game_worlds) {
                    game_worlds_destroy(g_game_worlds);
                    g_game_worlds = NULL;
                }

                    // Set up the game world for actual gameplay
  g_game_world = create_game_world();
  if (g_game_world) {
    find_safe_starting_position(g_game_world, &g_player_x, &g_player_y, &g_player_z);
    printf("DEBUG: Game world created, player spawn position: (%d, %d, %d)\n", g_player_x, g_player_y, g_player_z);

    // Debug: Check world dimensions and center
    printf("DEBUG: World dimensions: %dx%dx%d\n", g_game_world->width, g_game_world->height, g_game_world->depth);

    // Debug: Check if there are any voxels at the spawn position
    Voxel* spawn_voxel = world_get_voxel(g_game_world, g_player_x, g_player_y, g_player_z);
    if (spawn_voxel) {
      printf("DEBUG: Spawn voxel type: %d\n", spawn_voxel->type);
    } else {
      printf("DEBUG: No voxel found at spawn position!\n");
    }

    // Debug: Check a few positions around spawn
    for (int dx = -2; dx <= 2; dx++) {
      for (int dz = -2; dz <= 2; dz++) {
        Voxel* voxel = world_get_voxel(g_game_world, g_player_x + dx, g_player_y, g_player_z + dz);
        if (voxel && voxel->type != VOXEL_AIR) {
          printf("DEBUG: Found voxel at (%d, %d, %d): type %d\n",
                 g_player_x + dx, g_player_y, g_player_z + dz, voxel->type);
        }
      }
    }

    // Debug: Check if there are any voxels at all in the world
    int voxel_count = 0;
    for (int x = 0; x < g_game_world->width; x += 8) {
      for (int z = 0; z < g_game_world->depth; z += 8) {
        for (int y = 0; y < g_game_world->height; y++) {
          Voxel* voxel = world_get_voxel(g_game_world, x, y, z);
          if (voxel && voxel->type != VOXEL_AIR) {
            voxel_count++;
            if (voxel_count <= 10) { // Only print first 10
              printf("DEBUG: Found voxel at (%d, %d, %d): type %d\n", x, y, z, voxel->type);
            }
          }
        }
      }
    }
    printf("DEBUG: Total voxels found in world: %d\n", voxel_count);
  }

                snprintf(g_status_message, sizeof(g_status_message), "World generation complete! Starting your journey...");
                printf("Loading complete - transitioning to chapter\n");
            }
        } else {
            // We're in the generation phase
            if (current_time - g_loading_start_time >= 3000) { // 3 seconds for generation
                // Move to saving phase
                g_is_saving = true;
                g_saving_start_time = current_time;
                g_loading_current = g_loading_total; // Show 100% progress
                strcpy(g_loading_message, "Saving character...");
            }
        }
    }

    // Small delay to prevent excessive CPU usage
    usleep(16667); // ~60 FPS
  }

  printf("Cleaning up...\n");

  // Clean up background music system
  if (g_background_music) {
    background_music_destroy(g_background_music);
    g_background_music = NULL;
  }

  // Clean up audio system
  window_cleanup_audio();

  // Clean up spawn system
  world_spawn_cleanup();

  if (g_main_menu_world)
    world_destroy(g_main_menu_world);
  if (g_game_world)
    world_destroy(g_game_world);
  window_cleanup();

  printf("Interactive window test completed successfully!\n");
  return 0;
}
