#include "craft.h"
#ifndef WINDOW_H
#define WINDOW_H

#include "world.h"
#include "game_state.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "background_music.h"
#include "title_hum.h"
#include "settings.h"
#include "ui_sounds.h"

// Resolution and scaling constants
#define BASE_RESOLUTION_WIDTH 256
#define BASE_RESOLUTION_HEIGHT 240
#define MIN_SCALE_FACTOR 1
#define MAX_SCALE_FACTOR 4
#define DEFAULT_SCALE_FACTOR 2

// UI Button structure
typedef struct {
    int x, y, width, height;
    const char* text;
    int id;
    SDL_Color normal_color;
    SDL_Color hover_color;
    SDL_Color selected_color;
    SDL_Color text_color;
    bool selected;
} UIButton;

// Button callback function type
typedef void (*ButtonCallback)(int button_id);

// Window state structure
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *font_texture;
    SDL_Texture *base_render_texture;
    TTF_Font *font;
    int width;
    int height;
    int base_width;
    int base_height;
    int scale_factor;
    int cell_width;
    int cell_height;
    SDL_Color background_color;
    SDL_Color text_color;
    SDL_Color ui_color;
    SDL_Color highlight_color;
    void (*key_callback)(int key);
    void (*mouse_callback)(int x, int y, int button);
    void (*mouse_up_callback)(int x, int y, int button);
    void (*mouse_motion_callback)(int x, int y);
    void (*window_callback)(int event_type);
    ButtonCallback button_callback;
    UIButton buttons[16];
    int button_count;
    int fullscreen;
    int settings_section;
    int settings_selection;
    int text_speed;
    int tuning_scale;
    bool background_music_enabled;
    bool menu_sounds_enabled;
    int master_volume;
    int music_volume;
    bool custom_cursor_active;
    int cursor_x;
    int cursor_y;
} WindowState;

// Global window state (extern declaration)
extern WindowState window_state;

// Window initialization and cleanup
int window_init(const char* title, int width, int height);
void window_cleanup();
void window_toggle_render_mode();
bool window_is_fp_mode();

// Mouse look capture. While active the pointer is grabbed and motion is delivered as deltas via
// window_consume_mouse_look_delta instead of updating the on-screen cursor.
void window_set_mouse_look_active(bool active);
bool window_mouse_look_active(void);
void window_consume_mouse_look_delta(int *dx, int *dy);

// Basic rendering functions
void window_clear();
void window_present();
void window_render_text(const char* text, int x, int y, SDL_Color color);
void window_render_text_box(const char* text, int x, int y, int width, int height, SDL_Color bg_color, SDL_Color text_color);
void window_render_wrapped_text(const char* text, int x, int y, int max_width, SDL_Color color);
void window_render_help_modal();
void window_render_world_editor_modal(const World *world);
int window_world_editor_modal_hit(int x, int y, float *bar_t);

#define WINDOW_WORLD_EDITOR_HIT_NONE 0
#define WINDOW_WORLD_EDITOR_HIT_GRAVITY_DEC 1
#define WINDOW_WORLD_EDITOR_HIT_GRAVITY_INC 2
#define WINDOW_WORLD_EDITOR_HIT_GRAVITY_BAR 3
#define WINDOW_WORLD_EDITOR_HIT_RESET 4
#define WINDOW_WORLD_EDITOR_HIT_CLOSE 5
void window_render_name_input(const char* prompt, const char* current_name);
void window_render_chapter(const char* chapter_title, const char* chapter_content);
void window_render_chapter_with_fade(const char* chapter_title, const char* chapter_content, float fade_alpha, int current_char, int total_chars);
void window_render_scene_text_box(const char* text, int current_char, int total_chars);
void window_render_dialogue_box(const char* speaker, const char* text, int current_char,
                                int line_index, int line_count);
