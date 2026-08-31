#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <fcntl.h>
#include <unistd.h>
#include "game_state.h"
#include "world_spawn.h"
#include "character.h"
#include "constants.h"
#include "isometric_renderer.h"
#include "universe.h"
#include "player_controls.h"
#include "window.h"
#include "storyline.h"
#include "constants.h"
#include "world_gen_job.h"
#include "task_scheduler.h"
#include "world_physics_jobs.h"
#include "item.h"
#include "particle_effects.h"
#include "weather_storm.h"
#include "fp_renderer.h"
#include "world_transition.h"
#include "shadow_world.h"
#include "mob_ai.h"
#include "dialogue.h"
#include "console.h"
#include "actor.h"
#include "foliage_bend.h"
#include "settlement.h"
#include "household.h"
#include "game_log.h"
#include "nav_aide.h"
#include "fog_volume.h"
#include "debris.h"
#include "voxel_combat.h"
#include "gamepad.h"
#include "voxel_fracture.h"
#include "fire_sim.h"
#include "game_sfx.h"
#include "universe_biome.h"
#include "skill.h"
#include "shop.h"
#include "currency.h"

// Collision and movement tracing.
//
// game_state_can_move_to runs up to four times per occupancy test, three occupancy tests per frame
// for the slide fallbacks, so an unconditional printf here is a line of output per voxel per frame
// for as long as the player leans on a wall — enough to dominate a frame and to bury the output of
// anything else running. Set VERSE_MOVE_DEBUG to get it back.
static bool move_debug_enabled(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = (getenv("VERSE_MOVE_DEBUG") != NULL);
    return cached;
}

#define MOVE_DBG(...)              \
    do                             \
    {                              \
        if (move_debug_enabled())  \
            printf(__VA_ARGS__);   \
    } while (0)

// Defined with the shadow-cluster residency block; called from boundary/teleport/update paths.
static void game_state_sync_shadow_world_budgeted(GameState *state, int max_rebuilds);

GameState *game_state_create(void)
{
    GameState *state = (GameState *)calloc(1, sizeof(GameState));
    if (!state)
        return NULL;

    // Initialize state
    state->engine = engine_create();
    state->game_started = false;
    state->current_screen = GAME_SCREEN_MAIN_MENU;
    state->first_time = true;

    // Initialize player position (both integer and floating-point)
    state->player_x = 0;
    state->player_y = 0;
    state->player_z = 0;

    state->player_world_x = 0.0f;
    state->player_world_y = 0.0f;
    state->player_world_z = 0.0f;

    state->player_voxel_x = 0;
    state->player_voxel_y = 0;
    state->player_voxel_z = 0;

    // Initialize UI state
    state->show_exit_prompt = false;
    state->show_new_game_warning = false;
    state->show_loading = false;
    state->show_help_modal = false;
    state->show_tutorial_modal = false;
    state->show_world_editor_modal = false;
    state->show_name_input = false;
    state->show_chapter = false;
    state->show_scene = false;

    state->loot_corpse = NULL;
    memset(&state->loot_held, 0, sizeof(state->loot_held));
    state->loot_held_src = 0;
    state->loot_held_index = -1;
    wallet_clear(&state->purse);
    shop_session_clear(&state->shop);

    // Initialize loading screen state
    state->show_loading = false;
    state->loading_progress = 0;
    state->loading_total = 100;
    strcpy(state->loading_message, "");

    // Initialize async world generation state
    state->world_generation_active = false;
    state->world_generation_step = 0;
    state->last_world_gen_time = 0;

    // Chapter/Story state
    state->chapter_current_page = 0;
    state->chapter_current_char = 0;
    state->chapter_total_chars = 0;
    state->chapter_text_complete = false;
    state->chapter_fade_alpha = 0.0f;
    state->chapter_fade_start_time = 0;
    state->chapter_last_text_update = 0;
    storyline_init(&state->storyline);
    nav_aide_clear(&state->nav_aide);
    state->mission_tracked = true;
    state->hunters_shack_quest_started = false;
    state->quest_sheep_actor_id = 0;
    state->quest_sheep_loot_dropped = false;
    state->quest_town_has_target = false;
    state->quest_town_gx = 0;
    state->quest_town_gy = 0;
    state->waypoint_count = 0;
    state->selected_waypoint = -1;
    state->show_scene = false;

    // Initialize movement state
    state->movement_destination.has_destination = false;

    // Initialize floating-point destinations
    state->movement_destination.target_world_x = 0.0f;
    state->movement_destination.target_world_y = 0.0f;
    state->movement_destination.target_world_z = 0.0f;

    // Initialize legacy integer destinations
    state->movement_destination.target_x = 0;
    state->movement_destination.target_y = 0;
    state->movement_destination.target_z = 0;

    state->movement_destination.path_progress = 0.0f;

    // Initialize animation state
    state->movement_destination.is_animating = false;
    state->movement_destination.animation_progress = 0.0f;

    // Initialize floating-point animation positions
    state->movement_destination.start_world_x = 0.0f;
    state->movement_destination.start_world_y = 0.0f;
    state->movement_destination.start_world_z = 0.0f;

    // Initialize legacy integer animation positions
    state->movement_destination.start_x = 0;
    state->movement_destination.start_y = 0;
    state->movement_destination.start_z = 0;
    state->movement_destination.next_x = 0;
    state->movement_destination.next_y = 0;
    state->movement_destination.next_z = 0;

    state->movement_destination.animation_start_time = 0;
    state->movement_destination.movement_speed_ms = 500; // Base speed per tile for floating-point movement

    // Initialize status message
    strcpy(state->status_message, "Game state initialized");

    // Runtime clock (actors, fluids, fire). Starts stopped until play begins —
    // see game_state_ensure_world_clock.
    state->runtime_clock_running = false;
    state->runtime_clock_ms = 0;
    state->runtime_epoch_index = 0;
    state->time_scale = 1.0f;

    // Initialize toast
    state->toast_text[0] = '\0';
    state->toast_start_ms = 0;
    state->toast_duration_ms = 1000; // 1s default
    state->toast_y_offset = 0.0f;
    state->toast_active = false;

    state->player_flying = true; // default on

    state->player_universe_x = 0;
    state->player_universe_y = 0;
    state->player_universe_z = (uint64_t)UNIVERSE_HOME_Z;
    state->stamina_meter_alpha = 0.0f;

    player_controls_init(&state->controls);
    dialogue_init(&state->dialogue);
    // Any fixed seed will do; it only decides which leaf in a canopy stops a given shot, and a fixed
    // one makes a session reproducible.
    projectile_system_reset(&state->projectiles, 0xF1EBA11Du);
    debris_system_reset(&state->debris);
    voxel_debris_volume_reset(&state->debris_volumes);
    voxel_fracture_set_debris_volumes(&state->debris_volumes);

    state->fog = fog_atlas_create();

    return state;
}

void game_state_destroy(GameState *state)
{
    if (!state)
        return;

    if (state->engine)
    {
        engine_destroy(state->engine);
    }

    // Join the generation worker before freeing anything it might still be writing into.
    if (state->world_generation_job)
    {
        world_gen_job_destroy(state->world_generation_job);
        state->world_generation_job = NULL;
        state->world_generation_active = false;
    }

    // Before the worlds they borrow: a job may still be generating one, and the shadow world holds
    // pointers into the universe.
    for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
    {
        if (state->cell_refill_jobs[i])
        {
            cell_gen_job_destroy(state->cell_refill_jobs[i]);
            state->cell_refill_jobs[i] = NULL;
        }
    }

    if (state->shadow_world)
    {
        shadow_world_destroy(state->shadow_world);
        state->shadow_world = NULL;
    }

    fog_atlas_destroy(state->fog);
    state->fog = NULL;
    voxel_fracture_set_debris_volumes(NULL);

    // Whether game_worlds owns the world the player is standing in has to be decided before
    // game_worlds is destroyed, because destroying it clears the pointer this used to test. As
    // written, the test below was always true once game_worlds existed, so current_world — which is
    // normally one of game_worlds' own worlds — was freed a second time and quitting a game aborted.
    const bool game_worlds_owned_current_world = (state->game_worlds != NULL);

    if (state->game_worlds)
    {
        game_worlds_destroy(state->game_worlds);
        state->game_worlds = NULL;
    }

    if (state->current_world && !game_worlds_owned_current_world)
    {
        world_destroy(state->current_world);
    }

    if (state->main_menu_world)
    {
        world_destroy(state->main_menu_world);
    }

    storyline_destroy(&state->storyline);

    if (state->player)
    {
        actor_destroy(state->player);
        state->player = NULL;
    }

    free(state);
}

// Actors, fluids, and fire only step while the runtime clock is running. Play used to leave it
// stopped until the player found an obscure UI toggle, so wildlife sat frozen forever.
static void game_state_ensure_world_clock(GameState *state)
{
    if (!state)
        return;
    if (!state->runtime_clock_running)
    {
        state->runtime_clock_running = true;
        snprintf(state->status_message, sizeof(state->status_message), "Clock: running");
    }
}

bool game_state_init(GameState *state)
{
    if (!state)
        return false;

    printf("Initializing game state...\n");

    // Create main menu world (WORLD_SIZE cube)
    state->main_menu_world = world_create(WORLD_SIZE_CUBE);
    if (!state->main_menu_world)
    {
        printf("Failed to create main menu world\n");
        return false;
    }

    // Menu backdrop only — keep this cheap. Wilderness/scoured both run multi-second strata
    // pipelines and freeze the title→menu transition; HOME is ~200ms and still looks like VERSE.
    world_generate_with_type(state->main_menu_world, UNIVERSE_SEED_HEX, WORLD_TYPE_HOME);

    // Set initial player position in main menu world
    if (!game_state_place_player_at_surface_spawn(state))
    {
        game_state_set_player_position(state, 32, 8, 32);
    }

    // Initialize Universe (two initial history events)
    universe_init(&state->universe, UNIVERSE_SEED_HEX, WORLD_AFFINITY, 10);
    world_transition_init();
    game_log_init(&state->game_log);

    // The backdrop deliberately stays out of the universe. It used to be placed at cell (0,0,0),
    // which is not a spare slot: it is the wilderness cell directly beneath the home column, so a
    // player who walked off the island fell home -> cloud -> and landed in the menu backdrop, which
    // is generated as WORLD_TYPE_HOME above for the sake of a fast title screen. That is the "we end
    // up back at a home world on the bottom plane" report, and it also meant the real wilderness
    // world never landed there, because universe_place refuses an occupied cell and so the placement
    // in generate_full_neighbourhood silently failed.
    //
    // Nothing needed it there. window_render_main_menu looks the cell up and already falls back to
    // main_menu_world when it is empty, which is now always. Leaving the cell empty also settles who
    // owns the backdrop: game_state_cleanup destroys it, and it is no longer reachable from the
    // universe that universe_free walks.

    // Do not world_save_by_seed here: a 128³ serialize on every boot stalls startup for a
    // disposable menu backdrop.

    // Set initial screen to main menu
    state->current_screen = GAME_SCREEN_MAIN_MENU;

    printf("Game state initialized successfully\n");
    return true;
}

// Put the world the player was in back. Preferring the copy on disk is the whole point: it carries
// whatever they changed about it, and regenerating from the seed would silently undo all of that.
// Falling back to generating is still right when there is no file — a save whose worlds were deleted
// should drop the player onto the same island rather than refusing to load.
static World *game_state_restore_home_world(const char *base_seed)
{
    if (!base_seed)
        return NULL;

    World *home = world_create(WORLD_SIZE_CUBE);
    if (!home)
        return NULL;

    if (world_load_by_seed(home, base_seed))
    {
        // world_load adopts the file's dimensions and voxel buffer wholesale, which leaves every
        // cache derived from the old contents stale. Rebuilding them here is not optional: the
        // occupancy bitfield is what world_is_solid_fast reads, and physics would be consulting the
        // shape of a world that no longer exists.
        world_refresh_occupancy_bitfield(home);
        world_build_heightmap(home);
        printf("Restored home world from disk\n");
    }
    else
    {
        printf("No saved home world for this seed; generating it\n");
        world_generate_with_type(home, base_seed, WORLD_TYPE_HOME);
        world_refresh_occupancy_bitfield(home);
        world_build_heightmap(home);
    }
    return home;
}

static void game_state_teardown_play_worlds(GameState *state)
{
    if (!state)
        return;

    for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
    {
        if (state->cell_refill_jobs[i])
        {
            cell_gen_job_destroy(state->cell_refill_jobs[i]);
            state->cell_refill_jobs[i] = NULL;
        }
    }
    if (state->shadow_world)
    {
        shadow_world_destroy(state->shadow_world);
        state->shadow_world = NULL;
    }

    World *home_cell = universe_get(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z);
    if (home_cell)
    {
        World *taken = NULL;
        universe_remove(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z, &taken);
        (void)taken;
    }

    const bool worlds_owned = state->game_worlds != NULL;
    World *orphan = worlds_owned ? NULL : state->current_world;
    if (state->game_worlds)
    {
        game_worlds_destroy(state->game_worlds);
        state->game_worlds = NULL;
    }
    else if (orphan)
    {
        world_destroy(orphan);
    }
    state->current_world = NULL;
}

static bool game_state_apply_character_save(GameState *state, const CharacterSave *save)
{
    if (!state || !save)
        return false;

    strncpy(state->player_name, save->name, sizeof(state->player_name) - 1);
    state->player_name[sizeof(state->player_name) - 1] = '\0';

    game_state_teardown_play_worlds(state);

    state->game_worlds = game_worlds_create(save->name);
    if (!state->game_worlds || !state->game_worlds->base_seed)
    {
        printf("Failed to create game worlds for %s\n", save->name);
        return false;
    }
    if (save->world_seed[0] && strcmp(save->world_seed, state->game_worlds->base_seed) != 0)
    {
        printf("Save seed differs from the name-derived seed; using the saved one\n");
        char *replacement = strdup(save->world_seed);
        if (replacement)
        {
            free(state->game_worlds->base_seed);
            state->game_worlds->base_seed = replacement;
        }
    }

    World *home = game_state_restore_home_world(state->game_worlds->base_seed);
    if (!home)
    {
        printf("Failed to restore the home world\n");
        return false;
    }

    state->game_worlds->home_world = home;
    state->current_world = home;

    state->player_universe_x = 0;
    state->player_universe_y = 0;
    state->player_universe_z = (uint64_t)UNIVERSE_HOME_Z;
    if (!universe_get(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z))
        universe_place(&state->universe, 0, 0, (uint64_t)UNIVERSE_HOME_Z, home);

    if (save->x >= 0 && save->x < (int)home->width &&
        save->y >= 0 && save->y < (int)home->height &&
        save->z >= 0 && save->z < (int)home->depth)
    {
        game_state_set_player_position(state, save->x, save->y, save->z);
    }
    else
    {
        printf("Saved position (%d,%d,%d) is outside the world; respawning\n", save->x, save->y,
               save->z);
        game_state_place_player_at_surface_spawn(state);
    }

    player_controls_init(&state->controls);
    state->controls.velocity_z = 0.0f;

    if (!state->player)
    {
        const char *spirit_name = state->player_name[0] ? state->player_name : "Spirit";
        state->player = actor_create(spirit_name, "Ancient spirit", "home");
        if (state->player)
            inventory_seed_spirit_starter(&state->player->inventory);
    }
    if (state->player)
    {
        state->player->strength = (uint32_t)save->strength;
        state->player->dexterity = (uint32_t)save->dexterity;
        state->player->intelligence = (uint32_t)save->intelligence;
        state->player->wisdom = (uint32_t)save->wisdom;
        state->player->constitution = (uint32_t)save->constitution;
        state->player->luck = (uint32_t)save->luck;
        state->player->experience = (uint32_t)save->experience_points;
        state->player->level = (state->player->experience / 100u) + 1u;
        state->player->attribute_points =
            save->attribute_points > 0 ? (uint32_t)save->attribute_points : 0u;
        state->player->skill_points =
            save->skill_points > 0 ? (uint32_t)save->skill_points : 0u;
        state->player->unlocked_skills = save->unlocked_skills;
        memcpy(state->player->skill_ranks, save->skill_ranks, sizeof(state->player->skill_ranks));
        // Legacy saves: bit set but rank 0 → treat as rank I.
        for (SkillId sid = (SkillId)1; sid < SKILL_COUNT && (int)sid < 32; sid++)
        {
            if (skill_is_unlocked(state->player, sid) && state->player->skill_ranks[sid] == 0)
                state->player->skill_ranks[sid] = SKILL_RANK_BASIC;
        }
        actor_recalculate_stats(state->player);
        state->player->health = actor_max_health(state->player);
        state->player->stamina = actor_max_stamina(state->player);
        state->player->mana = actor_max_mana(state->player);
        if (inventory_used_slots(&state->player->inventory) == 0)
            inventory_seed_spirit_starter(&state->player->inventory);
        actor_set_position(state->player, state->player_world_x, state->player_world_y,
                           state->player_world_z);
        skill_refresh_hotbar(state, &state->controls);
        for (SkillId sid = (SkillId)1; sid < SKILL_COUNT; sid++)
        {
            if (!skill_is_unlocked(state->player, sid))
                continue;
            for (int slot = 0; slot < PLAYER_HOTBAR_SLOTS; slot++)
            {
                if (state->controls.hotbar[slot] == SKILL_NONE)
                {
                    skill_equip_hotbar(state, &state->controls, slot, sid);
                    break;
                }
            }
        }
    }

    // CharacterSave.gold is total copper in the player purse.
    wallet_from_copper(&state->purse, save->gold > 0 ? (int64_t)save->gold : 0);
    shop_session_clear(&state->shop);

    if (state->fog)
        fog_atlas_clear(state->fog);

    world_transition_set_game_worlds(state->game_worlds);

    state->game_started = true;
    state->current_screen = GAME_SCREEN_WORLD;
    state->world_generation_active = false;
    state->world_generation_step = state->loading_total > 0 ? state->loading_total : 1;
    state->save_browser_confirm_delete = false;
    game_state_ensure_world_clock(state);

    game_state_sync_shadow_world(state);
    game_state_pump_world_streaming(state);

    printf("Loaded %s at (%d,%d,%d)\n", save->name, state->player_voxel_x,
           state->player_voxel_y, state->player_voxel_z);
    return true;
}

bool game_state_load_game(GameState *state, const char *character_name)
{
    if (!state || !character_name)
        return false;

    printf("Loading game for character: %s\n", character_name);

    CharacterSave save;
    memset(&save, 0, sizeof(save));
    if (!character_load_game(character_name, &save))
    {
        printf("No save data for %s; starting a new game instead\n", character_name);
        return game_state_start_new_game(state);
    }

    return game_state_apply_character_save(state, &save);
}

bool game_state_load_from_path(GameState *state, const char *path)
{
    if (!state || !path)
        return false;

    printf("Loading game from %s\n", path);

    CharacterSave save;
    memset(&save, 0, sizeof(save));
    if (!character_load_from_path(path, &save))
        return false;

    return game_state_apply_character_save(state, &save);
}

void game_state_open_save_browser(GameState *state)
{
    if (!state)
        return;

    state->save_browser_count = character_list_save_files(state->save_browser_entries,
                                                          CHARACTER_SAVE_MAX_LIST);
    state->save_browser_selected = 0;
    state->save_browser_scroll = 0;
    state->save_browser_confirm_delete = false;
    state->current_screen = GAME_SCREEN_SAVE_BROWSER;
}

void game_state_save_browser_move(GameState *state, int delta)
{
    if (!state || state->save_browser_count <= 0)
        return;

    int next = state->save_browser_selected + delta;
    if (next < 0)
        next = 0;
    if (next >= state->save_browser_count)
        next = state->save_browser_count - 1;
    state->save_browser_selected = next;

    if (state->save_browser_selected < state->save_browser_scroll)
        state->save_browser_scroll = state->save_browser_selected;
    if (state->save_browser_selected >= state->save_browser_scroll + SAVE_BROWSER_VISIBLE_ROWS)
        state->save_browser_scroll = state->save_browser_selected - SAVE_BROWSER_VISIBLE_ROWS + 1;
}

bool game_state_save_browser_load(GameState *state)
{
    if (!state || state->save_browser_count <= 0)
        return false;
    if (state->save_browser_selected < 0 || state->save_browser_selected >= state->save_browser_count)
        return false;
    return game_state_load_from_path(state, state->save_browser_entries[state->save_browser_selected].path);
}

bool game_state_save_browser_delete(GameState *state)
{
    if (!state || state->save_browser_count <= 0)
        return false;
    if (state->save_browser_selected < 0 || state->save_browser_selected >= state->save_browser_count)
        return false;

    const char *path = state->save_browser_entries[state->save_browser_selected].path;
    if (!character_delete_save_file(path))
        return false;

    window_invalidate_saves_exist_cache();
    game_state_open_save_browser(state);
    return true;
}

bool game_state_save_game(GameState *state)
{
    if (!state || !state->game_started)
        return false;

    printf("Saving game...\n");

    // Save all worlds
    if (state->game_worlds)
    {
        if (!game_worlds_save_all(state->game_worlds, state->player_name))
        {
            printf("Failed to save game worlds\n");
            return false;
        }
    }

    // The player themselves. Without this the save was only the terrain: loading put a brand new
    // spirit at the spawn point, so anything the player had done to their own body or anywhere they
    // had walked to was gone even though the world they walked through came back.
    //
    // The seed goes in because it is what ties the save to its worlds. game_worlds_create derives it
    // from the character name, so it is recoverable either way, but storing it means a load can tell
    // that a save and the worlds on disk belong together rather than assuming it.
    const Actor *p = state->player;
    const char *seed = (state->game_worlds && state->game_worlds->base_seed)
                           ? state->game_worlds->base_seed
                           : "";
    if (!character_save_game(state->player_name,
                             state->player_voxel_x, state->player_voxel_y, state->player_voxel_z,
                             p ? (int)p->strength : 0,
                             p ? (int)p->dexterity : 0,
                             p ? (int)p->intelligence : 0,
                             p ? (int)p->wisdom : 0,
                             p ? (int)p->constitution : 0,
                             p ? (int)p->luck : 0,
                             p ? (int)p->experience : 0,
                             (int)wallet_total_copper(&state->purse),
                             seed,
                             p ? (int)p->attribute_points : 0,
                             p ? (int)p->skill_points : 0,
                             p ? p->unlocked_skills : 0u,
                             p ? p->skill_ranks : NULL))
    {
        printf("Failed to save character data\n");
        return false;
    }

    window_invalidate_saves_exist_cache();
    printf("Game saved successfully\n");
    return true;
}

