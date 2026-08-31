// world_editor.c
// GPU-accelerated world editor based on world_viewer, using isometric_renderer GPU path

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include "world.h"
#include "world_bulk_ops.h"
#include "model_transformer.h"
#include "constants.h"
#include "isometric_renderer.h"
#include "cpu_renderer_optimized.h"
#include "fp_renderer.h"
#include "universe.h"
#include "gpu_voxel_buffer.h"
#include "gl_occupancy.h"
#include "fluid_navier_stokes.h"
#include "gpu_physics.h"
#include "world_editor_ui.h"
#include "world_editor_input.h"

#define WINDOW_W 1024
#define WINDOW_H 768

// Global debug flag
static bool g_debug = false;

// Forward declaration for GameWorlds wrapper
static GameWorlds g_fp_gameworlds;

// Strong actor symbols for renderer overlay (renderer declares weak imports)
#include "actor.h"
Actor g_actors[256];
int g_actor_count = 0;
static int g_controlled_actor_index = -1; // -1 = none selected

static void draw_text(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y, SDL_Color color)
{
  if (!font || !text)
    return;
  SDL_Surface *surface = TTF_RenderText_Solid(font, text, color);
  if (!surface)
    return;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surface);
  if (tex)
  {
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
  }
  SDL_FreeSurface(surface);
}

// Forward declarations
static void draw_actor_panel(SDL_Renderer *ren, TTF_Font *font, int x, int y, int width);

// NEW: File operation callback for unified UI system
static void on_file_operation(int operation_id, const char *filename, void *user_data);

// New world configuration functions
static void set_new_world_config(WorldGenerationType world_type, const char *seed, bool generate_neighbors);
static void clear_new_world_config(void);

// Public function for UI to set new world configuration
void world_editor_set_new_world_config(WorldGenerationType world_type, const char *seed, bool generate_neighbors);

// Tool change callback for toolbar
static void on_tool_change(int tool_id, void *user_data);
static void on_actor_move(int dx, int dy, int dz, void *user_data);

// File browser callback forward declarations
static void on_file_browser_file_selected(const char *filepath, void *user_data);
static void on_file_browser_directory_changed(const char *new_path, void *user_data);

// File browsing function forward declarations
static void world_editor_populate_file_browser(const char *path);
static void world_editor_file_browser_go_up(void);
static void world_editor_file_browser_go_home(void);
static void world_editor_file_browser_refresh(void);
static void on_file_browser_refresh_click(int button_id, void *user_data);
static void on_file_browser_up_click(int button_id, void *user_data);
static void on_file_browser_home_click(int button_id, void *user_data);

// Input system callback forward declarations
static void world_editor_insert_actor(void *user_data);
static void world_editor_toggle_mouse_look(bool enabled, void *user_data);
static void world_editor_time_control(int direction, void *user_data);
static void world_editor_zoom_control(float factor, void *user_data);
static void world_editor_camera_move(int dx, int dy, int dz, void *user_data);
static void world_editor_camera_rotate(int direction, void *user_data);
static void world_editor_camera_reset(void *user_data);
static void world_editor_renderer_toggle(int toggle_type, void *user_data);
static void world_editor_chat_toggle(void *user_data);
static void world_editor_clock_toggle(void *user_data);

// Palette and UI state
static VoxelType g_brush_type = VOXEL_GRASS;
static SDL_Rect g_palette_rects[VOXEL_COUNT];
static int g_palette_rect_count = 0;
static SDL_Rect g_chart_rects[VOXEL_COUNT];
static int g_chart_rect_count = 0;
static bool g_palette_expanded = false;
static bool g_palette_visible = false; // palette only visible when hovering over composition chart
static char g_world_name[128] = {0};

// New world configuration (set by modal, used by file operation)
static struct
{
  WorldGenerationType world_type;
  char seed[65];
  bool configured;
  bool generate_neighbors;
} g_new_world_config = {WORLD_TYPE_HOME, "", false, false};
static bool g_palette_rarity_mode = true;
static int g_mass_highlight_type = -1; // current hovered type for visual swatch highlight
static SDL_Rect g_palette_panel_rect = {0, 0, 0, 0};

// Legacy menu variables removed - now handled by unified UI system

// NEW: Unified UI system
static WorldEditorUI *g_editor_ui = NULL;
static WorldEditorInputSystem *g_input_system = NULL;
static int g_current_z = 0;                            // Current Z layer for camera and rendering
IsometricRenderer *g_isometric_renderer = NULL; // Global reference to isometric renderer

// Cached list of .world files under "worlds/"
#define MAX_WORLD_FILES 64
static char g_world_files[MAX_WORLD_FILES][256];
static int g_world_file_count = 0;

static void refresh_world_file_list(void)
{
  g_world_file_count = 0;
  DIR *d = opendir("worlds");
  if (!d)
    return;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL)
  {
    const char *n = ent->d_name;
    size_t len = n ? strlen(n) : 0;
    if (len > 6 && strcmp(n + (len - 6), ".world") == 0)
    {
      if (g_world_file_count < MAX_WORLD_FILES)
      {
        snprintf(g_world_files[g_world_file_count], sizeof(g_world_files[0]), "worlds/%s", n);
        g_world_file_count++;
      }
    }
  }
  closedir(d);
}

// Runtime clock (editor-local)
static bool g_clock_running = false;
static uint64_t g_clock_ms = 0;          // accumulated runtime in ms
static Uint32 g_clock_last_tick = 0;     // last SDL_GetTicks sample
static uint32_t g_clock_epoch_index = 0; // increments every 10 minutes
static Uint32 g_epoch_flash_start = 0;   // tick when epoch changed
static Uint32 g_toast_until = 0;         // tick until which toast is visible
static char g_toast_buf[128] = {0};
static unsigned long long g_time_factor = BASE_TIME_FACTOR; // adjustable with , and . keys
static int g_clock_last_whole = -1;                         // last whole seconds shown
static Uint32 g_sec_flash_start = 0;                        // tick when whole seconds changed
// Physics stepping is tied to simulated T time, same clock the game uses. The tick rate and cell
// budget match game_state (20 Hz, 4096 cells): enough for water to look live, small enough that a
// 128³ world stays interactive. The previous editor path used the entire voxel volume as the
// budget (~2M cells) inside an unbounded catch-up loop; one slow step fell behind T, the loop
// tried to repay the debt with more full-volume steps, and the UI froze at ~0.1 FPS.
static unsigned long long g_physics_last_ms = 0;
static bool g_physics_init = false;

#define PHYSICS_TICK_MS 50
#define PHYSICS_FLUIDS_BUDGET_PER_STEP 4096
// Cap steps per rendered frame. Each step is budget-bounded (~0.2 ms), so a handful stays
// interactive under mild time acceleration; an unbounded catch-up loop is what froze the editor.
#define PHYSICS_MAX_STEPS_PER_FRAME 4

// Time acceleration hold state
static bool g_accel_increase_held = false; // PERIOD/>
static bool g_accel_decrease_held = false; // COMMA/<
static Uint32 g_accel_increase_last_change = 0;
static Uint32 g_accel_decrease_last_change = 0;

static inline void editor_physics_step(World *world, unsigned long long t_us)
{
  if (!world)
    return;
  world_update_springs(world, t_us);
  world_step_fluids(world, PHYSICS_FLUIDS_BUDGET_PER_STEP);
}

// Toast history (recent epoch messages)
#define TOAST_HISTORY_MAX 10
static char g_toast_history[TOAST_HISTORY_MAX][128];
static int g_toast_history_count = 0;

// Chat input state
static bool g_chat_open = false;
static char g_chat_buf[256] = {0};
static size_t g_last_counts[VOXEL_COUNT];
static bool g_have_counts = false;
// Hover state for voxel under mouse
static bool g_hover_voxel_valid = false;
static int g_hover_vx = -1, g_hover_vy = -1, g_hover_vz = -1;
static int g_hover_mouse_x = 0, g_hover_mouse_y = 0;

// Mouse look controls for first-person camera
static bool g_mouse_look_enabled = false;
static int g_last_mouse_x = 0, g_last_mouse_y = 0;
static bool g_mouse_look_initialized = false;

// Global first-person camera for mouse look access
static FPCamera g_fp_camera = {0};
// Selection state for clicked voxel (shows details panel in bottom-left)
static bool g_selected_voxel_valid = false;
static int g_selected_vx = -1, g_selected_vy = -1, g_selected_vz = -1;
static int g_selected_actor_index = -1; // if selected voxel contains an actor
// When true, keep camera centered on the selected actor
static bool g_follow_actor_camera = false;

// Persistent universe for the editor session so generated worlds are cached
static Universe g_editor_universe;
static bool g_universe_inited = false;
static char g_universe_seed[65] = {0};
static int g_home_gx = 0, g_home_gy = 0; // absolute grid coords of current home world (z=0)
static float g_universe_base_ox = 0.0f, g_universe_base_oy = 0.0f, g_universe_base_oz = 0.0f;
// Temporary stub home to allow instant camera transition before generation completes
static World *g_stub_home = NULL;
static int g_stub_gx = 0, g_stub_gy = 0;

// Persist currently displayed edge worlds into the universe under absolute grid coords
static void editor_persist_current_edges(IsometricRenderer *ir, World *home)
{
  if (!ir || !home || !g_universe_inited)
    return;
  // Ensure home is placed
  (void)universe_place(&g_editor_universe, (uint64_t)g_home_gx, (uint64_t)g_home_gy, 0, home);
  for (int i = 0; i < 125; i++)
  {
    World *w = ir->edge_worlds[i];
    if (!w)
      continue;
    WorldOffset off = ir->world_offsets[i];
    uint64_t ax = (uint64_t)((int64_t)g_home_gx + off.dx);
    uint64_t ay = (uint64_t)((int64_t)g_home_gy + off.dy);
    uint64_t az = (uint64_t)((int64_t)0 + off.dz);
    (void)universe_place(&g_editor_universe, ax, ay, az, w);
  }
}

// Get existing world at absolute universe coord, or build + place a new one
static World *editor_get_or_build_world_at(uint64_t gx, uint64_t gy, int gz, uint32_t w, uint32_t h, uint32_t d)
{
  if (!g_universe_inited)
    return NULL;
  World *exists = universe_get(&g_editor_universe, gx, gy, (uint64_t)gz);
  if (exists)
    return exists;
  World *nw = world_create(w, h, d);
  if (!nw)
    return NULL;
  // Use the universe seed for all worlds to ensure a single shared noise/entropy field
  const char *final_seed = (g_universe_seed[0] ? g_universe_seed : "editor");
  // Align noise offsets to global universe base offsets

  // Delegate type decision entirely to the universe policy
  WorldGenerationType gtype;
  VoxelType fill;
  (void)universe_wfc_decide_cell(UNIVERSE_GEN_GAMEWORLD, (int)gx, (int)gy, gz, &gtype, &fill);
  if (gtype == WORLD_TYPE_SOLID)
    world_generate_with_type_and_fill(nw, final_seed, gtype, fill);
  else
    world_generate_with_type(nw, final_seed, gtype);
  world_refresh_occupancy_bitfield(nw);
  (void)universe_place(&g_editor_universe, gx, gy, (uint64_t)gz, nw);
  return nw;
}

// Rebuild renderer edge worlds for current home absolute coords; do not free cached worlds
static void editor_rebuild_edges(IsometricRenderer *ir, World *home)
{
  if (!ir || !home || !g_universe_inited)
    return;
  for (int i = 0; i < 125; i++)
    ir->edge_worlds[i] = NULL;
  // Neighbors in a 5x5 square on z=0 plane, plus clouds above them
  const char *seed0 = (g_universe_seed[0] ? g_universe_seed : ((home && home->seed_id[0]) ? home->seed_id : "editor"));
  for (int dy = -2; dy <= 2; dy++)
  {
    for (int dx = -2; dx <= 2; dx++)
    {
      int idx = isometric_renderer_offset_index(ir, dx, dy, 0);
      if (idx > 0)
      {
        uint64_t ax = (uint64_t)((int64_t)g_home_gx + dx);
        uint64_t ay = (uint64_t)((int64_t)g_home_gy + dy);
        World *w = universe_get(&g_editor_universe, ax, ay, 0);
        if (!w)
        {
          (void)universe_generate_neighbors_wfc_around(&g_editor_universe, seed0, (uint64_t)g_home_gx, (uint64_t)g_home_gy, 0, home, UNIVERSE_GEN_GAMEWORLD, true, true);
          w = universe_get(&g_editor_universe, ax, ay, 0);
        }
        if (w)
          ir->edge_worlds[idx] = w;
      }
      int idx_up = isometric_renderer_offset_index(ir, dx, dy, 1);
      if (idx_up > 0)
      {
        uint64_t ax = (uint64_t)((int64_t)g_home_gx + dx);
        uint64_t ay = (uint64_t)((int64_t)g_home_gy + dy);
        World *wc = universe_get(&g_editor_universe, ax, ay, 1);
        if (!wc)
        {
          (void)universe_generate_neighbors_wfc_around(&g_editor_universe, seed0, (uint64_t)g_home_gx, (uint64_t)g_home_gy, 0, home, UNIVERSE_GEN_GAMEWORLD, true, true);
          wc = universe_get(&g_editor_universe, ax, ay, 1);
        }
        if (wc)
          ir->edge_worlds[idx_up] = wc;
      }
    }
  }
}

// ------------------------------------------------------------
// Background world generation worker (single-threaded queue)
// ------------------------------------------------------------

// New world generation job structure for World Editor
typedef struct WorldGenJob
{
  int used;
  WorldGenerationType world_type;
  char seed[65];
  World *target_world;
  bool generate_neighbors;
  void (*progress_callback)(float progress, const char *message, void *user_data);
  void *progress_user_data;
} WorldGenJob;

static SDL_Thread *g_world_gen_thread = NULL;
static SDL_mutex *g_world_gen_mutex = NULL;
static int g_world_gen_running = 0;
static WorldGenJob g_world_gen_job = {0};

// ------------------------------------------------------------
// Background world generation worker (single-threaded queue)
// ------------------------------------------------------------
typedef struct GenJob
{
  int used;
  uint64_t gx, gy;
  int gz;
  uint32_t w, h, d;
} GenJob;
typedef struct GenDone
{
  int used;
  uint64_t gx, gy;
  int gz;
  World *world;
} GenDone;
static SDL_Thread *g_gen_thread = NULL;
static SDL_mutex *g_gen_mutex = NULL;
static int g_gen_running = 0;
static GenJob g_jobs[64];
static GenDone g_done[64];

static int gen_worker(void *ud)
{
  (void)ud;
  while (g_gen_running)
  {
    int found = -1;
    GenJob job = (GenJob){0};
    if (g_gen_mutex)
      SDL_LockMutex(g_gen_mutex);
    for (int i = 0; i < (int)(sizeof(g_jobs) / sizeof(g_jobs[0])); i++)
    {
      if (g_jobs[i].used)
      {
        found = i;
        job = g_jobs[i];
        g_jobs[i].used = 0;
        break;
      }
    }

    if (g_gen_mutex)
      SDL_UnlockMutex(g_gen_mutex);

    if (found < 0)
    {
      SDL_Delay(8);
      continue;
    }
    World *nw = world_create(job.w, job.h, job.d);
    if (nw)
    {
      // Use only the universe seed so all worlds share the exact same noise/entropy field
      const char *final_seed = (g_universe_seed[0] ? g_universe_seed : "editor");

      // Use WFC (arena variant) to decide the world type for this universe cell
      WorldGenerationType gtype;
      VoxelType fill;
      if (universe_wfc_decide_cell(UNIVERSE_GEN_ARENA, (int)job.gx, (int)job.gy, job.gz, &gtype, &fill))
      {
        if (gtype == WORLD_TYPE_SOLID)
          world_generate_with_type_and_fill(nw, final_seed, gtype, fill);
        else
          world_generate_with_type(nw, final_seed, gtype);
      }
      else
      {
        world_generate_with_type(nw, final_seed, (job.gz > 0 ? WORLD_TYPE_CLOUD : WORLD_TYPE_WILDERNESS));
      }
      world_refresh_occupancy_bitfield(nw);

      if (g_gen_mutex)
        SDL_LockMutex(g_gen_mutex);
      for (int i = 0; i < (int)(sizeof(g_done) / sizeof(g_done[0])); i++)
      {
        if (!g_done[i].used)
        {
          g_done[i].used = 1;
          g_done[i].gx = job.gx;
          g_done[i].gy = job.gy;
          g_done[i].gz = job.gz;
          g_done[i].world = nw;
          nw = NULL;
          break;
        }
      }
      if (g_gen_mutex)
        SDL_UnlockMutex(g_gen_mutex);
      if (nw)
        world_destroy(nw);
    }
  }
  return 0;
}

// World generation worker thread for World Editor
static int world_gen_worker(void *ud)
{
  (void)ud;
  while (g_world_gen_running)
  {
    WorldGenJob job = {0};
    bool has_job = false;

    // Check for a job
    if (g_world_gen_mutex)
      SDL_LockMutex(g_world_gen_mutex);

    if (g_world_gen_job.used)
    {
      job = g_world_gen_job;
      g_world_gen_job.used = 0;
      has_job = true;
    }

    if (g_world_gen_mutex)
      SDL_UnlockMutex(g_world_gen_mutex);

    if (!has_job)
    {
      SDL_Delay(16); // 60 FPS check rate
      continue;
    }

    // Execute the world generation job
    if (job.target_world && job.progress_callback)
    {
      // Report progress start
      job.progress_callback(0.0f, "Starting world generation...", job.progress_user_data);

      // Generate the main world (this is the blocking operation we want to move to background)
      world_generate_with_type(job.target_world, job.seed, job.world_type);

      // Place the main world in the universe at origin
      universe_place(&g_editor_universe, 0, 0, 0, job.target_world);

      // Report progress for main world
      job.progress_callback(0.5f, "Main world generated, generating neighbors...", job.progress_user_data);

      // Generate neighboring worlds if requested
      if (job.generate_neighbors)
      {
        // Generate 26 adjacent worlds in a 3x3x3 grid (excluding center)
        int neighbor_count = 0;
        for (int x = -1; x <= 1; x++)
        {
          for (int y = -1; y <= 1; y++)
          {
            for (int z = -1; z <= 1; z++)
            {
              // Skip the center (0,0,0) as that's our main world
              if (x == 0 && y == 0 && z == 0)
                continue;

              // Create neighbor world
              World *neighbor = world_create(job.target_world->width, job.target_world->height, job.target_world->depth);
              if (neighbor)
              {
                // Generate neighbor seed by appending direction
                char neighbor_seed[80];
                snprintf(neighbor_seed, sizeof(neighbor_seed), "%s_%d_%d_%d", job.seed, x, y, z);

                // Generate neighbor world as wilderness
                world_generate_with_type(neighbor, neighbor_seed, WORLD_TYPE_WILDERNESS);

                // Place in universe adjacent to main world
                universe_place_adjacent(&g_editor_universe, 0, 0, 0, x, y, z, neighbor);

                neighbor_count++;

                // Update progress
                float progress = 0.5f + (0.4f * neighbor_count / 26.0f);
                char message[128];
                snprintf(message, sizeof(message), "Generated neighbor %d/26...", neighbor_count);
                job.progress_callback(progress, message, job.progress_user_data);
              }
            }
          }
        }

        printf("[world_gen_worker] Generated %d neighboring worlds\n", neighbor_count);
      }

      // Report progress complete
      job.progress_callback(1.0f, "World generation complete!", job.progress_user_data);

      printf("[world_gen_worker] World generation completed for type %d with seed %s, neighbors: %s\n",
             job.world_type, job.seed, job.generate_neighbors ? "yes" : "no");
    }
  }
  return 0;
}

static void gen_start_worker(void)
{
  if (!g_gen_mutex)
    g_gen_mutex = SDL_CreateMutex();
  if (!g_gen_running)
  {
    g_gen_running = 1;
    g_gen_thread = SDL_CreateThread(gen_worker, "worldgen-bg", NULL);
  }
}

static void gen_stop_worker(void)
{
  if (g_gen_running)
  {
    g_gen_running = 0;
    if (g_gen_thread)
    {
      SDL_WaitThread(g_gen_thread, NULL);
      g_gen_thread = NULL;
    }
  }
  if (g_gen_mutex)
  {
    SDL_DestroyMutex(g_gen_mutex);
    g_gen_mutex = NULL;
  }
}

// World generation worker thread management
static void world_gen_start_worker(void)
{
  if (!g_world_gen_mutex)
    g_world_gen_mutex = SDL_CreateMutex();
  if (!g_world_gen_running)
  {
    g_world_gen_running = 1;
    g_world_gen_thread = SDL_CreateThread(world_gen_worker, "world-gen-bg", NULL);
    printf("[world_gen] World generation worker thread started\n");
  }
}

static void world_gen_stop_worker(void)
{
  if (g_world_gen_running)
  {
    g_world_gen_running = 0;
    if (g_world_gen_thread)
    {
      SDL_WaitThread(g_world_gen_thread, NULL);
      g_world_gen_thread = NULL;
    }
    printf("[world_gen] World generation worker thread stopped\n");
  }
  if (g_world_gen_mutex)
  {
    SDL_DestroyMutex(g_world_gen_mutex);
    g_world_gen_mutex = NULL;
  }
}

// Enqueue a world generation job
static void world_gen_enqueue(WorldGenerationType world_type, const char *seed, World *target_world,
                              bool generate_neighbors,
                              void (*progress_callback)(float progress, const char *message, void *user_data),
                              void *progress_user_data)
{
  if (!g_world_gen_mutex)
    return;

  SDL_LockMutex(g_world_gen_mutex);

  if (!g_world_gen_job.used)
  {
    g_world_gen_job.used = 1;
    g_world_gen_job.world_type = world_type;
    strncpy(g_world_gen_job.seed, seed ? seed : "default", sizeof(g_world_gen_job.seed) - 1);
    g_world_gen_job.seed[sizeof(g_world_gen_job.seed) - 1] = '\0';
    g_world_gen_job.target_world = target_world;
    g_world_gen_job.generate_neighbors = generate_neighbors;
    g_world_gen_job.progress_callback = progress_callback;
    g_world_gen_job.progress_user_data = progress_user_data;

    printf("[world_gen] Enqueued world generation job: type=%d, seed=%s, neighbors=%s\n",
           world_type, g_world_gen_job.seed, generate_neighbors ? "yes" : "no");
  }
  else
  {
    printf("[world_gen] WARNING: World generation job already in progress, ignoring new request\n");
  }

  SDL_UnlockMutex(g_world_gen_mutex);
}

// Progress callback for world generation
static void world_gen_progress_callback(float progress, const char *message, void *user_data)
{
  (void)user_data; // We'll use the global editor UI reference

  if (g_editor_ui)
  {
    if (progress >= 1.0f)
    {
      // Generation complete - hide progress bar after a short delay
      world_editor_ui_hide_progress_bar(g_editor_ui);

      // Refresh the renderer with the new world
      if (g_isometric_renderer && g_fp_gameworlds.home_world)
      {
        isometric_renderer_try_load_layer_texture(g_isometric_renderer, g_fp_gameworlds.home_world);

        // Return camera to height above world for full view
        World *world = g_fp_gameworlds.home_world;
        int center_x = (int)(world->width / 2);
        int center_y = (int)(world->height / 2);
        int overview_z = (int)(world->depth); // Height of world + 1 for full view

        // Update the current Z layer and camera position
        g_current_z = overview_z;
        g_isometric_renderer->camera_z = overview_z;
        isometric_renderer_set_camera(g_isometric_renderer, center_x, center_y, overview_z);
        // Reset composition chart cache when Z layer changes
        g_have_counts = false;

        printf("[editor] Moved camera to overview height %d for full world view\n", overview_z);
      }

      // Update UI panels with new world data
      world_editor_ui_update_world_info(g_editor_ui, g_fp_gameworlds.home_world);
      world_editor_ui_update_actor_list(g_editor_ui, g_actors, g_actor_count);
    }
    else if (progress == 0.0f)
    {
      // World generation starting - center camera on the layer being generated
      if (g_isometric_renderer && g_fp_gameworlds.home_world)
      {
        World *world = g_fp_gameworlds.home_world;
        // Center camera on the middle layer of the world being generated
        int center_z = (int)(world->depth / 2);
        int center_x = (int)(world->width / 2);
        int center_y = (int)(world->height / 2);

        // Update the current Z layer and camera position
        g_current_z = center_z;
        g_isometric_renderer->camera_z = center_z;
        isometric_renderer_set_camera(g_isometric_renderer, center_x, center_y, center_z);
        // Reset composition chart cache when Z layer changes
        g_have_counts = false;

        printf("[editor] Centered camera on layer %d during world generation\n", center_z);
      }

      // Update progress
      world_editor_ui_update_progress_bar(g_editor_ui, progress, message);
    }
    else
    {
      // Update progress
      world_editor_ui_update_progress_bar(g_editor_ui, progress, message);
    }
  }
}

