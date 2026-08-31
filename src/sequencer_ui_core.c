/*
 * sequencer_ui_core.c - Core sequencer UI system implementation
 *
 * This is a minimal implementation for demonstration purposes.
 * In a full implementation, this would contain the complete UI system.
 */

#include "sequencer_ui_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Global SDL resources
static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static TTF_Font* g_font = NULL;

// Stub implementations for demonstration
SequencerUI* sequencer_ui_create(Songwriter* songwriter) {
    SequencerUI* ui = malloc(sizeof(SequencerUI));
    if (!ui) return NULL;

    memset(ui, 0, sizeof(SequencerUI));
    ui->songwriter = songwriter;
    ui->is_initialized = false;
    ui->is_open = false;
    ui->state = SEQUENCER_UI_STATE_IDLE;
    ui->mouse_mode = SEQUENCER_MOUSE_MODE_SELECT;
    ui->current_pattern = 0;
    // Initialize default 16-step patterns (kick on 1/5/9/13)
    for (int i = 0; i < 8; i++) {
        ui->pattern_steps[i] = 0x1111; // steps 0,4,8,12 active
    }

    // Set default window size
    ui->window_width = SEQUENCER_UI_DEFAULT_WIDTH;
    ui->window_height = SEQUENCER_UI_DEFAULT_HEIGHT;

    // Initialize layout rectangles
    ui->transport_rect = (SDL_Rect){0, 0, ui->window_width, SEQUENCER_TRANSPORT_HEIGHT};
    ui->track_list_rect = (SDL_Rect){0, SEQUENCER_TRANSPORT_HEIGHT, SEQUENCER_TRACK_LIST_WIDTH,
                                    ui->window_height - SEQUENCER_TRANSPORT_HEIGHT - SEQUENCER_MIXER_HEIGHT};
    ui->piano_roll_rect = (SDL_Rect){SEQUENCER_TRACK_LIST_WIDTH, SEQUENCER_TRANSPORT_HEIGHT,
                                    ui->window_width - SEQUENCER_TRACK_LIST_WIDTH,
                                    ui->window_height - SEQUENCER_TRANSPORT_HEIGHT - SEQUENCER_MIXER_HEIGHT};
    ui->mixer_rect = (SDL_Rect){0, ui->window_height - SEQUENCER_MIXER_HEIGHT,
                               ui->window_width, SEQUENCER_MIXER_HEIGHT};

    // Initialize timeline state
    ui->current_tick = 0;
    ui->visible_start_tick = 0;
    ui->visible_end_tick = 1920; // 4 measures at 120 BPM
    ui->pixels_per_tick = 1.0f;
    ui->pixels_per_note = 20.0f;
    ui->visible_notes_start = 60; // Start at C4
    ui->visible_notes_count = 24; // Show 2 octaves

    // Initialize interaction state
    ui->selected_track = 0;
    ui->selected_notes = NULL;
    ui->selected_note_count = 0;
    ui->is_dragging = false;

    // Initialize playback state
    ui->is_playing = false;
    ui->is_recording = false;
    ui->loop_start_tick = 0;
    ui->loop_end_tick = 1920;
    ui->loop_enabled = false;

    // Initialize zoom and scroll
    ui->horizontal_zoom = 1.0f;
    ui->vertical_zoom = 1.0f;
    ui->scroll_x = 0;
    ui->scroll_y = 0;

    printf("✓ SequencerUI created\n");
    return ui;
}

void sequencer_ui_destroy(SequencerUI* ui) {
    if (!ui) return;

    if (ui->selected_notes) {
        free(ui->selected_notes);
    }

    free(ui);
    printf("✓ SequencerUI destroyed\n");
}

bool sequencer_ui_initialize(SequencerUI* ui) {
    if (!ui) return false;

    // Initialize SDL video subsystem
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("❌ Failed to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    // Initialize TTF
    if (TTF_Init() < 0) {
        printf("❌ Failed to initialize TTF: %s\n", TTF_GetError());
        SDL_Quit();
        return false;
    }

    // Load pixel-perfect monospace font for GameBoy-style UI
    g_font = TTF_OpenFont("/System/Library/Fonts/Monaco.ttf", 8);
    if (!g_font) {
        // Try alternative monospace fonts
        g_font = TTF_OpenFont("/System/Library/Fonts/Courier.ttc", 8);
    }
    if (!g_font) {
        g_font = TTF_OpenFont("/System/Library/Fonts/Arial.ttf", 8);
    }
    if (!g_font) {
        printf("⚠️ Warning: Could not load font, text rendering will be limited\n");
    }

    ui->is_initialized = true;
    printf("✓ SequencerUI initialized with SDL\n");
    return true;
}

void sequencer_ui_shutdown(SequencerUI* ui) {
    if (!ui || !ui->is_initialized) return;

    // Clean up SDL resources
    if (g_font) {
        TTF_CloseFont(g_font);
        g_font = NULL;
    }

    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }

    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }

    TTF_Quit();
    SDL_Quit();

    ui->is_initialized = false;
    printf("✓ SequencerUI shut down\n");
}

bool sequencer_ui_open(SequencerUI* ui) {
    if (!ui || !ui->is_initialized) return false;

    // Create SDL window
    g_window = SDL_CreateWindow(
        "Verse Sequencer UI",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        ui->window_width,
        ui->window_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!g_window) {
        printf("❌ Failed to create window: %s\n", SDL_GetError());
        return false;
    }

    // Create SDL renderer
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        printf("❌ Failed to create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        return false;
    }

    ui->is_open = true;
    printf("✓ SequencerUI window opened (%dx%d)\n", ui->window_width, ui->window_height);
    return true;
}

void sequencer_ui_close(SequencerUI* ui) {
    if (!ui || !ui->is_open) return;

    // Clean up SDL resources
    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }

    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }

    ui->is_open = false;
    printf("✓ SequencerUI window closed\n");
}

bool sequencer_ui_is_open(SequencerUI* ui) {
    return ui && ui->is_open;
}

