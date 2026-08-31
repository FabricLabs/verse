#ifndef VERSE_GAME_STATE_H
#define VERSE_GAME_STATE_H

#include <stdbool.h>
#include <SDL.h>
#include "world.h"
#include "actor.h"
#include "item.h"
#include "engine.h"
#include "universe.h"
#include "player_controls_types.h"
#include "projectile.h"
#include "debris.h"
#include "voxel_debris_volume.h"
#include "isometric_renderer.h"
#include "storyline.h"
#include "dialogue.h"
#include "fog_of_war.h"
#include "character.h"
#include "nav_aide.h"
#include "adventure_hooks.h"
#include "game_log.h"
#include "currency.h"
#include "shop.h"
#include "craft.h"

// How many worlds may be generating at once while streaming in around the player.
//
// The streamer used to run strictly one, which meant filling the neighbourhood end to end no matter
// how many cores were idle. Several at once is the whole point of having a worker pool.
//
// The cap is a memory budget as much as a parallelism budget: a Voxel is 48 bytes, so a 128^3 world
// is 96MB, and an in-flight job holds a fully allocated one that is not yet reachable from the
// universe. Six is ~576MB of worlds in progress on top of the resident radius-2 cluster. Raising it
// further buys less than it looks like it should once writes are bandwidth-bound
// (./world-gen-startup-profile).
#define WORLD_STREAM_MAX_IN_FLIGHT 6

// Game screen states
typedef enum
{
    GAME_SCREEN_MAIN_MENU = 0,
    GAME_SCREEN_WORLD = 1,
    GAME_SCREEN_BATTLE = 2,
    GAME_SCREEN_CHARACTER = 3,
    GAME_SCREEN_INVENTORY = 4,
    GAME_SCREEN_IN_GAME_MENU = 5,
    GAME_SCREEN_SETTINGS = 6,
    GAME_SCREEN_CHAPTER = 7,
    GAME_SCREEN_SCENE = 8,
    GAME_SCREEN_LOADING = 9,
    GAME_SCREEN_SAVE_BROWSER = 10,
    GAME_SCREEN_QUEST_JOURNAL = 11,
    GAME_SCREEN_MAP = 12,
    GAME_SCREEN_LEGENDS = 13,
    GAME_SCREEN_LOOT = 14,
    GAME_SCREEN_SHOP = 15,
    GAME_SCREEN_CRAFT = 16,
    // Song editor screen removed - now standalone program
} GameScreen;

#define PLAYER_CIV_REP_MAX 12
#define PLAYER_FAMILY_REP_MAX 16

typedef struct {
    uint32_t civ_ids[PLAYER_CIV_REP_MAX];
    int8_t civ_scores[PLAYER_CIV_REP_MAX];
    uint8_t civ_count;
    uint32_t family_ids[PLAYER_FAMILY_REP_MAX];
    int8_t family_scores[PLAYER_FAMILY_REP_MAX];
    uint8_t family_count;
} PlayerSocialMemory;

// How many save rows the browser paints at once. The 256×240 menu cannot show more than this
// without overlapping the Load/Delete/Back row, so the rest are reached by scrolling.
#define SAVE_BROWSER_VISIBLE_ROWS 6

