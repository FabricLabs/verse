#ifndef ISOMETRIC_RENDERER_H
#define ISOMETRIC_RENDERER_H

#include "world.h"
// For FogAtlas, which appears in this header's own struct and prototypes. It used to be left to
// whoever included this file to have included fog_of_war.h first, so adding an unrelated include
// anywhere upstream could break the build.
#include "fog_of_war.h"
#include "foliage_bend.h"
#include "projectile.h"
#include "debris.h"
#include <SDL2/SDL.h>

typedef struct FogAtlas FogAtlas;

// Edge connection offsets for the 26 adjacent worlds
// These match the adjacent_farm_worlds and adjacent_home_worlds arrays
typedef struct {
    int dx, dy, dz;
} WorldOffset;

// Chunk dimensions for rendering (greedy meshing to follow)
#include "chunk_config.h"
#define ISO_CHUNK_SIZE_X CHUNK_SIZE_X
#define ISO_CHUNK_SIZE_Y CHUNK_SIZE_Y
#define ISO_CHUNK_SIZE_Z CHUNK_SIZE_Z

// Voxel render data for optimized rendering
typedef struct {
    int world_x, world_y, world_z;  // Position in world space
    int screen_x, screen_y;         // Position in screen space
    VoxelType type;                 // Type of voxel
    int depth;                      // Depth for sorting
    SDL_Color color;                // Color to render
    bool visible_faces[6];          // Which faces are visible
    int world_index;                // Which world this voxel belongs to
    uint8_t damage;                 // VOXEL_FIELD_DAMAGE, for crack overlays
    bool fog_hidden;                // Unexplored under fog of war — draw as black, ignore textures
} VoxelRenderData;