void window_render_quest_box(const char* quest_name, const char* quest_description, int completed_objectives, int total_objectives);
// Mission journal modal (active + completed history, newest first). Caller paints the world first.
// scroll_y is pixels into the scrollable body; clamped and written back if non-NULL.
// mission_tracked controls the Track/Untrack control for the active mission's nav aide.
void window_render_quest_journal(const StorylineState *storyline, int *scroll_y, bool mission_tracked);
// True when (base-resolution) x,y hits the journal Track/Untrack control from the last paint.
bool window_quest_journal_hit_track(int base_x, int base_y);
void window_render_legends(const Chronicle *chronicle, int *scroll_y);
void window_render_game_log(const GameLog *log);
void window_render_nav_aide(GameState *state);

// World rendering
void window_render_world_grid(World* world, int player_x, int player_y, int player_z);
void window_render_ui_panel(const char* title, const char* content, int x, int y, int width, int height);

// Map framebuffer and rendering (overhead / isometric / actor views)
typedef enum {
    MAP_CAMERA_OVERHEAD = 0,
    MAP_CAMERA_ISOMETRIC = 1,
    MAP_CAMERA_ACTOR = 2
} MapCameraType;

typedef struct {
    MapCameraType camera_type;  // overhead, isometric, actor
    int width;                  // framebuffer width
    int height;                 // framebuffer height
    float fov_degrees;          // reserved for perspective cameras
    int y_min;                  // min world Y to project (for overhead)
    int y_max;                  // max world Y to project (for overhead)
} MapRenderParams;

typedef struct {
    SDL_Texture* texture;       // SDL target texture used as framebuffer
    int width;
    int height;
} MapFramebuffer;

// Map framebuffer lifecycle
bool window_mapfb_init(MapFramebuffer* fb, int width, int height);
void window_mapfb_destroy(MapFramebuffer* fb);

// Render map into framebuffer
void window_render_map(World* world,
                       int player_x, int player_y, int player_z,
                       const MapRenderParams* params,
                       MapFramebuffer* fb,
                       const FogAtlas* fog);

// Unified world renderer (uses framebuffer for all camera types)
void window_render_world_view(World* world,
                              int player_x, int player_y, int player_z,
                              const MapRenderParams* params,
                              MapFramebuffer* fb,
                              const FogAtlas* fog);

// Read pixels from framebuffer (ARGB8888)
bool window_mapfb_read_pixels(MapFramebuffer* fb, void* dst_pixels, int dst_pitch_bytes);

// Screen rendering
void window_render_main_menu();
void window_render_game_world(World* world, int player_x, int player_y, int player_z, const char* player_name);
void window_render_battle_interface(int time_remaining, const char* enemy_info);
void window_render_character_sheet(const char* player_name, int strength, int dexterity, int intelligence,
                                 int wisdom, int constitution, int luck, int experience_points);
// Minecraft-like inventory modal (draws over the current frame; caller should paint the world first).
// equipment NULL = spirit form (bag only). body_name is reserved for future labeling.
void window_render_inventory(const char *owner_name, const Wallet *purse,
                             const Inventory *inventory,
                             const Equipment *equipment,
                             const char *body_name);

typedef enum {
    WINDOW_INV_HIT_NONE = 0,
    WINDOW_INV_HIT_BAG,
    WINDOW_INV_HIT_EQUIP
} WindowInvHit;

// Hit-test inventory modal. index is bag slot or EquipmentSlot.
WindowInvHit window_inventory_hit(int mouse_x, int mouse_y, int *out_index);

// Settlement shop modal (shop stock + player bag). Caller paints the world first.
void window_render_shop(const char *merchant_name,
                        const Wallet *player_purse,
                        const Wallet *merchant_purse,
                        const ShopSession *shop,
                        const Inventory *player_inv);

typedef enum {
    WINDOW_SHOP_HIT_NONE = 0,
    WINDOW_SHOP_HIT_LISTING,
    WINDOW_SHOP_HIT_PLAYER_BAG,
    WINDOW_SHOP_HIT_CLOSE
} WindowShopHit;

WindowShopHit window_shop_hit(int mouse_x, int mouse_y, int *out_index);

// Crafting / recipe-book modal. Caller paints the world first.
void window_render_craft(const CraftSession *craft, const Inventory *player_inv);

typedef enum {
    WINDOW_CRAFT_HIT_NONE = 0,
    WINDOW_CRAFT_HIT_RECIPE,
    WINDOW_CRAFT_HIT_CRAFT,
    WINDOW_CRAFT_HIT_CLOSE
} WindowCraftHit;