static void gen_enqueue(uint64_t gx, uint64_t gy, int gz, uint32_t w, uint32_t h, uint32_t d)
{
  if (!g_gen_mutex)
    return;
  SDL_LockMutex(g_gen_mutex);
  for (int i = 0; i < (int)(sizeof(g_jobs)) / (int)(sizeof(g_jobs[0])); i++)
  {
    if (g_jobs[i].used && g_jobs[i].gx == gx && g_jobs[i].gy == gy && g_jobs[i].gz == gz)
    {
      SDL_UnlockMutex(g_gen_mutex);
      return;
    }
  }
  for (int i = 0; i < (int)(sizeof(g_jobs) / sizeof(g_jobs[0])); i++)
  {
    if (!g_jobs[i].used)
    {
      g_jobs[i].used = 1;
      g_jobs[i].gx = gx;
      g_jobs[i].gy = gy;
      g_jobs[i].gz = gz;
      g_jobs[i].w = w;
      g_jobs[i].h = h;
      g_jobs[i].d = d;
      break;
    }
  }
  SDL_UnlockMutex(g_gen_mutex);
}

static void gen_integrate_completed(IsometricRenderer *ir, World **p_home)
{
  if (!g_gen_mutex || !ir || !p_home || !*p_home)
    return;
  SDL_LockMutex(g_gen_mutex);
  for (int i = 0; i < (int)(sizeof(g_done) / sizeof(g_done[0])); i++)
  {
    if (!g_done[i].used)
      continue;
    GenDone d = g_done[i];
    g_done[i].used = 0;
    SDL_UnlockMutex(g_gen_mutex);

    (void)universe_place(&g_editor_universe, d.gx, d.gy, (uint64_t)d.gz, d.world);

    int dx = (int)((int64_t)d.gx - (int64_t)g_home_gx);
    int dy = (int)((int64_t)d.gy - (int64_t)g_home_gy);
    int dz = d.gz;
    if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && (dz == 0 || dz == 1))
    {
      int idx = isometric_renderer_offset_index(ir, dx, dy, dz);
      if (idx > 0)
        ir->edge_worlds[idx] = d.world;
    }
    SDL_LockMutex(g_gen_mutex);
  }
  SDL_UnlockMutex(g_gen_mutex);
}

static void enqueue_neighbors_around_home(World *home)
{
  if (!home)
    return;
  for (int dy = -2; dy <= 2; dy++)
  {
    for (int dx = -2; dx <= 2; dx++)
    {
      if (dx == 0 && dy == 0)
        continue;
      uint64_t ax = (uint64_t)((int64_t)g_home_gx + dx);
      uint64_t ay = (uint64_t)((int64_t)g_home_gy + dy);
      gen_enqueue(ax, ay, 0, home->width, home->height, home->depth);
      gen_enqueue(ax, ay, 1, home->width, home->height, home->depth);
    }
  }
  // Also ensure cloud above center is requested
  gen_enqueue((uint64_t)g_home_gx, (uint64_t)g_home_gy, 1, home->width, home->height, home->depth);
}

// ------------------------------------------------------------
// Overlay helpers
// ------------------------------------------------------------
static void editor_draw_world_boundary(SDL_Renderer *ren, IsometricRenderer *ir, World *home, int current_z, int dx, int dy, SDL_Color color)
{
  if (!ren || !ir || !home)
    return;
  int x0, y0, x1, y1;
  int zface = current_z;
  if (zface < 0)
    zface = 0;
  if (zface >= (int)home->depth)
    zface = (int)home->depth - 1;
  // Determine boundary endpoints on the shared face in home coordinates
  if (dx == 1 && dy == 0)
  {
    // East boundary: x = width-1, y from 0..height-1
    isometric_world_to_screen(ir, (int)home->width - 1, 0, zface + 1, 0, &x0, &y0);
    isometric_world_to_screen(ir, (int)home->width - 1, (int)home->height - 1, zface + 1, 0, &x1, &y1);
  }
  else if (dx == -1 && dy == 0)
  {
    // West boundary: x = 0
    isometric_world_to_screen(ir, 0, 0, zface + 1, 0, &x0, &y0);
    isometric_world_to_screen(ir, 0, (int)home->height - 1, zface + 1, 0, &x1, &y1);
  }
  else if (dx == 0 && dy == 1)
  {
    // North boundary: y = height-1
    isometric_world_to_screen(ir, 0, (int)home->height - 1, zface + 1, 0, &x0, &y0);
    isometric_world_to_screen(ir, (int)home->width - 1, (int)home->height - 1, zface + 1, 0, &x1, &y1);
  }
  else if (dx == 0 && dy == -1)
  {
    // South boundary: y = 0
    isometric_world_to_screen(ir, 0, 0, zface + 1, 0, &x0, &y0);
    isometric_world_to_screen(ir, (int)home->width - 1, 0, zface + 1, 0, &x1, &y1);
  }
  else
  {
    return;
  }
  SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
  SDL_RenderDrawLine(ren, x0, y0, x1, y1);
}

// Tooling state
typedef enum
{
  TOOL_CURSOR = 0,
  TOOL_SHAPE = 1
} EditorTool;
typedef enum
{
  SHAPE_CUBE = 0,
  SHAPE_SPHERE = 1
} ShapeMode;
static EditorTool g_current_tool = TOOL_CURSOR;
static ShapeMode g_shape_mode = SHAPE_CUBE;
static bool g_shape_has_first = false;
static uint32_t g_shape_x1 = 0, g_shape_y1 = 0, g_shape_z1 = 0;
// Shape thickness control (for cube drawing). When >1, extends along +Z from first click.
static int g_shape_thickness = 1;
static SDL_Rect g_btn_cursor_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_shape_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_cube_rect = {0, 0, 0, 0};
static SDL_Rect g_btn_sphere_rect = {0, 0, 0, 0};
// Adjacent worlds controls
static SDL_Rect g_btn_adjacent_rect = (SDL_Rect){0, 0, 0, 0};
static SDL_Rect g_adj_buttons[6] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
static bool g_adj_menu_open = false;

// Recent swatches (MRU)
#define RECENT_MAX 5
static int g_recent_count = 5;
static VoxelType g_recent_swatches[RECENT_MAX] = {
    VOXEL_GRASS, VOXEL_SOIL, VOXEL_STONE, VOXEL_WATER, VOXEL_WOOD};

static void mru_push(VoxelType t)
{
  // Move existing to front or insert at front
  int pos = -1;
  for (int i = 0; i < g_recent_count; i++)
    if (g_recent_swatches[i] == t)
    {
      pos = i;
      break;
    }
  if (pos > 0)
  {
    for (int i = pos; i > 0; i--)
      g_recent_swatches[i] = g_recent_swatches[i - 1];
    g_recent_swatches[0] = t;
  }
  else if (pos < 0)
  {
    for (int i = RECENT_MAX - 1; i > 0; i--)
      g_recent_swatches[i] = g_recent_swatches[i - 1];
    g_recent_swatches[0] = t;
  }
}

// ------------------------------------------------------------
// Left vertical Z-level swatch column (10 nearest slices)
// ------------------------------------------------------------
static SDL_Rect g_left_z_swatch_rects[10];
static int g_left_z_swatch_z[10];
static int g_left_z_swatch_count = 0;
static void draw_left_z_swatch_column(SDL_Renderer *ren, TTF_Font *font, World *world, int current_z, int start_x, int start_y)
{
  (void)font;
  if (!world)
  {
    g_left_z_swatch_count = 0;
    return;
  }
  const int sw = 16;
  const int pad = 4;
  const int window = 10;
  g_left_z_swatch_count = 0;
  int depth = (int)world->depth;
  if (depth <= 0)
    return;
  int half = window / 2;
  int zStart = current_z - half;
  if (zStart < 0)
    zStart = 0;
  if (zStart + window > depth)
    zStart = (depth - window) < 0 ? 0 : (depth - window);
  int x = start_x, y = start_y;
  for (int i = 0; i < window; i++)
  {
    int zz = zStart + (window - 1 - i); // invert order: highest slice at the top
    if (zz < 0 || zz >= depth)
      continue;
    // Compute dominant voxel type on this Z slice (ignore AIR)
    size_t counts[(int)VOXEL_COUNT];
    for (int t = 0; t < (int)VOXEL_COUNT; t++)
      counts[t] = 0;
    for (uint32_t yy = 0; yy < world->height; yy++)
      for (uint32_t xx = 0; xx < world->width; xx++)
      {
        Voxel *v = world_get_voxel(world, xx, yy, (uint32_t)zz);
        if (v && v->type != VOXEL_AIR)
          counts[v->type]++;
      }
    int best_t = VOXEL_AIR;
    size_t best_c = 0;
    for (int t = 0; t < (int)VOXEL_COUNT; t++)
      if (t != VOXEL_AIR && counts[t] > best_c)
      {
        best_c = counts[t];
        best_t = t;
      }
    uint8_t r = 20, g = 20, b = 30;
    if (best_t != VOXEL_AIR)
      world_voxel_type_color((VoxelType)best_t, &r, &g, &b);
    SDL_Rect rct = (SDL_Rect){x, y, sw, sw};
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    SDL_RenderFillRect(ren, &rct);
    // Outline, with highlight for current_z
    if (zz == current_z)
      SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    else
      SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderDrawRect(ren, &rct);
    if (g_left_z_swatch_count < (int)(sizeof(g_left_z_swatch_rects) / sizeof(g_left_z_swatch_rects[0])))
    {
      g_left_z_swatch_rects[g_left_z_swatch_count] = rct;
      g_left_z_swatch_z[g_left_z_swatch_count] = zz;
      g_left_z_swatch_count++;
    }
    y += sw + pad;
  }
}

// ------------------------------------------------------------
// Simple actor spawn and fall-to-surface behavior
// ------------------------------------------------------------
static void editor_spawn_falling_actor(World *world)
{
  if (!world)
    return;
  if (g_actor_count >= (int)(sizeof(g_actors) / sizeof(g_actors[0])))
    return;
  int wx = (int)(rand() % (world->width > 0 ? world->width : 1));
  int wy = (int)(rand() % (world->height > 0 ? world->height : 1));
  // Start above top layer
  double ax = (double)wx + 0.5, ay = (double)wy + 0.5, az = (double)world->depth + 2.0;
  Actor a = {0};
  a.id = (uint32_t)(g_actor_count + 1);
  snprintf(a.name, sizeof(a.name), "Actor%u", a.id);
  a.x = ax;
  a.y = ay;
  a.z = az;
  a.velocity_x = 0;
  a.velocity_y = 0;
  a.velocity_z = -0.25;
  a.is_active = true;
  strncpy(a.world_id, "editor", sizeof(a.world_id) - 1);
  g_actors[g_actor_count++] = a;
}

static void editor_step_actors(World *world)
{
  if (!world)
    return;
  for (int i = 0; i < g_actor_count; i++)
  {
    Actor *a = &g_actors[i];
    if (!a->is_active)
      continue;
    // Apply simple gravity
    a->z += a->velocity_z;
    if (a->z < 0.5)
      a->z = 0.5;
    // Check surface below; if solid, stop falling and rest on top
    int vx = (int)floor(a->x);
    int vy = (int)floor(a->y);
    int vz = (int)floor(a->z - 0.5);
    if (vx >= 0 && vy >= 0 && vz >= 0 && vx < (int)world->width && vy < (int)world->height && vz < (int)world->depth)
    {
      Voxel *v = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)vz);
      if (v && v->type != VOXEL_AIR)
      {
        a->z = (double)vz + 1.0;
        a->velocity_z = 0.0;
      }
    }
  }
}

// Find highest solid voxel at (x,y); returns z or -1 if none
static int find_highest_solid_z(World *world, int x, int y)
{
  if (!world)
    return -1;
  for (int z = (int)world->depth - 1; z >= 0; z--)
  {
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (v && v->type != VOXEL_AIR)
      return z;
  }
  return -1;
}

// Find highest air voxel at (x,y); returns z or -1 if none
static int find_highest_air_z(World *world, int x, int y)
{
  if (!world)
    return -1;
  for (int z = (int)world->depth - 1; z >= 0; z--)
  {
    Voxel *v = world_get_voxel(world, (uint32_t)x, (uint32_t)y, (uint32_t)z);
    if (v && v->type == VOXEL_AIR)
      return z;
  }
  return -1;
}

static int g_palette_rect_to_type[1024];
static void draw_palette(SDL_Renderer *ren, TTF_Font *font, int start_x, int start_y, bool embedded)
{
  const int count = (int)VOXEL_COUNT;
  const int cols = 2; // match original style: fewer, wider columns with labels
  const int sw = 22;  // swatch size
  const int pad = 6;  // gap between items
  const int line_h = font ? TTF_FontHeight(font) : 12;
  const int name_w = 120;             // room for label text like original
  const int item_w = sw + 6 + name_w; // swatch + gap + text
  const int row_h = (sw > line_h ? sw : line_h) + 2;
  const int rows = (count + cols - 1) / cols;

  // Background panel (legend only; MRU moved to left panel)
  const int box_w = cols * item_w + (cols - 1) * pad;
  const int total_h = rows * row_h + (rows - 1) * pad + 16; // +16 border padding
  SDL_Rect panel = {start_x, start_y, box_w + 16, total_h};
  g_palette_panel_rect = panel;
  if (!embedded)
  {
    SDL_SetRenderDrawColor(ren, 25, 25, 40, 210);
    SDL_RenderFillRect(ren, &panel);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 180);
    SDL_RenderDrawRect(ren, &panel);
  }

  // Content origin inside panel
  const int content_x0 = panel.x + 8;
  const int content_y0 = panel.y + 8;
  int x = content_x0;
  int y = content_y0;
  g_palette_rect_count = 0;

  // Build ordering
  int order[count];
  for (int i = 0; i < count; i++)
    order[i] = i;
  if (g_palette_rarity_mode && g_have_counts)
  {
    // Sort by count descending (most common first)
    for (int i = 0; i < count - 1; i++)
      for (int j = i + 1; j < count; j++)
        if (g_last_counts[order[j]] > g_last_counts[order[i]])
        {
          int t = order[i];
          order[i] = order[j];
          order[j] = t;
        }
  }
  for (int idx = 0; idx < count; idx++)
  {
    int i = order[idx];
    uint8_t r = 64, g = 64, b = 64;
    world_voxel_type_color((VoxelType)i, &r, &g, &b);
    SDL_Color c = {r, g, b, 255};
    SDL_Rect rect = {x, y + (row_h - sw) / 2, sw, sw};
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(ren, &rect);

    // Border
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderDrawRect(ren, &rect);

    // Selection highlight and mass-hover highlight
    if ((VoxelType)i == g_brush_type)
    {
      SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
      SDL_Rect inset = {rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4};
      SDL_RenderDrawRect(ren, &inset);
    }
    if (g_mass_highlight_type >= 0 && g_mass_highlight_type == i)
    {
      SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
      SDL_Rect inset2 = {rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6};
      SDL_RenderDrawRect(ren, &inset2);
    }

    // Label to the right of the swatch
    if (font)
    {
      const char *label = world_voxel_type_name((VoxelType)i);
      if (!label)
        label = "?";
      draw_text(ren, font, label, rect.x + rect.w + 6, y + (row_h - line_h) / 2, (SDL_Color){220, 220, 220, 255});
    }

    // Store hit rect including label area so text is clickable
    if (g_palette_rect_count < (int)(sizeof(g_palette_rects) / sizeof(g_palette_rects[0])))
    {
      SDL_Rect hit = {x, y, item_w, row_h};
      g_palette_rects[g_palette_rect_count] = hit;
      g_palette_rect_to_type[g_palette_rect_count] = i;
      g_palette_rect_count++;
    }

    // Grid advance using stable palette index, not voxel type id
    x += item_w + pad;
    if (((idx + 1) % cols) == 0)
    {
      x = content_x0;
      y += row_h + pad;
    }
  }

  // Label current brush
  if (font)
  {
    const char *name = world_voxel_type_name(g_brush_type);
    char buf[64];
    snprintf(buf, sizeof(buf), "Brush: %s", name ? name : "?");
    draw_text(ren, font, buf, start_x, y + 6, (SDL_Color){220, 220, 220, 255});
  }
}

static bool handle_palette_click(int mx, int my)
{
  for (int i = 0; i < g_palette_rect_count; i++)
  {
    SDL_Rect r = g_palette_rects[i];
    if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
    {
      int t = g_palette_rect_to_type[i];
      if (t >= 0)
        g_brush_type = (VoxelType)t;
      return true;
    }
  }
  return false;
}

// Left-hand tools panel
static SDL_Rect g_tools_panel_rect = {0, 0, 0, 0};
static void draw_tools_panel(SDL_Renderer *ren, TTF_Font *font, int x, int y, int width)
{
  const int controls_h = (font ? TTF_FontHeight(font) : 12) + 4;
  SDL_Rect ctrl_bg = {x, y, width, controls_h + 12};
  SDL_SetRenderDrawColor(ren, 25, 25, 40, 210);
  SDL_RenderFillRect(ren, &ctrl_bg);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 180);
  SDL_RenderDrawRect(ren, &ctrl_bg);
  g_tools_panel_rect = ctrl_bg;
  int controls_y = ctrl_bg.y + 6;
  // Cursor button
  g_btn_cursor_rect = (SDL_Rect){ctrl_bg.x + 8, controls_y, 70, controls_h};
  SDL_SetRenderDrawColor(ren, g_current_tool == TOOL_CURSOR ? 90 : 50, g_current_tool == TOOL_CURSOR ? 140 : 90, 200, 220);
  SDL_RenderFillRect(ren, &g_btn_cursor_rect);
  SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
  SDL_RenderDrawRect(ren, &g_btn_cursor_rect);
  if (font)
    draw_text(ren, font, "Select", g_btn_cursor_rect.x + 8, g_btn_cursor_rect.y + 2, (SDL_Color){255, 255, 255, 255});
  // Shape button
  g_btn_shape_rect = (SDL_Rect){g_btn_cursor_rect.x + g_btn_cursor_rect.w + 8, controls_y, 70, controls_h};
  SDL_SetRenderDrawColor(ren, g_current_tool == TOOL_SHAPE ? 90 : 50, g_current_tool == TOOL_SHAPE ? 140 : 90, 200, 220);
  SDL_RenderFillRect(ren, &g_btn_shape_rect);
  SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
  SDL_RenderDrawRect(ren, &g_btn_shape_rect);
  if (font)
    draw_text(ren, font, "Draw", g_btn_shape_rect.x + 8, g_btn_shape_rect.y + 2, (SDL_Color){255, 255, 255, 255});
  // Sub-options inline when shape selected
  if (g_current_tool == TOOL_SHAPE)
  {
    g_btn_cube_rect = (SDL_Rect){g_btn_shape_rect.x + g_btn_shape_rect.w + 10, controls_y, 70, controls_h};
    SDL_SetRenderDrawColor(ren, g_shape_mode == SHAPE_CUBE ? 140 : 80, 120, 80, 220);
    SDL_RenderFillRect(ren, &g_btn_cube_rect);
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderDrawRect(ren, &g_btn_cube_rect);
    if (font)
      draw_text(ren, font, "Cube", g_btn_cube_rect.x + 10, g_btn_cube_rect.y + 2, (SDL_Color){255, 255, 255, 255});
    g_btn_sphere_rect = (SDL_Rect){g_btn_cube_rect.x + g_btn_cube_rect.w + 8, controls_y, 70, controls_h};
    SDL_SetRenderDrawColor(ren, g_shape_mode == SHAPE_SPHERE ? 140 : 80, 120, 80, 220);
    SDL_RenderFillRect(ren, &g_btn_sphere_rect);
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderDrawRect(ren, &g_btn_sphere_rect);
    if (font)
      draw_text(ren, font, "Sphere", g_btn_sphere_rect.x + 6, g_btn_sphere_rect.y + 2, (SDL_Color){255, 255, 255, 255});
  }
  // Adjacent worlds toggle button (optional)
  int right_of = (g_current_tool == TOOL_SHAPE ? (g_btn_sphere_rect.x + g_btn_sphere_rect.w) : (g_btn_shape_rect.x + g_btn_shape_rect.w));
  g_btn_adjacent_rect = (SDL_Rect){right_of + 10, controls_y, 46, controls_h};
  SDL_SetRenderDrawColor(ren, 80, 160, 120, 220);
  SDL_RenderFillRect(ren, &g_btn_adjacent_rect);
  SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
  SDL_RenderDrawRect(ren, &g_btn_adjacent_rect);
  if (font)
    draw_text(ren, font, "Adj", g_btn_adjacent_rect.x + 10, g_btn_adjacent_rect.y + 2, (SDL_Color){255, 255, 255, 255});

  // Adjacent worlds flyout if open: render to the right of the tools panel
  if (g_adj_menu_open)
  {
    const char *labels[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    int rows = 2, cols = 3;
    int idx = 0;
    int bw = 60, bh = controls_h;
    int start_x = ctrl_bg.x + ctrl_bg.w + 8;
    int start_y = controls_y;
    for (int r = 0; r < rows; r++)
      for (int c = 0; c < cols; c++, idx++)
      {
        SDL_Rect br = {start_x + c * (bw + 6), start_y + r * (bh + 6), bw, bh};
        g_adj_buttons[idx] = br;
        SDL_SetRenderDrawColor(ren, 80, 160, 120, 220);
        SDL_RenderFillRect(ren, &br);
        SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
        SDL_RenderDrawRect(ren, &br);
        if (font)
          draw_text(ren, font, labels[idx], br.x + 10, br.y + 2, (SDL_Color){255, 255, 255, 255});
      }
  }
}