void sequencer_ui_update(SequencerUI* ui, double delta_time) {
    if (!ui || !ui->is_open) return;

    // In a real implementation, this would:
    // - Update timeline position
    // - Handle audio playback
    // - Update UI animations
    // - Process input events

    ui->frame_count++;

    // Simulate timeline update
    if (ui->is_playing && ui->songwriter) {
        ui->current_tick += (uint32_t)(delta_time * 1000.0); // Rough tick calculation
        if (ui->current_tick >= ui->loop_end_tick && ui->loop_enabled) {
            ui->current_tick = ui->loop_start_tick;
        }
    }
}

void sequencer_ui_render(SequencerUI* ui) {
    if (!ui || !ui->is_open || !g_renderer) return;

    // Clear with black background (GameBoy style)
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);

    // Render stepper interface
    render_stepper_header(ui);
    render_stepper_grid(ui);
    render_stepper_status(ui);

    // Present the frame
    SDL_RenderPresent(g_renderer);
}

// Render stepper header (like STEPPER GBA)
void render_stepper_header(SequencerUI* ui) {
    if (!g_renderer) return;

    // Header background (white)
    SDL_Rect header_rect = {0, 0, ui->window_width, 24};
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(g_renderer, &header_rect);

    // Header border (black)
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &header_rect);

    if (g_font) {
        SDL_Color black_color = {0, 0, 0, 255};

        // Pattern info
        char pattern_text[16];
        snprintf(pattern_text, sizeof(pattern_text), "PAT:%02d", ui->current_pattern);

        SDL_Surface* pattern_surface = TTF_RenderText_Solid(g_font, pattern_text, black_color);
        if (pattern_surface) {
            SDL_Texture* pattern_texture = SDL_CreateTextureFromSurface(g_renderer, pattern_surface);
            if (pattern_texture) {
                SDL_Rect pattern_rect = {4, 2, pattern_surface->w, pattern_surface->h};
                SDL_RenderCopy(g_renderer, pattern_texture, NULL, &pattern_rect);
                SDL_DestroyTexture(pattern_texture);
            }
            SDL_FreeSurface(pattern_surface);
        }

        // BPM
        if (ui->songwriter && ui->songwriter->current_song) {
            char bpm_text[16];
            snprintf(bpm_text, sizeof(bpm_text), "BPM:%03d", ui->songwriter->current_song->tempo);

            SDL_Surface* bpm_surface = TTF_RenderText_Solid(g_font, bpm_text, black_color);
            if (bpm_surface) {
                SDL_Texture* bpm_texture = SDL_CreateTextureFromSurface(g_renderer, bpm_surface);
                if (bpm_texture) {
                    SDL_Rect bpm_rect = {ui->window_width - bpm_surface->w - 4, 2, bpm_surface->w, bpm_surface->h};
                    SDL_RenderCopy(g_renderer, bpm_texture, NULL, &bpm_rect);
                    SDL_DestroyTexture(bpm_texture);
                }
                SDL_FreeSurface(bpm_surface);
            }
        }

        // Play status
        char status_text[8];
        snprintf(status_text, sizeof(status_text), "%s", ui->is_playing ? "PLAY" : "STOP");

        SDL_Surface* status_surface = TTF_RenderText_Solid(g_font, status_text, black_color);
        if (status_surface) {
            SDL_Texture* status_texture = SDL_CreateTextureFromSurface(g_renderer, status_surface);
            if (status_texture) {
                SDL_Rect status_rect = {ui->window_width/2 - status_surface->w/2, 2, status_surface->w, status_surface->h};
                SDL_RenderCopy(g_renderer, status_texture, NULL, &status_rect);
                SDL_DestroyTexture(status_texture);
            }
            SDL_FreeSurface(status_surface);
        }
    }
}

// Render advanced stepper interface
void render_stepper_grid(SequencerUI* ui) {
    if (!g_renderer) return;

    int start_y = 24; // Below header
    int grid_height = ui->window_height - 24 - 16; // Above status bar

    // Grid background (black)
    SDL_Rect grid_rect = {0, start_y, ui->window_width, grid_height};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(g_renderer, &grid_rect);

    // Layout: Left panel (patterns), top hint bar, parameter rows, center (piano + steps), right panel (controls)
    int left_panel_width = 64;
    int right_panel_width = 128;
    int center_width = ui->window_width - left_panel_width - right_panel_width;

    // Top hint bar (matches image: "L/R: NOTE  SEL+L/R: OCTAVE  A: PARAMS  B: TOGGLE")
    SDL_Rect hint_rect = {left_panel_width + 8, start_y + 4, center_width - 16, 20};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &hint_rect);
    if (g_font) {
        SDL_Color white = {255, 255, 255, 255};
        const char* hint = "L/R: NOTE  SEL+L/R: OCTAVE  A: PARAMS  B: TOGGLE";
        SDL_Surface* s = TTF_RenderText_Solid(g_font, hint, white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(g_renderer, s);
            if (t) {
                SDL_Rect r = {hint_rect.x + 8, hint_rect.y + 2, s->w, s->h};
                SDL_RenderCopy(g_renderer, t, NULL, &r);
                SDL_DestroyTexture(t);
            }
            SDL_FreeSurface(s);
        }
    }

    // Draw left panel - Pattern selection (A-H)
    render_pattern_selector(ui, start_y, left_panel_width, grid_height);

    // Parameter panels under hint bar (wave/envelope, probability, voice/vol, envelope box)
    int params_y = start_y + 28;
    int params_h = 88;
    render_param_panels(ui, left_panel_width, params_y, center_width, params_h);

    // Draw center area - Piano roll and step sequencer
    int content_y = params_y + params_h + 8;
    int content_h = start_y + grid_height - content_y - 8;
    render_piano_and_steps(ui, left_panel_width, content_y, center_width, content_h);

    // Draw right panel - Controls
    render_control_panel(ui, left_panel_width + center_width, right_panel_width, grid_height);
}

