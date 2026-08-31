#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>
#include "window.h"
#include "game_state.h"
#include "player_controls.h"
#include "isometric_renderer.h"
#include "console.h"
#include "particle_effects.h"
#include "background_music.h"
#include "songwriter/songwriter.h"
#include "ui_sounds.h"
#include "game_sfx.h"
#include "title_hum.h"
#include "storyline.h"
#include "dialogue.h"
#include "game_log.h"
#include "task_scheduler.h"
#include "material_atlas.h"
#include "mob_ai.h"
#include "item.h"
#include "gamepad.h"
#include "settlement.h"
#include "skill.h"
// Song editor removed - now standalone program

// Global game state
GameState *g_game_state = NULL;
Console *g_console = NULL;
static BackgroundMusicSystem *g_background_music = NULL;
static Songwriter *g_songwriter = NULL;
static UISoundSystem *g_ui_sounds = NULL;
static GameSfxSystem *g_game_sfx = NULL;
static TitleHumSystem *g_title_hum = NULL;
// Song editor removed - now standalone program

// Global variables expected by window system
int g_show_exit_prompt = 0;
static bool g_should_exit = false;

// Title screen state
static bool g_show_title_screen = true;
static float g_title_fade_alpha = 0.0f;
static Uint32 g_title_start_time = 0;

static World *client_active_world(void)
{
  if (!g_game_state)
    return NULL;
  return g_game_state->current_world ? g_game_state->current_world : g_game_state->main_menu_world;
}

static void client_apply_world_editor_hit(int hit, float bar_t)
{
  World *world = client_active_world();
  if (!world || !g_game_state)
    return;

  switch (hit)
  {
  case WINDOW_WORLD_EDITOR_HIT_GRAVITY_DEC:
    world_set_gravity(world, world_get_gravity(world) - GRAVITY_STEP);
    break;
  case WINDOW_WORLD_EDITOR_HIT_GRAVITY_INC:
    world_set_gravity(world, world_get_gravity(world) + GRAVITY_STEP);
    break;
  case WINDOW_WORLD_EDITOR_HIT_GRAVITY_BAR:
    world_set_gravity(world, GRAVITY_MIN + bar_t * (GRAVITY_MAX - GRAVITY_MIN));
    break;
  case WINDOW_WORLD_EDITOR_HIT_RESET:
    world_set_gravity(world, GRAVITY_DEFAULT);
    break;
  case WINDOW_WORLD_EDITOR_HIT_CLOSE:
    g_game_state->show_world_editor_modal = false;
    break;
  default:
    break;
  }
}

// Menu backdrop (HOME world) is generated on a worker while the title plays so leaving the
// title does not stall on world_create + generate.
static TaskGroup *g_menu_backdrop_group = NULL;
static bool g_menu_backdrop_ok = false;

static void menu_backdrop_init_task(void *user_data)
{
  GameState *state = (GameState *)user_data;
  g_menu_backdrop_ok = game_state_init(state);
}

static bool ensure_menu_backdrop_ready(void)
{
  if (g_menu_backdrop_group)
  {
    task_group_wait(g_menu_backdrop_group);
    task_group_destroy(g_menu_backdrop_group);
    g_menu_backdrop_group = NULL;
  }
  return g_menu_backdrop_ok && g_game_state && g_game_state->main_menu_world;
}

static void warm_material_atlas_if_needed(void)
{
  extern IsometricRenderer *g_isometric_renderer;
  if (!g_isometric_renderer || !window_state.renderer)
    return;
  if (!g_isometric_renderer->subvoxel_detail_enabled)
    return;
  if (g_isometric_renderer->material_atlas || g_isometric_renderer->material_atlas_tried)
    return;
  g_isometric_renderer->material_atlas_tried = true;
  g_isometric_renderer->material_atlas = material_atlas_create(window_state.renderer);
}

// Forward declarations
void handle_button_click(int button_id);
void handle_mouse_motion(int x, int y);
static void begin_loaded_game_story(void);

// Sound function declarations (available globally)
void play_highlight_sound();
void play_select_sound();
void play_error_sound();
void play_button_sound(); // Legacy compatibility

// UI Sound functions
void play_highlight_sound()
{
  if (g_ui_sounds)
  {
    ui_sounds_play_highlight(g_ui_sounds);
  }
}

void play_select_sound()
{
  if (g_ui_sounds)
  {
    ui_sounds_play_select(g_ui_sounds);
  }
}

void play_error_sound()
{
  if (g_ui_sounds)
  {
    ui_sounds_play_error(g_ui_sounds);
  }
}

// Legacy function for compatibility
void play_button_sound()
{
  play_select_sound();
}

// Button IDs (using different values to avoid conflicts)
#define CLIENT_BUTTON_NEW_GAME 100
#define CLIENT_BUTTON_CONTINUE 101
#define CLIENT_BUTTON_LOAD_GAME 102
#define CLIENT_BUTTON_SETTINGS 103
// Song editor button removed - now standalone program
#define CLIENT_BUTTON_EXIT 104