// Isometric renderer state
typedef struct {
    // Camera position (center of view)
    int camera_x, camera_y, camera_z;
    float camera_world_x, camera_world_y, camera_world_z;

    // Player avatar overlay (drawn at screen center when camera tracks player)
    bool show_player_avatar;
    float player_facing_yaw;
    bool player_attacking;
    float player_attack_progress; // 0..1 while swinging
    bool player_attack_armed;
    float player_attack_radius;
    uint32_t player_attack_strength;
    uint32_t player_stamina;
    uint32_t player_stamina_max;

    // Rendering parameters
    int tile_width;         // Width of isometric tile
    int tile_height;        // Height of isometric tile
    int voxel_height;       // Height of voxel in pixels
    int render_distance;    // How far to render from camera
    float zoom_scale;       // Zoom multiplier (1.0 = default)

    // Screen dimensions
    int screen_width;
    int screen_height;
    int screen_center_x;
    int screen_center_y;
    // When drawing into a constrained viewport (e.g., PIP), allow caller to disable the internal clear
    bool allow_clear;

    // Render buffer
    VoxelRenderData* render_buffer;
    int render_buffer_size;
    int render_buffer_capacity;

    // Stats
    int last_rendered_voxel_count;

    // Scan cost for the frame, reset by isometric_renderer_clear_buffer. The ratio of
    // visited to emitted is what says whether the scan is finding work or just walking air, which is
    // the number to watch when tuning culling — see verse_benchmark.
    long scan_visited;  // cells the inner loop touched
    long scan_air;      // rejected as empty
    long scan_solid;    // solid, so face visibility was computed
    long scan_emitted;  // reached the draw buffer

    // Edge-connected worlds data
    GameWorlds* game_worlds;
    FogAtlas *fog;

    // What the player has in flight and on the ground as debris, borrowed from GameState each frame.
    // NULL in the editor and other standalone tools.
    const ProjectileSystem *projectiles;
    const DebrisSystem *debris;
    // Actor currently speaking, if any. Zero means no bubble. Set from GameState each frame.
    uint32_t dialogue_speaker_id;
    // Runtime actor selected by click in isometric view. Zero = none. Name labels only show for this id.
    uint32_t selected_actor_id;
    // Optional: adjacent worlds for editor preview (center + 4 edges). Indices use world_offsets mapping
    World* edge_worlds[125];
    float world_alpha[125]; // per-world alpha for rendering (1.0 = opaque)

    // Camera behavior
    bool auto_center_camera; // when true, renderer recenters based on content bbox
    WorldOffset world_offsets[125];  // Center + 2-ring (5x5x5)

    // Voxel interaction state
    bool has_highlighted_voxel;
    int highlighted_x, highlighted_y, highlighted_z;

    // Hero selection state
    bool hero_selected;

    // Mouse interaction state for battle arena
    bool movement_target_set;
    int movement_target_x, movement_target_y, movement_target_z;
    bool show_movement_preview;

    // Player movement state
    int player_x, player_y, player_z;
    bool player_moving;
    float move_progress;

    // Mass highlight by type (for palette/chart hover)
    bool mass_highlight_active;
    int highlighted_type; // -1 none, otherwise VoxelType value

  // Optional: override per-pixel color for layer worlds using embedded 255-color texture
  bool layer_texture_override_active;
  int layer_tex_w;
  int layer_tex_h;
  SDL_Color* layer_tex_pixels; // length = layer_tex_w * layer_tex_h

  // Per-voxel-type layer textures loaded from models/layer_*.world (32x32x1)
  SDL_Color* per_type_layer_pixels[VOXEL_COUNT]; // NULL if not present; otherwise width*height
  int per_type_layer_w[VOXEL_COUNT];
  int per_type_layer_h[VOXEL_COUNT];

  // Cached GPU vertex buffer for SDL_RenderGeometry path (reused across frames)
  SDL_Vertex* gpu_vertices;    // allocated array of SDL_Vertex
  size_t gpu_vertex_capacity;  // number of SDL_Vertex entries allocated

  // Adjacent world inclusion control: how many world steps beyond origin to include
  // 0 = only origin world, 1 = immediate neighbors, up to 2 (max supported by 5x5x5 offsets).
  // Default is 2 so the wilderness rings under the sky island are drawn as distance terrain.
  int neighbor_inclusion_radius;

  // Universe layer of the player. Wilderness at z=0 is drawn as scenery from the home island;
  // hide_wilderness_below_home remains for callers that want the old underfoot-hide behaviour.
  uint64_t player_universe_z;
  bool hide_wilderness_below_home;

  // Stamina meter overlay alpha (0..1)
  float stamina_meter_alpha;

  // Debug overlay: show world bounds/centers for placement verification
  bool debug_show_world_bounds;

      // Optimization toggles
    bool greedy_neighbors_top_only;    // if true, emit greedy top faces for neighbors, keep origin per-voxel
    bool show_alignment_baseline;      // if true, draw z=0 baselines for origin and a neighbor
    bool show_grid;                    // if true, render grid lines for debugging/readability
    bool disable_culling;              // if true, render entire world without distance culling

    // Sub-voxel rendering. The atlas holds every material template's baked faces; when it is
    // present a voxel face is drawn as a textured quad showing that material's sub-voxel structure
    // instead of a flat diamond. Built lazily on the first frame, since it needs an SDL_Renderer.
    struct MaterialAtlas *material_atlas;
    bool material_atlas_tried;   // so a failed build is not retried every frame
    bool subvoxel_detail_enabled; // VERSE_SUBVOXEL=0 turns it off for comparison

    // Water surfaces. Unlike a material template, no two water voxels look alike: each carries its
    // own wave field, disturbed by the fluid the simulation moves into it. Held here rather than on
    // the world because it is a property of what is being looked at, not of what exists — the cache
    // keeps the surfaces on screen and lets the rest go. See fluid_surface.h.
    struct FluidSurface *water_surface;
    uint32_t water_surface_last_ms; // for the fixed-rate wave step
    int water_faces_drawn;          // last frame's count, for diagnostics

    // Volumetric fog inside steam/gas voxels. Same live-cache idea as water_surface, but a 32³
    // density field per disturbed cloud cell rather than a 32² height field. See fog_volume.h.
    struct FogVolume *fog_volume;
    uint32_t fog_volume_last_ms;
    int fog_faces_drawn;

    // Developer console render modes
    bool wireframe_mode; // outline faces only, no fills
    bool xray_mode;      // most solid voxels drawn semi-transparent

    // Bodies that part foliage this frame (player / inhabited actor). Empty means no bend.
    FoliageBendField foliage_bend;

} IsometricRenderer;