// Render pattern selector (A-H buttons with waveforms)
void render_pattern_selector(SequencerUI* ui, int start_y, int width, int height) {
    if (!g_renderer) return;

    int button_size = 24;
    int button_spacing = 4;
    int start_x = 4;

    // PAT label
    if (g_font) {
        SDL_Color white = {255,255,255,255};
        SDL_Surface* s = TTF_RenderText_Solid(g_font, "PAT", white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(g_renderer, s);
            if (t) {
                SDL_Rect r = {start_x, start_y - 16, s->w, s->h};
                SDL_RenderCopy(g_renderer, t, NULL, &r);
                SDL_DestroyTexture(t);
            }
            SDL_FreeSurface(s);
        }
    }

    // Draw pattern buttons A-H
    for (int i = 0; i < 8; i++) {
        int x = start_x;
        int y = start_y + i * (button_size + button_spacing) + 4;

        SDL_Rect button_rect = {x, y, button_size, button_size};

        // Pattern button (A-H)
        if (i == ui->current_pattern) {
            // Selected pattern (white)
            SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
            SDL_RenderFillRect(g_renderer, &button_rect);
            SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        } else {
            // Unselected pattern (black with white border)
            SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(g_renderer, &button_rect);
            SDL_SetRenderDrawColor(g_renderer, 128, 128, 128, 255);
        }
        // White border per style guide
        SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(g_renderer, &button_rect);

        // Draw pattern letter (A-H)
        if (g_font) {
            SDL_Color text_color = (i == ui->current_pattern) ? (SDL_Color){0, 0, 0, 255} : (SDL_Color){255, 255, 255, 255};
            char pattern_letter = 'A' + i;
            char pattern_text[2] = {pattern_letter, '\0'};

            SDL_Surface* text_surface = TTF_RenderText_Solid(g_font, pattern_text, text_color);
            if (text_surface) {
                SDL_Texture* text_texture = SDL_CreateTextureFromSurface(g_renderer, text_surface);
                if (text_texture) {
                    SDL_Rect text_rect = {x + 6, y + 4, text_surface->w, text_surface->h};
                    SDL_RenderCopy(g_renderer, text_texture, NULL, &text_rect);
                    SDL_DestroyTexture(text_texture);
                }
                SDL_FreeSurface(text_surface);
            }
        }

        // Draw waveform icon below button
        SDL_Rect wave_rect = {x + 2, y + button_size + 2, button_size - 4, 8};
        render_waveform_icon(ui, i, wave_rect);
    }
}

// Render waveform icon
void render_waveform_icon(SequencerUI* ui, int pattern, SDL_Rect rect) {
    if (!g_renderer) return;

    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);

    // Simple waveform representations
    switch (pattern % 4) {
        case 0: // Sine wave
            for (int x = 0; x < rect.w; x++) {
                int y = rect.y + rect.h/2 + (int)(sin(x * 0.5) * rect.h/4);
                SDL_RenderDrawPoint(g_renderer, rect.x + x, y);
            }
            break;
        case 1: // Square wave
            SDL_RenderDrawLine(g_renderer, rect.x, rect.y, rect.x + rect.w/2, rect.y);
            SDL_RenderDrawLine(g_renderer, rect.x + rect.w/2, rect.y, rect.x + rect.w/2, rect.y + rect.h);
            SDL_RenderDrawLine(g_renderer, rect.x + rect.w/2, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h);
            break;
        case 2: // Saw wave
            SDL_RenderDrawLine(g_renderer, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y);
            break;
        case 3: // Triangle wave
            SDL_RenderDrawLine(g_renderer, rect.x, rect.y + rect.h/2, rect.x + rect.w/2, rect.y);
            SDL_RenderDrawLine(g_renderer, rect.x + rect.w/2, rect.y, rect.x + rect.w, rect.y + rect.h/2);
            break;
    }
}

// Render parameter panels matching reference image
void render_param_panels(SequencerUI* ui, int start_x, int start_y, int width, int height) {
    if (!g_renderer) return;

    // Split into columns: [Wave/Type graph] [Envelope graph] [Probability %]
    int col_w = width / 3;

    // Panel 1: Wave SHAPE/TYPE with tiny graph and labels
    SDL_Rect p1 = {start_x + 8, start_y + 4, col_w - 16, height - 8};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &p1);
    if (g_font) {
        SDL_Color white = {255,255,255,255};
        SDL_Surface* s1 = TTF_RenderText_Solid(g_font, "SHAPE", white);
        if (s1) {
            SDL_Texture* t1 = SDL_CreateTextureFromSurface(g_renderer, s1);
            SDL_Rect r1 = {p1.x + 4, p1.y + 2, s1->w, s1->h};
            SDL_RenderCopy(g_renderer, t1, NULL, &r1);
            SDL_DestroyTexture(t1); SDL_FreeSurface(s1);
        }
        SDL_Surface* s2 = TTF_RenderText_Solid(g_font, "TYPE", white);
        if (s2) {
            SDL_Texture* t2 = SDL_CreateTextureFromSurface(g_renderer, s2);
            SDL_Rect r2 = {p1.x + p1.w/2, p1.y + 2, s2->w, s2->h};
            SDL_RenderCopy(g_renderer, t2, NULL, &r2);
            SDL_DestroyTexture(t2); SDL_FreeSurface(s2);
        }
    }
    // small sine preview
    SDL_SetRenderDrawColor(g_renderer, 255, 64, 128, 255);
    for (int x = 0; x < p1.w-8; x++) {
        int y = p1.y + 24 + (int)(sin(x * 0.2) * 8.0);
        SDL_RenderDrawPoint(g_renderer, p1.x + 4 + x, y);
    }

    // Panel 2: Envelope graph (ATTACK/DECAY box)
    SDL_Rect p2 = {start_x + col_w + 8, start_y + 4, col_w - 16, height - 8};
    SDL_SetRenderDrawColor(g_renderer, 0, 255, 255, 255); // cyan border per image
    SDL_RenderDrawRect(g_renderer, &p2);
    if (g_font) {
        SDL_Color white = {255,255,255,255};
        SDL_Surface* sa = TTF_RenderText_Solid(g_font, "ATTACK", white);
        SDL_Surface* sd = TTF_RenderText_Solid(g_font, "DECAY", white);
        if (sa) {
            SDL_Texture* ta = SDL_CreateTextureFromSurface(g_renderer, sa);
            SDL_Rect ra = {p2.x, p2.y + p2.h + 2, sa->w, sa->h};
            SDL_RenderCopy(g_renderer, ta, NULL, &ra);
            SDL_DestroyTexture(ta); SDL_FreeSurface(sa);
        }
        if (sd) {
            SDL_Texture* td = SDL_CreateTextureFromSurface(g_renderer, sd);
            SDL_Rect rd = {p2.x + p2.w - sd->w, p2.y + p2.h + 2, sd->w, sd->h};
            SDL_RenderCopy(g_renderer, td, NULL, &rd);
            SDL_DestroyTexture(td); SDL_FreeSurface(sd);
        }
    }

    // Panel 3: Probability + Voice/Vol + MID/PAN labels (simplified)
    SDL_Rect p3 = {start_x + col_w*2 + 8, start_y + 4, col_w - 16, height - 8};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &p3);
    if (g_font) {
        SDL_Color white = {255,255,255,255};
        SDL_Surface* sp = TTF_RenderText_Solid(g_font, "100%", white);
        SDL_Surface* sv = TTF_RenderText_Solid(g_font, "VOL", white);
        SDL_Surface* svx = TTF_RenderText_Solid(g_font, "75%", white);
        if (sp) { SDL_Texture* tp = SDL_CreateTextureFromSurface(g_renderer, sp);
            SDL_Rect rp = {p3.x + p3.w - sp->w - 4, p3.y + 2, sp->w, sp->h};
            SDL_RenderCopy(g_renderer, tp, NULL, &rp); SDL_DestroyTexture(tp); SDL_FreeSurface(sp); }
        int rv_x = 0, rv_y = 0;
        if (sv) { SDL_Texture* tv = SDL_CreateTextureFromSurface(g_renderer, sv);
            SDL_Rect rv = {p3.x + 4, p3.y + p3.h/2 - sv->h/2, sv->w, sv->h};
            rv_x = rv.x; rv_y = rv.y;
            SDL_RenderCopy(g_renderer, tv, NULL, &rv); SDL_DestroyTexture(tv); SDL_FreeSurface(sv); }
        if (svx) { SDL_Texture* tvx = SDL_CreateTextureFromSurface(g_renderer, svx);
            SDL_Rect rvx = {rv_x + 36, rv_y, svx->w, svx->h};
            SDL_RenderCopy(g_renderer, tvx, NULL, &rvx); SDL_DestroyTexture(tvx); SDL_FreeSurface(svx); }
    }
}