// Check if any saved games exist
bool game_state_has_saved_games()
{
    // Use the existing character save system
    return character_any_saves_exist();
}

// Get the most recent save (based on file modification time)
bool game_state_get_most_recent_save(char *save_name, size_t save_name_size)
{
    if (!save_name || save_name_size == 0)
        return false;

    // Get list of character saves
    CharacterSave saves[10];
    int save_count = character_list_saves(saves, 10);

    if (save_count == 0)
    {
        return false;
    }

    // Find the most recent save by comparing timestamps
    int most_recent_index = 0;
    for (int i = 1; i < save_count; i++)
    {
        if (strcmp(saves[i].save_timestamp, saves[most_recent_index].save_timestamp) > 0)
        {
            most_recent_index = i;
        }
    }

    // Copy the character name of the most recent save
    strncpy(save_name, saves[most_recent_index].name, save_name_size - 1);
    save_name[save_name_size - 1] = '\0';

    return true;
}

// Load the most recent save automatically
bool game_state_load_most_recent_save(GameState *state)
{
    if (!state)
        return false;

    CharacterSaveEntry files[CHARACTER_SAVE_MAX_LIST];
    int n = character_list_save_files(files, 1);
    if (n <= 0)
    {
        printf("No saved games found\n");
        return false;
    }

    printf("Loading most recent save: %s\n", files[0].filename);
    return game_state_load_from_path(state, files[0].path);
}

bool game_state_start_new_game(GameState *state)
{
    if (!state)
        return false;

    printf("Starting new game...\n");

    // TEMPORARY: Generate a secure random seed and create a wilderness world for new games
    char seed_buffer[129] = {0};
    {
        unsigned char random_bytes[64];
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            printf("Failed to open /dev/urandom for seed generation\n");
            return false;
        }
        ssize_t got = read(fd, random_bytes, sizeof(random_bytes));
        close(fd);
        if (got <= 0) {
            printf("Failed to read random seed bytes\n");
            return false;
        }
        // Use first 32 bytes for a 64-hex-char seed
        for (int i = 0; i < 32; i++) {
            snprintf(seed_buffer + (i * 2), 3, "%02x", random_bytes[i]);
        }
    }

    World *wilderness = world_create(WORLD_SIZE_CUBE);
    if (!wilderness) {
        printf("Failed to create wilderness world\n");
        return false;
    }
    world_generate_with_type(wilderness, seed_buffer, WORLD_TYPE_WILDERNESS);

    state->current_world = wilderness;
    state->game_worlds = NULL; // not used in this temporary mode

    // Find safe spawn on the wilderness surface
    if (!game_state_place_player_at_surface_spawn(state))
    {
        printf("Failed to find safe spawn position\n");
        return false;
    }

    if (state->fog)
        fog_atlas_clear(state->fog);

    // Set game state
    state->game_started = true;
    state->current_screen = GAME_SCREEN_WORLD;
    game_state_ensure_world_clock(state);

    if (!state->player)
    {
        const char *spirit_name = state->player_name[0] ? state->player_name : "Spirit";
        state->player = actor_create(spirit_name, "Ancient spirit", "wilderness");
        if (state->player)
        {
            state->player->turn_speed = 240;
            state->player->is_flying = true;
            inventory_seed_spirit_starter(&state->player->inventory);
        }
    }

    snprintf(state->status_message, sizeof(state->status_message),
             "New wilderness game started at (%d, %d, %d)! Seed: %.12s...",
             state->player_x, state->player_y, state->player_z, seed_buffer);

    printf("New wilderness game started at (%d, %d, %d) with seed %s\n",
           state->player_x, state->player_y, state->player_z, seed_buffer);

    return true;
}

World *game_state_create_game_world(GameState *state)
{
    if (!state)
        return NULL;

    printf("Creating new game world...\n");

    World *world = world_create(WORLD_SIZE_CUBE);
    if (!world)
    {
        printf("Failed to create world\n");
        return NULL;
    }

    // Generate world content
    world_generate(world, "game_world");

    printf("Created new game world\n");
    return world;
}

bool game_state_find_safe_spawn(GameState *state, int *x, int *y, int *z)
{
    if (!state || !x || !y || !z)
        return false;

    World *world = state->current_world ? state->current_world : state->main_menu_world;
    if (!world)
        return false;

    // Use type-specific spawn logic
    SpawnPosition spawn = (world->generation_type == WORLD_TYPE_WILDERNESS)
                            ? world_find_wilderness_spawn(world, false)
                            : world_find_best_spawn_position(world);
    if (spawn.is_safe)
    {
        *x = spawn.x;
        *y = spawn.y;
        *z = spawn.z;
        printf("Found safe spawn at (%d, %d, %d): %s\n",
               spawn.x, spawn.y, spawn.z,
               spawn.spawn_reason ? spawn.spawn_reason : "Safe position");

        // Clean up allocated memory
        if (spawn.spawn_reason)
        {
            free(spawn.spawn_reason);
        }
        return true;
    }

    printf("Failed to find any safe spawn position in world\n");
    return false;
}

void game_state_set_player_position(GameState *state, int x, int y, int z)
{
    if (!state)
        return;

    state->player_world_x = (float)x + 0.5f;
    state->player_world_y = (float)y + 0.5f;
    state->player_world_z = (float)z + 0.5f;
    state->player_x = x;
    state->player_y = y;
    state->player_z = z;
    state->player_voxel_x = x;
    state->player_voxel_y = y;
    state->player_voxel_z = z;
}

static bool game_state_surface_spawn_fallback(GameState *state, int *x, int *y, int *z)
{
    if (!state || !x || !y || !z)
        return false;

    World *world = state->current_world ? state->current_world : state->main_menu_world;
    if (!world)
        return false;

    if (world->generation_type == WORLD_TYPE_HOME)
    {
        SpawnPosition home = world_get_home_spawn_position(world);
        if (home.is_safe)
        {
            *x = home.x;
            *y = home.y;
            *z = home.z;
            if (home.spawn_reason)
                free(home.spawn_reason);
            return true;
        }
        if (home.spawn_reason)
            free(home.spawn_reason);
    }

    int cx = (int)(world->width / 2);
    int cy = (int)(world->height / 2);
    int safe_z = world_find_safe_ground(world, cx, cy);
    if (safe_z >= 0)
    {
        *x = cx;
        *y = cy;
        *z = safe_z;
        return true;
    }

    return false;
}

bool game_state_place_player_at_surface_spawn(GameState *state)
{
    if (!state)
        return false;

    int x = 0, y = 0, z = 0;
    if (!game_state_find_safe_spawn(state, &x, &y, &z))
    {
        if (!game_state_surface_spawn_fallback(state, &x, &y, &z))
        {
            printf("Failed to place player at surface spawn\n");
            return false;
        }
        printf("Using surface spawn fallback at (%d, %d, %d)\n", x, y, z);
    }

    // Spawn one voxel clear of the ground rather than flush on it, and let gravity settle the
    // player onto the surface. Arriving under gravity means the landing is what proves the
    // spawn column is solid, instead of trusting the search that picked it.
    int spawn_z = z + 1;
    World *world = state->current_world ? state->current_world : state->main_menu_world;
    if (world && spawn_z >= (int)world->depth)
        spawn_z = z; // no headroom at the top of the world: stand where we found ground

    game_state_set_player_position(state, x, y, spawn_z);

    // Spirit defaults to hover-flight — a fairy float above the spawn column, not a hard landing.
    player_controls_set_spirit_hover(state, true);

    player_controls_reset(&state->controls);

    printf("Player spawned at (%d, %d, %d), one voxel above ground at z=%d; "
           "world (%.2f, %.2f, %.2f) [spirit hover]\n",
           x, y, spawn_z, z, state->player_world_x, state->player_world_y,
           state->player_world_z);
    return true;
}

// The world the player's body is in, whether that is a game world or the menu backdrop.
static World *game_state_physics_world(const GameState *state)
{
    if (!state)
        return NULL;
    return state->current_world ? state->current_world : state->main_menu_world;
}

// True when a voxel inside this world holds a player whose feet are at world_z up.
//
// Whatever holds the player up has to be something they could not have walked into, or the two
// disagree: leaves that let a body through cannot also stop it falling, and a player standing on a
// canopy would be standing on nothing.
//
// The floor of the world is deliberately not support. Worlds are stacked, so what is under the lowest
// voxel is usually the world below rather than nothing, and treating the boundary as solid here is
// what used to end a fall off the island at the bottom of the home world instead of carrying it into
// the cloud layer. Callers that need "is the player standing on something" rather than "is there
// ground in this world" want game_state_has_ground_support.
static bool game_state_has_voxel_support(const GameState *state, float world_z)
{
    World *world = game_state_physics_world(state);
    if (!world)
        return true;

    int voxel_z = (int)floorf(world_z);
    if (voxel_z <= 0)
        return false;

    Voxel *below = world_get_voxel(world, state->player_voxel_x, state->player_voxel_y,
                                   voxel_z - 1);
    return below && world_voxel_type_blocks_movement(below->type);
}

// True when a player whose feet are at world_z is at rest rather than falling. The bottom of the
// world counts: a player who has come to a stop there — because there is no layer below, or because
// the one below is solid at that column — is standing, and should be able to jump.
static bool game_state_has_ground_support(const GameState *state, float world_z)
{
    if ((int)floorf(world_z) <= 0)
        return true;
    return game_state_has_voxel_support(state, world_z);
}

// True when a player whose feet are at world_z fits: the voxel their body is in and the one their
// head is in are both clear. The same two-voxel volume game_state_can_move_to checks horizontally,
// so rising into a ceiling stops where walking into a wall would.
static bool game_state_has_head_clearance(const GameState *state, float world_z)
{
    World *world = game_state_physics_world(state);
    if (!world)
        return true;

    const int body_z = (int)floorf(world_z);
    if (body_z + 1 >= (int)world->depth)
        return true; // open sky above the world; the caller clamps instead

    for (int z = body_z; z <= body_z + 1; z++)
    {
        if (z < 0)
            continue;
        Voxel *v = world_get_voxel(world, state->player_voxel_x, state->player_voxel_y, z);
        if (!v || world_voxel_type_blocks_movement(v->type))
            return false;
    }
    return true;
}

bool game_state_player_is_grounded(const GameState *state)
{
    if (!state)
        return false;
    return game_state_has_ground_support(state, state->player_world_z);
}

float game_state_player_gravity(const GameState *state)
{
    // world->gravity is already voxels/second^2, matching player positions and actor/projectile
    // motion, so a world's setting is the fall rate everything in it shares.
    World *world = game_state_physics_world(state);
    float gravity = world ? world_get_gravity(world) : GRAVITY_DEFAULT;
    if (gravity <= 0.0f)
        gravity = GRAVITY_DEFAULT;
    return gravity;
}

float game_state_player_jump_velocity(const GameState *state)
{
    // v = sqrt(2*g*h) is the launch speed that just reaches height h. Solving for the height wanted
    // rather than fixing the speed is what keeps a jump able to clear a step: the same 5 voxels/s
    // that reaches shoulder height under light gravity does not leave the ground under heavy.
    // Strength does not enter here — it only scales the takeoff impulse in player_controls.
    return sqrtf(2.0f * game_state_player_gravity(state) * PLAYER_JUMP_APEX_VOXELS);
}

// Terminal velocity in voxels/second. Tunnelling is prevented by the voxel-at-a-time stepping
// in the fall loop, not by this; the cap is here to keep a long drop readable rather than a
// blur, and to stop velocity growing without bound while the player is off the island.
#define PLAYER_TERMINAL_VELOCITY 200.0f

static void game_state_collect_stream_job(GameState *state, int slot);

// True when this column has no floor in the current world, which is the home-island case of walking
// off the plateau: the body is still in the home cell, but the next thing under it is another layer.
// A jump on solid ground does not count, because the floor is still in the column.
static bool game_state_column_falls_out_of_world(const GameState *state)
{
    if (!state)
        return false;
    World *world = game_state_physics_world(state);
    if (!world)
        return false;

    const int x = state->player_voxel_x;
    const int y = state->player_voxel_y;
    if (x < 0 || y < 0 || x >= (int)world->width || y >= (int)world->height)
        return true;

    const int feet_z = state->player_voxel_z;
    const int top = (feet_z < (int)world->depth) ? feet_z : (int)world->depth;
    for (int z = 0; z < top; z++)
    {
        Voxel *v = world_get_voxel(world, x, y, z);
        if (v && world_voxel_type_blocks_movement(v->type))
            return false;
    }
    return true;
}

// The wilderness plane (universe z=0) is always drawn as distance terrain from the sky island.
// Streaming still treats a fall / already-being-below-home as the moment neighbouring ground cells
// become urgent to walk into.
static bool game_state_wilderness_plane_active(const GameState *state)
{
    if (!state)
        return false;
    if (state->player_universe_z < (uint64_t)UNIVERSE_HOME_Z)
        return true;
    return game_state_column_falls_out_of_world(state);
}

static bool game_state_neighbor_visible(const GameState *state, int dz)
{
    if (!state)
        return true;
    int64_t target_uz = (int64_t)state->player_universe_z + dz;
    if (target_uz < 0)
        return false;
    // Prefer downward scenery from the sky island: skip the far sky ring two layers above home.
    // One cloud/sky layer up is enough; wilderness is two layers down and must stay visible.
    if (dz > 1 && state->player_universe_z >= (uint64_t)UNIVERSE_HOME_Z)
        return false;
    return true;
}

static World *game_state_ensure_universe_cell(GameState *state, uint64_t ux, uint64_t uy, uint64_t uz)
{
    if (!state || !state->game_worlds || !state->game_worlds->base_seed)
        return NULL;

    World *existing = universe_get(&state->universe, ux, uy, uz);
    if (existing)
        return existing;

    // A streamer already building this cell is the work we would do here. Waiting for it avoids a
    // second full generation of the same world, which is what used to hitch on the frame a falling
    // player arrived in a wilderness cell that the fall itself had already asked for.
    for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
    {
        if (!state->cell_refill_jobs[i])
            continue;
        uint64_t jx = 0, jy = 0, jz = 0;
        cell_gen_job_cell(state->cell_refill_jobs[i], &jx, &jy, &jz);
        if (jx != ux || jy != uy || jz != uz)
            continue;
        while (!cell_gen_job_is_complete(state->cell_refill_jobs[i]))
            usleep(1000);
        game_state_collect_stream_job(state, i);
        return universe_get(&state->universe, ux, uy, uz);
    }

    // Prefer the task scheduler over generating on the play thread. Sync generation here used to
    // contend with in-flight stream jobs for CPU and stall the frame for the full wilderness cost
    // (~seconds). A one-off job shares the worker pool; without a pool, cell_gen_job_start still
    // runs inline and returns complete, so headless tools keep working.
    CellGenJob *job = cell_gen_job_start(state->game_worlds->base_seed, ux, uy, uz);
    if (!job)
        return NULL;
    while (!cell_gen_job_is_complete(job))
        usleep(1000);
    World *made = cell_gen_job_take_world(job);
    cell_gen_job_destroy(job);
    if (!made)
        return NULL;

    if (universe_get(&state->universe, ux, uy, uz) ||
        !universe_place(&state->universe, ux, uy, uz, made))
    {
        world_destroy(made);
        return universe_get(&state->universe, ux, uy, uz);
    }

    made->universe_context = &state->universe;
    made->universe_x = ux;
    made->universe_y = uy;
    made->universe_z = uz;
    return made;
}

// Floor division and modulo, which is what mapping a coordinate onto a grid of worlds needs and what
// C's / and % do not give for negative operands: -1 / 128 is 0 and -1 % 128 is -1, so a step one
// voxel west of a world's origin would resolve to that same world at a negative local coordinate
// instead of to the last column of its western neighbour.
static int game_state_floor_div(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

static int game_state_floor_mod(int a, int b)
{
    int r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}

// The world that actually contains a column expressed in the current world's coordinates, and the
// column's coordinates inside it. Coordinates outside the current world resolve to its neighbours.
//
// This is what lets a body walk off the side of a world. Collision used to answer "out of bounds" for
// anything past the edge, which fenced the player inside whichever world they were standing in — an
// invisible wall a third of a voxel short of the rim, in a universe whose whole point is that worlds
// are stacked and tiled. Returns NULL when the neighbour has not been streamed in yet, which reads as
// solid to the caller: stopping at the seam is the safe way to lose that race.
//
// Universe coordinates are unsigned keys that behave as signed values reinterpreted, which is the
// convention the streamer and the shadow world already use for the cells west and south of the
// origin; the cast here matches them so both agree on which cell they mean.
static World *game_state_resolve_column(GameState *state, int x, int y, int *lx, int *ly)
{
    World *world = game_state_physics_world(state);
    if (!world)
        return NULL;

    const int w = (int)world->width;
    const int h = (int)world->height;
    if (w <= 0 || h <= 0)
        return NULL;

    const int cx = game_state_floor_div(x, w);
    const int cy = game_state_floor_div(y, h);
    *lx = game_state_floor_mod(x, w);
    *ly = game_state_floor_mod(y, h);

    if (cx == 0 && cy == 0)
        return world;

    return universe_get(&state->universe,
                        (uint64_t)((int64_t)state->player_universe_x + cx),
                        (uint64_t)((int64_t)state->player_universe_y + cy),
                        state->player_universe_z);
}

// An inhabited body lives in the current world's runtime list. Crossing a seam changes
// current_world, so without this the next dominate lookup fails and the spirit is released —
// and eviction can destroy the left-behind MobActor. Pull the entry out before the switch and
// drop it into the destination with player-local coordinates.
static void game_state_carry_dominated_actor(GameState *state, World *from, World *to)
{
    if (!state || !from || !to || from == to || state->dominated_actor_id == 0)
        return;

    Actor carried;
    if (!world_extract_runtime_actor_by_id(from, state->dominated_actor_id, &carried))
        return;

    carried.x = (double)state->player_world_x;
    carried.y = (double)state->player_world_y;
    carried.z = (double)state->player_world_z;
    carried.velocity_x = (double)state->controls.velocity_x;
    carried.velocity_y = (double)state->controls.velocity_y;
    carried.velocity_z = (double)state->controls.velocity_z;
    carried.is_controlled = true;
    if (carried.extra_data)
    {
        MobActor *mob = (MobActor *)carried.extra_data;
        mob->base.x = carried.x;
        mob->base.y = carried.y;
        mob->base.z = carried.z;
        mob->base.velocity_x = carried.velocity_x;
        mob->base.velocity_y = carried.velocity_y;
        mob->base.velocity_z = carried.velocity_z;
        mob->base.is_controlled = true;
    }

    if (!world_add_runtime_actor(to, &carried))
    {
        // OOM: put the body back where it was so we do not leak the MobActor.
        (void)world_add_runtime_actor(from, &carried);
    }
}

bool game_state_cross_world_boundary(GameState *state)
{
    if (!state)
        return false;
    World *world = game_state_physics_world(state);
    if (!world)
        return false;

    const int w = (int)world->width;
    const int h = (int)world->height;
    if (w <= 0 || h <= 0)
        return false;

    const int cx = game_state_floor_div((int)floorf(state->player_world_x), w);
    const int cy = game_state_floor_div((int)floorf(state->player_world_y), h);
    if (cx == 0 && cy == 0)
        return false;

    const uint64_t ux = (uint64_t)((int64_t)state->player_universe_x + cx);
    const uint64_t uy = (uint64_t)((int64_t)state->player_universe_y + cy);
    const uint64_t uz = state->player_universe_z;

    World *target = universe_get(&state->universe, ux, uy, uz);
    if (!target)
        target = game_state_ensure_universe_cell(state, ux, uy, uz);
    if (!target)
    {
        // Nothing over there to stand in. Put the body back inside the world it is in rather than
        // leaving it outside every world, where nothing holds it up and nothing draws it.
        if (state->player_world_x < 0.0f)
            state->player_world_x = 0.5f;
        else if (state->player_world_x >= (float)w)
            state->player_world_x = (float)w - 0.5f;
        if (state->player_world_y < 0.0f)
            state->player_world_y = 0.5f;
        else if (state->player_world_y >= (float)h)
            state->player_world_y = (float)h - 0.5f;
        game_state_sync_positions(state);
        return false;
    }

    // Same body, same height, same speed — only the frame of reference moves. Velocity is kept on
    // horizontal seams and on vertical layer changes alike (capped at terminal / glide sink).
    state->current_world = target;
    state->player_universe_x = ux;
    state->player_universe_y = uy;
    state->player_world_x -= (float)(cx * w);
    state->player_world_y -= (float)(cy * h);
    game_state_sync_positions(state);

    game_state_carry_dominated_actor(state, world, target);

    if (state->player)
        actor_set_position(state->player, state->player_world_x, state->player_world_y,
                           state->player_world_z);

    if (state->shadow_world)
    {
        int rebase_x = 0, rebase_y = 0, rebase_z = 0;
        (void)shadow_world_recenter(state->shadow_world, ux, uy, uz, &rebase_x, &rebase_y,
                                    &rebase_z);
    }
    // Recenter clears every pyramid bit. Rebuilding the full 5x5x5 cluster here is ~100ms+ and is
    // the hitch players feel at a world edge. Centre + a couple of faces are enough to walk and
    // collide; the rest drains on subsequent update/physics frames.
    game_state_sync_shadow_world_budgeted(state, 3);

    game_state_evict_distant_worlds(state);
    game_state_pump_world_streaming(state);
    return true;
}

bool game_state_teleport_home(GameState *state)
{
    if (!state || !state->game_started)
        return false;

    // Spirit only: leave any inhabited body where it stands under AI control.
    if (player_controls_is_dominating(state))
        player_controls_release_dominate(state);

    const uint64_t home_z = (uint64_t)UNIVERSE_HOME_Z;
    World *home = NULL;
    if (state->game_worlds)
        home = state->game_worlds->home_world;
    if (!home)
        home = universe_get(&state->universe, 0, 0, home_z);
    if (!home)
        return false;

    if (!universe_get(&state->universe, 0, 0, home_z))
    {
        if (!universe_place(&state->universe, 0, 0, home_z, home))
            return false;
    }

    state->current_world = home;
    state->player_universe_x = 0;
    state->player_universe_y = 0;
    state->player_universe_z = home_z;

    if (!game_state_place_player_at_surface_spawn(state))
        return false;

    state->controls.velocity_x = 0.0f;
    state->controls.velocity_y = 0.0f;
    state->controls.velocity_z = 0.0f;

    if (state->player)
        actor_set_position(state->player, state->player_world_x, state->player_world_y,
                           state->player_world_z);

    if (state->shadow_world)
    {
        int rebase_x = 0, rebase_y = 0, rebase_z = 0;
        (void)shadow_world_recenter(state->shadow_world, 0, 0, home_z, &rebase_x, &rebase_y,
                                    &rebase_z);
    }
    game_state_sync_shadow_world_budgeted(state, 3);
    game_state_evict_distant_worlds(state);
    game_state_pump_world_streaming(state);

    strncpy(state->toast_text, "Returned home", sizeof(state->toast_text) - 1);
    state->toast_text[sizeof(state->toast_text) - 1] = '\0';
    state->toast_start_ms = SDL_GetTicks();
    state->toast_duration_ms = 3000;
    state->toast_y_offset = 0.0f;
    state->toast_active = true;
    return true;
}

#define TELEPORT_SETTLEMENT_SEARCH_RADIUS 256

bool game_state_teleport_to_settlement(GameState *state, int scale)
{
    if (!state || !state->game_started)
        return false;
    if (scale < 1 || scale > 9)
        return false;

    if (player_controls_is_dominating(state))
        player_controls_release_dominate(state);

    const int from_gx = (int)(int64_t)state->player_universe_x;
    const int from_gy = (int)(int64_t)state->player_universe_y;
    int tgx = from_gx;
    int tgy = from_gy;

    // Already standing in a matching wilderness settlement — just re-seat at the anchor.
    const bool already_here = state->player_universe_z == 0 &&
                              universe_settlement_scale(from_gx, from_gy) == scale;
    if (!already_here)
    {
        if (!universe_nearest_settlement_of_scale(from_gx, from_gy, scale,
                                                 TELEPORT_SETTLEMENT_SEARCH_RADIUS, &tgx, &tgy))
            return false;
    }

    const uint64_t ux = (uint64_t)(int64_t)tgx;
    const uint64_t uy = (uint64_t)(int64_t)tgy;
    const uint64_t uz = 0;

    World *target = universe_get(&state->universe, ux, uy, uz);
    if (!target)
        target = game_state_ensure_universe_cell(state, ux, uy, uz);
    if (!target)
        return false;

    state->current_world = target;
    state->player_universe_x = ux;
    state->player_universe_y = uy;
    state->player_universe_z = uz;

    bool placed = false;
    int ax = 0, ay = 0, az = 0;
    if (settlement_anchor(target, &ax, &ay, &az))
    {
        int spawn_z = az + 1;
        if (spawn_z >= (int)target->depth)
            spawn_z = az;
        game_state_set_player_position(state, ax, ay, spawn_z);
        player_controls_set_spirit_hover(state, true);
        player_controls_reset(&state->controls);
        placed = true;
    }
    if (!placed && !game_state_place_player_at_surface_spawn(state))
        return false;

    state->controls.velocity_x = 0.0f;
    state->controls.velocity_y = 0.0f;
    state->controls.velocity_z = 0.0f;

    if (state->player)
        actor_set_position(state->player, state->player_world_x, state->player_world_y,
                           state->player_world_z);

    if (state->shadow_world)
    {
        int rebase_x = 0, rebase_y = 0, rebase_z = 0;
        (void)shadow_world_recenter(state->shadow_world, ux, uy, uz, &rebase_x, &rebase_y,
                                    &rebase_z);
    }
    game_state_sync_shadow_world_budgeted(state, 3);
    game_state_evict_distant_worlds(state);
    game_state_pump_world_streaming(state);

    game_state_discover_settlement_waypoint(state, tgx, tgy);

    char toast[96];
    snprintf(toast, sizeof(toast), "Teleported to %s (%d,%d)", settlement_scale_label(scale), tgx,
             tgy);
    strncpy(state->toast_text, toast, sizeof(state->toast_text) - 1);
    state->toast_text[sizeof(state->toast_text) - 1] = '\0';
    state->toast_start_ms = SDL_GetTicks();
    state->toast_duration_ms = 3000;
    state->toast_y_offset = 0.0f;
    state->toast_active = true;
    return true;
}

static void game_state_show_toast(GameState *state, const char *text, Uint32 duration_ms)
{
    if (!state || !text)
        return;
    strncpy(state->toast_text, text, sizeof(state->toast_text) - 1);
    state->toast_text[sizeof(state->toast_text) - 1] = '\0';
    state->toast_start_ms = SDL_GetTicks();
    state->toast_duration_ms = duration_ms;
    state->toast_y_offset = 0.0f;
    state->toast_active = true;
}

static World *game_state_ensure_home_drop_world(GameState *state)
{
    if (!state)
        return NULL;
    World *drop = universe_get(&state->universe, 0, 0, 0);
    if (!drop)
        drop = game_state_ensure_universe_cell(state, 0, 0, 0);
    return drop;
}


static Actor *game_state_find_runtime_actor(World *world, uint32_t id)
{
    if (!world || !world->runtime_actors || id == 0)
        return NULL;
    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        if (world->runtime_actors[i].id == id)
            return &world->runtime_actors[i];
    }
    return NULL;
}