// Game state
typedef struct GameState
{
    // Core engine
    Engine *engine;

    // Current game state
    bool game_started;
    GameScreen current_screen;

    // Player state
    Actor *player;

    // Legacy integer positions (kept for compatibility and voxel-based logic)
    int player_x, player_y, player_z;

    // New floating-point position system for smooth movement
    float player_world_x, player_world_y, player_world_z;  // Relative to world origin (0,0,0)

    // Voxel index of the tile the player's model is currently located at
    int player_voxel_x, player_voxel_y, player_voxel_z;

    char player_name[64];

    // World state
    World *current_world;
    World *main_menu_world;
    GameWorlds *game_worlds;
    Universe universe;

    // The radius-2 (5x5x5) cluster around the player, presented as one queryable space. Holds
    // borrowed pointers into the universe above, so it must be resynced whenever those pointers
    // change.
    struct ShadowWorld *shadow_world;

    // Player-explored voxels (sparse per-world chunk bitfields). Runtime-only.
    FogAtlas *fog;

    // Cells being streamed in around the player. Generation runs on the task pool, so a frame is
    // never spent generating; the cap is what keeps the work bounded, since the alternative to one
    // at a time is not "all of them".
    struct CellGenJob *cell_refill_jobs[WORLD_STREAM_MAX_IN_FLIGHT];

    // UI state
    bool show_exit_prompt;
    bool show_new_game_warning;
    bool show_loading;
    bool show_help_modal;
    bool show_tutorial_modal;
    bool show_world_editor_modal;
    bool show_name_input;
    bool show_chapter;
    bool show_scene;

    // Loading screen state
    int loading_progress;
    int loading_total;
    char loading_message[128];

    // Async world generation state. The generation itself runs on a worker thread; this
    // side only holds the handle to it and the progress last copied out for the loading bar.
    bool world_generation_active;
    int world_generation_step;
    Uint32 last_world_gen_time;
    struct WorldGenJob *world_generation_job;

    // Player position in universe grid (which world cell the spirit occupies)
    uint64_t player_universe_x;
    uint64_t player_universe_y;
    uint64_t player_universe_z;

    // Stamina ring HUD fade (1 = visible, 0 = hidden)
    float stamina_meter_alpha;

    // World simulation, stepped on worker threads at a fixed rate
    Uint32 last_physics_tick_ms;
    Uint32 last_residency_tick_ms; // streaming and eviction, on their own slower clock
    int last_physics_world_count;

  // Runtime clock (UI/epoch system)
  bool runtime_clock_running;
  uint64_t runtime_clock_ms;
  uint32_t runtime_epoch_index; // increments every 10 minutes of runtime
  // Simulation speed relative to wall clock (1.0 = realtime). [ slows, ] speeds.
  float time_scale;

    // Chapter/Story state
    int chapter_current_page;
    int chapter_current_char;
    int chapter_total_chars;
    bool chapter_text_complete;
    float chapter_fade_alpha;
    Uint32 chapter_fade_start_time;
    Uint32 chapter_last_text_update;

    // Storyline (chapters, scenes, quests)
    StorylineState storyline;

    // Reusable navigational aide (arrow toward an objective).
    NavAide nav_aide;
    // When false, the aide keeps its target but the HUD chevron is hidden (journal Track toggle).
    bool mission_tracked;

    // Hunter's-camp storyline chain (wilderness home-drop).
    bool hunters_shack_quest_started;
    uint32_t quest_sheep_actor_id;
    bool quest_sheep_loot_dropped;
    // Follow-up: navigate to nearest size-5 settlement (locked at quest start).
    bool quest_town_has_target;
    int quest_town_gx;
    int quest_town_gy;

#define GAME_WAYPOINT_MAX 48

    // Discovered settlement waypoints (universe map). Click one to aim the nav aide.
    struct
    {
        int gx, gy; // wilderness plane cells (z=0)
        int scale;
        char label[40];
    } waypoints[GAME_WAYPOINT_MAX];
    int waypoint_count;
    int selected_waypoint; // index into waypoints, or -1

    // Movement state
    struct
    {
        bool has_destination;

        // Floating-point destination coordinates (world-relative)
        float target_world_x, target_world_y, target_world_z;

        // Legacy integer targets (kept for compatibility)
        int target_x, target_y, target_z;

        float path_progress;

        // Animation state for smooth movement
        bool is_animating;
        float animation_progress; // 0.0 to 1.0 for current step

        // Floating-point animation positions
        float start_world_x, start_world_y, start_world_z;

        // Legacy integer animation positions (kept for compatibility)
        int start_x, start_y, start_z; // Starting position for current animation step
        int next_x, next_y, next_z;    // Next discrete position

        Uint32 animation_start_time;   // Time when current step started
        Uint32 movement_speed_ms;      // Milliseconds per movement (now for smooth floating-point movement)
    } movement_destination;

    // Status messages
    char status_message[256];

    // Corpse loot modal (GAME_SCREEN_LOOT). loot_corpse is a borrowed runtime actor.
    Actor *loot_corpse;
    ItemStack loot_held;
    int8_t loot_held_src;   // 0 none, 1 corpse bag, 2 corpse equip, 3 player bag
    int16_t loot_held_index; // bag slot or EquipmentSlot

    // Player purse (copper / silver / gold). CharacterSave.gold stores total copper.
    Wallet purse;

    // Shop modal (GAME_SCREEN_SHOP) opened from shopkeeper dialogue.
    ShopSession shop;

    // Crafting / recipe-book modal (GAME_SCREEN_CRAFT).
    CraftSession craft;

    // Ephemeral toast (top-left fade/slide)
    char toast_text[128];
    Uint32 toast_start_ms;
    Uint32 toast_duration_ms; // e.g., 1000ms
    float toast_y_offset;     // animated upward offset in pixels
    bool toast_active;

    // Flying / gravity state (duplicated here to not depend on Actor allocation)
    bool player_flying;

    // First time flag
    bool first_time;

    // Action RPG controls (mouse facing, momentum movement, swing attacks)
    PlayerControls controls;

    // Everything the player has in flight. Owned here rather than per world so a projectile is not
    // destroyed by the world it was fired in being evicted mid-flight.
    ProjectileSystem projectiles;

    // Chunks of destroyed voxels waiting to be picked up. Same ownership story as projectiles.
    DebrisSystem debris;

    // Pass-2 falling islands extracted from the dense World (Teardown-style volumes).
    DebrisVolumeSystem debris_volumes;

    // Actor id the spirit currently inhabits. Zero means the player is a free spirit.
    uint32_t dominated_actor_id;

    // Live conversation with a nearby mob or NPC, overlaid on the world view.
    DialogueState dialogue;

    // Session reputation with civilizations and families (Phase 4).
    PlayerSocialMemory social;

    // Cached adventure hooks near the player (Phase 5).
    AdventureHook adventure_hooks[ADVENTURE_HOOK_MAX];
    int adventure_hook_count;
    int adventure_hook_selected;

    // Legends journal scroll (0 = most recent at top).
    int legends_scroll;

    // On-screen game log + Global chat.
    GameLog game_log;

    // Save file browser. Filled when the screen is opened, so a paint does not scan the directory.
    CharacterSaveEntry save_browser_entries[CHARACTER_SAVE_MAX_LIST];
    int save_browser_count;
    int save_browser_selected;
    int save_browser_scroll;
    bool save_browser_confirm_delete;

    // Mission journal vertical scroll in pixels (clamped while rendering).
    int quest_journal_scroll;

} GameState;

