#ifndef MELODY_LOADER_H
#define MELODY_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_MELODIES_PER_FILE 10
#define MAX_NOTES_PER_MELODY 32
#define MAX_NOTE_LENGTH 8

// Advanced note structure with timing and articulation
typedef struct {
    char note[MAX_NOTE_LENGTH];
    float duration;        // Duration in beats (0.25 = quarter note, 0.5 = half note, etc.)
    float velocity;        // Note velocity/volume (0.0 to 1.0)
    bool syncopated;       // Whether this note is syncopated (off-beat)
    bool sustained;        // Whether this note should be sustained
    float sustain_level;   // Sustain level (0.0 to 1.0)
    float sustain_time;    // How long to sustain (in beats)
    int accent;           // Accent level (0 = normal, 1 = light accent, 2 = heavy accent)
} AdvancedNote;

// Enhanced melody structure
typedef struct {
    char name[64];
    char description[256];
    char notes[MAX_NOTES_PER_MELODY][MAX_NOTE_LENGTH];
    int note_count;

    // Advanced features
    AdvancedNote advanced_notes[MAX_NOTES_PER_MELODY];
    bool use_advanced_notes;
    float tempo;           // Beats per minute
    float time_signature_numerator;   // Time signature (e.g., 4/4 = 4)
    float time_signature_denominator; // Time signature (e.g., 4/4 = 4)
    bool syncopation_enabled;
    bool sustain_enabled;
    int beat_pattern[MAX_NOTES_PER_MELODY]; // Alternative beat sequence
    int beat_pattern_length;
} Melody;

typedef struct {
    char name[64];
    char description[256];
    Melody melodies[MAX_MELODIES_PER_FILE];
    int melody_count;
} MelodyFile;

typedef struct {
    MelodyFile* files;
    int file_count;
    int capacity;
} MelodyLibrary;

// Melody library management
MelodyLibrary* melody_library_create(void);
void melody_library_destroy(MelodyLibrary* library);
bool melody_library_load_from_directory(MelodyLibrary* library, const char* directory_path);

// Melody access
Melody* melody_library_get_random_melody(MelodyLibrary* library, const char* category);
Melody* melody_library_get_melody_by_name(MelodyLibrary* library, const char* file_name, const char* melody_name);

// Note parsing and conversion
float note_to_frequency(const char* note, float tuning_scale);
bool is_valid_note(const char* note);

// Advanced note processing
void process_advanced_note(AdvancedNote* note, const char* note_string);
float calculate_syncopation_offset(float beat_position, float time_signature);
float calculate_sustain_envelope(float time, float sustain_level, float sustain_time);

// Beat pattern generation
void generate_beat_pattern(Melody* melody, int pattern_type);
void apply_syncopation_to_melody(Melody* melody);
void apply_sustain_to_melody(Melody* melody);

// Utility functions
void melody_library_print_summary(MelodyLibrary* library);

#endif // MELODY_LOADER_H