static bool game_state_quest_id_is(const GameState *state, const char *id)
{
    if (!state || !id)
        return false;
    const StorylineQuest *quest = storyline_active_quest(&state->storyline);
    return quest && quest->id && strcmp(quest->id, id) == 0;
}

static void game_state_aim_nav_at(GameState *state, uint64_t ux, uint64_t uy, uint64_t uz,
                                 float x, float y, float z, const char *label)
{
    if (!state)
        return;
    // A fresh objective (aide was off) turns tracking back on so new missions are visible by default.
    const bool was_inactive = !nav_aide_active(&state->nav_aide);
    nav_aide_set_target(&state->nav_aide, ux, uy, uz, x, y, z, label);
    if (was_inactive)
        state->mission_tracked = true;
}

int game_state_discover_settlement_waypoint(GameState *state, int gx, int gy)
{
    if (!state)
        return -1;
    const int scale = universe_settlement_scale(gx, gy);
    if (scale <= 0)
        return -1;

    for (int i = 0; i < state->waypoint_count; i++)
    {
        if (state->waypoints[i].gx == gx && state->waypoints[i].gy == gy)
        {
            state->waypoints[i].scale = scale;
            return i;
        }
    }
    if (state->waypoint_count >= GAME_WAYPOINT_MAX)
        return -1;

    const int idx = state->waypoint_count++;
    state->waypoints[idx].gx = gx;
    state->waypoints[idx].gy = gy;
    state->waypoints[idx].scale = scale;
    if (gx == 0 && gy == 0)
        snprintf(state->waypoints[idx].label, sizeof(state->waypoints[idx].label),
                 "Hunter's Shack");
    else if (state->universe.chronicle_ready &&
             chronicle_site_label(&state->universe.chronicle, gx, gy,
                                  state->waypoints[idx].label,
                                  sizeof(state->waypoints[idx].label)))
        ; // label filled
    else
        snprintf(state->waypoints[idx].label, sizeof(state->waypoints[idx].label), "%s (%d,%d)",
                 settlement_scale_label(scale), gx, gy);
    return idx;
}

void game_state_try_discover_current_settlement(GameState *state)
{
    if (!state || state->player_universe_z != 0)
        return;
    const int gx = (int)(int64_t)state->player_universe_x;
    const int gy = (int)(int64_t)state->player_universe_y;
    if (universe_settlement_scale(gx, gy) <= 0)
        return;
    const int before = state->waypoint_count;
    const int idx = game_state_discover_settlement_waypoint(state, gx, gy);
    if (idx >= 0 && state->waypoint_count > before)
    {
        char toast[96];
        snprintf(toast, sizeof(toast), "Waypoint: %s", state->waypoints[idx].label);
        game_state_show_toast(state, toast, 3500);
        game_state_log_site_majors(state, gx, gy, 3);
    }
    game_state_refresh_adventure_hooks(state);
}

bool game_state_select_waypoint(GameState *state, int index)
{
    if (!state || index < 0 || index >= state->waypoint_count)
        return false;

    state->selected_waypoint = index;
    state->mission_tracked = true;

    const int gx = state->waypoints[index].gx;
    const int gy = state->waypoints[index].gy;
    const uint64_t ux = (uint64_t)(int64_t)gx;
    const uint64_t uy = (uint64_t)(int64_t)gy;
    World *cell = universe_get(&state->universe, ux, uy, 0);
    int ax = 0, ay = 0, az = 0;
    if (cell && settlement_anchor(cell, &ax, &ay, &az))
    {
        game_state_aim_nav_at(state, ux, uy, 0, (float)ax + 0.5f, (float)ay + 0.5f,
                              (float)az + 0.5f, state->waypoints[index].label);
    }
    else
    {
        const float mid = (float)WORLD_SIZE_X * 0.5f + 0.5f;
        game_state_aim_nav_at(state, ux, uy, 0, mid, mid, 4.0f, state->waypoints[index].label);
    }

    char toast[96];
    snprintf(toast, sizeof(toast), "Navigating to %s", state->waypoints[index].label);
    game_state_show_toast(state, toast, 3500);
    return true;
}

int game_state_waypoint_count(const GameState *state)
{
    return state ? state->waypoint_count : 0;
}

int game_state_selected_waypoint(const GameState *state)
{
    return state ? state->selected_waypoint : -1;
}

const char *game_state_waypoint_label(const GameState *state, int index)
{
    if (!state || index < 0 || index >= state->waypoint_count)
        return "";
    return state->waypoints[index].label;
}

bool game_state_waypoint_cell(const GameState *state, int index, int *out_gx, int *out_gy,
                              int *out_scale)
{
    if (!state || index < 0 || index >= state->waypoint_count)
        return false;
    if (out_gx)
        *out_gx = state->waypoints[index].gx;
    if (out_gy)
        *out_gy = state->waypoints[index].gy;
    if (out_scale)
        *out_scale = state->waypoints[index].scale;
    return true;
}

static bool game_state_begin_sheep_wool_quest(GameState *state);
static bool game_state_begin_nearest_town_quest(GameState *state);
static bool game_state_begin_find_shopkeeper_quest(GameState *state);
static bool game_state_begin_sell_wool_quest(GameState *state);

static bool game_state_begin_find_note_quest(GameState *state)
{
    if (!state)
        return false;
    World *drop = game_state_ensure_home_drop_world(state);
    int nx = 0, ny = 0, nz = 0;
    if (!drop || !settlement_note(drop, &nx, &ny, &nz))
        return false;
    if (!storyline_activate_quest(&state->storyline, "00002-find-hunters-note"))
        return false;
    game_state_aim_nav_at(state, 0, 0, 0, (float)nx + 0.5f, (float)ny + 0.5f, (float)nz + 0.5f,
                          "Hunter's Note");
    game_state_show_toast(state, "Search the shack for a note", 4000);
    return true;
}

static bool game_state_spawn_quest_sheep(GameState *state)
{
    if (!state || !state->current_world)
        return false;
    if (state->quest_sheep_actor_id != 0)
        return true;

    World *world = state->current_world;
    int ax = (int)world->width / 2;
    int ay = (int)world->height / 2;
    int az = 2;
    if (settlement_has_anchor(world))
        settlement_anchor(world, &ax, &ay, &az);

    // Pasture a short walk from the shack.
    const float sx = (float)ax + 10.5f;
    const float sy = (float)ay - 6.5f;
    float sz = (float)az;
    // Keep the spawn on the column surface when possible.
    for (int z = (int)world->depth - 2; z >= 0; z--)
    {
        const Voxel *v = world_get_voxel(world, (uint32_t)sx, (uint32_t)sy, (uint32_t)z);
        if (v && v->type != VOXEL_AIR && v->type != VOXEL_WATER)
        {
            sz = (float)z + 1.0f;
            break;
        }
    }

    MobActor *sheep = mob_actor_create_sheep((double)sx, (double)sy, (double)sz);
    if (!sheep)
        return false;
    strncpy(sheep->base.name, "Marked Sheep", sizeof(sheep->base.name) - 1);
    strncpy(sheep->base.description, "A sheep tagged in the hunter's note",
            sizeof(sheep->base.description) - 1);
    sheep->base.name[sizeof(sheep->base.name) - 1] = '\0';
    sheep->base.description[sizeof(sheep->base.description) - 1] = '\0';

    if (!mob_actor_spawn_in_world(world, sheep))
        return false;

    state->quest_sheep_actor_id = sheep->base.id;
    state->quest_sheep_loot_dropped = false;
    game_state_aim_nav_at(state, state->player_universe_x, state->player_universe_y,
                          state->player_universe_z, sx, sy, sz, "Marked Sheep");
    return true;
}

static bool game_state_begin_sheep_wool_quest(GameState *state)
{
    if (!state)
        return false;
    if (!storyline_activate_quest(&state->storyline, "00003-harvest-sheep-wool"))
        return false;
    if (!game_state_spawn_quest_sheep(state))
    {
        game_state_show_toast(state, "Find and shear a sheep for wool", 4000);
        return true;
    }
    game_state_show_toast(state, "Kill the marked sheep and gather wool", 4500);
    return true;
}

// First time the player stands on the wilderness surface: assign the hunter's-shack quest and aim
// the navigational aide at the home-drop settlement building.
static void game_state_try_begin_hunters_shack_quest(GameState *state)
{
    if (!state || state->hunters_shack_quest_started)
        return;
    if (state->player_universe_z != 0)
        return;

    World *drop = game_state_ensure_home_drop_world(state);
    if (!drop || !settlement_has_anchor(drop))
        return;

    if (!storyline_activate_quest(&state->storyline, "00001-locate-hunters-shack"))
        return;

    state->hunters_shack_quest_started = true;
    game_state_discover_settlement_waypoint(state, 0, 0);

    int ax = 0, ay = 0, az = 0;
    settlement_anchor(drop, &ax, &ay, &az);
    game_state_aim_nav_at(state, 0, 0, 0, (float)ax + 0.5f, (float)ay + 0.5f, (float)az + 0.5f,
                          "Hunter's Shack");
    game_state_show_toast(state, "Locate the hunter's shack", 4000);
}

static void game_state_update_find_note_quest(GameState *state)
{
    if (!game_state_quest_id_is(state, "00002-find-hunters-note"))
        return;

    World *drop = game_state_ensure_home_drop_world(state);
    int nx = 0, ny = 0, nz = 0;
    if (!drop || !settlement_note(drop, &nx, &ny, &nz))
        return;

    if (!nav_aide_active(&state->nav_aide))
        game_state_aim_nav_at(state, 0, 0, 0, (float)nx + 0.5f, (float)ny + 0.5f, (float)nz + 0.5f,
                              "Hunter's Note");

    if (state->player_universe_x != 0 || state->player_universe_y != 0 ||
        state->player_universe_z != 0)
        return;
    if (!state->current_world)
        return;

    const float dx = state->player_world_x - ((float)nx + 0.5f);
    const float dy = state->player_world_y - ((float)ny + 0.5f);
    const float dz = state->player_world_z - ((float)nz + 0.5f);
    if (dx * dx + dy * dy + dz * dz > 9.0f)
        return;

    if (state->player)
        inventory_add(&state->player->inventory, ITEM_HUNTERS_NOTE, 1);

    storyline_update_quest_progress(&state->storyline, "locate", "hunters_note", 1);
    nav_aide_clear(&state->nav_aide);
    game_state_show_toast(state, "Note: bring wool from a marked sheep", 4500);
    game_state_begin_sheep_wool_quest(state);
}

static void game_state_update_sheep_wool_quest(GameState *state)
{
    if (!game_state_quest_id_is(state, "00003-harvest-sheep-wool"))
        return;

    World *world = state->current_world;
    if (!world)
        return;

    // Keep (or re-aim) the aide on the marked sheep while it lives.
    Actor *sheep = game_state_find_runtime_actor(world, state->quest_sheep_actor_id);
    if (sheep && sheep->is_active && sheep->health > 0)
    {
        game_state_aim_nav_at(state, state->player_universe_x, state->player_universe_y,
                              state->player_universe_z, (float)sheep->x, (float)sheep->y,
                              (float)sheep->z, "Marked Sheep");
    }
    else if (sheep && sheep->health == 0)
    {
        // Kill objective + scatter wool for the harvest step.
        storyline_update_quest_progress(&state->storyline, "kill", "sheep", 1);
        if (!state->quest_sheep_loot_dropped)
        {
            debris_spawn_from_voxel(&state->debris, world, (float)sheep->x, (float)sheep->y,
                                    (float)sheep->z + 0.4f, VOXEL_WOOL, 3u,
                                    sheep->id * 2654435761u);
            state->quest_sheep_loot_dropped = true;
            game_state_show_toast(state, "Gather the scattered wool", 3500);
            nav_aide_clear(&state->nav_aide);
        }
        // Prevent re-triggering kill progress every frame.
        state->quest_sheep_actor_id = 0;
    }
    else if (state->quest_sheep_actor_id == 0 && !state->quest_sheep_loot_dropped)
    {
        // World reloaded or sheep never spawned — try again.
        game_state_spawn_quest_sheep(state);
    }

    // Harvest completes when wool is in the bag (debris pickup or otherwise).
    if (state->player && inventory_count_item(&state->player->inventory, ITEM_WOOL) > 0)
    {
        storyline_update_quest_progress(&state->storyline, "harvest", "wool", 1);
        if (!storyline_has_active_quest(&state->storyline))
        {
            nav_aide_clear(&state->nav_aide);
            game_state_show_toast(state, "Wool gathered — seek the nearest town", 4000);
            game_state_begin_nearest_town_quest(state);
        }
    }
}

#define QUEST_TOWN_SEARCH_RADIUS 96
#define QUEST_TOWN_SCALE 5

static void game_state_aim_nav_at_town(GameState *state)
{
    if (!state || !state->quest_town_has_target)
        return;

    const uint64_t ux = (uint64_t)(int64_t)state->quest_town_gx;
    const uint64_t uy = (uint64_t)(int64_t)state->quest_town_gy;
    World *town = universe_get(&state->universe, ux, uy, 0);
    int ax = 0, ay = 0, az = 0;
    if (town && settlement_anchor(town, &ax, &ay, &az))
    {
        game_state_aim_nav_at(state, ux, uy, 0, (float)ax + 0.5f, (float)ay + 0.5f,
                              (float)az + 0.5f, "Nearest Town");
        return;
    }

    // Cell not loaded yet — aim at the geographic centre of that wilderness cube.
    const float mid = (float)WORLD_SIZE_X * 0.5f + 0.5f;
    game_state_aim_nav_at(state, ux, uy, 0, mid, mid, 4.0f, "Nearest Town");
}

static bool game_state_begin_nearest_town_quest(GameState *state)
{
    if (!state)
        return false;

    const int from_gx = (int)(int64_t)state->player_universe_x;
    const int from_gy = (int)(int64_t)state->player_universe_y;
    int tgx = 0, tgy = 0;
    if (!universe_nearest_settlement_of_scale(from_gx, from_gy, QUEST_TOWN_SCALE,
                                             QUEST_TOWN_SEARCH_RADIUS, &tgx, &tgy))
    {
        game_state_show_toast(state, "No size-5 town found nearby", 4000);
        return false;
    }

    if (!storyline_activate_quest(&state->storyline, "00004-reach-nearest-town"))
        return false;

    state->quest_town_has_target = true;
    state->quest_town_gx = tgx;
    state->quest_town_gy = tgy;
    game_state_discover_settlement_waypoint(state, tgx, tgy);
    game_state_aim_nav_at_town(state);
    game_state_show_toast(state, "Follow the arrow to the nearest town", 4500);
    return true;
}

static void game_state_update_nearest_town_quest(GameState *state)
{
    if (!game_state_quest_id_is(state, "00004-reach-nearest-town"))
        return;
    if (!state->quest_town_has_target)
    {
        // Recover target if save/state lost the lock but the quest is still active.
        const int from_gx = (int)(int64_t)state->player_universe_x;
        const int from_gy = (int)(int64_t)state->player_universe_y;
        int tgx = 0, tgy = 0;
        if (!universe_nearest_settlement_of_scale(from_gx, from_gy, QUEST_TOWN_SCALE,
                                                 QUEST_TOWN_SEARCH_RADIUS, &tgx, &tgy))
            return;
        state->quest_town_has_target = true;
        state->quest_town_gx = tgx;
        state->quest_town_gy = tgy;
    }

    game_state_aim_nav_at_town(state);

    if (state->player_universe_z != 0)
        return;
    if ((int)(int64_t)state->player_universe_x != state->quest_town_gx ||
        (int)(int64_t)state->player_universe_y != state->quest_town_gy)
        return;
    if (!state->current_world)
        return;

    // Arrive when standing near the town anchor (or cell centre if stamp has no anchor yet).
    float tx = (float)WORLD_SIZE_X * 0.5f + 0.5f;
    float ty = (float)WORLD_SIZE_Y * 0.5f + 0.5f;
    int ax = 0, ay = 0, az = 0;
    if (settlement_anchor(state->current_world, &ax, &ay, &az))
    {
        tx = (float)ax + 0.5f;
        ty = (float)ay + 0.5f;
    }
    const float dx = state->player_world_x - tx;
    const float dy = state->player_world_y - ty;
    // Towns are large — generous plaza radius.
    if (dx * dx + dy * dy > 100.0f)
        return;

    storyline_update_quest_progress(&state->storyline, "locate", "nearest_town", 1);
    nav_aide_clear(&state->nav_aide);
    game_state_show_toast(state, "You have reached the town — find the shopkeeper", 4500);
    game_state_begin_find_shopkeeper_quest(state);
}

static Actor *game_state_find_town_shopkeeper(GameState *state)
{
    if (!state || !state->current_world)
        return NULL;
    World *world = state->current_world;
    // Ensure settlement NPCs exist before we look for the shopkeeper.
    if (world->settlement_scale >= 3)
        world_spawn_settlement_villagers(world);
    return shop_find_settlement_merchant(world);
}

static void game_state_aim_nav_at_shopkeeper(GameState *state, Actor *merchant)
{
    if (!state || !merchant || !state->current_world)
        return;
    game_state_aim_nav_at(state, state->current_world->universe_x, state->current_world->universe_y,
                          state->current_world->universe_z, (float)merchant->x, (float)merchant->y,
                          (float)merchant->z, merchant->name[0] ? merchant->name : "Shopkeeper");
}