WindowCraftHit window_craft_hit(int mouse_x, int mouse_y, int *out_index);

// Corpse loot modal (two bags + Loot All). Caller paints the world first.
void window_render_loot(const char *corpse_name,
                        const Inventory *corpse_inv,
                        const Equipment *corpse_eq,
                        const Inventory *player_inv,
                        const ItemStack *held);

typedef enum {
    WINDOW_LOOT_HIT_NONE = 0,
    WINDOW_LOOT_HIT_CORPSE_BAG,
    WINDOW_LOOT_HIT_CORPSE_EQUIP,
    WINDOW_LOOT_HIT_PLAYER_BAG,
    WINDOW_LOOT_HIT_LOOT_ALL,
    WINDOW_LOOT_HIT_CLOSE
} WindowLootHit;

// Hit-test loot modal controls. index is bag/equip slot when applicable.
WindowLootHit window_loot_hit(int mouse_x, int mouse_y, int *out_index);


// Character profile modal (spirit + optional inhabited body). Caller paints the world first.
void window_render_character_profile(const Actor *spirit, const Actor *body);

// Hit-test a + button on the character profile. Returns true and fills out_is_body / out_attr.
bool window_character_profile_hit(int mouse_x, int mouse_y, bool *out_is_body,
                                  ActorAttribute *out_attr);

// Hit-test a skill-tree unlock button. Returns true and fills out_skill when the click can buy it.
bool window_character_profile_skill_hit(int mouse_x, int mouse_y, SkillId *out_skill);

// Character-screen skill UI: hotbar strip + optional scrollable skill-list popout.
typedef enum {
    WINDOW_CHAR_SKILL_HIT_NONE = 0,
    WINDOW_CHAR_SKILL_HIT_OPEN,    // "Skills" button on the hotbar strip
    WINDOW_CHAR_SKILL_HIT_CLOSE,   // close the skill-list popout
    WINDOW_CHAR_SKILL_HIT_UNLOCK,  // purchasable row; out_skill set
    WINDOW_CHAR_SKILL_HIT_DRAG,    // known skill row (or hotbar chip) for drag; out_skill set
    WINDOW_CHAR_SKILL_HIT_HOTBAR   // hotbar slot; out_slot set (0-based)
} WindowCharSkillHit;

WindowCharSkillHit window_character_skill_ui_hit(int mouse_x, int mouse_y,
                                                 SkillId *out_skill, int *out_slot);

void window_character_skills_toggle(void);
void window_character_skills_close(void);
bool window_character_skills_is_open(void);
void window_character_skills_on_wheel(int wheel_y); // SDL wheel.y; only when popout open

// Drag ghost for hotbar assignment (list → slot or slot → slot).
void window_character_skill_drag_begin(SkillId skill, int from_hotbar_slot); // -1 = from list
void window_character_skill_drag_clear(void);
SkillId window_character_skill_drag_skill(void);
int window_character_skill_drag_from_slot(void); // -1 if from list / inactive
void window_character_skills_reset_ui(void); // close popout + clear drag (leave character screen)

void window_render_exit_prompt();
void window_render_new_game_warning();
void window_render_loading_bar(int current, int total, const char* message);
void window_render_in_game_menu();
void window_render_settings();
void window_render_save_browser();

// The main menu caches whether any saves exist so it does not scan the directory every paint.
// Call this after writing or deleting a save so Continue / Load Game appear or vanish.
void window_invalidate_saves_exist_cache(void);

// UI Button functions
void window_add_button(int x, int y, int width, int height, const char* text, int id);
void window_clear_buttons();
int window_handle_button_click(int mouse_x, int mouse_y);
void window_render_buttons();
void window_set_button_callback(ButtonCallback callback);

// Selection functions
void window_select_button(int button_id);
void window_clear_selection();
int window_get_selected_button();
void window_next_selection();
void window_prev_selection();
void window_activate_selection();

// Event handling
int window_handle_events();

// Resolution and scaling functions
void window_set_scale_factor(int scale_factor);
int window_get_scale_factor();
void window_get_base_size(int* width, int* height);
void window_get_scaled_size(int* width, int* height);

