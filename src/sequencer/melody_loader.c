#include "melody_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <jansson.h>
#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include "platform_android.h"
#endif

// Note frequency mapping (A4 = 440Hz by default)
static const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const float note_frequencies[] = {261.63f, 277.18f, 293.66f, 311.13f, 329.63f, 349.23f,
                                        369.99f, 392.00f, 415.30f, 440.00f, 466.16f, 493.88f};

// Beat pattern templates
static const int BEAT_PATTERN_4_4[] = {1, 0, 1, 0, 1, 0, 1, 0}; // Standard 4/4
static const int BEAT_PATTERN_3_4[] = {1, 0, 1, 0, 1, 0}; // Waltz
static const int BEAT_PATTERN_6_8[] = {1, 0, 0, 1, 0, 0}; // 6/8 time
static const int BEAT_PATTERN_SYNCOPATED[] = {1, 0, 0, 1, 0, 1, 0, 0}; // Syncopated
static const int BEAT_PATTERN_JAZZ[] = {1, 0, 0, 1, 0, 0, 1, 0}; // Jazz swing

MelodyLibrary* melody_library_create(void) {
    MelodyLibrary* library = malloc(sizeof(MelodyLibrary));
    if (!library) return NULL;

    library->capacity = 10;
    library->file_count = 0;
    library->files = malloc(sizeof(MelodyFile) * library->capacity);

    if (!library->files) {
        free(library);
        return NULL;
    }

    return library;
}

void melody_library_destroy(MelodyLibrary* library) {
    if (!library) return;

    if (library->files) {
        free(library->files);
    }
    free(library);
}

// Process advanced note from string format (e.g., "C4:0.5:0.8:s:1.0:0.5:1")
void process_advanced_note(AdvancedNote* note, const char* note_string) {
    if (!note || !note_string) return;

    // Initialize with defaults
    strcpy(note->note, "");
    note->duration = 0.25f; // Quarter note default
    note->velocity = 0.8f;   // Default velocity
    note->syncopated = false;
    note->sustained = false;
    note->sustain_level = 0.7f;
    note->sustain_time = 0.5f;
    note->accent = 0;

    char* str = strdup(note_string);
    char* token = strtok(str, ":");
    int token_count = 0;

    while (token && token_count < 7) {
        switch (token_count) {
            case 0: // Note name
                strncpy(note->note, token, MAX_NOTE_LENGTH - 1);
                note->note[MAX_NOTE_LENGTH - 1] = '\0';
                break;
            case 1: // Duration
                note->duration = atof(token);
                break;
            case 2: // Velocity
                note->velocity = atof(token);
                break;
            case 3: // Syncopation flag
                note->syncopated = (strcmp(token, "s") == 0);
                break;
            case 4: // Sustain level
                note->sustain_level = atof(token);
                break;
            case 5: // Sustain time
                note->sustain_time = atof(token);
                break;
            case 6: // Accent
                note->accent = atoi(token);
                break;
        }
        token = strtok(NULL, ":");
        token_count++;
    }

    free(str);
}

// Calculate syncopation offset based on beat position
float calculate_syncopation_offset(float beat_position, float time_signature) {
    // Syncopation moves notes slightly off the beat
    float beat_in_measure = fmodf(beat_position, time_signature);
    float syncopation_strength = 0.125f; // 1/8 note offset

    // Apply syncopation to off-beat positions
    if (beat_in_measure > 0.5f && beat_in_measure < time_signature - 0.5f) {
        return syncopation_strength;
    }

    return 0.0f;
}

// Calculate sustain envelope
float calculate_sustain_envelope(float time, float sustain_level, float sustain_time) {
    if (time <= 0.0f) return 1.0f;
    if (time >= sustain_time) return sustain_level;

    // Smooth decay to sustain level
    float decay_rate = (1.0f - sustain_level) / sustain_time;
    return 1.0f - (decay_rate * time);
}