// Input handling
void handle_key_press(int key)
{
  if (!g_game_state)
    return;

  printf("Key press: %d\n", key);

  // Developer console (tilde / backtick). Capture while open; still allow toggle during play.
  // Skip during name entry so backtick can be typed into the name field if desired.
  // Global chat also captures keys while open (except tilde still reaches console toggle first).
  if (g_console && !g_game_state->show_name_input)
  {
    if (console_handle_key(g_console, key))
      return;
  }

  // Global chat: Enter opens; Enter sends; Esc cancels; Backspace edits.
  if (g_game_state->game_started &&
      g_game_state->current_screen == GAME_SCREEN_WORLD &&
      !g_game_state->show_name_input &&
      !dialogue_active(&g_game_state->dialogue) &&
      !(g_console && console_is_open(g_console)))
  {
    if (game_log_chat_is_open(&g_game_state->game_log))
    {
      if (key == SDLK_ESCAPE)
      {
        game_log_chat_close(&g_game_state->game_log);
        window_stop_text_input();
        return;
      }
      if (key == SDLK_BACKSPACE)
      {
        game_log_chat_backspace(&g_game_state->game_log);
        return;
      }
      if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
      {
        const char *name = g_game_state->player_name[0] ? g_game_state->player_name : "You";
        game_log_chat_send(&g_game_state->game_log, name);
        window_stop_text_input();
        return;
      }
      // Swallow gameplay keys while composing.
      return;
    }
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
    {
      game_log_chat_open(&g_game_state->game_log);
      window_start_text_input();
      return;
    }
  }

  // Loot corpses with F while exploring. Skip title / name entry / editor so F types normally there.
  if (key == SDLK_f && !g_show_title_screen && !g_game_state->show_name_input &&
      !g_game_state->show_world_editor_modal &&
      g_game_state->current_screen == GAME_SCREEN_WORLD)
  {
    if (!player_controls_try_craft(g_game_state))
      player_controls_try_loot(g_game_state);
    return;
  }

  // Recipe book
  if (key == SDLK_b && !g_show_title_screen && !g_game_state->show_name_input &&
      !g_game_state->show_world_editor_modal &&
      g_game_state->game_started &&
      (g_game_state->current_screen == GAME_SCREEN_WORLD ||
       g_game_state->current_screen == GAME_SCREEN_INVENTORY ||
       g_game_state->current_screen == GAME_SCREEN_CRAFT))
  {
    player_controls_open_recipe_book(g_game_state);
    return;
  }

  // Handle name input screen
  if (g_game_state->show_name_input)
  {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
    {
      // Simulate clicking the Confirm button
      printf("ENTER pressed - simulating Confirm button click\n");
      handle_button_click(BUTTON_CONFIRM_NAME);
      return;
    }
    else if (key == SDLK_BACKSPACE)
    {
      // Handle backspace - remove last character
      int len = strlen(g_game_state->player_name);
      if (len > 0)
      {
        g_game_state->player_name[len - 1] = '\0';
        printf("Name after backspace: '%s'\n", g_game_state->player_name);
      }
      return;
    }
    else if (key == SDLK_ESCAPE)
    {
      // Simulate clicking the Cancel button
      printf("ESCAPE pressed - simulating Cancel button click\n");
      handle_button_click(BUTTON_CANCEL_EXIT);
      return;
    }
    else if (key == SDLK_TAB)
    {
      // Tab navigation between buttons
      printf("TAB pressed - cycling button selection\n");
      window_next_selection();
      return;
    }
    return; // Don't process other keys when in name input mode
  }

  // World editor modal (opened by the `admin` console command).
  if (g_game_state->show_world_editor_modal)
  {
    if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER)
    {
      g_game_state->show_world_editor_modal = false;
      return;
    }
    if (key == SDLK_LEFT || key == SDLK_MINUS || key == SDLK_KP_MINUS)
    {
      client_apply_world_editor_hit(WINDOW_WORLD_EDITOR_HIT_GRAVITY_DEC, 0.0f);
      return;
    }
    if (key == SDLK_RIGHT || key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS)
    {
      client_apply_world_editor_hit(WINDOW_WORLD_EDITOR_HIT_GRAVITY_INC, 0.0f);
      return;
    }
    if (key == SDLK_r)
    {
      client_apply_world_editor_hit(WINDOW_WORLD_EDITOR_HIT_RESET, 0.0f);
      return;
    }
    return;
  }

  // Handle title screen
  if (g_show_title_screen)
  {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
    {
      if (!ensure_menu_backdrop_ready())
      {
        printf("Failed to initialize game state\n");
        g_should_exit = true;
        return;
      }
      warm_material_atlas_if_needed();
      g_show_title_screen = false;
      g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
      window_set_title_mode(false);
      // Stop the title hum once leaving the title screen
      if (g_title_hum) {
        title_hum_stop(g_title_hum);
      }
      // Begin subtle ambient bed; combat intensity will raise it later.
      if (g_background_music && window_state.background_music_enabled) {
        background_music_play_ambient(g_background_music);
      }
      return;
    }
    return;
  }

  // Handle chapter progression
  if (g_game_state->current_screen == GAME_SCREEN_CHAPTER && g_game_state->show_chapter)
  {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
    {
      game_state_advance_chapter(g_game_state);
      return;
    }
    return;
  }

  // Handle scene (in-world dialogue) progression
  if (g_game_state->current_screen == GAME_SCREEN_SCENE && g_game_state->show_scene)
  {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
    {
      game_state_advance_scene(g_game_state);
      return;
    }
    return;
  }

  // Live mob/NPC talk stays on the world screen; Enter/Space advance or confirm choice,
  // arrows / 1-4 pick choices, Esc dismisses.
  // Town Portal's channel dialogue is hold-T only — Enter must not dismiss it mid-cast.
  if (g_game_state->game_started &&
      g_game_state->current_screen == GAME_SCREEN_WORLD &&
      dialogue_active(&g_game_state->dialogue))
  {
    if (player_controls_town_portal_charging(&g_game_state->controls))
    {
      if (key == SDLK_ESCAPE)
      {
        player_controls_cancel_town_portal(g_game_state, &g_game_state->controls);
        return;
      }
      if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
        return;
    }
    else if (dialogue_awaiting_choice(&g_game_state->dialogue))
    {
      if (key == SDLK_UP || key == SDLK_w)
      {
        dialogue_choice_move(&g_game_state->dialogue, -1);
        return;
      }
      if (key == SDLK_DOWN || key == SDLK_s)
      {
        dialogue_choice_move(&g_game_state->dialogue, 1);
        return;
      }
      if (key >= SDLK_1 && key <= SDLK_4)
      {
        int idx = (int)(key - SDLK_1);
        if (idx < dialogue_choice_count(&g_game_state->dialogue))
        {
          g_game_state->dialogue.selected_choice = idx;
          game_state_apply_dialogue_choice(g_game_state);
        }
        return;
      }
      if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
      {
        game_state_apply_dialogue_choice(g_game_state);
        return;
      }
      if (key == SDLK_ESCAPE)
      {
        dialogue_close(&g_game_state->dialogue);
        return;
      }
    }
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
    {
      dialogue_advance(&g_game_state->dialogue);
      return;
    }
    else if (key == SDLK_ESCAPE)
    {
      dialogue_close(&g_game_state->dialogue);
      return;
    }
  }

  // Song editor removed - now standalone program

  // Handle game state input
  game_state_handle_input(g_game_state, key);

  // Handle function keys (F1-F4) for in-game selection system
  if (g_game_state->current_screen == GAME_SCREEN_WORLD && g_game_state->game_started)
  {
    switch (key)
    {
    case SDLK_F1:
      printf("F1 pressed - selecting hero character\n");
      {
        // Create hero selection data with position and name
        struct {
          int coords[3];
          char name[64];
        } hero_data;

        hero_data.coords[0] = g_game_state->player_x;
        hero_data.coords[1] = g_game_state->player_y;
        hero_data.coords[2] = g_game_state->player_z;
        strncpy(hero_data.name, g_game_state->player_name, sizeof(hero_data.name) - 1);
        hero_data.name[sizeof(hero_data.name) - 1] = '\0';

        window_set_selection(SELECTION_HERO, &hero_data);
        play_select_sound();
      }
      return; // Don't process other keys when F1 is pressed

    case SDLK_F2:
      printf("F2 pressed - selecting pet (placeholder)\n");
      window_set_selection(SELECTION_PET, "Faithful Companion");
      play_select_sound();
      return; // Don't process other keys when F2 is pressed

    case SDLK_F3:
      printf("F3 pressed - teleport home (placeholder)\n");
      window_set_selection(SELECTION_TELEPORT_HOME, "Home");
      play_select_sound();
      return; // Don't process other keys when F3 is pressed

    case SDLK_F4:
      // Isometric / first-person camera toggle (moved off F so F can loot corpses).
      window_toggle_render_mode();
      return; // Don't process other keys when F4 is pressed

    case SDLK_1:
    case SDLK_KP_1:
      if (!player_controls_use_hotbar_slot(g_game_state, &g_game_state->controls, 0))
      {
        if (!g_game_state->status_message[0])
          snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                   "Skill 1 not ready");
      }
      return;
    case SDLK_2:
    case SDLK_KP_2:
      if (!player_controls_use_hotbar_slot(g_game_state, &g_game_state->controls, 1))
      {
        if (!g_game_state->status_message[0])
          snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                   "Skill 2 not ready");
      }
      return;
    case SDLK_3:
    case SDLK_KP_3:
      player_controls_use_hotbar_slot(g_game_state, &g_game_state->controls, 2);
      return;
    case SDLK_4:
    case SDLK_KP_4:
      player_controls_use_hotbar_slot(g_game_state, &g_game_state->controls, 3);
      return;
    }
  }

  // Handle global keys
  switch (key)
  {
  case SDLK_i:
    if (g_game_state->game_started)
    {
      if (g_game_state->current_screen == GAME_SCREEN_LOOT)
      {
        player_controls_close_loot(g_game_state);
      }
      else if (g_game_state->current_screen == GAME_SCREEN_SHOP)
      {
        game_state_close_shop(g_game_state);
      }
      else if (g_game_state->current_screen == GAME_SCREEN_CRAFT)
      {
        player_controls_close_craft(g_game_state);
      }
      else if (g_game_state->current_screen == GAME_SCREEN_WORLD)
      {
        g_game_state->current_screen = GAME_SCREEN_INVENTORY;
        snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                 "Inventory opened");
      }
      else if (g_game_state->current_screen == GAME_SCREEN_INVENTORY)
      {
        g_game_state->current_screen = GAME_SCREEN_WORLD;
      }
      else if (g_game_state->current_screen == GAME_SCREEN_CHARACTER ||
               g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL ||
               g_game_state->current_screen == GAME_SCREEN_MAP)
      {
        if (g_game_state->current_screen == GAME_SCREEN_CHARACTER)
          window_character_skills_reset_ui();
        g_game_state->current_screen = GAME_SCREEN_INVENTORY;
      }
    }
    break;
  case SDLK_j:
    if (g_game_state->game_started)
    {
      if (g_game_state->current_screen == GAME_SCREEN_WORLD)
      {
        g_game_state->current_screen = GAME_SCREEN_QUEST_JOURNAL;
        g_game_state->quest_journal_scroll = 0;
        snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                 "Mission journal");
      }
      else if (g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL)
      {
        g_game_state->current_screen = GAME_SCREEN_WORLD;
      }
      else if (g_game_state->current_screen == GAME_SCREEN_INVENTORY ||
               g_game_state->current_screen == GAME_SCREEN_CHARACTER ||
               g_game_state->current_screen == GAME_SCREEN_MAP)
      {
        if (g_game_state->current_screen == GAME_SCREEN_CHARACTER)
          window_character_skills_reset_ui();
        g_game_state->current_screen = GAME_SCREEN_QUEST_JOURNAL;
        g_game_state->quest_journal_scroll = 0;
      }
    }
    break;
  case SDLK_t:
    if (g_game_state->game_started &&
        g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL &&
        storyline_has_active_quest(&g_game_state->storyline))
    {
      g_game_state->mission_tracked = !g_game_state->mission_tracked;
      snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
               g_game_state->mission_tracked ? "Mission tracked" : "Mission untracked");
      play_select_sound();
    }
    break;
  case SDLK_m:
    if (g_game_state->game_started)
    {
      if (g_game_state->current_screen == GAME_SCREEN_WORLD)
      {
        g_game_state->current_screen = GAME_SCREEN_MAP;
        // Reveal settlements already loaded in the neighbourhood.
        if (g_game_state->current_world)
        {
          game_state_try_discover_current_settlement(g_game_state);
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
              const int gx = (int)(int64_t)g_game_state->player_universe_x + dx;
              const int gy = (int)(int64_t)g_game_state->player_universe_y + dy;
              if (universe_settlement_scale(gx, gy) > 0)
                game_state_discover_settlement_waypoint(g_game_state, gx, gy);
            }
        }
        snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                 "Map");
      }
      else if (g_game_state->current_screen == GAME_SCREEN_MAP)
      {
        g_game_state->current_screen = GAME_SCREEN_WORLD;
      }
      else if (g_game_state->current_screen == GAME_SCREEN_INVENTORY ||
               g_game_state->current_screen == GAME_SCREEN_CHARACTER ||
               g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL)
      {
        if (g_game_state->current_screen == GAME_SCREEN_CHARACTER)
          window_character_skills_reset_ui();
        g_game_state->current_screen = GAME_SCREEN_MAP;
      }
    }
    break;
  case SDLK_g: // toggle gravity / flying
    {
      // Prefer GameState flag to not depend on Actor allocation timing
      g_game_state->player_flying = !g_game_state->player_flying;
      if (g_game_state->player) {
        g_game_state->player->is_flying = g_game_state->player_flying;
      }
      const char* mode = g_game_state->player_flying ? "Flying ON" : "Flying OFF";
      // Set toast message
      strncpy(g_game_state->toast_text, mode, sizeof(g_game_state->toast_text)-1);
      g_game_state->toast_text[sizeof(g_game_state->toast_text)-1] = '\0';
      g_game_state->toast_start_ms = SDL_GetTicks();
      g_game_state->toast_duration_ms = 4000; // total display time adjusted in renderer (3s hold + 1s fade)
      g_game_state->toast_y_offset = 0.0f;
      g_game_state->toast_active = true;

      // Also set engine/player status to ensure engine-side physics can respect flying
      if (g_game_state->engine) {
        // For now, mirror the state onto Actor if present
        if (g_game_state->player) {
          g_game_state->player->is_flying = g_game_state->player_flying;
        }
      }
    }
    break;
  case SDLK_ESCAPE:
    printf("Escape key pressed\n");
    if (g_game_state->current_screen == GAME_SCREEN_SAVE_BROWSER)
    {
      if (g_game_state->save_browser_confirm_delete)
        g_game_state->save_browser_confirm_delete = false;
      else
        g_game_state->current_screen = g_game_state->game_started ? GAME_SCREEN_IN_GAME_MENU
                                                                  : GAME_SCREEN_MAIN_MENU;
    }
    else if (g_game_state->current_screen == GAME_SCREEN_LOOT)
    {
      player_controls_close_loot(g_game_state);
    }
    else if (g_game_state->current_screen == GAME_SCREEN_SHOP)
    {
      game_state_close_shop(g_game_state);
    }
    else if (g_game_state->current_screen == GAME_SCREEN_CRAFT)
    {
      player_controls_close_craft(g_game_state);
    }
    else if (g_game_state->current_screen == GAME_SCREEN_INVENTORY ||
             g_game_state->current_screen == GAME_SCREEN_CHARACTER ||
             g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL ||
             g_game_state->current_screen == GAME_SCREEN_LEGENDS ||
             g_game_state->current_screen == GAME_SCREEN_MAP)
    {
      if (g_game_state->current_screen == GAME_SCREEN_CHARACTER &&
          window_character_skills_is_open())
      {
        window_character_skills_close();
      }
      else
      {
        if (g_game_state->current_screen == GAME_SCREEN_CHARACTER)
          window_character_skills_reset_ui();
        g_game_state->current_screen = GAME_SCREEN_WORLD;
      }
    }
    else if (g_game_state->current_screen == GAME_SCREEN_WORLD)
    {
      g_game_state->current_screen = GAME_SCREEN_IN_GAME_MENU;
      window_clear_selection();
      window_select_button(0); // Select first option (Resume Game)
    }
    else if (g_game_state->current_screen == GAME_SCREEN_IN_GAME_MENU)
    {
      g_game_state->current_screen = GAME_SCREEN_WORLD;
    }
    else if (g_game_state->current_screen == GAME_SCREEN_SETTINGS)
    {
      g_game_state->current_screen = g_game_state->game_started ? GAME_SCREEN_IN_GAME_MENU
                                                                : GAME_SCREEN_MAIN_MENU;
    }
    else
    {
      g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
    }
    break;

  case SDLK_UP:
    printf("Up arrow pressed, current_screen=%d\n", g_game_state->current_screen);
    if (g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL)
      g_game_state->quest_journal_scroll -= 16;
    else if (g_game_state->current_screen == GAME_SCREEN_LEGENDS)
      g_game_state->legends_scroll -= 1;
    else if (g_game_state->current_screen == GAME_SCREEN_SAVE_BROWSER)
      game_state_save_browser_move(g_game_state, -1);
    else if (g_game_state->current_screen == GAME_SCREEN_SETTINGS)
    {
      window_settings_prev_selection();
    }
    else
    {
      window_prev_selection();
    }
    break;

  case SDLK_DOWN:
    printf("Down arrow pressed, current_screen=%d\n", g_game_state->current_screen);
    if (g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL)
      g_game_state->quest_journal_scroll += 16;
    else if (g_game_state->current_screen == GAME_SCREEN_LEGENDS)
      g_game_state->legends_scroll += 1;
    else if (g_game_state->current_screen == GAME_SCREEN_SAVE_BROWSER)
      game_state_save_browser_move(g_game_state, 1);
    else if (g_game_state->current_screen == GAME_SCREEN_SETTINGS)
    {
      window_settings_next_selection();
    }
    else
    {
      window_next_selection();
    }
    break;

  case SDLK_LEFT:
    printf("Left arrow pressed\n");
    if (g_game_state->current_screen == GAME_SCREEN_SETTINGS)
    {
      window_settings_prev_section();
    }
    break;

  case SDLK_RIGHT:
    printf("Right arrow pressed\n");
    if (g_game_state->current_screen == GAME_SCREEN_SETTINGS)
    {
      window_settings_next_section();
    }
    break;

  case SDLK_RETURN:
  case SDLK_KP_ENTER:
    printf("Enter pressed\n");
    if (g_game_state->current_screen == GAME_SCREEN_SETTINGS)
    {
      // Handle settings changes using new functions
      window_activate_setting(g_background_music, g_ui_sounds);
    }
    else if (g_game_state->current_screen == GAME_SCREEN_SAVE_BROWSER)
    {
      if (g_game_state->save_browser_confirm_delete)
        game_state_save_browser_delete(g_game_state);
      else if (game_state_save_browser_load(g_game_state))
        begin_loaded_game_story();
    }
    else if (g_game_state->current_screen == GAME_SCREEN_IN_GAME_MENU)
    {
      // Handle in-game menu selection
      int selected_option = window_get_selected_button();
      switch (selected_option)
      {
      case 0: // Resume Game
        g_game_state->current_screen = GAME_SCREEN_WORLD;
        break;
      case 1: // Save Game
        game_state_save_game(g_game_state);
        break;
      case 2: // Load Game
        game_state_open_save_browser(g_game_state);
        break;
      case 3: // Settings
        g_game_state->current_screen = GAME_SCREEN_SETTINGS;
        break;
      case 4: // Main Menu
        g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
        break;
      case 5: // Exit Game
        g_should_exit = true;
        break;
      }
    }
    else
    {
      int selected_button = window_get_selected_button();
      if (selected_button > 0)
      {
        handle_button_click(selected_button);
      }
    }
    break;

  case SDLK_TAB:
    if (g_game_state->game_started &&
        (g_game_state->current_screen == GAME_SCREEN_WORLD ||
         g_game_state->current_screen == GAME_SCREEN_CHARACTER ||
         g_game_state->current_screen == GAME_SCREEN_INVENTORY ||
         g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL ||
         g_game_state->current_screen == GAME_SCREEN_MAP))
    {
      if (g_game_state->current_screen == GAME_SCREEN_CHARACTER)
      {
        window_character_skills_reset_ui();
        g_game_state->current_screen = GAME_SCREEN_WORLD;
      }
      else
        g_game_state->current_screen = GAME_SCREEN_CHARACTER;
    }
    else
    {
      printf("Tab pressed\n");
      window_next_selection();
    }
    break;
  }
}