// Game state management
GameState *game_state_create(void);
void game_state_destroy(GameState *state);

// Game initialization
bool game_state_init(GameState *state);
bool game_state_start_new_game(GameState *state);
bool game_state_load_game(GameState *state, const char *character_name);
bool game_state_load_from_path(GameState *state, const char *path);
bool game_state_save_game(GameState *state);

// Save file browser. Open scans characters/; move/load/delete operate on the selected row.
void game_state_open_save_browser(GameState *state);
void game_state_save_browser_move(GameState *state, int delta);
bool game_state_save_browser_load(GameState *state);
bool game_state_save_browser_delete(GameState *state);

// Save/load utilities
bool game_state_has_saved_games(void);
bool game_state_get_most_recent_save(char* save_name, size_t save_name_size);
bool game_state_load_most_recent_save(GameState *state);

// Async world generation
void game_state_start_async_world_generation(GameState *state);
bool game_state_update_world_generation(GameState *state);

// Game logic
void game_state_update(GameState *state, double delta_time);
void game_state_handle_input(GameState *state, int key);
void game_state_handle_mouse(GameState *state, int x, int y, int button);
void game_state_handle_mouse_motion(GameState *state, int mouse_x, int mouse_y);
void game_state_poll_movement_keys(GameState *state);

// World management
World *game_state_create_game_world(GameState *state);
bool game_state_find_safe_spawn(GameState *state, int *x, int *y, int *z);
void game_state_set_player_position(GameState *state, int x, int y, int z);
bool game_state_place_player_at_surface_spawn(GameState *state);
// Integrates the player's vertical motion for one frame, up as well as down. delta_time is seconds.
void game_state_apply_gravity(GameState *state, float delta_time);