static bool game_state_begin_find_shopkeeper_quest(GameState *state)
{
    if (!state)
        return false;
    if (!storyline_activate_quest(&state->storyline, "00005-find-shopkeeper"))
        return false;

    Actor *merchant = game_state_find_town_shopkeeper(state);
    if (merchant)
        game_state_aim_nav_at_shopkeeper(state, merchant);
    else if (state->quest_town_has_target)
        game_state_aim_nav_at_town(state);

    game_state_show_toast(state, "Talk to the town shopkeeper", 4500);
    return true;
}

static void game_state_update_find_shopkeeper_quest(GameState *state)
{
    if (!game_state_quest_id_is(state, "00005-find-shopkeeper"))
        return;
    if (!state->current_world)
        return;

    // Prefer the locked storyline town cell; otherwise any settled cell with a shopkeeper.
    if (state->quest_town_has_target)
    {
        if (state->player_universe_z != 0)
            return;
        if ((int)(int64_t)state->player_universe_x != state->quest_town_gx ||
            (int)(int64_t)state->player_universe_y != state->quest_town_gy)
        {
            game_state_aim_nav_at_town(state);
            return;
        }
    }

    Actor *merchant = game_state_find_town_shopkeeper(state);
    if (merchant)
        game_state_aim_nav_at_shopkeeper(state, merchant);

    // Complete once the player is speaking with the shopkeeper or has opened their shop.
    bool found = false;
    if (merchant && dialogue_active(&state->dialogue) &&
        dialogue_speaker_id(&state->dialogue) == merchant->id)
        found = true;
    if (state->shop.active && merchant && state->shop.merchant_id == merchant->id)
        found = true;
    if (!found)
        return;

    storyline_update_quest_progress(&state->storyline, "locate", "shopkeeper", 1);
    if (!storyline_has_active_quest(&state->storyline))
        game_state_begin_sell_wool_quest(state);
}

static bool game_state_begin_sell_wool_quest(GameState *state)
{
    if (!state)
        return false;
    if (!storyline_activate_quest(&state->storyline, "00006-sell-wool"))
        return false;

    Actor *merchant = game_state_find_town_shopkeeper(state);
    if (merchant)
        game_state_aim_nav_at_shopkeeper(state, merchant);
    else if (state->quest_town_has_target)
        game_state_aim_nav_at_town(state);

    // Ensure the player still has wool to sell (quest chain assumes the harvest step).
    if (state->player && inventory_count_item(&state->player->inventory, ITEM_WOOL) == 0)
        inventory_add(&state->player->inventory, ITEM_WOOL, 1);

    game_state_show_toast(state, "Open the shop and sell your wool", 4500);
    return true;
}

static void game_state_update_sell_wool_quest(GameState *state)
{
    if (!game_state_quest_id_is(state, "00006-sell-wool"))
        return;
    if (!state->current_world)
        return;

    if (state->quest_town_has_target)
    {
        if (state->player_universe_z != 0)
            return;
        if ((int)(int64_t)state->player_universe_x != state->quest_town_gx ||
            (int)(int64_t)state->player_universe_y != state->quest_town_gy)
        {
            game_state_aim_nav_at_town(state);
            return;
        }
    }

    Actor *merchant = game_state_find_town_shopkeeper(state);
    if (merchant)
        game_state_aim_nav_at_shopkeeper(state, merchant);

    // Completion is driven by shop_sell → storyline_update_quest_progress("sell","wool").
    // Keep the aide pointed until the sell lands.
}

static void game_state_update_hunters_shack_quest(GameState *state)
{
    if (!state || !state->hunters_shack_quest_started)
        return;

    // Chain follow-ups even if the previous quest just completed.
    if (game_state_quest_id_is(state, "00002-find-hunters-note"))
    {
        game_state_update_find_note_quest(state);
        return;
    }
    if (game_state_quest_id_is(state, "00003-harvest-sheep-wool"))
    {
        game_state_update_sheep_wool_quest(state);
        return;
    }
    if (game_state_quest_id_is(state, "00004-reach-nearest-town"))
    {
        game_state_update_nearest_town_quest(state);
        return;
    }
    if (game_state_quest_id_is(state, "00005-find-shopkeeper"))
    {
        game_state_update_find_shopkeeper_quest(state);
        return;
    }
    if (game_state_quest_id_is(state, "00006-sell-wool"))
    {
        game_state_update_sell_wool_quest(state);
        return;
    }

    if (!storyline_has_active_quest(&state->storyline))
    {
        if (nav_aide_active(&state->nav_aide))
            nav_aide_clear(&state->nav_aide);
        return;
    }

    if (!game_state_quest_id_is(state, "00001-locate-hunters-shack"))
        return;

    // Keep the aide aimed if the drop world finished generating after quest start.
    if (!nav_aide_active(&state->nav_aide))
    {
        World *drop = game_state_ensure_home_drop_world(state);
        int ax = 0, ay = 0, az = 0;
        if (drop && settlement_anchor(drop, &ax, &ay, &az))
            game_state_aim_nav_at(state, 0, 0, 0, (float)ax + 0.5f, (float)ay + 0.5f,
                                  (float)az + 0.5f, "Hunter's Shack");
    }

    if (state->player_universe_x != 0 || state->player_universe_y != 0 ||
        state->player_universe_z != 0)
        return;
    if (!state->current_world || !settlement_has_anchor(state->current_world))
        return;

    int ax = 0, ay = 0, az = 0;
    settlement_anchor(state->current_world, &ax, &ay, &az);
    const float dx = state->player_world_x - ((float)ax + 0.5f);
    const float dy = state->player_world_y - ((float)ay + 0.5f);
    if (dx * dx + dy * dy > 16.0f)
        return;

    storyline_update_quest_progress(&state->storyline, "locate", "hunters_shack", 1);
    nav_aide_clear(&state->nav_aide);
    game_state_show_toast(state, "Found the hunter's shack", 3500);
    game_state_begin_find_note_quest(state);
}


static bool game_state_transition_vertical(GameState *state, int dz)
{
    if (!state || !state->current_world || dz == 0)
        return false;

    int64_t new_uz = (int64_t)state->player_universe_z + dz;
    if (new_uz < 0)
        return false;

    uint64_t ux = state->player_universe_x;
    uint64_t uy = state->player_universe_y;
    uint64_t uz = (uint64_t)new_uz;

    World *from = state->current_world;
    World *target = universe_get(&state->universe, ux, uy, uz);
    if (!target)
        target = game_state_ensure_universe_cell(state, ux, uy, uz);
    if (!target)
        return false;

    // Keep horizontal motion and the fall/jump speed across the seam. Snapping vz to zero here is
    // what used to make a multi-layer fall feel like three separate drops.
    const float keep_vx = state->controls.velocity_x;
    const float keep_vy = state->controls.velocity_y;
    float keep_vz = state->controls.velocity_z;
    const float old_x = state->player_world_x;
    const float old_y = state->player_world_y;
    const float old_z = state->player_world_z;

    float sink_max = PLAYER_TERMINAL_VELOCITY;
    if (state->controls.fly_active)
    {
        Actor *body = player_controls_dominated_actor(state);
        if (mob_actor_is_bird(body))
            sink_max = mob_bird_stats(mob_actor_bird_kind(body))->sink_max;
        else
            sink_max = 1.4f;
    }
    if (keep_vz < -sink_max)
        keep_vz = -sink_max;

    state->current_world = target;
    state->player_universe_z = uz;

    // Continuous height: the floor of `from` maps onto the ceiling of `target` (and vice versa),
    // so a body mid-voxel when it crosses keeps the same fractional offset.
    float new_z;
    if (dz < 0)
    {
        float overshoot = 0.5f - old_z;
        if (overshoot < 0.0f)
            overshoot = 0.0f;
        new_z = (float)target->depth - 0.5f - overshoot;
        if (new_z < 0.5f)
            new_z = 0.5f;
    }
    else
    {
        float overshoot = old_z - ((float)from->depth - 0.5f);
        if (overshoot < 0.0f)
            overshoot = 0.0f;
        new_z = 0.5f + overshoot;
        const float ceil_z = (float)target->depth - 0.5f;
        if (new_z > ceil_z)
            new_z = ceil_z;
    }

    state->player_world_x = old_x;
    state->player_world_y = old_y;
    state->player_world_z = new_z;
    game_state_sync_positions(state);
    state->controls.velocity_x = keep_vx;
    state->controls.velocity_y = keep_vy;
    state->controls.velocity_z = keep_vz;

    game_state_carry_dominated_actor(state, from, target);

    if (state->player)
        actor_set_position(state->player, state->player_world_x, state->player_world_y,
                           state->player_world_z);

    // Wilderness / settlement cells generated before fauna existed (or loaded without actors)
    // pick up sparse life on entry; idempotent when the generator already spawned them.
    if (target->generation_type == WORLD_TYPE_WILDERNESS ||
        target->generation_type == WORLD_TYPE_WFC_TOWN)
    {
        world_spawn_wilderness_mobs(target);
        if (target->generation_type == WORLD_TYPE_WFC_TOWN)
            world_spawn_settlement_villagers(target);
        universe_biome_apply_ambient_weather(target);
    }

    // Re-frame the cluster around the new layer. The worlds that are still in range are kept rather
    // than re-looked-up, and the reported delta is what a cluster-space coordinate would need adding
    // to follow the move — nothing stores those yet, since entity positions are world-local, but the
    // rebase has to be available before anything can.
    if (state->shadow_world)
    {
        int rebase_x = 0, rebase_y = 0, rebase_z = 0;
        const int retained = shadow_world_recenter(state->shadow_world, ux, uy, uz,
                                                   &rebase_x, &rebase_y, &rebase_z);
        (void)retained;
    }
    game_state_sync_shadow_world_budgeted(state, 3);

    // The layer the player just left may now be out of range, and the one entered has new
    // neighbours to stream. Both are cheap; neither generates on this thread.
    game_state_evict_distant_worlds(state);
    game_state_pump_world_streaming(state);

    snprintf(state->status_message, sizeof(state->status_message),
             "Entered layer z=%llu", (unsigned long long)uz);
    return true;
}

void game_state_apply_gravity(GameState *state, float delta_time)
{
    if (!state || !state->game_started)
        return;

    World *world = state->current_world ? state->current_world : state->main_menu_world;
    if (!world)
        return;

    if ((state->player && state->player->is_flying) || state->player_flying)
    {
        // Spirit hover: integrate soft vertical thrust from Space/Ctrl; no gravity pull.
        if (delta_time <= 0.0f)
            return;
        if (delta_time > 0.1f)
            delta_time = 0.1f;

        const float vz = state->controls.velocity_z;
        if (fabsf(vz) < 1e-5f)
            return;

        const float start_z = state->player_world_z;
        float target_z = start_z + vz * delta_time;

        while (state->player_world_z < target_z)
        {
            float next_z = state->player_world_z + 1.0f;
            if (next_z > target_z)
                next_z = target_z;
            if (!game_state_has_head_clearance(state, next_z))
            {
                state->controls.velocity_z = 0.0f;
                game_state_sync_positions(state);
                return;
            }
            state->player_world_z = next_z;
            const float ceiling_z = (float)world->depth - 0.5f;
            if (state->player_world_z >= ceiling_z)
            {
                state->player_world_z = ceiling_z;
                state->controls.velocity_z = 0.0f;
                game_state_sync_positions(state);
                return;
            }
        }

        while (state->player_world_z > target_z)
        {
            float next_z = state->player_world_z - 1.0f;
            if (next_z < target_z)
                next_z = target_z;
            // Hover may sink into ground support, but stop on solid rather than tunnelling.
            if (game_state_has_voxel_support(state, next_z))
            {
                float rest_z = floorf(next_z) + 0.5f;
                state->player_world_z = rest_z;
                state->controls.velocity_z = 0.0f;
                game_state_sync_positions(state);
                return;
            }
            state->player_world_z = next_z;
            if (state->player_world_z <= 0.5f)
            {
                state->player_world_z = 0.5f;
                state->controls.velocity_z = 0.0f;
                game_state_sync_positions(state);
                return;
            }
        }

        game_state_sync_positions(state);
        return;
    }

    if (delta_time <= 0.0f)
        return;
    if (delta_time > 0.1f)
        delta_time = 0.1f;

    const float resting_z = floorf(state->player_world_z) + 0.5f;
    bool supported = game_state_has_ground_support(state, state->player_world_z);
    if (supported && state->controls.velocity_z <= 0.0f &&
        state->player_world_z <= resting_z + 1e-4f)
    {
        if (state->controls.fly_active && state->controls.fly_was_airborne)
        {
            state->controls.fly_active = false;
            state->controls.fly_was_airborne = false;
            snprintf(state->status_message, sizeof(state->status_message), "You land");
        }
        state->controls.velocity_z = 0.0f;
        return;
    }

    if (state->controls.fly_active && !supported)
        state->controls.fly_was_airborne = true;

    float gravity = game_state_player_gravity(state);
    float sink_max = PLAYER_TERMINAL_VELOCITY;
    if (state->controls.fly_active)
    {
        float lift = player_controls_inhabited_lift(state);
        if (lift < 0.0f)
            lift = 0.0f;
        if (lift > 0.97f)
            lift = 0.97f;
        gravity *= (1.0f - lift);
        Actor *body = player_controls_dominated_actor(state);
        if (mob_actor_is_bird(body))
            sink_max = mob_bird_stats(mob_actor_bird_kind(body))->sink_max;
        else
            sink_max = 1.4f;
    }

    const float start_velocity = state->controls.velocity_z;
    state->controls.velocity_z -= gravity * delta_time;
    if (state->controls.velocity_z < -sink_max)
        state->controls.velocity_z = -sink_max;

    const float start_z = state->player_world_z;
    // Average the velocity across the step rather than using the end of it. For constant
    // acceleration that is exact, which is what makes the height a jump reaches independent of the
    // frame rate: stepping with the end velocity alone lost about a seventh of the jump at 60fps and
    // enough at 20 that it no longer cleared a one-voxel step.
    float target_z = start_z + 0.5f * (start_velocity + state->controls.velocity_z) * delta_time;

    // Rising. Step up a voxel at a time for the same reason the fall does: a fast jump must not
    // pass through the ceiling it should have hit.
    while (state->player_world_z < target_z)
    {
        float next_z = state->player_world_z + 1.0f;
        if (next_z > target_z)
            next_z = target_z;

        if (!game_state_has_head_clearance(state, next_z))
        {
            // Stopped by whatever is overhead. Velocity goes to zero rather than reversing, so the
            // fall that follows starts from rest instead of bouncing.
            state->controls.velocity_z = 0.0f;
            game_state_sync_positions(state);
            snprintf(state->status_message, sizeof(state->status_message), "Bumped your head!");
            return;
        }

        state->player_world_z = next_z;

        // The top of the world is a lid, not a doorway: falling out of the bottom drops the player
        // into the layer below, but there is nothing above to rise into.
        const float ceiling_z = (float)world->depth - 0.5f;
        if (state->player_world_z >= ceiling_z)
        {
            state->player_world_z = ceiling_z;
            state->controls.velocity_z = 0.0f;
            game_state_sync_positions(state);
            return;
        }
    }

    // Step down a voxel at a time so a fast fall cannot skip the floor it should land on.
    while (state->player_world_z > target_z)
    {
        float next_z = state->player_world_z - 1.0f;
        if (next_z < target_z)
            next_z = target_z;

        // Only a voxel in this world ends the fall here. The world's own floor is handled below,
        // where falling out of the bottom becomes a move into the layer beneath.
        if (game_state_has_voxel_support(state, next_z))
        {
            // Come to rest at the centre of the voxel sitting on top of the ground, which is
            // where game_state_set_player_position would have placed a standing player. Never
            // above where this frame started, so gravity can only ever move the player down.
            float rest_z = floorf(next_z) + 0.5f;
            state->player_world_z = rest_z < start_z ? rest_z : start_z;
            state->controls.velocity_z = 0.0f;
            game_state_sync_positions(state);
            snprintf(state->status_message, sizeof(state->status_message), "Landed!");
            if (state->player_universe_z == 0)
            {
                game_state_try_begin_hunters_shack_quest(state);
                game_state_try_discover_current_settlement(state);
            }
            return;
        }

        state->player_world_z = next_z;

        if (state->player_world_z <= 0.5f)
        {
            // Anything the player could have fallen through on the way down lets them keep going.
            // Testing for air alone stranded them on the cloud layer: clouds are steam, which the
            // player falls through everywhere else in the world, so a wisp of it in the lowest voxel
            // of their column was an invisible floor two layers above the ground.
            Voxel *bottom = world_get_voxel(world, state->player_voxel_x, state->player_voxel_y, 0);
            if (bottom && !world_voxel_type_blocks_movement(bottom->type) &&
                game_state_transition_vertical(state, -1))
            {
                snprintf(state->status_message, sizeof(state->status_message),
                         "Falling into the world below...");
                return;
            }

            state->player_world_z = 0.5f;
            state->controls.velocity_z = 0.0f;
            game_state_sync_positions(state);
            return;
        }
    }

    game_state_sync_positions(state);
    if (state->controls.velocity_z > 0.0f)
        snprintf(state->status_message, sizeof(state->status_message), "Airborne!");
    else
        snprintf(state->status_message, sizeof(state->status_message),
                 "Falling! Gravity: %.2f voxels/s²", gravity);
}

bool game_state_can_move_to(GameState *state, int x, int y, int z)
{
    if (!state) {
        MOVE_DBG("COLLISION DEBUG: No state\n");
        return false;
    }

    // A column past the edge of this world belongs to the neighbour beyond it, and is resolved there
    // rather than reported out of bounds. Height is still this world's business: worlds are stacked,
    // and leaving one through its floor or ceiling is a transition, not a step.
    int x_local = x, y_local = y;
    World *world = game_state_resolve_column(state, x, y, &x_local, &y_local);
    if (!world) {
        MOVE_DBG("COLLISION DEBUG: No world beyond (%d,%d,%d)\n", x, y, z);
        return false;
    }

    if (z < 0 || z >= (int)world->depth)
    {
        MOVE_DBG("COLLISION DEBUG: Position (%d,%d,%d) out of bounds\n", x, y, z);
        return false;
    }

    // Check if position is walkable (Z-vertical system: position should be AIR)
    Voxel *voxel = world_get_voxel(world, x_local, y_local, z);
    if (!voxel) {
        MOVE_DBG("COLLISION DEBUG: No voxel at (%d,%d,%d)\n", x, y, z);
        return false;
    }

    if (world_voxel_type_blocks_movement(voxel->type)) {
        MOVE_DBG("COLLISION: Blocked by voxel type %d at (%d,%d,%d)\n", voxel->type, x, y, z);
        return false;
    }

    // If flying, skip headroom requirement to allow reaching topmost layer.
    bool is_flying = (state->player && state->player->is_flying) || state->player_flying ||
                     state->controls.fly_active;
    if (!is_flying) {
        // Check if position above is air (Z+1 should be air for headroom)
        Voxel *voxel_above = world_get_voxel(world, x_local, y_local, z + 1);
        if (!voxel_above || world_voxel_type_blocks_movement(voxel_above->type))
        {
            MOVE_DBG("COLLISION: No headroom at (%d,%d,%d)\n", x, y, z+1);
            return false;
        }
    }

    // Nothing underneath is not a reason to refuse. Requiring ground below meant the player stopped
    // dead at the edge of every drop, because the voxel they were about to step into had air under
    // it; gravity then never got a chance to run because they had not moved. Walking off is allowed
    // and game_state_apply_gravity takes over from there, which is also what gives air control to a
    // player who is already falling or mid-jump.
    return true;
}

void game_state_move_player(GameState *state, int dx, int dy, int dz)
{
    if (!state || !state->game_started)
        return;

    int old_x = state->player_x;
    int old_y = state->player_y;
    int old_z = state->player_z;

    int new_x = state->player_x + dx;
    int new_y = state->player_y + dy;
    int new_z = state->player_z + dz;

    MOVE_DBG("MOVEMENT DEBUG: Player at (%d,%d,%d) trying to move by (%d,%d,%d) to (%d,%d,%d)\n",
           old_x, old_y, old_z, dx, dy, dz, new_x, new_y, new_z);

    if (game_state_can_move_to(state, new_x, new_y, new_z))
    {
        state->player_x = new_x;
        state->player_y = new_y;
        state->player_z = new_z;

        // Update floating-point coordinates to match
        state->player_world_x = (float)new_x + 0.5f;
        state->player_world_y = (float)new_y + 0.5f;
        state->player_world_z = (float)new_z + 0.5f;

        // Update camera position to follow player
        extern IsometricRenderer *g_isometric_renderer;
        if (g_isometric_renderer)
        {
            isometric_renderer_set_camera_world(g_isometric_renderer, (float)new_x + 0.5f,
                                              (float)new_y + 0.5f, (float)new_z + 0.5f);
        }

        MOVE_DBG("MOVEMENT SUCCESS: Player moved from (%d,%d,%d) to (%d,%d,%d)\n",
               old_x, old_y, old_z, new_x, new_y, new_z);
        snprintf(state->status_message, sizeof(state->status_message),
                 "Moved to (%d, %d, %d)", new_x, new_y, new_z);
    }
    else
    {
        MOVE_DBG("MOVEMENT BLOCKED: Cannot move from (%d,%d,%d) to (%d,%d,%d)\n",
               old_x, old_y, old_z, new_x, new_y, new_z);
        snprintf(state->status_message, sizeof(state->status_message),
                 "Cannot move to (%d, %d, %d)", new_x, new_y, new_z);
    }
}

void game_state_handle_input(GameState *state, int key)
{
    if (!state)
        return;

    if (!state->game_started || state->current_screen != GAME_SCREEN_WORLD)
        return;

    switch (key)
    {
    case SDLK_w:
        state->controls.move_forward = true;
        state->controls.has_move_target = false;
        break;
    case SDLK_s:
        state->controls.move_backward = true;
        state->controls.has_move_target = false;
        break;
    case SDLK_a:
        state->controls.move_left = true;
        state->controls.has_move_target = false;
        break;
    case SDLK_d:
        state->controls.move_right = true;
        state->controls.has_move_target = false;
        break;
    case SDLK_LEFTBRACKET:
        // Decelerate simulation time.
        if (state->time_scale > 0.125f)
        {
            state->time_scale *= 0.5f;
            if (state->time_scale < 0.125f)
                state->time_scale = 0.125f;
            snprintf(state->status_message, sizeof(state->status_message),
                     "Time scale ×%.3g", (double)state->time_scale);
            snprintf(state->toast_text, sizeof(state->toast_text), "Time ×%.3g",
                     (double)state->time_scale);
            state->toast_start_ms = SDL_GetTicks();
            state->toast_duration_ms = 1500;
            state->toast_y_offset = 0.0f;
            state->toast_active = true;
        }
        break;
    case SDLK_RIGHTBRACKET:
        // Accelerate simulation time.
        if (state->time_scale < 64.0f)
        {
            state->time_scale *= 2.0f;
            if (state->time_scale > 64.0f)
                state->time_scale = 64.0f;
            snprintf(state->status_message, sizeof(state->status_message),
                     "Time scale ×%.3g", (double)state->time_scale);
            snprintf(state->toast_text, sizeof(state->toast_text), "Time ×%.3g",
                     (double)state->time_scale);
            state->toast_start_ms = SDL_GetTicks();
            state->toast_duration_ms = 1500;
            state->toast_y_offset = 0.0f;
            state->toast_active = true;
        }
        break;
    }
}