void isometric_renderer_set_wireframe(IsometricRenderer *renderer, bool enabled);
void isometric_renderer_set_xray(IsometricRenderer *renderer, bool enabled);

// Initialize the isometric renderer
IsometricRenderer* isometric_renderer_create(int screen_width, int screen_height);
void isometric_renderer_destroy(IsometricRenderer* renderer);

// Set the game worlds to render
void isometric_renderer_set_game_worlds(IsometricRenderer* renderer, GameWorlds* game_worlds);
void isometric_renderer_set_fog(IsometricRenderer* renderer, FogAtlas* fog);

// Update camera position
void isometric_renderer_set_camera(IsometricRenderer* renderer, int x, int y, int z);
void isometric_renderer_set_camera_world(IsometricRenderer* renderer, float x, float y, float z);
void isometric_renderer_set_player_avatar(IsometricRenderer* renderer, float facing_yaw,
                                          bool attacking, float attack_progress, bool armed,
                                          float attack_radius, uint32_t attack_strength,
                                          bool show);
void isometric_renderer_set_player_stamina(IsometricRenderer* renderer,
                                           uint32_t stamina, uint32_t max_stamina);
void isometric_renderer_set_universe_layer(IsometricRenderer* renderer, uint64_t universe_z,
                                             bool hide_wilderness_below_home);
void isometric_renderer_set_stamina_meter_alpha(IsometricRenderer* renderer, float alpha);
// Enable/disable auto-centering camera in render pass
void isometric_renderer_set_auto_center(IsometricRenderer* renderer, bool enabled);

// Update renderer screen size (in pixels). Also resets center to screen midpoint.
void isometric_renderer_set_screen_size(IsometricRenderer* renderer, int screen_width, int screen_height);

// Enable/disable culling for full world visibility (useful for editors)
// Usage: isometric_renderer_set_disable_culling(renderer, true); // Disable culling to see entire world
void isometric_renderer_set_disable_culling(IsometricRenderer* renderer, bool disable);

// Convert between world and screen coordinates. Matches the GPU voxel lattice:
// camera XY maps to the window centre, then the point is projected in absolute
// world coordinates (see isometric_renderer_render_gpu).
void isometric_world_to_screen(IsometricRenderer* renderer,
                              int world_x, int world_y, int world_z,
                              int world_index,
                              int* screen_x, int* screen_y);

void isometric_world_to_screen_float(IsometricRenderer* renderer,
                                     float world_x, float world_y, float world_z,
                                     int world_index,
                                     int* screen_x, int* screen_y);

void isometric_screen_to_world(IsometricRenderer* renderer,
                              int screen_x, int screen_y,
                              int* world_x, int* world_y, int* world_z,
                              int* world_index);

// Main render function - renders all edge-connected worlds
void isometric_renderer_render(IsometricRenderer* renderer, SDL_Renderer* sdl_renderer);

// GPU-batched render using SDL_RenderGeometry. Falls back to CPU path if unavailable.
void isometric_renderer_render_gpu(IsometricRenderer* renderer, SDL_Renderer* sdl_renderer);

// Stats accessors
int isometric_renderer_get_last_rendered_count(IsometricRenderer* renderer);

  // Screen-space picking that mirrors the viewer's lattice and centering.
  // Returns true on hit and writes voxel/world coords.
  bool isometric_renderer_pick_voxel(IsometricRenderer* renderer,
                                     World* world,
                                     int view_z,
                                     int mouse_x,
                                     int mouse_y,
                                     uint32_t* out_x,
                                     uint32_t* out_y,
                                     uint32_t* out_z);

  // Mass highlight control
  void isometric_renderer_set_highlight_type(IsometricRenderer* renderer, int voxel_type);
  void isometric_renderer_clear_highlight_type(IsometricRenderer* renderer);

  // Get world_offsets index for a given (dx,dy,dz) neighbor. Returns -1 if not found.
  int isometric_renderer_offset_index(IsometricRenderer* renderer, int dx, int dy, int dz);

// Render a single world at offset
void isometric_renderer_render_world(IsometricRenderer* renderer,
                                   World* world,
                                   int world_index,
                                   int offset_x, int offset_y, int offset_z);