// The current world's gravity in voxels/second^2, which is the unit player positions are in.
// Same value world_step_actors and projectiles use, so everything in the world falls together.
float game_state_player_gravity(const GameState *state);

// Upward speed a jump starts with, in voxels/second — gravity-derived baseline only.
//
// Derived from the world's gravity rather than fixed, so a jump clears the same height in voxels
// wherever the player is. A constant velocity would clear a comfortable step in one world and not
// get off the ground in another. Body strength scales the takeoff impulse separately
// (player_controls_jump_launch_speed); it does not change this gravity relationship.
float game_state_player_jump_velocity(const GameState *state);

// True when something directly beneath the player is holding them up. The single answer both the
// jump and the fall consult, so they cannot disagree about whether the player is standing.
bool game_state_player_is_grounded(const GameState *state);
// Steps fluids and actors for the current world and its loaded neighbours, on worker threads.
// Rate-limited internally, so it is safe to call every frame.
void game_state_step_world_physics(GameState *state, double delta_time);

// Advance everything in flight and resolve what it hits: damage to actors and to solid voxels
// (with debris drops), and foliage burned out of the way. Called from game_state_update; exposed
 // so a test can drive it without a frame loop.
void game_state_step_projectiles(GameState *state, double delta_time);

// Integrate debris physics and collect pieces near the player into inventory.
void game_state_step_debris(GameState *state, double delta_time);

// Award experience to the spirit always; while inhabiting a mob, that body also
// receives the full amount (separate totals). reason is optional status text.
void game_state_award_experience(GameState *state, uint32_t amount, const char *reason);

// XP granted when the player (or inhabited body) finishes off a mob / shatters a block.
#define XP_BLOCK_DESTROY   5u
#define XP_FOLIAGE_DESTROY 1u
#define XP_MOB_KILL_BASE   25u

// Player movement
bool game_state_can_move_to(GameState *state, int x, int y, int z);

// Move the player into the neighbouring world if walking has carried them out of the one they were
// in, rebasing their position into the new world's coordinates. Call it after horizontal movement and
// before anything reads the player's column, since a body outside every world has nothing holding it
// up. Returns true when a crossing happened. Collision already resolves across the seam, so this is
// bookkeeping catching up with a move that was legal rather than a check that can refuse one.
bool game_state_cross_world_boundary(GameState *state);
void game_state_move_player(GameState *state, int dx, int dy, int dz);

// Legacy movement destination (integer-based)
void game_state_set_movement_destination(GameState *state, int x, int y, int z);

// New floating-point movement destination
void game_state_set_movement_destination_world(GameState *state, float world_x, float world_y, float world_z);

void game_state_update_movement(GameState *state);
void game_state_get_animated_player_position(GameState *state, float *x, float *y, float *z);

// Sync isometric renderer camera and avatar state from game state
void game_state_sync_isometric_renderer(GameState *state, IsometricRenderer *renderer);

// Point the shadow world cluster at the 3x3x3 of universe cells around the player, creating it on
// first call. Cheap to call every frame: slots whose world has not changed keep their pyramid.
//
// Must not run while worlds are being mutated on worker threads. See the phase ordering in
// game_state_step_world_physics.
void game_state_sync_shadow_world(GameState *state);