void game_state_poll_movement_keys(GameState *state)
{
    if (!state)
        return;

    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (!keys)
        return;

    if (!state->game_started || state->current_screen != GAME_SCREEN_WORLD ||
        state->show_world_editor_modal)
    {
        if (state->show_world_editor_modal)
        {
            state->controls.move_forward = false;
            state->controls.move_backward = false;
            state->controls.move_left = false;
            state->controls.move_right = false;
            state->controls.move_up = false;
            state->controls.move_down = false;
            state->controls.boost = false;
            state->controls.bank_left = false;
            state->controls.bank_right = false;
        }
        if (state->controls.teleport_charging)
            player_controls_cancel_town_portal(state, &state->controls);
        return;
    }

    state->controls.move_forward = keys[SDL_SCANCODE_W];
    state->controls.move_backward = keys[SDL_SCANCODE_S];
    state->controls.move_left = keys[SDL_SCANCODE_A];
    state->controls.move_right = keys[SDL_SCANCODE_D];
    state->controls.move_up = keys[SDL_SCANCODE_SPACE] && !dialogue_active(&state->dialogue);
    state->controls.move_down = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    state->controls.boost = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    state->controls.bank_left = keys[SDL_SCANCODE_Q];
    state->controls.bank_right = keys[SDL_SCANCODE_E];

    gamepad_apply_movement(state);

    if (state->controls.move_forward || state->controls.move_backward ||
        state->controls.move_left || state->controls.move_right ||
        state->controls.move_up || state->controls.move_down)
    {
        player_controls_clear_command_queue(&state->controls);
        state->controls.has_move_target = false;
    }

    const bool portal_held = keys[SDL_SCANCODE_T] != 0 || gamepad_town_portal_held();
    player_controls_update_town_portal(state, &state->controls, portal_held);
    player_controls_update_sun_strike(state, &state->controls);
}

void game_state_handle_mouse_motion(GameState *state, int mouse_x, int mouse_y)
{
    if (!state || !state->game_started || state->current_screen != GAME_SCREEN_WORLD)
        return;

    player_controls_update_facing_from_screen(state, &state->controls, mouse_x, mouse_y);
}

void game_state_handle_mouse(GameState *state, int x, int y, int button)
{
    if (!state || !state->game_started || state->current_screen != GAME_SCREEN_WORLD)
        return;

    float world_x, world_y, world_z;
    if (!window_screen_to_world_coords_float(x, y, &world_x, &world_y, &world_z))
        return;

    if (button == SDL_BUTTON_RIGHT)
    {
        // Right-click queues an attack-move command (move to point, engaging enemies on the way).
        if (player_controls_enqueue_attack_move(state, &state->controls,
                                               world_x, world_y, world_z))
        {
            snprintf(state->status_message, sizeof(state->status_message),
                     "Attack-move → (%.1f, %.1f)  [queue %d]",
                     world_x, world_y, state->controls.command_count +
                     (state->controls.has_move_target ? 1 : 0));
        }
        else
        {
            snprintf(state->status_message, sizeof(state->status_message),
                     "Command queue full");
        }
    }
}

void game_state_update(GameState *state, double delta_time)
{
    if (!state)
        return;

    // While the generation worker owns the worlds and the universe, this thread must not write
    // to either. Nothing below is meaningful before there is a world to play in anyway.
    if (state->world_generation_active)
        return;

    // Apply player time-scale ([ slower, ] faster).
    if (state->time_scale > 0.0f && state->time_scale != 1.0f)
        delta_time *= (double)state->time_scale;

    game_state_update_hunters_shack_quest(state);

    // Update runtime clock if running; drive world evolution epochs
    if (state->runtime_clock_running) {
        state->runtime_clock_ms += (uint64_t)(delta_time * 1000.0);
        // Every 10 minutes (600000 ms), advance epoch and attempt flowering evolution
        uint64_t new_epoch = state->runtime_clock_ms / 600000ULL;
        if (new_epoch > state->runtime_epoch_index) {
            state->runtime_epoch_index = (uint32_t)new_epoch;
            // 5% chance to turn a random FLOWERING voxel into VOXEL_BUSH
            World *world = state->current_world ? state->current_world : state->main_menu_world;
            if (world) {
                char* patch = world_advance_epoch_and_patch(world, state->runtime_epoch_index);
                if (patch && patch[0] != '\0') {
                    universe_append_event(&state->universe, "EPOCH_UPDATE", patch);
                    free(patch);
                } else if (patch) {
                    free(patch);
                }
            }
            // Toast message for epoch tick
            strncpy(state->toast_text, "The world ticks on...", sizeof(state->toast_text)-1);
            state->toast_text[sizeof(state->toast_text)-1] = '\0';
            state->toast_start_ms = SDL_GetTicks();
            state->toast_duration_ms = 4000;
            state->toast_y_offset = 0.0f;
            state->toast_active = true;
        }
    }

    // Action RPG movement: momentum, click-to-move, swing attacks
    game_state_poll_movement_keys(state);

    // First person aims with the mouse itself; isometric aims at the crosshair's ground position.
    // Capture is only appropriate during actual play, so menus and modals release the pointer.
    {
        const bool mouse_look = window_is_fp_mode() && state->game_started &&
                                state->current_screen == GAME_SCREEN_WORLD &&
                                !state->show_name_input && !state->show_exit_prompt &&
                                !state->show_new_game_warning &&
                                !state->show_world_editor_modal;
        window_set_mouse_look_active(mouse_look);

        // In flight, yaw belongs to Q/E bank (and FP mouse look). Isometric cursor aim would
        // fight every bank frame and undo the turn as soon as Q/E were released.
        const bool in_flight = player_controls_in_flight(state, &state->controls);
        if (mouse_look)
        {
            int mouse_dx = 0, mouse_dy = 0;
            window_consume_mouse_look_delta(&mouse_dx, &mouse_dy);
            player_controls_apply_mouse_look(&state->controls, mouse_dx, mouse_dy);
        }
        else if (!state->controls.is_attacking && !in_flight &&
                 !state->controls.bank_left && !state->controls.bank_right)
        {
            int cursor_x = 0, cursor_y = 0;
            window_get_cursor_pos(&cursor_x, &cursor_y);
            player_controls_update_facing_from_screen(state, &state->controls, cursor_x, cursor_y);
        }
        // Right stick aims in both FP and isometric (after cursor aim so stick wins while deflected).
        gamepad_apply_actions(state, delta_time);
    }
    player_controls_apply_bank(state, &state->controls, delta_time);
    player_controls_update_facing(state, &state->controls, delta_time);
    player_controls_update_attack(state, &state->controls);

    // Swings may turn the body toward the click, but they do not pause WASD or stamina.
    player_controls_apply_vertical(state, &state->controls, delta_time);

    bool wasd = state->controls.move_forward || state->controls.move_backward ||
                state->controls.move_left || state->controls.move_right;
    bool gliding = state->controls.fly_active && !game_state_player_is_grounded(state) &&
                   !state->player_flying &&
                   !(state->player && state->player->is_flying);

    if (wasd || gliding)
        player_controls_apply_wasd(state, &state->controls, delta_time);
    else if (!state->controls.is_attacking)
        player_controls_update_command_queue(state, &state->controls, delta_time);

    if (!wasd && !gliding && !state->controls.has_move_target &&
        (state->controls.velocity_x != 0.0f || state->controls.velocity_y != 0.0f))
        player_controls_apply_wasd(state, &state->controls, delta_time);

    game_state_apply_gravity(state, (float)delta_time);
    player_controls_update_auto_glide(state, &state->controls, delta_time);

    if (state->dominated_actor_id != 0)
    {
        Actor *body = player_controls_dominated_actor(state);
        if (!body || !body->is_active || body->health == 0)
        {
            player_controls_release_dominate(state);
        }
        else
        {
            body->x = (double)state->player_world_x;
            body->y = (double)state->player_world_y;
            body->z = (double)state->player_world_z;
            body->is_controlled = true;
            body->is_flying = state->controls.fly_active && !game_state_player_is_grounded(state);
            if (body->extra_data)
            {
                MobActor *mob = (MobActor *)body->extra_data;
                mob->base.x = body->x;
                mob->base.y = body->y;
                mob->base.z = body->z;
                mob->base.velocity_x = (double)state->controls.velocity_x;
                mob->base.velocity_y = (double)state->controls.velocity_y;
                mob->base.velocity_z = (double)state->controls.velocity_z;
                mob->base.is_controlled = true;
                mob->base.is_flying = body->is_flying;
                // Point the mesh where the player is facing so Q/E bank rolls the crow immediately,
                // rather than waiting for horizontal velocity to catch up.
                mob->facing_yaw = state->controls.facing_yaw;
                mob->facing_roll = state->controls.roll;
                if (mob->mob_type == MOB_TYPE_BIRD || mob->mob_type == MOB_TYPE_BAT)
                    mob->ai_state.bird_phase = body->is_flying ? BIRD_PHASE_CIRCLE
                                                               : BIRD_PHASE_PERCH;
                mob_actor_tick_animation(mob, (float)delta_time);
            }
        }
    }

    // Track exploration for the world map with a cheap sphere around the player. The old
    // view-cone ray march (thousands of long rays) was built for first-person hide-masks and
    // burned several ms every moving frame; map FoW only needs presence.
    if (state->game_started && state->fog && state->shadow_world)
    {
        float px = state->player_world_x;
        float py = state->player_world_y;
        float pz = state->player_world_z;
        game_state_get_animated_player_position(state, &px, &py, &pz);
        fog_reveal_around(state->fog, state->shadow_world, px, py, pz + 0.6f, 24);
    }

    if (state->player && state->game_started)
    {
        const float max_stam = actor_max_stamina(state->player);
        const float ratio = (float)state->player->stamina / max_stam;
        if (ratio < 1.0f)
            state->stamina_meter_alpha = 1.0f;
        else if (state->stamina_meter_alpha > 0.0f)
        {
            state->stamina_meter_alpha -= (float)delta_time;
            if (state->stamina_meter_alpha < 0.0f)
                state->stamina_meter_alpha = 0.0f;
        }
    }

    // Legacy smooth click-to-move (disabled when action RPG controls active)
    if (state->movement_destination.has_destination && !state->controls.has_move_target)
        game_state_update_movement(state);

    // Collect finished stream jobs and start the next nearest holes every frame. The header on
    // game_state_pump_world_streaming asks for this; leaving it on the one-second residency tick
    // meant a finished wilderness neighbour could sit uncollected — and unrendered, and an
    // invisible wall — for most of a second after the worker was done.
    if (state->game_started)
        game_state_pump_world_streaming(state);

    // Drain one dirty pyramid slot on light frames too. Boundary crosses leave most of the
    // cluster invalid; waiting only for the 20 Hz physics budget stretches that over too long.
    if (state->game_started && state->shadow_world)
        game_state_sync_shadow_world_budgeted(state, 1);

    game_state_step_world_physics(state, delta_time);

    if (state->game_started)
        game_state_tick_dialogue(state);

    if (state->game_started)
        game_state_step_projectiles(state, delta_time);

    if (state->game_started)
        game_state_step_debris(state, delta_time);

    // Dust / rain / fire ambience shares the physics clock (20 Hz). Running it every render frame
    // used to rescan flame columns whenever no emitters were nearby — tens of ms on wilderness.
    // Accumulate dt so particle lifetimes stay correct across the skipped frames.
    {
        static double s_particle_dt_accum;
        s_particle_dt_accum += delta_time;
        const double particle_period = 0.05; // match WORLD_PHYSICS_TICK_MS (20 Hz)
        const bool particles_due =
            s_particle_dt_accum >= particle_period || state->last_physics_tick_ms == 0;
        if (particles_due)
        {
            World *particle_world =
                state->current_world ? state->current_world : state->main_menu_world;
            if (particle_world)
            {
                static double s_weather_accum;
                s_weather_accum += s_particle_dt_accum;
                if (s_weather_accum >= 2.0)
                {
                    s_weather_accum = 0.0;
                    universe_biome_apply_ambient_weather(particle_world);
                    {
                        static uint32_t s_storm_salt;
                        s_storm_salt++;
                        weather_storm_tick(particle_world, s_storm_salt);
                    }
                }
                particle_effects_update(particle_world, (float)s_particle_dt_accum,
                                        state->player_world_x, state->player_world_y,
                                        state->player_world_z, 64, &state->projectiles);
            }
            s_particle_dt_accum = 0.0;
        }
    }
}

static void game_state_sync_mob_progress(Actor *actor)
{
    if (!actor || !actor->extra_data)
        return;
    MobActor *mob = (MobActor *)actor->extra_data;
    mob->base.experience = actor->experience;
    mob->base.level = actor->level;
    mob->base.attribute_points = actor->attribute_points;
    mob->base.skill_points = actor->skill_points;
    mob->base.health = actor->health;
    mob->base.stamina = actor->stamina;
    mob->base.mana = actor->mana;
    mob->base.turn_speed = actor->turn_speed;
    mob->base.strength = actor->strength;
    mob->base.dexterity = actor->dexterity;
    mob->base.intelligence = actor->intelligence;
    mob->base.wisdom = actor->wisdom;
    mob->base.constitution = actor->constitution;
    mob->base.charisma = actor->charisma;
    mob->base.luck = actor->luck;
}

static uint32_t game_state_skill_points_gained(uint32_t old_level, uint32_t new_level)
{
    uint32_t skill_gained = 0;
    for (uint32_t L = old_level + 1u; L <= new_level; L++)
    {
        if ((L % ACTOR_SKILL_POINT_LEVELS) == 0u)
            skill_gained++;
    }
    return skill_gained;
}

void game_state_award_experience(GameState *state, uint32_t amount, const char *reason)
{
    if (!state || !state->player || amount == 0)
        return;

    // Spirit always banks XP. While inhabiting a mob, that body also gets the full award.
    const uint32_t old_spirit_level = state->player->level;
    actor_add_experience(state->player, amount);

    Actor *body = player_controls_dominated_actor(state);
    uint32_t old_body_level = 0;
    const bool body_earns = body && body != state->player && body->extra_data;
    if (body_earns)
    {
        old_body_level = body->level;
        actor_add_experience(body, amount);
        game_state_sync_mob_progress(body);
    }

    if (reason && reason[0])
        snprintf(state->status_message, sizeof(state->status_message), "%s", reason);
    else
        snprintf(state->status_message, sizeof(state->status_message), "+%u XP", amount);

    const bool spirit_leveled = state->player->level > old_spirit_level;
    const bool body_leveled = body_earns && body->level > old_body_level;
    if (!spirit_leveled && !body_leveled)
        return;

    const uint32_t spirit_skills =
        game_state_skill_points_gained(old_spirit_level, state->player->level);
    const uint32_t body_skills =
        body_leveled ? game_state_skill_points_gained(old_body_level, body->level) : 0;

    if (spirit_leveled && body_leveled)
    {
        const char *name = body->name[0] ? body->name : "Body";
        snprintf(state->toast_text, sizeof(state->toast_text),
                 "Spirit Lv %u! %s Lv %u!", state->player->level, name, body->level);
    }
    else if (body_leveled)
    {
        const char *name = body->name[0] ? body->name : "Body";
        if (body_skills > 0)
            snprintf(state->toast_text, sizeof(state->toast_text),
                     "%s Lv %u! +%u skill pt", name, body->level, body_skills);
        else
            snprintf(state->toast_text, sizeof(state->toast_text),
                     "%s Lv %u!", name, body->level);
    }
    else
    {
        if (spirit_skills > 0)
            snprintf(state->toast_text, sizeof(state->toast_text),
                     "Level %u! +%u skill pt", state->player->level, spirit_skills);
        else
            snprintf(state->toast_text, sizeof(state->toast_text),
                     "Level %u!", state->player->level);
    }
    state->toast_text[sizeof(state->toast_text) - 1] = '\0';
    state->toast_start_ms = SDL_GetTicks();
    state->toast_duration_ms = 3500;
    state->toast_y_offset = 0.0f;
    state->toast_active = true;
}

static bool game_state_is_player_attacker(const GameState *state, uint32_t attacker_id)
{
    if (!state)
        return false;
    if (attacker_id == MOB_THREAT_PLAYER)
        return true;
    if (state->dominated_actor_id != 0 && attacker_id == state->dominated_actor_id)
        return true;
    return false;
}

static void game_state_try_award_kill_xp(GameState *state, Actor *victim, uint32_t attacker_id)
{
    if (!state || !victim || victim->health > 0)
        return;
    if (!game_state_is_player_attacker(state, attacker_id))
        return;
    // Never grant XP for the inhabited body dropping.
    if (state->dominated_actor_id != 0 && victim->id == state->dominated_actor_id)
        return;

    uint32_t xp = XP_MOB_KILL_BASE + victim->level * 5u;
    char reason[96];
    snprintf(reason, sizeof(reason), "Defeated %s (+%u XP)",
             victim->name[0] ? victim->name : "foe", xp);
    game_state_award_experience(state, xp, reason);
}

// Apply ice chill / freeze from a hit of `chill_add` (already potency-scaled).
static void game_state_apply_ice_chill(GameState *state, Actor *actor, uint32_t chill_add,
                                       float freeze_s)
{
    if (!state || !actor || chill_add == 0)
        return;
    uint16_t chill = (uint16_t)actor->chill + (uint16_t)(chill_add > 255u ? 255u : chill_add);
    if (chill >= 255u)
    {
        actor->chill = 255u;
        actor->freeze_ttl = freeze_s;
        snprintf(state->status_message, sizeof(state->status_message),
                 "Ice Bolt freezes %s!", actor->name);
    }
    else
    {
        actor->chill = (uint8_t)chill;
        snprintf(state->status_message, sizeof(state->status_message),
                 "Ice Bolt chills %s (%u%%)", actor->name,
                 (unsigned)((actor->chill * 100u) / 255u));
    }
}

