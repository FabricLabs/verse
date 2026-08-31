// Save a game, load it, and be where you left off.
//
// That is the whole of the naive case, and it is what this pins down: one character, standing on the
// home island, having changed one voxel and earned one point of strength. Save, quit, load, and all
// three should still be true — the same world, the same spot in it, the same body.
//
// Everything runs in a temporary directory, because both save locations are relative to the working
// directory ("characters/" and "worlds/") and a test that writes into the repository is a test nobody
// runs twice.

#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "actor.h"
#include "character.h"
#include "console.h"
#include "constants.h"
#include "game_state.h"
#include "player_controls.h"
#include "shadow_world.h"
#include "universe.h"
#include "world.h"

GameState *g_game_state = NULL;
int g_show_exit_prompt = 0;
Console *g_console = NULL;
void handle_button_click(int button_id) { (void)button_id; }
void play_highlight_sound(void) {}

static int test_failures = 0;

static void report(const char *name, bool passed)
{
  printf("  %s %s\n", passed ? "ok  " : "FAIL", name);
  if (!passed)
    test_failures++;
}

// The character whose save this exercises. The name matters: game_worlds_create hashes it into the
// base seed, so the same name always means the same world, which is what makes a save findable again.
#define CHARACTER "savetest"

// Distinctive values, chosen so that a restored state cannot be confused with a freshly generated one.
#define MARK_X 5
#define MARK_Y 6
#define MARK_Z 7
#define MARK_VOXEL VOXEL_ORE_GOLD
#define STAND_X 10
#define STAND_Y 12
#define STAND_Z 20
#define STRENGTH 42

#define SIZE 32

// A scratch directory to save into, created under the working directory rather than /tmp so the test
// stays inside the tree it was built in. Anything left from a previous run is removed, so an old save
// cannot be mistaken for this run's.
static bool enter_temp_dir(char *out, size_t out_size)
{
  const char *dir = ".save-load-test";
  mkdir(dir, 0755); // may already exist, which is fine
  snprintf(out, out_size, "%s", dir);
  if (chdir(dir) != 0)
    return false;
  mkdir("characters", 0755);
  DIR *d = opendir("characters");
  if (d)
  {
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
      size_t n = strlen(e->d_name);
      if (n > 5 && strcmp(e->d_name + n - 5, ".save") == 0)
      {
        char path[256];
        snprintf(path, sizeof(path), "characters/%s", e->d_name);
        remove(path);
      }
    }
    closedir(d);
  }
  return true;
}

// A game state that looks like a game in progress on the home island: a home world in the universe at
// the home layer, a player standing on it, and a GameWorlds whose seed is derived from the character
// name the way the real new-game path derives it.
static GameState *make_running_game(World **out_home)
{
  GameState *state = (GameState *)calloc(1, sizeof(GameState));
  if (!state)
    return NULL;

  strncpy(state->player_name, CHARACTER, sizeof(state->player_name) - 1);

  if (!universe_init(&state->universe, UNIVERSE_SEED_HEX, WORLD_AFFINITY, 4))
  {
    free(state);
    return NULL;
  }

  state->game_worlds = game_worlds_create(CHARACTER);
  if (!state->game_worlds)
  {
    free(state);
    return NULL;
  }

  World *home = world_create(SIZE, SIZE, SIZE);
  if (!home)
  {
    free(state);
    return NULL;
  }
  world_generate_with_type(home, state->game_worlds->base_seed, WORLD_TYPE_HOME);

  state->game_worlds->home_world = home;
  state->current_world = home;
  state->game_started = true;
  state->current_screen = GAME_SCREEN_WORLD;
  state->player_universe_x = 0;
  state->player_universe_y = 0;
  state->player_universe_z = (uint64_t)UNIVERSE_HOME_Z;
  universe_place(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z, home);

  player_controls_init(&state->controls);
  state->player = actor_create(CHARACTER, "Ancient spirit", "home");

  if (out_home)
    *out_home = home;
  return state;
}