// Render piano roll and step sequencer
void render_piano_and_steps(SequencerUI* ui, int start_x, int start_y, int width, int height) {
    if (!g_renderer) return;

    int piano_height = 60;
    int steps_height = height - piano_height;

    // Draw piano roll
    render_piano_roll(ui, start_x, start_y, width, piano_height);

    // Draw step sequencer
    render_step_sequencer(ui, start_x, start_y + piano_height, width, steps_height);
}

// Render piano roll
void render_piano_roll(SequencerUI* ui, int start_x, int start_y, int width, int height) {
    if (!g_renderer) return;

    // Piano roll background
    SDL_Rect piano_rect = {start_x, start_y, width, height};
    SDL_SetRenderDrawColor(g_renderer, 32, 32, 32, 255);
    SDL_RenderFillRect(g_renderer, &piano_rect);

    // Draw piano keys (simplified)
    int key_width = width / 25; // 25 white keys
    for (int i = 0; i < 25; i++) {
        int x = start_x + i * key_width;
        SDL_Rect key_rect = {x, start_y, key_width, height};

        // White keys
        SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(g_renderer, &key_rect);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(g_renderer, &key_rect);
    }
}

// Render step sequencer (8 steps per pattern)
void render_step_sequencer(SequencerUI* ui, int start_x, int start_y, int width, int height) {
    if (!g_renderer) return;

    int step_width = width / 8;
    int pattern_height = height / 8; // 8 patterns (A-H)

    // Draw step grid
    SDL_SetRenderDrawColor(g_renderer, 64, 64, 64, 255);

    // Vertical lines (steps)
    for (int i = 0; i <= 8; i++) {
        int x = start_x + i * step_width;
        SDL_RenderDrawLine(g_renderer, x, start_y, x, start_y + height);
    }

    // Horizontal lines (patterns)
    for (int i = 0; i <= 8; i++) {
        int y = start_y + i * pattern_height;
        SDL_RenderDrawLine(g_renderer, start_x, y, start_x + width, y);
    }

    // Draw step buttons with note names
    for (int pattern = 0; pattern < 8; pattern++) {
        uint16_t mask = ui->pattern_steps[pattern];
        for (int step = 0; step < 16; step++) {
            int x = start_x + step * step_width + 2;
            int y = start_y + pattern * pattern_height + 2;
            int w = step_width - 4;
            int h = pattern_height - 4;

            SDL_Rect step_rect = {x, y, w, h};

            // Check if step is active
            bool is_active = (mask >> step) & 1u;

            if (is_active) {
                // Active step (white)
                SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
                SDL_RenderFillRect(g_renderer, &step_rect);
                SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
            } else {
                // Inactive step (black with white border)
                SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
                SDL_RenderFillRect(g_renderer, &step_rect);
                SDL_SetRenderDrawColor(g_renderer, 128, 128, 128, 255);
            }
            SDL_RenderDrawRect(g_renderer, &step_rect);

            // Draw note name (CS, B6S, etc.)
            if (g_font && is_active) {
                SDL_Color text_color = {0, 0, 0, 255};
                const char* note_names[] = {"CS", "B6S", "CS", "B6S", "CS", "B6S", "CS", "B6S"};
                const char* note_name = note_names[step];

                SDL_Surface* text_surface = TTF_RenderText_Solid(g_font, note_name, text_color);
                if (text_surface) {
                    SDL_Texture* text_texture = SDL_CreateTextureFromSurface(g_renderer, text_surface);
                    if (text_texture) {
                        SDL_Rect text_rect = {x + 2, y + 2, text_surface->w, text_surface->h};
                        SDL_RenderCopy(g_renderer, text_texture, NULL, &text_rect);
                        SDL_DestroyTexture(text_texture);
                    }
                    SDL_FreeSurface(text_surface);
                }
            }
        }
    }

    // Highlight current step
    int current_step = (ui->current_tick / 48) % 16;
    if (current_step >= 0 && current_step < 16) {
        SDL_Rect current_step_rect = {start_x + current_step * step_width, start_y, step_width, height};
        SDL_SetRenderDrawColor(g_renderer, 255, 255, 0, 64); // Yellow highlight
        SDL_RenderFillRect(g_renderer, &current_step_rect);
    }
}