// Explosion at the end of a combat projectile: splash damage, temperature transfer, particles.
// Direct-hit actors are skipped (they already took full damage); falloff is linear to the rim.
static bool game_state_apply_projectile_blast(GameState *state, World *hit_world,
                                              const ProjectileImpact *im)
{
    if (!state || !hit_world || !im)
        return false;

    const bool ice = (im->projectile == PROJECTILE_ICE_BOLT);
    const bool fire = (im->projectile == PROJECTILE_FIREBALL || im->projectile == PROJECTILE_METEOR);
    const bool lightning = (im->projectile == PROJECTILE_LIGHTNING);
    if (!ice && !fire && !lightning)
        return false;

    float blast_r;
    float splash_frac;
    uint32_t heat_amount = 0;
    if (ice)
    {
        blast_r = PLAYER_ICE_BOLT_BLAST_RADIUS;
        splash_frac = PLAYER_ICE_BOLT_SPLASH_FRAC;
    }
    else if (im->projectile == PROJECTILE_METEOR)
    {
        blast_r = PLAYER_METEOR_BLAST_RADIUS;
        splash_frac = PLAYER_METEOR_SPLASH_FRAC;
        heat_amount = PLAYER_METEOR_HEAT_AMOUNT;
    }
    else if (lightning)
    {
        blast_r = PLAYER_LIGHTNING_BOLT_BLAST_RADIUS;
        splash_frac = PLAYER_LIGHTNING_BOLT_SPLASH_FRAC;
    }
    else
    {
        blast_r = PLAYER_FIREBALL_BLAST_RADIUS;
        splash_frac = PLAYER_FIREBALL_SPLASH_FRAC;
        heat_amount = PLAYER_FIREBALL_HEAT_AMOUNT;
    }

    const uint16_t pot = im->potency > 0 ? im->potency : 100u;
    const uint32_t attacker = im->owner_id ? im->owner_id : MOB_THREAT_PLAYER;
    const uint32_t skip_actor =
        (im->kind == PROJECTILE_IMPACT_ACTOR) ? im->actor_id : 0u;

    particle_effects_spawn_blast(hit_world, im->x, im->y, im->z, ice, blast_r);

    bool voxels_changed = false;

    // Splash damage / chill on nearby living actors.
    if (hit_world->runtime_actors && im->damage > 0)
    {
        const float r2 = blast_r * blast_r;
        for (int a = 0; a < hit_world->runtime_actor_count; a++)
        {
            Actor *actor = &hit_world->runtime_actors[a];
            if (!actor->is_active || actor->health == 0)
                continue;
            if (skip_actor != 0 && actor->id == skip_actor)
                continue;
            if (attacker != 0 && actor->id == attacker)
                continue;

            const float dx = (float)actor->x - im->x;
            const float dy = (float)actor->y - im->y;
            const float az = (float)actor->z;
            // Closest point on the standing capsule to the blast centre (same extents as hits).
            float cz = im->z;
            if (cz < az - PROJECTILE_ACTOR_HIT_BELOW)
                cz = az - PROJECTILE_ACTOR_HIT_BELOW;
            else if (cz > az + PROJECTILE_ACTOR_HIT_ABOVE)
                cz = az + PROJECTILE_ACTOR_HIT_ABOVE;
            const float dz = cz - im->z;
            const float dist2 = dx * dx + dy * dy + dz * dz;
            if (dist2 > r2)
                continue;
            const float dist = sqrtf(dist2);
            const float t = 1.0f - dist / blast_r;
            if (t <= 0.0f)
                continue;

            const uint32_t splash = (uint32_t)((float)im->damage * splash_frac * t + 0.5f);
            if (splash > 0)
            {
                actor_apply_damage(actor, splash);
                mob_actor_after_damage(actor, attacker);
                game_state_try_award_kill_xp(state, actor, attacker);
                if (state->dominated_actor_id == actor->id && actor->health == 0)
                    player_controls_release_dominate(state);
            }

            if (ice)
            {
                const uint32_t chill_add =
                    (uint32_t)((float)((PLAYER_ICE_BOLT_ACTOR_CHILL * (uint32_t)pot + 50u) / 100u) *
                                   splash_frac * t +
                               0.5f);
                const float freeze_s = PLAYER_ICE_BOLT_FREEZE_S * ((float)pot / 100.0f);
                game_state_apply_ice_chill(state, actor, chill_add, freeze_s);
            }
        }
    }

    if (lightning)
        return voxels_changed;

    // Temperature transfer through a voxel neighbourhood around the blast.
    const int cx = (int)floorf(im->x);
    const int cy = (int)floorf(im->y);
    const int cz = (int)floorf(im->z);
    const int ir = (int)ceilf(blast_r);
    for (int dz = -ir; dz <= ir; dz++)
    {
        for (int dy = -ir; dy <= ir; dy++)
        {
            for (int dx = -ir; dx <= ir; dx++)
            {
                const float dist = sqrtf((float)(dx * dx + dy * dy + dz * dz));
                if (dist > blast_r)
                    continue;
                const float t = 1.0f - dist / blast_r;
                if (t <= 0.0f)
                    continue;

                const int x = cx + dx;
                const int y = cy + dy;
                const int z = cz + dz;
                if (x < 0 || y < 0 || z < 0 ||
                    x >= (int)hit_world->width || y >= (int)hit_world->height ||
                    z >= (int)hit_world->depth)
                    continue;

                Voxel *v = world_get_voxel(hit_world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
                if (!v || v->type == VOXEL_AIR)
                    continue;

                if (ice)
                {
                    fire_extinguish_at(hit_world, x, y, z);
                    const uint32_t cool =
                        (uint32_t)((float)((PLAYER_ICE_BOLT_COOL_AMOUNT * (uint32_t)pot + 50u) /
                                           100u) *
                                       t +
                                   0.5f);
                    if (cool == 0)
                        continue;
                    const VoxelType before = v->type;
                    const bool froze =
                        voxel_cool(v, cool > 255u ? (uint8_t)255u : (uint8_t)cool);
                    if (froze)
                    {
                        world_set_voxel(hit_world, (uint32_t)x, (uint32_t)y, (uint32_t)z,
                                        VOXEL_ICE);
                    }
                    else
                    {
                        hit_world->voxel_revision++;
                        (void)before;
                    }
                    voxels_changed = true;
                }
                else if (fire)
                {
                    const uint32_t heat =
                        (uint32_t)((float)heat_amount * t + 0.5f);
                    if (heat > 0)
                    {
                        // Fresh cells store temp 0; lift to ambient first so heating is visible.
                        if (voxel_get_temperature(v) == 0u && v->type != VOXEL_ICE)
                            voxel_set_temperature(v, VOXEL_TEMP_AMBIENT);
                        voxel_heat(v, heat > 255u ? (uint8_t)255u : (uint8_t)heat);
                        hit_world->voxel_revision++;
                        voxels_changed = true;
                    }
                    // Stronger toward the core: medium burn near centre, low toward the rim.
                    if (t >= 0.35f)
                        fire_ignite_at(hit_world, x, y, z,
                                       t >= 0.7f ? "BURNING_MEDIUM" : "BURNING_LOW");
                }
            }
        }
    }

    return voxels_changed;
}

void game_state_step_projectiles(GameState *state, double delta_time)
{
    if (!state || !(delta_time > 0.0))
        return;

    World *world = game_state_physics_world(state);
    if (!world)
        return;

    // One frame cannot produce more impacts than there are projectiles, since a projectile is
    // retired by its first impact.
    ProjectileImpact impacts[PROJECTILE_MAX];
    int impact_count = 0;
    projectile_system_step_universe(&state->projectiles, &state->universe, world, (float)delta_time,
                                    impacts, PROJECTILE_MAX, &impact_count);

    bool voxels_changed = false;

    for (int i = 0; i < impact_count; i++)
    {
        const ProjectileImpact *im = &impacts[i];
        World *hit_world = im->world ? im->world : world;

        if (im->kind == PROJECTILE_IMPACT_ACTOR)
        {
            // The projectile reports who it hit and for how much; deciding what that costs is the
            // game's business, not the flight simulation's.
            for (int a = 0; a < hit_world->runtime_actor_count; a++)
            {
                Actor *actor = &hit_world->runtime_actors[a];
                if (actor->id != im->actor_id)
                    continue;
                const uint32_t attacker =
                    im->owner_id ? im->owner_id : MOB_THREAT_PLAYER;
                actor_apply_damage(actor, im->damage);
                mob_actor_after_damage(actor, attacker);
                play_projectile_impact_sound(im->projectile);
                if (im->projectile == PROJECTILE_ICE_BOLT)
                {
                    const uint16_t pot = im->potency > 0 ? im->potency : 100u;
                    const uint32_t chill_add =
                        (PLAYER_ICE_BOLT_ACTOR_CHILL * (uint32_t)pot + 50u) / 100u;
                    const float freeze_s =
                        PLAYER_ICE_BOLT_FREEZE_S * ((float)pot / 100.0f);
                    game_state_apply_ice_chill(state, actor, chill_add, freeze_s);
                }
                else if (im->projectile == PROJECTILE_SHADOW_STRIKE)
                {
                    const uint16_t pot = im->potency > 0 ? im->potency : 100u;
                    actor->poison_ttl = PLAYER_SHADOW_STRIKE_POISON_S * ((float)pot / 100.0f);
                    actor->poison_dps =
                        PLAYER_SHADOW_STRIKE_POISON_DPS * ((float)pot / 100.0f);
                    actor->poison_accum = 0.0f;
                    snprintf(state->status_message, sizeof(state->status_message),
                             "Shadow Strike poisons %s!", actor->name);
                }
                else
                {
                    const char *label = "Fireball";
                    if (im->projectile == PROJECTILE_METEOR)
                        label = "Meteor";
                    else if (im->projectile == PROJECTILE_LIGHTNING)
                        label = "Lightning";
                    else if (im->projectile == PROJECTILE_MAGIC_MISSILE)
                        label = "Magic Missile";
                    snprintf(state->status_message, sizeof(state->status_message),
                             "%s hits %s for %u", label, actor->name, im->damage);
                }
                game_state_try_award_kill_xp(state, actor, attacker);
                if (state->dominated_actor_id == actor->id && actor->health == 0)
                    player_controls_release_dominate(state);
                break;
            }
            if (game_state_apply_projectile_blast(state, hit_world, im))
                voxels_changed = true;
            continue;
        }

        if (im->kind == PROJECTILE_IMPACT_EXPIRED)
        {
            // Airburst when the shot burns out mid-flight.
            play_projectile_impact_sound(im->projectile);
            if (game_state_apply_projectile_blast(state, hit_world, im))
                voxels_changed = true;
            continue;
        }

        if (im->kind != PROJECTILE_IMPACT_VOXEL)
            continue;

        // Ice bolt: cool the impact cell. Water freezes once temperature hits zero;
        // burning surfaces are doused. No ignition, no shatter.
        if (im->projectile == PROJECTILE_ICE_BOLT)
        {
            play_projectile_impact_sound(im->projectile);
            Voxel *v = world_get_voxel(hit_world, (uint32_t)im->vx, (uint32_t)im->vy,
                                       (uint32_t)im->vz);
            if (v && v->type != VOXEL_AIR)
            {
                fire_extinguish_at(hit_world, im->vx, im->vy, im->vz);
                const VoxelType before = v->type;
                const uint16_t pot = im->potency > 0 ? im->potency : 100u;
                const uint32_t cool =
                    (PLAYER_ICE_BOLT_COOL_AMOUNT * (uint32_t)pot + 50u) / 100u;
                const bool froze = voxel_cool(v, cool > 255u ? (uint8_t)255u : (uint8_t)cool);
                if (froze)
                {
                    // Re-write through world_set_voxel so fluid presence / occupancy stay honest.
                    world_set_voxel(hit_world, (uint32_t)im->vx, (uint32_t)im->vy,
                                    (uint32_t)im->vz, VOXEL_ICE);
                    snprintf(state->status_message, sizeof(state->status_message),
                             "The water freezes solid");
                }
                else
                {
                    hit_world->voxel_revision++;
                    snprintf(state->status_message, sizeof(state->status_message),
                             "Ice Bolt cools the %s", world_voxel_type_name(before));
                }
                voxels_changed = true;
            }
            else
            {
                snprintf(state->status_message, sizeof(state->status_message),
                         "Ice Bolt dissipates");
            }
            if (game_state_apply_projectile_blast(state, hit_world, im))
                voxels_changed = true;
            continue;
        }

        // A fireball that a canopy catches burns the leaves out of the way.
        if (world_voxel_type_is_foliage(im->type))
        {
            world_set_voxel(hit_world, (uint32_t)im->vx, (uint32_t)im->vy, (uint32_t)im->vz, VOXEL_AIR);
            voxel_fracture_disconnect_at(hit_world, im->vx, im->vy, im->vz);
            play_projectile_impact_sound(im->projectile);
            snprintf(state->status_message, sizeof(state->status_message),
                     "The fireball burns through the %s", world_voxel_type_name(im->type));
            if (game_state_is_player_attacker(state, im->owner_id ? im->owner_id : MOB_THREAT_PLAYER))
            {
                char reason[96];
                snprintf(reason, sizeof(reason), "Cleared %s (+%u XP)",
                         world_voxel_type_name(im->type), XP_FOLIAGE_DESTROY);
                game_state_award_experience(state, XP_FOLIAGE_DESTROY, reason);
            }
            voxels_changed = true;
            if (game_state_apply_projectile_blast(state, hit_world, im))
                voxels_changed = true;
            continue;
        }

        if (!voxel_is_destructible(im->type))
        {
            // Even indestructible fixtures (none today) or already-handled foliage: still try to
            // light a candle/campfire that the ball glanced.
            if (im->type == VOXEL_CANDLE || im->type == VOXEL_CAMPFIRE)
                fire_ignite_at(hit_world, im->vx, im->vy, im->vz, "BURNING_HIGH");
            if (game_state_apply_projectile_blast(state, hit_world, im))
                voxels_changed = true;
            continue;
        }

        Voxel *v = world_get_voxel(hit_world, (uint32_t)im->vx, (uint32_t)im->vy, (uint32_t)im->vz);
        if (!v || v->type != im->type)
        {
            if (game_state_apply_projectile_blast(state, hit_world, im))
                voxels_changed = true;
            continue;
        }

        // Dry flammable surfaces catch on contact; wet ones shrug the flame off.
        if (fire_ignite_at(hit_world, im->vx, im->vy, im->vz, "BURNING_MEDIUM"))
        {
            snprintf(state->status_message, sizeof(state->status_message),
                     "The fireball ignites the %s", world_voxel_type_name(im->type));
        }

        const float impact_speed = im->speed > 0.0f ? im->speed : (float)im->damage;
        const uint8_t hit = voxel_impulse_damage(im->type, impact_speed);
        const bool destroyed = voxel_apply_damage(v, hit);
        hit_world->voxel_revision++; // damage lives in data8; bump so renderers refresh cracks
        voxels_changed = true;

        if (!destroyed)
        {
            play_block_hit_sound();
            if (!fire_voxel_is_burning(v))
                snprintf(state->status_message, sizeof(state->status_message),
                         "The fireball cracks the %s", world_voxel_type_name(im->type));
            if (game_state_apply_projectile_blast(state, hit_world, im))
                voxels_changed = true;
            continue;
        }

        const VoxelType broken = v->type;
        const ItemId drop = voxel_type_to_drop_item(broken);
        // Geometric stamp deletes the impact cell (and nearby shatter), then the disconnector
        // drops anything that can no longer reach bedrock / the floor.
        float dx = im->x - ((float)im->vx + 0.5f);
        float dy = im->y - ((float)im->vy + 0.5f);
        float dz = im->z - ((float)im->vz + 0.5f);
        if (dx * dx + dy * dy + dz * dz < 1e-6f)
        {
            dx = 1.0f;
            dy = 0.0f;
            dz = 0.0f;
        }
        voxel_fracture_apply_crack(hit_world, im->vx, im->vy, im->vz, dx, dy, dz, impact_speed,
                                   voxel_crack_seed(im->vx, im->vy, im->vz) ^ im->damage);
        play_block_break_sound();

        if (drop != ITEM_NONE)
        {
            debris_spawn_from_voxel(&state->debris, hit_world,
                                    (float)im->vx + 0.5f, (float)im->vy + 0.5f,
                                    (float)im->vz + 0.5f, broken, VOXEL_PIECES_PER_BLOCK,
                                    voxel_crack_seed(im->vx, im->vy, im->vz) ^ im->damage);
        }

        if (game_state_is_player_attacker(state, im->owner_id ? im->owner_id : MOB_THREAT_PLAYER))
        {
            char reason[96];
            snprintf(reason, sizeof(reason), "Shattered %s (+%u XP)",
                     world_voxel_type_name(broken), XP_BLOCK_DESTROY);
            game_state_award_experience(state, XP_BLOCK_DESTROY, reason);
        }
        else if (voxel_type_is_low_grass(broken))
        {
            snprintf(state->status_message, sizeof(state->status_message),
                     "The fireball scrapes the %s bare", world_voxel_type_name(broken));
        }
        else if (drop != ITEM_NONE)
        {
            snprintf(state->status_message, sizeof(state->status_message),
                     "The fireball shatters the %s", world_voxel_type_name(broken));
        }
        else
        {
            snprintf(state->status_message, sizeof(state->status_message),
                     "The fireball destroys the %s", world_voxel_type_name(broken));
        }

        if (game_state_apply_projectile_blast(state, hit_world, im))
            voxels_changed = true;
    }

    if (voxels_changed)
        fp_renderer_invalidate_cache();
}

void game_state_step_debris(GameState *state, double delta_time)
{
    if (!state || !(delta_time > 0.0))
        return;

    World *world = game_state_physics_world(state);
    if (!world)
        return;

    debris_system_step(&state->debris, world, (float)delta_time, game_state_player_gravity(state));
    voxel_debris_volume_step(&state->debris_volumes, world, (float)delta_time,
                             game_state_player_gravity(state));

    if (!state->player)
        return;

    const uint32_t got = debris_try_pickup(&state->debris, &state->player->inventory,
                                           state->player_world_x, state->player_world_y,
                                           state->player_world_z, DEBRIS_PICKUP_RADIUS);
    if (got > 0)
    {
        snprintf(state->status_message, sizeof(state->status_message),
                 "Collected %.2f blocks of debris", (double)got / (double)ITEM_BLOCK_PIECES);
    }
}

// The fluid simulation moves water one cell per step, so this interval is not a quality setting —
// it is how fast water falls. At 20 Hz a stream drops twenty voxels a second, which reads as
// falling; at the 1 Hz this used to run at, water crept.
//
// The rate is affordable because a step now costs what the fluid in motion costs rather than what
// the world costs. A world with no fluid, or one whose lake has found its level, costs nothing; a
// stream pouring into a basin costs about 0.4ms; the worst case measured — a 32x32x12 block of
// water released onto an open floor, which is what a dam breaking looks like — costs about 6ms for
// the few steps it takes to spread. The previous implementation swept the whole voxel array
// whatever was happening, which cost 90ms on a bone-dry world and is what forced the tick down to
// once a second. See fluid_sim.h and `verse-benchmark --only physics`.
#define WORLD_PHYSICS_TICK_MS 50

// Streaming and eviction stay on the old cadence. Both walk the universe rather than one world, and
// neither has anything to do with how fast water falls: a world does not become distant twenty
// times a second.
#define WORLD_RESIDENCY_TICK_MS 1000

// Cells of fluid one world may move per tick. This bounds the tick, and the bound is what makes the
// rate above safe: settled water queues nothing and costs nothing, but water in violent motion can
// queue more cells than a frame has time for, and the tick blocks the thread that draws.
//
// Measured at about 1.5us a cell on a world whose voxels do not fit in cache, so this is roughly a
// 6ms worst case — a dropped frame at 60Hz, but only while a dam is breaking, and only until the
// water spreads. Whatever does not fit stays queued for the next tick, which delays settling
// without distorting it; test_fluid_sim.c has the case that proves it, because getting that wrong
// leaves water hanging in mid-air.
#define WORLD_FLUID_CELLS_PER_TICK 4096

static void game_state_sync_shadow_world_budgeted(GameState *state, int max_rebuilds);
static void game_state_pump_deep_cluster(GameState *state);

void game_state_step_world_physics(GameState *state, double delta_time)
{
    (void)delta_time;
    if (!state || !state->game_started || state->world_generation_active)
        return;

    // Tied to the runtime clock so time only passes when it is running. Play paths call
    // game_state_ensure_world_clock so wildlife and fluids begin without a manual toggle.
    if (!state->runtime_clock_running)
        return;

    World *world = state->current_world;
    if (!world)
        return;

    Uint32 now = SDL_GetTicks();
    if (state->last_physics_tick_ms != 0 &&
        now - state->last_physics_tick_ms < WORLD_PHYSICS_TICK_MS)
        return;
    state->last_physics_tick_ms = now;

    const float tick_seconds = (float)WORLD_PHYSICS_TICK_MS / 1000.0f;

    // Spirit pose for mobs that aggro on the player (spirit is not a runtime actor).
    mob_ai_set_player_presence(state->player_world_x, state->player_world_y,
                               state->player_world_z, state->dominated_actor_id);

    // The tick runs in three ordered phases, and the order is what makes cross-world queries safe:
    //
    //   1. Parallel. One task per world, each touching only its own voxels. Shadow world queries
    //      are INVALID here — a search reads voxels and occupancy bits that a worker is writing, so
    //      it would see a torn mix of before and after. Nothing in this phase may query.
    //   2. Barrier. world_physics_step_neighbourhood waits for every task before returning.
    //   3. Read-only. The pyramid is refreshed against the new voxel revisions, after which any
    //      number of cross-world queries may run concurrently, because nothing is writing.
    //
    // Springs sit outside phases 1-2 entirely: world_update_springs shares one static cooldown
    // across every world, so it is not safe to run several at once. See world_physics_jobs.h.
    world_physics_step_springs(world, (unsigned long long)now * 1000ULL);

    // Phase 1 and 2: fluids and actors for the current world and one rotating face neighbour.
    state->last_physics_world_count = world_physics_step_neighbourhood(
        &state->universe, world, tick_seconds, WORLD_FLUID_CELLS_PER_TICK);

    // Phase 3. Fluid motion bumps voxel_revision through world_sync_occupancy_bit. Cap rebuilds so
    // a busy neighbourhood cannot spend a whole frame on the 5x5x5 pyramid; centre/faces first.
    game_state_sync_shadow_world_budgeted(state, 3);

    // Eviction on its own, slower clock. Streaming is pumped every frame from game_state_update
    // so finished neighbours land in the universe as soon as the worker is done.
    if (state->last_residency_tick_ms == 0 ||
        now - state->last_residency_tick_ms >= WORLD_RESIDENCY_TICK_MS)
    {
        state->last_residency_tick_ms = now;
        game_state_evict_distant_worlds(state);
        game_state_pump_deep_cluster(state);
    }
}

void game_state_set_movement_destination(GameState *state, int x, int y, int z)
{
    if (!state)
        return;

    // Cancel any existing movement and start fresh
    state->movement_destination.has_destination = true;
    state->movement_destination.is_animating = false;
    state->movement_destination.target_x = x;
    state->movement_destination.target_y = y;
    state->movement_destination.target_z = z;
    state->movement_destination.path_progress = 0.0f;

    // Store the starting position for accurate progress calculation
    state->movement_destination.start_x = state->player_x;
    state->movement_destination.start_y = state->player_y;
    state->movement_destination.start_z = state->player_z;

    printf("MOVEMENT: New destination set to (%d,%d,%d) from (%d,%d,%d)\n",
           x, y, z, state->player_x, state->player_y, state->player_z);
}

void game_state_update_movement(GameState *state)
{
    if (!state || !state->movement_destination.has_destination)
        return;

    Uint32 current_time = SDL_GetTicks();

    // Update current animation step for floating-point movement
    if (state->movement_destination.is_animating)
    {
        Uint32 elapsed = current_time - state->movement_destination.animation_start_time;
        state->movement_destination.animation_progress = (float)elapsed / (float)state->movement_destination.movement_speed_ms;

        // Clamp animation progress
        if (state->movement_destination.animation_progress > 1.0f)
        {
            state->movement_destination.animation_progress = 1.0f;
        }

        // Smoothly interpolate between start and target positions
        float t = state->movement_destination.animation_progress;

        // Use smooth ease-in-out curve for more natural movement
        t = t * t * (3.0f - 2.0f * t);

        state->player_world_x = state->movement_destination.start_world_x +
                               t * (state->movement_destination.target_world_x - state->movement_destination.start_world_x);
        state->player_world_y = state->movement_destination.start_world_y +
                               t * (state->movement_destination.target_world_y - state->movement_destination.start_world_y);
        state->player_world_z = state->movement_destination.start_world_z +
                               t * (state->movement_destination.target_world_z - state->movement_destination.start_world_z);

        // Update voxel index and legacy positions
        game_state_sync_positions(state);

        // Update camera to follow animated movement
        extern IsometricRenderer *g_isometric_renderer;
        if (g_isometric_renderer)
        {
            isometric_renderer_set_camera_world(g_isometric_renderer,
                                                state->player_world_x,
                                                state->player_world_y,
                                                state->player_world_z);
        }

        // Check if movement is complete
        if (state->movement_destination.animation_progress >= 1.0f)
        {
            // Movement complete
            state->movement_destination.has_destination = false;
            state->movement_destination.is_animating = false;
            state->movement_destination.path_progress = 1.0f;

            // Ensure final position is exact
            state->player_world_x = state->movement_destination.target_world_x;
            state->player_world_y = state->movement_destination.target_world_y;
            state->player_world_z = state->movement_destination.target_world_z;
            game_state_sync_positions(state);

            snprintf(state->status_message, sizeof(state->status_message),
                     "Reached destination (%.2f,%.2f,%.2f)",
                     state->player_world_x, state->player_world_y, state->player_world_z);

            MOVE_DBG("MOVEMENT: Reached floating-point destination (%.2f,%.2f,%.2f)\n",
                   state->player_world_x, state->player_world_y, state->player_world_z);
        }
        else
        {
            // Update overall path progress
            state->movement_destination.path_progress = state->movement_destination.animation_progress;
        }
    }
}

void game_state_get_animated_player_position(GameState *state, float *x, float *y, float *z)
{
    if (!state || !x || !y || !z)
        return;

    *x = state->player_world_x;
    *y = state->player_world_y;
    *z = state->player_world_z + player_controls_spirit_hover_bob(state);
}

// ---------------------------------------------------------------------------------------------
// Shadow world cluster: residency, streaming, and eviction
//
// The universe used to only grow. Every world ever visited stayed resident at 96MB of voxels plus
// caches, so a session that wandered accumulated gigabytes and never gave any of it back. These
// three functions are the whole residency policy: keep the cluster pointed at the right worlds,
// stream missing ones in on a worker, and hand the distant ones back.
// ---------------------------------------------------------------------------------------------

// Two regions, matched to the shadow cluster / isometric reach.
//
// Retention is what eviction may not free. The renderer walks dz -2..2 and dx/dy out to
// neighbor_inclusion_radius, which the world editor lets the player raise to 2, so any cell inside
// that box may be held in edge_worlds while a frame is drawn. Freeing one would be a use-after-free
// in the render path, so this is sized to the renderer's *maximum* reach rather than its current
// setting — the cost of being generous is zero for cells nothing ever put a world in.
#define SHADOW_RETAIN_XY 2
#define SHADOW_RETAIN_Z 2

// Streaming fills the shadow cluster (radius 2 / 5x5x5) so first-person can draw a three-world
// sightline and isometric radius-2 already has terrain waiting. The wilderness plane at universe
// z=0 is still prioritised as a 5x5 around the player's XY when they fall toward it.
#define SHADOW_STREAM_XY 2
#define SHADOW_STREAM_Z 2

void game_state_sync_shadow_world(GameState *state)
{
    game_state_sync_shadow_world_budgeted(state, 0);
}

// max_rebuilds <= 0 refreshes every dirty pyramid slot. The physics tick passes a small budget so
// a wet neighbourhood cannot rebuild the whole 5x5x5 cluster on one frame.
static void game_state_sync_shadow_world_budgeted(GameState *state, int max_rebuilds)
{
    if (!state)
        return;

    if (!state->shadow_world)
    {
        state->shadow_world = shadow_world_create(WORLD_SIZE_CUBE);
        if (!state->shadow_world)
            return; // queries fall back to their per-world paths
    }

    ShadowWorld *sw = state->shadow_world;
    const uint64_t ux = state->player_universe_x;
    const uint64_t uy = state->player_universe_y;
    const uint64_t uz = state->player_universe_z;

    shadow_world_set_centre(sw, ux, uy, uz);

    for (int dz = -SHADOW_CLUSTER_RADIUS; dz <= SHADOW_CLUSTER_RADIUS; dz++)
    {
        const int64_t az = (int64_t)uz + dz;
        for (int dy = -SHADOW_CLUSTER_RADIUS; dy <= SHADOW_CLUSTER_RADIUS; dy++)
        {
            for (int dx = -SHADOW_CLUSTER_RADIUS; dx <= SHADOW_CLUSTER_RADIUS; dx++)
            {
                // Below universe z=0 there is nothing, which is not the same as not loaded yet;
                // either way the slot is empty and sampling reports UNLOADED.
                World *w = (az < 0) ? NULL
                                    : universe_get(&state->universe,
                                                   (uint64_t)((int64_t)ux + dx),
                                                   (uint64_t)((int64_t)uy + dy),
                                                   (uint64_t)az);
                shadow_world_attach(sw, dx, dy, dz, w);
            }
        }
    }

    // Only slots whose world changed, or whose voxels moved, are rebuilt. A frame in which nothing
    // happened costs one revision comparison per cluster slot.
    shadow_world_refresh_budget(sw, max_rebuilds);
}

// Take the finished world from a completed streaming job and put it in the universe. Placement
// happens on this thread rather than on the worker because universe_place can rehash the map the
// render path reads every frame.
static void game_state_collect_stream_job(GameState *state, int slot)
{
    CellGenJob *job = state->cell_refill_jobs[slot];

    uint64_t ux = 0, uy = 0, uz = 0;
    cell_gen_job_cell(job, &ux, &uy, &uz);
    World *made = cell_gen_job_take_world(job);
    cell_gen_job_destroy(job);
    state->cell_refill_jobs[slot] = NULL;

    if (!made)
        return;

    if (universe_get(&state->universe, ux, uy, uz) ||
        !universe_place(&state->universe, ux, uy, uz, made))
    {
        world_destroy(made); // beaten to it, or the map refused
        return;
    }

    made->universe_context = &state->universe;
    made->universe_x = ux;
    made->universe_y = uy;
    made->universe_z = uz;
    // Budget the pyramid rebuild: collecting a stream job mid-frame must not rebuild the whole
    // 5x5x5 cluster before the next draw.
    game_state_sync_shadow_world_budgeted(state, 3);
}

// True when this cell is already resident or already being generated. Without the second half, a
// cell would be submitted again on every frame until the first job for it finished, and with several
// jobs in flight that means several workers generating the same world and all but one of the results
// being thrown away.
static bool game_state_cell_covered(GameState *state, uint64_t cx, uint64_t cy, uint64_t cz)
{
    if (universe_get(&state->universe, cx, cy, cz))
        return true;

    for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
    {
        if (!state->cell_refill_jobs[i])
            continue;
        uint64_t jx = 0, jy = 0, jz = 0;
        cell_gen_job_cell(state->cell_refill_jobs[i], &jx, &jy, &jz);
        if (jx == cx && jy == cy && jz == cz)
            return true;
    }
    return false;
}

int game_state_stream_jobs_in_flight(const GameState *state)
{
    if (!state)
        return 0;
    int n = 0;
    for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
        if (state->cell_refill_jobs[i])
            n++;
    return n;
}

// Starts a generation for (cx,cy,cz) in the first free slot, if any. Returns true when a slot was
// taken, which is also when *free_slots is decremented. Covered cells and failed starts do not
// spend a slot.
static bool game_state_try_start_stream_cell(GameState *state, uint64_t cx, uint64_t cy, uint64_t cz,
                                            int *free_slots)
{
    if (!state || !free_slots || *free_slots <= 0)
        return false;
    if (game_state_cell_covered(state, cx, cy, cz))
        return false;

    for (int slot = 0; slot < WORLD_STREAM_MAX_IN_FLIGHT; slot++)
    {
        if (state->cell_refill_jobs[slot])
            continue;
        state->cell_refill_jobs[slot] =
            cell_gen_job_start(state->game_worlds->base_seed, cx, cy, cz);
        if (state->cell_refill_jobs[slot])
        {
            (*free_slots)--;
            return true;
        }
        return false;
    }
    return false;
}

void game_state_pump_world_streaming(GameState *state)
{
    if (!state || !state->game_worlds || !state->game_worlds->base_seed)
        return;
    // Don't compete with the initial generation for workers, and don't stream while the player has
    // no position in the universe yet.
    if (state->world_generation_active || !state->game_started)
        return;

    // With no worker pool, cell_gen_job_start generates inline and returns an already-finished job.
    // Starting the full complement then means several 128^3 worlds generated back to back inside one
    // pump, which is a frame of a second or more rather than the stall-free streaming this is for.
    // One at a time is the old behaviour, and the right behaviour when there is nowhere to offload.
    const int max_in_flight =
        task_scheduler_is_running() ? WORLD_STREAM_MAX_IN_FLIGHT : 1;

    int in_flight = 0;
    for (int i = 0; i < WORLD_STREAM_MAX_IN_FLIGHT; i++)
    {
        if (state->cell_refill_jobs[i] && cell_gen_job_is_complete(state->cell_refill_jobs[i]))
            game_state_collect_stream_job(state, i);
        if (state->cell_refill_jobs[i])
            in_flight++;
    }

    int free_slots = max_in_flight - in_flight;
    if (free_slots <= 0)
        return;

    const uint64_t ux = state->player_universe_x;
    const uint64_t uy = state->player_universe_y;
    const uint64_t uz = state->player_universe_z;

    // Prefetch is only needed when the landing cell was not part of startup. New games already
    // generate the wilderness rings under the island; this path still covers older saves / loads
    // that arrive on home without a resident ground plane.
    if (uz == (uint64_t)UNIVERSE_HOME_Z &&
        !universe_get(&state->universe, ux, uy, 0))
    {
        game_state_try_start_stream_cell(state, ux, uy, 0, &free_slots);
        static const int home_faces[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int i = 0; i < 4 && free_slots > 0; i++)
        {
            const uint64_t fx = (uint64_t)((int64_t)ux + home_faces[i][0]);
            const uint64_t fy = (uint64_t)((int64_t)uy + home_faces[i][1]);
            game_state_try_start_stream_cell(state, fx, fy, uz, &free_slots);
        }
        return;
    }

    // When the player falls (or is already on cloud/wilderness), keep the landing cell and its
    // face ring urgent. Deeper wilderness rings share the deep-fill budget below.
    if (game_state_wilderness_plane_active(state))
    {
        game_state_try_start_stream_cell(state, ux, uy, 0, &free_slots);
        static const int wfaces[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int i = 0; i < 4 && free_slots > 0; i++)
        {
            const uint64_t cx = (uint64_t)((int64_t)ux + wfaces[i][0]);
            const uint64_t cy = (uint64_t)((int64_t)uy + wfaces[i][1]);
            game_state_try_start_stream_cell(state, cx, cy, 0, &free_slots);
        }
        if (free_slots <= 0)
            return;
    }

    // Face neighbours of the player's current cell — every frame, full concurrency. These are the
    // cells a step or a look can hit immediately.
    {
        static const int faces[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
                                       {0, 0, 1}, {0, 0, -1}};
        for (int i = 0; i < 6; i++)
        {
            const int64_t az = (int64_t)uz + faces[i][2];
            if (az < 0)
                continue;
            const uint64_t cx = (uint64_t)((int64_t)ux + faces[i][0]);
            const uint64_t cy = (uint64_t)((int64_t)uy + faces[i][1]);
            game_state_try_start_stream_cell(state, cx, cy, (uint64_t)az, &free_slots);
            if (free_slots <= 0)
                return;
        }

        // Deep cluster fill is not a per-frame job. Once the face ring is queued, further holes
        // close from the 1 Hz residency tick via game_state_pump_deep_cluster.
        return;
    }
}

static void game_state_pump_deep_cluster(GameState *state)
{
    if (!state || !state->game_worlds || !state->game_worlds->base_seed)
        return;
    if (state->world_generation_active || !state->game_started)
        return;
    if (!task_scheduler_is_running())
        return;

    int in_flight = game_state_stream_jobs_in_flight(state);
    int free_slots = WORLD_STREAM_MAX_IN_FLIGHT - in_flight;
    if (free_slots <= 0)
        return;
    free_slots = 1;

    const uint64_t ux = state->player_universe_x;
    const uint64_t uy = state->player_universe_y;
    const uint64_t uz = state->player_universe_z;

    const int max_ring = (SHADOW_STREAM_XY > SHADOW_STREAM_Z) ? SHADOW_STREAM_XY : SHADOW_STREAM_Z;

    for (int ring = 0; ring <= max_ring; ring++)
    {
        for (int off_axes = 0; off_axes <= 3; off_axes++)
        {
            for (int dz = -ring; dz <= ring; dz++)
            {
                for (int dy = -ring; dy <= ring; dy++)
                {
                    for (int dx = -ring; dx <= ring; dx++)
                    {
                        int cheb = abs(dx);
                        if (abs(dy) > cheb) cheb = abs(dy);
                        if (abs(dz) > cheb) cheb = abs(dz);
                        if (cheb != ring)
                            continue;

                        const int axes = (dx != 0) + (dy != 0) + (dz != 0);
                        if (axes != off_axes)
                            continue;

                        if (abs(dx) > SHADOW_STREAM_XY || abs(dy) > SHADOW_STREAM_XY ||
                            abs(dz) > SHADOW_STREAM_Z)
                            continue;

                        if (cheb <= 1 && axes <= 1)
                            continue;

                        const int64_t az = (int64_t)uz + dz;
                        if (az < 0)
                            continue;

                        const uint64_t cx = (uint64_t)((int64_t)ux + dx);
                        const uint64_t cy = (uint64_t)((int64_t)uy + dy);
                        if (game_state_try_start_stream_cell(state, cx, cy, (uint64_t)az, &free_slots))
                            return;
                        if (free_slots <= 0)
                            return;
                    }
                }
            }
        }
    }
}

// True when GameWorlds holds this pointer, in which case game_worlds_destroy will free it and
// eviction must not. The initial generation both places its worlds in the universe and keeps them in
// these arrays, so the two are not disjoint and every field has to be checked — freeing one here
// would leave game_worlds_destroy with a dangling pointer to free again.
static bool game_worlds_owns_world(const GameWorlds *gw, const World *world)
{
    if (!gw || !world)
        return false;
    if (world == gw->home_world || world == gw->farm_world || world == gw->wilderness_world ||
        world == gw->random_teleport_world)
        return true;
    for (int i = 0; i < 26; i++)
    {
        if (world == gw->adjacent_farm_worlds[i] || world == gw->adjacent_home_worlds[i])
            return true;
    }
    return false;
}

int game_state_evict_distant_worlds(GameState *state)
{
    if (!state || !state->game_started || state->world_generation_active)
        return 0;

    const uint64_t ux = state->player_universe_x;
    const uint64_t uy = state->player_universe_y;
    const uint64_t uz = state->player_universe_z;

    // Collect first, then remove: universe_remove reshuffles the table it is walking.
    uint64_t doomed[64][3];
    int doomed_count = 0;

    for (size_t i = 0; i < state->universe.capacity && doomed_count < 64; i++)
    {
        if (!state->universe.entries[i].used)
            continue;
        World *w = state->universe.entries[i].w;
        if (!w)
            continue;

        // Worlds referenced from somewhere other than the map are not ours to free. The world under
        // the player is obviously in use, and anything GameWorlds holds is freed by it instead.
        if (w == state->current_world || w == state->main_menu_world)
            continue;
        if (game_worlds_owns_world(state->game_worlds, w))
            continue;

        const int64_t dx = (int64_t)state->universe.entries[i].x - (int64_t)ux;
        const int64_t dy = (int64_t)state->universe.entries[i].y - (int64_t)uy;
        const int64_t dz = (int64_t)state->universe.entries[i].z - (int64_t)uz;
        if (llabs(dx) <= SHADOW_RETAIN_XY && llabs(dy) <= SHADOW_RETAIN_XY &&
            llabs(dz) <= SHADOW_RETAIN_Z)
            continue;

        doomed[doomed_count][0] = state->universe.entries[i].x;
        doomed[doomed_count][1] = state->universe.entries[i].y;
        doomed[doomed_count][2] = state->universe.entries[i].z;
        doomed_count++;
    }

    int evicted = 0;
    for (int i = 0; i < doomed_count; i++)
    {
        World *w = NULL;
        if (!universe_remove(&state->universe, doomed[i][0], doomed[i][1], doomed[i][2], &w) || !w)
            continue;

        // Generation is seeded, so an untouched world can be rebuilt bit for bit from its cell
        // coordinates. Only one the player has actually changed is worth the disk write.
        if (world_is_dirty(w) && w->seed_id[0] != '\0')
            world_save_by_seed(w, w->seed_id);

        world_destroy(w);
        evicted++;
    }

    if (evicted > 0)
        game_state_sync_shadow_world(state);
    return evicted;
}

void game_state_sync_isometric_renderer(GameState *state, IsometricRenderer *renderer)
{
    if (!state || !renderer)
        return;

    isometric_renderer_set_camera_world(renderer,
                                        state->player_world_x,
                                        state->player_world_y,
                                        state->player_world_z);
    isometric_renderer_set_player_avatar(renderer,
                                         state->controls.facing_yaw,
                                         state->controls.is_attacking,
                                         player_controls_swing_progress(&state->controls),
                                         state->controls.attack_armed,
                                         player_controls_swing_radius(&state->controls),
                                         state->controls.attack_strength,
                                         state->game_started &&
                                             !player_controls_is_dominating(state));
    // Borrowed, not copied: the renderer reads the pool during the frame it was set for, and the
    // game state outlives the renderer. The first-person renderer keeps its own pointer because it
    // is reached through module-level state rather than through this struct.
    renderer->projectiles = &state->projectiles;
    renderer->debris = &state->debris;
    fp_renderer_set_projectiles(&state->projectiles);
    fp_renderer_set_debris(&state->debris);
    fp_renderer_set_debris_volumes(&state->debris_volumes);
    if (state->controls.is_attacking)
    {
        fp_renderer_set_swing(state->controls.facing_yaw,
                              player_controls_swing_progress(&state->controls),
                              player_controls_swing_radius(&state->controls),
                              player_controls_swing_arc(&state->controls),
                              state->controls.attack_armed,
                              state->controls.attack_strength);
    }
    else
    {
        fp_renderer_set_swing(0.0f, 0.0f, PLAYER_SWING_RADIUS, PLAYER_SWING_ARC_RAD, false, 10);
    }
    renderer->dialogue_speaker_id =
        dialogue_active(&state->dialogue) ? dialogue_speaker_id(&state->dialogue) : 0;
    if (state->player)
    {
        isometric_renderer_set_player_stamina(renderer,
                                              (uint32_t)(state->player->stamina + 0.5f),
                                              (uint32_t)(actor_max_stamina(state->player) + 0.5f));
    }
    else
    {
        isometric_renderer_set_player_stamina(renderer, 0, PLAYER_DEFAULT_STAMINA_MAX);
    }

    isometric_renderer_set_universe_layer(renderer, state->player_universe_z,
                                          /*hide_wilderness_below_home=*/false);
    isometric_renderer_set_stamina_meter_alpha(renderer, state->stamina_meter_alpha);
    // Fog of war is map-only; play isometric never masks unexplored voxels.
    isometric_renderer_set_fog(renderer, NULL);

    // Part foliage around the player and any inhabited body. Radius covers a stride so a walk
    // through tall grass opens a short wake rather than only the single cell underfoot.
    foliage_bend_field_clear(&renderer->foliage_bend);
    foliage_bend_field_add(&renderer->foliage_bend, state->player_world_x, state->player_world_y,
                           state->player_world_z, 1.85f);
    if (state->current_world && state->current_world->runtime_actors)
    {
      for (int ai = 0; ai < state->current_world->runtime_actor_count; ai++)
      {
        const Actor *a = &state->current_world->runtime_actors[ai];
        if (!a->is_active || !a->is_controlled)
          continue;
        foliage_bend_field_add(&renderer->foliage_bend, (float)a->x, (float)a->y, (float)a->z,
                               1.85f);
      }
    }
    fp_renderer_set_foliage_bend(&renderer->foliage_bend);

    // Carve a volumetric wake through steam/gas the same way foliage parts: the player and any
    // inhabited body push fog aside. Velocity piles a bow wave ahead so a sprint reads differently
    // from standing still inside a cloud.
    if (renderer->fog_volume && state->current_world)
    {
      fog_volume_body_wake(renderer->fog_volume, state->current_world, state->player_world_x,
                           state->player_world_y, state->player_world_z, 1.1f,
                           state->controls.velocity_x, state->controls.velocity_y,
                           state->controls.velocity_z);
      if (state->current_world->runtime_actors)
      {
        for (int ai = 0; ai < state->current_world->runtime_actor_count; ai++)
        {
          const Actor *a = &state->current_world->runtime_actors[ai];
          if (!a->is_active || !a->is_controlled)
            continue;
          fog_volume_body_wake(renderer->fog_volume, state->current_world, (float)a->x,
                               (float)a->y, (float)a->z, 1.1f, (float)a->velocity_x,
                               (float)a->velocity_y, (float)a->velocity_z);
        }
      }
    }

    if (!state->current_world)
        return;

    for (int i = 1; i < 125; i++)
        renderer->edge_worlds[i] = NULL;

    const int radius = renderer->neighbor_inclusion_radius;
    const uint64_t ux = state->player_universe_x;
    const uint64_t uy = state->player_universe_y;
    const uint64_t uz = state->player_universe_z;

    // From the sky island, reach two worlds down to the wilderness plane (and keep one sky/cloud
    // ring up). Horizontal radius covers the pre-generated wilderness rings as distance terrain.
    const int dz_min = -2;
    const int dz_max =
        (uz >= (uint64_t)UNIVERSE_HOME_Z) ? 1 : 2;

    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            for (int dz = dz_min; dz <= dz_max; dz++)
            {
                if (dx == 0 && dy == 0 && dz == 0)
                    continue;
                if (!game_state_neighbor_visible(state, dz))
                    continue;

                int idx = isometric_renderer_offset_index(renderer, dx, dy, dz);
                if (idx <= 0)
                    continue;

                int64_t az = (int64_t)uz + dz;
                if (az < 0)
                    continue;

                World *w = universe_get(&state->universe,
                                        (uint64_t)((int64_t)ux + dx),
                                        (uint64_t)((int64_t)uy + dy),
                                        (uint64_t)az);
                if (w)
                    renderer->edge_worlds[idx] = w;
            }
        }
    }
}