static void begin_loaded_game_story(void)
{
  if (!g_game_state)
    return;
  printf("Game loaded successfully - moving to Genesis screen\n");
  g_game_state->current_screen = GAME_SCREEN_CHAPTER;
  g_game_state->show_chapter = true;
  g_game_state->chapter_current_page = 0;
  g_game_state->chapter_current_char = 0;
  g_game_state->chapter_total_chars = 0;
  g_game_state->chapter_text_complete = false;
  g_game_state->chapter_fade_alpha = 0.0f;
  g_game_state->chapter_fade_start_time = SDL_GetTicks();
  g_game_state->chapter_last_text_update = SDL_GetTicks();
}

void handle_button_click(int button_id)
{
  if (!g_game_state)
    return;

  printf("Button clicked: %d\n", button_id);
  play_button_sound(); // Play sound effect

  if (button_id >= BUTTON_SAVE_BROWSER_SLOT0 &&
      button_id < BUTTON_SAVE_BROWSER_SLOT0 + SAVE_BROWSER_VISIBLE_ROWS)
  {
    int i = button_id - BUTTON_SAVE_BROWSER_SLOT0;
    int idx = g_game_state->save_browser_scroll + i;
    if (idx >= 0 && idx < g_game_state->save_browser_count)
    {
      g_game_state->save_browser_selected = idx;
      g_game_state->save_browser_confirm_delete = false;
    }
    return;
  }

  switch (button_id)
  {
  case 901: // Start/Stop runtime clock
    g_game_state->runtime_clock_running = !g_game_state->runtime_clock_running;
    snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
             g_game_state->runtime_clock_running ? "Clock: running" : "Clock: stopped");
    break;
  case BUTTON_NEW_GAME:
    if (!g_game_state->game_started)
    {
      printf("Starting new game - requesting player name...\n");
      // First step: ask for player name
      g_game_state->show_name_input = true;
      strcpy(g_game_state->player_name, ""); // Start with empty name
      window_start_text_input();             // Enable text input
    }
    break;

  case BUTTON_CONTINUE:
    printf("Continue game selected\n");
    if (game_state_load_most_recent_save(g_game_state))
      begin_loaded_game_story();
    else
      printf("Failed to load saved game\n");
    break;

  case BUTTON_LOAD_GAME:
    printf("Load game selected\n");
    game_state_open_save_browser(g_game_state);
    break;

  case BUTTON_SAVE_BROWSER_LOAD:
    if (game_state_save_browser_load(g_game_state))
      begin_loaded_game_story();
    else
      printf("Failed to load selected save\n");
    break;

  case BUTTON_SAVE_BROWSER_DELETE:
    g_game_state->save_browser_confirm_delete = true;
    break;

  case BUTTON_SAVE_BROWSER_CONFIRM_DELETE:
    game_state_save_browser_delete(g_game_state);
    break;

  case BUTTON_SAVE_BROWSER_CANCEL_DELETE:
    g_game_state->save_browser_confirm_delete = false;
    break;

  case BUTTON_SAVE_BROWSER_BACK:
    g_game_state->save_browser_confirm_delete = false;
    g_game_state->current_screen = g_game_state->game_started ? GAME_SCREEN_IN_GAME_MENU
                                                              : GAME_SCREEN_MAIN_MENU;
    break;

  case BUTTON_SETTINGS:
    g_game_state->current_screen = GAME_SCREEN_SETTINGS;
    window_sync_settings_with_audio(g_background_music); // Sync settings with current audio state
    break;
    // Song editor button removed - now standalone program

  case BUTTON_EXIT:
    if (g_game_state->current_screen == GAME_SCREEN_MAIN_MENU)
    {
      g_game_state->show_exit_prompt = true;
    }
    break;

  case BUTTON_MAIN_MENU:
    g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
    break;

  case BUTTON_RESUME:
    printf("Resume game selected\n");
    g_game_state->current_screen = GAME_SCREEN_WORLD;
    break;

  case BUTTON_SAVE_GAME:
    printf("Save game selected\n");
    game_state_save_game(g_game_state);
    break;

  case BUTTON_CANCEL_EXIT:
    if (g_game_state->show_name_input)
    {
      printf("Cancel name input selected\n");
      g_game_state->show_name_input = false;
      window_stop_text_input();
      g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
    }
    else
    {
      printf("Cancel exit selected\n");
      g_game_state->show_exit_prompt = false;
    }
    break;

  case BUTTON_CONFIRM_NAME:
    printf("Name confirmed via button\n");
    if (strlen(g_game_state->player_name) > 0)
    {
      g_game_state->show_name_input = false;
      window_stop_text_input();
      printf("Name confirmed: %s\n", g_game_state->player_name);

      // Proceed to loading screen
      g_game_state->current_screen = GAME_SCREEN_LOADING;
      g_game_state->show_loading = true;
      g_game_state->loading_progress = 0;
      g_game_state->loading_total = 100;
      strcpy(g_game_state->loading_message, "Preparing to generate worlds...");

      printf("Proceeding to loading screen...\n");
    }
    else
    {
      // Play error sound for empty name
      printf("Error: Cannot confirm empty name\n");
      play_error_sound();
    }
    break;

  case BUTTON_CONFIRM_EXIT:
    printf("Confirm exit selected\n");
    g_should_exit = true;
    break;
  }
}


