#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "song_editor.h"
#include "songwriter/songwriter.h"
#include "synthesizer/synthesizer.h"
#include "sequencer/melody_loader.h"

// Window state for standalone editor
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    int width;
    int height;
    bool should_exit;
} StandaloneWindow;

static StandaloneWindow g_window = {0};
static Songwriter* g_songwriter = NULL;
static SongEditor* g_song_editor = NULL;

// Initialize SDL and create window
int window_init(const char* title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 0;
    }

    if (TTF_Init() < 0) {
        printf("TTF initialization failed: %s\n", TTF_GetError());
        return 0;
    }

    g_window.window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width,
        height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!g_window.window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        return 0;
    }

    g_window.renderer = SDL_CreateRenderer(
        g_window.window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!g_window.renderer) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return 0;
    }

    g_window.font = TTF_OpenFont("assets/fonts/visitor-tt2-brk.ttf", 12);
    if (!g_window.font) {
        g_window.font = TTF_OpenFont("/System/Library/Fonts/Helvetica.ttc", 12);
    }

    if (!g_window.font) {
        printf("Font loading failed: %s\n", TTF_GetError());
        return 0;
    }

    g_window.width = width;
    g_window.height = height;
    g_window.should_exit = false;

    return 1;
}

void window_cleanup() {
    if (g_window.font) {
        TTF_CloseFont(g_window.font);
    }
    if (g_window.renderer) {
        SDL_DestroyRenderer(g_window.renderer);
    }
    if (g_window.window) {
        SDL_DestroyWindow(g_window.window);
    }
    TTF_Quit();
    SDL_Quit();
}

void window_clear() {
    SDL_SetRenderDrawColor(g_window.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_window.renderer);
}

void window_present() {
    SDL_RenderPresent(g_window.renderer);
}

void window_render_text(const char* text, int x, int y, SDL_Color color) {
    if (!g_window.font) return;

    SDL_Surface* surface = TTF_RenderText_Solid(g_window.font, text, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(g_window.renderer, surface);
    if (texture) {
        SDL_Rect dest = {x, y, surface->w, surface->h};
        SDL_RenderCopy(g_window.renderer, texture, NULL, &dest);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
}

void window_render_rect(int x, int y, int width, int height, SDL_Color color) {
    SDL_SetRenderDrawColor(g_window.renderer, color.r, color.g, color.b, color.a);
    SDL_Rect rect = {x, y, width, height};
    SDL_RenderFillRect(g_window.renderer, &rect);
}

void window_render_line(int x1, int y1, int x2, int y2, SDL_Color color) {
    SDL_SetRenderDrawColor(g_window.renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(g_window.renderer, x1, y1, x2, y2);
}

// Audio callback for SDL
void audio_callback(void* userdata, Uint8* stream, int len) {
    if (!g_songwriter) return;

    float* float_stream = (float*)stream;
    int num_samples = len / sizeof(float);

    songwriter_generate_buffer(g_songwriter, float_stream, num_samples);
}

bool window_setup_audio() {
    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = 44100;
    desired.format = AUDIO_F32;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = audio_callback;
    desired.userdata = NULL;

    if (SDL_OpenAudio(&desired, &obtained) < 0) {
        printf("SDL audio setup failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_PauseAudio(0);
    return true;
}

void window_cleanup_audio() {
    SDL_CloseAudio();
}

// Handle SDL events
int window_handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                g_window.should_exit = true;
                return 1;

            case SDL_KEYDOWN:
                if (g_song_editor) {
                    song_editor_handle_keyboard(g_song_editor, event.key.keysym.sym, true);
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (g_song_editor) {
                    bool right_click = (event.button.button == SDL_BUTTON_RIGHT);
                    if (right_click) {
                        printf("SDL RIGHT-CLICK DETECTED at (%d, %d)\n", event.button.x, event.button.y);
                    }
                    song_editor_handle_mouse(g_song_editor, event.button.x, event.button.y, true, false, right_click);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (g_song_editor) {
                    bool right_click = (event.button.button == SDL_BUTTON_RIGHT);
                    song_editor_handle_mouse(g_song_editor, event.button.x, event.button.y, false, true, right_click);
                }
                break;

            case SDL_MOUSEWHEEL:
                if (g_song_editor) {
                    song_editor_handle_mouse_wheel(g_song_editor, 0, 0, event.wheel.y);
                }
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    g_window.width = event.window.data1;
                    g_window.height = event.window.data2;
                }
                break;
        }
    }
    return 0;
}

// Main function
int main(int argc, char* argv[]) {
    printf("Song Editor - Standalone Version\n");
    printf("================================\n");

    // Initialize window
    if (!window_init("Song Editor", 1200, 800)) {
        printf("Failed to initialize window\n");
        return 1;
    }

    // Initialize audio
    if (!window_setup_audio()) {
        printf("Failed to initialize audio\n");
        window_cleanup();
        return 1;
    }

    // Initialize songwriter
    g_songwriter = songwriter_create(44100);
    if (!g_songwriter) {
        printf("Failed to create songwriter\n");
        window_cleanup_audio();
        window_cleanup();
        return 1;
    }

    // Initialize song editor
    g_song_editor = song_editor_create(g_songwriter);
    if (!g_song_editor) {
        printf("Failed to create song editor\n");
        songwriter_destroy(g_songwriter);
        window_cleanup_audio();
        window_cleanup();
        return 1;
    }

    // Set the renderer for the song editor
    song_editor_set_renderer(g_window.renderer);

    // Create a new song
    if (!song_editor_new_song(g_song_editor, "New Song", "Composer", 120)) {
        printf("Failed to create new song\n");
        song_editor_destroy(g_song_editor);
        songwriter_destroy(g_songwriter);
        window_cleanup_audio();
        window_cleanup();
        return 1;
    }

    printf("Song Editor initialized successfully\n");
    printf("Controls:\n");
    printf("  Mouse: Click to add notes, right-click to remove\n");
    printf("  Space: Play/Pause\n");
    printf("  S: Select mode (without Ctrl)\n");
    printf("  D: Draw mode\n");
    printf("  E: Erase mode\n");
    printf("  I: Toggle instrument editor\n");
    printf("  Ctrl+S: Save song to JSON\n");
    printf("  Ctrl+O: Open song from JSON\n");
    printf("  +/-: Zoom in/out\n");
    printf("  Delete: Remove selected notes\n");
    printf("  ESC: Exit\n");

    // Main loop
    while (!g_window.should_exit) {
        window_handle_events();

        window_clear();

        if (g_song_editor) {
            song_editor_render(g_song_editor);
        }

        window_present();

        SDL_Delay(16); // ~60 FPS
    }

    // Cleanup
    song_editor_destroy(g_song_editor);
    songwriter_destroy(g_songwriter);
    window_cleanup_audio();
    window_cleanup();

    printf("Song Editor closed\n");
    return 0;
}