// Generate different beat patterns
void generate_beat_pattern(Melody* melody, int pattern_type) {
    if (!melody) return;

    switch (pattern_type) {
        case 0: // Standard 4/4
            melody->beat_pattern_length = 8;
            memcpy(melody->beat_pattern, BEAT_PATTERN_4_4, sizeof(BEAT_PATTERN_4_4));
            break;
        case 1: // 3/4 Waltz
            melody->beat_pattern_length = 6;
            memcpy(melody->beat_pattern, BEAT_PATTERN_3_4, sizeof(BEAT_PATTERN_3_4));
            break;
        case 2: // 6/8
            melody->beat_pattern_length = 6;
            memcpy(melody->beat_pattern, BEAT_PATTERN_6_8, sizeof(BEAT_PATTERN_6_8));
            break;
        case 3: // Syncopated
            melody->beat_pattern_length = 8;
            memcpy(melody->beat_pattern, BEAT_PATTERN_SYNCOPATED, sizeof(BEAT_PATTERN_SYNCOPATED));
            break;
        case 4: // Jazz
            melody->beat_pattern_length = 8;
            memcpy(melody->beat_pattern, BEAT_PATTERN_JAZZ, sizeof(BEAT_PATTERN_JAZZ));
            break;
        default:
            melody->beat_pattern_length = 8;
            memcpy(melody->beat_pattern, BEAT_PATTERN_4_4, sizeof(BEAT_PATTERN_4_4));
            break;
    }
}

// Apply syncopation to melody
void apply_syncopation_to_melody(Melody* melody) {
    if (!melody || !melody->syncopation_enabled) return;

    for (int i = 0; i < melody->note_count; i++) {
        float beat_position = (float)i;
        float offset = calculate_syncopation_offset(beat_position, melody->time_signature_numerator);

        if (offset > 0.0f) {
            melody->advanced_notes[i].syncopated = true;
            melody->advanced_notes[i].duration += offset;
        }
    }
}

// Apply sustain to melody
void apply_sustain_to_melody(Melody* melody) {
    if (!melody || !melody->sustain_enabled) return;

    for (int i = 0; i < melody->note_count; i++) {
        // Apply sustain to longer notes
        if (melody->advanced_notes[i].duration >= 0.5f) {
            melody->advanced_notes[i].sustained = true;
            melody->advanced_notes[i].sustain_level = 0.6f;
            melody->advanced_notes[i].sustain_time = melody->advanced_notes[i].duration * 0.8f;
        }
    }
}

float note_to_frequency(const char* note, float tuning_scale) {
    if (!note || strlen(note) < 2) return 0.0f;

    // Parse note components (e.g., "A4", "C#3", "Bm4")
    char note_name[4] = {0};
    int octave = 4; // Default octave
    bool is_minor = false;

    int i = 0;
    int note_idx = 0;

    // Extract note name
    while (note[i] && note[i] != 'm' && note[i] != '0' && note[i] != '1' &&
           note[i] != '2' && note[i] != '3' && note[i] != '4' && note[i] != '5' &&
           note[i] != '6' && note[i] != '7' && note[i] != '8' && note[i] != '9') {
        note_name[note_idx++] = note[i];
        i++;
    }

    // Check for minor indicator
    if (note[i] == 'm') {
        is_minor = true;
        i++;
    }

    // Extract octave
    if (note[i] >= '0' && note[i] <= '9') {
        octave = note[i] - '0';
    }

    // Find note index
    int note_index = -1;
    for (int j = 0; j < 12; j++) {
        if (strcmp(note_name, note_names[j]) == 0) {
            note_index = j;
            break;
        }
    }

    if (note_index == -1) return 0.0f;

    // Calculate frequency
    float base_freq = note_frequencies[note_index];
    float octave_multiplier = powf(2.0f, (float)(octave - 4));
    float tuning_multiplier = tuning_scale == 432.0f ? 432.0f / 440.0f : 1.0f;

    return base_freq * octave_multiplier * tuning_multiplier;
}

bool is_valid_note(const char* note) {
    if (!note || strlen(note) < 2) return false;

    // Check if it's a valid note format (e.g., "A4", "C#3", "Bm4")
    char note_name[4] = {0};
    int i = 0;

    while (note[i] && note[i] != 'm' && note[i] != '0' && note[i] != '1' &&
           note[i] != '2' && note[i] != '3' && note[i] != '4' && note[i] != '5' &&
           note[i] != '6' && note[i] != '7' && note[i] != '8' && note[i] != '9') {
        if (i >= 3) return false; // Note name too long
        note_name[i] = note[i];
        i++;
    }

    // Check if note name is valid
    for (int j = 0; j < 12; j++) {
        if (strcmp(note_name, note_names[j]) == 0) {
            return true;
        }
    }

    return false;
}

