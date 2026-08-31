/*
 * sequencer_ui_demo.c - Demonstration of the sequencer UI system
 *
 * This shows how to integrate the sequencer UI with the verse client
 * and demonstrates the modular architecture in action.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include "songwriter/songwriter.h"
#include "sequencer_ui_core.h"

// Demo song creation
Song* create_demo_song(Songwriter* songwriter) {
    printf("🎵 Creating demo song...\n");

    // Create a new song
    Song* song = songwriter_create_song("Demo Song", "Verse Sequencer", 120);
    if (!song) {
        printf("❌ Failed to create demo song\n");
        return NULL;
    }

    // Create tracks
    Track* bass_track = songwriter_create_track(song, "Bass", 0);
    Track* lead_track = songwriter_create_track(song, "Lead", 1);
    Track* drum_track = songwriter_create_track(song, "Drums", 9);

    if (!bass_track || !lead_track || !drum_track) {
        printf("❌ Failed to create tracks\n");
        songwriter_destroy_song(song);
        return NULL;
    }

    // Add some notes to create a simple pattern

    // Bass line (C major scale)
    songwriter_add_note(bass_track, 36, 100, 0, 480);      // C2
    songwriter_add_note(bass_track, 40, 100, 480, 480);    // E2
    songwriter_add_note(bass_track, 43, 100, 960, 480);    // G2
    songwriter_add_note(bass_track, 48, 100, 1440, 480);   // C3

    // Lead melody
    songwriter_add_note(lead_track, 60, 80, 0, 240);       // C4
    songwriter_add_note(lead_track, 64, 80, 240, 240);     // E4
    songwriter_add_note(lead_track, 67, 80, 480, 240);     // G4
    songwriter_add_note(lead_track, 72, 80, 720, 480);     // C5
    songwriter_add_note(lead_track, 67, 80, 1200, 240);    // G4
    songwriter_add_note(lead_track, 64, 80, 1440, 240);    // E4
    songwriter_add_note(lead_track, 60, 80, 1680, 480);    // C4

    // Simple drum pattern
    songwriter_add_note(drum_track, 36, 100, 0, 120);      // Kick
    songwriter_add_note(drum_track, 38, 80, 240, 120);     // Snare
    songwriter_add_note(drum_track, 36, 100, 480, 120);    // Kick
    songwriter_add_note(drum_track, 38, 80, 720, 120);     // Snare
    songwriter_add_note(drum_track, 36, 100, 960, 120);    // Kick
    songwriter_add_note(drum_track, 38, 80, 1200, 120);    // Snare
    songwriter_add_note(drum_track, 36, 100, 1440, 120);   // Kick
    songwriter_add_note(drum_track, 38, 80, 1680, 120);    // Snare

    printf("✅ Demo song created with %d tracks and %d total notes\n",
           song->track_count,
           bass_track->event_count + lead_track->event_count + drum_track->event_count);

    return song;
}

// Demo sequencer UI
void demo_sequencer_ui(void) {
    printf("\n🎵 Sequencer UI Demo\n");
    printf("===================\n\n");

    // Note: SDL and TTF are now initialized by sequencer_ui_initialize()

    // Create songwriter system
    printf("🔧 Creating songwriter system...\n");
    Songwriter* songwriter = songwriter_create(44100);
    if (!songwriter) {
        printf("❌ Failed to create songwriter\n");
        return;
    }

    // Create demo song
    Song* demo_song = create_demo_song(songwriter);
    if (!demo_song) {
        printf("❌ Failed to create demo song\n");
        songwriter_destroy(songwriter);
        return;
    }

    // Load song into songwriter
    if (!songwriter_load_song(songwriter, demo_song)) {
        printf("❌ Failed to load demo song\n");
        songwriter_destroy_song(demo_song);
        songwriter_destroy(songwriter);
        return;
    }

    // Create sequencer UI
    printf("🎨 Creating sequencer UI...\n");
    SequencerUI* sequencer_ui = sequencer_ui_create(songwriter);
    if (!sequencer_ui) {
        printf("❌ Failed to create sequencer UI\n");
        // demo_song is owned by songwriter once loaded; avoid double free
        songwriter_destroy(songwriter);
        return;
    }

    // Initialize and open UI
    if (!sequencer_ui_initialize(sequencer_ui)) {
        printf("❌ Failed to initialize sequencer UI\n");
        sequencer_ui_destroy(sequencer_ui);
        // demo_song will be destroyed by songwriter_destroy
        songwriter_destroy(songwriter);
        return;
    }

    if (!sequencer_ui_open(sequencer_ui)) {
        printf("❌ Failed to open sequencer UI window\n");
        sequencer_ui_destroy(sequencer_ui);
        // demo_song will be destroyed by songwriter_destroy
        songwriter_destroy(songwriter);
        return;
    }

    printf("✅ Sequencer UI opened successfully!\n");
    printf("\n🎮 Controls:\n");
    printf("  SPACE - Play/Pause\n");
    printf("  S - Stop\n");
    printf("  R - Record\n");
    printf("  L - Toggle Loop\n");
    printf("  +/- - Zoom In/Out\n");
    printf("  ESC - Exit\n");
    printf("\n🎵 Demo song loaded with %d tracks\n", demo_song->track_count);

    // Main demo loop
    bool running = true;
    SDL_Event event;
    Uint32 last_time = SDL_GetTicks();

    while (running && sequencer_ui_is_open(sequencer_ui)) {
        Uint32 current_time = SDL_GetTicks();
        double delta_time = (current_time - last_time) / 1000.0;
        last_time = current_time;

        // Handle events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_SPACE:
                    if (sequencer_ui_is_playing(sequencer_ui)) {
                        sequencer_ui_pause(sequencer_ui);
                    } else {
                        sequencer_ui_play(sequencer_ui);
                    }
                    break;
                case SDLK_s:
                    sequencer_ui_stop(sequencer_ui);
                    break;
                case SDLK_r:
                    sequencer_ui_record(sequencer_ui);
                    break;
                case SDLK_l:
                    sequencer_ui_toggle_loop(sequencer_ui);
                    break;
                case SDLK_PLUS:
                case SDLK_EQUALS:
                    sequencer_ui_zoom_horizontal(sequencer_ui, 1.2f);
                    break;
                case SDLK_MINUS:
                    sequencer_ui_zoom_horizontal(sequencer_ui, 0.8f);
                    break;
                }
            }

            // Pass event to sequencer UI
            sequencer_ui_handle_event(sequencer_ui, &event);
        }

        // Update sequencer UI
        sequencer_ui_update(sequencer_ui, delta_time);

        // Render sequencer UI
        sequencer_ui_render(sequencer_ui);

        // Cap frame rate
        SDL_Delay(16); // ~60 FPS
    }

    printf("\n🔧 Shutting down sequencer UI...\n");

    // Cleanup
    sequencer_ui_close(sequencer_ui);
    sequencer_ui_destroy(sequencer_ui);
    // demo_song is destroyed by songwriter_destroy
    songwriter_destroy(songwriter);
    // Note: SDL and TTF cleanup handled by sequencer_ui_shutdown()

    printf("✅ Sequencer UI demo completed successfully!\n");
}

// Integration with verse client demo
void demo_verse_client_integration(void) {
    printf("\n🔗 Verse Client Integration Demo\n");
    printf("================================\n\n");

    printf("This demonstrates how the sequencer UI integrates with the verse client:\n\n");

    printf("1. **Audio Integration**:\n");
    printf("   - Sequencer UI uses the same songwriter system as background music\n");
    printf("   - Audio output is mixed with game audio\n");
    printf("   - Real-time audio generation during playback\n\n");

    printf("2. **UI Integration**:\n");
    printf("   - Sequencer UI opens as a separate window\n");
    printf("   - Can be toggled from the main game menu\n");
    printf("   - Shares the same SDL context and rendering system\n\n");

    printf("3. **State Management**:\n");
    printf("   - Sequencer state is managed by the client_state module\n");
    printf("   - Song data can be saved/loaded with world data\n");
    printf("   - Integration with the game's save system\n\n");

    printf("4. **Input Handling**:\n");
    printf("   - Keyboard shortcuts work in both game and sequencer\n");
    printf("   - MIDI input can be routed to sequencer or game\n");
    printf("   - Mouse input is context-aware\n\n");

    printf("5. **Modular Architecture**:\n");
    printf("   - Sequencer UI is a separate module (sequencer_ui_core)\n");
    printf("   - Can be enabled/disabled at compile time\n");
    printf("   - Clean interface with the main client system\n\n");

    printf("✅ Integration points identified and documented!\n");
}

// Main demo function
int main(int argc, char* argv[]) {
    printf("🎵 Verse Sequencer UI Demo\n");
    printf("==========================\n\n");

    printf("This demo shows the sequencer UI system built on top of the songwriter module.\n");
    printf("The songwriter system provides a solid foundation with:\n");
    printf("- 280+ API functions for complete sequencer functionality\n");
    printf("- Advanced audio features (polyphony, effects, high-resolution timing)\n");
    printf("- MIDI compatibility with full note/velocity/channel support\n");
    printf("- Modular architecture ready for UI integration\n\n");

    // Check command line arguments
    if (argc > 1 && strcmp(argv[1], "--integration") == 0) {
        demo_verse_client_integration();
        return 0;
    }

    // Run the interactive demo
    demo_sequencer_ui();

    printf("\n🎉 Demo completed!\n");
    printf("\nTo see the integration demo, run:\n");
    printf("  ./sequencer_ui_demo --integration\n");

    return 0;
}