static SDL_Rect g_chart_container_rect = {0, 0, 0, 0};
static SDL_Rect g_recent_panel_rect = {0, 0, 0, 0};
static SDL_Rect g_actor_panel_rect = {0, 0, 0, 0};
static SDL_Rect g_actor_item_rects[256];
static int g_actor_item_count = 0;
static void draw_recent_panel(SDL_Renderer *ren, TTF_Font *font, int x, int y)
{
  const int sw = 22, pad = 6;
  const int line_h = font ? TTF_FontHeight(font) : 12;
  const int row_h = (sw > line_h ? sw : line_h) + 2;
  const int content_w = RECENT_MAX * sw + (RECENT_MAX - 1) * pad;
  SDL_Rect panel = {x, y, content_w + 16, row_h + 16};
  SDL_SetRenderDrawColor(ren, 25, 25, 40, 210);
  SDL_RenderFillRect(ren, &panel);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 180);
  SDL_RenderDrawRect(ren, &panel);
  g_recent_panel_rect = panel;
  int cx = panel.x + 8;
  int cy = panel.y + 8 + (row_h - sw) / 2;
  for (int i = 0; i < RECENT_MAX; i++)
  {
    VoxelType t = g_recent_swatches[i];
    uint8_t r = 64, g = 64, b = 64;
    world_voxel_type_color(t, &r, &g, &b);
    SDL_Rect rect = {cx, cy, sw, sw};
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    SDL_RenderFillRect(ren, &rect);
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderDrawRect(ren, &rect);
    cx += sw + pad;
  }
}
static void draw_composition_graph(SDL_Renderer *ren, TTF_Font *font, World *world, int current_z, int chart_x, int chart_y)
{
  (void)font; // Unused parameter
  if (!world)
    return;

  // Use same geometry baseline as palette to align widths
  const int cols = 2;
  const int sw = 22;
  const int pad = 6;
  const int name_w = 120;
  const int item_w = sw + 6 + name_w;
  const int box_w = cols * item_w + (cols - 1) * pad;

  // Count voxels only for the currently viewed layer
  size_t counts[VOXEL_COUNT];
  for (int i = 0; i < (int)VOXEL_COUNT; i++)
    counts[i] = 0;
  if (current_z >= 0 && current_z < (int)world->depth)
  {
    for (uint32_t y = 0; y < world->height; y++)
      for (uint32_t x = 0; x < world->width; x++)
      {
        Voxel *v = world_get_voxel(world, x, y, current_z);
        if (!v)
          continue;
        counts[v->type]++;
      }
  }

  // Background and border
  const int chart_w = box_w;
  const int chart_h = 18;
  // base container height (bar + padding)
  int container_h = chart_h + 16;
  // Labels are drawn by the caller; no internal label rendering here.
  SDL_Rect bg = {chart_x - 8, chart_y - 8, chart_w + 16, container_h};
  SDL_SetRenderDrawColor(ren, 25, 25, 40, 210);
  SDL_RenderFillRect(ren, &bg);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 180);
  SDL_RenderDrawRect(ren, &bg);
  g_chart_container_rect = bg;


  // No internal label drawing

  // No title per new design; bar starts at chart_y within panel

  // Compute total non-air
  size_t total_non_air = 0;
  for (int i = 0; i < (int)VOXEL_COUNT; i++)
    if (i != VOXEL_AIR)
      total_non_air += counts[i];
  // Cache counts for rarity sort
  for (int i = 0; i < (int)VOXEL_COUNT; i++)
    g_last_counts[i] = counts[i];
  g_have_counts = true;

  // Draw stacked bar and store hit rects
  g_chart_rect_count = 0;
  int cursor_x = chart_x;
  if (total_non_air == 0)
  {
    // empty
    SDL_SetRenderDrawColor(ren, 60, 60, 80, 255);
    SDL_Rect empty = {chart_x, chart_y, chart_w, chart_h};
    SDL_RenderFillRect(ren, &empty);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 160);
    SDL_RenderDrawRect(ren, &empty);
    return;
  }

  int remaining_w = chart_w;
  size_t remaining_total = total_non_air;
  int bar_y = chart_y;
  for (int i = 0; i < (int)VOXEL_COUNT; i++)
  {
    if (i == VOXEL_AIR)
    {
      g_chart_rects[i] = (SDL_Rect){0, 0, 0, 0};
      continue;
    }
    size_t c = counts[i];
    if (c == 0)
    {
      g_chart_rects[i] = (SDL_Rect){0, 0, 0, 0};
      continue;
    }
    // proportional width; allocate remaining to avoid rounding gaps
    int seg_w = (int)((double)c / (double)remaining_total * (double)remaining_w + 0.5);
    if (seg_w < 1)
      seg_w = 1;
    if (seg_w > remaining_w)
      seg_w = remaining_w;
    uint8_t r = 64, g = 64, b = 64;
    world_voxel_type_color((VoxelType)i, &r, &g, &b);
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    SDL_Rect seg = {cursor_x, bar_y, seg_w, chart_h};
    SDL_RenderFillRect(ren, &seg);
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderDrawRect(ren, &seg);
    g_chart_rects[i] = seg;
    cursor_x += seg_w;
    remaining_w -= seg_w;
    remaining_total -= c;
  }
}

typedef struct GenArgs
{
  World *world;
  const char *input;
  int done;             // 0 running, 1 done
  int success;          // 1 on success
  int loaded_from_file; // output flag
} GenArgs;

static int load_or_generate_world_thread(void *udata)
{
  GenArgs *ga = (GenArgs *)udata;
  World *world = ga->world;
  const char *input = ga->input;
  int ok = 1;
  ga->loaded_from_file = 0;

  if (g_debug)
  {
    printf("[editor] Generation thread started with world %p\n", (void *)world);
    printf("[editor] Input: %s\n", input ? input : "NULL");
  }
  if (input)
  {
    size_t len = strlen(input);
    if (len > 6 && strcmp(input + (len - 6), ".world") == 0)
    {
      printf("[editor] trying to load world file: %s\n", input);
      if (!world_load(world, input))
      {
        ok = 0;
        printf("[editor] failed to load world file: %s\n", input);
        ga->success = 0;
        ga->done = 1;
        return 0; // Do not generate if an explicit .world path fails
      }
      ga->loaded_from_file = 1;
      printf("[editor] loaded world file: %s\n", input);
      // Short-circuit generation path entirely when a world file is provided and loaded.
      world_refresh_occupancy_bitfield(world);
      ga->success = ok;
      ga->done = 1;
      return 0;
    }
    else if (len > 4 && strcmp(input + (len - 4), ".vox") == 0)
    {
      if (!model_transformer_load_vox(world, input, 0, 0, 0, VOXEL_STONE))
      {
        ok = 0;
      }
    }
    else
    {
      // Unrecognized input format; fallback to generate
      ok = 1;
    }
  }
  if (!input || !ga->loaded_from_file)
  {
    if (input && !ga->loaded_from_file)
      printf("[editor] falling back to generating a new world (input not loaded)\n");

    // Don't automatically generate a world - start with empty world
    // World generation will be triggered by the "New World" button
    printf("[editor] Starting with empty world - use 'New World' button to generate content\n");

    // Set a default seed for the empty world
    char seed[65];
    unsigned int t = (unsigned int)time(NULL);
    for (int i = 0; i < 32; i++)
    {
      t = t * 1103515245u + 12345u;
      sprintf(seed + i * 2, "%02x", (unsigned)(t & 0xFF));
    }
    seed[64] = '\0';

    world_set_universe_noise_seed(seed);


    // Don't generate terrain - leave world empty
    world->universe_depth = 0;
  }
  // Build occupancy bitfield for fast queries (optional)
  world_refresh_occupancy_bitfield(world);
  ga->success = ok;
  ga->done = 1;
  return 0;
}

// GameWorlds wrapper for FP renderer (prevents cache misses from changing pointers)
// Declaration moved to top of file

