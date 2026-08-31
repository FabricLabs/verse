#include "song_editor.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Access to window state
#ifdef SONG_EDITOR_STANDALONE
// Standalone window state
static SDL_Renderer* g_renderer = NULL;
void song_editor_set_renderer(SDL_Renderer* renderer) {
    g_renderer = renderer;
}
#define RENDERER g_renderer
#else
extern WindowState window_state;
#define RENDERER window_state.renderer
#endif

// Note names for piano keys
static const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

SongEditor* song_editor_create(Songwriter* writer) {
    SongEditor* editor = malloc(sizeof(SongEditor));
    if (!editor) return NULL;

    editor->writer = writer;
    editor->current_song = NULL;
    editor->current_track = NULL;

    // Initialize UI state
    editor->state = EDITOR_STATE_DRAW;
    editor->scroll_x = 0;
    editor->scroll_y = 0;
    editor->zoom_level = 1;
    editor->show_grid = true;
    editor->snap_to_grid = true;

    // Initialize selection
    editor->selection_capacity = 100;
    editor->selection_count = 0;
    editor->selections = malloc(sizeof(NoteSelection) * editor->selection_capacity);

    if (!editor->selections) {
        free(editor);
        return NULL;
    }

    // Initialize mouse state
    editor->mouse_x = 0;
    editor->mouse_y = 0;
    editor->mouse_down = false;
    editor->drag_start_x = 0;
    editor->drag_start_y = 0;

    // Initialize playback
    editor->is_playing = false;
    editor->playhead_position = 0;
    editor->playhead_x = 0;
    editor->loop_enabled = true; // Default to looping

    // Initialize UI rectangles
    editor->piano_roll_rect.x = 200;
    editor->piano_roll_rect.y = 100;
    editor->piano_roll_rect.w = PIANO_ROLL_WIDTH;
    editor->piano_roll_rect.h = PIANO_ROLL_HEIGHT;

    editor->toolbar_rect.x = 0;
    editor->toolbar_rect.y = 0;
    editor->toolbar_rect.w = 1200;
    editor->toolbar_rect.h = 50;

    editor->timeline_rect.x = 200;
    editor->timeline_rect.y = 50;
    editor->timeline_rect.w = PIANO_ROLL_WIDTH;
    editor->timeline_rect.h = 40;

    editor->track_list_rect.x = 0;
    editor->track_list_rect.y = 100;
    editor->track_list_rect.w = 200;
    editor->track_list_rect.h = PIANO_ROLL_HEIGHT;

    // Initialize instrument editor
    editor->show_instrument_editor = false;
    editor->selected_instrument_track = 0;
    editor->instrument_editor_rect.x = 300;
    editor->instrument_editor_rect.y = 200;
    editor->instrument_editor_rect.w = 600;
    editor->instrument_editor_rect.h = 400;

    return editor;
}

void song_editor_destroy(SongEditor* editor) {
    if (!editor) return;

    if (editor->selections) {
        free(editor->selections);
    }

    free(editor);
}

bool song_editor_new_song(SongEditor* editor, const char* name, const char* artist, uint32_t tempo) {
    if (!editor || !editor->writer) return false;

    Song* song = songwriter_create_song(name, artist, tempo);
    if (!song) return false;

    // Add multiple default tracks with different instruments and wave types
    Track* piano_track = songwriter_create_track(song, "Piano", 0);
    if (piano_track) {
        piano_track->program = 0; // Acoustic Grand Piano
        piano_track->wave_type = WAVE_SINE; // Smooth sine wave for piano
        piano_track->volume_float = 0.3f;
        songwriter_add_track(song, piano_track);
    }

    Track* bass_track = songwriter_create_track(song, "Bass", 1);
    if (bass_track) {
        bass_track->program = 32; // Acoustic Bass
        bass_track->wave_type = WAVE_TRIANGLE; // Triangle wave for bass
        bass_track->volume_float = 0.4f;
        bass_track->attack_time = 0.05f;
        bass_track->sustain_level = 0.8f;
        songwriter_add_track(song, bass_track);
    }

    Track* drums_track = songwriter_create_track(song, "Drums", 9);
    if (drums_track) {
        drums_track->program = 0; // Standard drum kit
        drums_track->wave_type = WAVE_NOISE; // Noise for drums
        drums_track->volume_float = 0.2f;
        drums_track->attack_time = 0.01f;
        drums_track->decay_time = 0.05f;
        drums_track->sustain_level = 0.3f;
        drums_track->release_time = 0.1f;
        songwriter_add_track(song, drums_track);
    }

    Track* strings_track = songwriter_create_track(song, "Strings", 2);
    if (strings_track) {
        strings_track->program = 48; // String Ensemble 1
        strings_track->wave_type = WAVE_SAW; // Saw wave for strings
        strings_track->volume_float = 0.25f;
        strings_track->attack_time = 0.3f;
        strings_track->decay_time = 0.4f;
        strings_track->sustain_level = 0.5f;
        strings_track->release_time = 0.8f;
        songwriter_add_track(song, strings_track);
    }

    editor->current_song = song;
    // Set current track to the first track in the song (after it's been added)
    if (song->track_count > 0) {
        editor->current_track = &song->tracks[0];
    } else {
        editor->current_track = NULL;
    }

    songwriter_load_song(editor->writer, song);

    // Set up looping for the entire song
    if (song->total_ticks > 0) {
        songwriter_set_loop(editor->writer, 0, song->total_ticks);
    }

    return true;
}