float game_state_music_intensity(const GameState *state)
{
    if (!state || !state->game_started)
        return 0.0f;

    if (state->current_screen == GAME_SCREEN_BATTLE)
        return 1.0f;

    if (state->controls.is_attacking)
        return 1.0f;

    World *world = state->current_world;
    if (!world || !world->runtime_actors || world->runtime_actor_count <= 0)
        return 0.0f;

    const uint32_t body_id = state->dominated_actor_id;
    const float px = state->player_world_x;
    const float py = state->player_world_y;
    const float pz = state->player_world_z;
    const float hear_range = 28.0f;
    float best = 0.0f;

    for (int i = 0; i < world->runtime_actor_count; i++)
    {
        const Actor *a = &world->runtime_actors[i];
        if (!a->is_active || a->health == 0 || !a->extra_data)
            continue;

        const MobActor *mob = (const MobActor *)a->extra_data;
        if (mob->aggro_ttl <= 0.0f || mob->aggro_target_id == 0)
            continue;

        const bool on_player =
            (mob->aggro_target_id == MOB_THREAT_PLAYER) ||
            (body_id != 0 && mob->aggro_target_id == body_id);
        if (!on_player)
            continue;

        // Fleeing prey (deer, etc.) shouldn't yank the score into combat.
        if (mob->goal == MOB_GOAL_FLEE)
            continue;

        float dx = (float)a->x - px;
        float dy = (float)a->y - py;
        float dz = (float)a->z - pz;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist >= hear_range)
            continue;

        float local = 1.0f - (dist / hear_range);
        if (local > best)
            best = local;
    }

    return best;
}