// Render control panel (right side)
void render_control_panel(SequencerUI* ui, int start_x, int width, int height) {
    if (!g_renderer) return;

    int start_y = 24;
    int section_height = height / 4;

    // BANK section
    render_bank_section(ui, start_x, start_y, width, section_height);

    // SCALE section
    render_scale_section(ui, start_x, start_y + section_height, width, section_height);

    // BPM section
    render_bpm_section(ui, start_x, start_y + section_height * 2, width, section_height);

    // Transport controls
    render_transport_controls(ui, start_x, start_y + section_height * 3, width, section_height);
}

// Render BANK section
void render_bank_section(SequencerUI* ui, int start_x, int start_y, int width, int height) {
    if (!g_renderer) return;

    // Section background
    SDL_Rect section_rect = {start_x, start_y, width, height};
    SDL_SetRenderDrawColor(g_renderer, 32, 32, 32, 255);
    SDL_RenderFillRect(g_renderer, &section_rect);

    // Draw BANK label
    if (g_font) {
        SDL_Color white_color = {255, 255, 255, 255};
        SDL_Surface* label_surface = TTF_RenderText_Solid(g_font, "BANK", white_color);
        if (label_surface) {
            SDL_Texture* label_texture = SDL_CreateTextureFromSurface(g_renderer, label_surface);
            if (label_texture) {
                SDL_Rect label_rect = {start_x + 4, start_y + 4, label_surface->w, label_surface->h};
                SDL_RenderCopy(g_renderer, label_texture, NULL, &label_rect);
                SDL_DestroyTexture(label_texture);
            }
            SDL_FreeSurface(label_surface);
        }
    }

    // Draw bank buttons A-F
    int button_size = 16;
    int button_spacing = 2;
    for (int i = 0; i < 6; i++) {
        int x = start_x + 4 + (i % 3) * (button_size + button_spacing);
        int y = start_y + 20 + (i / 3) * (button_size + button_spacing);

        SDL_Rect button_rect = {x, y, button_size, button_size};

        // Bank button
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(g_renderer, &button_rect);
        SDL_SetRenderDrawColor(g_renderer, 128, 128, 128, 255);
        SDL_RenderDrawRect(g_renderer, &button_rect);

        // Bank letter
        if (g_font) {
            SDL_Color white_color = {255, 255, 255, 255};
            char bank_letter = 'A' + i;
            char bank_text[2] = {bank_letter, '\0'};

            SDL_Surface* text_surface = TTF_RenderText_Solid(g_font, bank_text, white_color);
            if (text_surface) {
                SDL_Texture* text_texture = SDL_CreateTextureFromSurface(g_renderer, text_surface);
                if (text_texture) {
                    SDL_Rect text_rect = {x + 4, y + 2, text_surface->w, text_surface->h};
                    SDL_RenderCopy(g_renderer, text_texture, NULL, &text_rect);
                    SDL_DestroyTexture(text_texture);
                }
                SDL_FreeSurface(text_surface);
            }
        }
    }
}

// Render SCALE section
void render_scale_section(SequencerUI* ui, int start_x, int start_y, int width, int height) {
    if (!g_renderer) return;

    // Section background
    SDL_Rect section_rect = {start_x, start_y, width, height};
    SDL_SetRenderDrawColor(g_renderer, 32, 32, 32, 255);
    SDL_RenderFillRect(g_renderer, &section_rect);

    // Draw SCALE label
    if (g_font) {
        SDL_Color white_color = {255, 255, 255, 255};
        SDL_Surface* label_surface = TTF_RenderText_Solid(g_font, "SCALE", white_color);
        if (label_surface) {
            SDL_Texture* label_texture = SDL_CreateTextureFromSurface(g_renderer, label_surface);
            if (label_texture) {
                SDL_Rect label_rect = {start_x + 4, start_y + 4, label_surface->w, label_surface->h};
                SDL_RenderCopy(g_renderer, label_texture, NULL, &label_rect);
                SDL_DestroyTexture(label_texture);
            }
            SDL_FreeSurface(label_surface);
        }
    }

    // Draw CHRM button
    SDL_Rect chrm_rect = {start_x + 4, start_y + 20, width - 8, 16};
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(g_renderer, &chrm_rect);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &chrm_rect);

    if (g_font) {
        SDL_Color black_color = {0, 0, 0, 255};
        SDL_Surface* text_surface = TTF_RenderText_Solid(g_font, "CHRM", black_color);
        if (text_surface) {
            SDL_Texture* text_texture = SDL_CreateTextureFromSurface(g_renderer, text_surface);
            if (text_texture) {
                SDL_Rect text_rect = {start_x + 8, start_y + 22, text_surface->w, text_surface->h};
                SDL_RenderCopy(g_renderer, text_texture, NULL, &text_rect);
                SDL_DestroyTexture(text_texture);
            }
            SDL_FreeSurface(text_surface);
        }
    }
}