// Fullscreen functions
void window_toggle_fullscreen();
void window_set_fullscreen(int fullscreen);
int window_is_fullscreen();

// Text speed functions
void window_set_text_speed(int speed);
int window_get_text_speed();

// Settings navigation functions
void window_settings_next_section();
void window_settings_prev_section();
void window_settings_next_selection();
void window_settings_prev_selection();

// Settings persistence
void window_save_settings();
void window_load_settings();

// Text input functions
void window_start_text_input();
void window_stop_text_input();
void window_handle_text_input(const char* text);
int window_get_settings_section();
int window_get_settings_selection();

// Tuning scale functions
void window_set_tuning_scale(int scale);
int window_get_tuning_scale();

// Settings change functions
void window_change_text_speed();
void window_change_background_music(BackgroundMusicSystem* music);
void window_change_menu_sounds(UISoundSystem* ui_sounds);
void window_change_master_volume(BackgroundMusicSystem* music);
void window_change_music_volume(BackgroundMusicSystem* music);
void window_change_tuning_scale(BackgroundMusicSystem* music);
void window_change_scale_factor();
void window_change_fullscreen();
void window_activate_setting(BackgroundMusicSystem* music, UISoundSystem* ui_sounds);
void window_sync_settings_with_audio(BackgroundMusicSystem* music);

// Fullscreen and scaling functions
int window_calculate_optimal_scale_factor();
void window_set_fullscreen_with_optimal_scale();
void window_set_windowed_with_optimal_scale();
void window_center_rendering();

// Settings rendering functions
void render_video_settings();
void render_sound_settings();
void render_preferences_settings();

// Utility functions
void window_get_size(int* width, int* height);

// Callback function types
typedef void (*KeyCallback)(int key);
typedef void (*MouseCallback)(int x, int y, int button);
typedef void (*MouseUpCallback)(int x, int y, int button);
typedef void (*MouseMotionCallback)(int x, int y);
typedef void (*WindowCallback)(int event_type);

// Callback setters
void window_set_key_callback(KeyCallback callback);
void window_set_mouse_callback(MouseCallback callback);
void window_set_mouse_up_callback(MouseUpCallback callback);
void window_set_mouse_motion_callback(MouseMotionCallback callback);

void window_set_custom_cursor_active(bool active);
void window_render_custom_cursor(void);
void window_get_cursor_pos(int *x, int *y);
void window_render_player_spirit_overlay(float facing_yaw, uint32_t stamina, uint32_t stamina_max,
                                         float stamina_meter_alpha);
void window_set_window_callback(WindowCallback callback);

// Selection system types
typedef enum {
    SELECTION_NONE = 0,
    SELECTION_VOXEL,
    SELECTION_HERO,
    SELECTION_PET,
    SELECTION_TELEPORT_HOME,
    SELECTION_STORY_JOURNAL
} SelectionType;

typedef struct {
    SelectionType type;
    union {
        struct {
            int x, y, z;
        } voxel;
        struct {
            int player_x, player_y, player_z;
            char name[64];
        } hero;
        struct {
            char name[64];
        } pet;
        struct {
            char home_name[64];
        } teleport_home;
        struct {
            char journal_title[64];
        } story_journal;
    } data;
} Selection;

// Voxel interaction functions (legacy, still used for hover)
void window_set_hovered_voxel(int x, int y, int z);
void window_clear_hovered_voxel(void);
void window_set_voxel_info(int x, int y, int z);
void window_clear_voxel_info(void);
bool window_is_voxel_info_open(void);

// New selection system functions
void window_set_selection(SelectionType type, void* data);
void window_clear_entity_selection(void);
Selection* window_get_current_selection(void);
void window_render_selection_info(void);

// Resolution constants
#define BASE_RESOLUTION_WIDTH 256
#define BASE_RESOLUTION_HEIGHT 240
#define MIN_SCALE_FACTOR 1
#define MAX_SCALE_FACTOR 4
#define DEFAULT_SCALE_FACTOR 2

