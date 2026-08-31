/*
 * client_render.c - Rendering pipeline implementation
 */

#include "client_render.h"
#include "client_state.h"
#include "window.h"
#include "game_state.h"
#include "player_controls.h"
#include "mob_ai.h"
#include "item.h"
#include <SDL2/SDL.h>
#include <stdio.h>

// Initialize rendering system
void client_render_init(void) {
    // Nothing specific to initialize yet
}

// Main rendering function
void render_game(void) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    // Debug: Print current screen
    static int last_screen = -1;
    if (last_screen != g_game_state->current_screen) {
        printf("Screen changed to: %d\n", g_game_state->current_screen);
        last_screen = g_game_state->current_screen;
    }

    // Clear screen
    window_clear();

    // Render title screen if needed
    if (client_state_is_title_screen()) {
        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - client_state_get_title_start_time();
        window_set_title_mode(true);
        window_render_title_screen(client_state_get_title_fade(), elapsed);
        window_present();
        return;
    }

    // Render based on current screen
    switch (g_game_state->current_screen) {
    case GAME_SCREEN_MAIN_MENU:
        window_render_main_menu();
        break;

    case GAME_SCREEN_WORLD:
        if (g_game_state->game_started) {
            World *world = g_game_state->current_world ? g_game_state->current_world : g_game_state->main_menu_world;
            if (window_is_fp_mode()) {
                window_render_fp_world(world, g_game_state->player_x, g_game_state->player_y,
                                     g_game_state->player_z, g_game_state->player_name);
            } else {
                window_render_isomorphic_world(world, g_game_state->player_x, g_game_state->player_y,
                                             g_game_state->player_z, g_game_state->player_name);
            }
        } else {
            if (window_is_fp_mode()) {
                window_render_fp_world(g_game_state->main_menu_world, g_game_state->player_x,
                                     g_game_state->player_y, g_game_state->player_z, "Main Menu");
            } else {
                window_render_isomorphic_world(g_game_state->main_menu_world, g_game_state->player_x,
                                             g_game_state->player_y, g_game_state->player_z, "Main Menu");
            }
        }
        break;

    case GAME_SCREEN_INVENTORY:
        {
            // World stays visible; inventory is a centered modal overlay.
            if (g_game_state->game_started) {
                World *world = g_game_state->current_world ? g_game_state->current_world : g_game_state->main_menu_world;
                if (window_is_fp_mode()) {
                    window_render_fp_world(world, g_game_state->player_x, g_game_state->player_y,
                                         g_game_state->player_z, g_game_state->player_name);
                } else {
                    window_render_isomorphic_world(world, g_game_state->player_x, g_game_state->player_y,
                                                 g_game_state->player_z, g_game_state->player_name);
                }
            }
            const char *spirit_name = g_game_state->player_name[0] ? g_game_state->player_name : "Spirit";
            const Inventory *inv = g_game_state->player ? &g_game_state->player->inventory : NULL;
            const Equipment *eq = NULL;
            const char *body_name = NULL;
            if (player_controls_is_dominating(g_game_state)) {
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
            if (g_game_state->game_started) {
                World *world = g_game_state->current_world ? g_game_state->current_world : g_game_state->main_menu_world;
                if (window_is_fp_mode()) {
                    window_render_fp_world(world, g_game_state->player_x, g_game_state->player_y,
                                         g_game_state->player_z, g_game_state->player_name);
                } else {
                    window_render_isomorphic_world(world, g_game_state->player_x, g_game_state->player_y,
                                                 g_game_state->player_z, g_game_state->player_name);
                }
            }
            const Actor *spirit = g_game_state->player;
            const Actor *body = NULL;
            if (player_controls_is_dominating(g_game_state))
                body = player_controls_dominated_actor(g_game_state);
            window_render_character_profile(spirit, body);
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
        if (g_game_state->show_loading) {
            window_render_loading_screen(g_game_state);
        }
        break;

    case GAME_SCREEN_CHAPTER:
        // Render chapter/introduction scene
        if (g_game_state->show_chapter) {
            const char *chapter_title = "Genesis";
            const char *chapter_content = "Welcome to VERSE...\n\nA world of endless possibilities awaits you.\n\nPress ENTER to continue...";
            window_render_chapter_with_fade(chapter_title, chapter_content, g_game_state->chapter_fade_alpha,
                                          g_game_state->chapter_current_char, g_game_state->chapter_total_chars);
        }
        break;

    default:
        window_render_main_menu();
        break;
    }

    // Render modals
    if (g_game_state->show_exit_prompt) {
        window_render_exit_prompt();
    }

    if (g_game_state->show_new_game_warning) {
        window_render_new_game_warning();
    }

    if (g_game_state->show_name_input) {
        window_render_name_input("Enter your character name:", g_game_state->player_name);
    }

    // Present the frame
    window_present();
}