static int loot_screen_coords(int x, int y, int *out_x, int *out_y)
{
  int scale = window_state.scale_factor > 0 ? window_state.scale_factor : 1;
  *out_x = x / scale;
  *out_y = y / scale;
  return 1;
}

static void loot_handle_press(int sx, int sy)
{
  int index = -1;
  WindowLootHit hit = window_loot_hit(sx, sy, &index);
  if (hit == WINDOW_LOOT_HIT_LOOT_ALL)
  {
    player_controls_loot_all(g_game_state);
    play_select_sound();
    return;
  }
  if (hit == WINDOW_LOOT_HIT_CLOSE)
  {
    player_controls_close_loot(g_game_state);
    play_select_sound();
    return;
  }
  if (g_game_state->loot_held.id != ITEM_NONE && g_game_state->loot_held.pieces > 0)
    return; // wait for mouse-up to drop

  if (hit == WINDOW_LOOT_HIT_CORPSE_BAG)
    player_controls_loot_pick(g_game_state, 1, index);
  else if (hit == WINDOW_LOOT_HIT_CORPSE_EQUIP)
    player_controls_loot_pick(g_game_state, 2, index);
  else if (hit == WINDOW_LOOT_HIT_PLAYER_BAG)
    player_controls_loot_pick(g_game_state, 3, index);
}

static void loot_handle_release(int sx, int sy)
{
  if (g_game_state->loot_held.id == ITEM_NONE || g_game_state->loot_held.pieces == 0)
    return;

  int index = -1;
  WindowLootHit hit = window_loot_hit(sx, sy, &index);
  if (hit == WINDOW_LOOT_HIT_PLAYER_BAG)
  {
    if (player_controls_loot_drop_on_player(g_game_state, index))
      play_select_sound();
    return;
  }
  if (hit == WINDOW_LOOT_HIT_CORPSE_BAG)
  {
    if (player_controls_loot_drop_on_corpse(g_game_state, index))
      play_select_sound();
    return;
  }
  // Dropped outside bags: return to source.
  player_controls_loot_cancel_drag(g_game_state);
}

void handle_mouse_up(int x, int y, int button)
{
  if (!g_game_state)
    return;
  if (button != SDL_BUTTON_LEFT)
    return;

  int sx, sy;
  loot_screen_coords(x, y, &sx, &sy);

  if (g_game_state->current_screen == GAME_SCREEN_CHARACTER)
  {
    SkillId drag = window_character_skill_drag_skill();
    if (drag == SKILL_NONE)
      return;

    int from_slot = window_character_skill_drag_from_slot();
    SkillId hit_skill = SKILL_NONE;
    int hit_slot = -1;
    WindowCharSkillHit hit =
        window_character_skill_ui_hit(sx, sy, &hit_skill, &hit_slot);

    if (hit == WINDOW_CHAR_SKILL_HIT_HOTBAR && hit_slot >= 0)
    {
      SkillId dest = player_controls_hotbar_skill(&g_game_state->controls, hit_slot);
      if (from_slot >= 0 && from_slot != hit_slot)
      {
        // Swap slots.
        skill_equip_hotbar(g_game_state, &g_game_state->controls, hit_slot, drag);
        skill_equip_hotbar(g_game_state, &g_game_state->controls, from_slot, dest);
      }
      else if (from_slot < 0)
      {
        // From skill list: replace target; clear any other slot holding this skill.
        for (int i = 0; i < PLAYER_HOTBAR_SLOTS; i++)
        {
          if (i != hit_slot &&
              player_controls_hotbar_skill(&g_game_state->controls, i) == drag)
            skill_equip_hotbar(g_game_state, &g_game_state->controls, i, SKILL_NONE);
        }
        if (skill_equip_hotbar(g_game_state, &g_game_state->controls, hit_slot, drag))
          play_select_sound();
      }
      // Dropping back on the same slot is a no-op.
    }
    else if (from_slot >= 0 && hit != WINDOW_CHAR_SKILL_HIT_HOTBAR)
    {
      // Dragged a hotbar skill off the bar → clear the slot.
      skill_equip_hotbar(g_game_state, &g_game_state->controls, from_slot, SKILL_NONE);
    }

    window_character_skill_drag_clear();
    return;
  }

  if (g_game_state->current_screen != GAME_SCREEN_LOOT)
    return;
  loot_handle_release(sx, sy);
}

