/*
 * client_game_loop_simple.c - Simplified game loop for the verse client
 *
 * This is a minimal version that compiles without missing functions.
 */

#include "client_game_loop.h"
#include "client_state.h"
#include "client_audio.h"
#include "client_render.h"
#include "client_input.h"
#include "window.h"
#include "game_state.h"
#include <SDL.h>
#include <stdio.h>

// Main game loop
void game_loop(void) {
    GameState* g_game_state = client_state_get_game();
    if (!g_game_state) {
        printf("No game state available\n");
        return;
    }

    Uint32 last_time = SDL_GetTicks();
    static int frame_count = 0;
    static Uint32 fps_timer = 0;
    static float total_frame_time = 0.0f;
    static float max_frame_time = 0.0f;

    while (1) {
        Uint32 frame_start = SDL_GetTicks();

        // Handle events
        Uint32 event_start = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                printf("SDL_QUIT event received\n");
                client_state_request_exit();
                break;
            case SDL_KEYDOWN:
                handle_key_press(event.key.keysym.sym);
                break;
            case SDL_MOUSEBUTTONDOWN:
                handle_mouse_click(event.button.x, event.button.y, event.button.button);
                break;
            case SDL_MOUSEMOTION:
                handle_mouse_motion(event.motion.x, event.motion.y);
                break;
            case SDL_MOUSEWHEEL:
                handle_mouse_wheel(event.wheel.x, event.wheel.y, event.wheel.y);
                break;
            case SDL_TEXTINPUT:
                handle_text_input(event.text.text);
                break;
            default:
                break;
            }
        }
        Uint32 event_time = SDL_GetTicks() - event_start;

        // Check for exit
        if (client_state_should_exit()) {
            printf("Exit requested\n");
            break;
        }

        // Calculate delta time
        Uint32 current_time = SDL_GetTicks();
        double delta_time = (current_time - last_time) / 1000.0;
        last_time = current_time;

        // Update title screen fade
        if (client_state_is_title_screen()) {
            // Title fade functionality not available in current GameState
            // Uint32 elapsed = current_time - g_game_state->title_start_time;
            // if (elapsed < 2000) { // 2 second fade
            //     g_game_state->title_fade_alpha = (float)elapsed / 2000.0f;
            // } else {
            //     g_game_state->title_fade_alpha = 1.0f;
            // }
        }

        // Update game state
        Uint32 update_start = SDL_GetTicks();

        // Update world generation if active
        if (g_game_state->world_generation_active) {
            if (game_state_update_world_generation(g_game_state)) {
                printf("World generation completed\n");
                g_game_state->world_generation_active = false;
                g_game_state->current_screen = GAME_SCREEN_WORLD;
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
                    // g_game_state->chapter_total_chars = strlen(g_game_state->chapter_content);
                    g_game_state->chapter_current_char = 0;
                    g_game_state->chapter_last_text_update = current_time;
                }

                // Reveal text gradually (30 chars per second)
                if (!g_game_state->chapter_text_complete &&
                    current_time - g_game_state->chapter_last_text_update >= 33) { // ~30 chars/sec
                    g_game_state->chapter_current_char += 1;
                    g_game_state->chapter_last_text_update = current_time;

                    if (g_game_state->chapter_current_char >= g_game_state->chapter_total_chars) {
                        g_game_state->chapter_text_complete = true;
                    }
                }
            }
        }

        // Update name input caret blink
        if (g_game_state->show_name_input) {
            // Simple caret blink - not using the missing field
            // g_game_state->name_input_caret_visible = (SDL_GetTicks() / 500) % 2;
        }

        // Update player movement
        if (g_game_state->game_started) {
            game_state_update_movement(g_game_state);
        }

        // Update runtime clock
        if (g_game_state->runtime_clock_running) {
            g_game_state->runtime_clock_ms += (uint64_t)(delta_time * 1000.0);
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
