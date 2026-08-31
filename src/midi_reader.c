#include "midi_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <jansson.h>

// Global error state
static MIDIError last_error = MIDI_ERROR_NONE;

// Helper function to read big-endian values
static uint16_t read_be16(FILE* file) {
    uint16_t value;
    fread(&value, sizeof(uint16_t), 1, file);
    return (value << 8) | (value >> 8); // Swap bytes
}

static uint32_t read_be32(FILE* file) {
    uint32_t value;
    fread(&value, sizeof(uint32_t), 1, file);
    return ((value & 0xFF) << 24) |
           (((value >> 8) & 0xFF) << 16) |
           (((value >> 16) & 0xFF) << 8) |
           ((value >> 24) & 0xFF); // Swap bytes
}

MIDIFile* midi_reader_create(void) {
    MIDIFile* midi = malloc(sizeof(MIDIFile));
    if (!midi) {
        last_error = MIDI_ERROR_MEMORY_ALLOCATION;
        return NULL;
    }

    memset(midi, 0, sizeof(MIDIFile));
    midi->tracks = NULL;
    midi->num_tracks = 0;
    midi->total_ticks = 0;
    midi->tempo = 500000; // Default tempo (120 BPM)
    last_error = MIDI_ERROR_NONE;

    return midi;
}

void midi_reader_destroy(MIDIFile* midi) {
    if (!midi) return;

    if (midi->tracks) {
        for (uint16_t i = 0; i < midi->num_tracks; i++) {
            if (midi->tracks[i].events) {
                free(midi->tracks[i].events);
            }
        }
        free(midi->tracks);
    }

    free(midi);
}

uint32_t midi_reader_read_variable_length(FILE* file) {
    uint32_t value = 0;
    uint8_t byte;

    do {
        if (fread(&byte, 1, 1, file) != 1) {
            last_error = MIDI_ERROR_CORRUPTED_DATA;
            return 0;
        }
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);

    return value;
}

bool midi_reader_parse_header(MIDIFile* midi, FILE* file) {
    // Read header magic
    midi->header.magic = read_be32(file);
    if (midi->header.magic != MIDI_HEADER_MAGIC) {
        last_error = MIDI_ERROR_INVALID_HEADER;
        return false;
    }

    // Read header length
    midi->header.header_length = read_be32(file);
    if (midi->header.header_length != 6) {
        last_error = MIDI_ERROR_INVALID_HEADER;
        return false;
    }

    // Read format and track count
    midi->header.format = read_be16(file);
    midi->header.num_tracks = read_be16(file);
    midi->header.division = read_be16(file);

    // Validate format
    if (midi->header.format > 2) {
        last_error = MIDI_ERROR_UNSUPPORTED_FORMAT;
        return false;
    }

    // Allocate tracks array
    midi->tracks = malloc(sizeof(MIDITrack) * midi->header.num_tracks);
    if (!midi->tracks) {
        last_error = MIDI_ERROR_MEMORY_ALLOCATION;
        return false;
    }

    midi->num_tracks = midi->header.num_tracks;
    memset(midi->tracks, 0, sizeof(MIDITrack) * midi->num_tracks);

    return true;
}

