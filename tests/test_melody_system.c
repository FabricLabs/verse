#include "src/sequencer/melody_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
    printf("=== Melody System Test ===\n");

    // Initialize random seed
    srand(time(NULL));

    // Create melody library
    MelodyLibrary* library = melody_library_create();
    if (!library) {
        printf("Failed to create melody library\n");
        return 1;
    }

    // Load melodies from directory
    printf("Loading melodies from assets/melodies...\n");
    if (!melody_library_load_from_directory(library, "assets/melodies")) {
        printf("Failed to load melodies\n");
        melody_library_destroy(library);
        return 1;
    }

    // Print summary
    melody_library_print_summary(library);

    // Test note conversion
    printf("\n=== Note Conversion Test ===\n");
    const char* test_notes[] = {"A4", "C#3", "G5", "Bm4", "F#2"};
    for (int i = 0; i < 5; i++) {
        float freq = note_to_frequency(test_notes[i], 440.0f);
        printf("Note %s -> %.2f Hz\n", test_notes[i], freq);
    }

    // Test random melody selection
    printf("\n=== Random Melody Test ===\n");
    const char* categories[] = {"Theme", "Ambient", "Exploration", "Combat", "Home"};

    for (int i = 0; i < 5; i++) {
        Melody* melody = melody_library_get_random_melody(library, categories[i]);
        if (melody) {
            printf("Category '%s': %s (%s) - %d notes\n",
                   categories[i], melody->name, melody->description, melody->note_count);

            // Print first few notes
            printf("  Notes: ");
            for (int j = 0; j < melody->note_count && j < 5; j++) {
                printf("%s ", melody->notes[j]);
            }
            if (melody->note_count > 5) printf("...");
            printf("\n");
        } else {
            printf("Category '%s': No melody found\n", categories[i]);
        }
    }

    // Test specific melody lookup
    printf("\n=== Specific Melody Test ===\n");
    Melody* specific = melody_library_get_melody_by_name(library, "Theme", "Main");
    if (specific) {
        printf("Found Theme/Main: %s (%s) - %d notes\n",
               specific->name, specific->description, specific->note_count);
    } else {
        printf("Theme/Main not found\n");
    }

    // Cleanup
    melody_library_destroy(library);
    printf("\nMelody system test completed successfully!\n");
    return 0;
}