int main(void)
{
  setvbuf(stdout, NULL, _IONBF, 0); // so a crash mid-test still shows how far it got
  printf("=== Game Save and Load ===\n");

  char dir[PATH_MAX];
  if (!enter_temp_dir(dir, sizeof(dir)))
  {
    printf("could not create a temporary directory to work in\n");
    return 1;
  }
  printf("working in %s\n", dir);

  // ---------------------------------------------------------------------------------------------
  printf("\n-- saving a game in progress --\n");

  World *home = NULL;
  GameState *state = make_running_game(&home);
  if (!state || !home)
  {
    report("a game in progress was built", false);
    return 1;
  }
  g_game_state = state;

  char seed[128];
  snprintf(seed, sizeof(seed), "%s", state->game_worlds->base_seed);
  printf("       character %s -> world seed %.16s...\n", CHARACTER, seed);

  // Leave three marks: a change to the world, a place to be standing, and a change to the body.
  world_set_voxel(home, MARK_X, MARK_Y, MARK_Z, MARK_VOXEL);
  world_refresh_occupancy_bitfield(home);
  game_state_set_player_position(state, STAND_X, STAND_Y, STAND_Z);
  if (state->player)
    state->player->strength = STRENGTH;

  const bool saved = game_state_save_game(state);
  report("saving reports success", saved);
  report("a character save file was written", character_save_exists(CHARACTER));
  report("the save is findable as a saved game", game_state_has_saved_games());

  char world_file[256];
  snprintf(world_file, sizeof(world_file), "worlds/%s.HOME.world", seed);
  report("the home world was written to disk", access(world_file, F_OK) == 0);

  g_game_state = NULL;
  game_state_destroy(state);

  // ---------------------------------------------------------------------------------------------
  printf("\n-- loading it back --\n");

  GameState *loaded = (GameState *)calloc(1, sizeof(GameState));
  if (!loaded)
  {
    report("a fresh game state was built", false);
    return 1;
  }
  universe_init(&loaded->universe, UNIVERSE_SEED_HEX, WORLD_AFFINITY, 4);
  player_controls_init(&loaded->controls);
  g_game_state = loaded;

  const bool ok = game_state_load_game(loaded, CHARACTER);
  report("loading reports success", ok);
  report("the game is running after loading", loaded->game_started);
  report("the character name survived", strcmp(loaded->player_name, CHARACTER) == 0);

  if (loaded->game_worlds && loaded->game_worlds->base_seed)
  {
    printf("       loaded world seed %.16s...\n", loaded->game_worlds->base_seed);
    report("the loaded game uses the character's world seed",
           strcmp(loaded->game_worlds->base_seed, seed) == 0);
  }
  else
  {
    report("the loaded game uses the character's world seed", false);
  }

  World *world = loaded->current_world;
  report("a world is loaded", world != NULL);
  if (world)
  {
    printf("       loaded a %ux%ux%u world of generation type %d\n", world->width, world->height,
           world->depth, (int)world->generation_type);
    report("the loaded world is the home world", world->generation_type == WORLD_TYPE_HOME);

    Voxel *v = world_get_voxel(world, MARK_X, MARK_Y, MARK_Z);
    printf("       voxel (%d,%d,%d) is type %d, expected %d\n", MARK_X, MARK_Y, MARK_Z,
           v ? (int)v->type : -1, (int)MARK_VOXEL);
    report("the change made to the world came back", v && v->type == MARK_VOXEL);
  }

  printf("       player at voxel (%d,%d,%d), expected (%d,%d,%d)\n", loaded->player_voxel_x,
         loaded->player_voxel_y, loaded->player_voxel_z, STAND_X, STAND_Y, STAND_Z);
  report("the player is where they were standing",
         loaded->player_voxel_x == STAND_X && loaded->player_voxel_y == STAND_Y &&
             loaded->player_voxel_z == STAND_Z);
  report("the player is on the home layer",
         loaded->player_universe_z == (uint64_t)UNIVERSE_HOME_Z);

  if (loaded->player)
  {
    printf("       strength %u, expected %d\n", loaded->player->strength, STRENGTH);
    report("the body's stats came back", loaded->player->strength == STRENGTH);
  }
  else
  {
    report("the body's stats came back", false);
  }

  g_game_state = NULL;
  game_state_destroy(loaded);

  printf("\n-- the save browser lists and loads a chosen file --\n");

  CharacterSave extra;
  memset(&extra, 0, sizeof(extra));
  report("latest.save can be read as a template",
         character_load_from_path("characters/latest.save", &extra));
  extra.x = 3;
  extra.y = 4;
  extra.z = 5;
  strncpy(extra.save_timestamp, "1999-01-01 00:00:00", sizeof(extra.save_timestamp) - 1);
  {
    FILE *f = fopen("characters/savetest.19990101T000000.save", "wb");
    report("a second save file was written", f && fwrite(&extra, sizeof(extra), 1, f) == 1);
    if (f)
      fclose(f);
  }

  CharacterSaveEntry listed[CHARACTER_SAVE_MAX_LIST];
  int listed_n = character_list_save_files(listed, CHARACTER_SAVE_MAX_LIST);
  printf("       listed %d save files\n", listed_n);
  report("the listing sees more than one save", listed_n >= 2);

  bool found_extra = false;
  for (int i = 0; i < listed_n; i++)
  {
    if (listed[i].data.x == 3 && listed[i].data.y == 4 && listed[i].data.z == 5)
      found_extra = true;
  }
  report("the older position is in the listing", found_extra);

  GameState *browser = (GameState *)calloc(1, sizeof(GameState));
  report("a browser state was built", browser != NULL);
  if (browser)
  {
    universe_init(&browser->universe, UNIVERSE_SEED_HEX, WORLD_AFFINITY, 4);
    player_controls_init(&browser->controls);
    g_game_state = browser;
    game_state_open_save_browser(browser);
    report("opening the browser switches to that screen",
           browser->current_screen == GAME_SCREEN_SAVE_BROWSER);
    report("the browser lists the saves on disk", browser->save_browser_count >= 2);

    int extra_idx = -1;
    for (int i = 0; i < browser->save_browser_count; i++)
    {
      if (browser->save_browser_entries[i].data.z == 5)
        extra_idx = i;
    }
    report("the chosen save is among the rows", extra_idx >= 0);
    if (extra_idx >= 0)
    {
      browser->save_browser_selected = extra_idx;
      game_state_save_browser_move(browser, 0);
      bool loaded_choice = game_state_save_browser_load(browser);
      report("loading the selected row reports success", loaded_choice);
      printf("       loaded at (%d,%d,%d)\n", browser->player_voxel_x, browser->player_voxel_y,
             browser->player_voxel_z);
      report("the chosen save's position came back",
             browser->player_voxel_x == 3 && browser->player_voxel_y == 4 &&
                 browser->player_voxel_z == 5);
    }

    game_state_open_save_browser(browser);
    extra_idx = -1;
    for (int i = 0; i < browser->save_browser_count; i++)
    {
      if (strstr(browser->save_browser_entries[i].filename, "19990101"))
        extra_idx = i;
    }
    if (extra_idx >= 0)
    {
      int before = browser->save_browser_count;
      browser->save_browser_selected = extra_idx;
      bool deleted = game_state_save_browser_delete(browser);
      report("deleting a row removes the file", deleted);
      report("the listing is shorter after a delete", browser->save_browser_count < before);
    }
    else
    {
      report("deleting a row removes the file", false);
      report("the listing is shorter after a delete", false);
    }

    g_game_state = NULL;
    game_state_destroy(browser);
  }

  printf("\n%s (%d failure%s)\n", test_failures == 0 ? "PASS" : "FAIL", test_failures,
         test_failures == 1 ? "" : "s");
  return test_failures == 0 ? 0 : 1;
}