// Window constants
#define DEFAULT_WINDOW_WIDTH (BASE_RESOLUTION_WIDTH * DEFAULT_SCALE_FACTOR)
#define DEFAULT_WINDOW_HEIGHT (BASE_RESOLUTION_HEIGHT * DEFAULT_SCALE_FACTOR)
#define WINDOW_TITLE "VERSE - A Robust Rogue-like RPG"

// UI Button IDs
#define BUTTON_NEW_GAME 1
#define BUTTON_LOAD_GAME 2
#define BUTTON_SETTINGS 3
#define BUTTON_EXIT 4
#define BUTTON_CONFIRM_EXIT 5
#define BUTTON_CANCEL_EXIT 6
#define BUTTON_CLOSE_HELP 7
#define BUTTON_CONFIRM_NAME 8
#define BUTTON_CONTINUE 9
#define BUTTON_CONFIRM_NEW_GAME 10
#define BUTTON_CANCEL_NEW_GAME 11
#define BUTTON_RESUME 12
#define BUTTON_SAVE_GAME 13
#define BUTTON_MAIN_MENU 14
#define BUTTON_SAVE_BROWSER_LOAD 40
#define BUTTON_SAVE_BROWSER_DELETE 41
#define BUTTON_SAVE_BROWSER_BACK 42
#define BUTTON_SAVE_BROWSER_CONFIRM_DELETE 43
#define BUTTON_SAVE_BROWSER_CANCEL_DELETE 44
#define BUTTON_SAVE_BROWSER_SLOT0 50
// Song editor button removed - now standalone program

// Isomorphic renderer functions
void window_render_isomorphic_world(World* world, int player_x, int player_y, int player_z, const char* player_name);
// Full isometric overview of the current world plus horizontal neighbours, with fog of war.
void window_render_world_map(World *world, int player_x, int player_y, int player_z);
// Map-screen hit test for settlement waypoints. Returns index or -1.
int window_map_hit_waypoint(GameState *state, int screen_x, int screen_y);
void window_render_fp_world(World* world, int player_x, int player_y, int player_z, const char* player_name);
void window_handle_isomorphic_mouse(int mouse_x, int mouse_y, int button);
int window_screen_to_world_coords(int screen_x, int screen_y, int* world_x, int* world_y, int* world_z);
int window_screen_to_world_coords_float(int screen_x, int screen_y, float* world_x, float* world_y, float* world_z);
void window_world_to_screen_coords(int world_x, int world_y, int world_z, int player_x, int player_y, int player_z, int* screen_x, int* screen_y);

// Movement destination system
typedef struct {
    int target_x;
    int target_y;
    int target_z;
    bool has_destination;
    float path_progress; // 0.0 to 1.0 for smooth movement
} MovementDestination;

extern MovementDestination g_movement_destination;

// Tutorial and quest system
typedef struct {
    char* title;
    char* description;
    int objectives_count;
    int completed_objectives;
    bool is_active;
    bool is_completed;
} TutorialQuest;

extern TutorialQuest g_tutorial_quest;
extern bool g_show_tutorial_modal;
extern bool g_tutorial_completed;

void window_render_tutorial_modal();
void window_init_tutorial_quest();
void window_update_tutorial_progress(const char* action);

// Title screen functions
// Measure text in pixels using the current font
void window_measure_text(const char* text, int* out_w, int* out_h);

// Title screen
void window_render_title_screen(float fade_alpha, Uint32 title_elapsed_ms);

// Loading screen functions
void window_render_loading_screen(GameState* game_state);

// Arrow and navigation functions
void window_render_arrow(int x, int y, SDL_Color color);

// Audio callback function
void audio_callback(void* userdata, Uint8* stream, int len);

// Audio system functions
typedef struct {
    BackgroundMusicSystem* music;
    TitleHumSystem* title_hum;
} AudioMixContext;

// Backward-compatible setup (no title hum)
bool window_setup_audio(BackgroundMusicSystem* music);
// Extended setup with title hum mix
bool window_setup_audio_with_title(BackgroundMusicSystem* music, TitleHumSystem* title_hum);
void window_cleanup_audio();

// Title mode hint for audio mixing (mute background music while true)
void window_set_title_mode(bool active);

#endif // WINDOW_H