bool song_editor_load_song(SongEditor* editor, Song* song) {
    if (!editor || !song) return false;

    editor->current_song = song;
    if (song->track_count > 0) {
        editor->current_track = &song->tracks[0];
    } else {
        editor->current_track = NULL;
    }

    songwriter_load_song(editor->writer, song);

    // Set up looping for the entire song
    if (song->total_ticks > 0) {
        songwriter_set_loop(editor->writer, 0, song->total_ticks);
    }

    return true;
}

bool song_editor_save_song(SongEditor* editor, const char* filename) {
    if (!editor || !editor->current_song) return false;

    // TODO: Implement song saving to file
    printf("Saving song: %s\n", filename);
    return true;
}

bool song_editor_add_track(SongEditor* editor, const char* name, uint8_t channel) {
    if (!editor || !editor->current_song) return false;

    Track* track = songwriter_create_track(editor->current_song, name, channel);
    if (!track) return false;

    if (songwriter_add_track(editor->current_song, track)) {
        if (!editor->current_track) {
            editor->current_track = track;
        }
        return true;
    }

    return false;
}

bool song_editor_remove_track(SongEditor* editor, uint8_t track_index) {
    if (!editor || !editor->current_song || track_index >= editor->current_song->track_count) return false;

    // TODO: Implement track removal
    printf("Removing track %u\n", track_index);
    return true;
}

bool song_editor_select_track(SongEditor* editor, uint8_t track_index) {
    if (!editor || !editor->current_song || track_index >= editor->current_song->track_count) return false;

    editor->current_track = &editor->current_song->tracks[track_index];
    return true;
}

bool song_editor_add_note(SongEditor* editor, uint8_t note, uint32_t start_time, uint32_t duration, uint8_t velocity) {
    if (!editor || !editor->current_track) return false;

    bool success = songwriter_add_note(editor->current_track, note, velocity, start_time, duration);
    if (success && editor->current_song) {
        songwriter_update_song_duration(editor->current_song);
        // Update loop points if needed
        if (editor->current_song->total_ticks > 0) {
            songwriter_set_loop(editor->writer, 0, editor->current_song->total_ticks);
        }
    }
    return success;
}

bool song_editor_remove_note(SongEditor* editor, uint8_t track_index, uint32_t event_index) {
    if (!editor || !editor->current_song || track_index >= editor->current_song->track_count) return false;

    Track* track = &editor->current_song->tracks[track_index];
    return songwriter_remove_note(track, event_index);
}

bool song_editor_modify_note(SongEditor* editor, uint8_t track_index, uint32_t event_index, uint8_t note, uint32_t start_time, uint32_t duration, uint8_t velocity) {
    if (!editor || !editor->current_song || track_index >= editor->current_song->track_count) return false;

    Track* track = &editor->current_song->tracks[track_index];
    return songwriter_modify_note(track, event_index, note, velocity, start_time, duration);
}