bool midi_reader_parse_event(MIDIFile* midi, MIDITrack* track, FILE* file, uint32_t* current_time) {
    // Read delta time
    uint32_t delta_time = midi_reader_read_variable_length(file);
    *current_time += delta_time;

    // Read status byte
    uint8_t status_byte;
    if (fread(&status_byte, 1, 1, file) != 1) {
        last_error = MIDI_ERROR_CORRUPTED_DATA;
        return false;
    }

    // Check if this is a meta event
    if (status_byte == MIDI_META) {
        uint8_t meta_type;
        if (fread(&meta_type, 1, 1, file) != 1) {
            last_error = MIDI_ERROR_CORRUPTED_DATA;
            return false;
        }

        uint32_t meta_length = midi_reader_read_variable_length(file);

        // Handle tempo meta event
        if (meta_type == MIDI_META_TEMPO && meta_length == 3) {
            uint8_t tempo_bytes[3];
            if (fread(tempo_bytes, 1, 3, file) == 3) {
                midi->tempo = (tempo_bytes[0] << 16) | (tempo_bytes[1] << 8) | tempo_bytes[2];
            }
        }
        // Handle time signature meta event
        else if (meta_type == MIDI_META_TIME_SIGNATURE && meta_length == 4) {
            uint8_t time_sig[4];
            fread(time_sig, 1, 4, file);
            // Could store time signature info here
        }
        // Handle track name meta event
        else if (meta_type == MIDI_META_TRACK_NAME && meta_length > 0 && meta_length < 64) {
            char track_name[64];
            fread(track_name, 1, meta_length, file);
            track_name[meta_length] = '\0';
            strncpy(track->name, track_name, sizeof(track->name) - 1);
            track->name[sizeof(track->name) - 1] = '\0';
        }
        // Skip other meta events
        else {
            for (uint32_t i = 0; i < meta_length; i++) {
                uint8_t dummy;
                fread(&dummy, 1, 1, file);
            }
        }
        return true;
    }

    // Handle regular MIDI events
    uint8_t data1, data2 = 0;
    if (fread(&data1, 1, 1, file) != 1) {
        last_error = MIDI_ERROR_CORRUPTED_DATA;
        return false;
    }

    // Read second data byte for events that need it
    if ((status_byte & 0xF0) != MIDI_PROGRAM_CHANGE &&
        (status_byte & 0xF0) != MIDI_CHANNEL_PRESSURE) {
        if (fread(&data2, 1, 1, file) != 1) {
            last_error = MIDI_ERROR_CORRUPTED_DATA;
            return false;
        }
    }

    // Add event to track
    if (track->event_count >= track->event_capacity) {
        track->event_capacity = track->event_capacity == 0 ? 100 : track->event_capacity * 2;
        MIDIEvent* new_events = realloc(track->events, sizeof(MIDIEvent) * track->event_capacity);
        if (!new_events) {
            last_error = MIDI_ERROR_MEMORY_ALLOCATION;
            return false;
        }
        track->events = new_events;
    }

    MIDIEvent* event = &track->events[track->event_count];
    event->delta_time = delta_time;
    event->status = status_byte;
    event->data1 = data1;
    event->data2 = data2;
    event->absolute_time = *current_time;

    track->event_count++;

    // Update total ticks
    if (*current_time > midi->total_ticks) {
        midi->total_ticks = *current_time;
    }

    return true;
}

bool midi_reader_parse_track(MIDIFile* midi, FILE* file, uint16_t track_index) {
    if (track_index >= midi->num_tracks) {
        last_error = MIDI_ERROR_INVALID_TRACK;
        return false;
    }

    MIDITrack* track = &midi->tracks[track_index];
    MIDITrackHeader header;

    // Debug: print file position and next 8 bytes
    long pos = ftell(file);
    printf("DEBUG: Track %d, file position: %ld\n", track_index, pos);
    unsigned char debug_bytes[8];
    size_t debug_read = fread(debug_bytes, 1, 8, file);
    printf("DEBUG: Next 8 bytes: ");
    for (size_t i = 0; i < debug_read; ++i) {
        printf("%02X ", debug_bytes[i]);
    }
    printf("\n");
    fseek(file, pos, SEEK_SET);

    // Read track header
    header.magic = read_be32(file);
    if (header.magic != MIDI_TRACK_MAGIC) {
        last_error = MIDI_ERROR_INVALID_TRACK;
        return false;
    }

    header.track_length = read_be32(file);
    track->track_length = header.track_length;

    // Initialize track
    track->event_count = 0;
    track->event_capacity = 0;
    track->events = NULL;
    strcpy(track->name, "Track");

    // Parse track events
    uint32_t current_time = 0;
    long track_start = ftell(file);
    long track_end = track_start + header.track_length;

    while (ftell(file) < track_end) {
        if (!midi_reader_parse_event(midi, track, file, &current_time)) {
            return false;
        }
    }

    // Ensure file pointer is at the end of the track
    fseek(file, track_end, SEEK_SET);

    return true;
}