int main(int argc, char *argv[])
{
  // Set debug environment variable early if --debug flag is present
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--debug") == 0)
    {
      setenv("VERSE_DEBUG", "1", 1); // Set environment variable for voxel mesh functions
      break;
    }
  }

  const char *input = NULL;

  // Parse command line arguments
  for (int i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--debug") == 0)
    {
      g_debug = true;
      printf("Debug mode enabled\n");
    }
    else if (argv[i][0] != '-')
    {
      // First non-flag argument is the input file
      if (!input)
        input = argv[i];
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    printf("SDL init failed: %s\n", SDL_GetError());
    return 1;
  }
  if (TTF_Init() < 0)
  {
    printf("TTF init failed: %s\n", TTF_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Window *win = SDL_CreateWindow("VERSE World Editor (GPU)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
  if (!win)
  {
    printf("Window creation failed: %s\n", SDL_GetError());
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  // Create OpenGL context for FP renderer
  SDL_GLContext gl_context = SDL_GL_CreateContext(win);
  if (!gl_context)
  {
    printf("OpenGL context creation failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  // Initialize OpenGL for FP renderer
  fp_renderer_gl_init();

  // Set FP renderer to optimized mesh mode (same as universe-ref)
  fp_renderer_set_mode(FP_MODE_MESH);
  fp_renderer_enable_gpu_mesh(true);

  // Create SDL renderer for 2D UI elements
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren)
  {
    printf("Renderer creation failed: %s\n", SDL_GetError());
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  // Load font
  TTF_Font *font = TTF_OpenFont("assets/fonts/visitor-tt2-brk.ttf", 14);
  if (!font)
  {
    font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 14);
  }

  // Initialize the unified UI system
  printf("[editor] Creating world editor UI...\n");
  g_editor_ui = world_editor_ui_create(ren, "assets/fonts/visitor-tt2-brk.ttf");
  if (!g_editor_ui)
  {
    printf("Failed to create world editor UI\n");
    if (font)
      TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }
  printf("[editor] World editor UI created successfully\n");

  // Initialize the new input system for World Editor
  g_input_system = world_editor_input_create();
  if (!g_input_system)
  {
    printf("Failed to create world editor input system\n");
    if (font)
      TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  // Set up input system callbacks (will be configured after world and renderer are created)

  // Set up the file operation callback
  world_editor_ui_set_file_callback(g_editor_ui, on_file_operation, NULL);

  // Calculate layout for current window dimensions before creating panels
  printf("[editor] Calculating layout for window dimensions...\n");
  world_editor_ui_calculate_layout(g_editor_ui, WINDOW_W, WINDOW_H);
  printf("[editor] Layout calculated\n");

  // Create minimal UI panels to avoid covering right-hand elements
  printf("[editor] Creating minimal UI panels...\n");
  world_editor_ui_create_minimal_panels(g_editor_ui);
  printf("[editor] Minimal UI panels created\n");

  // Set up tool callbacks
  world_editor_ui_set_tool_callback(g_editor_ui, on_tool_change, NULL);

  // Set up actor movement callback
  world_editor_ui_set_actor_move_callback(g_editor_ui, on_actor_move, NULL);

  // Debug: check what UI panels were created
  printf("[editor] UI panels created - file_panel.panel: %p, file_browser_panel.panel: %p\n",
         (void *)g_editor_ui->file_panel.panel, (void *)g_editor_ui->file_browser_panel.panel);
  if (g_editor_ui->file_panel.panel)
  {
    printf("[editor] File panel panel exists\n");
  }
  if (g_editor_ui->file_browser_panel.file_browser)
  {
    printf("[editor] File browser panel has file browser: %p\n", (void *)g_editor_ui->file_browser_panel.file_browser);
  }

  // Set up file browser callbacks
  if (g_editor_ui->file_browser_panel.file_browser)
  {
    ui_file_browser_set_callbacks(g_editor_ui->file_browser_panel.file_browser,
                                  on_file_browser_file_selected,
                                  on_file_browser_directory_changed,
                                  NULL);
  }

  // Set up file browser navigation button callbacks
  if (g_editor_ui->file_browser_panel.refresh_button)
  {
    ui_button_set_callback(g_editor_ui->file_browser_panel.refresh_button,
                           on_file_browser_refresh_click, NULL);
  }

  if (g_editor_ui->file_browser_panel.home_button)
  {
    ui_button_set_callback(g_editor_ui->file_browser_panel.home_button,
                           on_file_browser_home_click, NULL);
  }

  // Create world object
  uint32_t sx = WORLD_SIZE_X, sy = WORLD_SIZE_Y, sz = WORLD_SIZE_Z;
  World *world = world_create(sx, sy, sz);
  if (!world)
  {
    printf("Failed to create world: %ux%ux%u\n", sx, sy, sz);
    if (font)
      TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  // Debug: verify world data integrity
  if (g_debug)
  {
    printf("[editor] World created: %dx%dx%d\n", (int)world->width, (int)world->height, (int)world->depth);
    printf("[editor] World seed: %s\n", world->seed_id);
    printf("[editor] World type: %d\n", world->generation_type);

    // Count non-air voxels
    size_t non_air_count = 0;
    size_t total_voxels = (size_t)world->width * (int)world->height * (int)world->depth;
    for (size_t i = 0; i < total_voxels && i < 10000; i++)
    { // Sample first 10k for speed
      if (world->voxels[i].type != VOXEL_AIR)
      {
        non_air_count++;
      }
    }
    printf("[editor] Non-air voxels (sampled): %zu/%zu (%.1f%%)\n",
           non_air_count, total_voxels, (non_air_count * 100.0) / total_voxels);
  }

  // No world generation at startup - start with empty world
  // World generation will be triggered by the "New World" button
  printf("[editor] Starting with empty world - no generation\n");

  // Start with completely empty world - no content at all
  if (world && world->voxels)
  {
    size_t total_voxels = (size_t)world->width * (size_t)world->height * (size_t)world->depth;
    for (size_t i = 0; i < total_voxels; i++)
    {
      world->voxels[i].type = VOXEL_AIR;
    }
    printf("[editor] World cleared to empty state\n");
  }

  // Initialize isometric renderer
  g_isometric_renderer = isometric_renderer_create(WINDOW_W, WINDOW_H);
  IsometricRenderer *ir = g_isometric_renderer; // Keep local reference for compatibility
  if (!ir)
  {
    printf("Failed to create isometric renderer\n");
    if (font)
      TTF_CloseFont(font);
    world_destroy(world);
    SDL_DestroyRenderer(ren);
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 1;
  }

  // Set up input system callbacks now that world and renderer are available
  world_editor_input_set_actor_insert_callback(g_input_system, world_editor_insert_actor, NULL);
  world_editor_input_set_mouse_look_toggle_callback(g_input_system, world_editor_toggle_mouse_look, NULL);
  world_editor_input_set_time_control_callback(g_input_system, world_editor_time_control, NULL);
  world_editor_input_set_zoom_control_callback(g_input_system, world_editor_zoom_control, g_isometric_renderer);
  world_editor_input_set_camera_move_callback(g_input_system, world_editor_camera_move, g_isometric_renderer);
  world_editor_input_set_camera_rotate_callback(g_input_system, world_editor_camera_rotate, NULL);
  world_editor_input_set_camera_reset_callback(g_input_system, world_editor_camera_reset, NULL);
  world_editor_input_set_renderer_toggle_callback(g_input_system, world_editor_renderer_toggle, NULL);
  world_editor_input_set_chat_toggle_callback(g_input_system, world_editor_chat_toggle, NULL);
  world_editor_input_set_clock_toggle_callback(g_input_system, world_editor_clock_toggle, NULL);
  // Ensure global user_data points at the isometric renderer so zoom/camera callbacks receive it
  g_input_system->user_data = g_isometric_renderer;

  // BOTH RENDERERS USE THE EXACT SAME WORLD INSTANCE - NO WRAPPERS
  g_fp_gameworlds = (GameWorlds){0}; // Initialize the struct
  g_fp_gameworlds.home_world = world;

  // Use the SAME world pointer for isometric renderer
  isometric_renderer_set_game_worlds(g_isometric_renderer, &g_fp_gameworlds);
  // Disable auto-centering when we've loaded a specific world file to preserve origin camera
  // Center camera explicitly for initial view; do not rely on auto-centering
  isometric_renderer_set_auto_center(g_isometric_renderer, false);
  // current_z already declared later; compute z here without redeclaring
  int init_z = (int)(world->depth - 1);
  isometric_renderer_set_camera(g_isometric_renderer, (int)(world->width / 2), (int)(world->height / 2), init_z);
  // Initialize persistent editor universe cache (seed + origin placement)
  if (!g_universe_inited)
  {
    // Generate a canonical universe seed first and use it for ALL worlds (origin + adjacent)
    const char *base_seed = (world && world->seed_id[0]) ? world->seed_id : "editor";
    snprintf(g_universe_seed, sizeof(g_universe_seed), "%s", base_seed);
    world_set_universe_noise_seed(g_universe_seed);
    universe_init(&g_editor_universe, g_universe_seed, 0, 0);
    g_universe_inited = true;
  }
  g_home_gx = 0;
  g_home_gy = 0;
  // Anchor universe noise to the origin world's offsets so seams match existing terrain

  // Place origin world only; do not regenerate its contents if loaded from file
  (void)universe_place(&g_editor_universe, 0, 0, 0, world);
  // No world generation at startup - keep world empty
  printf("[editor] skipping origin generation (starting with empty world)\n");

  // Debug: count again after potential generation
  size_t dbg_non_air_after = 0;
  if (world && world->voxels)
  {
    size_t voxels_total = (size_t)world->width * (size_t)world->height * (size_t)world->depth;
    size_t sample = voxels_total < 200000 ? voxels_total : 200000;
    for (size_t i = 0; i < sample; i++)
      if (world->voxels[i].type != VOXEL_AIR)
      {
        dbg_non_air_after++;
      }
  }
  printf("[editor] non-air voxels after placement/regeneration (sampled) = %zu\n", dbg_non_air_after);

  // Do not auto-generate clouds above home; universe policy will provide clouds over wilderness when needed

  // No neighbor generation at startup - start with truly empty world
  printf("[editor] skipping neighbor generation (starting with empty world)\n");

  printf("[editor] About to initialize renderer and enter main loop\n");

  // If this is a 32x32x1 layer world with an embedded topDownTexture, load it for full 255-color rendering
  isometric_renderer_try_load_layer_texture(g_isometric_renderer, world);

  printf("[editor] Checking for layer texture processing\n");
  // If the loaded world is a 32x32x1 layer and currently empty (all AIR), but it has a full-color
  // topDownTexture in its log, populate voxels by mapping each pixel's RGB to the nearest VoxelType.
  // This allows the editor to render and edit voxels even when the .world voxel buffer is blank.
  if (world && world->depth == 1 && world->width > 0 && world->height > 0)
  {
    bool any_non_air = false;
    for (uint32_t y = 0; y < world->height && !any_non_air; y++)
      for (uint32_t x = 0; x < world->width && !any_non_air; x++)
      {
        Voxel *v = world_get_voxel(world, x, y, 0u);
        if (v && v->type != VOXEL_AIR)
        {
          any_non_air = true;
          break;
        }
      }
    if (!any_non_air)
    {
      const char *log = world_get_log(world);
      if (log)
      {
        const char *td = strstr(log, "\"topDownTexture\"");
        const char *enc = td ? strstr(td, "\"encoding\":\"hex24\"") : NULL;
        const char *wpos = td ? strstr(td, "\"width\":") : NULL;
        const char *hpos = td ? strstr(td, "\"height\":") : NULL;
        const char *ppos = td ? strstr(td, "\"pixelsHex\":\"") : NULL;
        if (enc && wpos && hpos && ppos)
        {
          int tw = atoi(wpos + 9);
          int th = atoi(hpos + 10);
          const char *start = ppos + (int)strlen("\"pixelsHex\":\"");
          const char *end = start ? strchr(start, '"') : NULL;
          if (end && end > start && tw > 0 && th > 0)
          {
            size_t hex_len = (size_t)(end - start);
            size_t expected = (size_t)tw * (size_t)th * 6;
            if (hex_len >= expected)
            {
              // Map RGB -> nearest VoxelType for each pixel
              for (int py = 0; py < th; py++)
              {
                for (int px = 0; px < tw; px++)
                {
                  size_t idx = ((size_t)py * (size_t)tw + (size_t)px) * 6;
                  unsigned int rv = 0, gv = 0, bv = 0;
                  sscanf(start + idx, "%02X%02X%02X", &rv, &gv, &bv);
                  // Find nearest voxel type by squared RGB distance; skip AIR, prefer hex-storable range 0..15
                  int best_t = -1;
                  int best_d = 1 << 30;
                  for (int t = 0; t < 16; t++)
                  {
                    if ((VoxelType)t == VOXEL_AIR)
                      continue;
                    uint8_t tr = 0, tg = 0, tb = 0;
                    world_voxel_type_color((VoxelType)t, &tr, &tg, &tb);
                    int dr = (int)rv - (int)tr;
                    int dg = (int)gv - (int)tg;
                    int db = (int)bv - (int)tb;
                    int d = dr * dr + dg * dg + db * db;
                    if (d < best_d)
                    {
                      best_d = d;
                      best_t = t;
                    }
                  }
                  if (best_t < 0)
                    best_t = (int)VOXEL_STONE; // safe fallback
                  if ((uint32_t)px < world->width && (uint32_t)py < world->height)
                  {
                    world_set_voxel(world, (uint32_t)px, (uint32_t)py, 0u, (VoxelType)best_t);
                    fp_renderer_invalidate_cache(); // Invalidate mesh cache after world modification
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  printf("[editor] Skipping layer texture processing (starting with empty world)\n");

  printf("[editor] About to load model textures\n");
  // Load models/layer_*.world textures for per-voxel-type overrides
  isometric_renderer_load_layer_textures_from_models(ir, "models");
  printf("[editor] Model textures loaded\n");

  // No edge rebuilding at startup - start with truly empty world
  printf("[editor] skipping edge rebuilding (starting with empty world)\n");

  isometric_renderer_set_camera(ir, (int)(world->width / 2), (int)(world->height / 2), (int)(world->depth / 2));

  // Initialize occupancy bitfield + GL_TEXTURE_3D upload target (upload is no-op without a GL context)
  GpuVoxelBuffer *gpu_bits = gpu_voxel_buffer_create_from_world(world);
  GlOccupancyTexture gl_occ;
  gl_occupancy_init(&gl_occ);
  NSField *ns = ns_create_for_world(world);
  if (ns)
    ns_sync_from_world(ns, world);

  // Derive a simple world display name
  if (input)
  {
    snprintf(g_world_name, sizeof(g_world_name), "%s", input);
  }
  else if (world && world->seed_id[0])
  {
    snprintf(g_world_name, sizeof(g_world_name), "%s.world", world->seed_id);
  }
  else
  {
    snprintf(g_world_name, sizeof(g_world_name), "Untitled World");
  }

  bool running = true;
  g_current_z = (int)(world->depth - 1); // start with all layers rendered
  int current_z = g_current_z;           // Keep local reference for compatibility
  g_isometric_renderer->camera_z = g_current_z;

  static bool g_swap_views = false; // swap iso/FP when true

  printf("[editor] Entering main loop\n");
  while (running)
  {
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
      if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
      {
        int new_w = e.window.data1;
        int new_h = e.window.data2;
        if (new_w > 0 && new_h > 0)
        {
          isometric_renderer_set_screen_size(ir, new_w, new_h);
          // Recenter camera on world center for the new dimensions
          isometric_renderer_set_auto_center(ir, false);
          isometric_renderer_set_camera(ir, (int)(world->width / 2), (int)(world->height / 2), ir->camera_z);
        }
        continue;
      }

      // NEW: Let the unified UI system handle events first
      if (g_editor_ui && world_editor_ui_handle_event(g_editor_ui, &e))
      {
        // Event was handled by UI system
        continue;
      }

      // Let the new input system handle game input events
      if (g_input_system && world_editor_input_handle_event(g_input_system, &e))
      {
        // Event was handled by input system
        continue;
      }
      if (e.type == SDL_QUIT)
      {
        running = false;
      }
      else if (e.type == SDL_KEYDOWN)
      {
        // Debug: Print all key presses
        printf("[DEBUG] Main event loop received key: %d (sym: %d)\n", e.key.keysym.scancode, e.key.keysym.sym);
        if (e.key.keysym.sym == SDLK_ESCAPE)
          running = false;
        // Handle mouse look toggle (only when actors are available for FP camera)
        else if (e.key.keysym.scancode == SDL_SCANCODE_M)
        {
          printf("[editor] M key pressed! g_actor_count=%d\n", g_actor_count);
          if (g_actor_count > 0)
          {
            // Toggle mouse look mode
            g_mouse_look_enabled = !g_mouse_look_enabled;
            if (g_mouse_look_enabled)
            {
              printf("[editor] Mouse look ENABLED - move mouse to look around\n");
              // Initialize mouse position for relative movement
              g_mouse_look_initialized = false;
            }
            else
            {
              printf("[editor] Mouse look DISABLED\n");
            }
          }
          else
          {
            printf("[editor] M key ignored - no actors available for FP camera\n");
          }
        }
        else if ((SDL_GetModState() & KMOD_ALT) && e.key.keysym.scancode == SDL_SCANCODE_UP)
        {
          // ALT+Up: move selected actor up one voxel (if any), with collision
          if (g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count)
          {
            Actor *a = &g_actors[g_controlled_actor_index];
            if (gpu_physics_try_move_actor(world, a, 0, 0, +1))
            {
              int ccx = (int)floor(a->x + 0.0001);
              int ccy = (int)floor(a->y + 0.0001);
              isometric_renderer_set_auto_center(ir, false);
              isometric_renderer_set_camera(ir, ccx, ccy, ir->camera_z);
              g_follow_actor_camera = true;
            }
          }
          continue;
        }
        else if ((SDL_GetModState() & KMOD_ALT) && e.key.keysym.scancode == SDL_SCANCODE_DOWN)
        {
          // ALT+Down: move selected actor down one voxel (if any), with collision
          if (g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count)
          {
            Actor *a = &g_actors[g_controlled_actor_index];
            if (gpu_physics_try_move_actor(world, a, 0, 0, -1))
            {
              int ccx = (int)floor(a->x + 0.0001);
              int ccy = (int)floor(a->y + 0.0001);
              isometric_renderer_set_auto_center(ir, false);
              isometric_renderer_set_camera(ir, ccx, ccy, ir->camera_z);
              g_follow_actor_camera = true;
            }
          }
          continue;
        }
        // Alt + Arrow: switch to neighbor world, generating if missing; reset editor state like opening that world
        else if ((e.key.keysym.scancode == SDL_SCANCODE_LEFT || e.key.keysym.scancode == SDL_SCANCODE_RIGHT ||
                  e.key.keysym.scancode == SDL_SCANCODE_UP || e.key.keysym.scancode == SDL_SCANCODE_DOWN) &&
                 (SDL_GetModState() & KMOD_ALT))
        {
          // If chat was open, close it so arrows control camera/unit
          if (g_chat_open)
            g_chat_open = false;

          int sdx = 0, sdy = 0;
          if (e.key.keysym.scancode == SDL_SCANCODE_RIGHT)
          {
            sdx = +1;
          }
          else if (e.key.keysym.scancode == SDL_SCANCODE_LEFT)
          {
            sdx = -1;
          }
          else if (e.key.keysym.scancode == SDL_SCANCODE_UP)
          {
            sdy = -1;
          } // Up -> visual north = world -Y
          else if (e.key.keysym.scancode == SDL_SCANCODE_DOWN)
          {
            sdy = +1;
          } // Down -> visual south = world +Y

          // Proceed regardless of offset index; we can still move the camera and attach a home world
          {
            // Determine absolute grid for target home
            int tgt_gx = g_home_gx + sdx;
            int tgt_gy = g_home_gy + sdy;
            World *existing = universe_get(&g_editor_universe, (uint64_t)tgt_gx, (uint64_t)tgt_gy, 0);
            // Always advance universe cursor and camera immediately
            editor_persist_current_edges(ir, world);
            g_home_gx = tgt_gx;
            g_home_gy = tgt_gy;
            // Move camera by one world tile so user sees the next world position instantly
            int nx = ir->camera_x + sdx * (int)world->width;
            int ny = ir->camera_y + sdy * (int)world->height;
            isometric_renderer_set_auto_center(ir, false);
            isometric_renderer_set_camera(ir, nx, ny, ir->camera_z);
            // Select new home for renderer: use existing if available, otherwise create a lightweight stub
            World *new_home = existing;
            if (!new_home)
            {
              // Free any prior stub
              if (g_stub_home)
              {
                world_destroy(g_stub_home);
                g_stub_home = NULL;
              }
              g_stub_home = world_create(world->width, world->height, world->depth);
              if (g_stub_home)
              {

                snprintf(g_stub_home->seed_id, sizeof(g_stub_home->seed_id), "%s", g_universe_seed[0] ? g_universe_seed : "editor");
                new_home = g_stub_home;
                g_stub_gx = g_home_gx;
                g_stub_gy = g_home_gy;
              }
            }
            if (new_home)
            {
              // Update renderer home world and rebuild edges around the chosen home (existing or stub)
              g_fp_gameworlds.home_world = new_home;
              isometric_renderer_set_game_worlds(ir, &g_fp_gameworlds);
              for (int i = 0; i < 27; i++)
                ir->edge_worlds[i] = NULL;
              editor_rebuild_edges(ir, new_home);
              // Refresh GPU/NS resources for the home
              if (ns)
              {
                ns_destroy(ns);
                ns = NULL;
              }
              if (gpu_bits)
              {
                gpu_voxel_buffer_destroy(gpu_bits);
                gpu_bits = NULL;
              }
              gl_occupancy_destroy(&gl_occ);
              gl_occupancy_init(&gl_occ);
              gpu_bits = gpu_voxel_buffer_create_from_world(new_home);
              ns = ns_create_for_world(new_home);
              if (ns)
                ns_sync_from_world(ns, new_home);
              // Make sure the editor's active world pointer follows the renderer's home
              world = new_home;
            }
            // Generate/attach neighbors synchronously for the new home
            if (!existing)
              editor_rebuild_edges(ir, world);
          }
          continue; // consume event
        }

        else if (e.key.keysym.sym == SDLK_ESCAPE)
        {
          // Cancel in-progress shape preview
          if (g_shape_has_first)
          {
            g_shape_has_first = false;
          }
        }
        else if (e.key.keysym.sym == SDLK_i)
        {
          // Insert an actor at highest air voxel at center and select it
          int cx = (int)(world->width / 2);
          int cy = (int)(world->height / 2);
          int az = find_highest_air_z(world, cx, cy);
          if (az < 0)
          {
            // Fallback: place just above highest solid, or at z=0 if none
            int hz = find_highest_solid_z(world, cx, cy);
            az = (hz >= 0) ? (hz + 1) : 0;
          }
          if (g_actor_count < (int)(sizeof(g_actors) / sizeof(g_actors[0])))
          {
            Actor a = {0};
            a.id = (uint32_t)(g_actor_count + 1);
            snprintf(a.name, sizeof(a.name), "Actor%u", a.id);
            a.x = (double)cx + 0.5;
            a.y = (double)cy + 0.5;
            a.z = (double)az + 0.5; // center within the highest air voxel
            a.velocity_x = 0.0;
            a.velocity_y = 0.0;
            a.velocity_z = 0.0;
            a.is_active = true;
            strncpy(a.world_id, "editor", sizeof(a.world_id) - 1);
            g_actors[g_actor_count] = a;
            g_controlled_actor_index = g_actor_count;
            g_follow_actor_camera = true;
            g_actor_count++;
            // Place an ACTOR voxel at the spawn for easy visual debugging
            world_set_voxel(world, (uint32_t)cx, (uint32_t)cy, (uint32_t)az, VOXEL_ACTOR);
            fp_renderer_invalidate_cache(); // Invalidate mesh cache after world modification
            // Center camera on actor (round to nearest voxel to avoid half-voxel parallax)
            isometric_renderer_set_auto_center(ir, false);
            int ccx = (int)floor(a.x + 0.0001);
            int ccy = (int)floor(a.y + 0.0001);
            isometric_renderer_set_camera(ir, ccx, ccy, ir->camera_z);
          }
        }
        else if (e.key.keysym.sym == SDLK_F1)
        {
          // Toggle selecting the first inserted unit; if already selected, center camera on it
          if (g_actor_count > 0)
          {
            if (g_controlled_actor_index == 0)
            {
              // Center camera on selected unit
              Actor *a = &g_actors[0];
              isometric_renderer_set_auto_center(ir, false);
              int ccx = (int)floor(a->x + 0.5);
              int ccy = (int)floor(a->y + 0.5);
              isometric_renderer_set_camera(ir, ccx, ccy, ir->camera_z);
            }
            else
            {
              g_controlled_actor_index = 0;
              g_follow_actor_camera = true;
            }
          }
        }
        else if (e.key.keysym.sym == SDLK_PERIOD || e.key.keysym.sym == SDLK_GREATER)
        {
          // mark hold for acceleration increase (on first press)
          if (!g_accel_increase_held)
          {
            g_accel_increase_held = true;
            g_accel_increase_last_change = SDL_GetTicks();
          }
          if (g_time_factor < MAX_TIME_FACTOR)
            g_time_factor = (g_time_factor < 1000000ULL) ? g_time_factor + 1ULL : (g_time_factor * 2ULL <= MAX_TIME_FACTOR ? g_time_factor * 2ULL : MAX_TIME_FACTOR);
        }
        else if (e.key.keysym.sym == SDLK_COMMA || e.key.keysym.sym == SDLK_LESS)
        {
          // mark hold for acceleration decrease (on first press)
          if (!g_accel_decrease_held)
          {
            g_accel_decrease_held = true;
            g_accel_decrease_last_change = SDL_GetTicks();
          }
          if (g_time_factor > 1ULL)
            g_time_factor = (g_time_factor > 1000000ULL) ? (g_time_factor / 2ULL) : (g_time_factor - 1ULL);
        }
        // Zoom controls: '=' or '+' to zoom in; '-' to zoom out (discrete levels)
        else if (e.key.keysym.sym == SDLK_PLUS || e.key.keysym.sym == SDLK_EQUALS)
        {
          printf("[DEBUG] Main event loop: +/= key pressed\n");
          if (g_isometric_renderer)
          {
            // ALT modifies neighbor inclusion radius instead of zoom
            const SDL_Keymod mods = SDL_GetModState();
            if (mods & KMOD_ALT)
            {
              if (g_isometric_renderer->neighbor_inclusion_radius < 2)
                g_isometric_renderer->neighbor_inclusion_radius += 1;
            }
            else
            {
              printf("[DEBUG] Main event loop: Zooming in, current_zoom=%.2f\n", g_isometric_renderer->zoom_scale);

              // Step to next discrete zoom level
              if (g_isometric_renderer->zoom_scale < 0.25f)
                g_isometric_renderer->zoom_scale = 0.25f;
              else if (g_isometric_renderer->zoom_scale < 0.5f)
                g_isometric_renderer->zoom_scale = 0.5f;
              else if (g_isometric_renderer->zoom_scale < 0.75f)
                g_isometric_renderer->zoom_scale = 0.75f;
              else if (g_isometric_renderer->zoom_scale < 1.0f)
                g_isometric_renderer->zoom_scale = 1.0f;
              else if (g_isometric_renderer->zoom_scale < 1.5f)
                g_isometric_renderer->zoom_scale = 1.5f;
              else if (g_isometric_renderer->zoom_scale < 2.0f)
                g_isometric_renderer->zoom_scale = 2.0f;
              else if (g_isometric_renderer->zoom_scale < 3.0f)
                g_isometric_renderer->zoom_scale = 3.0f;
              else if (g_isometric_renderer->zoom_scale < 4.0f)
                g_isometric_renderer->zoom_scale = 4.0f;
              // Already at max zoom

              printf("[DEBUG] Main event loop: New zoom=%.2f\n", g_isometric_renderer->zoom_scale);
            }
          }
          else
          {
            printf("[DEBUG] Main event loop: g_isometric_renderer is NULL\n");
          }
        }
        else if (e.key.keysym.sym == SDLK_MINUS)
        {
          printf("[DEBUG] Main event loop: - key pressed\n");
          if (g_isometric_renderer)
          {
            // ALT modifies neighbor inclusion radius instead of zoom
            const SDL_Keymod mods = SDL_GetModState();
            if (mods & KMOD_ALT)
            {
              if (g_isometric_renderer->neighbor_inclusion_radius > 0)
                g_isometric_renderer->neighbor_inclusion_radius -= 1;
            }
            else
            {
              printf("[DEBUG] Main event loop: Zooming out, current_zoom=%.2f\n", g_isometric_renderer->zoom_scale);

              // Step to previous discrete zoom level
              if (g_isometric_renderer->zoom_scale > 4.0f)
                g_isometric_renderer->zoom_scale = 4.0f;
              else if (g_isometric_renderer->zoom_scale > 3.0f)
                g_isometric_renderer->zoom_scale = 3.0f;
              else if (g_isometric_renderer->zoom_scale > 2.0f)
                g_isometric_renderer->zoom_scale = 2.0f;
              else if (g_isometric_renderer->zoom_scale > 1.5f)
                g_isometric_renderer->zoom_scale = 1.5f;
              else if (g_isometric_renderer->zoom_scale > 1.0f)
                g_isometric_renderer->zoom_scale = 1.0f;
              else if (g_isometric_renderer->zoom_scale > 0.75f)
                g_isometric_renderer->zoom_scale = 0.75f;
              else if (g_isometric_renderer->zoom_scale > 0.5f)
                g_isometric_renderer->zoom_scale = 0.5f;
              else if (g_isometric_renderer->zoom_scale > 0.25f)
                g_isometric_renderer->zoom_scale = 0.25f;
              // Already at min zoom

              printf("[DEBUG] Main event loop: New zoom=%.2f\n", g_isometric_renderer->zoom_scale);
            }
          }
          else
          {
            printf("[DEBUG] Main event loop: g_isometric_renderer is NULL\n");
          }
        }

        // (Removed legacy per-arrow handlers; canonical handler above handles movement and panning)
      }
      else if (e.type == SDL_KEYUP)
      {
        if (e.key.keysym.sym == SDLK_PERIOD || e.key.keysym.sym == SDLK_GREATER)
        {
          g_accel_increase_held = false;
          g_accel_increase_last_change = 0;
        }
        else if (e.key.keysym.sym == SDLK_COMMA || e.key.keysym.sym == SDLK_LESS)
        {
          g_accel_decrease_held = false;
          g_accel_decrease_last_change = 0;
        }
      }
      else if (e.type == SDL_MOUSEMOTION)
      {
        int mx = e.motion.x;
        int my = e.motion.y;
        g_hover_mouse_x = mx;
        g_hover_mouse_y = my;

        // Handle mouse look for first-person camera
        if (g_mouse_look_enabled && g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count)
        {
          // Initialize mouse position on first movement
          if (!g_mouse_look_initialized)
          {
            g_last_mouse_x = mx;
            g_last_mouse_y = my;
            g_mouse_look_initialized = true;
          }
          else
          {
            // Calculate relative mouse movement
            int dx = mx - g_last_mouse_x;
            int dy = my - g_last_mouse_y;

            // Update camera yaw and pitch based on mouse movement
            // Sensitivity: adjust these values to control look speed
            const float yaw_sensitivity = 0.5f;   // degrees per pixel
            const float pitch_sensitivity = 0.3f; // degrees per pixel

            // Update camera yaw (left/right look)
            g_fp_camera.yaw += dx * yaw_sensitivity;

            // Update camera pitch (up/down look) with clamping
            g_fp_camera.pitch += dy * pitch_sensitivity;
            if (g_fp_camera.pitch > 89.0f)
              g_fp_camera.pitch = 89.0f; // Limit looking up
            if (g_fp_camera.pitch < -89.0f)
              g_fp_camera.pitch = -89.0f; // Limit looking down

            // Keep yaw in reasonable range (0-360 degrees)
            while (g_fp_camera.yaw >= 360.0f)
              g_fp_camera.yaw -= 360.0f;
            while (g_fp_camera.yaw < 0.0f)
              g_fp_camera.yaw += 360.0f;

            printf("[editor] Mouse look: yaw=%.1f, pitch=%.1f (dx=%d, dy=%d)\n", g_fp_camera.yaw, g_fp_camera.pitch, dx, dy);

            // Update last mouse position
            g_last_mouse_x = mx;
            g_last_mouse_y = my;
          }
        }
        // Determine if mouse is over PIP; skip world hover/picking if so
        bool over_pip = false;
        {
          int ww_sz = WINDOW_W, wh_sz = WINDOW_H;
          SDL_GetWindowSize(win, &ww_sz, &wh_sz);
          int pw_sz = (int)(ww_sz * 0.28f);
          if (pw_sz < 160)
            pw_sz = 160;
          int ph_sz = (int)(wh_sz * 0.28f);
          if (ph_sz < 120)
            ph_sz = 120;
          SDL_Rect pipr = {ww_sz - pw_sz - 12, wh_sz - ph_sz - 12, pw_sz, ph_sz};
          if (mx >= pipr.x && mx < pipr.x + pipr.w && my >= pipr.y && my < pipr.y + pipr.h)
            over_pip = true;
        }
        // Hover palette or composition graph to mass-highlight type
        bool over_palette = false;
        for (int i = 0; i < g_palette_rect_count; i++)
        {
          SDL_Rect r = g_palette_rects[i];
          if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
          {
            isometric_renderer_set_highlight_type(ir, i);
            over_palette = true;
            break;
          }
        }
        if (!over_palette && !over_pip)
        {
          for (int i = 0; i < (int)VOXEL_COUNT; i++)
          {
            SDL_Rect r = g_chart_rects[i];
            if (r.w > 0 && r.h > 0 && mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
            {
              isometric_renderer_set_highlight_type(ir, i);
              g_mass_highlight_type = i;
              over_palette = true;
              g_palette_rarity_mode = true;
              break;
            }
          }
        }
        // No ellipsis control anymore; rarity mode is toggled elsewhere if needed
        if (!over_palette)
        {
          uint32_t vx, vy, vz;
          if (isometric_renderer_pick_voxel(ir, world, current_z, mx, my, &vx, &vy, &vz))
          {
            // World hover should highlight only the single voxel under the cursor
            isometric_renderer_clear_highlight_type(ir);
            isometric_renderer_set_highlighted_voxel(ir, (int)vx, (int)vy, (int)vz);
            g_hover_voxel_valid = true;
            g_hover_vx = (int)vx;
            g_hover_vy = (int)vy;
            g_hover_vz = (int)vz;
          }
          else
          {
            isometric_renderer_clear_highlight_type(ir);
            g_hover_voxel_valid = false;
          }
        }
        // Palette visibility follows hover over chart area, chart segments, or palette.
        // Also block world hover/picking when mouse is over the palette panel.
        bool over_chart_area = false;

        // Check if mouse is over the overall chart container
        if (g_have_counts && g_chart_container_rect.w > 0 && g_chart_container_rect.h > 0)
        {
          printf("[DEBUG] Chart container: rect(%d,%d,%d,%d), mouse(%d,%d), g_have_counts=%d\n",
                 g_chart_container_rect.x, g_chart_container_rect.y,
                 g_chart_container_rect.w, g_chart_container_rect.h, mx, my, g_have_counts);
          if (mx >= g_chart_container_rect.x && mx < g_chart_container_rect.x + g_chart_container_rect.w &&
              my >= g_chart_container_rect.y && my < g_chart_container_rect.y + g_chart_container_rect.h)
          {
            printf("[DEBUG] Hover over chart container!\n");
            over_chart_area = true;
          }
        }

        // Check if mouse is over any individual chart segments
        if (!over_chart_area && g_have_counts)
        {
          printf("[DEBUG] Checking chart segments, g_have_counts=%d\n", g_have_counts);
          for (int i = 0; i < (int)VOXEL_COUNT; i++)
          {
            SDL_Rect r = g_chart_rects[i];
            if (r.w > 0 && r.h > 0)
            {
              printf("[DEBUG] Segment %d: rect(%d,%d,%d,%d), mouse(%d,%d)\n",
                     i, r.x, r.y, r.w, r.h, mx, my);
              if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
              {
                printf("[DEBUG] Hover over chart segment %d!\n", i);
                over_chart_area = true;
                break;
              }
            }
          }
        }

        // Check if mouse is over any palette rectangles (for blocking world hover)
        bool over_palette_area = false;
        for (int i = 0; i < g_palette_rect_count; i++)
        {
          SDL_Rect r = g_palette_rects[i];
          if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
          {
            over_palette_area = true;
            break;
          }
        }

        // Palette is visible when hovering over chart area or chart segments
        g_palette_visible = over_chart_area;
        if (g_palette_visible &&
            mx >= g_palette_panel_rect.x && mx < g_palette_panel_rect.x + g_palette_panel_rect.w &&
            my >= g_palette_panel_rect.y && my < g_palette_panel_rect.y + g_palette_panel_rect.h)
        {
          // When pointer is over palette panel, don't pass hover to world
          isometric_renderer_clear_highlight_type(ir);
          isometric_renderer_clear_highlighted_voxel(ir);
          g_hover_voxel_valid = false;
          continue;
        }
        if (!over_palette_area)
        {
          g_mass_highlight_type = -1;
        }
      }
      else if (e.type == SDL_MOUSEWHEEL)
      {
        // Adjust thickness with mouse wheel when drawing cube with first corner set and Shift held
        if (g_current_tool == TOOL_SHAPE && g_shape_mode == SHAPE_CUBE && g_shape_has_first)
        {
          const SDL_Keymod mods = SDL_GetModState();
          if ((mods & KMOD_SHIFT) != 0)
          {
            int delta = e.wheel.y; // positive = scroll up
            if (delta != 0)
            {
              int new_thickness = g_shape_thickness + delta;
              if (new_thickness < 1)
                new_thickness = 1;
              // Clamp to world depth from base z
              int max_thickness = (int)world->depth - (int)g_shape_z1;
              if (max_thickness < 1)
                max_thickness = 1;
              if (new_thickness > max_thickness)
                new_thickness = max_thickness;
              g_shape_thickness = new_thickness;
            }
          }
        }
      }
      else if (e.type == SDL_MOUSEBUTTONDOWN)
      {
        // Game mouse handling (UI mouse handling is done by world_editor_ui_handle_event)
        // Example: left click toggles voxel between AIR and GRASS on current slice
        int mx = e.button.x;
        int my = e.button.y;

        printf("[DEBUG] Mouse click detected: (%d, %d)\n", mx, my);
        // Actor panel: select actor row and consume
        if (mx >= g_actor_panel_rect.x && mx < g_actor_panel_rect.x + g_actor_panel_rect.w &&
            my >= g_actor_panel_rect.y && my < g_actor_panel_rect.y + g_actor_panel_rect.h)
        {
          printf("[DEBUG] Click in actor panel area: panel(%d,%d,%d,%d), mouse(%d,%d), item_count=%d\n",
                 g_actor_panel_rect.x, g_actor_panel_rect.y, g_actor_panel_rect.w, g_actor_panel_rect.h, mx, my, g_actor_item_count);
          for (int i = 0; i < g_actor_item_count; i++)
          {
            SDL_Rect r = g_actor_item_rects[i];
            printf("[DEBUG] Checking item %d: rect(%d,%d,%d,%d)\n", i, r.x, r.y, r.w, r.h);
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
            {
              printf("[DEBUG] Actor %d selected!\n", i);
              g_controlled_actor_index = i;
              g_follow_actor_camera = true;
              // Also select the actor's voxel for the details panel
              g_selected_actor_index = i;
              if (i >= 0 && i < g_actor_count)
              {
                Actor *a = &g_actors[i];
                if (a && a->is_active)
                {
                  int ax = (int)floor(a->x + 0.0001);
                  int ay = (int)floor(a->y + 0.0001);
                  int az = (int)floor(a->z + 0.0001);
                  g_selected_vx = ax;
                  g_selected_vy = ay;
                  g_selected_vz = az;
                  g_selected_voxel_valid = true;
                }
              }
              break;
            }
          }
          continue;
        }
        // Left Z swatch column: jump to clicked Z level
        for (int i = 0; i < g_left_z_swatch_count; i++)
        {
          SDL_Rect r = g_left_z_swatch_rects[i];
          if (r.w > 0 && mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
          {
            current_z = g_left_z_swatch_z[i];
            ir->camera_z = current_z;
            continue;
          }
        }
        // Adjacent worlds button
        if (mx >= g_btn_adjacent_rect.x && mx < g_btn_adjacent_rect.x + g_btn_adjacent_rect.w && my >= g_btn_adjacent_rect.y && my < g_btn_adjacent_rect.y + g_btn_adjacent_rect.h)
        {
          g_adj_menu_open = !g_adj_menu_open;
          continue;
        }

        // Adjacent direction buttons when menu is open
        if (g_adj_menu_open)
        {
          (void)0; // labels array not used in this version
          int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
          for (int i = 0; i < 6; i++)
          {
            SDL_Rect r = g_adj_buttons[i];
            if (r.w > 0 && mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
            {
              // Generate/attach via universe policy relative to origin
              int dx = dirs[i][0], dy = dirs[i][1], dz = dirs[i][2];
              const char *seed0 = (g_universe_seed[0] ? g_universe_seed : (world && world->seed_id[0] ? world->seed_id : "editor"));
              (void)universe_generate_neighbors_wfc_around(&g_editor_universe, seed0, (uint64_t)g_home_gx, (uint64_t)g_home_gy, 0, world, UNIVERSE_GEN_GAMEWORLD, true, true);
              int idx_attach = isometric_renderer_offset_index(ir, dx, dy, dz);
              if (idx_attach > 0)
              {
                World *nw = universe_get(&g_editor_universe, (uint64_t)((int64_t)g_home_gx + dx), (uint64_t)((int64_t)g_home_gy + dy), (uint64_t)dz);
                if (nw)
                  ir->edge_worlds[idx_attach] = nw;
              }
              // Retarget camera toward the new world center
              ir->camera_x = (int)(dx * (int)world->width + world->width / 2);
              ir->camera_y = (int)(dy * (int)world->height + world->height / 2);
              ir->camera_z = (int)(dz * (int)world->depth + world->depth / 2);
              g_adj_menu_open = false;
              break;
            }
          }
        }
        // Ellipsis removed
        // Tool buttons
        if (mx >= g_btn_cursor_rect.x && mx < g_btn_cursor_rect.x + g_btn_cursor_rect.w && my >= g_btn_cursor_rect.y && my < g_btn_cursor_rect.y + g_btn_cursor_rect.h)
        {
          g_current_tool = TOOL_CURSOR;
          continue;
        }
        if (mx >= g_btn_shape_rect.x && mx < g_btn_shape_rect.x + g_btn_shape_rect.w && my >= g_btn_shape_rect.y && my < g_btn_shape_rect.y + g_btn_shape_rect.h)
        {
          g_current_tool = TOOL_SHAPE;
          continue;
        }
        if (g_current_tool == TOOL_SHAPE)
        {
          if (mx >= g_btn_cube_rect.x && mx < g_btn_cube_rect.x + g_btn_cube_rect.w && my >= g_btn_cube_rect.y && my < g_btn_cube_rect.y + g_btn_cube_rect.h)
          {
            g_shape_mode = SHAPE_CUBE;
            continue;
          }
          if (mx >= g_btn_sphere_rect.x && mx < g_btn_sphere_rect.x + g_btn_sphere_rect.w && my >= g_btn_sphere_rect.y && my < g_btn_sphere_rect.y + g_btn_sphere_rect.h)
          {
            g_shape_mode = SHAPE_SPHERE;
            continue;
          }
        }
        // Block clicks to world when over palette panel
        if (g_palette_visible &&
            mx >= g_palette_panel_rect.x && mx < g_palette_panel_rect.x + g_palette_panel_rect.w &&
            my >= g_palette_panel_rect.y && my < g_palette_panel_rect.y + g_palette_panel_rect.h)
        {
          // Allow palette item clicks but do not let it fall through to world
          if (handle_palette_click(mx, my))
            ; // handled
          else
            ; // ignore
        }
        // Toggle swap when clicking PIP box (bottom-right) on mouse down
        {
          int ww_click = 0, wh_click = 0;
          SDL_GetWindowSize(win, &ww_click, &wh_click);
          int pw = (int)(ww_click * 0.28f);
          if (pw < 160)
            pw = 160;
          int ph = (int)(wh_click * 0.28f);
          if (ph < 120)
            ph = 120;
          int px = ww_click - pw - 12;
          int py = wh_click - ph - 12;
          SDL_Rect pip = {px, py, pw, ph};
          if (mx >= pip.x && mx < pip.x + pip.w && my >= pip.y && my < pip.y + pip.h)
          {
            g_swap_views = !g_swap_views;
            continue; // immediately consume; avoid further handling
          }
        }
        // Palette click takes precedence
        if (handle_palette_click(mx, my))
        {
          // brush changed
          mru_push(g_brush_type);
        }
        else
        {
          // Clicking the world selects voxel for details; deselect actor control
          g_controlled_actor_index = -1;
          g_follow_actor_camera = false;
          // If chat is open and click within chat box, focus stays; otherwise proceed
          // Accurate isometric picking
          uint32_t vx, vy, vz;
          if (isometric_renderer_pick_voxel(ir, world, current_z, mx, my, &vx, &vy, &vz))
          {
            g_selected_voxel_valid = true;
            g_selected_vx = (int)vx;
            g_selected_vy = (int)vy;
            g_selected_vz = (int)vz;
            g_selected_actor_index = -1;
            // If selecting an actor voxel, find the actor index occupying this cell
            Voxel *sv = world_get_voxel(world, vx, vy, vz);
            if (sv && sv->type == VOXEL_ACTOR)
            {
              for (int ai = 0; ai < g_actor_count; ai++)
              {
                Actor *aa = &g_actors[ai];
                if (!aa || !aa->is_active)
                  continue;
                int ax = (int)floor(aa->x + 0.0001);
                int ay = (int)floor(aa->y + 0.0001);
                int az = (int)floor(aa->z + 0.0001);
                if (ax == (int)vx && ay == (int)vy && az == (int)vz)
                {
                  g_selected_actor_index = ai;
                  break;
                }
              }
            }
            if (g_current_tool == TOOL_CURSOR)
            {
              // Select mode: do not modify voxels; only selection handled above
            }
            else if (g_current_tool == TOOL_SHAPE)
            {
              if (!g_shape_has_first)
              {
                g_shape_has_first = true;
                g_shape_x1 = vx;
                g_shape_y1 = vy;
                g_shape_z1 = vz;
              }
              else
              {
                // Second click: draw shape from (x1,y1,z1) to (vx,vy,vz)
                uint32_t x0 = g_shape_x1, y0 = g_shape_y1, z0 = g_shape_z1;
                uint32_t x1 = vx, y1 = vy, z1 = vz;
                if (x1 < x0)
                {
                  uint32_t t = x0;
                  x0 = x1;
                  x1 = t;
                }
                if (y1 < y0)
                {
                  uint32_t t = y0;
                  y0 = y1;
                  y1 = t;
                }
                if (z1 < z0)
                {
                  uint32_t t = z0;
                  z0 = z1;
                  z1 = t;
                }
                if (g_shape_mode == SHAPE_CUBE)
                {
                  // If Shift is held during the second click, extend along +Z from first click
                  const SDL_Keymod mods = SDL_GetModState();
                  bool extend_in_z = (mods & KMOD_SHIFT) != 0;
                  if (extend_in_z)
                  {
                    // Interpret second click as X/Y opposite corner; use thickness along +Z
                    uint32_t base_z0 = g_shape_z1;
                    uint32_t base_z1 = base_z0 + (g_shape_thickness > 0 ? (uint32_t)g_shape_thickness - 1U : 0U);
                    if (base_z1 >= world->depth)
                      base_z1 = world->depth - 1U;
                    z0 = base_z0;
                    z1 = base_z1;
                  }
                  for (uint32_t z = z0; z <= z1; z++)
                    for (uint32_t y = y0; y <= y1; y++)
                      for (uint32_t x = x0; x <= x1; x++)
                      {
                        Voxel *vv = world_get_voxel(world, x, y, z);
                        if (vv)
                          vv->type = g_brush_type;
                      }
                }
                else // SHAPE_SPHERE: cube bounds then fill if within ellipsoid
                {
                  float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, cz = (z0 + z1) * 0.5f;
                  float rx = (x1 - x0) * 0.5f + 0.0001f;
                  float ry = (y1 - y0) * 0.5f + 0.0001f;
                  float rz = (z1 - z0) * 0.5f + 0.0001f;
                  // If Shift is held, treat second click as XY only and use thickness for Z extent
                  const SDL_Keymod mods2 = SDL_GetModState();
                  if ((mods2 & KMOD_SHIFT) != 0)
                  {
                    uint32_t base_z0 = g_shape_z1;
                    uint32_t base_z1 = base_z0 + (g_shape_thickness > 0 ? (uint32_t)g_shape_thickness - 1U : 0U);
                    if (base_z1 >= world->depth)
                      base_z1 = world->depth - 1U;
                    z0 = base_z0;
                    z1 = base_z1;
                    cz = (z0 + z1) * 0.5f;
                    rz = (z1 - z0) * 0.5f + 0.0001f;
                  }
                  for (uint32_t z = z0; z <= z1; z++)
                    for (uint32_t y = y0; y <= y1; y++)
                      for (uint32_t x = x0; x <= x1; x++)
                      {
                        float dx = (x - cx) / rx, dy = (y - cy) / ry, dz = (z - cz) / rz;
                        if (dx * dx + dy * dy + dz * dz <= 1.0f)
                        {
                          Voxel *vv = world_get_voxel(world, x, y, z);
                          if (vv)
                            vv->type = g_brush_type;
                        }
                      }
                }
                g_shape_has_first = false;
                mru_push(g_brush_type);
              }
            }
          }
        }
      }
      // Integrate any completed background-generated worlds into the renderer/universe
      gen_integrate_completed(ir, &world);
    }

    // Update the input system (reset just_pressed/just_released flags)
    if (g_input_system)
    {
      world_editor_input_update(g_input_system);
    }

    // Tick runtime clock for terrain/physics epochs
    Uint32 now_ticks = SDL_GetTicks();
    if (g_clock_last_tick == 0)
      g_clock_last_tick = now_ticks;
    if (g_clock_running)
    {
      Uint32 dt_ms = (now_ticks - g_clock_last_tick);
      g_clock_ms += (uint64_t)((unsigned long long)dt_ms * g_time_factor);
      // Step physics in world instead of editor local loop when runtime actors are provided
      world->runtime_actors = g_actors;
      world->runtime_actor_count = g_actor_count;
      world->runtime_actor_capacity = (int)(sizeof(g_actors) / sizeof(g_actors[0]));
      world_step_actors(world, (float)dt_ms / 1000.0f);
      // Epoch every 10 minutes
      uint32_t new_epoch = (uint32_t)(g_clock_ms / 600000ULL);
      if (new_epoch > g_clock_epoch_index)
      {
        g_clock_epoch_index = new_epoch;
        // Advance world epoch hook (no patch output consumed here)
        (void)world_advance_epoch(world, g_clock_epoch_index);
        // Start epoch flash and toast (no statuses in toast text)
        g_epoch_flash_start = now_ticks;
        snprintf(g_toast_buf, sizeof(g_toast_buf), "Epoch %u", g_clock_epoch_index);
        g_toast_until = now_ticks + 1200; // ~1.2s
        // Build history line (statuses omitted in editor build)
        char hist_line[128];
        snprintf(hist_line, sizeof(hist_line), "Epoch %u", g_clock_epoch_index);
        // Push to toast history (most recent first)
        if (g_toast_history_count < TOAST_HISTORY_MAX)
          g_toast_history_count++;
        for (int i = g_toast_history_count - 1; i > 0; i--)
          strcpy(g_toast_history[i], g_toast_history[i - 1]);
        strncpy(g_toast_history[0], hist_line, sizeof(g_toast_history[0]) - 1);
        g_toast_history[0][sizeof(g_toast_history[0]) - 1] = '\0';
      }

      // Physics: one budgeted fluid step per PHYSICS_TICK_MS of simulated T time.
      unsigned long long current_ms = g_clock_ms;
      if (!g_physics_init)
      {
        g_physics_last_ms = current_ms; // start from now so enabling the clock does not backfill
        g_physics_init = true;
      }
      int steps = 0;
      while (g_physics_last_ms + PHYSICS_TICK_MS <= current_ms && steps < PHYSICS_MAX_STEPS_PER_FRAME)
      {
        g_physics_last_ms += PHYSICS_TICK_MS;
        editor_physics_step(world, g_physics_last_ms * 1000ULL);
        if (ns)
          ns_step(ns, (double)PHYSICS_TICK_MS / 1000.0);
        steps++;
      }
      // Hitch or time-acceleration debt: snap forward so the next frame is not a catch-up storm.
      if (g_physics_last_ms + PHYSICS_TICK_MS <= current_ms)
        g_physics_last_ms = current_ms;
      // Occupancy for rendering is maintained incrementally by fluid writes. A full-volume rebuild of
      // the GPU bit stub here used to cost hundreds of ms on 128³ and is not needed every tick.
    }
    // Handle time acceleration holds (one 10x change per real second held)
    if (g_accel_increase_held && !g_accel_decrease_held)
    {
      if (g_accel_increase_last_change == 0)
        g_accel_increase_last_change = now_ticks;
      Uint32 elapsed = now_ticks - g_accel_increase_last_change;
      if (elapsed >= 1000)
      {
        Uint32 steps = elapsed / 1000;
        for (Uint32 s = 0; s < steps; s++)
        {
          if (g_time_factor < MAX_TIME_FACTOR)
          {
            unsigned long long nf = (g_time_factor == 0ULL ? 10ULL : g_time_factor * 10ULL);
            if (nf > MAX_TIME_FACTOR)
              nf = MAX_TIME_FACTOR;
            g_time_factor = nf;
          }
        }
        g_accel_increase_last_change += steps * 1000;
      }
    }
    else if (g_accel_decrease_held && !g_accel_increase_held)
    {
      if (g_accel_decrease_last_change == 0)
        g_accel_decrease_last_change = now_ticks;
      Uint32 elapsed = now_ticks - g_accel_decrease_last_change;
      if (elapsed >= 1000)
      {
        Uint32 steps = elapsed / 1000;
        for (Uint32 s = 0; s < steps; s++)
        {
          if (g_time_factor > 1ULL)
          {
            unsigned long long nf = (g_time_factor / 10ULL);
            if (nf < 1ULL)
              nf = 1ULL;
            g_time_factor = nf;
          }
        }
        g_accel_decrease_last_change += steps * 1000;
      }
    }
    g_clock_last_tick = now_ticks;

    // Render main view and PIP, swapping if requested
    int ww = WINDOW_W, wh = WINDOW_H;
    SDL_GetWindowSize(win, &ww, &wh);
    int pip_w = (int)(ww * 0.28f);
    if (pip_w < 160)
      pip_w = 160;
    int pip_h = (int)(wh * 0.28f);
    if (pip_h < 120)
      pip_h = 120;
    int pip_x = ww - pip_w - 12;
    int pip_y = wh - pip_h - 12; // bottom-right
    bool have_fp = false;
    FPCamera cam = (FPCamera){0};
    if (g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count && g_actors[g_controlled_actor_index].is_active)
    {
      // Follow the controlled actor
      const Actor *a = &g_actors[g_controlled_actor_index];
      cam.fov_deg = 60.0f;
      cam.x = (float)a->x;
      cam.y = (float)a->y;
      cam.z = (float)a->z; // First-person camera at actor's exact position

      // Initialize orientation if this is the first frame or if mouse look is disabled
      if (g_fp_camera.fov_deg == 0.0f || !g_mouse_look_enabled)
      {
        cam.yaw = 90.0f;   // Face north (up arrow = forward)
        cam.pitch = 0.0f;  // Look horizontal (same as universe-ref)
        g_fp_camera = cam; // Full sync
      }
      else
      {
        // Preserve mouse look orientation, update position only
        g_fp_camera.fov_deg = cam.fov_deg;
        g_fp_camera.x = cam.x;
        g_fp_camera.y = cam.y;
        g_fp_camera.z = cam.z;
        // Keep existing yaw and pitch from mouse look
      }

      have_fp = true;
      // Debug: FP camera info removed
    }
    else if (g_actor_count > 0)
    {
      // Fallback to first actor if none selected
      const Actor *a = &g_actors[0];
      cam.fov_deg = 60.0f;
      cam.x = (float)a->x;
      cam.y = (float)a->y;
      cam.z = (float)a->z; // First-person camera at actor's exact position

      // Initialize orientation if this is the first frame or if mouse look is disabled
      if (g_fp_camera.fov_deg == 0.0f || !g_mouse_look_enabled)
      {
        cam.yaw = 90.0f;   // Face north (up arrow = forward)
        cam.pitch = 0.0f;  // Look horizontal (same as universe-ref)
        g_fp_camera = cam; // Full sync
      }
      else
      {
        // Preserve mouse look orientation, update position only
        g_fp_camera.fov_deg = cam.fov_deg;
        g_fp_camera.x = cam.x;
        g_fp_camera.y = cam.y;
        g_fp_camera.z = cam.z;
        // Keep existing yaw and pitch from mouse look
      }

      have_fp = true;
      // Debug: FP fallback camera info removed
    }
    else
    {
      // Fallback: place camera at spawn location (center of world, above terrain)
      int cx = (int)(world->width / 2);
      int cy = (int)(world->height / 2);
      int az = find_highest_air_z(world, cx, cy);
      if (az < 0)
      {
        // If no air found, place above highest solid, or at mid-height if none
        int hz = find_highest_solid_z(world, cx, cy);
        az = (hz >= 0) ? (hz + 1) : (int)(world->depth / 2);
      }

      cam.fov_deg = 60.0f;
      cam.x = (float)cx + 0.5f; // Center X
      cam.y = (float)cy + 0.5f; // Center Y
      cam.z = (float)az + 1.5f; // At air height (same as universe-ref)

      // Initialize orientation if this is the first frame or if mouse look is disabled
      if (g_fp_camera.fov_deg == 0.0f || !g_mouse_look_enabled)
      {
        cam.yaw = 90.0f;   // Face north (up arrow = forward)
        cam.pitch = 0.0f;  // Look horizontal (same as universe-ref)
        g_fp_camera = cam; // Full sync
      }
      else
      {
        // Preserve mouse look orientation, update position only
        g_fp_camera.fov_deg = cam.fov_deg;
        g_fp_camera.x = cam.x;
        g_fp_camera.y = cam.y;
        g_fp_camera.z = cam.z;
        // Keep existing yaw and pitch from mouse look
      }

      have_fp = true;
      // Debug: FP spawn camera info removed
    }
    if (!g_swap_views)
    {
      // Main: isometric; PIP: FP (if available)
      // Clear the background manually first
      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(ren, 20, 22, 35, 255); // Same blue as isometric renderer
      SDL_RenderClear(ren);

      // Disable clear so isometric doesn't clear again
      bool prev_allow_clear = ir->allow_clear;
      ir->allow_clear = false;
      isometric_renderer_render_gpu(ir, ren);

      // Rely on renderer's internal white outline for highlighted voxel; avoid duplicate overlay here

      // Draw cube preview when using Draw tool with Cube mode and first corner set
      if (g_current_tool == TOOL_SHAPE && g_shape_mode == SHAPE_CUBE && g_shape_has_first)
      {
        // First corner at (g_shape_x1,g_shape_y1,g_shape_z1). Determine second corner from current mouse.
        uint32_t x0 = g_shape_x1, y0 = g_shape_y1, z0 = g_shape_z1;
        int mx_cur, my_cur;
        SDL_GetMouseState(&mx_cur, &my_cur);
        uint32_t pvx = (uint32_t)g_hover_vx, pvy = (uint32_t)g_hover_vy, pvz = (uint32_t)g_hover_vz;
        // Try direct pick regardless of palette/chart hover gating
        if (!isometric_renderer_pick_voxel(ir, world, current_z, mx_cur, my_cur, &pvx, &pvy, &pvz))
        {
          // Fallback to last hover if available; otherwise center
          if (!g_hover_voxel_valid)
          {
            pvx = (uint32_t)(world->width / 2);
            pvy = (uint32_t)(world->height / 2);
            pvz = (uint32_t)current_z;
          }
        }
        uint32_t x1 = pvx, y1 = pvy, z1 = pvz;
        if (x1 < x0) { uint32_t t = x0; x0 = x1; x1 = t; }
        if (y1 < y0) { uint32_t t = y0; y0 = y1; y1 = t; }
        if (z1 < z0) { uint32_t t = z0; z0 = z1; z1 = t; }

        // If Shift is held, preview uses thickness along +Z from first click
        const SDL_Keymod mods = SDL_GetModState();
        if ((mods & KMOD_SHIFT) != 0)
        {
          uint32_t base_z0 = g_shape_z1;
          uint32_t base_z1 = base_z0 + (g_shape_thickness > 0 ? (uint32_t)g_shape_thickness - 1U : 0U);
          if (base_z1 >= world->depth) base_z1 = world->depth - 1U;
          z0 = base_z0; z1 = base_z1;
        }
        else
        {
          // No shift: respect the hover-picked z extent
        }

        // Determine world index for origin world in renderer
        int world_index = isometric_renderer_offset_index(ir, 0, 0, 0);
        if (world_index < 0) world_index = 0;

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        // Extend max edges by +1 so outline sits on voxel boundaries
        uint32_t ex0f = x0, ey0f = y0, ez0f = z0;
        uint32_t ex1f = x1 + 1U, ey1f = y1 + 1U, ez1f = z1 + 1U;
        isometric_renderer_draw_box_wireframe_centered(ir, ren,
                                                       (int)ex0f, (int)ey0f, (int)ez0f,
                                                       (int)ex1f, (int)ey1f, (int)ez1f,
                                                       (SDL_Color){255, 255, 255, 200},
                                                       -1, -1, true);

        // Draw dimensions/thickness label near top-front corner
        uint32_t dx = (x1 >= x0 ? (x1 - x0 + 1U) : (x0 - x1 + 1U));
        uint32_t dy = (y1 >= y0 ? (y1 - y0 + 1U) : (y0 - y1 + 1U));
        uint32_t dz = (z1 >= z0 ? (z1 - z0 + 1U) : (z0 - z1 + 1U));
        char box_info[64];
        snprintf(box_info, sizeof(box_info), "%ux%ux%u  (Shift+Wheel thickness=%d)", (unsigned)dx, (unsigned)dy, (unsigned)dz, g_shape_thickness);
        int label_sx, label_sy;
        // Use the top face front-right corner (x1,y0,z1) as anchor
        isometric_world_to_screen(ir, (int)x1, (int)y0, (int)z1, world_index, &label_sx, &label_sy);
        label_sy -= 14; // nudge upward for readability
        if (font)
          draw_text(ren, font, box_info, label_sx, label_sy, (SDL_Color){255, 255, 255, 200});
      }
      ir->allow_clear = prev_allow_clear;
      if (have_fp)
      {
        // Use static mesh - build only once
        static VoxelMesh static_mesh = {0};
        static bool mesh_built = false;

        if (!mesh_built)
        {
          // Only build mesh if world has content (not empty)
          if (g_fp_gameworlds.home_world && g_fp_gameworlds.home_world->voxels)
          {
            // Check if world has any non-air voxels
            bool has_content = false;
            size_t total_voxels = (size_t)g_fp_gameworlds.home_world->width *
                                  (size_t)g_fp_gameworlds.home_world->height *
                                  (size_t)g_fp_gameworlds.home_world->depth;
            for (size_t i = 0; i < total_voxels && !has_content; i++)
            {
              if (g_fp_gameworlds.home_world->voxels[i].type != VOXEL_AIR)
              {
                has_content = true;
                break;
              }
            }

            if (has_content)
            {
              voxel_mesh_build_all_faces_greedy(g_fp_gameworlds.home_world, &static_mesh);
              fp_renderer_set_cached_mesh(g_fp_gameworlds.home_world, &static_mesh);
            }
          }
          mesh_built = true;
        }

        // Only render FP view if world has content
        if (g_fp_gameworlds.home_world && g_fp_gameworlds.home_world->voxels)
        {
          // Check if world has any non-air voxels
          bool has_content = false;
          size_t total_voxels = (size_t)g_fp_gameworlds.home_world->width *
                                (size_t)g_fp_gameworlds.home_world->height *
                                (size_t)g_fp_gameworlds.home_world->depth;
          for (size_t i = 0; i < total_voxels && i < 1000; i++) // Sample first 1000 for speed
          {
            if (g_fp_gameworlds.home_world->voxels[i].type != VOXEL_AIR)
            {
              has_content = true;
              break;
            }
          }

          if (has_content)
          {
            // Set viewport for first-person renderer to ensure it only draws in PIP area
            SDL_Rect fp_viewport = {pip_x, pip_y, pip_w, pip_h};
            SDL_RenderSetViewport(ren, &fp_viewport);

            // Render using already cached mesh
            fp_renderer_render(ren, g_fp_gameworlds.home_world, &g_fp_camera, 0, 0, pip_w, pip_h, NULL);

            // Reset viewport after FP rendering
            SDL_RenderSetViewport(ren, NULL);
          }
        }
      }
    }
    else
    {
      // Main: FP full-screen; PIP: iso in the PIP box
      // Clear background for full-screen FP
      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      SDL_RenderClear(ren);
      if (have_fp)
      {
        // Use static mesh - build only once
        static VoxelMesh static_mesh_fullscreen = {0};
        static bool mesh_built_fullscreen = false;

        if (!mesh_built_fullscreen)
        {
          // Only build mesh if world has content (not empty)
          if (g_fp_gameworlds.home_world && g_fp_gameworlds.home_world->voxels)
          {
            // Check if world has any non-air voxels
            bool has_content = false;
            size_t total_voxels = (size_t)g_fp_gameworlds.home_world->width *
                                  (size_t)g_fp_gameworlds.home_world->height *
                                  (size_t)g_fp_gameworlds.home_world->depth;
            for (size_t i = 0; i < total_voxels && !has_content; i++)
            {
              if (g_fp_gameworlds.home_world->voxels[i].type != VOXEL_AIR)
              {
                has_content = true;
                break;
              }
            }

            if (has_content)
            {
              voxel_mesh_build_all_faces_greedy(g_fp_gameworlds.home_world, &static_mesh_fullscreen);
              fp_renderer_set_cached_mesh(g_fp_gameworlds.home_world, &static_mesh_fullscreen);
            }
          }
          mesh_built_fullscreen = true;
        }

        // Only render FP view if world has content
        if (g_fp_gameworlds.home_world && g_fp_gameworlds.home_world->voxels)
        {
          // Check if world has any non-air voxels
          bool has_content = false;
          size_t total_voxels = (size_t)g_fp_gameworlds.home_world->width *
                                (size_t)g_fp_gameworlds.home_world->height *
                                (size_t)g_fp_gameworlds.home_world->depth;
          for (size_t i = 0; i < total_voxels && i < 1000; i++) // Sample first 1000 for speed
          {
            if (g_fp_gameworlds.home_world->voxels[i].type != VOXEL_AIR)
            {
              has_content = true;
              break;
            }
          }

          if (has_content)
          {
            // Set viewport for full-screen first-person renderer
            SDL_Rect fp_viewport = {0, 0, ww, wh};
            SDL_RenderSetViewport(ren, &fp_viewport);

            // Render using already cached mesh
            fp_renderer_render(ren, g_fp_gameworlds.home_world, &g_fp_camera, 0, 0, ww, wh, NULL);

            // Reset viewport after FP rendering
            SDL_RenderSetViewport(ren, NULL);
          }
        }

        // Reset SDL render state after FP renderer (which uses OpenGL)
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      }
      // Draw iso into PIP using viewport so its clear only affects PIP region
      SDL_Rect vp = {pip_x, pip_y, pip_w, pip_h};
      SDL_RenderSetViewport(ren, &vp);
      bool prev_allow_clear = ir->allow_clear;
      ir->allow_clear = false;
      SDL_RenderSetClipRect(ren, &vp);
      isometric_renderer_render_gpu(ir, ren);

      // While viewport is active, draw shape preview aligned to PIP (use viewport-local coords)
      if (g_current_tool == TOOL_SHAPE && g_shape_mode == SHAPE_CUBE && g_shape_has_first)
      {
        uint32_t x0 = g_shape_x1, y0 = g_shape_y1, z0 = g_shape_z1;
        int mx_cur, my_cur;
        SDL_GetMouseState(&mx_cur, &my_cur);
        uint32_t pvx = (uint32_t)g_hover_vx, pvy = (uint32_t)g_hover_vy, pvz = (uint32_t)g_hover_vz;
        if (!isometric_renderer_pick_voxel(ir, world, current_z, mx_cur, my_cur, &pvx, &pvy, &pvz))
        {
          if (!g_hover_voxel_valid)
          {
            pvx = (uint32_t)(world->width / 2);
            pvy = (uint32_t)(world->height / 2);
            pvz = (uint32_t)current_z;
          }
        }
        uint32_t x1 = pvx, y1 = pvy, z1 = pvz;
        if (x1 < x0) { uint32_t t = x0; x0 = x1; x1 = t; }
        if (y1 < y0) { uint32_t t = y0; y0 = y1; y1 = t; }
        if (z1 < z0) { uint32_t t = z0; z0 = z1; z1 = t; }
        const SDL_Keymod mods = SDL_GetModState();
        if ((mods & KMOD_SHIFT) != 0)
        {
          uint32_t base_z0 = g_shape_z1;
          uint32_t base_z1 = base_z0 + (g_shape_thickness > 0 ? (uint32_t)g_shape_thickness - 1U : 0U);
          if (base_z1 >= world->depth) base_z1 = world->depth - 1U;
          z0 = base_z0; z1 = base_z1;
        }
        int world_index = isometric_renderer_offset_index(ir, 0, 0, 0);
        if (world_index < 0) world_index = 0;
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        // Align to voxel edges by extending max edges by +1 (min stays as-is)
        uint32_t ex0p = x0, ey0p = y0, ez0p = z0;
        uint32_t ex1p = x1 + 1U, ey1p = y1 + 1U, ez1p = z1 + 1U;
        // Use viewport-local center so overlay aligns with iso inside PIP
        int pip_cx = pip_x + pip_w / 2;
        int pip_cy = pip_y + pip_h / 2;
        isometric_renderer_draw_box_wireframe_centered(ir, ren,
                                                       (int)ex0p, (int)ey0p, (int)ez0p,
                                                       (int)ex1p, (int)ey1p, (int)ez1p,
                                                       (SDL_Color){255, 220, 0, 220},
                                                       pip_cx, pip_cy, true);
        // Label inside PIP
        uint32_t dxp = (ex1p - ex0p);
        uint32_t dyp = (ey1p - ey0p);
        uint32_t dzp = (ez1p - ez0p);
        char pip_info[64];
        snprintf(pip_info, sizeof(pip_info), "%ux%ux%u  (thickness=%d)", (unsigned)dxp, (unsigned)dyp, (unsigned)dzp, g_shape_thickness);
        int lsx, lsy;
        isometric_world_to_screen(ir, (int)ex1p, (int)ey0p, (int)ez1p, world_index, &lsx, &lsy);
        lsy -= 14;
        if (font)
          draw_text(ren, font, pip_info, lsx, lsy, (SDL_Color){255, 240, 200, 230});
      }

      // Reset viewport and clip rect before drawing border
      SDL_RenderSetViewport(ren, NULL);
      SDL_RenderSetClipRect(ren, NULL);

      // Draw PIP border using global coordinates
      SDL_SetRenderDrawColor(ren, 220, 220, 220, 200);
      SDL_RenderDrawRect(ren, &vp);
      ir->allow_clear = prev_allow_clear;
    }

    // Draw red boundary lines for any neighbor currently queued/generating
    {
      if (g_gen_mutex)
        SDL_LockMutex(g_gen_mutex);
      // Determine which cardinal neighbors are in-progress
      int card[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (int c = 0; c < 4; c++)
      {
        int dx = card[c][0], dy = card[c][1];
        uint64_t ax = (uint64_t)((int64_t)g_home_gx + dx);
        uint64_t ay = (uint64_t)((int64_t)g_home_gy + dy);
        int in_progress = 0;
        // Check if not present yet and either queued or building
        World *existing = universe_get(&g_editor_universe, ax, ay, 0);
        if (!existing)
        {
          for (int i = 0; i < (int)(sizeof(g_jobs) / sizeof(g_jobs[0])); i++)
          {
            if (g_jobs[i].used && g_jobs[i].gx == ax && g_jobs[i].gy == ay && g_jobs[i].gz == 0)
            {
              in_progress = 1;
              break;
            }
          }
          if (!in_progress)
          {
            for (int i = 0; i < (int)(sizeof(g_done) / sizeof(g_done[0])); i++)
            {
              if (g_done[i].used && g_done[i].gx == ax && g_done[i].gy == ay && g_done[i].gz == 0)
              {
                in_progress = 1;
                break;
              }
            }
          }
        }
        if (in_progress)
        {
          SDL_Color red = {255, 60, 60, 255};
          editor_draw_world_boundary(ren, ir, world, current_z, dx, dy, red);
        }
      }
      if (g_gen_mutex)
        SDL_UnlockMutex(g_gen_mutex);
    }

    // Bottom-center compass overlay (oriented by world axes using isometric projection)
    {
      int ww = WINDOW_W, wh = WINDOW_H;
      SDL_GetWindowSize(win, &ww, &wh);
      int cx = ww / 2;
      int base_y = wh - 36; // lift above bottom margin
      int len = 18;

      // Project local world basis at current camera Z to screen to get 2D directions
      int wx = ir->camera_x;
      int wy = ir->camera_y;
      int wz = current_z;
      int s0x = 0, s0y = 0, sNx = 0, sNy = 0, sEx = 0, sEy = 0;
      isometric_world_to_screen(ir, wx, wy, wz, 0, &s0x, &s0y);
      isometric_world_to_screen(ir, wx, wy - 1, wz, 0, &sNx, &sNy); // -Y projects up-right → treat as North
      isometric_world_to_screen(ir, wx + 1, wy, wz, 0, &sEx, &sEy); // +X = East

      float ndx = (float)(sNx - s0x);
      float ndy = (float)(sNy - s0y);
      float edx = (float)(sEx - s0x);
      float edy = (float)(sEy - s0y);
      float nlen = sqrtf(ndx * ndx + ndy * ndy);
      float elen = sqrtf(edx * edx + edy * edy);
      if (nlen < 1e-3f)
      {
        ndx = 0.0f;
        ndy = -1.0f;
        nlen = 1.0f;
      }
      if (elen < 1e-3f)
      {
        edx = 1.0f;
        edy = 0.0f;
        elen = 1.0f;
      }
      ndx /= nlen;
      ndy /= nlen;
      edx /= elen;
      edy /= elen;

      int nx = cx + (int)(ndx * len);
      int ny = base_y + (int)(ndy * len); // use projected screen delta directly
      int sx = cx - (int)(ndx * len);
      int sy = base_y - (int)(ndy * len);
      int ex = cx + (int)(edx * len);
      int ey = base_y + (int)(edy * len);
      int wx2 = cx - (int)(edx * len);
      int wy2 = base_y - (int)(edy * len);

      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(ren, 220, 220, 220, 220);
      // Axes lines
      SDL_RenderDrawLine(ren, sx, sy, nx, ny);   // S <-> N
      SDL_RenderDrawLine(ren, wx2, wy2, ex, ey); // W <-> E

      // North arrow head (perp to N dir)
      float pxv = -ndy; // perpendicular vector in screen space
      float pyv = ndx;
      int ah = 6;
      SDL_RenderDrawLine(ren, nx, ny, nx + (int)(pxv * ah), ny + (int)(pyv * ah));
      SDL_RenderDrawLine(ren, nx, ny, nx - (int)(pxv * ah), ny - (int)(pyv * ah));

      if (font)
      {
        draw_text(ren, font, "N", nx - 4, ny - 16, (SDL_Color){220, 220, 220, 255});
        draw_text(ren, font, "S", sx - 4, sy + 4, (SDL_Color){180, 180, 180, 255});
        draw_text(ren, font, "E", ex + 6, ey - 7, (SDL_Color){180, 180, 180, 255});
        draw_text(ren, font, "W", wx2 - 14, wy2 - 7, (SDL_Color){180, 180, 180, 255});
      }
    }

    // Bottom-right overlays: universe position above the FP panel and background generation status
    if (font)
    {
      int ww = WINDOW_W, wh = WINDOW_H;
      SDL_GetWindowSize(win, &ww, &wh);
      // Match FP panel placement to position labels just above it
      int pw = (int)(ww * 0.28f);
      if (pw < 160)
        pw = 160;
      int ph = (int)(wh * 0.28f);
      if (ph < 120)
        ph = 120;
      int px = ww - pw - 12;
      int py = wh - ph - 12;
      // Universe position line (x,y,z). We operate at z=0 in this editor view.
      char uni[64];
      snprintf(uni, sizeof(uni), "Universe: (%d,%d,%d)", g_home_gx, g_home_gy, 0);
      int tw1 = 0, th1 = 0;
      TTF_SizeText(font, uni, &tw1, &th1);
      int base_x = px + pw - tw1; // right-align above panel
      int base_y = py - th1 - 6;  // just above panel
      draw_text(ren, font, uni, base_x, base_y, (SDL_Color){220, 220, 220, 255});

      // Background generation pending count on the line above
      if (g_gen_mutex)
      {
        SDL_LockMutex(g_gen_mutex);
        int pending = 0;
        for (int i = 0; i < (int)(sizeof(g_jobs) / sizeof(g_jobs[0])); i++)
          if (g_jobs[i].used)
            pending++;
        SDL_UnlockMutex(g_gen_mutex);
        if (pending > 0)
        {
          char msg[64];
          snprintf(msg, sizeof(msg), "Generating worlds... (%d)", pending);
          int tw = 0, th = 0;
          TTF_SizeText(font, msg, &tw, &th);
          int x = px + pw - tw;    // right-align to panel
          int y = base_y - th - 6; // one line above the universe label
          draw_text(ren, font, msg, x, y, (SDL_Color){255, 220, 180, 255});
        }
      }
    }
    // If following a selected actor, keep camera centered on it after physics
    if (g_follow_actor_camera && g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count)
    {
      Actor *a = &g_actors[g_controlled_actor_index];
      if (a && a->is_active)
      {
        int ccx = (int)floor(a->x + 0.5);
        int ccy = (int)floor(a->y + 0.5);
        isometric_renderer_set_camera(ir, ccx, ccy, ir->camera_z);
      }
    }

    // Overlay: outline the top face of the solid voxel the selected actor stands on
    if (g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count)
    {
      Actor *a = &g_actors[g_controlled_actor_index];
      if (a && a->is_active && !a->is_flying)
      {
        int vx = (int)floor(a->x);
        int vy = (int)floor(a->y);
        int gz = (int)floor(a->z) - 1; // voxel directly under actor feet
        if (vx >= 0 && vy >= 0 && gz >= 0 &&
            vx < (int)world->width && vy < (int)world->height && gz < (int)world->depth)
        {
          Voxel *ground = world_get_voxel(world, (uint32_t)vx, (uint32_t)vy, (uint32_t)gz);
          if (ground && ground->type != VOXEL_AIR)
          {
            int zface = gz + 1; // top face height
            int cx, cy;
            isometric_world_to_screen(ir, vx, vy, zface, 0, &cx, &cy);
            const int half_tw = ir->tile_width / 2;
            const int half_th = ir->tile_height / 2;
            SDL_Point pL = {cx - half_tw, cy};
            SDL_Point pR = {cx + half_tw, cy};
            SDL_Point pT = {cx, cy - half_th};
            SDL_Point pB = {cx, cy + half_th};
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
            SDL_RenderDrawLine(ren, pT.x, pT.y, pR.x, pR.y);
            SDL_RenderDrawLine(ren, pR.x, pR.y, pB.x, pB.y);
            SDL_RenderDrawLine(ren, pB.x, pB.y, pL.x, pL.y);
            SDL_RenderDrawLine(ren, pL.x, pL.y, pT.x, pT.y);
          }
        }
      }
    }

    // Removed actor location labels above actors to avoid screen clutter

    // Hover tooltip: show voxel type and fields under cursor
    if (g_hover_voxel_valid)
    {
      // Suppress tooltip when mouse is over the PIP region
      int ww_sz = WINDOW_W, wh_sz = WINDOW_H;
      SDL_GetWindowSize(win, &ww_sz, &wh_sz);
      int pw_sz = (int)(ww_sz * 0.28f);
      if (pw_sz < 160)
        pw_sz = 160;
      int ph_sz = (int)(wh_sz * 0.28f);
      if (ph_sz < 120)
        ph_sz = 120;
      int px_sz = ww_sz - pw_sz - 12;
      int py_sz = wh_sz - ph_sz - 12;
      SDL_Rect pipr = {px_sz, py_sz, pw_sz, ph_sz};
      if (!(g_hover_mouse_x >= pipr.x && g_hover_mouse_x < pipr.x + pipr.w && g_hover_mouse_y >= pipr.y && g_hover_mouse_y < pipr.y + pipr.h))
      {
        Voxel *hv = world_get_voxel(world, (uint32_t)g_hover_vx, (uint32_t)g_hover_vy, (uint32_t)g_hover_vz);
        if (hv)
        {
          const char *tname = world_voxel_type_name(hv->type);
          uint8_t q = voxel_get_quantity(hv);
          uint8_t temp = voxel_get_temperature(hv);
          uint8_t entropy = voxel_get_entropy(hv);
          uint8_t dmg = voxel_get_damage(hv);
          uint8_t heat = voxel_get_heat(hv);
          // Build vertical lines
          const int max_lines = 8;
          char lines[max_lines][96];
          int n = 0;
          snprintf(lines[n++], sizeof(lines[0]), "%s", tname ? tname : "?");
          snprintf(lines[n++], sizeof(lines[0]), "Qty: %u", q);
          snprintf(lines[n++], sizeof(lines[0]), "Temp: %u", temp);
          snprintf(lines[n++], sizeof(lines[0]), "Entropy: %u", entropy);
          snprintf(lines[n++], sizeof(lines[0]), "Damage: %u", dmg);
          snprintf(lines[n++], sizeof(lines[0]), "Heat: %u", heat);
          // Size background based on text widths
          int max_w = 0;
          int line_h = (font ? TTF_FontHeight(font) : 12);
          for (int i = 0; i < n; i++)
          {
            int tw = 0, th = 0;
            if (font)
              TTF_SizeText(font, lines[i], &tw, &th);
            if (tw > max_w)
              max_w = tw;
          }
          int tx = g_hover_mouse_x + 12;
          int ty = g_hover_mouse_y + 12;
          SDL_Rect bg = {tx - 6, ty - 6, (max_w > 0 ? max_w : 8) + 12, n * line_h + 8};
          SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
          SDL_RenderFillRect(ren, &bg);
          // Draw lines
          SDL_Color white = {255, 255, 255, 255};
          int cy = ty;
          for (int i = 0; i < n; i++)
          {
            draw_text(ren, font, lines[i], tx, cy, white);
            cy += line_h;
          }
        }
      }
    }

    // Bottom-left details panel for selected voxel (and selected actor if present)
    if (g_selected_voxel_valid && font)
    {
      int ww_dl = WINDOW_W, wh_dl = WINDOW_H;
      SDL_GetWindowSize(win, &ww_dl, &wh_dl);
      const int pad = 8;
      const int line_h = (font ? TTF_FontHeight(font) : 12);
      // Build detail lines
      char lines[12][128];
      int n = 0;
      snprintf(lines[n++], sizeof(lines[0]), "Selected: (%d,%d,%d)", g_selected_vx, g_selected_vy, g_selected_vz);
      if (world_pos_in_bounds_fast(world, g_selected_vx, g_selected_vy, g_selected_vz))
      {
        Voxel *sv = world_get_voxel(world, (uint32_t)g_selected_vx, (uint32_t)g_selected_vy, (uint32_t)g_selected_vz);
        if (sv)
        {
          const char *tname = world_voxel_type_name(sv->type);
          snprintf(lines[n++], sizeof(lines[0]), "Type: %s", tname ? tname : "?");
          snprintf(lines[n++], sizeof(lines[0]), "Qty: %u  Temp: %u  Entropy: %u  Dmg: %u Heat: %u",
                   voxel_get_quantity(sv), voxel_get_temperature(sv), voxel_get_entropy(sv), voxel_get_damage(sv), voxel_get_heat(sv));
        }
      }
      if (g_selected_actor_index >= 0 && g_selected_actor_index < g_actor_count)
      {
        Actor *sa = &g_actors[g_selected_actor_index];
        if (sa && sa->is_active)
        {
          snprintf(lines[n++], sizeof(lines[0]), "Actor: %s (id=%u)", sa->name, sa->id);
          snprintf(lines[n++], sizeof(lines[0]), "HP: %u  Stamina: %.0f  Pos: %.1f,%.1f,%.1f",
                   sa->health, sa->stamina, sa->x, sa->y, sa->z);
        }
      }
      // Measure width
      int max_w = 0;
      for (int i = 0; i < n; i++)
      {
        int tw = 0, th = 0;
        if (font)
          TTF_SizeText(font, lines[i], &tw, &th);
        if (tw > max_w)
          max_w = tw;
      }
      int box_w = max_w + pad * 2;
      int box_h = n * line_h + pad * 2;
      SDL_Rect panel = {12, wh_dl - box_h - 12, box_w, box_h};
      SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
      SDL_RenderFillRect(ren, &panel);
      SDL_SetRenderDrawColor(ren, 220, 220, 220, 200);
      SDL_RenderDrawRect(ren, &panel);
      SDL_Color white = {255, 255, 255, 255};
      int ty = panel.y + pad;
      for (int i = 0; i < n; i++)
      {
        draw_text(ren, font, lines[i], panel.x + pad, ty, white);
        ty += line_h;
      }
    }

    // Opt-in GL occupancy upload (FP_GPU_OCCUPANCY=1). Live lighting is CPU; this is for a future
    // fullscreen pass and costs a full-world expand+upload every dirty frame when left on.
    if (gpu_bits)
    {
      if (getenv("FP_GPU_OCCUPANCY") != NULL)
        (void)gl_occupancy_upload(&gl_occ, gpu_bits, world ? world->voxel_revision : 0);
      (void)gpu_voxel_buffer_upload(gpu_bits);
    }

    // Overlay (top-right)

    // Right-hand UI elements moved to render after unified UI system

    // Toast overlay: centered, one-third down
    if (font && g_toast_until > now_ticks && g_toast_buf[0])
    {
      int tw = 0, th = 0;
      TTF_SizeText(font, g_toast_buf, &tw, &th);
      int cx = WINDOW_W / 2;
      int cy = WINDOW_H / 3;
      SDL_Rect tbg = {cx - (tw + 16) / 2, cy - (th + 10) / 2, tw + 16, th + 10};
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
      SDL_RenderFillRect(ren, &tbg);
      draw_text(ren, font, g_toast_buf, tbg.x + 8, tbg.y + 5, (SDL_Color){255, 255, 255, 255});
    }

    // Chat input box and world log at bottom-left; ensure chat is below the log
    int base_bottom = (wh > 0 ? wh : WINDOW_H);

    // Toast history panel (world log): bottom-left, snaps to window size. Push from bottom up.
    int panel_w = 0;
    int panel_h = 0;
    if (font && g_toast_history_count > 0)
    {
      int line_h = (font ? TTF_FontHeight(font) : 12) + 2;
      // Match tools panel width if available; else compute from text widths
      panel_w = g_tools_panel_rect.w > 0 ? g_tools_panel_rect.w : 0;
      if (panel_w <= 0)
      {
        for (int i = 0; i < g_toast_history_count; i++)
        {
          int tw = 0, th = 0;
          TTF_SizeText(font, g_toast_history[i], &tw, &th);
          if (tw > panel_w)
            panel_w = tw;
        }
        panel_w += 16;
      }
      panel_h = g_toast_history_count * line_h + 12;
      SDL_Rect hbg = {10, base_bottom - panel_h - 10, panel_w, panel_h};
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 120);
      SDL_RenderFillRect(ren, &hbg);
      SDL_SetRenderDrawColor(ren, 255, 255, 255, 80);
      SDL_RenderDrawRect(ren, &hbg);
      // Draw from bottom upwards
      int ycur = hbg.y + hbg.h - 6 - line_h;
      for (int i = 0; i < g_toast_history_count; i++)
      {
        (void)i; // idx not used in this version
        // We want bottom to be most recent: take from 0..count-1 but draw reversed
        int draw_idx = i; // build list bottom-up: start with most recent
        draw_idx = i;     // i=0 -> most recent
        draw_text(ren, font, g_toast_history[draw_idx], hbg.x + 8, ycur, (SDL_Color){220, 220, 220, 255});
        ycur -= line_h;
      }
    }

    // Chat input box below the log; position under the computed log panel
    if (font && g_chat_open)
    {
      int chat_w = (g_tools_panel_rect.w > 0 ? g_tools_panel_rect.w : 300);
      int chat_h = (font ? TTF_FontHeight(font) : 12) + 10;
      int chat_x = 10;
      int log_h = panel_h; // panel_h computed above if log visible, else 0
      int chat_y = base_bottom - 10 - log_h - chat_h - 6;
      if (chat_y < 10)
        chat_y = 10;
      SDL_Rect cbg = {chat_x, chat_y, chat_w, chat_h};
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
      SDL_RenderFillRect(ren, &cbg);
      SDL_SetRenderDrawColor(ren, 255, 255, 255, 120);
      SDL_RenderDrawRect(ren, &cbg);
      // Placeholder prompt text (enter toggles open/close; editing not implemented here)
      draw_text(ren, font, "> ", cbg.x + 6, cbg.y + 5, (SDL_Color){220, 220, 220, 255});
    }

    // Render the world using isometric renderer (only when there's a world to render)
    if (!g_swap_views && world && world->voxels)
    {
      // Check if world has any content to render
      bool has_content = false;
      size_t total_voxels = (size_t)world->width * (size_t)world->height * (size_t)world->depth;
      for (size_t i = 0; i < total_voxels && i < 1000; i++) // Sample first 1000 for speed
      {
        if (world->voxels[i].type != VOXEL_AIR)
        {
          has_content = true;
          break;
        }
      }

      if (has_content)
      {
        printf("[editor] Rendering isometric view\n");
        // Main: isometric; PIP: FP (if available)
        // Clear the background manually first
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 20, 22, 35, 255); // Same blue as isometric renderer
        SDL_RenderClear(ren);

        // Disable clear so isometric doesn't clear again
        bool prev_allow_clear = ir->allow_clear;
        ir->allow_clear = false;
        isometric_renderer_render_gpu(ir, ren);
        // CPURendererConfig config = cpu_renderer_config_default();
        // cpu_renderer_render_optimized(ir, ren, &config);
        ir->allow_clear = prev_allow_clear;
      }
      else
      {
        // printf("[editor] No world content to render\n");
      }
    }
    else if (!g_swap_views)
    {
      printf("[editor] No world to render\n");
    }

    // Draw menus last so they appear over all other UI elements
    if (font && g_editor_ui)
    {
      // NEW: Update UI state before rendering
      world_editor_ui_update_actor_list(g_editor_ui, g_actors, g_actor_count);
      world_editor_ui_update_world_info(g_editor_ui, world);

      // Render the unified UI system instead of manual menu rendering
      world_editor_ui_render(g_editor_ui);

      // RIGHT-HAND UI ELEMENTS: Render after unified UI to prevent coverage
      if (font)
      {
        // Compute FPS and voxel stats
        static Uint32 fps_last_ticks2 = 0;
        static float fps_avg_ms2 = 0.0f;
        Uint32 now2 = SDL_GetTicks();
        if (fps_last_ticks2 == 0)
          fps_last_ticks2 = now2;
        Uint32 dt2 = now2 - fps_last_ticks2;
        fps_last_ticks2 = now2;
        if (fps_avg_ms2 <= 0.0f)
          fps_avg_ms2 = (float)dt2;
        else
          fps_avg_ms2 = fps_avg_ms2 * 0.9f + (float)dt2 * 0.1f;
        float inst_fps2 = dt2 > 0 ? (1000.0f / (float)dt2) : 0.0f;
        // Onscreen count from renderer stats; total voxels = width*height*depth of home world; max across loaded worlds (home + edges)
        int onscreen = isometric_renderer_get_last_rendered_count(ir);
        unsigned long long total = (unsigned long long)world->width * (unsigned long long)world->height * (unsigned long long)world->depth;
        unsigned long long max_loaded = total;
        // Extend when edge worlds are present
        // Note: we don't have ir->edge_worlds header here; keep max as home for now or compute via universe if available
        char stats_buf[128];
        snprintf(stats_buf, sizeof(stats_buf), "FPS: %.1f  v:%llu  o:%d  s:%llu", inst_fps2, total, onscreen, max_loaded);
        // Defer drawing of labels; we'll render a compact block on the right above the graph
        char info[128];
        snprintf(info, sizeof(info), "Z: %d / %u    Use PageUp/PageDown (or [ / ])", current_z, world->depth - 1);
        // Epoch counter (flash yellow briefly after change) and clock seconds with flash on whole changes
        // Format: Epoch: N (xSCALE)  t: SS.S
        unsigned long long total_ms = g_clock_ms;
        unsigned long long whole_s = total_ms / 1000ULL;
        unsigned long long frac_ms = total_ms % 1000ULL;
        if ((int)whole_s != g_clock_last_whole)
        {
          g_clock_last_whole = (int)whole_s;
          g_sec_flash_start = now_ticks;
        }
        char epoch_buf[128];
        snprintf(epoch_buf, sizeof(epoch_buf), "Epoch: %u  (x%llu)  t: %llu.%03llu", g_clock_epoch_index, g_time_factor, whole_s, frac_ms);
        SDL_Color epoch_color = (SDL_Color){255, 255, 255, 255};
        if (g_epoch_flash_start > 0)
        {
          Uint32 since = now_ticks - g_epoch_flash_start;
          if (since < 900) // flash window ~0.9s
            epoch_color = (SDL_Color){255, 255, 0, 255};
          else
            g_epoch_flash_start = 0;
        }
        // If no epoch flash, flash seconds in yellow for brief period on whole-second change
        if (g_epoch_flash_start == 0 && g_sec_flash_start > 0)
        {
          Uint32 since2 = now_ticks - g_sec_flash_start;
          if (since2 < 250) // quick flash
            epoch_color = (SDL_Color){255, 255, 0, 255};
          else
            g_sec_flash_start = 0;
        }

        // Right-hand side: composition graph and palette, then Z swatches
        int ww_snap = WINDOW_W, wh_snap = WINDOW_H;
        SDL_GetWindowSize(win, &ww_snap, &wh_snap);
        int right_margin = 16;
        int right_panel_x = ww_snap - (400) - right_margin; // snap block to right; 400 is target content width
        // Add top padding to avoid bumping against top and to match left rhythm: place chart after labels we draw separately now
        int graph_y = (font ? TTF_FontHeight(font) : 12) * 2 + 22;
        // Align chart to vertical rhythm below a compact status block; draw block, then labels, then chart
        int labels_baseline = 0; // baseline Y for composition labels (one line below status block)
        {
          const int line_h2 = (font ? TTF_FontHeight(font) : 12);
          const int num_lines = 5;
          // Start from the existing intended chart Y, snap to rhythm line and leave space for the block above it
          int rhythm_y = ((graph_y + line_h2 - 1) / line_h2) * line_h2; // ceil to next line
          int block_y = rhythm_y - num_lines * line_h2 - 6;
          if (block_y < 6)
            block_y = 6;

          int by = block_y;
          // Align text flush-right by measuring widths
          int tw = 0, th = 0;
          TTF_SizeText(font, g_world_name, &tw, &th);
          draw_text(ren, font, g_world_name, (ww_snap - right_margin) - tw, by, (SDL_Color){255, 255, 255, 255});
          by += line_h2;
          // stats_buf computed earlier; recompute local copy to keep flow simple
          char stats_buf2[128];
          snprintf(stats_buf2, sizeof(stats_buf2), "FPS: %.1f  v:%llu  o:%d  s:%llu", inst_fps2, total, onscreen, max_loaded);
          TTF_SizeText(font, stats_buf2, &tw, &th);
          draw_text(ren, font, stats_buf2, (ww_snap - right_margin) - tw, by, (SDL_Color){220, 220, 220, 255});
          by += line_h2;
          draw_text(ren, font, info, (ww_snap - right_margin) - tw, by, (SDL_Color){255, 255, 255, 255});
          by += line_h2;
          draw_text(ren, font, epoch_buf, (ww_snap - right_margin) - tw, by, epoch_color);
          by += line_h2;
          const char *runlbl = g_clock_running ? "[running]" : "[stopped]";
          TTF_SizeText(font, runlbl, &tw, &th);
          draw_text(ren, font, runlbl, (ww_snap - right_margin) - tw, by, (SDL_Color){255, 255, 255, 255});

          // Compute labels baseline as the next line below status block
          labels_baseline = by + line_h2;
          if (labels_baseline < 6)
            labels_baseline = 6;

          // Draw composition labels at the labels baseline (one line below status block)
          if (g_have_counts)
          {
            char line[512];
            line[0] = '\0';
            size_t total_non_air = 0;
            for (int i = 0; i < (int)VOXEL_COUNT; i++)
              if (i != VOXEL_AIR)
                total_non_air += g_last_counts[i];
            if (total_non_air > 0)
            {
              int order[(int)VOXEL_COUNT];
              for (int i = 0; i < (int)VOXEL_COUNT; i++)
                order[i] = i;
              for (int i = 0; i < (int)VOXEL_COUNT - 1; i++)
                for (int j = i + 1; j < (int)VOXEL_COUNT; j++)
                  if (g_last_counts[order[j]] > g_last_counts[order[i]])
                  {
                    int t = order[i];
                    order[i] = order[j];
                    order[j] = t;
                  }
              int used_w = 0;
              int max_w = right_panel_x - 24;
              for (int k = 0; k < (int)VOXEL_COUNT; k++)
              {
                int t = order[k];
                if (t == VOXEL_AIR || g_last_counts[t] == 0)
                  continue;
                double pct = (double)g_last_counts[t] * 100.0 / (double)total_non_air;
                char seg[64];
                const char *name = world_voxel_type_name((VoxelType)t);
                snprintf(seg, sizeof(seg), "%s %.0f%%  ", name ? name : "?", pct);
                int seg_w = (int)strlen(seg) * 7;
                if (used_w + seg_w > max_w)
                {
                  strncat(line, "...", sizeof(line) - strlen(line) - 1);
                  break;
                }
                strncat(line, seg, sizeof(line) - strlen(line) - 1);
                used_w += seg_w;
              }
            }
            if (line[0] != '\0')
            {
              TTF_SizeText(font, line, &tw, &th);
              draw_text(ren, font, line, (ww_snap - right_margin) - tw, labels_baseline, (SDL_Color){255, 255, 255, 255});
            }
          }

          // Place chart one full line plus small margin below labels, then snap to rhythm
          int chart_y = labels_baseline + line_h2 + 6;
          chart_y = ((chart_y + line_h2 - 1) / line_h2) * line_h2;
          // Compute chart width to right margin
          int cols_c = 2, sw_c = 22, name_w_c = 120, pad_c = 6;
          int item_w_c = sw_c + 6 + name_w_c;
          int chart_w = cols_c * item_w_c + (cols_c - 1) * pad_c; // box_w inside draw
          // bg right edge = chart_x + chart_w + 8; snap that to right margin
          int chart_x = (ww_snap - right_margin) - (chart_w + 8);
          draw_composition_graph(ren, font, world, current_z, chart_x, chart_y);
        }
        // Labels are drawn before the chart above to avoid any overdraw

        // Palette directly under graph on the right
        int palette_y = g_chart_container_rect.y + g_chart_container_rect.h + 10;
        if (g_palette_visible)
        {
          int px = (ww_snap - right_margin) - (g_tools_panel_rect.w > 0 ? g_tools_panel_rect.w : 400);
          draw_palette(ren, font, px, palette_y, false);
        }
        // Move Z swatches to the right, directly below the chart container
        int z_start_y = g_chart_container_rect.y + g_chart_container_rect.h + 10;
        // Align to right-hand side: swatch width = 16, margin = 12
        int z_right_x = ww_snap - 16 - right_margin;
        draw_left_z_swatch_column(ren, font, world, current_z, z_right_x, z_start_y);

        // Draw shape preview for fullscreen isometric mode (no PIP)
        if (g_current_tool == TOOL_SHAPE && g_shape_mode == SHAPE_CUBE && g_shape_has_first)
        {
          uint32_t x0 = g_shape_x1, y0 = g_shape_y1, z0 = g_shape_z1;
          int mx_cur, my_cur;
          SDL_GetMouseState(&mx_cur, &my_cur);
          uint32_t pvx = (uint32_t)g_hover_vx, pvy = (uint32_t)g_hover_vy, pvz = (uint32_t)g_hover_vz;
          if (!isometric_renderer_pick_voxel(ir, world, current_z, mx_cur, my_cur, &pvx, &pvy, &pvz))
          {
            if (!g_hover_voxel_valid)
            {
              pvx = (uint32_t)(world->width / 2);
              pvy = (uint32_t)(world->height / 2);
              pvz = (uint32_t)current_z;
            }
          }
          uint32_t x1 = pvx, y1 = pvy, z1 = pvz;
          if (x1 < x0) { uint32_t t = x0; x0 = x1; x1 = t; }
          if (y1 < y0) { uint32_t t = y0; y0 = y1; y1 = t; }
          if (z1 < z0) { uint32_t t = z0; z0 = z1; z1 = t; }
          const SDL_Keymod mods = SDL_GetModState();
          if ((mods & KMOD_SHIFT) != 0)
          {
            uint32_t base_z0 = g_shape_z1;
            uint32_t base_z1 = base_z0 + (g_shape_thickness > 0 ? (uint32_t)g_shape_thickness - 1U : 0U);
            if (base_z1 >= world->depth) base_z1 = world->depth - 1U;
            z0 = base_z0; z1 = base_z1;
          }
          int world_index = isometric_renderer_offset_index(ir, 0, 0, 0);
          if (world_index < 0) world_index = 0;
          SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
          // Use voxel-edge aligned extents by extending max edges by +1
          uint32_t ex0 = x0, ey0 = y0, ez0 = z0;
          uint32_t ex1 = x1 + 1U, ey1 = y1 + 1U, ez1 = z1 + 1U;

          // Adjust center to match renderer's auto-centering used during main draw
          int adj_cx = ir->screen_center_x;
          int adj_cy = ir->screen_center_y;
          {
            const World *w0 = world;
            if (w0)
            {
              int half_tw = ir->tile_width / 2;
              int half_th = ir->tile_height / 2;
              int cx0 = ir->screen_center_x;
              int cy0 = ir->screen_center_y;
              int minx2 = (int)w0->width, miny2 = (int)w0->height, maxx2 = -1, maxy2 = -1;
              for (uint32_t yy = 0; yy < w0->height; yy++)
                for (uint32_t xx = 0; xx < w0->width; xx++)
                  for (int zz = 0; zz < (int)w0->depth; zz++)
                  {
                    Voxel *vv = world_pos_in_bounds_fast((World *)w0, (int)xx, (int)yy, (int)zz) ? world_voxel_ptr_fast((World *)w0, (int)xx, (int)yy, (int)zz) : NULL;
                    if (vv && vv->type != VOXEL_AIR)
                    {
                      if ((int)xx < minx2) minx2 = (int)xx;
                      if ((int)yy < miny2) miny2 = (int)yy;
                      if ((int)xx > maxx2) maxx2 = (int)xx;
                      if ((int)yy > maxy2) maxy2 = (int)yy;
                      break;
                    }
                  }
              if (maxx2 >= minx2 && maxy2 >= miny2)
              {
                float cmx = (minx2 + maxx2) * 0.5f;
                float cmy = (miny2 + maxy2) * 0.5f;
                int center_sx = cx0 + ((int)cmx - (int)cmy) * half_tw;
                int center_sy = cy0 + ((int)cmx + (int)cmy) * half_th;
                int ww_sz = WINDOW_W, wh_sz = WINDOW_H;
                SDL_GetWindowSize(win, &ww_sz, &wh_sz);
                adj_cx = cx0 - (center_sx - ww_sz / 2);
                adj_cy = cy0 - (center_sy - wh_sz / 2);
              }
            }
          }

          isometric_renderer_draw_box_wireframe_centered(ir, ren,
                                                         (int)ex0, (int)ey0, (int)ez0,
                                                         (int)ex1, (int)ey1, (int)ez1,
                                                         (SDL_Color){255, 220, 0, 220},
                                                         -1, -1, true);
          // Label
          uint32_t dxf = (ex1 - ex0);
          uint32_t dyf = (ey1 - ey0);
          uint32_t dzf = (ez1 - ez0);
          char fs_info[64];
          snprintf(fs_info, sizeof(fs_info), "%ux%ux%u  (thickness=%d)", (unsigned)dxf, (unsigned)dyf, (unsigned)dzf, g_shape_thickness);
          int lsx, lsy;
          isometric_world_to_screen(ir, (int)ex1, (int)ey0, (int)ez1, world_index, &lsx, &lsy);
          lsy -= 14;
          if (font)
            draw_text(ren, font, fs_info, lsx, lsy, (SDL_Color){255, 240, 200, 230});
        }
      }

      // LEGACY: Always render the Actor panel for now (will be migrated to new UI later)
      int left_margin = 16;
      int left_panel_x = left_margin;
      int left_panel_y = 200; // Below the new UI toolbar
      int left_panel_w = 300;

      // Draw legacy actor panel
      draw_actor_panel(ren, font, left_panel_x, left_panel_y, left_panel_w);
    }
    else
    {
      printf("[editor] Cannot render UI: font=%p, g_editor_ui=%p\n", (void *)font, (void *)g_editor_ui);
    }

  SDL_RenderPresent(ren);
  }

  // Cleanup
  gl_occupancy_destroy(&gl_occ);
  if (gpu_bits)
    gpu_voxel_buffer_destroy(gpu_bits);
  if (ns)
    ns_destroy(ns);
  if (g_editor_ui) {
    world_editor_ui_destroy(g_editor_ui);
  }

  if (world) {
    world_destroy(world);
  }

  if (font) {
    TTF_CloseFont(font);
  }

  // Stop world generation worker thread
  world_gen_stop_worker();

  TTF_Quit();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();

  return 0;
}

void draw_actor_panel(SDL_Renderer *ren, TTF_Font *font, int x, int y, int width)
{
  const int item_h = 20;
  const int panel_w = width;
  // Minimal bounding rect for hit-testing; visuals are removed per request
  int panel_h = 8 + (g_actor_count > 0 ? (g_actor_count * item_h) : item_h) + 8;
  SDL_Rect panel = (SDL_Rect){x, y, panel_w, panel_h};
  g_actor_panel_rect = panel;
  g_actor_item_count = 0;

  // printf("[DEBUG] Drawing actor panel: pos(%d,%d), size(%d,%d), actor_count=%d\n", x, y, panel_w, panel_h, g_actor_count);
  int cx = panel.x + 8;
  int cy = panel.y + 8;
  SDL_Color normal = {220, 220, 240, 255};
  SDL_Color selected = {255, 255, 140, 255};
  if (g_actor_count == 0)
  {
    draw_text(ren, font, "Actors: none (press 'i')", cx, cy, (SDL_Color){160, 160, 180, 255});
    return;
  }
  draw_text(ren, font, "Actors", cx, cy, (SDL_Color){255, 255, 255, 255});
  cy += item_h;
  for (int i = 0; i < g_actor_count && i < 256; i++)
  {
    Actor *a = &g_actors[i];
    char buf[64];
    snprintf(buf, sizeof(buf), "%u: (%.0f,%.0f,%.0f)", a->id, a->x, a->y, a->z);
    SDL_Rect r = {cx - 4, cy - 2, panel_w - 16, item_h};
    SDL_SetRenderDrawColor(ren, 46, 48, 62, 220);
    SDL_RenderFillRect(ren, &r);
    SDL_SetRenderDrawColor(ren, 80, 90, 120, 255);
    SDL_RenderDrawRect(ren, &r);
    draw_text(ren, font, buf, cx + 4, cy + 2, (i == g_controlled_actor_index ? selected : normal));
    if (g_actor_item_count < 256)
    {
      g_actor_item_rects[g_actor_item_count] = r;
      printf("[DEBUG] Setting item rect %d: (%d,%d,%d,%d)\n", g_actor_item_count, r.x, r.y, r.w, r.h);
      g_actor_item_count++;
    }
    cy += item_h;
  }

  // Show mouse look status if we have a controlled actor
  if (g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count)
  {
    cy += 4; // Add some spacing
    const char *look_status = g_mouse_look_enabled ? "Mouse Look: ON (M to toggle)" : "Mouse Look: OFF (M to toggle)";
    SDL_Color status_color = g_mouse_look_enabled ? (SDL_Color){140, 255, 140, 255} : (SDL_Color){200, 200, 200, 255};
    draw_text(ren, font, look_status, cx, cy, status_color);
  }
}

// NEW: Input system callback implementations
static void world_editor_insert_actor(void *user_data)
{
  (void)user_data; // Unused parameter
  World *world = g_fp_gameworlds.home_world;
  if (!world)
    return;

  // Get current camera position for actor spawn
  int cx = (int)(world->width / 2);
  int cy = (int)(world->height / 2);
  int az = find_highest_air_z(world, cx, cy);
  if (az < 0)
  {
    // Fallback: place just above highest solid, or at z=0 if none
    int hz = find_highest_solid_z(world, cx, cy);
    az = (hz >= 0) ? (hz + 1) : 0;
  }

  if (g_actor_count < (int)(sizeof(g_actors) / sizeof(g_actors[0])))
  {
    Actor a = {0};
    a.id = (uint32_t)(g_actor_count + 1);
    snprintf(a.name, sizeof(a.name), "Actor%u", a.id);
    a.x = (double)cx + 0.5;
    a.y = (double)cy + 0.5;
    a.z = (double)az + 0.5; // center within the highest air voxel
    a.velocity_x = 0.0;
    a.velocity_y = 0.0;
    a.velocity_z = 0.0;
    a.is_active = true;
    strncpy(a.world_id, "editor", sizeof(a.world_id) - 1);

    g_actors[g_actor_count] = a;
    g_controlled_actor_index = g_actor_count;
    g_follow_actor_camera = true;
    g_actor_count++;

    // Place an ACTOR voxel at the spawn for easy visual debugging
    world_set_voxel(world, (uint32_t)cx, (uint32_t)cy, (uint32_t)az, VOXEL_ACTOR);
    fp_renderer_invalidate_cache(); // Invalidate mesh cache after world modification

    printf("[editor] Actor %u inserted at (%d, %d, %d)\n", a.id, cx, cy, az);
  }
  else
  {
    printf("[editor] Cannot insert actor - maximum actor count reached\n");
  }
}

static void world_editor_toggle_mouse_look(bool enabled, void *user_data)
{
  (void)user_data; // Unused parameter

  printf("[editor] M key pressed! g_actor_count=%d\n", g_actor_count);
  if (g_actor_count > 0)
  {
    // Toggle mouse look mode
    g_mouse_look_enabled = enabled;
    if (g_mouse_look_enabled)
    {
      printf("[editor] Mouse look ENABLED - move mouse to look around\n");
      // Initialize mouse position for relative movement
      g_mouse_look_initialized = false;
    }
    else
    {
      printf("[editor] Mouse look DISABLED\n");
    }
  }
  else
  {
    printf("[editor] M key ignored - no actors available for FP camera\n");
  }
}

static void world_editor_time_control(int direction, void *user_data)
{
  (void)user_data; // Unused parameter

  if (direction > 0)
  {
    // Speed up time
    if (g_time_factor < MAX_TIME_FACTOR)
      g_time_factor = (g_time_factor < 1000000ULL) ? (g_time_factor + 1ULL) : (g_time_factor * 2ULL <= MAX_TIME_FACTOR ? g_time_factor * 2ULL : MAX_TIME_FACTOR);
  }
  else
  {
    // Slow down time
    if (g_time_factor > 1ULL)
      g_time_factor = (g_time_factor > 1000000ULL) ? (g_time_factor / 2ULL) : (g_time_factor - 1ULL);
  }
}

static void world_editor_zoom_control(float factor, void *user_data)
{
  IsometricRenderer *ir = (IsometricRenderer *)user_data;
  if (!ir)
  {
    printf("[DEBUG] Zoom control callback: user_data is NULL\n");
    return;
  }

  printf("[DEBUG] Zoom control callback: factor=%.2f, current_zoom=%.2f\n", factor, ir->zoom_scale);
  ir->zoom_scale *= factor;
  printf("[DEBUG] Zoom control callback: new_zoom=%.2f\n", ir->zoom_scale);
}

static void world_editor_camera_move(int dx, int dy, int dz, void *user_data)
{
  (void)user_data; // Unused parameter

  if (dz != 0)
  {
    // Handle Z-axis camera movement (PAGEUP/PAGEDOWN)
    World *world = g_fp_gameworlds.home_world;
    if (world && g_current_z + dz >= 0 && g_current_z + dz < (int)world->depth)
    {
      g_current_z += dz;
      if (g_isometric_renderer)
        g_isometric_renderer->camera_z = g_current_z;
      // Reset composition chart cache when Z layer changes
      g_have_counts = false;
    }
  }
  else if (dx != 0 || dy != 0)
  {
    // Handle X/Y camera movement (arrow keys)
    if (g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count)
    {
      // Actor is selected - move the actor instead of camera
      Actor *a = &g_actors[g_controlled_actor_index];
      World *world = g_fp_gameworlds.home_world;

      if (world && gpu_physics_try_move_actor(world, a, dx, dy, 0))
      {
        int ix = (int)floor(a->x + 0.0001);
        int iy = (int)floor(a->y + 0.0001);
        isometric_renderer_set_auto_center(g_isometric_renderer, false);
        isometric_renderer_set_camera(g_isometric_renderer, ix, iy, g_isometric_renderer->camera_z);
        g_follow_actor_camera = true;
        printf("[editor] unit move to (%d,%d)\n", ix, iy);
      }
    }
    else
    {
      // No actor selected - move camera directly
      if (g_isometric_renderer)
      {
        int new_camera_x = g_isometric_renderer->camera_x + dx;
        int new_camera_y = g_isometric_renderer->camera_y + dy;
        isometric_renderer_set_auto_center(g_isometric_renderer, false);
        isometric_renderer_set_camera(g_isometric_renderer, new_camera_x, new_camera_y, g_isometric_renderer->camera_z);
        g_follow_actor_camera = false; // Stop following actor when manually moving camera
        printf("[editor] camera move to (%d,%d)\n", new_camera_x, new_camera_y);
      }
    }
  }
}

static void world_editor_camera_rotate(int direction, void *user_data)
{
  (void)user_data; // Unused parameter

  if (direction < 0)
  {
    // Rotate camera view 90° counter-clockwise: (x,y) -> (-y, x)
    if (g_isometric_renderer)
    {
      int old_cx = g_isometric_renderer->camera_x;
      int old_cy = g_isometric_renderer->camera_y;
      g_isometric_renderer->camera_x = -old_cy;
      g_isometric_renderer->camera_y = old_cx;
    }
  }
  else
  {
    // Rotate camera view 90° clockwise: (x,y) -> (y, -x)
    if (g_isometric_renderer)
    {
      int old_cx = g_isometric_renderer->camera_x;
      int old_cy = g_isometric_renderer->camera_y;
      g_isometric_renderer->camera_x = old_cy;
      g_isometric_renderer->camera_y = -old_cx;
    }
  }
}

static void world_editor_camera_reset(void *user_data)
{
  (void)user_data; // Unused parameter

  // Reset camera to center of origin slice
  World *world = g_fp_gameworlds.home_world;
  if (world && g_isometric_renderer)
  {
    isometric_renderer_set_auto_center(g_isometric_renderer, false);
    g_isometric_renderer->camera_x = (int)(world->width / 2);
    g_isometric_renderer->camera_y = (int)(world->height / 2);
  }
}

static void world_editor_renderer_toggle(int toggle_type, void *user_data)
{
  (void)user_data; // Unused parameter

  if (g_isometric_renderer)
  {
    switch (toggle_type)
    {
    case 0: // Toggle greedy neighbors top-only
      g_isometric_renderer->greedy_neighbors_top_only = !g_isometric_renderer->greedy_neighbors_top_only;
      break;
    case 1: // Toggle alignment baseline overlay
      g_isometric_renderer->show_alignment_baseline = !g_isometric_renderer->show_alignment_baseline;
      break;
    }
  }
}

static void world_editor_chat_toggle(void *user_data)
{
  (void)user_data; // Unused parameter

  if (!g_chat_open)
  {
    g_chat_open = true;
    g_chat_buf[0] = '\0';
  }
  else
  {
    g_chat_open = false; /* submit if needed */
  }
}

static void world_editor_clock_toggle(void *user_data)
{
  (void)user_data; // Unused parameter

  g_clock_running = !g_clock_running;
  printf("[DEBUG] Clock toggle called! g_clock_running is now: %s\n", g_clock_running ? "true" : "false");
}

// New world configuration functions
static void set_new_world_config(WorldGenerationType world_type, const char *seed, bool generate_neighbors)
{
  g_new_world_config.world_type = world_type;
  if (seed && *seed)
  {
    strncpy(g_new_world_config.seed, seed, sizeof(g_new_world_config.seed) - 1);
    g_new_world_config.seed[sizeof(g_new_world_config.seed) - 1] = '\0';
  }
  else
  {
    g_new_world_config.seed[0] = '\0';
  }
  g_new_world_config.generate_neighbors = generate_neighbors;
  g_new_world_config.configured = true;
  printf("[editor] New world config set: type=%d, seed='%s', neighbors=%s\n",
         world_type, g_new_world_config.seed, generate_neighbors ? "yes" : "no");
}

static void clear_new_world_config(void)
{
  g_new_world_config.configured = false;
  g_new_world_config.seed[0] = '\0';
  g_new_world_config.generate_neighbors = false;
  printf("[editor] New world config cleared\n");
}

// Public function for UI to set new world configuration
void world_editor_set_new_world_config(WorldGenerationType world_type, const char *seed, bool generate_neighbors)
{
  set_new_world_config(world_type, seed, generate_neighbors);
}

// NEW: File operation callback implementation for unified UI system
static void on_file_operation(int operation_id, const char *filename, void *user_data)
{
  (void)user_data; // Unused parameter

  printf("[editor] File operation: %d", operation_id);
  if (filename)
  {
    printf(", filename: %s", filename);
  }
  printf("\n");

  switch (operation_id)
  {
  case WORLD_EDITOR_FILE_NEW:
    printf("[editor] -> New world file requested\n");

    // Use configuration from modal if available, otherwise use defaults
    WorldGenerationType world_type = g_new_world_config.configured ? g_new_world_config.world_type : WORLD_TYPE_HOME;
    const char *seed = g_new_world_config.configured && g_new_world_config.seed[0] ? g_new_world_config.seed : NULL;

    if (g_fp_gameworlds.home_world)
    {
      printf("[editor] Generating new world: type=%d, seed='%s'\n", world_type, seed ? seed : "random");

      // Generate seed if not provided
      char final_seed[65];
      if (!seed || !*seed)
      {
        unsigned int t = (unsigned int)time(NULL);
        for (int i = 0; i < 32; i++)
        {
          t = t * 1103515245u + 12345u;
          sprintf(final_seed + i * 2, "%02x", (unsigned)(t & 0xFF));
        }
        final_seed[64] = '\0';
        seed = final_seed;
        printf("[editor] Generated random seed: %s\n", seed);
      }

      // Set up the world for generation
      world_set_universe_noise_seed(seed);

      // Start the world generation worker thread if not already running
      world_gen_start_worker();

      // Show progress bar
      if (g_editor_ui)
      {
        world_editor_ui_show_progress_bar(g_editor_ui, "Preparing world generation...");
      }

      // Enqueue the world generation job to run in background
      world_gen_enqueue(world_type, seed, g_fp_gameworlds.home_world,
                       g_new_world_config.generate_neighbors,
                       world_gen_progress_callback, NULL);

      printf("[editor] World generation job enqueued for background processing\n");

      // Clear the configuration after use
      clear_new_world_config();
    }
    break;

  case WORLD_EDITOR_FILE_OPEN:
    printf("[editor] -> Open world file requested\n");
    // Show the file browser panel
    if (g_editor_ui->file_browser_panel.panel)
    {
      printf("[editor] Showing file browser panel\n");
      g_editor_ui->file_browser_panel.panel->visible = true;

      if (g_editor_ui->file_browser_panel.file_browser)
      {
        printf("[editor] Making file browser component visible\n");
        g_editor_ui->file_browser_panel.file_browser->visible = true;

        // Populate the file browser with the worlds directory
        if (g_editor_ui->file_browser_panel.file_browser->current_path)
          free(g_editor_ui->file_browser_panel.file_browser->current_path);
        g_editor_ui->file_browser_panel.file_browser->current_path = strdup("worlds/");

        printf("[editor] About to populate file browser with worlds/\n");
        // Populate the file list
        world_editor_populate_file_browser("worlds/");
        printf("[editor] File browser population completed\n");
      }
      else
      {
        printf("[editor] ERROR: File browser component is null!\n");
      }

      if (g_editor_ui->file_browser_panel.refresh_button)
      {
        printf("[editor] Making refresh button visible\n");
        g_editor_ui->file_browser_panel.refresh_button->visible = true;
      }
      else
      {
        printf("[editor] ERROR: Refresh button is null!\n");
      }

      if (g_editor_ui->file_browser_panel.home_button)
      {
        printf("[editor] Making home button visible\n");
        g_editor_ui->file_browser_panel.home_button->visible = true;
      }
      else
      {
        printf("[editor] ERROR: Home button is null!\n");
      }

      if (g_editor_ui->file_browser_panel.close_button)
      {
        printf("[editor] Making close button visible\n");
        g_editor_ui->file_browser_panel.close_button->visible = true;
      }
      else
      {
        printf("[editor] ERROR: Close button is null!\n");
      }
    }
    break;

  case WORLD_EDITOR_FILE_SAVE:
    printf("[editor] -> Save world file requested\n");
    // TODO: Implement save functionality
    break;

  case WORLD_EDITOR_FILE_SAVE_AS:
    printf("[editor] -> Save world file as... requested\n");
    // TODO: Implement save as functionality
    break;

  case WORLD_EDITOR_FILE_EXPORT:
    printf("[editor] -> Export world file requested\n");
    // TODO: Implement export functionality
    break;

  default:
    printf("[editor] -> Unknown file operation\n");
    break;
  }
}

// Tool change callback implementation for toolbar
static void on_tool_change(int tool_id, void *user_data)
{
  (void)user_data; // Unused parameter

  printf("[editor] Tool change: %d\n", tool_id);

  // Convert new tool IDs to old enum values
  switch (tool_id)
  {
  case WORLD_EDITOR_TOOL_CURSOR:
    printf("[editor] -> Select tool activated\n");
    g_current_tool = TOOL_CURSOR;
    // Reflect selection in UI without re-invoking callback
    if (g_editor_ui)
      world_editor_ui_set_selected_tool(g_editor_ui, WORLD_EDITOR_TOOL_CURSOR);
    break;

  case WORLD_EDITOR_TOOL_SHAPE:
    printf("[editor] -> Draw tool activated\n");
    g_current_tool = TOOL_SHAPE;
    if (g_editor_ui)
      world_editor_ui_set_selected_tool(g_editor_ui, WORLD_EDITOR_TOOL_SHAPE);
    break;

  case WORLD_EDITOR_TOOL_CUBE:
    printf("[editor] -> Cube tool activated\n");
    g_shape_mode = SHAPE_CUBE;
    // Enter Draw tool when a sub-shape is chosen
    g_current_tool = TOOL_SHAPE;
    if (g_editor_ui)
      world_editor_ui_set_selected_tool(g_editor_ui, WORLD_EDITOR_TOOL_CUBE);
    break;

  case WORLD_EDITOR_TOOL_SPHERE:
    printf("[editor] -> Sphere tool activated\n");
    g_shape_mode = SHAPE_SPHERE;
    // Enter Draw tool when a sub-shape is chosen
    g_current_tool = TOOL_SHAPE;
    if (g_editor_ui)
      world_editor_ui_set_selected_tool(g_editor_ui, WORLD_EDITOR_TOOL_SPHERE);
    break;

  case WORLD_EDITOR_TOOL_ADJACENT:
    printf("[editor] -> Adjacent tool activated\n");
    // Toggle adjacent menu
    g_adj_menu_open = !g_adj_menu_open;
    if (g_editor_ui)
      world_editor_ui_set_selected_tool(g_editor_ui, WORLD_EDITOR_TOOL_ADJACENT);
    break;

  default:
    printf("[editor] -> Unknown tool: %d\n", tool_id);
    break;
  }

  printf("[editor] Tool state updated: current_tool=%d, shape_mode=%d, adj_menu_open=%s\n",
         g_current_tool, g_shape_mode, g_adj_menu_open ? "true" : "false");
}

// Actor movement callback for UI movement buttons
static void on_actor_move(int dx, int dy, int dz, void *user_data)
{
  (void)user_data; // Unused parameter

  printf("[editor] Actor move requested: dx=%d, dy=%d, dz=%d\n", dx, dy, dz);

  // Check if an actor is selected
  if (g_controlled_actor_index >= 0 && g_controlled_actor_index < g_actor_count)
  {
    Actor *a = &g_actors[g_controlled_actor_index];
    World *world = g_fp_gameworlds.home_world;

    if (world && gpu_physics_try_move_actor(world, a, dx, dy, dz))
    {
      int ix = (int)floor(a->x + 0.0001);
      int iy = (int)floor(a->y + 0.0001);
      isometric_renderer_set_auto_center(g_isometric_renderer, false);
      isometric_renderer_set_camera(g_isometric_renderer, ix, iy, g_isometric_renderer->camera_z);
      g_follow_actor_camera = true;
      printf("[editor] Actor moved to (%d,%d,%d)\n", ix, iy, (int)floor(a->z + 0.0001));
    }
    else
    {
      printf("[editor] Actor move failed - collision or invalid world\n");
    }
  }
  else
  {
    printf("[editor] No actor selected for movement\n");
  }
}

// File browser callback for when a file is selected
static void on_file_browser_file_selected(const char *filepath, void *user_data)
{
  (void)user_data; // Unused parameter

  printf("[editor] File browser: File selected: %s\n", filepath);

  if (!filepath)
    return;

  // Check if it's a world file
  size_t len = strlen(filepath);
  if (len > 6 && strcmp(filepath + (len - 6), ".world") == 0)
  {
    printf("[editor] Loading world file: %s\n", filepath);

    // Check if we have a valid world reference
    if (!g_fp_gameworlds.home_world)
    {
      printf("[editor] Error: No valid world reference available\n");
      return;
    }

    // Load the world file
    if (world_load(g_fp_gameworlds.home_world, filepath))
    {
      printf("[editor] Successfully loaded world from: %s\n", filepath);

      // Refresh the renderer safely
      if (g_isometric_renderer)
      {
        // Note: The world is already set in the renderer, just refresh the texture
        isometric_renderer_try_load_layer_texture(g_isometric_renderer, g_fp_gameworlds.home_world);
      }

      // Hide the file browser panel safely
      if (g_editor_ui && g_editor_ui->file_browser_panel.panel)
      {
        g_editor_ui->file_browser_panel.panel->visible = false;
        if (g_editor_ui->file_browser_panel.file_browser)
          g_editor_ui->file_browser_panel.file_browser->visible = false;
        if (g_editor_ui->file_browser_panel.refresh_button)
          g_editor_ui->file_browser_panel.refresh_button->visible = false;

        if (g_editor_ui->file_browser_panel.home_button)
          g_editor_ui->file_browser_panel.home_button->visible = false;
        if (g_editor_ui->file_browser_panel.close_button)
          g_editor_ui->file_browser_panel.close_button->visible = false;
      }
    }
    else
    {
      printf("[editor] Failed to load world from: %s\n", filepath);
    }
  }
  else
  {
    printf("[editor] Not a world file: %s\n", filepath);
  }
}

// File browser callback for when directory changes
static void on_file_browser_directory_changed(const char *new_path, void *user_data)
{
  (void)user_data; // Unused parameter

  printf("[editor] File browser: Directory changed to: %s\n", new_path);

  if (!g_editor_ui || !g_editor_ui->file_browser_panel.file_browser)
    return;

  UIFileBrowser *browser = g_editor_ui->file_browser_panel.file_browser;

  if (strcmp(new_path, "..") == 0)
  {
    // Go up one directory
    world_editor_file_browser_go_up();
  }
  else if (strstr(new_path, "/") || strstr(new_path, "\\"))
  {
    // Navigate to directory
    if (browser->current_path)
      free(browser->current_path);

    // Handle relative paths
    if (new_path[0] == '/')
    {
      // Absolute path
      browser->current_path = strdup(new_path);
    }
    else
    {
      // Relative path - append to current path
      char *full_path = malloc(strlen(browser->current_path) + strlen(new_path) + 2);
      sprintf(full_path, "%s/%s", browser->current_path, new_path);
      browser->current_path = full_path;
    }

    // Refresh the file list
    world_editor_populate_file_browser(browser->current_path);
  }
}

// ============================================================================
// FILE BROWSING FUNCTIONS
// ============================================================================

// Scan directory and populate file browser
static void world_editor_populate_file_browser(const char *path)
{
  printf("[editor] world_editor_populate_file_browser called with path: %s\n", path);

  if (!g_editor_ui || !g_editor_ui->file_browser_panel.file_browser)
  {
    printf("[editor] Error: No editor UI or file browser available\n");
    return;
  }

  UIFileBrowser *browser = g_editor_ui->file_browser_panel.file_browser;

  printf("[editor] Freeing existing file list\n");
  // Free existing file list
  if (browser->file_list)
  {
    for (int i = 0; i < browser->file_count; i++)
    {
      if (browser->file_list[i])
        free(browser->file_list[i]);
    }
    free(browser->file_list);
    browser->file_list = NULL;
    browser->file_count = 0;
  }

  printf("[editor] Opening directory: %s\n", path);
  // Open directory for reading
  DIR *dir = opendir(path);
  if (!dir)
  {
    printf("[editor] Error: Could not open directory %s\n", path);
    // If we can't open the directory, just add a placeholder
    browser->file_count = 1;
    browser->file_list = malloc(sizeof(char *));
    browser->file_list[0] = strdup("(Directory not accessible)");
    return;
  }

  printf("[editor] Directory opened successfully, counting entries\n");
  // Count files and directories (add 1 for ".." entry)
  struct dirent *entry;
  int count = 1; // Start with 1 for ".." entry
  while ((entry = readdir(dir)) != NULL)
  {
    // Skip hidden files
    if (entry->d_name[0] == '.')
      continue;

    // Apply file filter for .world files
    if (entry->d_type == DT_REG)
    {
      const char *ext = strrchr(entry->d_name, '.');
      if (!ext || strcmp(ext, ".world") != 0)
        continue;
    }

    count++;
  }
  rewinddir(dir);

  printf("[editor] Found %d entries, allocating file list\n", count);
  // Allocate file list
  browser->file_count = count;
  browser->file_list = malloc(count * sizeof(char *));

  // Add ".." entry at the top
  browser->file_list[0] = strdup("..");

  printf("[editor] Populating file list\n");
  // Populate file list starting from index 1
  int index = 1;
  while ((entry = readdir(dir)) != NULL && index < count)
  {
    // Skip hidden files
    if (entry->d_name[0] == '.')
      continue;

    // Apply file filter for .world files
    if (entry->d_type == DT_REG)
    {
      const char *ext = strrchr(entry->d_name, '.');
      if (!ext || strcmp(ext, ".world") != 0)
        continue;
    }

    // Determine if it's a directory
    bool is_dir = (entry->d_type == DT_DIR);

    // Create entry name (add / for directories)
    char *name = malloc(strlen(entry->d_name) + 2);
    if (is_dir)
      sprintf(name, "%s/", entry->d_name);
    else
      strcpy(name, entry->d_name);

    browser->file_list[index++] = name;
  }

  closedir(dir);

  printf("[editor] Sorting file list\n");
  // Sort the list: directories first, then files, both alphabetically
  // Note: ".." stays at index 0, sort the rest starting from index 1
  for (int i = 1; i < browser->file_count - 1; i++)
  {
    for (int j = i + 1; j < browser->file_count; j++)
    {
      bool i_is_dir = (browser->file_list[i][strlen(browser->file_list[i]) - 1] == '/');
      bool j_is_dir = (browser->file_list[j][strlen(browser->file_list[j]) - 1] == '/');

      // Directories come before files
      if (i_is_dir && !j_is_dir)
        continue;
      if (!i_is_dir && j_is_dir)
      {
        // Swap
        char *temp = browser->file_list[i];
        browser->file_list[i] = browser->file_list[j];
        browser->file_list[j] = temp;
        continue;
      }

      // Both same type, sort alphabetically
      if (strcmp(browser->file_list[i], browser->file_list[j]) > 0)
      {
        char *temp = browser->file_list[i];
        browser->file_list[i] = browser->file_list[j];
        browser->file_list[j] = temp;
      }
    }
  }

  printf("[editor] File browser population completed with %d entries\n", browser->file_count);
}

// Navigate to parent directory
static void world_editor_file_browser_go_up(void)
{
  if (!g_editor_ui || !g_editor_ui->file_browser_panel.file_browser)
    return;

  UIFileBrowser *browser = g_editor_ui->file_browser_panel.file_browser;

  if (!browser->current_path)
    return;

  // Find the last slash
  char *last_slash = strrchr(browser->current_path, '/');
  if (last_slash && last_slash != browser->current_path)
  {
    // Remove the last directory component
    *last_slash = '\0';

    // If we're at root, add a trailing slash
    if (strlen(browser->current_path) == 0)
      strcpy(browser->current_path, "/");

    // Refresh the file list
    world_editor_populate_file_browser(browser->current_path);
  }
}

// Navigate to home directory (worlds/)
static void world_editor_file_browser_go_home(void)
{
  if (!g_editor_ui || !g_editor_ui->file_browser_panel.file_browser)
    return;

  UIFileBrowser *browser = g_editor_ui->file_browser_panel.file_browser;

  // Set path to worlds directory
  if (browser->current_path)
    free(browser->current_path);
  browser->current_path = strdup("worlds/");

  // Refresh the file list
  world_editor_populate_file_browser(browser->current_path);
}

// Refresh current directory
static void world_editor_file_browser_refresh(void)
{
  if (!g_editor_ui || !g_editor_ui->file_browser_panel.file_browser)
    return;

  UIFileBrowser *browser = g_editor_ui->file_browser_panel.file_browser;

  if (browser->current_path)
    world_editor_populate_file_browser(browser->current_path);
}

// Callback for refresh button
static void on_file_browser_refresh_click(int button_id, void *user_data)
{
  (void)button_id; // Unused parameter
  (void)user_data; // Unused parameter

  world_editor_file_browser_refresh();
}

// Callback for home button
static void on_file_browser_home_click(int button_id, void *user_data)
{
  (void)button_id; // Unused parameter
  (void)user_data; // Unused parameter

  world_editor_file_browser_go_home();
}