bool melody_library_load_from_directory(MelodyLibrary* library, const char* directory_path) {
    if (!library || !directory_path) return false;

    int loaded_files = 0;

#ifdef __ANDROID__
    // Try to enumerate assets from APK using AAssetManager
    AAssetManager *am = android_get_asset_manager();
    if (am) {
        const char *rel_dir = directory_path;
        if (strncmp(directory_path, "assets/", 7) == 0) {
            rel_dir = directory_path + 7; // strip leading assets/
        }
        AAssetDir *dir = AAssetManager_openDir(am, rel_dir);
        if (dir) {
            const char *name = NULL;
            while ((name = AAssetDir_getNextFileName(dir)) != NULL && loaded_files < library->capacity) {
                const char *ext = strrchr(name, '.');
                if (!ext || strcmp(ext, ".json") != 0) continue;

                char asset_path[512];
                snprintf(asset_path, sizeof(asset_path), "%s/%s", rel_dir, name);
                AAsset *asset = AAssetManager_open(am, asset_path, AASSET_MODE_BUFFER);
                if (!asset) continue;
                off_t len = AAsset_getLength(asset);
                if (len <= 0) { AAsset_close(asset); continue; }
                const void *buf = AAsset_getBuffer(asset);
                if (!buf) { AAsset_close(asset); continue; }

                json_error_t error;
                json_t *root = json_loadb((const char*)buf, (size_t)len, 0, &error);
                AAsset_close(asset);
                if (!root) {
                    printf("Failed to load JSON asset %s: %s\n", asset_path, error.text);
                    continue;
                }

                MelodyFile* file = &library->files[loaded_files];
                json_t* name_obj = json_object_get(root, "name");
                json_t* desc_obj = json_object_get(root, "description");
                if (name_obj && json_is_string(name_obj)) {
                    strncpy(file->name, json_string_value(name_obj), sizeof(file->name) - 1);
                } else {
                    strncpy(file->name, name, sizeof(file->name) - 1);
                }
                if (desc_obj && json_is_string(desc_obj)) {
                    strncpy(file->description, json_string_value(desc_obj), sizeof(file->description) - 1);
                } else {
                    file->description[0] = '\0';
                }

                json_t* melodies_array = json_object_get(root, "melodies");
                if (melodies_array && json_is_array(melodies_array)) {
                    size_t melody_count = json_array_size(melodies_array);
                    file->melody_count = 0;
                    for (size_t i = 0; i < melody_count && i < MAX_MELODIES_PER_FILE; i++) {
                        json_t* melody_obj = json_array_get(melodies_array, i);
                        if (!json_is_object(melody_obj)) continue;
                        Melody* melody = &file->melodies[file->melody_count];
                        json_t* melody_name = json_object_get(melody_obj, "name");
                        json_t* melody_desc = json_object_get(melody_obj, "description");
                        if (melody_name && json_is_string(melody_name)) {
                            strncpy(melody->name, json_string_value(melody_name), sizeof(melody->name) - 1);
                        } else {
                            snprintf(melody->name, sizeof(melody->name), "Melody_%zu", i);
                        }
                        if (melody_desc && json_is_string(melody_desc)) {
                            strncpy(melody->description, json_string_value(melody_desc), sizeof(melody->description) - 1);
                        } else {
                            melody->description[0] = '\0';
                        }
                        json_t* notes_array = json_object_get(melody_obj, "notes");
                        if (notes_array && json_is_array(notes_array)) {
                            size_t note_count = json_array_size(notes_array);
                            melody->note_count = 0;
                            for (size_t j = 0; j < note_count && j < MAX_NOTES_PER_MELODY; j++) {
                                json_t* note_obj = json_array_get(notes_array, j);
                                if (!json_is_string(note_obj)) continue;
                                const char* note_str = json_string_value(note_obj);
                                if (is_valid_note(note_str)) {
                                    strncpy(melody->notes[melody->note_count], note_str, MAX_NOTE_LENGTH - 1);
                                    melody->note_count++;
                                }
                            }
                        }
                        file->melody_count++;
                    }
                }
                json_decref(root);
                loaded_files++;
            }
            AAssetDir_close(dir);
            library->file_count = loaded_files;
            printf("Loaded %d melody files (Android assets)\n", loaded_files);
            return loaded_files > 0;
        }
    }
#endif

    // Fallback: standard filesystem directory
    DIR* dir = opendir(directory_path);
    if (!dir) {
        printf("Failed to open directory: %s\n", directory_path);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && loaded_files < library->capacity) {
        char* ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".json") != 0) continue;
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);
        json_error_t error;
        json_t* root = json_load_file(file_path, 0, &error);
        if (!root) {
            printf("Failed to load JSON file %s: %s\n", file_path, error.text);
            continue;
        }
        MelodyFile* file = &library->files[loaded_files];
        json_t* name_obj = json_object_get(root, "name");
        json_t* desc_obj = json_object_get(root, "description");
        if (name_obj && json_is_string(name_obj)) {
            strncpy(file->name, json_string_value(name_obj), sizeof(file->name) - 1);
        } else {
            strncpy(file->name, entry->d_name, sizeof(file->name) - 1);
        }
        if (desc_obj && json_is_string(desc_obj)) {
            strncpy(file->description, json_string_value(desc_obj), sizeof(file->description) - 1);
        } else {
            file->description[0] = '\0';
        }
        json_t* melodies_array = json_object_get(root, "melodies");
        if (melodies_array && json_is_array(melodies_array)) {
            size_t melody_count = json_array_size(melodies_array);
            file->melody_count = 0;
            for (size_t i = 0; i < melody_count && i < MAX_MELODIES_PER_FILE; i++) {
                json_t* melody_obj = json_array_get(melodies_array, i);
                if (!json_is_object(melody_obj)) continue;
                Melody* melody = &file->melodies[file->melody_count];
                json_t* melody_name = json_object_get(melody_obj, "name");
                json_t* melody_desc = json_object_get(melody_obj, "description");
                if (melody_name && json_is_string(melody_name)) {
                    strncpy(melody->name, json_string_value(melody_name), sizeof(melody->name) - 1);
                } else {
                    snprintf(melody->name, sizeof(melody->name), "Melody_%zu", i);
                }
                if (melody_desc && json_is_string(melody_desc)) {
                    strncpy(melody->description, json_string_value(melody_desc), sizeof(melody->description) - 1);
                } else {
                    melody->description[0] = '\0';
                }
                json_t* notes_array = json_object_get(melody_obj, "notes");
                if (notes_array && json_is_array(notes_array)) {
                    size_t note_count = json_array_size(notes_array);
                    melody->note_count = 0;
                    for (size_t j = 0; j < note_count && j < MAX_NOTES_PER_MELODY; j++) {
                        json_t* note_obj = json_array_get(notes_array, j);
                        if (!json_is_string(note_obj)) continue;
                        const char* note_str = json_string_value(note_obj);
                        if (is_valid_note(note_str)) {
                            strncpy(melody->notes[melody->note_count], note_str, MAX_NOTE_LENGTH - 1);
                            melody->note_count++;
                        }
                    }
                }
                file->melody_count++;
            }
        }
        json_decref(root);
        loaded_files++;
    }
    closedir(dir);
    library->file_count = loaded_files;
    printf("Loaded %d melody files\n", loaded_files);
    return loaded_files > 0;
}