void handle_mouse_click(int x, int y, int button)
{
  if (!g_game_state)
    return;

  if (g_game_state->current_screen == GAME_SCREEN_INVENTORY && button == SDL_BUTTON_RIGHT)
  {
    int sx, sy;
    loot_screen_coords(x, y, &sx, &sy);
    int index = -1;
    WindowInvHit hit = window_inventory_hit(sx, sy, &index);
    if (hit == WINDOW_INV_HIT_BAG && index >= 0)
    {
      if (player_controls_drop_inventory_slot(g_game_state, index))
        play_select_sound();
    }
    return;
  }

  if (g_game_state->current_screen == GAME_SCREEN_LOOT && button == SDL_BUTTON_LEFT)
  {
    int sx, sy;
    loot_screen_coords(x, y, &sx, &sy);
    loot_handle_press(sx, sy);
    return;
  }

  if (g_game_state->current_screen == GAME_SCREEN_CRAFT && button == SDL_BUTTON_LEFT)
  {
    int sx, sy;
    loot_screen_coords(x, y, &sx, &sy);
    int index = -1;
    WindowCraftHit hit = window_craft_hit(sx, sy, &index);
    if (hit == WINDOW_CRAFT_HIT_CLOSE)
    {
      player_controls_close_craft(g_game_state);
      play_select_sound();
    }
    else if (hit == WINDOW_CRAFT_HIT_RECIPE)
    {
      g_game_state->craft.selected = index;
      play_select_sound();
    }
    else if (hit == WINDOW_CRAFT_HIT_CRAFT)
    {
      int indices[64];
      int count = craft_session_list_recipes(&g_game_state->craft, indices, 64);
      if (index >= 0 && index < count && g_game_state->player)
      {
        const CraftRecipe *recipe = craft_recipe_at(indices[index]);
        if (recipe &&
            craft_apply(&g_game_state->player->inventory, recipe,
                        g_game_state->status_message,
                        sizeof(g_game_state->status_message)))
          play_select_sound();
      }
    }
    return;
  }

  if (g_game_state->current_screen == GAME_SCREEN_SHOP && button == SDL_BUTTON_LEFT)
  {
    int sx, sy;
    loot_screen_coords(x, y, &sx, &sy);
    int index = -1;
    WindowShopHit hit = window_shop_hit(sx, sy, &index);
    if (hit == WINDOW_SHOP_HIT_CLOSE)
    {
      game_state_close_shop(g_game_state);
      play_select_sound();
    }
    else if (hit == WINDOW_SHOP_HIT_LISTING)
    {
      if (shop_buy_listing(g_game_state, index, g_game_state->status_message,
                           sizeof(g_game_state->status_message)))
        play_select_sound();
    }
    else if (hit == WINDOW_SHOP_HIT_PLAYER_BAG)
    {
      if (shop_sell_player_slot(g_game_state, index, g_game_state->status_message,
                                sizeof(g_game_state->status_message)))
        play_select_sound();
    }
    return;
  }

  if (g_game_state->show_world_editor_modal)
  {
    if (button == SDL_BUTTON_LEFT)
    {
      int scaled_x = x / window_state.scale_factor;
      int scaled_y = y / window_state.scale_factor;
      float bar_t = 0.0f;
      int hit = window_world_editor_modal_hit(scaled_x, scaled_y, &bar_t);
      if (hit != WINDOW_WORLD_EDITOR_HIT_NONE)
      {
        play_select_sound();
        client_apply_world_editor_hit(hit, bar_t);
      }
    }
    return;
  }

  if (g_game_state->game_started &&
      g_game_state->current_screen == GAME_SCREEN_MAP &&
      button == SDL_BUTTON_LEFT)
  {
    int scaled_x = x / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
    int scaled_y = y / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
    const int hit = window_map_hit_waypoint(g_game_state, scaled_x, scaled_y);
    if (hit >= 0 && game_state_select_waypoint(g_game_state, hit))
    {
      play_select_sound();
      g_game_state->current_screen = GAME_SCREEN_WORLD;
      return;
    }
  }

  if (g_game_state->game_started &&
      g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL &&
      button == SDL_BUTTON_LEFT)
  {
    int scaled_x = x / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
    int scaled_y = y / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
    if (window_quest_journal_hit_track(scaled_x, scaled_y) &&
        storyline_has_active_quest(&g_game_state->storyline))
    {
      g_game_state->mission_tracked = !g_game_state->mission_tracked;
      snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
               g_game_state->mission_tracked ? "Mission tracked" : "Mission untracked");
      play_select_sound();
      return;
    }
  }

  if (g_game_state->game_started &&
      g_game_state->current_screen == GAME_SCREEN_CHARACTER &&
      button == SDL_BUTTON_LEFT)
  {
    int scaled_x = x / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
    int scaled_y = y / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);

    // Already dragging — wait for mouse-up to drop.
    if (window_character_skill_drag_skill() != SKILL_NONE)
      return;

    SkillId skill_id = SKILL_NONE;
    int slot = -1;
    WindowCharSkillHit skill_hit =
        window_character_skill_ui_hit(scaled_x, scaled_y, &skill_id, &slot);

    if (skill_hit == WINDOW_CHAR_SKILL_HIT_OPEN)
    {
      window_character_skills_toggle();
      play_select_sound();
      return;
    }
    if (skill_hit == WINDOW_CHAR_SKILL_HIT_CLOSE)
    {
      window_character_skills_close();
      play_select_sound();
      return;
    }
    if (skill_hit == WINDOW_CHAR_SKILL_HIT_UNLOCK && skill_id != SKILL_NONE)
    {
      if (skill_try_unlock(g_game_state, skill_id))
        play_select_sound();
      return;
    }
    if (skill_hit == WINDOW_CHAR_SKILL_HIT_DRAG && skill_id != SKILL_NONE)
    {
      window_character_skill_drag_begin(skill_id, -1);
      return;
    }
    if (skill_hit == WINDOW_CHAR_SKILL_HIT_HOTBAR && slot >= 0)
    {
      SkillId equipped = player_controls_hotbar_skill(&g_game_state->controls, slot);
      if (equipped != SKILL_NONE)
        window_character_skill_drag_begin(equipped, slot);
      return;
    }

    bool is_body = false;
    ActorAttribute attr = ACTOR_ATTR_STRENGTH;
    if (window_character_profile_hit(scaled_x, scaled_y, &is_body, &attr))
    {
      Actor *target = NULL;
      if (is_body)
        target = player_controls_dominated_actor(g_game_state);
      else
        target = g_game_state->player;

      if (target && actor_spend_attribute_point(target, attr))
      {
        if (target->extra_data)
        {
          MobActor *mob = (MobActor *)target->extra_data;
          mob->base.strength = target->strength;
          mob->base.dexterity = target->dexterity;
          mob->base.intelligence = target->intelligence;
          mob->base.wisdom = target->wisdom;
          mob->base.constitution = target->constitution;
          mob->base.charisma = target->charisma;
          mob->base.luck = target->luck;
          mob->base.health = target->health;
          mob->base.stamina = target->stamina;
          mob->base.mana = target->mana;
          mob->base.turn_speed = target->turn_speed;
          mob->base.attribute_points = target->attribute_points;
          mob->base.skill_points = target->skill_points;
          mob->base.experience = target->experience;
          mob->base.level = target->level;
        }
        static const char *attr_names[] = {
            "Strength", "Dexterity", "Intelligence", "Wisdom",
            "Constitution", "Charisma", "Luck"};
        const char *who = is_body ? (target->name[0] ? target->name : "Body") : "Spirit";
        snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                 "%s +1 %s (%u left)", who, attr_names[attr], target->attribute_points);
        play_select_sound();
      }
      return;
    }
  }

  // Song editor removed - now standalone program

    // Handle world interaction (action RPG controls)
  if (g_game_state->game_started && g_game_state->current_screen == GAME_SCREEN_WORLD)
  {
    if (button == SDL_BUTTON_RIGHT)
    {
      window_clear_voxel_info();
      game_state_handle_mouse(g_game_state, x, y, button);
      return;
    }

    // Left click: select a nearby actor (for nameplate), or swing / select hero.
    int world_x, world_y, world_z;
    if (window_screen_to_world_coords(x, y, &world_x, &world_y, &world_z))
    {
      if (button == SDL_BUTTON_LEFT)
      {
        extern IsometricRenderer *g_isometric_renderer;
        const int scaled_x = x / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
        const int scaled_y = y / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
        if (g_isometric_renderer && g_game_state->current_world && !window_is_fp_mode())
        {
          const uint32_t hit = isometric_renderer_pick_actor(
              g_isometric_renderer, g_game_state->current_world, 0, scaled_x, scaled_y, 28);
          if (hit != 0)
          {
            if (isometric_renderer_selected_actor(g_isometric_renderer) == hit)
              isometric_renderer_set_selected_actor(g_isometric_renderer, 0);
            else
              isometric_renderer_set_selected_actor(g_isometric_renderer, hit);
            play_select_sound();
            // Selecting an entity does not also swing — click empty ground to attack.
            return;
          }
          // Empty ground clears entity selection.
          if (isometric_renderer_selected_actor(g_isometric_renderer) != 0)
            isometric_renderer_set_selected_actor(g_isometric_renderer, 0);
        }

        float pdx = (float)world_x + 0.5f - g_game_state->player_world_x;
        float pdy = (float)world_y + 0.5f - g_game_state->player_world_y;
        float click_dist = sqrtf(pdx * pdx + pdy * pdy);

        if (click_dist < 1.0f)
        {
          printf("Clicked on hero character - selecting hero\n");
          struct {
            int coords[3];
            char name[64];
          } hero_data;

          hero_data.coords[0] = g_game_state->player_voxel_x;
          hero_data.coords[1] = g_game_state->player_voxel_y;
          hero_data.coords[2] = g_game_state->player_voxel_z;
          strncpy(hero_data.name, g_game_state->player_name, sizeof(hero_data.name) - 1);
          hero_data.name[sizeof(hero_data.name) - 1] = '\0';

          window_set_selection(SELECTION_HERO, &hero_data);
          play_select_sound();
        }
        else if (!g_game_state->controls.is_attacking)
        {
          window_clear_voxel_info();
          float tx = (float)world_x + 0.5f;
          float ty = (float)world_y + 0.5f;
          int pvox = 0, pvoy = 0, pvoz = 0;
          bool have_voxel = false;

          if (window_is_fp_mode())
          {
            const float yaw = g_game_state->controls.facing_yaw;
            const float pitch = g_game_state->controls.pitch;
            const float cos_pitch = cosf(pitch);
            tx = g_game_state->player_world_x + cosf(yaw) * cos_pitch * PLAYER_SWING_RADIUS;
            ty = g_game_state->player_world_y + sinf(yaw) * cos_pitch * PLAYER_SWING_RADIUS;
            have_voxel = player_controls_pick_melee_voxel(
                g_game_state, &g_game_state->controls, &pvox, &pvoy, &pvoz);
          }
          else if (g_isometric_renderer && g_game_state->current_world)
          {
            uint32_t vx = 0, vy = 0, vz = 0;
            if (isometric_renderer_pick_voxel(g_isometric_renderer, g_game_state->current_world,
                                              g_isometric_renderer->camera_z, scaled_x, scaled_y,
                                              &vx, &vy, &vz))
            {
              have_voxel = true;
              pvox = (int)vx;
              pvoy = (int)vy;
              pvoz = (int)vz;
              tx = (float)vx + 0.5f;
              ty = (float)vy + 0.5f;
            }
          }

          player_controls_start_attack(g_game_state, &g_game_state->controls, tx, ty);
          if (have_voxel)
            player_controls_set_attack_voxel(&g_game_state->controls, pvox, pvoy, pvoz);
        }
      }
    }
    else if (button == SDL_BUTTON_LEFT)
    {
      window_clear_voxel_info();
      // First-person click with no ground under the crosshair: still swing along facing.
      if (window_is_fp_mode() && !g_game_state->controls.is_attacking)
      {
        const float yaw = g_game_state->controls.facing_yaw;
        const float pitch = g_game_state->controls.pitch;
        const float cos_pitch = cosf(pitch);
        const float tx = g_game_state->player_world_x +
                         cosf(yaw) * cos_pitch * PLAYER_SWING_RADIUS;
        const float ty = g_game_state->player_world_y +
                         sinf(yaw) * cos_pitch * PLAYER_SWING_RADIUS;
        player_controls_start_attack(g_game_state, &g_game_state->controls, tx, ty);
        int pvox = 0, pvoy = 0, pvoz = 0;
        if (player_controls_pick_melee_voxel(g_game_state, &g_game_state->controls,
                                             &pvox, &pvoy, &pvoz))
          player_controls_set_attack_voxel(&g_game_state->controls, pvox, pvoy, pvoz);
      }
    }
  }
}