// Get voxel color based on type
SDL_Color isometric_get_voxel_color(VoxelType type);

// Clear render buffer
void isometric_renderer_clear_buffer(IsometricRenderer* renderer);

// Add voxel to render buffer
void isometric_renderer_add_voxel(IsometricRenderer* renderer,
                                 int world_x, int world_y, int world_z,
                                 int world_index,
                                 VoxelType type,
                                 bool visible_faces[6]);

// Sort render buffer by depth
void isometric_renderer_sort_buffer(IsometricRenderer* renderer);

// Draw a single voxel cube
void isometric_renderer_draw_voxel(IsometricRenderer *renderer,
                                  SDL_Renderer *sdl_renderer,
                                  VoxelRenderData *voxel);

// Comprehensive voxel rendering fix - ensures all voxels are rendered as proper cubes
void isometric_renderer_draw_voxel_comprehensive(IsometricRenderer *renderer,
                                                SDL_Renderer *sdl_renderer,
                                                VoxelRenderData *voxel);

// Voxel interaction functions
void isometric_renderer_set_highlighted_voxel(IsometricRenderer* renderer, int x, int y, int z);
void isometric_renderer_clear_highlighted_voxel(IsometricRenderer* renderer);

// Utility: draw an axis-aligned voxel-edge wireframe box using the same
// screen-space mapping as the isometric selection highlight. The box is
// defined by inclusive-exlusive edges: min at (x0,y0,z0), max at (x1,y1,z1).
// If screen_center_x/y are >=0, they override the renderer's screen center
// (useful when a viewport is active, e.g., PIP). Pass color and thickness flag.
void isometric_renderer_draw_box_wireframe_centered(IsometricRenderer* renderer,
                                                    SDL_Renderer* sdl_renderer,
                                                    int x0, int y0, int z0,
                                                    int x1, int y1, int z1,
                                                    SDL_Color color,
                                                    int screen_center_x,
                                                    int screen_center_y,
                                                    bool thick);

// Hero selection functions
void isometric_renderer_set_hero_selected(IsometricRenderer* renderer, bool selected);
void isometric_renderer_clear_all_selections(IsometricRenderer* renderer);
void isometric_renderer_set_selected_actor(IsometricRenderer *renderer, uint32_t actor_id);
uint32_t isometric_renderer_selected_actor(const IsometricRenderer *renderer);
// Nearest active runtime actor under the screen point (within pick_radius_px). 0 if none.
uint32_t isometric_renderer_pick_actor(IsometricRenderer *renderer, World *world, int world_index,
                                       int screen_x, int screen_y, int pick_radius_px);

// Mouse input handling for battle arena
bool isometric_renderer_handle_mouse_click(IsometricRenderer* renderer, int screen_x, int screen_y, int button);
bool isometric_renderer_handle_right_click_move(IsometricRenderer* renderer, int screen_x, int screen_y);
bool isometric_renderer_handle_left_click_select(IsometricRenderer* renderer, int screen_x, int screen_y);

// Movement path finding and validation
bool isometric_renderer_can_move_to(IsometricRenderer* renderer, int world_x, int world_y, int world_z);
void isometric_renderer_show_movement_preview(IsometricRenderer* renderer, int target_x, int target_y, int target_z);

// Improved face culling for proper cube rendering
bool isometric_should_render_face(IsometricRenderer* renderer, World* world, int x, int y, int z, int face_index);
void isometric_optimize_face_culling(IsometricRenderer* renderer, World* world, VoxelRenderData* voxel_data);

// Attempt to load a 32px top-down full-color texture override from world's log metadata
// Returns true if a valid texture was found and cached.
bool isometric_renderer_try_load_layer_texture(IsometricRenderer* renderer, World* world);

// Load all models/layer_*.world textures (32x32x1). Each filename suffix after 'layer_'
// is matched to a VoxelType name, and the world is converted to an RGB texture using
// world_voxel_type_color for each voxel. Invalid sizes are skipped.
// Returns number of textures loaded.
int isometric_renderer_load_layer_textures_from_models(IsometricRenderer* renderer, const char* models_dir);

#endif // ISOMETRIC_RENDERER_H
