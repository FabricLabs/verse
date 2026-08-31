#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/midi_reader.h"
#include "src/songwriter/songwriter.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <midi_file>\n", argv[0]);
        printf("This program reads a MIDI file and converts it to the song format.\n");
        return 1;
    }

    const char* midi_filename = argv[1];

    // Check if file exists and is a valid MIDI file
    if (!midi_reader_is_valid_file(midi_filename)) {
        printf("Error: %s is not a valid MIDI file.\n", midi_filename);
        return 1;
    }

    printf("Loading MIDI file: %s\n", midi_filename);

    // Create MIDI reader and load file
    MIDIFile* midi = midi_reader_create();
    if (!midi) {
        printf("Error: Failed to create MIDI reader.\n");
        return 1;
    }

    if (!midi_reader_load_file(midi, midi_filename)) {
        MIDIError error = midi_reader_get_last_error();
        printf("Error loading MIDI file: %s\n", midi_reader_get_error_string(error));
        midi_reader_destroy(midi);
        return 1;
    }

    // Print MIDI file information
    printf("\n=== MIDI File Information ===\n");
    midi_reader_print_info(midi);

    // Convert to song format
    printf("\n=== Converting to Song Format ===\n");
    Song* song = midi_reader_convert_to_song(midi);
    if (!song) {
        printf("Error: Failed to convert MIDI to song format.\n");
        midi_reader_destroy(midi);
        return 1;
    }

    // Print song information
    printf("\n=== Converted Song Information ===\n");
    songwriter_print_song_info(song);

    // Export to JSON melody format
    printf("\n=== Exporting to JSON Melody Format ===\n");
    char base_name_buf[256];
    char json_filename[256];

    // Extract base name from the MIDI filename
    const char* base_name = strrchr(midi_filename, '/');
    if (base_name) {
        base_name++; // Skip the '/'
    } else {
        base_name = midi_filename;
    }

    // Copy base name to separate buffer
    strncpy(base_name_buf, base_name, sizeof(base_name_buf) - 1);
    base_name_buf[sizeof(base_name_buf) - 1] = '\0';

    // Remove .mid extension
    char* ext = strrchr(base_name_buf, '.');
    if (ext && strcmp(ext, ".mid") == 0) {
        *ext = '\0';
    }

    // Create full path
    snprintf(json_filename, sizeof(json_filename), "assets/melodies/%s.json", base_name_buf);

    printf("DEBUG: MIDI filename: %s\n", midi_filename);
    printf("DEBUG: Base name: %s\n", base_name);
    printf("DEBUG: Base name (cleaned): %s\n", base_name_buf);
    printf("DEBUG: JSON filename: %s\n", json_filename);

    if (midi_reader_export_song_to_json(song, json_filename, "Main", "Converted from MIDI")) {
        printf("Exported song to %s as melody 'Main'.\n", json_filename);
    } else {
        printf("Failed to export song to JSON melody format.\n");
    }

    // Create a simple songwriter to test playback
    printf("\n=== Testing Playback ===\n");
    Songwriter* writer = songwriter_create(44100);
    if (writer) {
        if (songwriter_load_song(writer, song)) {
            printf("Song loaded successfully for playback.\n");
            printf("Song duration: %.2f seconds\n",
                   (float)song->total_ticks / song->ticks_per_beat * 60.0f / song->tempo);

            // Generate a few seconds of audio to test
            printf("Generating 5 seconds of audio...\n");
            float buffer[44100 * 5]; // 5 seconds at 44.1kHz
            songwriter_generate_buffer(writer, buffer, 44100 * 5);
            printf("Audio generation completed.\n");
        } else {
            printf("Failed to load song for playback.\n");
        }
        songwriter_destroy(writer);
        // Do not call songwriter_destroy_song(song) here; already freed by songwriter_destroy
        song = NULL;
    }

    // Cleanup
    if (song) {
        songwriter_destroy_song(song);
    }
    midi_reader_destroy(midi);

    printf("\n=== Conversion Complete ===\n");
    printf("The MIDI file has been successfully converted to the song format.\n");
    printf("You can now use this song data in your application.\n");

    return 0;
}