void handle_mouse_motion(int x, int y)
{
  if (!g_game_state)
    return;

  if (g_game_state->show_world_editor_modal)
    return;

  // Handle voxel highlighting on hover
  if (g_game_state->game_started && g_game_state->current_screen == GAME_SCREEN_WORLD)
  {
    game_state_handle_mouse_motion(g_game_state, x, y);

    extern IsometricRenderer *g_isometric_renderer;
    const int scaled_x = x / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
    const int scaled_y = y / (window_state.scale_factor > 0 ? window_state.scale_factor : 1);
    int hx = 0, hy = 0, hz = 0;
    bool have = false;

    if (window_is_fp_mode())
    {
      have = player_controls_pick_melee_voxel(g_game_state, &g_game_state->controls, &hx, &hy, &hz);
    }
    else if (g_isometric_renderer && g_game_state->current_world)
    {
      uint32_t vx = 0, vy = 0, vz = 0;
      if (isometric_renderer_pick_voxel(g_isometric_renderer, g_game_state->current_world,
                                        g_isometric_renderer->camera_z, scaled_x, scaled_y,
                                        &vx, &vy, &vz))
      {
        hx = (int)vx;
        hy = (int)vy;
        hz = (int)vz;
        have = true;
      }
    }

    if (have)
      window_set_hovered_voxel(hx, hy, hz);
    else
      window_clear_hovered_voxel();
  }
}

// Draw the live world into the base texture. Used by play and by storyline scenes so
// dialogue sits on the world rather than a blank screen.
static void render_play_world(void)
{
  if (!g_game_state)
    return;

  World *world = g_game_state->current_world ? g_game_state->current_world : g_game_state->main_menu_world;
  if (!world)
    return;

  const char *name = g_game_state->game_started ? g_game_state->player_name : "Main Menu";
  if (window_is_fp_mode())
    window_render_fp_world(world, g_game_state->player_x, g_game_state->player_y,
                           g_game_state->player_z, name);
  else
    window_render_isomorphic_world(world, g_game_state->player_x, g_game_state->player_y,
                                   g_game_state->player_z, name);
}