// Render BPM section
void render_bpm_section(SequencerUI* ui, int start_x, int start_y, int width, int height) {
    if (!g_renderer) return;

    // Section background
    SDL_Rect section_rect = {start_x, start_y, width, height};
    SDL_SetRenderDrawColor(g_renderer, 32, 32, 32, 255);
    SDL_RenderFillRect(g_renderer, &section_rect);

    // Draw BPM label
    if (g_font) {
        SDL_Color white_color = {255, 255, 255, 255};
        SDL_Surface* label_surface = TTF_RenderText_Solid(g_font, "BPM", white_color);
        if (label_surface) {
            SDL_Texture* label_texture = SDL_CreateTextureFromSurface(g_renderer, label_surface);
            if (label_texture) {
                SDL_Rect label_rect = {start_x + 4, start_y + 4, label_surface->w, label_surface->h};
                SDL_RenderCopy(g_renderer, label_texture, NULL, &label_rect);
                SDL_DestroyTexture(label_texture);
            }
            SDL_FreeSurface(label_surface);
        }
    }

    // Draw BPM value
    SDL_Rect bpm_rect = {start_x + 4, start_y + 20, width - 8, 16};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(g_renderer, &bpm_rect);
    SDL_SetRenderDrawColor(g_renderer, 128, 128, 128, 255);
    SDL_RenderDrawRect(g_renderer, &bpm_rect);

    if (g_font) {
        SDL_Color white_color = {255, 255, 255, 255};
        char bpm_text[8];
        snprintf(bpm_text, sizeof(bpm_text), "%d", ui->songwriter && ui->songwriter->current_song ? ui->songwriter->current_song->tempo : 120);

        SDL_Surface* text_surface = TTF_RenderText_Solid(g_font, bpm_text, white_color);
        if (text_surface) {
            SDL_Texture* text_texture = SDL_CreateTextureFromSurface(g_renderer, text_surface);
            if (text_texture) {
                SDL_Rect text_rect = {start_x + 8, start_y + 22, text_surface->w, text_surface->h};
                SDL_RenderCopy(g_renderer, text_texture, NULL, &text_rect);
                SDL_DestroyTexture(text_texture);
            }
            SDL_FreeSurface(text_surface);
        }
    }
}

// Render transport controls
void render_transport_controls(SequencerUI* ui, int start_x, int start_y, int width, int height) {
    if (!g_renderer) return;

    // Section background
    SDL_Rect section_rect = {start_x, start_y, width, height};
    SDL_SetRenderDrawColor(g_renderer, 32, 32, 32, 255);
    SDL_RenderFillRect(g_renderer, &section_rect);

    int button_size = 20;
    int button_spacing = 4;
    int start_button_x = start_x + 4;
    int start_button_y = start_y + 4;

    // Loop button (double arrow)
    SDL_Rect loop_rect = {start_button_x, start_button_y, button_size, button_size};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(g_renderer, &loop_rect);
    SDL_SetRenderDrawColor(g_renderer, 128, 128, 128, 255);
    SDL_RenderDrawRect(g_renderer, &loop_rect);

    // Play button (triangle)
    SDL_Rect play_rect = {start_button_x + button_size + button_spacing, start_button_y, button_size, button_size};
    SDL_SetRenderDrawColor(g_renderer, 0, 200, 255, 255); // Light blue
    SDL_RenderFillRect(g_renderer, &play_rect);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &play_rect);

    // Stop button (square)
    SDL_Rect stop_rect = {start_button_x + (button_size + button_spacing) * 2, start_button_y, button_size, button_size};
    SDL_SetRenderDrawColor(g_renderer, 255, 0, 0, 255); // Red
    SDL_RenderFillRect(g_renderer, &stop_rect);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &stop_rect);
}

// Render stepper status bar (bottom)
void render_stepper_status(SequencerUI* ui) {
    if (!g_renderer) return;

    int status_y = ui->window_height - 16;

    // Status bar background (white)
    SDL_Rect status_rect = {0, status_y, ui->window_width, 16};
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(g_renderer, &status_rect);

    // Status bar border (black)
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(g_renderer, &status_rect);

    if (g_font) {
        SDL_Color black_color = {0, 0, 0, 255};

        // Step position
        char step_text[16];
        snprintf(step_text, sizeof(step_text), "STEP:%02d", (ui->current_tick / 48) % 16 + 1);

        SDL_Surface* step_surface = TTF_RenderText_Solid(g_font, step_text, black_color);
        if (step_surface) {
            SDL_Texture* step_texture = SDL_CreateTextureFromSurface(g_renderer, step_surface);
            if (step_texture) {
                SDL_Rect step_rect = {4, status_y + 2, step_surface->w, step_surface->h};
                SDL_RenderCopy(g_renderer, step_texture, NULL, &step_rect);
                SDL_DestroyTexture(step_texture);
            }
            SDL_FreeSurface(step_surface);
        }

        // Loop indicator
        if (ui->loop_enabled) {
            SDL_Surface* loop_surface = TTF_RenderText_Solid(g_font, "LOOP", black_color);
            if (loop_surface) {
                SDL_Texture* loop_texture = SDL_CreateTextureFromSurface(g_renderer, loop_surface);
                if (loop_texture) {
                    SDL_Rect loop_rect = {ui->window_width - loop_surface->w - 4, status_y + 2, loop_surface->w, loop_surface->h};
                    SDL_RenderCopy(g_renderer, loop_texture, NULL, &loop_rect);
                    SDL_DestroyTexture(loop_texture);
                }
                SDL_FreeSurface(loop_surface);
            }
        }
    }
}

void sequencer_ui_handle_event(SequencerUI* ui, SDL_Event* event) {
    if (!ui || !event) return;

    switch (event->type) {
    case SDL_KEYDOWN:
        handle_keyboard_event(ui, &event->key);
        break;
    case SDL_MOUSEBUTTONDOWN:
        handle_mouse_click(ui, event->button.x, event->button.y, event->button.button);
        break;
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_CLOSE) {
            ui->is_open = false;
        }
        break;
    default:
        break;
    }
}

// Handle keyboard events
void handle_keyboard_event(SequencerUI* ui, SDL_KeyboardEvent* key_event) {
    switch (key_event->keysym.sym) {
    case SDLK_SPACE:
        if (ui->is_playing) {
            sequencer_ui_pause(ui);
        } else {
            sequencer_ui_play(ui);
        }
        break;
    case SDLK_s:
        sequencer_ui_stop(ui);
        break;
    case SDLK_r:
        sequencer_ui_record(ui);
        break;
    case SDLK_l:
        sequencer_ui_toggle_loop(ui);
        break;
    case SDLK_PLUS:
    case SDLK_EQUALS:
        sequencer_ui_zoom_horizontal(ui, 1.2f);
        break;
    case SDLK_MINUS:
        sequencer_ui_zoom_horizontal(ui, 0.8f);
        break;
    case SDLK_ESCAPE:
        ui->is_open = false;
        break;
    default:
        break;
    }
}