bool midi_reader_load_file(MIDIFile* midi, const char* filename) {
    if (!midi || !filename) {
        last_error = MIDI_ERROR_FILE_NOT_FOUND;
        return false;
    }

    FILE* file = fopen(filename, "rb");
    if (!file) {
        last_error = MIDI_ERROR_FILE_NOT_FOUND;
        return false;
    }

    strncpy(midi->filename, filename, sizeof(midi->filename) - 1);
    midi->filename[sizeof(midi->filename) - 1] = '\0';

    // Parse header
    if (!midi_reader_parse_header(midi, file)) {
        fclose(file);
        return false;
    }

    // Parse tracks
    for (uint16_t i = 0; i < midi->num_tracks; i++) {
        if (!midi_reader_parse_track(midi, file, i)) {
            fclose(file);
            return false;
        }
    }

    fclose(file);
    last_error = MIDI_ERROR_NONE;
    return true;
}

Song* midi_reader_convert_to_song(MIDIFile* midi) {
    if (!midi) return NULL;

    // Calculate BPM from tempo
    uint32_t bpm = 60000000 / midi->tempo; // Convert microseconds per quarter note to BPM

    // Create song
    Song* song = songwriter_create_song("Converted MIDI", "MIDI Import", bpm);
    if (!song) return NULL;

    // Set ticks per beat from MIDI division
    song->ticks_per_beat = midi->header.division;
    song->total_ticks = midi->total_ticks;

    // Convert each track
    for (uint16_t i = 0; i < midi->num_tracks; i++) {
        MIDITrack* midi_track = &midi->tracks[i];

        // Skip empty tracks
        if (midi_track->event_count == 0) continue;

        // Create song track
        Track* song_track = songwriter_create_track(song, midi_track->name, i);
        if (!song_track) continue;

        // Convert events to notes
        for (uint32_t j = 0; j < midi_track->event_count; j++) {
            MIDIEvent* event = &midi_track->events[j];

            if ((event->status & 0xF0) == MIDI_NOTE_ON && event->data2 > 0) {
                // Note on event
                uint8_t note = event->data1;
                uint8_t velocity = event->data2;
                uint32_t start_time = event->absolute_time;

                // Find corresponding note off event
                uint32_t duration = 0;
                for (uint32_t k = j + 1; k < midi_track->event_count; k++) {
                    MIDIEvent* off_event = &midi_track->events[k];
                    if ((off_event->status & 0xF0) == MIDI_NOTE_OFF &&
                        (off_event->data1 == note ||
                         ((off_event->status & 0xF0) == MIDI_NOTE_ON && off_event->data2 == 0))) {
                        duration = off_event->absolute_time - start_time;
                        break;
                    }
                }

                // If no note off found, use a default duration
                if (duration == 0) {
                    duration = midi->header.division; // One quarter note
                }

                songwriter_add_note(song_track, note, velocity, start_time, duration);
            }
        }

        songwriter_add_track(song, song_track);
    }

    return song;
}

Track* midi_reader_convert_track_to_song_track(MIDIFile* midi, MIDITrack* midi_track, uint8_t channel) {
    if (!midi || !midi_track) return NULL;

    Track* song_track = songwriter_create_track(NULL, midi_track->name, channel);
    if (!song_track) return NULL;

    // Convert events to notes (same logic as in convert_to_song)
    for (uint32_t j = 0; j < midi_track->event_count; j++) {
        MIDIEvent* event = &midi_track->events[j];

        if ((event->status & 0xF0) == MIDI_NOTE_ON && event->data2 > 0) {
            uint8_t note = event->data1;
            uint8_t velocity = event->data2;
            uint32_t start_time = event->absolute_time;

            uint32_t duration = 0;
            for (uint32_t k = j + 1; k < midi_track->event_count; k++) {
                MIDIEvent* off_event = &midi_track->events[k];
                if ((off_event->status & 0xF0) == MIDI_NOTE_OFF &&
                    (off_event->data1 == note ||
                     ((off_event->status & 0xF0) == MIDI_NOTE_ON && off_event->data2 == 0))) {
                    duration = off_event->absolute_time - start_time;
                    break;
                }
            }

            if (duration == 0) {
                duration = midi->header.division;
            }

            songwriter_add_note(song_track, note, velocity, start_time, duration);
        }
    }

    return song_track;
}

uint32_t midi_reader_get_tempo(MIDIFile* midi) {
    if (!midi) return 120;
    return 60000000 / midi->tempo;
}