// Main rendering function
void render_game()
{
  if (!g_game_state)
    return;

  // Debug: Print current screen
  static int last_screen = -1;
  if (last_screen != g_game_state->current_screen)
  {
    printf("Screen changed to: %d\n", g_game_state->current_screen);
    last_screen = g_game_state->current_screen;
  }

  // Clear screen
  window_clear();

  bool world_cursor = g_game_state->game_started &&
                      (g_game_state->current_screen == GAME_SCREEN_WORLD ||
                       g_game_state->current_screen == GAME_SCREEN_LOOT ||
                       g_game_state->current_screen == GAME_SCREEN_SHOP ||
                       g_game_state->current_screen == GAME_SCREEN_INVENTORY ||
                       g_game_state->current_screen == GAME_SCREEN_CHARACTER ||
                       g_game_state->current_screen == GAME_SCREEN_QUEST_JOURNAL ||
                       g_game_state->current_screen == GAME_SCREEN_MAP) &&
                      !g_game_state->show_world_editor_modal;
  window_set_custom_cursor_active(world_cursor);

  // Render title screen if needed
  if (g_show_title_screen)
  {
    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - g_title_start_time;
    window_set_title_mode(true);
    window_render_title_screen(g_title_fade_alpha, elapsed);
    window_present();
    return;
  }

  // Render based on current screen
  switch (g_game_state->current_screen)
  {
  case GAME_SCREEN_MAIN_MENU:
    window_render_main_menu();
    break;

  case GAME_SCREEN_WORLD:
    render_play_world();
    break;

  case GAME_SCREEN_LOOT:
    {
      render_play_world();
      player_controls_update_loot(g_game_state);
      if (g_game_state->current_screen != GAME_SCREEN_LOOT)
        break;
      Actor *corpse = g_game_state->loot_corpse;
      const char *cname = (corpse && corpse->name[0]) ? corpse->name : "Corpse";
      const Inventory *cinv = corpse ? &corpse->inventory : NULL;
      const Equipment *ceq = corpse ? mob_actor_equipment_const(corpse) : NULL;
      const Inventory *pinv = g_game_state->player ? &g_game_state->player->inventory : NULL;
      const ItemStack *held =
          (g_game_state->loot_held.id != ITEM_NONE && g_game_state->loot_held.pieces > 0)
              ? &g_game_state->loot_held
              : NULL;
      window_render_loot(cname, cinv, ceq, pinv, held);
    }
    break;

  case GAME_SCREEN_CRAFT:
    {
      render_play_world();
      if (!g_game_state->craft.active)
      {
        player_controls_close_craft(g_game_state);
        break;
      }
      const Inventory *pinv = g_game_state->player ? &g_game_state->player->inventory : NULL;
      window_render_craft(&g_game_state->craft, pinv);
    }
    break;

  case GAME_SCREEN_SHOP:
    {
      render_play_world();
      if (!g_game_state->shop.active)
      {
        game_state_close_shop(g_game_state);
        break;
      }
      Actor *merchant =
          world_find_runtime_actor(g_game_state->current_world, g_game_state->shop.merchant_id);
      if (!merchant || !merchant->is_active || merchant->health == 0)
      {
        snprintf(g_game_state->status_message, sizeof(g_game_state->status_message),
                 "Shopkeeper gone");
        game_state_close_shop(g_game_state);
        break;
      }
      const Wallet *mpurse = NULL;
      if (merchant->extra_data)
        mpurse = &((MobActor *)merchant->extra_data)->purse;
      const Inventory *pinv = g_game_state->player ? &g_game_state->player->inventory : NULL;
      window_render_shop(g_game_state->shop.merchant_name, &g_game_state->purse, mpurse,
                         &g_game_state->shop, pinv);
    }
    break;

  case GAME_SCREEN_INVENTORY:
    {
      // Keep the world visible behind a centered inventory modal.
      render_play_world();
      const char *spirit_name = g_game_state->player_name[0] ? g_game_state->player_name : "Spirit";
      const Inventory *inv = g_game_state->player ? &g_game_state->player->inventory : NULL;
      const Equipment *eq = NULL;
      const char *body_name = NULL;
      if (player_controls_is_dominating(g_game_state))
      {
        Actor *body = player_controls_dominated_actor(g_game_state);
        eq = mob_actor_equipment_const(body);
        if (body)
          body_name = body->name;
      }
      window_render_inventory(spirit_name, &g_game_state->purse, inv, eq, body_name);
    }
    break;

  case GAME_SCREEN_CHARACTER:
    {
      render_play_world();
      const Actor *spirit = g_game_state->player;
      const Actor *body = NULL;
      if (player_controls_is_dominating(g_game_state))
        body = player_controls_dominated_actor(g_game_state);
      window_render_character_profile(spirit, body);
    }
    break;

  case GAME_SCREEN_QUEST_JOURNAL:
    {
      render_play_world();
      window_render_quest_journal(&g_game_state->storyline, &g_game_state->quest_journal_scroll,
                                  g_game_state->mission_tracked);
    }
    break;

  case GAME_SCREEN_LEGENDS:
    {
      render_play_world();
      const Chronicle *c =
          g_game_state->universe.chronicle_ready ? &g_game_state->universe.chronicle : NULL;
      window_render_legends(c, &g_game_state->legends_scroll);
    }
    break;

  case GAME_SCREEN_MAP:
    {
      World *world = g_game_state->current_world ? g_game_state->current_world
                                                : g_game_state->main_menu_world;
      window_render_world_map(world, g_game_state->player_x, g_game_state->player_y,
                              g_game_state->player_z);
    }
    break;

  case GAME_SCREEN_SETTINGS:
    window_render_settings();
    break;

  case GAME_SCREEN_IN_GAME_MENU:
    window_render_in_game_menu();
    break;

  case GAME_SCREEN_SAVE_BROWSER:
    window_render_save_browser();
    break;

  case GAME_SCREEN_LOADING:
    // Render loading screen
    if (g_game_state->show_loading)
    {
      window_render_loading_screen(g_game_state);
    }
    break;

  case GAME_SCREEN_CHAPTER:
    if (g_game_state->show_chapter)
    {
      const char *chapter_title = storyline_chapter_title(&g_game_state->storyline);
      const char *chapter_content = storyline_chapter_page_text(&g_game_state->storyline);
      window_render_chapter_with_fade(chapter_title, chapter_content, g_game_state->chapter_fade_alpha,
                                      g_game_state->chapter_current_char, g_game_state->chapter_total_chars);
    }
    break;

  case GAME_SCREEN_SCENE:
    render_play_world();
    if (g_game_state->show_scene && storyline_scene_active(&g_game_state->storyline))
    {
      const char *elem_type = storyline_scene_element_type(&g_game_state->storyline);
      if (elem_type && strcmp(elem_type, "quest") == 0)
      {
        window_render_quest_box(storyline_scene_quest_name(&g_game_state->storyline),
                                storyline_scene_quest_description(&g_game_state->storyline),
                                storyline_scene_quest_completed(&g_game_state->storyline),
                                storyline_scene_quest_objectives(&g_game_state->storyline));
      }
      else
      {
        window_render_scene_text_box(storyline_scene_text(&g_game_state->storyline),
                                     storyline_scene_current_char(&g_game_state->storyline),
                                     storyline_scene_total_chars(&g_game_state->storyline));
      }
    }
    break;
    // Song editor screen removed - now standalone program

  default:
    window_render_main_menu();
    break;
  }

  // Render modals
  if (g_game_state->show_exit_prompt)
  {
    window_render_exit_prompt();
  }

  if (g_game_state->show_new_game_warning)
  {
    window_render_new_game_warning();
  }

  if (g_game_state->show_name_input)
  {
    window_render_name_input("Enter your character name:", g_game_state->player_name);
  }

  if (g_game_state->show_world_editor_modal)
  {
    window_render_world_editor_modal(client_active_world());
  }

  if (world_cursor)
  {
    window_render_custom_cursor();
  }

  if (g_console)
  {
    console_render(g_console, window_state.renderer, window_state.font,
                   window_state.base_width, window_state.base_height);
  }

  // Present the frame
  window_present();
}

// Main game loop
// Note: Removed loading_progress_callback - now using async world generation

// Target frame rate. SDL_GetTicks has millisecond resolution, which is too coarse to say anything
// useful about an 8.3ms frame, so the loop below times itself off the performance counter.
// VERSE_TARGET_FPS overrides this at runtime; 0 means run uncapped, which is how the renderer's
// real headroom gets measured.
#define VERSE_DEFAULT_TARGET_FPS 120.0

static double client_target_fps(void)
{
  static double cached = -1.0;
  if (cached >= 0.0)
    return cached;

  cached = VERSE_DEFAULT_TARGET_FPS;
  const char *env = getenv("VERSE_TARGET_FPS");
  if (env && *env)
  {
    const double parsed = atof(env);
    if (parsed >= 0.0)
      cached = parsed;
  }
  return cached;
}