// Handle mouse clicks
void handle_mouse_click(SequencerUI* ui, int x, int y, int button) {
    if (button != SDL_BUTTON_LEFT) return;

    // Check header area (top 24 pixels)
    if (y < 24) {
        handle_stepper_header_click(ui, x, y);
        return;
    }

    // Check stepper grid (main area)
    if (y < ui->window_height - 16) {
        handle_stepper_grid_click(ui, x, y);
        return;
    }

    // Check status bar (bottom 16 pixels)
    handle_stepper_status_click(ui, x, y);
}

// Handle stepper header clicks
void handle_stepper_header_click(SequencerUI* ui, int x, int y) {
    // Click in header toggles play/pause
    if (ui->is_playing) {
        sequencer_ui_pause(ui);
    } else {
        sequencer_ui_play(ui);
    }
}

// Handle stepper grid clicks
void handle_stepper_grid_click(SequencerUI* ui, int x, int y) {
    int start_y = 24;
    int grid_height = ui->window_height - 24 - 16;

    // Calculate which step and track was clicked
    int left_panel_width = 64;
    int right_panel_width = 128;
    int center_width = ui->window_width - left_panel_width - right_panel_width;
    int params_y = start_y + 28;
    int params_h = 88;
    int content_y = params_y + params_h + 8;
    int content_h = start_y + grid_height - content_y - 8;
    
    // Step grid geometry
    int step_width = center_width / 16;
    int pattern_height = content_h / 8;

    // Adjust coordinates into center content area
    int local_x = x - left_panel_width;
    int local_y = y - content_y;
    if (local_x < 0 || local_y < 0) return;
    int step = local_x / step_width;
    int pattern = local_y / pattern_height;

    if (step >= 0 && step < 16 && pattern >= 0 && pattern < 8) {
        printf("🎵 Step click: pattern=%d, step=%d\n", pattern, step);
        // Toggle step bit
        ui->pattern_steps[pattern] ^= (1u << step);
    }
}

// Handle stepper status clicks
void handle_stepper_status_click(SequencerUI* ui, int x, int y) {
    // Click in status bar toggles loop
    sequencer_ui_toggle_loop(ui);
}

// Playback control stubs
void sequencer_ui_play(SequencerUI* ui) {
    if (!ui) return;
    ui->is_playing = true;
    ui->state = SEQUENCER_UI_STATE_PLAYING;
    printf("▶️ Sequencer playing\n");
}

void sequencer_ui_pause(SequencerUI* ui) {
    if (!ui) return;
    ui->is_playing = false;
    ui->state = SEQUENCER_UI_STATE_PAUSED;
    printf("⏸️ Sequencer paused\n");
}

void sequencer_ui_stop(SequencerUI* ui) {
    if (!ui) return;
    ui->is_playing = false;
    ui->state = SEQUENCER_UI_STATE_IDLE;
    ui->current_tick = 0;
    printf("⏹️ Sequencer stopped\n");
}

void sequencer_ui_record(SequencerUI* ui) {
    if (!ui) return;
    ui->is_recording = !ui->is_recording;
    ui->state = ui->is_recording ? SEQUENCER_UI_STATE_RECORDING : SEQUENCER_UI_STATE_PLAYING;
    printf("⏺️ Sequencer recording: %s\n", ui->is_recording ? "ON" : "OFF");
}

void sequencer_ui_toggle_loop(SequencerUI* ui) {
    if (!ui) return;
    ui->loop_enabled = !ui->loop_enabled;
    printf("🔄 Sequencer loop: %s\n", ui->loop_enabled ? "ON" : "OFF");
}

// Zoom control stubs
void sequencer_ui_zoom_horizontal(SequencerUI* ui, float factor) {
    if (!ui) return;
    ui->horizontal_zoom *= factor;
    ui->pixels_per_tick *= factor;
    printf("🔍 Horizontal zoom: %.2fx\n", ui->horizontal_zoom);
}

// State query stubs
bool sequencer_ui_is_playing(SequencerUI* ui) {
    return ui && ui->is_playing;
}

bool sequencer_ui_is_recording(SequencerUI* ui) {
    return ui && ui->is_recording;
}

uint32_t sequencer_ui_get_current_tick(SequencerUI* ui) {
    return ui ? ui->current_tick : 0;
}

// Stub implementations for other functions
bool sequencer_ui_load_song(SequencerUI* ui, Song* song) {
    if (!ui || !song) return false;
    printf("📁 Loading song: %s\n", song->name);
    return true;
}

bool sequencer_ui_new_song(SequencerUI* ui) {
    if (!ui) return false;
    printf("📄 Creating new song\n");
    return true;
}

bool sequencer_ui_save_song(SequencerUI* ui, const char* filename) {
    if (!ui || !filename) return false;
    printf("💾 Saving song to: %s\n", filename);
    return true;
}

// All other functions are stubs that return success or do nothing
bool sequencer_ui_add_track(SequencerUI* ui, const char* name, uint8_t channel) {
    if (!ui || !name) return false;
    printf("➕ Adding track: %s (channel %d)\n", name, channel);
    return true;
}

bool sequencer_ui_remove_track(SequencerUI* ui, int track_index) {
    if (!ui) return false;
    printf("➖ Removing track: %d\n", track_index);
    return true;
}

bool sequencer_ui_rename_track(SequencerUI* ui, int track_index, const char* name) {
    if (!ui || !name) return false;
    printf("✏️ Renaming track %d to: %s\n", track_index, name);
    return true;
}

void sequencer_ui_select_track(SequencerUI* ui, int track_index) {
    if (!ui) return;
    ui->selected_track = track_index;
    printf("🎯 Selected track: %d\n", track_index);
}

// Note editing stubs
bool sequencer_ui_add_note(SequencerUI* ui, int track_index, uint8_t note,
                          uint32_t start_tick, uint32_t duration, uint8_t velocity) {
    if (!ui) return false;
    printf("🎵 Adding note: track=%d, note=%d, tick=%u, duration=%u, velocity=%d\n",
           track_index, note, start_tick, duration, velocity);
    return true;
}

bool sequencer_ui_remove_note(SequencerUI* ui, int track_index, uint32_t note_index) {
    if (!ui) return false;
    printf("🗑️ Removing note: track=%d, index=%u\n", track_index, note_index);
    return true;
}

