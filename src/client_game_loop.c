/*
 * client_game_loop.c - Main game loop implementation
 */

#include "client_game_loop.h"
#include "client_state.h"
#include "client_audio.h"
#include "client_render.h"
#include "client_input.h"
#include "window.h"
#include "game_state.h"
#include <SDL2/SDL.h>
#include <stdio.h>

// Main game loop
void game_loop(void) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) return;

    Uint32 last_time = SDL_GetTicks();
    static int frame_count = 0;
    static Uint32 fps_timer = 0;
    static float total_frame_time = 0.0f;
    static float max_frame_time = 0.0f;

    while (1) {
        Uint32 frame_start = SDL_GetTicks();

        // Handle events
        Uint32 event_start = SDL_GetTicks();
        int event_result = window_handle_events();
        if (event_result == -1 || client_input_should_exit()) {
            break; // Exit requested
        }
        Uint32 event_time = SDL_GetTicks() - event_start;

        // Calculate delta time
        Uint32 current_time = SDL_GetTicks();
        double delta_time = (current_time - last_time) / 1000.0;
        last_time = current_time;

        // Update title screen fade
        if (client_state_is_title_screen()) {
            Uint32 title_elapsed = current_time - client_state_get_title_start_time();
            if (title_elapsed < 2000) { // 2 second fade in
                client_state_set_title_fade((float)title_elapsed / 2000.0f);
            } else if (title_elapsed < 7000) { // Hold for 5 seconds
                client_state_set_title_fade(1.0f);
                // Start playing title hum after fade in
                static bool title_hum_started = false;
                if (!title_hum_started && title_elapsed >= 2000) {
                    client_audio_start_title_hum();
                    title_hum_started = true;
                }
            } else if (title_elapsed < 9000) { // 2 second fade out
                float fade = 1.0f - ((float)(title_elapsed - 7000) / 2000.0f);
                client_state_set_title_fade(fade);
            } else {
                // Title screen complete
                client_state_set_title_screen(false);
                client_audio_stop_title_hum();
                window_set_title_mode(false);
                // Play background music
                client_audio_play_ambient_track();
            }
        }

        // Update game state
        Uint32 update_start = SDL_GetTicks();

        // Update world generation if active
        if (g_game_state->world_generation_active) {
            if (game_state_update_world_generation(g_game_state)) {
                // Generation step completed
                if (!g_game_state->world_generation_active) {
                    // Generation finished!
                    printf("World generation complete! Starting game...\n");
                    if (game_state_finalize_world_generation(g_game_state)) {
                        // Move to chapter screen
                        g_game_state->current_screen = GAME_SCREEN_CHAPTER;
                        g_game_state->show_chapter = true;
                        g_game_state->chapter_current_page = 0;
                        g_game_state->chapter_current_char = 0;
                        g_game_state->chapter_total_chars = 0;
                        g_game_state->chapter_text_complete = false;
                        g_game_state->chapter_fade_alpha = 0.0f;
                        g_game_state->chapter_fade_start_time = SDL_GetTicks();
                        g_game_state->chapter_last_text_update = SDL_GetTicks();
                    } else {
                        printf("Failed to finalize world generation\n");
                        g_game_state->current_screen = GAME_SCREEN_MAIN_MENU;
                    }
                }
            }
        }

        // Update chapter fade and text reveal
        if (g_game_state->show_chapter) {
            Uint32 chapter_elapsed = current_time - g_game_state->chapter_fade_start_time;

            // Fade in over 1 second
            if (chapter_elapsed < 1000) {
                g_game_state->chapter_fade_alpha = (float)chapter_elapsed / 1000.0f;
            } else {
                g_game_state->chapter_fade_alpha = 1.0f;

                // Start revealing text after fade in
                if (g_game_state->chapter_total_chars == 0) {
                    // Initialize text length
                    const char *chapter_content = "Welcome to VERSE...\n\nA world of endless possibilities awaits you.\n\nPress ENTER to continue...";
                    g_game_state->chapter_total_chars = strlen(chapter_content);
                    g_game_state->chapter_last_text_update = current_time;
                }

                // Reveal text gradually (30 chars per second)
                if (!g_game_state->chapter_text_complete &&
                    current_time - g_game_state->chapter_last_text_update > 33) { // ~30 chars/sec
                    if (g_game_state->chapter_current_char < g_game_state->chapter_total_chars) {
                        g_game_state->chapter_current_char++;
                        g_game_state->chapter_last_text_update = current_time;
                    } else {
                        g_game_state->chapter_text_complete = true;
                    }
                }
            }
        }

        // Update name input caret blink
        if (g_game_state->show_name_input) {
            g_game_state->name_input_caret_visible = (SDL_GetTicks() / 500) % 2;
        }

        // Update player movement
        if (g_game_state->game_started) {
            game_state_update_player_movement(g_game_state, delta_time);
        }

        // Update runtime clock
        if (g_game_state->runtime_clock_running) {
            g_game_state->runtime_clock += delta_time;
        }

        Uint32 update_time = SDL_GetTicks() - update_start;

        // Update audio
        client_audio_update(delta_time);

        // Render
        Uint32 render_start = SDL_GetTicks();
        render_game();
        Uint32 render_time = SDL_GetTicks() - render_start;

        // Calculate frame time
        Uint32 frame_time = SDL_GetTicks() - frame_start;
        total_frame_time += frame_time;
        if (frame_time > max_frame_time) {
            max_frame_time = frame_time;
        }
        frame_count++;

        // Update FPS counter every second
        if (current_time - fps_timer >= 1000) {
            float avg_frame_time = total_frame_time / frame_count;
            float fps = 1000.0f / avg_frame_time;

            printf("FPS: %.1f | Avg frame: %.1fms | Max frame: %.1fms | Event: %ums | Update: %ums | Render: %ums\n",
                   fps, avg_frame_time, max_frame_time, event_time, update_time, render_time);

            // Reset counters
            fps_timer = current_time;
            frame_count = 0;
            total_frame_time = 0.0f;
            max_frame_time = 0.0f;
        }

        // Cap to ~60 FPS
        Uint32 target_frame_time = 16; // ~60 FPS
        if (frame_time < target_frame_time) {
            SDL_Delay(target_frame_time - frame_time);
        }
    }
}