void game_loop()
{
  const double perf_freq = (double)SDL_GetPerformanceFrequency();
  const double target_fps = client_target_fps();
  const double target_frame_seconds = (target_fps > 0.0) ? 1.0 / target_fps : 0.0;

  Uint64 last_time = SDL_GetPerformanceCounter();
  Uint64 fps_timer = last_time;
  int frame_count = 0;
  double total_frame_time = 0.0;
  double max_frame_time = 0.0;
  double event_time = 0.0, update_time = 0.0, render_time = 0.0;

  printf("FRAME TIMING: targeting %.0f FPS (%.2fms per frame)%s\n", target_fps,
         target_fps > 0.0 ? 1000.0 / target_fps : 0.0,
         target_fps > 0.0 ? "" : " — uncapped");

  while (1)
  {
    const Uint64 frame_start = SDL_GetPerformanceCounter();

    // Handle events
    const Uint64 event_start = SDL_GetPerformanceCounter();
    int event_result = window_handle_events();
    if (event_result == -1 || g_should_exit)
    {
      break; // Exit requested
    }
    event_time = (double)(SDL_GetPerformanceCounter() - event_start) * 1000.0 / perf_freq;

    // Calculate delta time
    const Uint64 current_time = SDL_GetPerformanceCounter();
    double delta_time = (double)(current_time - last_time) / perf_freq;
    last_time = current_time;

    // Update title screen fade
    if (g_show_title_screen)
    {
      Uint32 title_elapsed = current_time - g_title_start_time;
      if (title_elapsed < 2000)
      { // 2 second fade in
        g_title_fade_alpha = (float)title_elapsed / 2000.0f;
      }
      else
      {
        g_title_fade_alpha = 1.0f;
      }

      // Once the backdrop worker is done, bake the subvoxel atlas during title idle time so the
      // first menu frame does not hitch on material_worlds_init + texture upload.
      if (g_menu_backdrop_group && task_group_is_complete(g_menu_backdrop_group))
        warm_material_atlas_if_needed();
    }

    // Handle loading screen world generation
    if (g_game_state && g_game_state->current_screen == GAME_SCREEN_LOADING && g_game_state->show_loading)
    {
      // Start async world generation if not already started
      if (!g_game_state->world_generation_active && g_game_state->world_generation_step == 0)
      {
        game_state_start_async_world_generation(g_game_state);
      }

      // Update world generation (one step per frame to keep UI responsive)
      if (g_game_state->world_generation_active)
      {
        if (game_state_update_world_generation(g_game_state))
        {
          printf("World generation complete - starting Genesis chapter\n");
          g_game_state->show_loading = false;
          game_state_show_chapter(g_game_state);
        }
      }
    }

    // Update chapter scene
    if (g_game_state && g_game_state->current_screen == GAME_SCREEN_CHAPTER && g_game_state->show_chapter)
    {
      Uint32 chapter_elapsed = current_time - g_game_state->chapter_fade_start_time;

      if (chapter_elapsed < 1000)
        g_game_state->chapter_fade_alpha = (float)chapter_elapsed / 1000.0f;
      else
        g_game_state->chapter_fade_alpha = 1.0f;

      const char *chapter_content = storyline_chapter_page_text(&g_game_state->storyline);
      if (g_game_state->chapter_total_chars == 0)
        g_game_state->chapter_total_chars = (int)strlen(chapter_content);

      if (current_time - g_game_state->chapter_last_text_update >= 50)
      {
        if (g_game_state->chapter_current_char < g_game_state->chapter_total_chars)
        {
          g_game_state->chapter_current_char++;
          g_game_state->chapter_last_text_update = current_time;
        }
        else
        {
          g_game_state->chapter_text_complete = true;
        }
      }
    }

    // Update scene dialogue streaming
    if (g_game_state && g_game_state->current_screen == GAME_SCREEN_SCENE && g_game_state->show_scene)
    {
      const char *elem_type = storyline_scene_element_type(&g_game_state->storyline);
      if (elem_type && strcmp(elem_type, "text") == 0)
      {
        if (storyline_scene_total_chars(&g_game_state->storyline) == 0)
          storyline_scene_prepare_stream(&g_game_state->storyline);

        if (current_time - g_game_state->chapter_last_text_update >= 50)
        {
          if (!storyline_scene_text_complete(&g_game_state->storyline))
          {
            storyline_scene_tick_char(&g_game_state->storyline);
            g_game_state->chapter_last_text_update = current_time;
          }
        }
      }
    }

    // Track update phase timing
    const Uint64 update_start = SDL_GetPerformanceCounter();
    // Skip while the title owns the screen or the menu-backdrop worker still owns GameState.
    if (g_game_state && !g_show_title_screen && !g_menu_backdrop_group)
    {
      game_state_update(g_game_state, delta_time);
    }

    // Dynamic music: combat raises intensity; release eases back to ambient.
    if (g_background_music && !g_show_title_screen)
    {
      float combat = 0.0f;
      if (g_game_state && g_game_state->game_started)
        combat = game_state_music_intensity(g_game_state);
      background_music_set_intensity(g_background_music, combat);
      background_music_update(g_background_music, (float)delta_time);
    }
    update_time = (double)(SDL_GetPerformanceCounter() - update_start) * 1000.0 / perf_freq;

    // Render game
    const Uint64 render_start = SDL_GetPerformanceCounter();
    render_game();
    render_time = (double)(SDL_GetPerformanceCounter() - render_start) * 1000.0 / perf_freq;

    // Work done. Everything from here is measurement and pacing.
    const double work_seconds = (double)(SDL_GetPerformanceCounter() - frame_start) / perf_freq;

    // Sleep off whatever is left of the frame budget, rather than a fixed amount. The old code
    // slept 16ms unconditionally after the work, so a frame cost work + 16ms and the game could
    // never reach 60 FPS, let alone 120. Give the last millisecond back to a spin, because
    // SDL_Delay rounds up to the OS scheduler's granularity and overshooting an 8.3ms budget by a
    // millisecond is a third of it.
    if (target_frame_seconds > 0.0)
    {
      const double remaining = target_frame_seconds - work_seconds;
      if (remaining > 0.0)
      {
        if (remaining > 0.002)
          SDL_Delay((Uint32)((remaining - 0.001) * 1000.0));
        while ((double)(SDL_GetPerformanceCounter() - frame_start) / perf_freq <
               target_frame_seconds)
          ; // spin out the remainder
      }
    }

    const Uint64 frame_end = SDL_GetPerformanceCounter();
    const double frame_time_ms = (double)(frame_end - frame_start) * 1000.0 / perf_freq;

    // Track frame timing statistics
    total_frame_time += frame_time_ms;
    if (frame_time_ms > max_frame_time)
    {
      max_frame_time = frame_time_ms;
    }
    frame_count++;

    // Print frame timing every second
    if ((double)(frame_end - fps_timer) / perf_freq >= 1.0)
    {
      const double avg_frame_time = total_frame_time / frame_count;
      const double fps = (avg_frame_time > 0.0) ? 1000.0 / avg_frame_time : 0.0;

      // Work excludes the pacing sleep, so it is the number that says whether the target is
      // actually being met or merely being waited for.
      printf("FRAME TIMING: FPS: %.1f | Avg: %.2fms | Max: %.2fms | "
             "Work: %.2fms (event %.2f, update %.2f, render %.2f)\n",
             fps, avg_frame_time, max_frame_time,
             event_time + update_time + render_time, event_time, update_time, render_time);

      // Reset counters
      fps_timer = frame_end;
      total_frame_time = 0.0;
      max_frame_time = 0.0;
      frame_count = 0;
    }
  }
}

int main()
{
  printf("=== VERSE Client ===\n");

  particle_effects_registry_init();

  // Worker pool for world generation and world simulation. Failing to start it is not fatal:
  // submitted work then runs inline on this thread, exactly as it did before.
  if (!task_scheduler_init(0))
    printf("Worker pool unavailable; background work will run on the main thread\n");

  // Initialize window system
  if (!window_init("VERSE", 1024, 960))
  {
    printf("Failed to initialize window system\n");
    return 1;
  }

  gamepad_init();

  g_console = console_create();
  if (!g_console)
  {
    printf("Failed to create developer console\n");
    return 1;
  }

  // Initialize background music
  g_background_music = background_music_create(44100);
  if (!g_background_music)
  {
    printf("Failed to initialize background music\n");
    return 1;
  }

  // Initialize background music with melodies
  if (!background_music_initialize(g_background_music, "assets/melodies"))
  {
    printf("Failed to initialize background music with melodies\n");
    return 1;
  }

  // Do not start background music yet; wait until leaving title screen
  background_music_set_enabled(g_background_music, false);

  // Initialize title hum (uses its own internal synth)
  g_title_hum = title_hum_create(44100);
  if (!g_title_hum) {
    printf("Failed to initialize title hum system\n");
    return 1;
  }

  // Initialize UI sound system (use a high channel index to avoid conflicts with music pad channels)
  g_ui_sounds = ui_sounds_create(g_background_music->synth, 10);
  if (!g_ui_sounds)
  {
    printf("Failed to initialize UI sound system\n");
    return 1;
  }
  printf("UI sound system initialized successfully\n");

  // Game SFX: pulse on 11, noise on 12 (Game Boy–style dual lanes above UI)
  g_game_sfx = game_sfx_create(g_background_music->synth, 11, 12);
  if (!g_game_sfx)
  {
    printf("Failed to initialize game SFX system\n");
    return 1;
  }
  printf("Game SFX system initialized successfully\n");

  // Respect menu sounds enabled setting (default true)
  if (!window_state.menu_sounds_enabled) {
    ui_sounds_set_enabled(g_ui_sounds, false);
  }

  // Apply loaded settings (ensure defaults are reflected on first launch)
  background_music_set_music_volume(g_background_music, window_state.music_volume / 100.0f);
  background_music_set_enabled(g_background_music, window_state.background_music_enabled);
  if (g_background_music->current_melody)
  {
    printf("Current melody: %s with %d notes\n", g_background_music->current_melody->name, g_background_music->current_melody->note_count);
  }
  else
  {
    printf("No current melody loaded\n");
  }

  // Create game state shell; generate the menu HOME backdrop on a worker while the title plays.
  g_game_state = game_state_create();
  if (!g_game_state)
  {
    printf("Failed to create game state\n");
    return 1;
  }

  g_menu_backdrop_group = task_group_create("menu-backdrop");
  if (!g_menu_backdrop_group ||
      !task_group_submit(g_menu_backdrop_group, menu_backdrop_init_task, g_game_state))
  {
    // Inline fallback (no workers, or submit failed).
    if (g_menu_backdrop_group)
    {
      task_group_destroy(g_menu_backdrop_group);
      g_menu_backdrop_group = NULL;
    }
    if (!game_state_init(g_game_state))
    {
      printf("Failed to initialize game state\n");
      return 1;
    }
    g_menu_backdrop_ok = true;
  }

  // Set up callbacks
  window_set_key_callback(handle_key_press);
  window_set_mouse_callback(handle_mouse_click);
  window_set_mouse_up_callback(handle_mouse_up);
  window_set_mouse_motion_callback(handle_mouse_motion);
  window_set_button_callback(handle_button_click);

  // Add main menu buttons (these will be overridden by window_render_main_menu)
  // The buttons are actually added in window_render_main_menu() function

  // Set up audio (mix title hum + background music)
  window_setup_audio_with_title(g_background_music, g_title_hum);

  // Initialize title screen
  g_title_start_time = SDL_GetTicks();
  // Start the title hum immediately so it plays on title screen
  title_hum_start(g_title_hum);

  printf("Starting game loop...\n");

  // Run game loop
  game_loop();

  // Cleanup
  printf("Cleaning up...\n");

  (void)ensure_menu_backdrop_ready();

  ui_sounds_destroy(g_ui_sounds);
  game_sfx_destroy(g_game_sfx);
  g_game_sfx = NULL;
  window_cleanup_audio();
  if (g_title_hum) {
    title_hum_destroy(g_title_hum);
    g_title_hum = NULL;
  }
  background_music_destroy(g_background_music);
  game_state_destroy(g_game_state); // joins any generation worker first
  console_destroy(g_console);
  g_console = NULL;
  window_cleanup();
  task_scheduler_shutdown();

  printf("VERSE Client completed successfully!\n");
  return 0;
}