Melody* melody_library_get_random_melody(MelodyLibrary* library, const char* category) {
    if (!library || library->file_count == 0) return NULL;

    // If category is specified, find matching file
    if (category) {
        for (int i = 0; i < library->file_count; i++) {
            if (strcasecmp(library->files[i].name, category) == 0) {
                if (library->files[i].melody_count > 0) {
                    int random_idx = rand() % library->files[i].melody_count;
                    return &library->files[i].melodies[random_idx];
                }
            }
        }
    }

    // Fallback to random file and melody
    int file_idx = rand() % library->file_count;
    MelodyFile* file = &library->files[file_idx];

    if (file->melody_count > 0) {
        int melody_idx = rand() % file->melody_count;
        return &file->melodies[melody_idx];
    }

    return NULL;
}

Melody* melody_library_get_melody_by_name(MelodyLibrary* library, const char* file_name, const char* melody_name) {
    if (!library || !file_name || !melody_name) return NULL;

    for (int i = 0; i < library->file_count; i++) {
        if (strcasecmp(library->files[i].name, file_name) == 0) {
            for (int j = 0; j < library->files[i].melody_count; j++) {
                if (strcasecmp(library->files[i].melodies[j].name, melody_name) == 0) {
                    return &library->files[i].melodies[j];
                }
            }
        }
    }

    return NULL;
}

void melody_library_print_summary(MelodyLibrary* library) {
    if (!library) return;

    printf("Melody Library Summary:\n");
    printf("Files loaded: %d\n", library->file_count);

    for (int i = 0; i < library->file_count; i++) {
        MelodyFile* file = &library->files[i];
        printf("  %s (%s): %d melodies\n", file->name, file->description, file->melody_count);

        for (int j = 0; j < file->melody_count; j++) {
            Melody* melody = &file->melodies[j];
            printf("    - %s (%s): %d notes\n", melody->name, melody->description, melody->note_count);
        }
    }
}