// Collects finished background world generations and starts new ones for the nearest missing cells.
// Non-blocking, and capped at WORLD_STREAM_MAX_IN_FLIGHT outstanding, so holes in the cluster close
// over the next few frames using the idle cores rather than stalling a frame or queueing without
// bound. Call it once per frame.
void game_state_pump_world_streaming(GameState *state);

// How many background world generations are outstanding. For tests and diagnostics.
int game_state_stream_jobs_in_flight(const GameState *state);

// Frees worlds outside the retention region, saving first only if the player changed them.
// Returns how many were freed.
int game_state_evict_distant_worlds(GameState *state);

// Coordinate conversion utilities
void game_state_update_voxel_index(GameState *state);
void game_state_sync_positions(GameState *state);  // Sync between integer and float positions
bool game_state_can_move_to_world(GameState *state, float world_x, float world_y, float world_z);

// 0 = calm / ambient, 1 = full combat. Driven by attacks and nearby player-aggro'd mobs.
float game_state_music_intensity(const GameState *state);

// Screen transitions
void game_state_set_screen(GameState *state, GameScreen screen);
void game_state_show_chapter(GameState *state);
void game_state_advance_chapter(GameState *state);
bool game_state_advance_scene(GameState *state);
// Typewriter + walk-away close for in-world mob/NPC talk. Safe to call when idle.
void game_state_tick_dialogue(GameState *state);
// Confirm the highlighted dialogue choice and apply side effects (reputation, legends, shop, nav).
void game_state_apply_dialogue_choice(GameState *state);
void game_state_social_adjust_civ(GameState *state, uint32_t civ_id, int delta);
void game_state_social_adjust_family(GameState *state, uint32_t family_id, int delta);
int8_t game_state_social_civ_score(const GameState *state, uint32_t civ_id);
int8_t game_state_social_family_score(const GameState *state, uint32_t family_id);
void game_state_refresh_adventure_hooks(GameState *state);
void game_state_open_legends(GameState *state);
// Open the settlement shop UI for a shopkeeper actor (from dialogue).
bool game_state_open_shop(GameState *state, Actor *merchant);
void game_state_close_shop(GameState *state);
bool game_state_open_craft(GameState *state, CraftStation station, bool book_mode);
void game_state_close_craft(GameState *state);
// Called after a successful shop sale so storyline sell objectives can advance.
void game_state_notify_item_sold(GameState *state, ItemId item_id);
// Announce major site legends into the on-screen game log (newest first).
void game_state_log_site_majors(GameState *state, int gx, int gy, int max_lines);
void game_state_game_log_push(GameState *state, GameLogChannel channel, const char *text);

// Return the player's spirit to the home world at (0,0,UNIVERSE_HOME_Z). Releases dominate first
// so an inhabited mob stays in place under AI. False when there is no home world to return to.
bool game_state_teleport_home(GameState *state);

// Teleport to the nearest wilderness settlement of exact `scale` (1..9). Uses the player's current
// cell if it already matches. Loads the target cell if needed. False when none are in range.
bool game_state_teleport_to_settlement(GameState *state, int scale);

// Discover a settlement waypoint for the wilderness cell (gx,gy). Idempotent.
// Returns the waypoint index, or -1 if none.
int game_state_discover_settlement_waypoint(GameState *state, int gx, int gy);
// If the player stands in a settled wilderness cell, discover it.
void game_state_try_discover_current_settlement(GameState *state);
// Aim the navigational aide at a discovered waypoint. Sets mission_tracked.
bool game_state_select_waypoint(GameState *state, int index);
int game_state_waypoint_count(const GameState *state);
int game_state_selected_waypoint(const GameState *state);
const char *game_state_waypoint_label(const GameState *state, int index);
bool game_state_waypoint_cell(const GameState *state, int index, int *out_gx, int *out_gy,
                              int *out_scale);

// UI state management
void game_state_show_modal(GameState *state, const char *modal_type);
void game_state_hide_modal(GameState *state, const char *modal_type);

#endif // VERSE_GAME_STATE_H