void game_state_set_screen(GameState *state, GameScreen screen)
{
    if (!state)
        return;
    state->current_screen = screen;
}

void game_state_show_modal(GameState *state, const char *modal_type)
{
    if (!state || !modal_type)
        return;

    if (strcmp(modal_type, "exit_prompt") == 0)
    {
        state->show_exit_prompt = true;
    }
    else if (strcmp(modal_type, "new_game_warning") == 0)
    {
        state->show_new_game_warning = true;
    }
    else if (strcmp(modal_type, "name_input") == 0)
    {
        state->show_name_input = true;
    }
    else if (strcmp(modal_type, "chapter") == 0)
    {
        state->show_chapter = true;
    }
}

void game_state_hide_modal(GameState *state, const char *modal_type)
{
    if (!state || !modal_type)
        return;

    if (strcmp(modal_type, "exit_prompt") == 0)
    {
        state->show_exit_prompt = false;
    }
    else if (strcmp(modal_type, "new_game_warning") == 0)
    {
        state->show_new_game_warning = false;
    }
    else if (strcmp(modal_type, "name_input") == 0)
    {
        state->show_name_input = false;
    }
    else if (strcmp(modal_type, "chapter") == 0)
    {
        state->show_chapter = false;
    }
}

// Async world generation functions
void game_state_start_async_world_generation(GameState *state)
{
    if (!state)
        return;

    printf("Starting async world generation...\n");
    state->world_generation_active = true;
    state->world_generation_step = 0;
    state->last_world_gen_time = SDL_GetTicks();

    // Create the game worlds structure
    state->game_worlds = game_worlds_create(state->player_name);
    if (!state->game_worlds)
    {
        printf("Failed to create game worlds structure\n");
        state->world_generation_active = false;
        return;
    }

    // URGENT: only generate initial player-owned home world here for now.
    // Set VERSE_HOME_ONLY_STARTUP to 0 in constants.h to restore the full sequence.
    WorldGenRequest request = {
        .worlds = state->game_worlds,
        .universe = &state->universe,
        .home_only = (VERSE_HOME_ONLY_STARTUP != 0),
    };

    state->loading_total = world_gen_job_total_steps(&request);
    state->loading_progress = 0;
    strcpy(state->loading_message, "Initializing world generation...");

    state->world_generation_job = world_gen_job_start(&request);
    if (!state->world_generation_job)
    {
        printf("Failed to start world generation job\n");
        state->world_generation_active = false;
        // Leave a non-zero step behind: the client treats step 0 as "not started yet" and would
        // otherwise call this again every frame, allocating a fresh GameWorlds each time.
        state->world_generation_step = 1;
        game_worlds_destroy(state->game_worlds);
        state->game_worlds = NULL;
    }
}

bool game_state_update_world_generation(GameState *state)
{
    if (!state || !state->world_generation_active)
        return false;

    if (!state->world_generation_job)
    {
        state->world_generation_active = false;
        return false;
    }

    // The worker owns state->game_worlds and the universe until it reports complete, so all
    // this does is copy out the progress the loading screen draws.
    int progress = 0;
    char message[sizeof(state->loading_message)];
    bool complete = world_gen_job_snapshot(state->world_generation_job, &progress, message,
                                           sizeof(message));

    state->loading_progress = progress;
    state->world_generation_step = progress;
    strncpy(state->loading_message, message, sizeof(state->loading_message) - 1);
    state->loading_message[sizeof(state->loading_message) - 1] = '\0';

    if (!complete)
        return false;

    // Generation has finished, so the worlds are ours to read again. Everything from here
    // touches player and render state and therefore belongs on this thread.
    state->current_world = world_gen_job_primary_world(state->world_generation_job);

    world_gen_job_destroy(state->world_generation_job);
    state->world_generation_job = NULL;

    state->loading_progress = state->loading_total;
    // The client uses a zero step count to mean "generation has not been started", so this has
    // to end up non-zero even when generation failed, or it would be started again every frame.
    state->world_generation_step = state->loading_total > 0 ? state->loading_total : 1;

    if (!state->current_world)
    {
        printf("World generation produced no playable world\n");
        state->world_generation_active = false;
        return true;
    }

    if (!game_state_place_player_at_surface_spawn(state))
    {
        printf("Failed to find safe spawn after world generation\n");
    }

    state->player_universe_x = 0;
    state->player_universe_y = 0;
    state->player_universe_z = (uint64_t)UNIVERSE_HOME_Z;

    if (state->game_worlds)
        world_transition_set_game_worlds(state->game_worlds);

    // Neighbours are not generated here. game_state_pump_world_streaming fills them in from the
    // player's position, nearest first. Wilderness neighbours wait until the player falls toward
    // that plane (or is already on it), which is when they are worth drawing and walking into.

    if (!state->player)
    {
        const char *spirit_name = state->player_name[0] ? state->player_name : "Spirit";
        state->player = actor_create(spirit_name, "Ancient spirit", "home");
        if (state->player)
        {
            state->player->turn_speed = 240;
            inventory_seed_spirit_starter(&state->player->inventory);
        }
    }
    state->player_flying = true;
    if (state->player)
        state->player->is_flying = true;

    if (state->current_world)
        world_spawn_home_mobs(state->current_world);

    state->world_generation_active = false;
    state->game_started = false;

    // Build the query cluster now that the player has a cell, rather than waiting for the first
    // physics tick (clock starts when play begins via game_state_ensure_world_clock).
    game_state_sync_shadow_world(state);

    printf("Async world generation completed successfully\n");
    return true;
}

// ============================================================================
// FLOATING-POINT POSITION SYSTEM FUNCTIONS
// ============================================================================

// Update the voxel index based on current world position
void game_state_update_voxel_index(GameState *state)
{
    if (!state)
        return;

    // The voxel index is the floor of the world position
    state->player_voxel_x = (int)floorf(state->player_world_x);
    state->player_voxel_y = (int)floorf(state->player_world_y);
    state->player_voxel_z = (int)floorf(state->player_world_z);

    // Clamp to world bounds
    World *world = state->current_world ? state->current_world : state->main_menu_world;
    if (world)
    {
        if (state->player_voxel_x < 0) state->player_voxel_x = 0;
        if (state->player_voxel_x >= (int)world->width) state->player_voxel_x = (int)world->width - 1;
        if (state->player_voxel_y < 0) state->player_voxel_y = 0;
        if (state->player_voxel_y >= (int)world->height) state->player_voxel_y = (int)world->height - 1;
        if (state->player_voxel_z < 0) state->player_voxel_z = 0;
        if (state->player_voxel_z >= (int)world->depth) state->player_voxel_z = (int)world->depth - 1;
    }
}

// Synchronize positions between integer and floating-point systems
void game_state_sync_positions(GameState *state)
{
    if (!state)
        return;

    // Update voxel index from world position
    game_state_update_voxel_index(state);

    // Keep legacy integer positions in sync for compatibility
    state->player_x = state->player_voxel_x;
    state->player_y = state->player_voxel_y;
    state->player_z = state->player_voxel_z;
}

// Check if a floating-point world position is walkable
bool game_state_can_move_to_world(GameState *state, float world_x, float world_y, float world_z)
{
    if (!state)
        return false;

    // Get the voxel coordinates this world position would be in
    int voxel_x = (int)floorf(world_x);
    int voxel_y = (int)floorf(world_y);
    int voxel_z = (int)floorf(world_z);

    // Use the existing integer-based collision detection
    return game_state_can_move_to(state, voxel_x, voxel_y, voxel_z);
}

// Set movement destination using floating-point world coordinates
void game_state_set_movement_destination_world(GameState *state, float world_x, float world_y, float world_z)
{
    if (!state)
        return;

    // Cancel any existing movement and start fresh
    state->movement_destination.has_destination = true;
    state->movement_destination.is_animating = true; // Start animating immediately

    // Set floating-point destination
    state->movement_destination.target_world_x = world_x;
    state->movement_destination.target_world_y = world_y;
    state->movement_destination.target_world_z = world_z;

    // Set legacy integer destination for compatibility
    state->movement_destination.target_x = (int)floorf(world_x);
    state->movement_destination.target_y = (int)floorf(world_y);
    state->movement_destination.target_z = (int)floorf(world_z);

    // Store the starting position for smooth animation
    state->movement_destination.start_world_x = state->player_world_x;
    state->movement_destination.start_world_y = state->player_world_y;
    state->movement_destination.start_world_z = state->player_world_z;

    // Legacy integer starting positions
    state->movement_destination.start_x = state->player_x;
    state->movement_destination.start_y = state->player_y;
    state->movement_destination.start_z = state->player_z;

    // Calculate movement time based on distance (makes longer movements take more time)
    float dx = world_x - state->player_world_x;
    float dy = world_y - state->player_world_y;
    float dz = world_z - state->player_world_z;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);

    // Calculate movement time: 300ms per tile, minimum 200ms, maximum 2000ms
    Uint32 movement_time = (Uint32)(distance * 300.0f);
    if (movement_time < 200) movement_time = 200;
    if (movement_time > 2000) movement_time = 2000;

    // Store the calculated movement time for this specific movement
    state->movement_destination.movement_speed_ms = movement_time;

    // Reset animation progress
    state->movement_destination.path_progress = 0.0f;
    state->movement_destination.animation_progress = 0.0f;
    state->movement_destination.animation_start_time = SDL_GetTicks();

    printf("MOVEMENT: New floating-point destination set to (%.2f,%.2f,%.2f) from (%.2f,%.2f,%.2f), distance=%.2f, time=%ums\n",
           world_x, world_y, world_z,
           state->player_world_x, state->player_world_y, state->player_world_z,
           distance, movement_time);
}

void game_state_show_chapter(GameState *state)
{
    if (!state)
        return;

    storyline_start_chapter(&state->storyline, "00000-genesis", state->player_name);
    state->current_screen = GAME_SCREEN_CHAPTER;
    state->show_chapter = true;
    state->show_scene = false;
    state->chapter_current_page = 0;
    state->chapter_current_char = 0;
    state->chapter_total_chars = 0;
    state->chapter_text_complete = false;
    state->chapter_fade_alpha = 0.0f;
    state->chapter_fade_start_time = SDL_GetTicks();
    state->chapter_last_text_update = SDL_GetTicks();
}

void game_state_advance_chapter(GameState *state)
{
    if (!state)
        return;

    if (!state->chapter_text_complete)
    {
        state->chapter_current_char = state->chapter_total_chars;
        state->chapter_text_complete = true;
        return;
    }

    if (storyline_chapter_has_more_pages(&state->storyline))
    {
        storyline_advance_chapter_page(&state->storyline);
        state->chapter_current_page++;
        state->chapter_current_char = 0;
        state->chapter_total_chars = 0;
        state->chapter_text_complete = false;
        state->chapter_last_text_update = SDL_GetTicks();
        return;
    }

    // Chapter intro complete — begin the first scene dialogue.
    state->show_chapter = false;
    if (state->storyline.scene_count > 0 && state->storyline.scene_ids[0])
    {
        if (storyline_start_scene(&state->storyline, state->storyline.scene_ids[0]))
        {
            state->current_screen = GAME_SCREEN_SCENE;
            state->show_scene = true;
            storyline_scene_prepare_stream(&state->storyline);
            state->chapter_fade_alpha = 1.0f;
            state->chapter_fade_start_time = SDL_GetTicks();
            state->chapter_last_text_update = SDL_GetTicks();
            return;
        }
    }

    state->current_screen = GAME_SCREEN_WORLD;
    if (!state->player)
    {
        const char *spirit_name = state->player_name[0] ? state->player_name : "Spirit";
        state->player = actor_create(spirit_name, "Ancient spirit", "home");
        if (state->player)
        {
            state->player->turn_speed = 240;
            inventory_seed_spirit_starter(&state->player->inventory);
        }
    }
    state->player_flying = true;
    if (state->player)
        state->player->is_flying = true;
    state->game_started = true;
    game_state_ensure_world_clock(state);
}

bool game_state_advance_scene(GameState *state)
{
    if (!state || !state->show_scene)
        return false;

    const char *elem_type = storyline_scene_element_type(&state->storyline);

    if (elem_type && strcmp(elem_type, "text") == 0 && !storyline_scene_text_complete(&state->storyline))
    {
        storyline_scene_skip_to_end(&state->storyline);
        return true;
    }

    if (!storyline_advance_scene(&state->storyline))
    {
        state->show_scene = false;
        state->current_screen = GAME_SCREEN_WORLD;
        state->game_started = true;
        game_state_ensure_world_clock(state);
        return false;
    }

    storyline_scene_prepare_stream(&state->storyline);
    state->chapter_last_text_update = SDL_GetTicks();
    return true;
}

void game_state_tick_dialogue(GameState *state)
{
    if (!state || !dialogue_active(&state->dialogue))
        return;

    dialogue_tick(&state->dialogue, SDL_GetTicks());

    // speaker_id 0 is a system channel (Town Portal), not a living actor.
    const uint32_t id = dialogue_speaker_id(&state->dialogue);
    if (id == 0)
        return;

    World *world = state->current_world ? state->current_world : state->main_menu_world;
    Actor *speaker = world_find_runtime_actor(world, id);

    if (!speaker || !speaker->is_active || speaker->health == 0 || speaker->is_controlled)
    {
        dialogue_close(&state->dialogue);
        return;
    }

    if (state->dialogue.notified_anim_line != state->dialogue.current_line)
    {
        state->dialogue.notified_anim_line = state->dialogue.current_line;
        mob_actor_notify_talk(speaker, state->dialogue.current_line);
    }

    const float dx = (float)speaker->x - state->player_world_x;
    const float dy = (float)speaker->y - state->player_world_y;
    const float dz = (float)speaker->z - state->player_world_z;
    const float limit = PLAYER_TALK_LEAVE_RANGE;
    if (dx * dx + dy * dy + dz * dz > limit * limit)
        dialogue_close(&state->dialogue);
}

void game_state_social_adjust_civ(GameState *state, uint32_t civ_id, int delta)
{
    if (!state || !civ_id || delta == 0)
        return;
    PlayerSocialMemory *s = &state->social;
    for (uint8_t i = 0; i < s->civ_count; i++)
    {
        if (s->civ_ids[i] == civ_id)
        {
            int v = (int)s->civ_scores[i] + delta;
            if (v > 100)
                v = 100;
            if (v < -100)
                v = -100;
            s->civ_scores[i] = (int8_t)v;
            return;
        }
    }
    if (s->civ_count >= PLAYER_CIV_REP_MAX)
        return;
    s->civ_ids[s->civ_count] = civ_id;
    int v = delta;
    if (v > 100)
        v = 100;
    if (v < -100)
        v = -100;
    s->civ_scores[s->civ_count] = (int8_t)v;
    s->civ_count++;
}

void game_state_social_adjust_family(GameState *state, uint32_t family_id, int delta)
{
    if (!state || !family_id || delta == 0)
        return;
    PlayerSocialMemory *s = &state->social;
    for (uint8_t i = 0; i < s->family_count; i++)
    {
        if (s->family_ids[i] == family_id)
        {
            int v = (int)s->family_scores[i] + delta;
            if (v > 100)
                v = 100;
            if (v < -100)
                v = -100;
            s->family_scores[i] = (int8_t)v;
            return;
        }
    }
    if (s->family_count >= PLAYER_FAMILY_REP_MAX)
        return;
    s->family_ids[s->family_count] = family_id;
    int v = delta;
    if (v > 100)
        v = 100;
    if (v < -100)
        v = -100;
    s->family_scores[s->family_count] = (int8_t)v;
    s->family_count++;
}

int8_t game_state_social_civ_score(const GameState *state, uint32_t civ_id)
{
    if (!state || !civ_id)
        return 0;
    for (uint8_t i = 0; i < state->social.civ_count; i++)
        if (state->social.civ_ids[i] == civ_id)
            return state->social.civ_scores[i];
    return 0;
}

int8_t game_state_social_family_score(const GameState *state, uint32_t family_id)
{
    if (!state || !family_id)
        return 0;
    for (uint8_t i = 0; i < state->social.family_count; i++)
        if (state->social.family_ids[i] == family_id)
            return state->social.family_scores[i];
    return 0;
}

void game_state_refresh_adventure_hooks(GameState *state)
{
    if (!state)
        return;
    state->adventure_hook_count = 0;
    state->adventure_hook_selected = 0;
    if (!state->universe.chronicle_ready)
        return;
    state->adventure_hook_count = adventure_hooks_generate(
        &state->universe.chronicle, (int)(int64_t)state->player_universe_x,
        (int)(int64_t)state->player_universe_y, 24, state->adventure_hooks, ADVENTURE_HOOK_MAX);
}

void game_state_open_legends(GameState *state)
{
    if (!state)
        return;
    state->legends_scroll = 0;
    state->current_screen = GAME_SCREEN_LEGENDS;
}

bool game_state_open_shop(GameState *state, Actor *merchant)
{
    if (!state || !merchant || !state->current_world)
        return false;
    if (!shop_session_open(&state->shop, state->current_world, merchant))
        return false;
    dialogue_close(&state->dialogue);
    state->current_screen = GAME_SCREEN_SHOP;
    return true;
}

void game_state_close_shop(GameState *state)
{
    if (!state)
        return;
    shop_session_clear(&state->shop);
    if (state->current_screen == GAME_SCREEN_SHOP)
        state->current_screen = GAME_SCREEN_WORLD;
}

bool game_state_open_craft(GameState *state, CraftStation station, bool book_mode)
{
    if (!state || !state->game_started || !state->player)
        return false;
    craft_session_open(&state->craft, station, book_mode);
    state->current_screen = GAME_SCREEN_CRAFT;
    if (book_mode)
        snprintf(state->status_message, sizeof(state->status_message), "Recipe book");
    else
        snprintf(state->status_message, sizeof(state->status_message), "%s",
                 craft_station_name(station));
    return true;
}

void game_state_close_craft(GameState *state)
{
    if (!state)
        return;
    craft_session_clear(&state->craft);
    if (state->current_screen == GAME_SCREEN_CRAFT)
        state->current_screen = GAME_SCREEN_WORLD;
}

void game_state_notify_item_sold(GameState *state, ItemId item_id)
{
    if (!state || item_id == ITEM_NONE)
        return;
    if (item_id == ITEM_WOOL && game_state_quest_id_is(state, "00006-sell-wool"))
    {
        storyline_update_quest_progress(&state->storyline, "sell", "wool", 1);
        if (!storyline_has_active_quest(&state->storyline))
        {
            nav_aide_clear(&state->nav_aide);
            game_state_show_toast(state, "Wool sold — the hunter's errand is complete", 4500);
        }
    }
}

void game_state_game_log_push(GameState *state, GameLogChannel channel, const char *text)
{
    if (!state || !text)
        return;
    game_log_push(&state->game_log, channel, text);
}

void game_state_log_site_majors(GameState *state, int gx, int gy, int max_lines)
{
    if (!state || !state->universe.chronicle_ready || max_lines <= 0)
        return;
    const Chronicle *c = &state->universe.chronicle;
    const ChronicleSite *site = chronicle_site_at(c, gx, gy);
    if (!site)
        return;

    char intro[GAME_LOG_LINE_MAX];
    char label[64];
    if (chronicle_site_label(c, gx, gy, label, sizeof(label)))
        snprintf(intro, sizeof(intro), "Legends of %s:", label);
    else
        snprintf(intro, sizeof(intro), "Local legends:");
    game_log_push(&state->game_log, GAME_LOG_CHRONICLE, intro);

    const ChronicleEvent *evs[8];
    int n = chronicle_query_by_site_recent(c, site->id, evs, 8);
    int posted = 0;
    char buf[CHRONICLE_LINE_MAX];
    for (int i = 0; i < n && posted < max_lines; i++)
    {
        if (!game_log_channel_is_major_chronicle((int)evs[i]->type))
            continue;
        chronicle_format_line(c, evs[i], buf, sizeof(buf));
        if (!buf[0])
            continue;
        game_log_push(&state->game_log, GAME_LOG_CHRONICLE, buf);
        posted++;
    }
}

void game_state_apply_dialogue_choice(GameState *state)
{
    if (!state || !dialogue_awaiting_choice(&state->dialogue))
        return;

    World *world = state->current_world;
    Actor *speaker = world_find_runtime_actor(world, dialogue_speaker_id(&state->dialogue));
    const MobActor *vm =
        speaker && speaker->extra_data ? (const MobActor *)speaker->extra_data : NULL;
    uint32_t family_id = vm ? vm->family_id : 0;
    uint32_t civ_id = (world && world->settlement_roster.active) ? world->settlement_roster.civ_id : 0;

    dialogue_choice_confirm(&state->dialogue);
    DialogueEffect effect = dialogue_last_effect(&state->dialogue);

    switch (effect)
    {
    case DIALOGUE_EFFECT_REPUTATION:
        game_state_social_adjust_family(state, family_id, 5);
        game_state_social_adjust_civ(state, civ_id, 2);
        game_state_show_toast(state, "They regard you more warmly.", 2500);
        break;
    case DIALOGUE_EFFECT_NAV_FAMILY:
        if (vm && world)
        {
            uint32_t ids[8];
            int n = household_members(world, family_id, ids, 8);
            for (int i = 0; i < n; i++)
            {
                Actor *kin = world_find_runtime_actor(world, ids[i]);
                if (kin && kin != speaker)
                {
                    game_state_aim_nav_at(state, world->universe_x, world->universe_y,
                                          world->universe_z, (float)kin->x, (float)kin->y,
                                          (float)kin->z, kin->name);
                    game_state_show_toast(state, "Nav set to kin", 2500);
                    break;
                }
            }
            game_state_social_adjust_family(state, family_id, 2);
        }
        break;
    case DIALOGUE_EFFECT_NAV_SETTLEMENT:
        if (world)
        {
            int wp = game_state_discover_settlement_waypoint(state, (int)world->universe_x,
                                                            (int)world->universe_y);
            if (wp >= 0)
                game_state_select_waypoint(state, wp);
            game_state_social_adjust_civ(state, civ_id, 1);
        }
        break;
    case DIALOGUE_EFFECT_OPEN_LEGENDS:
        if (world)
            game_state_log_site_majors(state, (int)world->universe_x, (int)world->universe_y, 4);
        game_state_open_legends(state);
        break;
    case DIALOGUE_EFFECT_OPEN_SHOP:
        if (speaker && game_state_open_shop(state, speaker))
            game_state_show_toast(state, "Welcome to the shop.", 2000);
        else
            game_state_show_toast(state, "The shop is closed.", 2000);
        break;
    default:
        break;
    }
}