void midi_reader_print_info(MIDIFile* midi) {
    if (!midi) return;

    printf("MIDI File: %s\n", midi->filename);
    printf("Format: %d\n", midi->header.format);
    printf("Tracks: %d\n", midi->header.num_tracks);
    printf("Division: %d ticks per quarter note\n", midi->header.division);
    printf("Tempo: %d BPM\n", midi_reader_get_tempo(midi));
    printf("Total ticks: %d\n", midi->total_ticks);
    printf("Duration: %.2f seconds\n",
           (float)midi->total_ticks / midi->header.division * 60.0f / midi_reader_get_tempo(midi));

    for (uint16_t i = 0; i < midi->num_tracks; i++) {
        printf("Track %d: %s (%d events)\n",
               i, midi->tracks[i].name, midi->tracks[i].event_count);
    }
}

bool midi_reader_is_valid_file(const char* filename) {
    if (!filename) return false;

    FILE* file = fopen(filename, "rb");
    if (!file) return false;

    uint32_t magic;
    bool valid = (fread(&magic, sizeof(uint32_t), 1, file) == 1) &&
                 (magic == MIDI_HEADER_MAGIC ||
                  ((magic & 0xFF) == 'M' && ((magic >> 8) & 0xFF) == 'T' &&
                   ((magic >> 16) & 0xFF) == 'h' && ((magic >> 24) & 0xFF) == 'd'));

    fclose(file);
    return valid;
}

MIDIError midi_reader_get_last_error(void) {
    return last_error;
}

const char* midi_reader_get_error_string(MIDIError error) {
    switch (error) {
        case MIDI_ERROR_NONE: return "No error";
        case MIDI_ERROR_FILE_NOT_FOUND: return "File not found";
        case MIDI_ERROR_INVALID_HEADER: return "Invalid MIDI header";
        case MIDI_ERROR_INVALID_TRACK: return "Invalid MIDI track";
        case MIDI_ERROR_UNSUPPORTED_FORMAT: return "Unsupported MIDI format";
        case MIDI_ERROR_MEMORY_ALLOCATION: return "Memory allocation failed";
        case MIDI_ERROR_CORRUPTED_DATA: return "Corrupted MIDI data";
        default: return "Unknown error";
    }
}

bool midi_reader_export_song_to_json(const Song* song, const char* filename, const char* melody_name, const char* description) {
    if (!song || !filename || !melody_name) {
        printf("DEBUG: Export failed - null parameters\n");
        return false;
    }
    if (song->track_count == 0) {
        printf("DEBUG: Export failed - no tracks in song\n");
        return false;
    }

    printf("DEBUG: Attempting to export to file: %s\n", filename);
    FILE* f = fopen(filename, "w");
    if (!f) {
        printf("DEBUG: Export failed - could not open file for writing\n");
        return false;
    }

    // Use jansson to build the JSON
    json_t* root = json_object();
    json_object_set_new(root, "name", json_string("Theme"));
    json_object_set_new(root, "description", json_string(description ? description : "Converted from MIDI"));
    json_t* melodies = json_array();
    json_t* melody = json_object();
    json_object_set_new(melody, "name", json_string(melody_name));
    json_object_set_new(melody, "description", json_string(description ? description : "Converted from MIDI"));
    json_t* notes = json_array();

    // Export only the first track as the melody
    const Track* track = &song->tracks[0];
    printf("DEBUG: Exporting track with %d events\n", track->event_count);
    for (uint32_t i = 0; i < track->event_count; ++i) {
        const NoteEvent* event = &track->events[i];
        if (event->is_note_on) {
            // Convert MIDI note number to note name (e.g., C4)
            char note_name[8];
            int note_num = event->note;
            static const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            int octave = (note_num / 12) - 1;
            int note_index = note_num % 12;
            snprintf(note_name, sizeof(note_name), "%s%d", note_names[note_index], octave);
            json_array_append_new(notes, json_string(note_name));
            printf("DEBUG: Added note %s (MIDI %d)\n", note_name, note_num);
        }
    }

    printf("DEBUG: Total notes exported: %zu\n", json_array_size(notes));
    json_object_set_new(melody, "notes", notes);
    json_array_append_new(melodies, melody);
    json_object_set_new(root, "melodies", melodies);

    // Write JSON to file
    int ret = json_dumpf(root, f, JSON_INDENT(2));
    json_decref(root);
    fclose(f);

    if (ret != 0) {
        printf("DEBUG: Export failed - json_dumpf returned %d\n", ret);
        return false;
    }

    printf("DEBUG: Export successful\n");
    return true;
}