void song_editor_handle_mouse(SongEditor* editor, int x, int y, bool down, bool up, bool right_click) {
    if (!editor) return;

    editor->mouse_x = x;
    editor->mouse_y = y;

    printf("Mouse: down=%d, up=%d, right=%d, x=%d, y=%d\n", down, up, right_click, x, y);

    if (down && !editor->mouse_down) {
        editor->mouse_down = true;
        editor->drag_start_x = x;
        editor->drag_start_y = y;

        // Check if clicking in toolbar
        if (x >= editor->toolbar_rect.x && x < editor->toolbar_rect.x + editor->toolbar_rect.w &&
            y >= editor->toolbar_rect.y && y < editor->toolbar_rect.y + editor->toolbar_rect.h) {

            // Play button (10, 10, 60, 30)
            if (x >= 10 && x < 70 && y >= 10 && y < 40) {
                printf("Play button clicked\n");
                if (editor->is_playing) {
                    song_editor_pause(editor);
                } else {
                    song_editor_play(editor);
                }
            }
            // Select button (80, 10, 60, 30)
            else if (x >= 80 && x < 140 && y >= 10 && y < 40) {
                printf("Select button clicked\n");
                editor->state = EDITOR_STATE_SELECT;
            }
            // Draw button (150, 10, 60, 30)
            else if (x >= 150 && x < 210 && y >= 10 && y < 40) {
                printf("Draw button clicked\n");
                editor->state = EDITOR_STATE_DRAW;
            }
            // Erase button (220, 10, 60, 30)
            else if (x >= 220 && x < 280 && y >= 10 && y < 40) {
                printf("Erase button clicked\n");
                editor->state = EDITOR_STATE_ERASE;
            }
            // Grid button (290, 10, 60, 30)
            else if (x >= 290 && x < 350 && y >= 10 && y < 40) {
                printf("Grid button clicked\n");
                editor->show_grid = !editor->show_grid;
            }
            // Loop button (360, 10, 60, 30)
            else if (x >= 360 && x < 420 && y >= 10 && y < 40) {
                printf("Loop button clicked\n");
                editor->loop_enabled = !editor->loop_enabled;
            }
        }
        // Check if clicking in track list
        else if (x >= editor->track_list_rect.x && x < editor->track_list_rect.x + editor->track_list_rect.w &&
                 y >= editor->track_list_rect.y && y < editor->track_list_rect.y + editor->track_list_rect.h) {

            if (editor->current_song) {
                int track_y = y - editor->track_list_rect.y;
                int track_index = (track_y - 10) / 25;

                if (track_index >= 0 && track_index < editor->current_song->track_count) {
                    printf("Track %d selected\n", track_index);
                    song_editor_select_track(editor, track_index);
                }
            }
        }
        // Check if clicking in piano roll
        else if (x >= editor->piano_roll_rect.x && x < editor->piano_roll_rect.x + editor->piano_roll_rect.w &&
                 y >= editor->piano_roll_rect.y && y < editor->piano_roll_rect.y + editor->piano_roll_rect.h) {

            uint8_t note;
            uint32_t time;
            song_editor_screen_to_grid(editor, x, y, &note, &time);

            if (right_click) {
                // Right-click removes notes regardless of state
                printf("RIGHT-CLICK DETECTED: removing note at note=%d, time=%u\n", note, time);
                if (editor->current_track) {
                    printf("Searching through %u events in current track\n", editor->current_track->event_count);
                    for (uint32_t i = 0; i < editor->current_track->event_count; i++) {
                        NoteEvent* event = &editor->current_track->events[i];
                        printf("  Event %u: note=%d, start_time=%u\n", i, event->note, event->start_time);
                        // Allow small tolerance for time matching (within 10 ticks)
                        if (event->note == note && abs((int)event->start_time - (int)time) <= 10) {
                            printf("  MATCH FOUND! Removing event %u (note=%d, time=%u vs click_time=%u)\n", i, event->note, event->start_time, time);
                            songwriter_remove_note(editor->current_track, i);
                            // Update song duration after removal
                            if (editor->current_song) {
                                songwriter_update_song_duration(editor->current_song);
                                if (editor->current_song->total_ticks > 0) {
                                    songwriter_set_loop(editor->writer, 0, editor->current_song->total_ticks);
                                }
                            }
                            break;
                        }
                    }
                }
            } else {
                switch (editor->state) {
                    case EDITOR_STATE_DRAW:
                        printf("Adding note: note=%d, time=%u, track_count=%u, current_track_events=%u\n",
                               note, time, editor->current_song->track_count,
                               editor->current_track ? editor->current_track->event_count : 0);
                        bool success = song_editor_add_note(editor, note, time, 480, 100); // Quarter note
                        printf("Note add result: %s\n", success ? "SUCCESS" : "FAILED");
                        break;
                    case EDITOR_STATE_ERASE:
                        // Find and remove note at this position
                        if (editor->current_track) {
                            for (uint32_t i = 0; i < editor->current_track->event_count; i++) {
                                NoteEvent* event = &editor->current_track->events[i];
                                if (event->note == note && event->start_time == time) {
                                    songwriter_remove_note(editor->current_track, i);
                                    break;
                                }
                            }
                        }
                        break;
                    case EDITOR_STATE_SELECT:
                        // Select note at this position
                        song_editor_clear_selection(editor);
                        if (editor->current_track) {
                            for (uint32_t i = 0; i < editor->current_track->event_count; i++) {
                                NoteEvent* event = &editor->current_track->events[i];
                                if (event->note == note && event->start_time == time) {
                                    song_editor_select_note(editor, 0, i);
                                    break;
                                }
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (up && editor->mouse_down) {
        editor->mouse_down = false;
    }
}

void song_editor_handle_keyboard(SongEditor* editor, SDL_Keycode key, bool pressed) {
    if (!editor || !pressed) return;

    // Check for Ctrl key
    const Uint8* keystate = SDL_GetKeyboardState(NULL);
    bool ctrl_held = keystate[SDL_SCANCODE_LCTRL] || keystate[SDL_SCANCODE_RCTRL];

    switch (key) {
        case SDLK_SPACE:
            if (editor->is_playing) {
                song_editor_pause(editor);
            } else {
                song_editor_play(editor);
            }
            break;
        case SDLK_s:
            if (ctrl_held) {
                // Ctrl+S - Save song as JSON
                song_editor_save_json(editor, "song.json");
                printf("Song saved to song.json\n");
            } else {
                editor->state = EDITOR_STATE_SELECT;
            }
            break;
        case SDLK_o:
            if (ctrl_held) {
                // Ctrl+O - Open song from JSON
                if (song_editor_load_json(editor, "song.json")) {
                    printf("Song loaded from song.json\n");
                }
            }
            break;
        case SDLK_d:
            editor->state = EDITOR_STATE_DRAW;
            break;
        case SDLK_e:
            editor->state = EDITOR_STATE_ERASE;
            break;
        case SDLK_i:
            // Toggle instrument editor
            song_editor_toggle_instrument_editor(editor);
            break;
        case SDLK_DELETE:
            // Delete selected notes
            for (int i = 0; i < editor->selection_count; i++) {
                NoteSelection* sel = &editor->selections[i];
                song_editor_remove_note(editor, sel->track_index, sel->event_index);
            }
            song_editor_clear_selection(editor);
            break;
        case SDLK_g:
            editor->show_grid = !editor->show_grid;
            break;
        case SDLK_n:
            editor->snap_to_grid = !editor->snap_to_grid;
            break;
        case SDLK_t:
            // Add a new track
            song_editor_add_track(editor, "Track", editor->current_song->track_count);
            printf("Added new track. Total tracks: %u\n", editor->current_song->track_count);
            break;
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            editor->zoom_level = fminf(4.0f, editor->zoom_level * 1.2f);
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            editor->zoom_level = fmaxf(0.25f, editor->zoom_level / 1.2f);
            break;
    }
}

void song_editor_handle_mouse_wheel(SongEditor* editor, int x, int y, int delta) {
    if (!editor) return;

    if (delta > 0) {
        editor->zoom_level = fminf(4.0f, editor->zoom_level * 1.1f);
    } else {
        editor->zoom_level = fmaxf(0.25f, editor->zoom_level / 1.1f);
    }
}

void song_editor_render(SongEditor* editor) {
    if (!editor) return;

    song_editor_render_toolbar(editor);
    song_editor_render_timeline(editor);
    song_editor_render_track_list(editor);
    song_editor_render_piano_roll(editor);
    song_editor_render_playhead(editor);

    // Draw instrument editor if visible
    if (editor->show_instrument_editor) {
        song_editor_render_instrument_editor(editor);
    }
}

void song_editor_render_piano_roll(SongEditor* editor) {
    if (!editor) return;

    // Draw piano roll background
    SDL_SetRenderDrawColor(RENDERER, 40, 40, 40, 255);
    SDL_RenderFillRect(RENDERER, &editor->piano_roll_rect);

    // Draw grid
    if (editor->show_grid) {
        SDL_SetRenderDrawColor(RENDERER, 60, 60, 60, 255);

        // Vertical lines (time)
        for (int x = 0; x < editor->piano_roll_rect.w; x += GRID_SIZE * editor->zoom_level) {
            SDL_RenderDrawLine(RENDERER,
                              editor->piano_roll_rect.x + x,
                              editor->piano_roll_rect.y,
                              editor->piano_roll_rect.x + x,
                              editor->piano_roll_rect.y + editor->piano_roll_rect.h);
        }

        // Horizontal lines (notes)
        for (int y = 0; y < editor->piano_roll_rect.h; y += NOTE_HEIGHT) {
            SDL_RenderDrawLine(RENDERER,
                              editor->piano_roll_rect.x,
                              editor->piano_roll_rect.y + y,
                              editor->piano_roll_rect.x + editor->piano_roll_rect.w,
                              editor->piano_roll_rect.y + y);
        }
    }

    song_editor_render_notes(editor);
}

void song_editor_render_timeline(SongEditor* editor) {
    if (!editor) return;

    // Draw timeline background
    SDL_SetRenderDrawColor(RENDERER, 30, 30, 30, 255);
    SDL_RenderFillRect(RENDERER, &editor->timeline_rect);

    // Draw measure markers
    SDL_SetRenderDrawColor(RENDERER, 100, 100, 100, 255);
    for (int x = 0; x < editor->timeline_rect.w; x += 480 * editor->zoom_level) { // 480 ticks = 1 beat
        SDL_RenderDrawLine(RENDERER,
                          editor->timeline_rect.x + x,
                          editor->timeline_rect.y,
                          editor->timeline_rect.x + x,
                          editor->timeline_rect.y + editor->timeline_rect.h);
    }

    // Draw time labels
    SDL_Color text_color = {200, 200, 200, 255};
    for (int i = 0; i < 10; i++) {
        int x = editor->timeline_rect.x + i * 480 * editor->zoom_level;
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%d", i);
        window_render_text(time_str, x + 5, editor->timeline_rect.y + 5, text_color);
    }
}

void song_editor_render_toolbar(SongEditor* editor) {
    if (!editor) return;

    // Draw toolbar background
    SDL_SetRenderDrawColor(RENDERER, 50, 50, 50, 255);
    SDL_RenderFillRect(RENDERER, &editor->toolbar_rect);

    // Draw toolbar buttons
    SDL_Color text_color = {200, 200, 200, 255};
    SDL_Color button_color = {80, 80, 80, 255};

    // Play button
    SDL_Rect play_rect = {10, 10, 60, 30};
    SDL_SetRenderDrawColor(RENDERER, button_color.r, button_color.g, button_color.b, button_color.a);
    SDL_RenderFillRect(RENDERER, &play_rect);
    window_render_text(editor->is_playing ? "Pause" : "Play", 15, 15, text_color);

    // State buttons
    const char* states[] = {"Select", "Draw", "Erase"};
    for (int i = 0; i < 3; i++) {
        SDL_Rect state_rect = {80 + i * 70, 10, 60, 30};
        SDL_SetRenderDrawColor(RENDERER,
                              (editor->state == i) ? 120 : 80,
                              (editor->state == i) ? 120 : 80,
                              (editor->state == i) ? 120 : 80, 255);
        SDL_RenderFillRect(RENDERER, &state_rect);
        window_render_text(states[i], 85 + i * 70, 15, text_color);
    }

    // Grid toggle
    SDL_Rect grid_rect = {290, 10, 60, 30};
    SDL_SetRenderDrawColor(RENDERER, editor->show_grid ? 120 : 80,
                          editor->show_grid ? 120 : 80,
                          editor->show_grid ? 120 : 80, 255);
    SDL_RenderFillRect(RENDERER, &grid_rect);
    window_render_text("Grid", 295, 15, text_color);

    // Loop toggle
    SDL_Rect loop_rect = {360, 10, 60, 30};
    SDL_SetRenderDrawColor(RENDERER, editor->loop_enabled ? 120 : 80,
                          editor->loop_enabled ? 120 : 80,
                          editor->loop_enabled ? 120 : 80, 255);
    SDL_RenderFillRect(RENDERER, &loop_rect);
    window_render_text("Loop", 365, 15, text_color);

    // Song info
    if (editor->current_song) {
        char info_str[128];
        snprintf(info_str, sizeof(info_str), "Song: %s | Tempo: %u BPM",
                editor->current_song->name, editor->current_song->tempo);
        window_render_text(info_str, 400, 15, text_color);
    }
}

void song_editor_render_track_list(SongEditor* editor) {
    if (!editor) return;

    // Draw track list background
    SDL_SetRenderDrawColor(RENDERER, 35, 35, 35, 255);
    SDL_RenderFillRect(RENDERER, &editor->track_list_rect);

    // Draw track names
    SDL_Color text_color = {200, 200, 200, 255};
    if (editor->current_song) {
        for (uint8_t i = 0; i < editor->current_song->track_count; i++) {
            Track* track = &editor->current_song->tracks[i];
            int y = editor->track_list_rect.y + 10 + i * 25;

            // Highlight current track
            if (track == editor->current_track) {
                SDL_Rect highlight_rect = {editor->track_list_rect.x, y - 5, editor->track_list_rect.w, 20};
                SDL_SetRenderDrawColor(RENDERER, 80, 80, 120, 255);
                SDL_RenderFillRect(RENDERER, &highlight_rect);
            }

            window_render_text(track->name, editor->track_list_rect.x + 10, y, text_color);
        }
    }
}

void song_editor_render_notes(SongEditor* editor) {
    if (!editor || !editor->current_song) return;

    SDL_Color note_color = {100, 150, 255, 255};
    SDL_Color selected_color = {255, 200, 100, 255};

    printf("Rendering notes: song_tracks=%u\n", editor->current_song->track_count);
    for (uint8_t track_idx = 0; track_idx < editor->current_song->track_count; track_idx++) {
        Track* track = &editor->current_song->tracks[track_idx];
        printf("  Track %u: events=%u\n", track_idx, track->event_count);

        for (uint32_t event_idx = 0; event_idx < track->event_count; event_idx++) {
            NoteEvent* event = &track->events[event_idx];

            // Convert note to screen coordinates
            int note_x = song_editor_time_to_screen(editor, event->start_time);
            int note_y = song_editor_note_to_screen(editor, event->note);
            int note_w = (int)(event->duration * editor->zoom_level); // Fixed width based on duration
            int note_h = NOTE_HEIGHT - 2;

            // Check if note is visible
            if (note_x + note_w >= editor->piano_roll_rect.x &&
                note_x <= editor->piano_roll_rect.x + editor->piano_roll_rect.w &&
                note_y >= editor->piano_roll_rect.y &&
                note_y <= editor->piano_roll_rect.y + editor->piano_roll_rect.h) {

                SDL_Rect note_rect = {note_x, note_y, note_w, note_h};

                // Check if note is selected
                bool is_selected = song_editor_is_note_selected(editor, track_idx, event_idx);
                SDL_Color color = is_selected ? selected_color : note_color;

                SDL_SetRenderDrawColor(RENDERER, color.r, color.g, color.b, color.a);
                SDL_RenderFillRect(RENDERER, &note_rect);

                // Draw note border
                SDL_SetRenderDrawColor(RENDERER, 50, 50, 50, 255);
                SDL_RenderDrawRect(RENDERER, &note_rect);
            }
        }
    }
}

void song_editor_render_playhead(SongEditor* editor) {
    if (!editor) return;

    // Draw playhead line
    int playhead_x = song_editor_time_to_screen(editor, editor->playhead_position);
    SDL_SetRenderDrawColor(RENDERER, 255, 100, 100, 255);
    SDL_RenderDrawLine(RENDERER,
                      playhead_x, editor->piano_roll_rect.y,
                      playhead_x, editor->piano_roll_rect.y + editor->piano_roll_rect.h);
}

// Utility functions
void song_editor_screen_to_grid(SongEditor* editor, int screen_x, int screen_y, uint8_t* note, uint32_t* time) {
    if (!editor || !note || !time) return;

    // Convert screen coordinates to note and time
    *note = song_editor_screen_to_note(editor, screen_y);
    *time = song_editor_screen_to_time(editor, screen_x);

    // Snap to grid if enabled
    if (editor->snap_to_grid) {
        *time = (*time / (GRID_SIZE * editor->zoom_level)) * (GRID_SIZE * editor->zoom_level);
    }
}

void song_editor_grid_to_screen(SongEditor* editor, uint8_t note, uint32_t time, int* screen_x, int* screen_y) {
    if (!editor || !screen_x || !screen_y) return;

    *screen_x = song_editor_time_to_screen(editor, time);
    *screen_y = song_editor_note_to_screen(editor, note);
}

uint8_t song_editor_screen_to_note(SongEditor* editor, int screen_y) {
    if (!editor) return 60; // Middle C

    int relative_y = screen_y - editor->piano_roll_rect.y;
    int note_index = (PIANO_ROLL_HEIGHT - relative_y) / NOTE_HEIGHT;
    return (uint8_t)(60 + note_index); // Start from middle C
}

uint32_t song_editor_screen_to_time(SongEditor* editor, int screen_x) {
    if (!editor) return 0;

    int relative_x = screen_x - editor->piano_roll_rect.x;
    return (uint32_t)(relative_x / editor->zoom_level);
}

int song_editor_note_to_screen(SongEditor* editor, uint8_t note) {
    if (!editor) return 0;

    int note_index = note - 60; // Relative to middle C
    return editor->piano_roll_rect.y + PIANO_ROLL_HEIGHT - (note_index * NOTE_HEIGHT) - NOTE_HEIGHT;
}

int song_editor_time_to_screen(SongEditor* editor, uint32_t time) {
    if (!editor) return 0;

    return editor->piano_roll_rect.x + (int)(time * editor->zoom_level);
}

// Playback control
void song_editor_play(SongEditor* editor) {
    if (!editor) return;
    editor->is_playing = true;
    songwriter_play(editor->writer);
}

void song_editor_stop(SongEditor* editor) {
    if (!editor) return;
    editor->is_playing = false;
    editor->playhead_position = 0;
    songwriter_stop(editor->writer);

    // Ensure synthesizer is completely silent
    if (editor->writer && editor->writer->synth) {
        for (int i = 0; i < 16; i++) {
            editor->writer->synth->channels[i].active = false;
            editor->writer->synth->channels[i].note_on = false;
            editor->writer->synth->channels[i].envelope_level = 0.0f;
            editor->writer->synth->channels[i].envelope_time = 0.0f;
        }
    }
}

void song_editor_pause(SongEditor* editor) {
    if (!editor) return;
    editor->is_playing = false;
    songwriter_pause(editor->writer);
}

void song_editor_seek(SongEditor* editor, uint32_t time) {
    if (!editor) return;
    editor->playhead_position = time;
    songwriter_seek(editor->writer, time);
}

// Selection
void song_editor_clear_selection(SongEditor* editor) {
    if (!editor) return;
    editor->selection_count = 0;
}

void song_editor_select_note(SongEditor* editor, uint8_t track_index, uint32_t event_index) {
    if (!editor || editor->selection_count >= editor->selection_capacity) return;

    NoteSelection* sel = &editor->selections[editor->selection_count];
    sel->track_index = track_index;
    sel->event_index = event_index;
    sel->selected = true;
    editor->selection_count++;
}

void song_editor_deselect_note(SongEditor* editor, uint8_t track_index, uint32_t event_index) {
    if (!editor) return;

    for (int i = 0; i < editor->selection_count; i++) {
        NoteSelection* sel = &editor->selections[i];
        if (sel->track_index == track_index && sel->event_index == event_index) {
            // Remove this selection by shifting remaining ones
            for (int j = i; j < editor->selection_count - 1; j++) {
                editor->selections[j] = editor->selections[j + 1];
            }
            editor->selection_count--;
            break;
        }
    }
}

bool song_editor_is_note_selected(SongEditor* editor, uint8_t track_index, uint32_t event_index) {
    if (!editor) return false;

    for (int i = 0; i < editor->selection_count; i++) {
        NoteSelection* sel = &editor->selections[i];
        if (sel->track_index == track_index && sel->event_index == event_index) {
            return true;
        }
    }
    return false;
}

// Status
bool song_editor_is_playing(SongEditor* editor) {
    return editor ? editor->is_playing : false;
}

uint32_t song_editor_get_playhead_position(SongEditor* editor) {
    return editor ? editor->playhead_position : 0;
}

void song_editor_print_status(SongEditor* editor) {
    if (!editor) return;

    printf("Song Editor Status:\n");
    printf("  State: %d\n", editor->state);
    printf("  Playing: %s\n", editor->is_playing ? "Yes" : "No");
    printf("  Zoom: %.2f\n", editor->zoom_level);
    printf("  Grid: %s\n", editor->show_grid ? "On" : "Off");
    printf("  Snap: %s\n", editor->snap_to_grid ? "On" : "Off");
    printf("  Selected Notes: %d\n", editor->selection_count);

    if (editor->current_song) {
        printf("  Current Song: %s\n", editor->current_song->name);
        printf("  Tracks: %u\n", editor->current_song->track_count);
    }
}

// Instrument editor implementation
void song_editor_toggle_instrument_editor(SongEditor* editor) {
    if (!editor) return;
    editor->show_instrument_editor = !editor->show_instrument_editor;
}

void song_editor_render_instrument_editor(SongEditor* editor) {
    if (!editor || !RENDERER || !editor->current_song) return;

    // Draw background
    SDL_SetRenderDrawColor(RENDERER, 40, 40, 40, 255);
    SDL_RenderFillRect(RENDERER, &editor->instrument_editor_rect);

    // Draw border
    SDL_SetRenderDrawColor(RENDERER, 100, 100, 100, 255);
    SDL_RenderDrawRect(RENDERER, &editor->instrument_editor_rect);

    // Title bar
    SDL_Rect title_rect = {
        editor->instrument_editor_rect.x,
        editor->instrument_editor_rect.y,
        editor->instrument_editor_rect.w,
        30
    };
    SDL_SetRenderDrawColor(RENDERER, 60, 60, 60, 255);
    SDL_RenderFillRect(RENDERER, &title_rect);

    // Close button
    SDL_Rect close_button = {
        editor->instrument_editor_rect.x + editor->instrument_editor_rect.w - 25,
        editor->instrument_editor_rect.y + 5,
        20, 20
    };
    SDL_SetRenderDrawColor(RENDERER, 200, 50, 50, 255);
    SDL_RenderFillRect(RENDERER, &close_button);

    if (editor->selected_instrument_track >= editor->current_song->track_count) return;
    Track* track = &editor->current_song->tracks[editor->selected_instrument_track];

    int y = editor->instrument_editor_rect.y + 50;
    int x = editor->instrument_editor_rect.x + 20;

    // Wave type selector
    const char* wave_names[] = {"Sine", "Saw", "Square", "Triangle", "Pulse", "Noise"};
    for (int i = 0; i < 6; i++) {
        SDL_Rect wave_button = {x + i * 90, y, 80, 30};
        if (track->wave_type == i) {
            SDL_SetRenderDrawColor(RENDERER, 100, 150, 255, 255);
        } else {
            SDL_SetRenderDrawColor(RENDERER, 80, 80, 80, 255);
        }
        SDL_RenderFillRect(RENDERER, &wave_button);
        SDL_SetRenderDrawColor(RENDERER, 150, 150, 150, 255);
        SDL_RenderDrawRect(RENDERER, &wave_button);
    }

    y += 50;

    // ADSR sliders
    struct {
        const char* name;
        float* value;
        float min, max;
    } sliders[] = {
        {"Attack", &track->attack_time, 0.001f, 2.0f},
        {"Decay", &track->decay_time, 0.01f, 2.0f},
        {"Sustain", &track->sustain_level, 0.0f, 1.0f},
        {"Release", &track->release_time, 0.01f, 5.0f},
        {"Volume", &track->volume_float, 0.0f, 1.0f}
    };

    for (int i = 0; i < 5; i++) {
        // Slider background
        SDL_Rect slider_bg = {x, y + i * 60, 400, 40};
        SDL_SetRenderDrawColor(RENDERER, 50, 50, 50, 255);
        SDL_RenderFillRect(RENDERER, &slider_bg);

        // Slider track
        SDL_Rect slider_track = {x + 10, y + i * 60 + 15, 380, 10};
        SDL_SetRenderDrawColor(RENDERER, 70, 70, 70, 255);
        SDL_RenderFillRect(RENDERER, &slider_track);

        // Slider handle
        float normalized = (*sliders[i].value - sliders[i].min) / (sliders[i].max - sliders[i].min);
        int handle_x = x + 10 + (int)(normalized * 380);
        SDL_Rect handle = {handle_x - 5, y + i * 60 + 10, 10, 20};
        SDL_SetRenderDrawColor(RENDERER, 150, 150, 255, 255);
        SDL_RenderFillRect(RENDERER, &handle);
    }
}

void song_editor_handle_instrument_editor_input(SongEditor* editor, int x, int y, bool mouse_down) {
    if (!editor || !editor->current_song || !mouse_down) return;

    // Check close button
    SDL_Rect close_button = {
        editor->instrument_editor_rect.x + editor->instrument_editor_rect.w - 25,
        editor->instrument_editor_rect.y + 5,
        20, 20
    };

    if (x >= close_button.x && x <= close_button.x + close_button.w &&
        y >= close_button.y && y <= close_button.y + close_button.h) {
        editor->show_instrument_editor = false;
        return;
    }

    if (editor->selected_instrument_track >= editor->current_song->track_count) return;
    Track* track = &editor->current_song->tracks[editor->selected_instrument_track];

    int base_y = editor->instrument_editor_rect.y + 50;
    int base_x = editor->instrument_editor_rect.x + 20;

    // Check wave type buttons
    for (int i = 0; i < 6; i++) {
        SDL_Rect wave_button = {base_x + i * 90, base_y, 80, 30};
        if (x >= wave_button.x && x <= wave_button.x + wave_button.w &&
            y >= wave_button.y && y <= wave_button.y + wave_button.h) {
            track->wave_type = i;
            // Update synthesizer if playing
            if (editor->writer && editor->writer->synth) {
                editor->writer->synth->channels[track->channel].wave_type = i;
            }
            return;
        }
    }

    // Check ADSR sliders
    base_y += 50;
    struct {
        float* value;
        float min, max;
    } sliders[] = {
        {&track->attack_time, 0.001f, 2.0f},
        {&track->decay_time, 0.01f, 2.0f},
        {&track->sustain_level, 0.0f, 1.0f},
        {&track->release_time, 0.01f, 5.0f},
        {&track->volume_float, 0.0f, 1.0f}
    };

    for (int i = 0; i < 5; i++) {
        SDL_Rect slider_area = {base_x + 10, base_y + i * 60 + 10, 380, 20};
        if (x >= slider_area.x && x <= slider_area.x + slider_area.w &&
            y >= slider_area.y && y <= slider_area.y + slider_area.h) {
            float normalized = (float)(x - slider_area.x) / slider_area.w;
            *sliders[i].value = sliders[i].min + normalized * (sliders[i].max - sliders[i].min);

            // Update synthesizer if playing
            if (editor->writer && editor->writer->synth && i < 4) {
                Channel* channel = &editor->writer->synth->channels[track->channel];
                channel->attack_time = track->attack_time;
                channel->decay_time = track->decay_time;
                channel->sustain_level = track->sustain_level;
                channel->release_time = track->release_time;
            }
            return;
        }
    }
}

void song_editor_set_track_wave_type(SongEditor* editor, int track_idx, int wave_type) {
    if (!editor || !editor->current_song || track_idx >= editor->current_song->track_count) return;
    editor->current_song->tracks[track_idx].wave_type = wave_type;
}

void song_editor_set_track_envelope(SongEditor* editor, int track_idx, float attack, float decay, float sustain, float release) {
    if (!editor || !editor->current_song || track_idx >= editor->current_song->track_count) return;
    Track* track = &editor->current_song->tracks[track_idx];
    track->attack_time = attack;
    track->decay_time = decay;
    track->sustain_level = sustain;
    track->release_time = release;
}

// JSON Save/Load implementation
bool song_editor_save_json(SongEditor* editor, const char* filename) {
    if (!editor || !editor->current_song || !filename) return false;

    FILE* file = fopen(filename, "w");
    if (!file) return false;

    fprintf(file, "{\n");
    fprintf(file, "  \"name\": \"%s\",\n", editor->current_song->name);
    fprintf(file, "  \"artist\": \"%s\",\n", editor->current_song->artist);
    fprintf(file, "  \"tempo\": %d,\n", editor->current_song->tempo);
    fprintf(file, "  \"tracks\": [\n");

    for (uint8_t t = 0; t < editor->current_song->track_count; t++) {
        Track* track = &editor->current_song->tracks[t];
        fprintf(file, "    {\n");
        fprintf(file, "      \"name\": \"%s\",\n", track->name);
        fprintf(file, "      \"channel\": %d,\n", track->channel);
        fprintf(file, "      \"wave_type\": %d,\n", track->wave_type);
        fprintf(file, "      \"volume\": %.3f,\n", track->volume_float);
        fprintf(file, "      \"attack\": %.3f,\n", track->attack_time);
        fprintf(file, "      \"decay\": %.3f,\n", track->decay_time);
        fprintf(file, "      \"sustain\": %.3f,\n", track->sustain_level);
        fprintf(file, "      \"release\": %.3f,\n", track->release_time);
        fprintf(file, "      \"notes\": [\n");

        for (uint32_t n = 0; n < track->event_count; n++) {
            NoteEvent* note = &track->events[n];
            fprintf(file, "        {\"note\": %d, \"velocity\": %d, \"tick\": %d, \"duration\": %d}",
                    note->note, note->velocity, note->start_time, note->duration);
            if (n < track->event_count - 1) fprintf(file, ",");
            fprintf(file, "\n");
        }

        fprintf(file, "      ]\n");
        fprintf(file, "    }");
        if (t < editor->current_song->track_count - 1) fprintf(file, ",");
        fprintf(file, "\n");
    }

    fprintf(file, "  ]\n");
    fprintf(file, "}\n");

    fclose(file);
    return true;
}

bool song_editor_load_json(SongEditor* editor, const char* filename) {
    if (!editor || !filename) return false;

    // For now, just return false - full JSON parsing would require a JSON library
    printf("JSON loading not fully implemented yet: %s\n", filename);
    return false;
}

// Enhanced functions placeholders
bool song_editor_is_recording(SongEditor* editor) {
    return editor ? editor->is_recording : false;
}

float song_editor_get_playhead_time(SongEditor* editor) {
    if (!editor || !editor->writer) return 0.0f;
    return songwriter_get_current_time(editor->writer);
}