bool sequencer_ui_modify_note(SequencerUI* ui, int track_index, uint32_t note_index,
                             uint8_t note, uint32_t start_tick, uint32_t duration, uint8_t velocity) {
    if (!ui) return false;
    printf("✏️ Modifying note: track=%d, index=%u, note=%d, tick=%u, duration=%u, velocity=%d\n",
           track_index, note_index, note, start_tick, duration, velocity);
    return true;
}

void sequencer_ui_select_note(SequencerUI* ui, int track_index, uint32_t note_index) {
    if (!ui) return;
    printf("🎯 Selected note: track=%d, index=%u\n", track_index, note_index);
}

void sequencer_ui_clear_selection(SequencerUI* ui) {
    if (!ui) return;
    ui->selected_note_count = 0;
    printf("🧹 Cleared selection\n");
}

// All other functions are minimal stubs
void sequencer_ui_seek(SequencerUI* ui, uint32_t tick) {
    if (ui) ui->current_tick = tick;
}

void sequencer_ui_set_loop(SequencerUI* ui, uint32_t start_tick, uint32_t end_tick) {
    if (!ui) return;
    ui->loop_start_tick = start_tick;
    ui->loop_end_tick = end_tick;
}

void sequencer_ui_zoom_vertical(SequencerUI* ui, float factor) {
    if (!ui) return;
    ui->vertical_zoom *= factor;
    ui->pixels_per_note *= factor;
}

void sequencer_ui_scroll_to_tick(SequencerUI* ui, uint32_t tick) {
    if (!ui) return;
    ui->visible_start_tick = tick;
}

void sequencer_ui_scroll_to_note(SequencerUI* ui, uint8_t note) {
    if (!ui) return;
    ui->visible_notes_start = note;
}

void sequencer_ui_fit_to_content(SequencerUI* ui) {
    if (!ui) return;
    printf("📐 Fitting to content\n");
}

SequencerUIState sequencer_ui_get_state(SequencerUI* ui) {
    return ui ? ui->state : SEQUENCER_UI_STATE_IDLE;
}

int sequencer_ui_get_selected_track(SequencerUI* ui) {
    return ui ? ui->selected_track : -1;
}

int sequencer_ui_get_selected_note_count(SequencerUI* ui) {
    return ui ? ui->selected_note_count : 0;
}

void sequencer_ui_set_mouse_mode(SequencerUI* ui, SequencerMouseMode mode) {
    if (ui) ui->mouse_mode = mode;
}

SequencerMouseMode sequencer_ui_get_mouse_mode(SequencerUI* ui) {
    return ui ? ui->mouse_mode : SEQUENCER_MOUSE_MODE_SELECT;
}

bool sequencer_ui_handle_mouse_click(SequencerUI* ui, int x, int y, int button) {
    if (!ui) return false;
    printf("🖱️ Mouse click: (%d, %d) button %d\n", x, y, button);
    return true;
}

bool sequencer_ui_handle_mouse_drag(SequencerUI* ui, int x, int y) {
    if (!ui) return false;
    printf("🖱️ Mouse drag: (%d, %d)\n", x, y);
    return true;
}

bool sequencer_ui_handle_mouse_wheel(SequencerUI* ui, int x, int y, int delta) {
    if (!ui) return false;
    printf("🖱️ Mouse wheel: (%d, %d) delta %d\n", x, y, delta);
    return true;
}

void sequencer_ui_handle_keyboard(SequencerUI* ui, SDL_Keycode key, bool ctrl, bool shift, bool alt) {
    if (!ui) return;
    printf("⌨️ Keyboard: key=%d, ctrl=%d, shift=%d, alt=%d\n", key, ctrl, shift, alt);
}

void sequencer_ui_show_message(SequencerUI* ui, const char* message, int duration_ms) {
    if (!ui || !message) return;
    printf("💬 Message: %s (duration: %dms)\n", message, duration_ms);
}

void sequencer_ui_show_error(SequencerUI* ui, const char* error) {
    if (!ui || !error) return;
    printf("❌ Error: %s\n", error);
}

void sequencer_ui_show_confirm(SequencerUI* ui, const char* message,
                              void (*callback)(bool confirmed, void* user_data), void* user_data) {
    if (!ui || !message) return;
    printf("❓ Confirm: %s\n", message);
    if (callback) callback(true, user_data);
}

void sequencer_ui_set_theme(SequencerUI* ui, const char* theme_name) {
    if (!ui || !theme_name) return;
    printf("🎨 Setting theme: %s\n", theme_name);
}

void sequencer_ui_set_color_scheme(SequencerUI* ui, uint32_t background, uint32_t foreground, uint32_t accent) {
    if (!ui) return;
    printf("🎨 Setting color scheme: bg=0x%08X, fg=0x%08X, accent=0x%08X\n", background, foreground, accent);
}

void sequencer_ui_show_fps(SequencerUI* ui, bool show) {
    if (!ui) return;
    printf("📊 FPS display: %s\n", show ? "ON" : "OFF");
}

void sequencer_ui_show_debug_info(SequencerUI* ui, bool show) {
    if (!ui) return;
    printf("🐛 Debug info: %s\n", show ? "ON" : "OFF");
}

void sequencer_ui_print_stats(SequencerUI* ui) {
    if (!ui) return;
    printf("📊 SequencerUI Stats:\n");
    printf("  - Window: %dx%d\n", ui->window_width, ui->window_height);
    printf("  - State: %d\n", ui->state);
    printf("  - Current tick: %u\n", ui->current_tick);
    printf("  - Playing: %s\n", ui->is_playing ? "Yes" : "No");
    printf("  - Recording: %s\n", ui->is_recording ? "Yes" : "No");
    printf("  - Loop: %s (%u-%u)\n", ui->loop_enabled ? "ON" : "OFF", ui->loop_start_tick, ui->loop_end_tick);
    printf("  - Selected track: %d\n", ui->selected_track);
    printf("  - Zoom: %.2fx horizontal, %.2fx vertical\n", ui->horizontal_zoom, ui->vertical_zoom);
}
